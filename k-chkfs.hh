#ifndef CHICKADEE_K_CHKFS_HH
#define CHICKADEE_K_CHKFS_HH
#include "kernel.hh"
#include "chickadeefs.hh"
#include "k-lock.hh"
#include "k-wait.hh"

// buffer cache

using bcentry_clean_function = void (*)(chkfs::blocknum_t, unsigned char*);

struct bcentry {
    using blocknum_t = chkfs::blocknum_t;

    enum state_t {
        state_empty, state_allocated, state_loading, state_clean
    };

    std::atomic<int> state_ = state_empty;

    spinlock lock_;                      // protects most `state_` changes
    blocknum_t bn_;                      // disk block number (unless empty)
    unsigned ref_ = 0;                   // reference count
    unsigned char* buf_ = nullptr;       // memory buffer used for entry


    // test if this entry is empty (`state_ == state_empty`)
    inline bool empty() const;

    // release a reference on this entry
    void put();

    // obtain/release a write reference to this entry
    void get_write();
    void put_write();


    // internal functions
    void clear();
    bool load(irqstate& irqs, bcentry_clean_function cleaner);
};

struct bufcache {
    using blocknum_t = bcentry::blocknum_t;

    static constexpr size_t ne = 10;

    spinlock lock_;                  // protects all entries' bn_ and ref_
    wait_queue read_wq_;
    bcentry e_[ne];


    static inline bufcache& get();

    bcentry* get_disk_entry(blocknum_t bn,
                            bcentry_clean_function cleaner = nullptr);
    bcentry* find_entry(void* data);

    int sync(int drop);

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


    static inline chkfsstate& get();

    // obtain an inode by number
    inode* get_inode(inum_t inum);

    // directory lookup in `dirino`
    inode* lookup_inode(inode* dirino, const char* name);
    // directory lookup starting at root directory
    inode* lookup_inode(const char* name);

    blocknum_t allocate_extent(unsigned count = 1);


  private:
    static chkfsstate fs;

    chkfsstate();
    NO_COPY_OR_ASSIGN(chkfsstate);
};


inline bufcache& bufcache::get() {
    return bc;
}

inline chkfsstate& chkfsstate::get() {
    return fs;
}

inline bool bcentry::empty() const {
    return state_.load(std::memory_order_relaxed) == state_empty;
}

inline void bcentry::clear() {
    assert(ref_ == 0);
    state_ = state_empty;
    if (buf_) {
        kfree(buf_);
        buf_ = nullptr;
    }
}

#endif
