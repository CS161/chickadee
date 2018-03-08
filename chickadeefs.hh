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
        inum_t ino;
        char name[maxnamelen + 1];
    };
};

#endif
