#ifndef CHICKADEE_K_CHKFSITER_HH
#define CHICKADEE_K_CHKFSITER_HH
#include "k-chkfs.hh"

class chkfs_fileiter {
 public:
    using blocknum_t = chickadeefs::blocknum_t;
    using inode = chickadeefs::inode;
    static constexpr size_t blocksize = chickadeefs::blocksize;
    static constexpr blocknum_t emptyblock = bufentry::emptyblock;


    // Initialize an iterator for `ino` at file offset `off`.
    chkfs_fileiter(inode* ino, size_t off = 0);
    NO_COPY_OR_ASSIGN(chkfs_fileiter);
    ~chkfs_fileiter();


    // Return the current file offset.
    inline size_t offset() const;
    // Return true iff the iter is within ChickadeeFS's file size limits.
    inline bool active() const;
    // Return the block number corresponding to the current file offset.
    // Returns 0 if there is no block stored for the current offset.
    inline blocknum_t blocknum() const;
    // Return true iff `blocknum() != 0`.
    inline bool present() const;


    // Move the iterator to file offset `off`. Returns `*this`.
    chkfs_fileiter& find(size_t off);
    // Like `find(offset() + delta)`.
    inline chkfs_fileiter& operator+=(ssize_t delta);
    // Like `find(offset() - delta)`.
    inline chkfs_fileiter& operator-=(ssize_t delta);


    // Move the iterator to the next larger file offset with a different
    // present block. If there is no such block, move the iterator to
    // offset `chickadeefs::maxsize` (making the iterator `!active()`).
    void next();


    // Change the block stored at the current offset to `bn`, allocating
    // blocks for indirect and doubly-indirect blocks as necessary.
    // Returns 0 on success, a negative error code on failure.
    //
    // This function can call:
    // * `chkfsstate::allocate_block`, to allocate indirect[2] blocks
    // * `bufcache::get_disk_entry(blocknum_t)`, to find indirect[2] bufentries
    // * `bufcache::get_write(bufentry*)` and `bufcache::put_write(bufentry*)`,
    //   to obtain write references to indirect[2] blocks and/or the inode
    //   block
    int map(blocknum_t bn);


 private:
    inode* ino_;                    // inode
    size_t off_;                    // file offset
    blocknum_t* dptr_;              // pointer into buffer cache to
                                    // data block number for `off_`

    // bufentries for inode, indirect2 block, current indirect block
    bufentry* ino_entry_;
    bufentry* indirect2_entry_ = nullptr;
    bufentry* indirect_entry_ = nullptr;

    static inline constexpr unsigned iclass(unsigned bi);
    inline blocknum_t* get_iptr(unsigned bi) const;
    inline blocknum_t* get_dptr(unsigned bi) const;
    blocknum_t allocate_metablock_entry(bufentry** eptr) const;
};


inline chkfs_fileiter::chkfs_fileiter(inode* ino, size_t off)
    : ino_(ino), off_(0), dptr_(&ino->direct[0]) {
    ino_entry_ = bufcache::get().find_entry(ino);
    if (off != 0) {
        find(off);
    }
}

inline chkfs_fileiter::~chkfs_fileiter() {
    auto& bc = bufcache::get();
    if (indirect_entry_) {
        bc.put_entry(indirect_entry_);
    }
    if (indirect2_entry_) {
        bc.put_entry(indirect2_entry_);
    }
}

inline size_t chkfs_fileiter::offset() const {
    return off_;
}
inline bool chkfs_fileiter::active() const {
    return off_ < chickadeefs::maxsize;
}
inline auto chkfs_fileiter::blocknum() const -> blocknum_t {
    return dptr_ ? *dptr_ : 0;
}
inline bool chkfs_fileiter::present() const {
    return dptr_ && *dptr_ != 0;
}

inline chkfs_fileiter& chkfs_fileiter::operator+=(ssize_t delta) {
    return find(off_ + delta);
}
inline chkfs_fileiter& chkfs_fileiter::operator-=(ssize_t delta) {
    return find(off_ - delta);
}

#endif
