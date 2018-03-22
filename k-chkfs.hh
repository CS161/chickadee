#ifndef CHICKADEE_K_CHKFS_HH
#define CHICKADEE_K_CHKFS_HH
#include "kernel.hh"
#include "chickadeefs.hh"
#include "k-lock.hh"

// buffer cache

struct bufentry {
    spinlock lock_;                  // protects modification to `flags_`
                                     // and initial setting of `buf_`
    unsigned ref_;                   // refcount: protects entry
    chickadeefs::blocknum_t bn_;     // disk block number
    unsigned flags_;                 // flags
    void* buf_;                      // memory buffer used for entry

    enum {
        f_loaded = 1, f_loading = 2, f_dirty = 4
    };


    inline bufentry();
    inline void clear();
};

struct bufcache {
    static constexpr size_t ne = 10;

    spinlock lock_;                  // protects all entries' bn_ and ref_
    bufentry e_[ne];


    static inline bufcache& get();

    typedef void (*clean_block_function)(void*);
    void* get_disk_block(chickadeefs::blocknum_t bn,
                         clean_block_function cleaner = nullptr);
    void put_block(void* buf);

 private:
    static bufcache bc;

    bufcache();
    NO_COPY_OR_ASSIGN(bufcache);
};


// chickadeefs state: a Chickadee file system on a specific disk
// (Our implementation only speaks to `sata_disk`.)

struct chkfsstate {
    using inum_t = chickadeefs::inum_t;
    using inode = chickadeefs::inode;


    static inline chkfsstate& get();

    inode* get_inode(inum_t inum);
    void put_inode(inode* ino);

    void* get_data_page(inode* ino, size_t off, size_t* n_valid_bytes);

    inode* lookup_inode(inode* dirino, const char* name);


  private:
    static chkfsstate fs;

    chkfsstate();
    NO_COPY_OR_ASSIGN(chkfsstate);
};


inline bufentry::bufentry()
    : ref_(0), flags_(0), buf_(nullptr) {
}
inline void bufentry::clear() {
    assert(ref_ == 0);
    flags_ = 0;
    buf_ = nullptr;
}

inline bufcache& bufcache::get() {
    return bc;
}

inline chkfsstate& chkfsstate::get() {
    return fs;
}

size_t chickadeefs_read_file_data(const char* filename,
                                  void* buf, size_t sz, size_t off);

#endif
