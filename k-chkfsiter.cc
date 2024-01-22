#include "k-chkfsiter.hh"


// MAIN CHICKADEEFS ITERATOR FUNCTIONS

chkfs_fileiter& chkfs_fileiter::find(off_t off) {
    // if moving backwards, rewind to start
    off_ = off;
    if (!eptr_ || off_ < eoff_) {
        eoff_ = 0;
        eidx_ = 0;
        eptr_ = &ino_->direct[0];
        if (indirect_entry_) {
            indirect_entry_->put();
            indirect_entry_ = nullptr;
        }
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
            if (indirect_entry_) {
                indirect_entry_->put();
                indirect_entry_ = nullptr;
            }
            unsigned ibi = (eidx_ - chkfs::ndirect) / chkfs::extentsperblock;
            if (ino_->indirect.count <= ibi) {
                goto not_found;
            }
            auto& bc = bufcache::get();
            indirect_entry_ = bc.get_disk_entry(ino_->indirect.first + ibi);
            if (!indirect_entry_) {
                goto not_found;
            }
            eptr_ = reinterpret_cast<chkfs::extent*>(indirect_entry_->buf_);
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
    assert(ino_->has_write_lock());
    assert(count != 0);
    assert(!eptr_ || !eptr_->count);
    assert((eoff_ % blocksize) == 0);
    auto& bc = bufcache::get();
    auto ino_entry = inode()->entry();

    // grow previous direct extent if possible
    if (eidx_ > 0 && eidx_ <= chkfs::ndirect) {
        chkfs::extent* peptr = &ino_->direct[eidx_ - 1];
        if (peptr->first + peptr->count == first) {
            ino_entry->get_write();
            eptr_ = peptr;
            --eidx_;
            eoff_ -= eptr_->count * blocksize;
            eptr_->count += count;
            ino_entry->put_write();
            return 0;
        }
    }

    // allocate & initialize indirect extent if necessary
    if (eidx_ == chkfs::ndirect && !ino_->indirect.count) {
        auto& chkfs = chkfsstate::get();
        assert(!indirect_entry_);

        blocknum_t indirect_bn = chkfs.allocate_extent(1);
        if (indirect_bn >= blocknum_t(E_MINERROR)) {
            return int(indirect_bn);
        }

        indirect_entry_ = bc.get_disk_entry(indirect_bn);
        if (!indirect_entry_) {
            return E_NOMEM;
        }

        indirect_entry_->get_write();
        memset(indirect_entry_->buf_, 0, blocksize);
        indirect_entry_->put_write();

        ino_entry->get_write();
        ino_->indirect.first = indirect_bn;
        ino_->indirect.count = 1;
        ino_entry->put_write();
    }

    // fail if required to grow indirect extent
    if (eidx_ >= chkfs::ndirect && !indirect_entry_) {
        return E_FBIG;
    }

    // add new extent
    bcentry* entry;
    if (eidx_ < chkfs::ndirect) {
        entry = ino_entry;
        eptr_ = &ino_->direct[eidx_];
    } else {
        entry = indirect_entry_;
        eptr_ = reinterpret_cast<chkfs::extent*>(indirect_entry_->buf_)
            + (eidx_ - chkfs::ndirect) % chkfs::extentsperblock;
    }
    entry->get_write();
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
    entry->put_write();
    return 0;
}
