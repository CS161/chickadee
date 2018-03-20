#ifndef CHICKADEE_K_CHKFS_HH
#define CHICKADEE_K_CHKFS_HH
#include "kernel.hh"
#include "chickadeefs.hh"
#include "k-lock.hh"

struct bufentry {
    spinlock lock_;
    unsigned ref_;
    chickadeefs::blocknum_t bn_;
    unsigned flags_;
    void* buf_;

    enum {
        f_loaded = 1, f_loading = 2, f_dirty = 4
    };


    inline bufentry();
    inline void clear();
};

struct bufcache {
    static constexpr size_t ne = 10;

    spinlock lock_;
    size_t n_;
    bufentry e_[ne];


    bufcache();
    NO_COPY_OR_ASSIGN(bufcache);

    static inline bufcache& get();

    typedef void (*clean_block_function)(void*);
    void* get_disk_block(chickadeefs::blocknum_t bn,
                         clean_block_function cleaner = nullptr);
    void put_block(void* buf);

 private:
    static bufcache bc;
};


size_t chickadeefs_read_file_data(const char* filename,
                                  void* buf, size_t sz, size_t off);


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

#endif
