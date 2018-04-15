#include "k-chkfsiter.hh"


// MAIN CHICKADEEFS ITERATOR FUNCTIONS

chkfs_fileiter& chkfs_fileiter::find(size_t off) {
    auto& bc = bufcache::get();
    unsigned bi = min(off, chickadeefs::maxsize) / blocksize;

    // clear prior indirect_entry_ if we're in a different indirect class
    if (indirect_entry_ && iclass(bi) != iclass(off_ / blocksize)) {
        bc.put_entry(indirect_entry_);
        indirect_entry_ = nullptr;
    }

    off_ = off;
    dptr_ = nullptr;

    // load doubly-indirect block if necessary
    if (bi >= chickadeefs::ndirect + chickadeefs::nindirect
        && !indirect2_entry_) {
        if (ino_->indirect2 != 0) {
            indirect2_entry_ = bc.get_disk_entry(ino_->indirect2);
        }
        if (!indirect2_entry_) {
            goto done;
        }
    }

    // load indirect block if necessary
    if (bi >= chickadeefs::ndirect
        && !indirect_entry_) {
        blocknum_t* iptr = get_iptr(bi);
        if (*iptr != 0) {
            indirect_entry_ = bc.get_disk_entry(*iptr);
        }
        if (!indirect_entry_) {
            goto done;
        }
    }

    // set `dptr_`, which points to data block pointer
    dptr_ = get_dptr(bi);

 done:
    return *this;
}


void chkfs_fileiter::next() {
    while (1) {
        if (off_ < chickadeefs::maxsize) {
            find(ROUNDUP(off_ + 1, blocksize));
        }

        if (present() || !active()) {
            return;
        }

        unsigned biclass = iclass(off_ / blocksize);
        if (biclass > 1 && !indirect2_entry_) {
            off_ = chickadeefs::maxsize - 1;
        } else if (biclass > 0 && !indirect_entry_) {
            off_ = biclass * chickadeefs::nindirect * blocksize
                + chickadeefs::maxdirectsize - 1;
        }
    }
}


int chkfs_fileiter::map(blocknum_t bn) {
    assert(ino_->has_write_lock());
    auto& bc = bufcache::get();
    unsigned bi = off_ / blocksize;

    // simple cases: clearing empty mapping, beyond file size limit
    if (!bn && !dptr_) {
        return 0;
    }
    if (off_ >= chickadeefs::maxsize) {
        return E_FBIG;
    }

    // allocate & initialize doubly-indirect block if necessary
    if (bi >= chickadeefs::ndirect + chickadeefs::nindirect
        && !indirect2_entry_) {
        blocknum_t i2bn = allocate_metablock_entry(&indirect2_entry_);
        if (i2bn >= blocknum_t(E_MINERROR)) {
            return i2bn;
        }

        bc.get_write(ino_entry_);
        ino_->indirect2 = i2bn;
        bc.put_write(ino_entry_);
    }

    // allocate & initialize indirect block if necessary
    if (bi >= chickadeefs::ndirect
        && !indirect_entry_) {
        blocknum_t ibn = allocate_metablock_entry(&indirect_entry_);
        if (ibn >= blocknum_t(E_MINERROR)) {
            return ibn;
        }

        bufentry* iptr_entry =
            bi >= chickadeefs::ndirect + chickadeefs::nindirect
            ? indirect2_entry_ : ino_entry_;
        bc.get_write(iptr_entry);
        *get_iptr(bi) = ibn;
        bc.put_write(iptr_entry);
    }

    // store data block pointer
    bufentry* entry = bi >= chickadeefs::ndirect ? indirect_entry_ : ino_entry_;
    bc.get_write(entry);
    dptr_ = get_dptr(bi);
    *dptr_ = bn;
    bc.put_write(entry);

    return 0;
}


// INTERNAL CHICKADEEFS ITERATOR FUNCTIONS

// Return the "indirect class" for block index `bi`.
// This is 0 for direct blocks, 1 for blocks referenced by the primary
// indirect block, and 2 or more for blocks referenced from
// the indirect2 block.
inline constexpr unsigned chkfs_fileiter::iclass(unsigned bi) {
    return (bi + chickadeefs::nindirect - chickadeefs::ndirect)
        / chickadeefs::nindirect;
}

// Return a pointer to the location in the buffer cache where the
// indirect block number for block index `bi` is stored.
inline auto chkfs_fileiter::get_iptr(unsigned bi) const -> blocknum_t* {
    unsigned bi_ix = iclass(bi);
    if (bi_ix > 1) {
        auto iptrs = reinterpret_cast<blocknum_t*>(indirect2_entry_->buf_);
        return &iptrs[bi_ix - 2];
    } else if (bi_ix > 0) {
        return &ino_->indirect;
    } else {
        return nullptr;
    }
}

// Return a pointer to the location in the buffer cache where the
// data block number for block index `bi` is stored.
inline auto chkfs_fileiter::get_dptr(unsigned bi) const -> blocknum_t* {
    if (bi >= chickadeefs::ndirect) {
        return reinterpret_cast<blocknum_t*>(indirect_entry_->buf_)
            + chickadeefs::bi_direct_index(bi);
    } else {
        return &ino_->direct[bi];
    }
}

// Allocate and initialize a pointer block (i.e., an indirect or
// doubly-indirect block). Store a pointer to the bufentry in `*eptr`.
// Return the block number or an error code.
auto chkfs_fileiter::allocate_metablock_entry(bufentry** eptr) const
    -> blocknum_t {
    auto& chkfs = chkfsstate::get();
    auto& bc = bufcache::get();

    blocknum_t new_bn = chkfs.allocate_block();
    if (new_bn >= blocknum_t(E_MINERROR)) {
        return new_bn;
    }

    *eptr = bc.get_disk_entry(new_bn);
    if (!*eptr) {
        return blocknum_t(E_NOMEM);
    }

    bc.get_write(*eptr);
    memset((*eptr)->buf_, 0, blocksize);
    bc.put_write(*eptr);
    return new_bn;
}
