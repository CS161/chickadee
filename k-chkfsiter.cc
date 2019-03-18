#include "k-chkfsiter.hh"


// MAIN CHICKADEEFS ITERATOR FUNCTIONS

chkfs_fileiter& chkfs_fileiter::find(size_t off) {
    auto& bc = bufcache::get();

    // if moving backwards, rewind to start
    off_ = off;
    if (!eptr_ || off_ < eoff_) {
        eoff_ = 0;
        eidx_ = 0;
        eptr_ = &ino_->direct[0];
        if (indirect_entry_) {
            bc.put_entry(indirect_entry_);
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
                bc.put_entry(indirect_entry_);
                indirect_entry_ = nullptr;
            }
            unsigned ibi = (eidx_ - chkfs::ndirect) / chkfs::extentsperblock;
            if (ino_->indirect.count <= ibi) {
                goto not_found;
            }
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
    while (off_ < npos) {
        find(round_up(off_ + 1, blocksize));

        if (!eptr_) {
            off_ = npos;
        } else if (eptr_->first) {
            break;
        }
    }
}


int chkfs_fileiter::append(blocknum_t first, unsigned count) {
    assert(ino_->has_write_lock());
    assert(count != 0);
    assert(!eptr_ || (eidx_ == 0 && !eptr_->count));
    auto& bc = bufcache::get();

    // grow previous direct extent if possible
    if (eidx_ > 0 && eidx_ <= chkfs::ndirect) {
        chkfs::extent* prev_eptr = &ino_->direct[eidx_ - 1];
        if (prev_eptr->first + prev_eptr->count == first) {
            eoff_ -= prev_eptr->count * blocksize;
            --eidx_;
            eptr_ = prev_eptr;
            eptr_->count += count;
            return 0;
        }
    }

    // allocate & initialize indirect extent if necessary
    if (eidx_ == chkfs::ndirect && !ino_->indirect.count) {
        auto& chkfs = chkfsstate::get();

        blocknum_t indirect_bn = chkfs.allocate_extent(1);
        if (indirect_bn >= blocknum_t(E_MINERROR)) {
            return int(indirect_bn);
        }

        indirect_entry_ = bc.get_disk_entry(indirect_bn);
        if (!indirect_entry_) {
            return E_NOMEM;
        }

        bc.get_write(indirect_entry_);
        memset(indirect_entry_->buf_, 0, blocksize);
        bc.put_write(indirect_entry_);

        bc.get_write(ino_entry_);
        ino_->indirect.first = indirect_bn;
        ino_->indirect.count = 1;
        bc.put_write(ino_entry_);
    }

    // fail if required to grow indirect extent
    if (eidx_ >= chkfs::ndirect && !indirect_entry_) {
        return E_FBIG;
    }

    // add new extent
    bcentry* entry;
    if (eidx_ < chkfs::ndirect) {
        entry = ino_entry_;
        eptr_ = &ino_->direct[eidx_];
    } else {
        entry = indirect_entry_;
        eptr_ = reinterpret_cast<chkfs::extent*>(indirect_entry_->buf_)
            + (eidx_ - chkfs::ndirect) % chkfs::extentsperblock;
    }
    bc.get_write(entry);
    eptr_->first = first;
    eptr_->count = count;
    bc.put_write(entry);
    return 0;
}
