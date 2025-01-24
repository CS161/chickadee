#include "k-chkfs.hh"
#include "k-ahci.hh"
#include "k-chkfsiter.hh"

bufcache bufcache::bc;

bufcache::bufcache() {
}


// bufcache::load(bn, cleaner)
//    Reads disk block `bn` into the buffer cache and returns a reference
//    to that bcslot. The returned slot has `buf_ != nullptr` and
//    `state_ >= bcslot::s_clean`. The function may block.
//
//    If this function reads the disk block from disk, and `cleaner != nullptr`,
//    then `cleaner` is called on the slot to clean the block data.
//
//    Returns a null reference if there's no room for the block.

bcref bufcache::load(chkfs::blocknum_t bn, block_clean_function cleaner) {
    assert(chkfs::blocksize == PAGESIZE);
    auto irqs = lock_.lock();

    // look for slot containing `bn`
    size_t i, empty_slot = -1;
    for (i = 0; i != nslots; ++i) {
        if (slots_[i].empty()) {
            if (empty_slot == size_t(-1)) {
                empty_slot = i;
            }
        } else if (slots_[i].bn_ == bn) {
            break;
        }
    }

    // if not found, use free slot
    if (i == nslots) {
        if (empty_slot == size_t(-1)) {
            // cache full!
            lock_.unlock(irqs);
            log_printf("bufcache: no room for block %u\n", bn);
            return nullptr;
        }
        i = empty_slot;
    }

    // acquire lock on slot
    auto& slot = slots_[i];
    slot.lock_.lock_noirq();

    // mark allocated if empty
    if (slot.empty()) {
        slot.state_ = bcslot::s_allocated;
        slot.bn_ = bn;
    }

    // no longer need cache lock
    lock_.unlock_noirq();

    // add reference
    ++slot.ref_;

    // load block
    bool ok = slot.load(irqs, cleaner);

    // unlock
    if (!ok) {
        // remove reference since load was unsuccessful
        --slot.ref_;
    }
    slot.lock_.unlock(irqs);

    // return reference to slot
    if (ok) {
        return bcref(&slot);
    } else {
        return bcref();
    }
}


// bcslot::load(irqs, cleaner)
//    Completes the loading process for a block. Requires that `lock_` is
//    locked, that `state_ >= s_allocated`, and that `bn_` is set to the
//    desired block number.

bool bcslot::load(irqstate& irqs, block_clean_function cleaner) {
    bufcache& bc = bufcache::get();

    // load block, or wait for concurrent reader to load it
    while (true) {
        assert(state_ != s_empty);
        if (state_ == s_allocated) {
            if (!buf_) {
                buf_ = reinterpret_cast<unsigned char*>
                    (kalloc(chkfs::blocksize));
                if (!buf_) {
                    return false;
                }
            }
            state_ = s_loading;
            lock_.unlock(irqs);

            sata_disk->read(buf_, chkfs::blocksize,
                            bn_ * chkfs::blocksize);

            irqs = lock_.lock();
            state_ = s_clean;
            if (cleaner) {
                cleaner(this);
            }
            bc.read_wq_.notify_all();
        } else if (state_ == s_loading) {
            waiter().wait_until(bc.read_wq_, [&] () {
                    return state_ != s_loading;
                }, lock_, irqs);
        } else {
            return true;
        }
    }
}


// bcslot::decrement_reference_count()
//    Decrements this buffer cache slot’s reference count.
//
//    The handout code *erases* the slot (freeing its buffer) once the
//    reference count reaches zero. This is bad for performance, and you
//    will change this behavior in pset 4 part A.

void bcslot::decrement_reference_count() {
    spinlock_guard guard(lock_);    // needed in case we `clear()`
    assert(ref_ != 0);
    if (--ref_ == 0) {
        clear();
    }
}


// bcslot::lock_buffer()
//    Acquires a write lock for the contents of this slot. Must be called
//    with no spinlocks held.

void bcslot::lock_buffer() {
    spinlock_guard guard(lock_);
    assert(state_ == s_clean || state_ == s_dirty);
    assert(buf_owner_ != current());
    while (buf_owner_) {
        guard.unlock();
        current()->yield();
        guard.lock();
    }
    buf_owner_ = current();
    state_ = s_dirty;
}


// bcslot::unlock_buffer()
//    Releases the write lock for the contents of this slot.

void bcslot::unlock_buffer() {
    spinlock_guard guard(lock_);
    assert(buf_owner_ == current());
    buf_owner_ = nullptr;
}


// bufcache::sync(drop)
//    Writes all dirty buffers to disk, blocking until complete.
//    If `drop > 0`, then additionally free all buffer cache contents,
//    except referenced blocks. If `drop > 1`, then assert that all inode
//    and data blocks are unreferenced.

int bufcache::sync(int drop) {
    // write dirty buffers to disk
    // Your code here!

    // drop clean buffers if requested
    if (drop > 0) {
        spinlock_guard guard(lock_);
        for (size_t i = 0; i != nslots; ++i) {
            spinlock_guard eguard(slots_[i].lock_);

            // validity checks: referenced entries aren't empty; if drop > 1,
            // no data blocks are referenced
            assert(slots_[i].ref_ == 0 || slots_[i].state_ != bcslot::s_empty);
            if (slots_[i].ref_ > 0 && drop > 1 && slots_[i].bn_ >= 2) {
                error_printf(CPOS(22, 0), "sync(2): block %u has nonzero reference count\n", slots_[i].bn_);
                assert_fail(__FILE__, __LINE__, "slots_[i].bn_ < 2");
            }

            // actually drop buffer
            if (slots_[i].ref_ == 0) {
                slots_[i].clear();
            }
        }
    }

    return 0;
}


// inode lock functions
//    The inode lock protects the inode's size and data references.
//    It is a read/write lock; multiple readers can hold the lock
//    simultaneously.
//
//    IMPORTANT INVARIANT: If a kernel task has an inode lock, it
//    must also hold a reference to the disk page containing that
//    inode.

namespace chkfs {

void inode::lock_read() {
    mlock_t v = mlock.load(std::memory_order_relaxed);
    while (true) {
        if (v == mlock_t(-1)) {
            // write locked
            current()->yield();
            v = mlock.load(std::memory_order_relaxed);
        } else if (mlock.compare_exchange_weak(v, v + 1,
                                               std::memory_order_acquire)) {
            return;
        } else {
            pause();
        }
    }
}

void inode::unlock_read() {
    mlock_t v = mlock.load(std::memory_order_relaxed);
    assert(v != 0 && v != mlock_t(-1));
    while (!mlock.compare_exchange_weak(v, v - 1,
                                        std::memory_order_release)) {
        pause();
    }
}

void inode::lock_write() {
    mlock_t v = 0;
    while (!mlock.compare_exchange_weak(v, mlock_t(-1),
                                        std::memory_order_acquire)) {
        current()->yield();
        v = 0;
    }
}

void inode::unlock_write() {
    assert(is_write_locked());
    mlock.store(0, std::memory_order_release);
}

bool inode::is_write_locked() const {
    return mlock.load(std::memory_order_relaxed) == mlock_t(-1);
}

}


// clean_inode_block(slot)
//    Called when loading an inode block into the buffer cache. It clears
//    values that are only used in memory.

static void clean_inode_block(bcslot* slot) {
    uint32_t slot_index = slot->index();
    auto is = reinterpret_cast<chkfs::inode*>(slot->buf_);
    for (unsigned i = 0; i != chkfs::inodesperblock; ++i) {
        // inode is initially unlocked
        is[i].mlock = 0;
        // containing slot's buffer cache position is `slot_index`
        is[i].mbcindex = slot_index;
    }
}


namespace chkfs {
// chkfs::inode::slot()
//    Returns a pointer to the buffer cache slot containing this inode.
//    Requires that this inode is a pointer into buffer cache data.
bcslot* inode::slot() const {
    assert(mbcindex < bufcache::nslots);
    auto& slot = bufcache::get().slots_[mbcindex];
    assert(slot.contains(this));
    return &slot;
}

// chkfs::inode::decrement_reference_Count()
//    Releases the caller’s reference to this inode, which must be located
//    in the buffer cache.
void inode::decrement_reference_count() {
    slot()->decrement_reference_count();
}
}


// chickadeefs state

chkfsstate chkfsstate::fs;

chkfsstate::chkfsstate() {
}


// chkfsstate::inode(inum)
//    Returns a reference to inode number `inum`, or a null reference if
//    there’s no such inode.

chkfs_iref chkfsstate::inode(inum_t inum) {
    auto& bc = bufcache::get();
    auto superblock_slot = bc.load(0);
    assert(superblock_slot);
    auto& sb = *reinterpret_cast<chkfs::superblock*>
        (&superblock_slot->buf_[chkfs::superblock_offset]);

    if (inum <= 0 || inum >= sb.ninodes) {
        return chkfs_iref();
    }

    auto bn = sb.inode_bn + inum / chkfs::inodesperblock;
    auto inode_slot = bc.load(bn, clean_inode_block);
    if (!inode_slot) {
        return chkfs_iref();
    }

    auto iarray = reinterpret_cast<chkfs::inode*>(inode_slot->buf_);
    inode_slot.release(); // the `chkfs_iref` claims the reference
    return chkfs_iref(&iarray[inum % chkfs::inodesperblock]);
}


// chkfsstate::lookup_inode(dirino, filename)
//    Returns the inode corresponding to the file named `filename` in
//    directory inode `dirino`. Returns a null reference if not found.
//    The caller must have acquired at least a read lock on `dirino`.

chkfs_iref chkfsstate::lookup_inode(chkfs::inode* dirino,
                                    const char* filename) {
    chkfs_fileiter it(dirino);
    size_t diroff = 0;
    while (true) {
        auto e = it.find(diroff).load();
        if (!e) {
            return chkfs_iref();
        }
        size_t bsz = min(dirino->size - diroff, blocksize);
        auto dirent = reinterpret_cast<chkfs::dirent*>(e->buf_);
        for (size_t pos = 0; pos < bsz; pos += chkfs::direntsize, ++dirent) {
            if (dirent->inum && strcmp(dirent->name, filename) == 0) {
                return inode(dirent->inum);
            }
        }
        diroff += blocksize;
    }
}


// chkfsstate::lookup_inode(filename)
//    Looks up `filename` in the root directory.

chkfs_iref chkfsstate::lookup_inode(const char* filename) {
    auto dirino = inode(1);
    if (!dirino) {
        return chkfs_iref();
    }
    dirino->lock_read();
    auto ino = fs.lookup_inode(dirino.get(), filename);
    dirino->unlock_read();
    return ino;
}


// chkfsstate::allocate_extent(unsigned count)
//    Allocates and returns the first block number of a fresh extent.
//    The returned extent doesn't need to be initialized (but it should not be
//    in flight to the disk or part of any incomplete journal transaction).
//    Returns the block number of the first block in the extent, or an error
//    code on failure. Errors can be distinguished by
//    `blocknum >= blocknum_t(E_MINERROR)`.

auto chkfsstate::allocate_extent(unsigned count) -> blocknum_t {
    // Your code here
    return E_INVAL;
}
