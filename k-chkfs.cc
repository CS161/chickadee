#include "k-chkfs.hh"
#include "k-devices.hh"

bufcache bufcache::bc;

bufcache::bufcache() {
}


// bufcache::get_disk_block(bn, cleaner)
//    Read disk block `bn` into the buffer cache, obtain a reference to it,
//    and return a pointer to its data. The function may block. If this
//    function reads the disk block from disk, and `cleaner != nullptr`,
//    then `cleaner` is called on the block data. Returns `nullptr` if
//    there's no room for the block.

void* bufcache::get_disk_block(chickadeefs::blocknum_t bn,
                               clean_block_function cleaner) {
    auto irqs = lock_.lock();

    // look for slot containing `bn`
    size_t i;
    for (i = 0; i != ne; ++i) {
        if (e_[i].ref_ != 0 && e_[i].bn_ == bn) {
            break;
        }
    }

    // if not found, look for free slot
    if (i == ne) {
        for (i = 0; i != ne && e_[i].ref_ != 0; ++i) {
        }
        if (i == ne) {
            // cache full!
            lock_.unlock(irqs);
            log_printf("bufcache: no room for block %u\n", bn);
            return nullptr;
        }
        e_[i].bn_ = bn;
        e_[i].buf_ = nullptr;
    }

    // mark reference
    ++e_[i].ref_;

    // switch lock to entry lock
    e_[i].lock_.lock_noirq();
    lock_.unlock_noirq();

    // load block, or wait for concurrent reader to load it
    while (!(e_[i].flags_ & bufentry::f_loaded)) {
        if (!(e_[i].flags_ & bufentry::f_loading)) {
            void* x = kalloc(chickadeefs::blocksize);
            if (!x) {
                e_[i].lock_.unlock(irqs);
                return nullptr;
            }
            e_[i].flags_ |= bufentry::f_loading;
            e_[i].lock_.unlock(irqs);
            sata_disk->read
                (bn * (chickadeefs::blocksize / sata_disk->sectorsize),
                 x, chickadeefs::blocksize / sata_disk->sectorsize);
            irqs = e_[i].lock_.lock();
            e_[i].flags_ = (e_[i].flags_ & ~bufentry::f_loading)
                | bufentry::f_loaded;
            e_[i].buf_ = x;
            if (cleaner) {
                cleaner(e_[i].buf_);
            }
        } else {
            waiter(current()).block_until(sata_disk->wq_, [&] () {
                    return (e_[i].flags_ & bufentry::f_loading) != 0;
                }, e_[i].lock_, irqs);
        }
    }

    // return memory
    auto buf = e_[i].buf_;
    e_[i].lock_.unlock(irqs);
    return buf;
}


// bufcache::put_block(buf)
//    Decrement the reference count for buffer cache block `buf`.

void bufcache::put_block(void* buf) {
    if (!buf) {
        return;
    }

    auto irqs = lock_.lock();

    // find block
    size_t i;
    for (i = 0; i != ne; ++i) {
        if (e_[i].ref_ != 0 && e_[i].buf_ == buf) {
            break;
        }
    }
    assert(i != ne);

    // drop reference
    --e_[i].ref_;
    if (e_[i].ref_ == 0) {
        kfree(e_[i].buf_);
        e_[i].clear();
    }

    lock_.unlock(irqs);
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

inline void inode::lock_read() {
    uint32_t v = mlock.load(std::memory_order_relaxed);
    while (1) {
        if (v == uint32_t(-1)) {
            current()->yield();
        } else if (mlock.compare_exchange_weak(v, v + 1,
                                               std::memory_order_acquire)) {
            return;
        } else {
            pause();
        }
    }
}

inline void inode::unlock_read() {
    uint32_t v = mlock.load(std::memory_order_relaxed);
    assert(v != 0 && v != uint32_t(-1));
    while (!mlock.compare_exchange_weak(v, v - 1,
                                        std::memory_order_release)) {
        pause();
    }
}

inline void inode::lock_write() {
    uint32_t v = 0;
    while (!mlock.compare_exchange_weak(v, uint32_t(-1),
                                        std::memory_order_acquire)) {
        current()->yield();
        v = 0;
    }
}

inline void inode::unlock_write() {
    assert(mlock.load(std::memory_order_relaxed) == uint32_t(-1));
    mlock.store(0, std::memory_order_release);
}

}


// chickadeefs_get_inode_contents(in, bi, n_valid_bytes)
//    Read the `bi`th data block in inode `in` and return a pointer to
//    its contents, using the buffer cache. The caller must later call
//    `bc.put_block()` using the result. `*n_valid_bytes` is set to
//    the number of bytes in the returned block that contain valid file
//    data.

void* chickadeefs_get_inode_contents(chickadeefs::inum_t in, unsigned bi,
                                     size_t* n_valid_bytes) {
    auto& bc = bufcache::get();

    // read superblock
    unsigned char* superblock_data = reinterpret_cast<unsigned char*>
        (bc.get_disk_block(0));
    assert(superblock_data);
    auto sb = reinterpret_cast<chickadeefs::superblock*>
        (&superblock_data[chickadeefs::superblock_offset]);
    auto inode_bn = sb->inode_bn;
    auto ninodes = sb->ninodes;
    bc.put_block(superblock_data);

    // check inode number
    if (in <= 0 || in >= sb->ninodes) {
        if (n_valid_bytes) {
            *n_valid_bytes = 0;
        }
        return nullptr;
    }

    // read inode block, obtain read lock on inode
    auto inode_data = bc.get_disk_block
        (inode_bn + in / chickadeefs::inodesperblock, clean_inode_block);
    assert(inode_data);
    auto ino = reinterpret_cast<chickadeefs::inode*>(inode_data)
        + in % chickadeefs::inodesperblock;
    ino->lock_read();

    // look up data block number
    chickadeefs::blocknum_t databn = 0;
    if (bi * chickadeefs::blocksize >= ino->size) {
        // past end of file
    } else if (bi < chickadeefs::ndirect) {
        databn = ino->direct[bi];
    } else if (bi < chickadeefs::ndirect + chickadeefs::nindirect) {
        auto indirect_data = bc.get_disk_block(ino->indirect);
        assert(indirect_data);
        databn = reinterpret_cast<chickadeefs::blocknum_t*>(indirect_data)
            [bi - chickadeefs::ndirect];
        bc.put_block(indirect_data);
    } else {
        auto indirect2_data = bc.get_disk_block(ino->indirect2);
        assert(indirect2_data);
        bi -= chickadeefs::ndirect + chickadeefs::nindirect;
        databn = reinterpret_cast<chickadeefs::blocknum_t*>(indirect2_data)
            [bi / chickadeefs::nindirect];

        auto indirect_data = bc.get_disk_block(databn);
        assert(indirect_data);
        databn = reinterpret_cast<chickadeefs::blocknum_t*>(indirect_data)
            [bi % chickadeefs::nindirect];
        bc.put_block(indirect_data);
        bc.put_block(indirect2_data);
    }

    // load data block
    void* data = nullptr;
    if (databn) {
        data = bc.get_disk_block(databn);
    }

    // set inode size
    if (n_valid_bytes) {
        if (data && ino->size > (bi + 1) * chickadeefs::blocksize) {
            *n_valid_bytes = chickadeefs::blocksize;
        } else if (data) {
            *n_valid_bytes = ino->size - bi * chickadeefs::blocksize;
        } else {
            *n_valid_bytes = 0;
        }
    }

    // clean up
    ino->unlock_read();
    bc.put_block(inode_data);
    return data;
}


// chickadeefs_read_file_data(filename, buf, sz, off)
//    Read up to `sz` bytes, from the file named `filename` in the
//    disk's root directory, into `buf`, starting at file offset `off`.
//    Returns the number of bytes read.

size_t chickadeefs_read_file_data(const char* filename,
                                  void* buf, size_t sz, size_t off) {
    auto& bc = bufcache::get();

    // read directory to find file inode
    chickadeefs::inum_t in = 0;
    for (unsigned directory_bn = 0; !in; ++directory_bn) {
        size_t bsz;
        void* directory_data = chickadeefs_get_inode_contents
            (1, directory_bn, &bsz);
        if (!directory_data) {
            break;
        }

        auto dirent = reinterpret_cast<chickadeefs::dirent*>(directory_data);
        for (unsigned i = 0; i * sizeof(*dirent) < bsz; ++i, ++dirent) {
            if (dirent->inum && strcmp(dirent->name, filename) == 0) {
                in = dirent->inum;
                break;
            }
        }

        bc.put_block(directory_data);
    }

    size_t nread = 0;
    while (sz > 0) {
        // read inode contents
        size_t bsz;
        void* data = chickadeefs_get_inode_contents
            (in, off / chickadeefs::blocksize, &bsz);

        // copy data from inode contents
        size_t ncopy = 0;
        if (data) {
            size_t boff = off % chickadeefs::blocksize;
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

    return nread;
}
