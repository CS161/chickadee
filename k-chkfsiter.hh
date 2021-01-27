#ifndef CHICKADEE_K_CHKFSITER_HH
#define CHICKADEE_K_CHKFSITER_HH
#include "k-chkfs.hh"

class chkfs_fileiter {
 public:
    using blocknum_t = chkfs::blocknum_t;
    static constexpr size_t blocksize = chkfs::blocksize;


    // Initialize an iterator for `ino` at file offset `off`.
    // The caller must have a reference on `ino`.
    chkfs_fileiter(chkfs::inode* ino, off_t off = 0);
    NO_COPY_OR_ASSIGN(chkfs_fileiter);
    ~chkfs_fileiter();

    // Return the inode
    inline chkfs::inode* inode() const;

    // Return the current file offset
    inline off_t offset() const;
    // Return true iff the offset is within the file (i.e., in some extent)
    inline bool active() const;
    // Return true iff the offset does not point at data
    inline bool empty() const;

    // Return the block number corresponding to the current file offset.
    // Returns 0 if there is no block stored for the current offset.
    inline blocknum_t blocknum() const;
    // Return a buffer cache entry containing the current file offset’s data.
    // Returns nullptr if there is no block stored for the current offset.
    inline bcentry* get_disk_entry() const;
    // Return the file offset relative to the current block
    inline unsigned block_relative_offset() const;


    // Move the iterator to file offset `off`. Returns `*this`.
    chkfs_fileiter& find(off_t off);
    // Like `find(offset() + delta)`
    inline chkfs_fileiter& operator+=(ssize_t delta);
    // Like `find(offset() - delta)`
    inline chkfs_fileiter& operator-=(ssize_t delta);


    // Move the iterator to the next larger file offset with a different
    // present block. At the end of the file, the iterator becomes `!active()`.
    void next();


    // Add the extent `[first, first+count)` at this file offset.
    // Returns 0 on success, a negative error code on failure.
    //
    // The handout version can only add an extent to the end of the file,
    // immediately after all existing extents. An assertion will fail if
    // `offset()` is in the middle of the file or is not block-aligned.
    //
    // The handout version will allocate an indirect extent if necessary,
    // but it supports at most one indirect-extent block.
    //
    // This function can call:
    // * `chkfsstate::allocate_extent`, to allocate an indirect extent
    // * `bufcache::get_disk_entry`, to find indirect-extent blocks
    // * `bcentry::get_write` and `bcentry::put_write`, to obtain write
    //   references to inode and/or indirect-extent entries
    int insert(blocknum_t first, uint32_t count = 1);


 private:
    chkfs::inode* ino_;             // inode
    size_t off_ = 0;                // file offset
    size_t eoff_ = 0;               // file offset of this extent
    size_t eidx_ = 0;               // index of this extent
    chkfs::extent* eptr_;           // pointer into buffer cache to
                                    // extent for `off_`

    // bcentry containing indirect extent block for `eidx_`
    bcentry* indirect_entry_ = nullptr;
};


inline chkfs_fileiter::chkfs_fileiter(chkfs::inode* ino, off_t off)
    : ino_(ino), eptr_(&ino->direct[0]) {
    assert(ino_);
    if (off != 0) {
        find(off);
    }
}

inline chkfs_fileiter::~chkfs_fileiter() {
    if (indirect_entry_) {
        indirect_entry_->put();
    }
}

inline chkfs::inode* chkfs_fileiter::inode() const {
    return ino_;
}
inline off_t chkfs_fileiter::offset() const {
    return off_;
}
inline bool chkfs_fileiter::active() const {
    return eptr_ && eptr_->count != 0;
}
inline unsigned chkfs_fileiter::block_relative_offset() const {
    return off_ % blocksize;
}
inline bool chkfs_fileiter::empty() const {
    return !eptr_ || eptr_->first == 0;
}
inline auto chkfs_fileiter::blocknum() const -> blocknum_t {
    if (!empty()) {
        return eptr_->first + (off_ - eoff_) / blocksize;
    } else {
        return 0;
    }
}
inline bcentry* chkfs_fileiter::get_disk_entry() const {
    blocknum_t bn = blocknum();
    return bn ? bufcache::get().get_disk_entry(bn) : nullptr;
}

inline chkfs_fileiter& chkfs_fileiter::operator+=(ssize_t delta) {
    return find(off_ + delta);
}
inline chkfs_fileiter& chkfs_fileiter::operator-=(ssize_t delta) {
    return find(off_ - delta);
}

#endif
