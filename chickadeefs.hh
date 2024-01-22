#ifndef CHICKADEE_CHICKADEEFS_HH
#define CHICKADEE_CHICKADEEFS_HH
#include <atomic>
#if defined(CHICKADEE_KERNEL) || defined(CHICKADEE_PROCESS)
# include "lib.hh"
#else
# include <assert.h>
# include <inttypes.h>
#endif
#ifdef CHICKADEE_KERNEL
struct bcentry;
#endif

namespace chkfs {

using blocknum_t = uint32_t;      // type of block numbers
using inum_t = int32_t;           // type of inode numbers
using mlock_t = uint8_t;          // underlying type of `inode::mlock`

// block size
static constexpr size_t blocksize = 4096;
static constexpr size_t bitsperblock = blocksize * 8;

// superblock information
static constexpr size_t superblock_offset = 512;   // offset of struct in block
static constexpr uint64_t magic = 0xFBBFBB003EE9BEEFUL;

// inode information
static constexpr size_t ndirect = 4;       // # direct extents per inode
static constexpr size_t inodesize = 64;    // `sizeof(struct inode)`
static constexpr size_t inodesperblock = blocksize / inodesize;
static constexpr size_t extentsize = 8;    // `sizeof(extent)`
static constexpr size_t extentsperblock = blocksize / extentsize;

// directory entry information
static constexpr size_t maxnamelen = 123;  // max strlen(name) supported
static constexpr size_t direntsize = 128;  // `sizeof(struct dirent)`

// `inode::type` constants
static constexpr uint32_t type_regular = 1;
static constexpr uint32_t type_directory = 2;


struct superblock {
    uint64_t magic;               // must equal `chkfs::magic`
    blocknum_t nblocks;           // # blocks in file system
    blocknum_t nswap;             // # blocks in swap space (unused)
    inum_t ninodes;               // # inodes in file system
    blocknum_t njournal;          // # blocks in journal
    blocknum_t swap_bn;           // first swap space block
    blocknum_t fbb_bn;            // first free block bitmap block
    blocknum_t inode_bn;          // first inode block
    blocknum_t data_bn;           // first data-area block
    blocknum_t journal_bn;        // first block in journal
};

struct extent {
    blocknum_t first;             // first block (0 means none)
    uint32_t count;               // number of blocks (0 means end)
};

struct inode {
    uint32_t type;                // file type (regular, directory, or 0/none)
    uint32_t size;                // file size
    uint32_t nlink;               // # hard links to file
    uint32_t flags;               // flags (currently unused)
    std::atomic<mlock_t> mlock;   // used in memory, 0 when loaded from disk
    uint32_t mbcindex;            // used in memory, 0 when loaded from disk
    extent direct[ndirect];       // extents
    extent indirect;

#ifdef CHICKADEE_KERNEL
    // return the buffer cache entry containing this buffer-cached inode
    bcentry* entry();
    // drop reference to this buffer-cached inode
    void put();
    // obtain/release locks; the lock_ functions may yield, so cannot be
    // called with spinlocks
    void lock_read();
    void unlock_read();
    void lock_write();
    void unlock_write();
    bool has_write_lock() const;
#endif
};

struct dirent {
    inum_t inum;                  // inode # (0 = none)
    char name[maxnamelen + 1];    // file name (null terminated)
};


using tid_t = uint16_t;
using tiddiff_t = int16_t;

inline bool tid_lt(tid_t x, tid_t y) {
    return tiddiff_t(x - y) < 0;
}
inline bool tid_le(tid_t x, tid_t y) {
    return tiddiff_t(x - y) <= 0;
}
inline bool tid_ge(tid_t x, tid_t y) {
    return tiddiff_t(x - y) >= 0;
}
inline bool tid_gt(tid_t x, tid_t y) {
    return tiddiff_t(x - y) > 0;
}

static constexpr uint64_t journalmagic = 0xFBBFBB009EEBCEEDUL;
static constexpr uint32_t nochecksum = 0x82600A5F;
static constexpr size_t ref_size = (blocksize / 4 - 7) / 3;

struct jblockref {              // component of `jmetablock`
    blocknum_t bn;              // destination block number
    uint32_t bchecksum;         // CRC32C checksum of block data
    uint16_t bflags;            // see `jbf_` constants
};
struct jmetablock {
    uint64_t magic;             // must equal `chkfs::journalmagic`
    uint32_t checksum;          // CRC32C checksum of block starting at `seq`
    uint32_t padding;
    // checksum starts here:
    tid_t seq;                  // sequence number
    tid_t tid;                  // associated tid
    tid_t commit_boundary;      // first noncommitted tid
    tid_t complete_boundary;    // first noncompleted tid
    uint16_t flags;             // see `jf_` constants
    uint16_t nref;              // # valid entries in `ref`
    jblockref ref[ref_size];

    inline bool is_valid_meta() const;
};
enum {
    // jmetablock::flags bits
    jf_meta = 0x01,             // this block is a metablock (mandatory)
    jf_error = 0x02,
    jf_corrupt = 0x04,
    jf_start = 0x10,            // metablock starts transaction `tid`
    jf_commit = 0x20,           // metablock commits `tid` (optional)
    jf_complete = 0x40,         // metablock marks `tid` complete (optional)

    // jblockref::bflags bits
    jbf_escaped = 0x100,        // refblock is escaped in journal
    jbf_nonjournaled = 0x200,   // refblock is no longer journaled
    jbf_overwritten = 0x400     // refblock overwritten in later txn
};


struct journalreplayer {
    struct metaref {
        unsigned bi;
        jmetablock* b;
    };
    unsigned char* jd_;
    unsigned nb_;
    metaref* mr_;
    unsigned nmr_;
    bool ok_;


    journalreplayer();
    virtual ~journalreplayer();

    bool analyze(unsigned char* jd, unsigned nb);
    void run();


    // The following are callbacks called by `run()`.

    // Report a progress message at journal block index `bi`.
    virtual void message(unsigned bi, const char* format, ...);
    // Report an error at journal block index `bi`.
    virtual void error(unsigned bi, const char* format, ...);
    // Write the data in `buf` to block number `bn` (txn was `tid`).
    virtual void write_block(tid_t tid, blocknum_t bn, unsigned char* buf);
    // Called at the end of `run()`.
    virtual void write_replay_complete();


 private:
    static inline bool is_potential_metablock(const unsigned char* buf);
    void analyze_block(unsigned bi);
    unsigned analyze_block_reference(jmetablock* jmb, const jblockref& ref,
                                     unsigned bi, unsigned delta);
    void analyze_tid(tid_t tid);
    void analyze_overwritten_blocks(unsigned mi);
    void mark_overwritten_block(blocknum_t bn, unsigned mi, unsigned refi);

    journalreplayer(const journalreplayer&) = delete;
    journalreplayer(journalreplayer&&) = delete;
    journalreplayer& operator=(const journalreplayer&) = delete;
    journalreplayer& operator=(journalreplayer&&) = delete;
};


inline bool jmetablock::is_valid_meta() const {
    return (flags & (jf_meta | jf_error)) == jf_meta;
}

}

#endif
