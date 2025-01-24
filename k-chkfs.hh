#ifndef CHICKADEE_K_CHKFS_HH
#define CHICKADEE_K_CHKFS_HH
#include "kernel.hh"
#include "chickadeefs.hh"
#include "k-lock.hh"
#include "k-wait.hh"

// buffer cache

using block_clean_function = void (*)(bcslot*);

struct bcslot {
    using blocknum_t = chkfs::blocknum_t;

    enum state_t {
        s_empty, s_allocated, s_loading, s_clean, s_dirty
    };

    spinlock lock_;

    std::atomic<int> state_ = s_empty;   // slot state
    std::atomic<unsigned> ref_ = 0;      // reference count

    blocknum_t bn_;                      // disk block number (unless empty)
    unsigned char* buf_ = nullptr;       // memory buffer
    proc* buf_owner_ = nullptr;          // `proc` holding buffer content lock


    // return the index of this slot in the buffer cache
    inline size_t index() const;

    // test if this slot is empty (`state_ == s_empty`)
    inline bool empty() const;

    // test if `ptr` is contained in this slot's memory buffer
    inline bool contains(const void* ptr) const;

    // decrement reference count
    void decrement_reference_count();

    // acquire/release buffer_lock_, the lock on buffer data
    void lock_buffer();
    void unlock_buffer();


    // internal functions
    void clear();
    bool load(irqstate& irqs, block_clean_function cleaner);
};

using bcref = ref_ptr<bcslot>;

struct bufcache {
    using blocknum_t = chkfs::blocknum_t;

    static constexpr size_t nslots = 10;

    spinlock lock_;                  // protects all entries' bn_ and ref_
    wait_queue read_wq_;
    bcslot slots_[nslots];


    static inline bufcache& get();

    bcref load(blocknum_t bn, block_clean_function cleaner = nullptr);

    int sync(int drop);

 private:
    static bufcache bc;

    bufcache();
    NO_COPY_OR_ASSIGN(bufcache);
};


inline bufcache& bufcache::get() {
    return bc;
}

inline size_t bcslot::index() const {
    auto& bc = bufcache::get();
    assert(this >= bc.slots_ && this < bc.slots_ + bc.nslots);
    return this - bc.slots_;
}

inline bool bcslot::empty() const {
    return state_.load(std::memory_order_relaxed) == s_empty;
}

inline bool bcslot::contains(const void* ptr) const {
    return state_.load(std::memory_order_relaxed) >= s_clean
        && reinterpret_cast<uintptr_t>(ptr) - reinterpret_cast<uintptr_t>(buf_)
               < chkfs::blocksize;
}

inline void bcslot::clear() {
    assert(ref_ == 0);
    state_ = s_empty;
    if (buf_) {
        kfree(buf_);
        buf_ = nullptr;
    }
}


using chkfs_iref = ref_ptr<chkfs::inode>;


// chickadeefs state: a Chickadee file system on a specific disk
// (Our implementation only speaks to `sata_disk`.)

struct chkfsstate {
    using blocknum_t = chkfs::blocknum_t;
    using inum_t = chkfs::inum_t;
    static constexpr size_t blocksize = chkfs::blocksize;


    static inline chkfsstate& get();

    // obtain an inode by number
    chkfs_iref inode(inum_t inum);
    // directory lookup in `dirino`
    chkfs_iref lookup_inode(chkfs::inode* dirino, const char* name);
    // directory lookup starting at root directory
    chkfs_iref lookup_inode(const char* name);

    blocknum_t allocate_extent(unsigned count = 1);


  private:
    static chkfsstate fs;

    chkfsstate();
    NO_COPY_OR_ASSIGN(chkfsstate);
};


inline chkfsstate& chkfsstate::get() {
    return fs;
}

#endif
