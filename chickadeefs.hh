#ifndef CHICKADEE_CHICKADEEFS_HH
#define CHICKADEE_CHICKADEEFS_HH
#include <atomic>

namespace chickadeefs {

typedef uint32_t blocknum_t;
typedef int32_t inum_t;

static constexpr size_t blocksize = 4096;
static constexpr size_t bitsperblock = blocksize * 8;
static constexpr size_t ndirect = 9;
static constexpr size_t maxdirectsize = ndirect * blocksize;
static constexpr size_t nindirect = blocksize / sizeof(blocknum_t);
static constexpr size_t inodesize = 64;
static constexpr size_t inodesperblock = blocksize / inodesize;
static constexpr size_t maxindirectsize = maxdirectsize
    + nindirect * blocksize;
static constexpr size_t maxindirect2size = maxindirectsize
    + nindirect * nindirect * blocksize;
static constexpr size_t maxsize = maxindirect2size;
static constexpr size_t maxnamelen = 123;
static constexpr size_t superblock_offset = 512;

static constexpr uint64_t magic = 0xFBBFBB003EE9BEEFUL;

static constexpr uint32_t type_regular = 1;
static constexpr uint32_t type_directory = 2;

struct superblock {
    uint64_t magic;
    blocknum_t nblocks;
    blocknum_t nswap;
    inum_t ninodes;
    blocknum_t swap_bn;
    blocknum_t fbb_bn;
    blocknum_t inode_bn;
    blocknum_t data_bn;
    blocknum_t journal_bn;
};

struct inode {
    uint32_t type;
    uint32_t size;
    uint32_t nlink;
    std::atomic<uint32_t> mlock;
    std::atomic<uint32_t> mref;
    blocknum_t direct[ndirect];
    blocknum_t indirect;
    blocknum_t indirect2;

    // functions only defined in the kernel; the lock_ functions yield,
    // so cannot be called with spinlocks held
    void lock_read();
    void unlock_read();
    void lock_write();
    void unlock_write();
};

struct dirent {
    inum_t inum;
    char name[maxnamelen + 1];
};


static constexpr uint64_t journalmagic = 0xFBBFBB009EEBCEEDUL;
static constexpr size_t ref_size = (nindirect - 7) / 3;
typedef uint16_t tid_t;
typedef int16_t tiddiff_t;

struct jblockref {
    blocknum_t bn;
    uint32_t bchecksum;
    uint16_t bflags;
};
struct jmetablock {
    uint64_t magic;
    uint32_t checksum;
    uint32_t padding;
    tid_t seq;
    tid_t tid;
    tid_t commit_boundary;
    tid_t complete_boundary;
    uint16_t flags;
    uint16_t nref;
    jblockref ref[ref_size];

    inline bool is_valid_meta() const;
};
enum {
    jf_meta = 0x01,             // this block is a metablock (mandatory)
    jf_error = 0x02,
    jf_corrupt = 0x04,
    jf_start = 0x10,            // metablock starts a txn
    jf_commit = 0x20,           // metablock commits this txn
    jf_complete = 0x40,         // metablock marks this txn as complete
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

    enum jstate {
        js_complete, js_committable, js_invalid_committed, js_uncommitted
    };


    journalreplayer();
    virtual ~journalreplayer();

    bool analyze(unsigned char* jd, unsigned nb);
    void run();


    // The following are callbacks called by `run()`.

    // Report an error at journal block index `bi`.
    virtual void error(unsigned bi, const char* text);
    // Write the data in `buf` to block number `bn`.
    virtual void write_block(blocknum_t bn, unsigned char* buf);
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
