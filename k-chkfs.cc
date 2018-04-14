#include "k-chkfs.hh"
#include "k-devices.hh"

bufcache bufcache::bc;

bufcache::bufcache() {
}


// bufcache::get_disk_entry(bn, cleaner)
//    Read disk block `bn` into the buffer cache, obtain a reference to it,
//    and return a pointer to its bufentry. The function may block. If this
//    function reads the disk block from disk, and `cleaner != nullptr`,
//    then `cleaner` is called on the block data. Returns `nullptr` if
//    there's no room for the block.

bufentry* bufcache::get_disk_entry(chickadeefs::blocknum_t bn,
                                   clean_block_function cleaner) {
    assert(chickadeefs::blocksize == PAGESIZE);
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
                e_[i].buf_ = kalloc(chickadeefs::blocksize);
                if (!e_[i].buf_) {
                    --e_[i].ref_;
                    e_[i].lock_.unlock(irqs);
                    return nullptr;
                }
            }
            e_[i].flags_ |= bufentry::f_loading;
            e_[i].lock_.unlock(irqs);
            sata_disk->read(e_[i].buf_, chickadeefs::blocksize,
                            bn * chickadeefs::blocksize);
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
        buf = ROUNDDOWN(buf, chickadeefs::blocksize);

        // Synchronization is not necessary!
        // 1. The relevant entry has nonzero `ref_`, so its `buf_`
        //    will not change.
        // 2. No other entry has the same `buf_` because nonempty
        //    entries have unique `buf_`s.
        // (XXX Really, though, `buf_` should be std::atomic<void*>.)
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
        auto irqs = e->lock_.lock();
        --e->ref_;
        if (e->ref_ == 0) {
            kfree(e->buf_);
            e->clear();
        }
        e->lock_.unlock(irqs);
    }
}


// bufcache::sync(drop)
//    Write all dirty buffers to disk (blocking until complete).
//    Additionally free all buffer cache contents, except referenced
//    blocks, if `drop` is true.

int bufcache::sync(bool drop) {
    // Write dirty buffers to disk: your code here!

    if (drop) {
        auto irqs = lock_.lock();
        for (size_t i = 0; i != ne; ++i) {
            if (e_[i].bn_ != emptyblock && !e_[i].ref_) {
                kfree(e_[i].buf_);
                e_[i].clear();
            }
        }
        lock_.unlock(irqs);
    }

    return 0;
}


// clean_inode_block(buf)
//    This function is called when loading an inode block into the
//    buffer cache. It clears values that are only used in memory.

static void clean_inode_block(void* buf) {
    auto is = reinterpret_cast<chickadeefs::inode*>(buf);
    for (unsigned i = 0; i != chickadeefs::inodesperblock; ++i) {
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

namespace chickadeefs {

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
    assert(mlock.load(std::memory_order_relaxed) == uint32_t(-1));
    mlock.store(0, std::memory_order_release);
}

}


// chickadeefs state

chkfsstate chkfsstate::fs;

chkfsstate::chkfsstate() {
}


// chkfsstate::get_inode(inum)
//    Return inode number `inum`, or `nullptr` if there's no such inode.
//    The returned pointer should eventually be passed to `put_inode`.
chickadeefs::inode* chkfsstate::get_inode(inum_t inum) {
    auto& bc = bufcache::get();

    unsigned char* superblock_data = reinterpret_cast<unsigned char*>
        (bc.get_disk_block(0));
    assert(superblock_data);
    auto sb = reinterpret_cast<chickadeefs::superblock*>
        (&superblock_data[chickadeefs::superblock_offset]);
    auto inode_bn = sb->inode_bn;
    auto ninodes = sb->ninodes;
    bc.put_block(superblock_data);

    chickadeefs::inode* ino = nullptr;
    if (inum > 0 && inum < ninodes) {
        ino = reinterpret_cast<inode*>
            (bc.get_disk_block(inode_bn + inum / chickadeefs::inodesperblock,
                               clean_inode_block));
    }
    if (ino != nullptr) {
        ino += inum % chickadeefs::inodesperblock;
    }
    return ino;
}


// chkfsstate::put_inode(ino)
//    Drop the reference to `ino`.
void chkfsstate::put_inode(inode* ino) {
    if (ino) {
        bufcache::get().put_block(ROUNDDOWN(ino, PAGESIZE));
    }
}


// chkfsstate::get_data_block(ino, off)
//    Return a pointer to the data page at offset `off` into inode `ino`.
//    `off` must be a multiple of `blocksize`. May return `nullptr` if
//    no block has been allocated there. If the file is being read,
//    then at most `min(blocksize, ino->size - off)` bytes of data
//    in the returned page are valid.
unsigned char* chkfsstate::get_data_block(inode* ino, size_t off) {
    assert(off % blocksize == 0);
    auto& bc = bufcache::get();

    bufentry* i2e = nullptr;       // bufentry for indirect2 block (if needed)
    blocknum_t* iptr = nullptr;    // pointer to indirect block # (if needed)
    bufentry* ie = nullptr;        // bufentry for indirect block (if needed)
    blocknum_t* dptr = nullptr;    // pointer to direct block #
    bufentry* de = nullptr;        // bufentry for direct block

    unsigned bi = off / blocksize;

    // Set `iptr` to point to the relevant indirect block number
    // (if one is needed). This is either a pointer into the
    // indirect2 block, or a pointer to `ino->indirect`, or null.
    if (bi >= chickadeefs::ndirect + chickadeefs::nindirect) {
        if (ino->indirect2 != 0) {
            i2e = bc.get_disk_entry(ino->indirect2);
        }
        if (!i2e) {
            goto done;
        }
        iptr = reinterpret_cast<blocknum_t*>(i2e->buf_)
            + chickadeefs::bi_indirect_index(bi);
    } else if (bi >= chickadeefs::ndirect) {
        iptr = &ino->indirect;
    }

    // Set `dptr` to point to the relevant data block number.
    // This is either a pointer into an indirect block, or a
    // pointer to one of the `ino->direct` entries.
    if (iptr) {
        if (*iptr != 0) {
            ie = bc.get_disk_entry(*iptr);
        }
        if (!ie) {
            goto done;
        }
        dptr = reinterpret_cast<blocknum_t*>(ie->buf_)
            + chickadeefs::bi_direct_index(bi);
    } else {
        dptr = &ino->direct[chickadeefs::bi_direct_index(bi)];
    }

    // Finally, load the data block.
    if (*dptr != 0) {
        de = bc.get_disk_entry(*dptr);
    }

 done:
    // We don't need the indirect and doubly-indirect entries.
    bc.put_entry(ie);
    bc.put_entry(i2e);
    return de ? reinterpret_cast<unsigned char*>(de->buf_) : nullptr;
}


// chkfsstate::lookup_inode(dirino, filename)
//    Look up `filename` in the directory inode `dirino`, returning the
//    corresponding inode (or nullptr if not found). The caller should
//    eventually call `put_inode` on the returned inode pointer.
chickadeefs::inode* chkfsstate::lookup_inode(inode* dirino,
                                             const char* filename) {
    auto& bc = bufcache::get();

    // read directory to find file inode
    chickadeefs::inum_t in = 0;
    for (size_t diroff = 0; !in; diroff += blocksize) {
        void* directory_data = get_data_block(dirino, diroff);
        if (!directory_data) {
            break;
        }

        size_t bsz = min(dirino->size - diroff, blocksize);
        auto dirent = reinterpret_cast<chickadeefs::dirent*>(directory_data);
        for (unsigned i = 0; i * sizeof(*dirent) < bsz; ++i, ++dirent) {
            if (dirent->inum && strcmp(dirent->name, filename) == 0) {
                in = dirent->inum;
                break;
            }
        }

        bc.put_block(directory_data);
    }

    return get_inode(in);
}


// chickadeefs_read_file_data(filename, buf, sz, off)
//    Read up to `sz` bytes, from the file named `filename` in the
//    disk's root directory, into `buf`, starting at file offset `off`.
//    Returns the number of bytes read.

size_t chickadeefs_read_file_data(const char* filename,
                                  void* buf, size_t sz, size_t off) {
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

    size_t nread = 0;
    while (sz > 0) {
        size_t ncopy = 0;

        // read inode contents, copy data
        size_t blockoff = ROUNDDOWN(off, fs.blocksize);
        if (void* data = fs.get_data_block(ino, blockoff)) {
            size_t bsz = min(ino->size - blockoff, fs.blocksize);
            size_t boff = off - blockoff;
            if (bsz > boff) {
                ncopy = bsz - boff;
                if (ncopy > sz) {
                    ncopy = sz;
                }
                memcpy(reinterpret_cast<unsigned char*>(buf) + nread,
                       reinterpret_cast<unsigned char*>(data) + boff,
                       ncopy);
            }
            bc.put_block(data);
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
