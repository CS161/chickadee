#include "k-chkfs.hh"
#include "k-ahci.hh"
#include "k-chkfsiter.hh"

bufcache bufcache::bc;

bufcache::bufcache() {
}


// bufcache::get_disk_entry(bn, cleaner)
//    Read disk block `bn` into the buffer cache, obtain a reference to it,
//    and return a pointer to its bcentry. The returned bcentry has
//    `buf_ != nullptr` and `state_ >= state_clean`. The function may block.
//
//    If this function reads the disk block from disk, and `cleaner != nullptr`,
//    then `cleaner` is called on the block data.
//
//    Returns `nullptr` if there's no room for the block.

bcentry* bufcache::get_disk_entry(chkfs::blocknum_t bn,
                                  bcentry_clean_function cleaner) {
    assert(chkfs::blocksize == PAGESIZE);
    auto irqs = lock_.lock();

    // look for slot containing `bn`
    size_t i, empty_slot = -1;
    for (i = 0; i != ne; ++i) {
        if (e_[i].empty()) {
            if (empty_slot == size_t(-1)) {
                empty_slot = i;
            }
        } else if (e_[i].bn_ == bn) {
            break;
        }
    }

    // if not found, use free slot
    if (i == ne) {
        if (empty_slot == size_t(-1)) {
            // cache full!
            lock_.unlock(irqs);
            log_printf("bufcache: no room for block %u\n", bn);
            return nullptr;
        }
        i = empty_slot;
    }

    // obtain entry lock
    e_[i].lock_.lock_noirq();

    // mark allocated if empty
    if (e_[i].empty()) {
        e_[i].state_ = bcentry::state_allocated;
        e_[i].bn_ = bn;
    }

    // no longer need cache lock
    lock_.unlock_noirq();

    // mark reference
    ++e_[i].ref_;

    // load block
    bool ok = e_[i].load(irqs, cleaner);

    // unlock and return entry
    if (!ok) {
        --e_[i].ref_;
    }
    e_[i].lock_.unlock(irqs);
    return ok ? &e_[i] : nullptr;
}


// bcentry::load(irqs, cleaner)
//    This function completes the loading process for a block. It requires
//    that `lock_` is locked, that `state_ >= state_allocated`, and that
//    `bn_` is set to the desired block number.

bool bcentry::load(irqstate& irqs, bcentry_clean_function cleaner) {
    bufcache& bc = bufcache::get();

    // load block, or wait for concurrent reader to load it
    while (true) {
        assert(state_ != state_empty);
        if (state_ == state_allocated) {
            if (!buf_) {
                buf_ = reinterpret_cast<unsigned char*>
                    (kalloc(chkfs::blocksize));
                if (!buf_) {
                    return false;
                }
            }
            state_ = state_loading;
            lock_.unlock(irqs);

            sata_disk->read(buf_, chkfs::blocksize,
                            bn_ * chkfs::blocksize);

            irqs = lock_.lock();
            state_ = state_clean;
            if (cleaner) {
                cleaner(this);
            }
            bc.read_wq_.wake_all();
        } else if (state_ == state_loading) {
            waiter(current()).block_until(bc.read_wq_, [&] () {
                    return state_ != state_loading;
                }, lock_, irqs);
        } else {
            return true;
        }
    }
}


// bcentry::put()
//    Release a reference to this buffer cache entry. The caller must
//    not use the entry after this call.

void bcentry::put() {
    spinlock_guard guard(lock_);
    assert(ref_ != 0);
    if (--ref_ == 0) {
        clear();
    }
}


// bcentry::get_write()
//    Obtain a write reference for this entry.

void bcentry::get_write() {
    // Your code here
    assert(false);
}


// bcentry::put_write()
//    Release a write reference for this entry.

void bcentry::put_write() {
    // Your code here
    assert(false);
}


// bufcache::sync(drop)
//    Write all dirty buffers to disk (blocking until complete).
//    If `drop > 0`, then additionally free all buffer cache contents,
//    except referenced blocks. If `drop > 1`, then assert that all inode
//    and data blocks are unreferenced.

int bufcache::sync(int drop) {
    // write dirty buffers to disk
    // Your code here!

    // drop clean buffers if requested
    if (drop > 0) {
        spinlock_guard guard(lock_);
        for (size_t i = 0; i != ne; ++i) {
            spinlock_guard eguard(e_[i].lock_);

            // validity checks: referenced entries aren't empty; if drop > 1,
            // no data blocks are referenced
            assert(e_[i].ref_ == 0 || e_[i].state_ != bcentry::state_empty);
            if (e_[i].ref_ > 0 && drop > 1 && e_[i].bn_ >= 2) {
                error_printf(CPOS(22, 0), COLOR_ERROR, "sync(2): block %u has nonzero reference count\n", e_[i].bn_);
                assert_fail(__FILE__, __LINE__, "e_[i].bn_ < 2");
            }

            // actually drop buffer
            if (e_[i].ref_ == 0) {
                e_[i].clear();
            }
        }
    }

    return 0;
}


// inode lock functions
//    The inode lock protects the inode's size and data references.
//    It is a read/write lock; multiple readers can hold the lock
//    simultaneously.
//    IMPORTANT INVARIANT: If a kernel task has an inode lock, it
//    must also hold a reference to the disk page containing that
//    inode.

namespace chkfs {

void inode::lock_read() {
    uint16_t v = mlock.load(std::memory_order_relaxed);
    while (true) {
        assert(v != uint16_t(-2));   // at most 2^16 - 2 read locks allowed

        if (v == uint16_t(-1)) {
            current()->yield();
            v = mlock.load(std::memory_order_relaxed);
        } else if (mlock.compare_exchange_weak(v, v + 1,
                                               std::memory_order_acquire)) {
            return;
        } else {
            // `compare_exchange_weak` already reloaded `v`
            pause();
        }
    }
}

void inode::unlock_read() {
    uint16_t v = mlock.load(std::memory_order_relaxed);
    assert(v != 0 && v != uint16_t(-1));
    while (!mlock.compare_exchange_weak(v, v - 1,
                                        std::memory_order_release)) {
        pause();
    }
}

void inode::lock_write() {
    uint16_t v = 0;
    while (!mlock.compare_exchange_weak(v, uint16_t(-1),
                                        std::memory_order_acquire)) {
        current()->yield();
        v = 0;
    }
}

void inode::unlock_write() {
    assert(has_write_lock());
    mlock.store(0, std::memory_order_release);
}

bool inode::has_write_lock() const {
    return mlock.load(std::memory_order_relaxed) == uint16_t(-1);
}

}


// chickadeefs state

chkfsstate chkfsstate::fs;

chkfsstate::chkfsstate() {
}


// clean_inode_block(buf)
//    This function is called when loading an inode block into the
//    buffer cache. It clears values that are only used in memory.

static void clean_inode_block(bcentry* entry) {
    uint32_t entry_index = entry - bufcache::get().e_;
    auto is = reinterpret_cast<chkfs::inode*>(entry->buf_);
    for (unsigned i = 0; i != chkfs::inodesperblock; ++i) {
        is[i].mlock = is[i].mref = 0;
        is[i].mbcindex = entry_index;
    }
}


// chkfsstate::get_inode(inum)
//    Return inode number `inum`, or `nullptr` if there's no such inode.
//    Obtains a reference on the buffer cache block containing the inode;
//    you should eventually release this reference by calling `ino->put()`.

chkfs::inode* chkfsstate::get_inode(inum_t inum) {
    auto& bc = bufcache::get();

    auto superblock_entry = bc.get_disk_entry(0);
    assert(superblock_entry);
    auto sb = reinterpret_cast<chkfs::superblock*>
        (&superblock_entry->buf_[chkfs::superblock_offset]);
    auto inode_bn = sb->inode_bn;
    auto ninodes = sb->ninodes;
    superblock_entry->put();

    chkfs::inode* ino = nullptr;
    if (inum > 0 && inum < ninodes) {
        auto bn = inode_bn + inum / chkfs::inodesperblock;
        if (auto inode_entry = bc.get_disk_entry(bn, clean_inode_block)) {
            ino = reinterpret_cast<inode*>(inode_entry->buf_);
        }
    }
    if (ino != nullptr) {
        ino += inum % chkfs::inodesperblock;
    }
    return ino;
}


namespace chkfs {
// chkfs::inode::entry()
//    Return a pointer to the buffer cache entry containing this inode.
//    Requires that the inode is located in the buffer cache.

bcentry* inode::entry() {
    return &bufcache::get().e_[mbcindex];
}

// chkfs::inode::put()
//    Releases the reference to this inode's buffer cache entry.

void inode::put() {
    entry()->put();
}

}


// chkfsstate::lookup_inode(dirino, filename)
//    Look up `filename` in the directory inode `dirino`, returning the
//    corresponding inode (or nullptr if not found). The caller must have
//    a read lock on `dirino`. The returned inode has a reference that
//    the caller should eventually release with `ino->put()`.

chkfs::inode* chkfsstate::lookup_inode(inode* dirino,
                                       const char* filename) {
    auto& bc = bufcache::get();
    chkfs_fileiter it(dirino);

    // read directory to find file inode
    chkfs::inum_t in = 0;
    for (size_t diroff = 0; !in; diroff += blocksize) {
        if (bcentry* e = it.find(diroff).get_disk_entry()) {
            size_t bsz = min(dirino->size - diroff, blocksize);
            auto dirent = reinterpret_cast<chkfs::dirent*>(e->buf_);
            for (unsigned i = 0; i * sizeof(*dirent) < bsz; ++i, ++dirent) {
                if (dirent->inum && strcmp(dirent->name, filename) == 0) {
                    in = dirent->inum;
                    break;
                }
            }
            e->put();
        } else {
            return nullptr;
        }
    }
    return get_inode(in);
}


// chkfsstate::lookup_inode(filename)
//    Look up `filename` in the root directory.

chkfs::inode* chkfsstate::lookup_inode(const char* filename) {
    auto dirino = get_inode(1);
    if (dirino) {
        dirino->lock_read();
        auto ino = fs.lookup_inode(dirino, filename);
        dirino->unlock_read();
        dirino->put();
        return ino;
    } else {
        return nullptr;
    }
}


// chkfsstate::allocate_extent(unsigned count)
//    Allocate and return the number of a fresh extent. The returned
//    extent doesn't need to be initialized (but it should not be in flight
//    to the disk or part of any incomplete journal transaction).
//    Returns the block number of the first block in the extent, or an error
//    code on failure. Errors can be distinguished by
//    `blocknum >= blocknum_t(E_MINERROR)`.

auto chkfsstate::allocate_extent(unsigned count) -> blocknum_t {
    // Your code here
    return E_INVAL;
}
