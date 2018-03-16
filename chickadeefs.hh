#ifndef CHICKADEE_CHICKADEEFS_HH
#define CHICKADEE_CHICKADEEFS_HH

struct chickadeefs {
    typedef uint32_t blocknum_t;
    typedef uint32_t inum_t;

    static constexpr size_t blocksize = 4096;
    static constexpr size_t bitsperblock = blocksize * 8;
    static constexpr size_t ndirect = 10;
    static constexpr size_t maxdirectsize = ndirect * blocksize;
    static constexpr size_t nindirect = blocksize / sizeof(blocknum_t);
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
        uint32_t reserved;
        blocknum_t direct[ndirect];
        blocknum_t indirect;
        blocknum_t indirect2;
    };

    struct dirent {
        inum_t inum;
        char name[maxnamelen + 1];
    };


    static constexpr size_t njstart_bn = nindirect - 2;
    static constexpr size_t njbn = nindirect - 5;

    static constexpr uint64_t journalmagic = 0xFBBFBB009EEBCEEDUL;

    struct journalheader {
        uint64_t magic;
        blocknum_t jstart_bn[njstart_bn];
    };

    struct jentheader {
        uint64_t magic;
        uint32_t tid;
        uint32_t nblocks;
        uint32_t committed;
        blocknum_t jbn[njbn];
    };

    struct jenttrailer {
        uint32_t completed;
    };
};

#endif
