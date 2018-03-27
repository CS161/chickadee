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

void inode::lock_read() {
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
            (bc.get_disk_block(inode_bn + inum / chickadeefs::inodesperblock));
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

    // look up data block number
    unsigned bi = off / blocksize;
    chickadeefs::blocknum_t databn = 0;
    if (bi < chickadeefs::ndirect) {
        databn = ino->direct[bi];
    } else if (bi < chickadeefs::ndirect + chickadeefs::nindirect) {
        if (ino->indirect != 0) {
            auto indirect_data = bc.get_disk_block(ino->indirect);
            assert(indirect_data);
            databn = reinterpret_cast<chickadeefs::blocknum_t*>(indirect_data)
                [bi - chickadeefs::ndirect];
            bc.put_block(indirect_data);
        }
    } else {
        chickadeefs::blocknum_t indirbn = 0;
        if (ino->indirect2 != 0) {
            auto indirect2_data = bc.get_disk_block(ino->indirect2);
            assert(indirect2_data);
            bi -= chickadeefs::ndirect + chickadeefs::nindirect;
            indirbn = reinterpret_cast<chickadeefs::blocknum_t*>(indirect2_data)
                [bi / chickadeefs::nindirect];
            bc.put_block(indirect2_data);
        }

        if (indirbn != 0) {
            auto indirect_data = bc.get_disk_block(databn);
            assert(indirect_data);
            databn = reinterpret_cast<chickadeefs::blocknum_t*>(indirect_data)
                [bi % chickadeefs::nindirect];
            bc.put_block(indirect_data);
        }
    }

    // load data block
    void* data = nullptr;
    if (databn) {
        data = bc.get_disk_block(databn);
    }

    // clean up
    return reinterpret_cast<unsigned char*>(data);
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
