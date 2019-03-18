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
        s_empty, s_loading, s_clean
    };

    std::atomic<int> state_ = s_empty;   // state (empty, loading, clean)

    spinlock lock_;                      // protects most `state_` changes
    unsigned ref_ = 0;                   // reference count
    blocknum_t bn_;                      // disk block number (unless empty)
    unsigned char* buf_ = nullptr;       // memory buffer used for entry


    inline bool empty() const;
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
    void put_entry(bcentry* e);

    bcentry* find_entry(void* data);

    void get_write(bcentry* e);
    void put_write(bcentry* e);

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


inline bufcache& bufcache::get() {
    return bc;
}

inline chkfsstate& chkfsstate::get() {
    return fs;
}

inline bool bcentry::empty() const {
    return state_.load(std::memory_order_relaxed) == s_empty;
}

size_t chickadeefs_read_file_data(const char* filename,
                                  unsigned char* buf, size_t sz, size_t off);

#endif
