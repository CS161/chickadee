#ifndef CHICKADEE_K_CHKFSITER_HH
#define CHICKADEE_K_CHKFSITER_HH
#include "k-chkfs.hh"

class chkfs_fileiter {
 public:
    using blocknum_t = chkfs::blocknum_t;
    using inode = chkfs::inode;
    static constexpr size_t blocksize = chkfs::blocksize;
    static constexpr size_t npos = -1;


    // initialize an iterator for `ino` at file offset `off`
    chkfs_fileiter(chkfs::inode* ino, off_t off = 0);
    NO_COPY_OR_ASSIGN(chkfs_fileiter);
    ~chkfs_fileiter();


    // return the current file offset
    inline off_t offset() const;
    // return true iff the iterator points to real data
    inline bool active() const;
    // return the file offset of the current block
    inline off_t block_offset() const;
    // return true iff `blocknum() != 0`
    inline bool present() const;

    // Return the block number corresponding to the current file offset.
    // Returns 0 if there is no block stored for the current offset.
    inline blocknum_t blocknum() const;
    // Return a buffer cache entry containing the current file offset’s data.
    // Returns nullptr if there is no block stored for the current offset.
    inline bcentry* get_disk_entry() const;


    // Move the iterator to file offset `off`. Returns `*this`.
    chkfs_fileiter& find(off_t off);
    // Like `find(offset() + delta)`.
    inline chkfs_fileiter& operator+=(ssize_t delta);
    // Like `find(offset() - delta)`.
    inline chkfs_fileiter& operator-=(ssize_t delta);


    // Move the iterator to the next larger file offset with a different
    // present block. If there is no such block, move the iterator to
    // offset `npos` (making the iterator `!active()`).
    void next();


    // xxxxxx
    // Change the block stored at the current offset to `bn`, allocating
    // blocks for indirect and doubly-indirect blocks as necessary.
    // Returns 0 on success, a negative error code on failure.
    //
    // This function can call:
    // * `chkfsstate::allocate_block`, to allocate indirect[2] blocks
    // * `bufcache::get_disk_entry(blocknum_t)`, to find indirect[2] bufentries
    // * `bufcache::get_write(bcentry*)` and `bufcache::put_write(bcentry*)`,
    //   to obtain write references to indirect[2] blocks and/or the inode
    //   block
    int append(blocknum_t bn, uint32_t count = 1);


 private:
    chkfs::inode* ino_;             // inode
    size_t off_;                    // file offset
    size_t eoff_;                   // file offset of this extent
    size_t eidx_;                   // index of this extent
    chkfs::extent* eptr_;           // pointer into buffer cache to
                                    // extent for `off_`

    bcentry* ino_entry_;            // buffer cache entry for inode
    // bcentry containing indirect extent block for `eidx_`
    bcentry* indirect_entry_ = nullptr;
};


inline chkfs_fileiter::chkfs_fileiter(inode* ino, off_t off)
    : ino_(ino), off_(0), eoff_(0), eidx_(0), eptr_(&ino->direct[0]) {
    ino_entry_ = bufcache::get().find_entry(ino);
    if (off != 0) {
        find(off);
    }
}

inline chkfs_fileiter::~chkfs_fileiter() {
    if (indirect_entry_) {
        indirect_entry_->put();
    }
}

inline off_t chkfs_fileiter::offset() const {
    return off_;
}
inline bool chkfs_fileiter::active() const {
    return off_ < npos;
}
inline off_t chkfs_fileiter::block_offset() const {
    return eoff_;
}
inline bool chkfs_fileiter::present() const {
    return eptr_ && eptr_->first != 0;
}
inline auto chkfs_fileiter::blocknum() const -> blocknum_t {
    if (eptr_ && eptr_->first != 0) {
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
