#ifndef CHICKADEE_K_CHKFS_HH
#define CHICKADEE_K_CHKFS_HH
#include "kernel.hh"
#include "chickadeefs.hh"
#include "k-lock.hh"
#include "k-wait.hh"

// buffer cache

struct bufentry {
    using blocknum_t = chkfs::blocknum_t;
    static constexpr blocknum_t emptyblock = blocknum_t(-1);

    spinlock lock_;                  // protects modification to `flags_`
                                     // and initial setting of `buf_`
    blocknum_t bn_ = emptyblock;     // disk block number or `emptyblock`
    unsigned ref_ = 0;               // refcount: protects entry
    unsigned flags_ = 0;             // flags
    unsigned char* buf_ = nullptr;   // memory buffer used for entry

    enum {
        f_loaded = 1, f_loading = 2, f_dirty = 4
    };


    inline void clear();
};

struct bufcache {
    using blocknum_t = bufentry::blocknum_t;
    static constexpr blocknum_t emptyblock = bufentry::emptyblock;

    static constexpr size_t ne = 10;

    spinlock lock_;                  // protects all entries' bn_ and ref_
    wait_queue read_wq_;
    bufentry e_[ne];


    static inline bufcache& get();

    typedef void (*clean_block_function)(unsigned char*);
    bufentry* get_disk_entry(blocknum_t bn,
                             clean_block_function cleaner = nullptr);
    void put_entry(bufentry* e);

    bufentry* find_entry(void* data);

    void get_write(bufentry* e);
    void put_write(bufentry* e);

    int sync(bool drop);

 private:
    static bufcache bc;

    bufcache();
    NO_COPY_OR_ASSIGN(bufcache);
};


// chickadeefs state: a Chickadee file system on a specific disk
// (Our implementation only speaks to `sata_disk`.)

struct chkfsstate {
    using blocknum_t = chkfs::blocknum_t;
    using inum_t = chkfs::inum_t;
    using inode = chkfs::inode;
    static constexpr size_t blocksize = chkfs::blocksize;
    static constexpr blocknum_t emptyblock = bufentry::emptyblock;


    static inline chkfsstate& get();

    inode* get_inode(inum_t inum);
    void put_inode(inode* ino);

    inode* lookup_inode(inode* dirino, const char* name);

    blocknum_t allocate_extent(unsigned count = 1);


  private:
    static chkfsstate fs;

    chkfsstate();
    NO_COPY_OR_ASSIGN(chkfsstate);
};


inline void bufentry::clear() {
    bn_ = emptyblock;
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
                                  unsigned char* buf, size_t sz, size_t off);

#endif
