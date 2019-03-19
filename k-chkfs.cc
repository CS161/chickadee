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
            e_[i].clear();
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
        e_[i].state_ = bcentry::state_allocated;
        e_[i].bn_ = bn;
    }

    // switch lock to entry lock
    e_[i].lock_.lock_noirq();
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
                cleaner(bn_, buf_);
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


// bufcache::find_entry(buf)
//    Return the `bcentry` containing pointer `buf`. Requires that the current
//    kernel task holds a reference to the corresponding entry.

bcentry* bufcache::find_entry(void* buf) {
    if (buf) {
        buf = reinterpret_cast<void*>(
            round_down(reinterpret_cast<uintptr_t>(buf), chkfs::blocksize)
        );
        for (size_t i = 0; i != ne; ++i) {
            if (!e_[i].empty() && e_[i].buf_ == buf) {
                return &e_[i];
            }
        }
        assert(false);
    }
    return nullptr;
}


// bcentry::put()
//    Release a reference to this buffer cache entry. The caller must
//    not use the entry after this call.

void bcentry::put() {
    spinlock_guard guard(lock_);
    if (--ref_ == 0) {
        state_ = bcentry::state_empty;
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
//    Additionally free all buffer cache contents, except referenced
//    blocks, if `drop` is true.

int bufcache::sync(bool drop) {
    // Write dirty buffers to disk: your code here!

    if (drop) {
        spinlock_guard guard(lock_);
        for (size_t i = 0; i != ne; ++i) {
            spinlock_guard eguard(e_[i].lock_);
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
    uint32_t v = mlock.load(std::memory_order_relaxed);
    while (true) {
        if (v == uint32_t(-1)) {
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
    uint32_t v = mlock.load(std::memory_order_relaxed);
    assert(v != 0 && v != uint32_t(-1));
    while (!mlock.compare_exchange_weak(v, v - 1,
                                        std::memory_order_release)) {
        pause();
    }
}

void inode::lock_write() {
    uint32_t v = 0;
    while (!mlock.compare_exchange_weak(v, uint32_t(-1),
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
    return mlock.load(std::memory_order_relaxed) == uint32_t(-1);
}

}


// chickadeefs state

chkfsstate chkfsstate::fs;

chkfsstate::chkfsstate() {
}


// clean_inode_block(buf)
//    This function is called when loading an inode block into the
//    buffer cache. It clears values that are only used in memory.

static void clean_inode_block(chkfs::blocknum_t, unsigned char* buf) {
    auto is = reinterpret_cast<chkfs::inode*>(buf);
    for (unsigned i = 0; i != chkfs::inodesperblock; ++i) {
        is[i].mlock = is[i].mref = 0;
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


// chkfs::inode::put()
//    Release the caller’s reference to this inode, which must be located
//    in the buffer cache.

namespace chkfs {
void inode::put() {
    bufcache::get().find_entry(this)->put();
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

auto chkfsstate::allocate_extent(unsigned) -> blocknum_t {
    // Your code here
    return E_INVAL;
}
