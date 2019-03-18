#include "k-chkfs.hh"
#include "k-ahci.hh"
#include "k-chkfsiter.hh"

bufcache bufcache::bc;

bufcache::bufcache() {
}


// bufcache::get_disk_entry(bn, cleaner)
//    Read disk block `bn` into the buffer cache, obtain a reference to it,
//    and return a pointer to its bufentry. The function may block. If this
//    function reads the disk block from disk, and `cleaner != nullptr`,
//    then `cleaner` is called on the block data. Returns `nullptr` if
//    there's no room for the block.

bufentry* bufcache::get_disk_entry(chkfs::blocknum_t bn,
                                   clean_block_function cleaner) {
    assert(chkfs::blocksize == PAGESIZE);
    auto irqs = lock_.lock();

    // look for slot containing `bn`
    size_t i;
    for (i = 0; i != ne; ++i) {
        if (e_[i].bn_ == bn) {
            break;
        }
    }

    // if not found, look for free slot
    if (i == ne) {
        for (i = 0; i != ne && e_[i].bn_ != emptyblock; ++i) {
        }
        if (i == ne) {
            // cache full!
            lock_.unlock(irqs);
            log_printf("bufcache: no room for block %u\n", bn);
            return nullptr;
        }
        e_[i].bn_ = bn;
    }

    // mark reference
    ++e_[i].ref_;

    // switch lock to entry lock
    e_[i].lock_.lock_noirq();
    lock_.unlock_noirq();

    // load block, or wait for concurrent reader to load it
    while (!(e_[i].flags_ & bufentry::f_loaded)) {
        if (!(e_[i].flags_ & bufentry::f_loading)) {
            if (!e_[i].buf_) {
                e_[i].buf_ = reinterpret_cast<unsigned char*>
                    (kalloc(chkfs::blocksize));
                if (!e_[i].buf_) {
                    --e_[i].ref_;
                    e_[i].lock_.unlock(irqs);
                    return nullptr;
                }
            }
            e_[i].flags_ |= bufentry::f_loading;
            e_[i].lock_.unlock(irqs);
            sata_disk->read(e_[i].buf_, chkfs::blocksize,
                            bn * chkfs::blocksize);
            irqs = e_[i].lock_.lock();
            e_[i].flags_ = (e_[i].flags_ & ~bufentry::f_loading)
                | bufentry::f_loaded;
            if (cleaner) {
                cleaner(e_[i].buf_);
            }
            read_wq_.wake_all();
        } else {
            waiter(current()).block_until(read_wq_, [&] () {
                    return (e_[i].flags_ & bufentry::f_loading) == 0;
                }, e_[i].lock_, irqs);
        }
    }

    // return entry
    e_[i].lock_.unlock(irqs);
    return &e_[i];
}


// bufcache::find_entry(buf)
//    Return the `bufentry` containing pointer `buf`. This entry
//    must have a nonzero `ref_`.

bufentry* bufcache::find_entry(void* buf) {
    if (buf) {
        buf = reinterpret_cast<void*>(
            round_down(reinterpret_cast<uintptr_t>(buf), chkfs::blocksize)
        );

        // Synchronization is not necessary!
        // 1. The relevant entry has nonzero `ref_`, so its `buf_`
        //    will not change.
        // 2. No other entry has the same `buf_` because nonempty
        //    entries have unique `buf_`s.
        // (XXX Really, though, `bufentry::buf_` should be std::atomic.)
        for (size_t i = 0; i != ne; ++i) {
            if (e_[i].buf_ == buf) {
                return &e_[i];
            }
        }
        assert(false);
    }
    return nullptr;
}


// bufcache::put_entry(e)
//    Decrement the reference count for buffer cache entry `e`.

void bufcache::put_entry(bufentry* e) {
    if (e) {
        spinlock_guard guard(e->lock_);
        --e->ref_;
        if (e->ref_ == 0) {
            kfree(e->buf_);
            e->clear();
        }
    }
}


// bufcache::get_write(e)
//    Obtain a write reference for `e`.

void bufcache::get_write(bufentry* e) {
    // Your code here
    assert(false);
}


// bufcache::put_write(e)
//    Release a write reference for `e`.

void bufcache::put_write(bufentry* e) {
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
            if (e_[i].bn_ != emptyblock && !e_[i].ref_) {
                kfree(e_[i].buf_);
                e_[i].clear();
            }
        }
    }

    return 0;
}


// clean_inode_block(buf)
//    This function is called when loading an inode block into the
//    buffer cache. It clears values that are only used in memory.

static void clean_inode_block(unsigned char* buf) {
    auto is = reinterpret_cast<chkfs::inode*>(buf);
    for (unsigned i = 0; i != chkfs::inodesperblock; ++i) {
        is[i].mlock = is[i].mref = 0;
    }
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
    while (1) {
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


// chkfsstate::get_inode(inum)
//    Return inode number `inum`, or `nullptr` if there's no such inode.
//    The returned pointer should eventually be passed to `put_inode`.
chkfs::inode* chkfsstate::get_inode(inum_t inum) {
    auto& bc = bufcache::get();

    auto superblock_entry = bc.get_disk_entry(0);
    assert(superblock_entry);
    auto sb = reinterpret_cast<chkfs::superblock*>
        (&superblock_entry->buf_[chkfs::superblock_offset]);
    auto inode_bn = sb->inode_bn;
    auto ninodes = sb->ninodes;
    bc.put_entry(superblock_entry);

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


// chkfsstate::put_inode(ino)
//    Drop the reference to `ino`.
void chkfsstate::put_inode(inode* ino) {
    auto& bc = bufcache::get();
    bc.put_entry(bc.find_entry(ino));
}


// chkfsstate::lookup_inode(dirino, filename)
//    Look up `filename` in the directory inode `dirino`, returning the
//    corresponding inode (or nullptr if not found). The caller should
//    eventually call `put_inode` on the returned inode pointer.
chkfs::inode* chkfsstate::lookup_inode(inode* dirino,
                                       const char* filename) {
    auto& bc = bufcache::get();
    chkfs_fileiter it(dirino);

    // read directory to find file inode
    chkfs::inum_t in = 0;
    for (size_t diroff = 0; !in; diroff += blocksize) {
        bufentry* e;
        if (!(it.find(diroff).present()
              && (e = bc.get_disk_entry(it.blocknum())))) {
            break;
        }

        size_t bsz = min(dirino->size - diroff, blocksize);
        auto dirent = reinterpret_cast<chkfs::dirent*>(e->buf_);
        for (unsigned i = 0; i * sizeof(*dirent) < bsz; ++i, ++dirent) {
            if (dirent->inum && strcmp(dirent->name, filename) == 0) {
                in = dirent->inum;
                break;
            }
        }

        bc.put_entry(e);
    }

    return get_inode(in);
}


// chkfsstate::allocate_block()
//    Allocate and return the number of a fresh block. The returned
//    block need not be initialized (but it should not be in flight
//    to the disk or part of any incomplete journal transaction).
//    Returns the block number or an error code on failure. Errors
//    can be distinguished by `blocknum >= blocknum_t(E_MINERROR)`.

auto chkfsstate::allocate_block() -> blocknum_t {
    // Your code here
    return E_INVAL;
}


// chickadeefs_read_file_data(filename, buf, sz, off)
//    Read up to `sz` bytes, from the file named `filename` in the
//    disk's root directory, into `buf`, starting at file offset `off`.
//    Returns the number of bytes read.

size_t chickadeefs_read_file_data(const char* filename,
                                  unsigned char* buf, size_t sz, size_t off) {
    auto& bc = bufcache::get();
    auto& fs = chkfsstate::get();

    // read directory to find file inode number
    auto dirino = fs.get_inode(1);
    assert(dirino);
    dirino->lock_read();

    auto ino = fs.lookup_inode(dirino, filename);

    dirino->unlock_read();
    fs.put_inode(dirino);

    if (!ino) {
        return 0;
    }

    // read file inode
    ino->lock_read();
    chkfs_fileiter it(ino);

    size_t nread = 0;
    while (sz > 0) {
        size_t ncopy = 0;

        // read inode contents, copy data
        bufentry* e;
        if (it.find(off).present()
            && (e = bc.get_disk_entry(it.blocknum()))) {
            size_t blockoff = round_down(off, fs.blocksize);
            size_t bsz = min(ino->size - blockoff, fs.blocksize);
            size_t boff = off - blockoff;
            if (bsz > boff) {
                ncopy = bsz - boff;
                if (ncopy > sz) {
                    ncopy = sz;
                }
                memcpy(buf + nread, e->buf_ + boff, ncopy);
            }
            bc.put_entry(e);
        }

        // account for copied data
        if (ncopy == 0) {
            break;
        }
        nread += ncopy;
        off += ncopy;
        sz -= ncopy;
    }

    ino->unlock_read();
    fs.put_inode(ino);
    return nread;
}
