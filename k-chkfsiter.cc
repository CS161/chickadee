#include "k-chkfsiter.hh"


// MAIN CHICKADEEFS ITERATOR FUNCTIONS

chkfs_fileiter& chkfs_fileiter::find(off_t off) {
    // if moving backwards, rewind to start
    off_ = off;
    if (!eptr_ || off_ < eoff_) {
        eoff_ = 0;
        eidx_ = 0;
        eptr_ = &ino_->direct[0];
        indirect_slot_.reset();
    }

    // walk extents to relevant position
    while (off_ >= eoff_ + eptr_->count * blocksize) {
        if (eptr_->count == 0) {
            goto not_found;
        }

        eoff_ += eptr_->count * blocksize;
        ++eidx_;
        ++eptr_;

        if (eidx_ < chkfs::ndirect) {
            // do nothing
        } else if ((eidx_ - chkfs::ndirect) % chkfs::extentsperblock == 0) {
            indirect_slot_.reset();
            unsigned ibi = (eidx_ - chkfs::ndirect) / chkfs::extentsperblock;
            if (ino_->indirect.count <= ibi) {
                goto not_found;
            }
            auto& bc = bufcache::get();
            indirect_slot_ = bc.load(ino_->indirect.first + ibi);
            if (!indirect_slot_) {
                goto not_found;
            }
            eptr_ = reinterpret_cast<chkfs::extent*>(indirect_slot_->buf_);
        }
    }

    return *this;

 not_found:
    eptr_ = nullptr;
    return *this;
}


void chkfs_fileiter::next() {
    if (eptr_ && eptr_->count != 0) {
        do {
            find(round_up(off_ + 1, blocksize));
        } while (eptr_ && eptr_->first == 0 && eptr_->count != 0);
    }
}


int chkfs_fileiter::insert(blocknum_t first, unsigned count) {
    assert(ino_->is_write_locked());
    assert(count != 0);
    assert(!eptr_ || !eptr_->count);
    assert((eoff_ % blocksize) == 0);
    auto& bc = bufcache::get();
    auto ino_slot = inode()->slot();

    // grow previous direct extent if possible
    if (eidx_ > 0 && eidx_ <= chkfs::ndirect) {
        chkfs::extent* peptr = &ino_->direct[eidx_ - 1];
        if (peptr->first + peptr->count == first) {
            ino_slot->lock_buffer();
            eptr_ = peptr;
            --eidx_;
            eoff_ -= eptr_->count * blocksize;
            eptr_->count += count;
            ino_slot->unlock_buffer();
            return 0;
        }
    }

    // allocate & initialize indirect extent if necessary
    if (eidx_ == chkfs::ndirect && !ino_->indirect.count) {
        auto& chkfs = chkfsstate::get();
        assert(!indirect_slot_);

        blocknum_t indirect_bn = chkfs.allocate_extent(1);
        if (indirect_bn >= blocknum_t(E_MINERROR)) {
            return int(indirect_bn);
        }

        indirect_slot_ = bc.load(indirect_bn);
        if (!indirect_slot_) {
            return E_NOMEM;
        }

        indirect_slot_->lock_buffer();
        memset(indirect_slot_->buf_, 0, blocksize);
        indirect_slot_->unlock_buffer();

        ino_slot->lock_buffer();
        ino_->indirect.first = indirect_bn;
        ino_->indirect.count = 1;
        ino_slot->unlock_buffer();
    }

    // fail if required to grow indirect extent
    if (eidx_ >= chkfs::ndirect && !indirect_slot_) {
        return E_FBIG;
    }

    // add new extent
    bcslot* slot;
    if (eidx_ < chkfs::ndirect) {
        slot = ino_slot;
        eptr_ = &ino_->direct[eidx_];
    } else {
        slot = indirect_slot_.get();
        eptr_ = reinterpret_cast<chkfs::extent*>(indirect_slot_->buf_)
            + (eidx_ - chkfs::ndirect) % chkfs::extentsperblock;
    }
    slot->lock_buffer();
    if (eidx_ >= chkfs::ndirect
        && (eidx_ - chkfs::ndirect) % chkfs::extentsperblock != 0
        && eptr_[-1].first + eptr_[-1].count == first) {
        // grow previous extent
        --eptr_;
        --eidx_;
        eoff_ -= eptr_->count * blocksize;
        eptr_->count += count;
    } else {
        // add new extent
        eptr_->first = first;
        eptr_->count = count;
    }
    slot->unlock_buffer();
    return 0;
}
