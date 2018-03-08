#define _LARGEFILE_SOURCE 1
#define _FILE_OFFSET_BITS 64
#include <sys/types.h>
#include <inttypes.h>
#include <fcntl.h>
#include "elf.h"
#include "chickadeefs.hh"
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <errno.h>
#include <assert.h>
#include <vector>
#ifdef __APPLE__
# include <libkern/OSByteOrder.h>
# define htole16(x) OSSwapHostToLittleInt16((x))
# define htole32(x) OSSwapHostToLittleInt32((x))
# define htole64(x) OSSwapHostToLittleInt64((x))
# define le16toh(x) OSSwapLittleToHostInt16((x))
# define le32toh(x) OSSwapLittleToHostInt32((x))
# define le64toh(x) OSSwapLittleToHostInt64((x))
#else
# include <endian.h>
#endif
#if defined(_MSDOS) || defined(_WIN32)
# include <fcntl.h>
# include <io.h>
#endif

static constexpr size_t blocksize = chickadeefs::blocksize;
static constexpr size_t inodesize = sizeof(chickadeefs::inode);
using blocknum_t = chickadeefs::blocknum_t;

static chickadeefs::superblock sb;
static unsigned char** blocks;
static unsigned freeb;
static unsigned freeinode;
static std::vector<chickadeefs::dirent> root;

inline uint32_t to_le(uint32_t x) {
    return htole32(x);
}
inline uint64_t to_le(uint64_t x) {
    return htole64(x);
}
inline uint32_t from_le(uint32_t x) {
    return le32toh(x);
}
inline uint64_t from_le(uint64_t x) {
    return le64toh(x);
}


inline void mark_free(blocknum_t bnum) {
    unsigned char* b = blocks[sb.fbb_bn];
    b[bnum / 8] |= 1U << (bnum % 8);
}
inline void mark_allocated(blocknum_t bnum) {
    unsigned char* b = blocks[sb.fbb_bn];
    b[bnum / 8] &= ~(1U << (bnum % 8));
}


FILE* fopencheck(const char* name) {
    FILE *f = strcmp(name, "-") == 0 ? stdin : fopen(name, "rb");
    if (!f) {
        fprintf(stderr, "%s: %s\n", name, strerror(errno));
        exit(1);
    }
    return f;
}
void fclosecheck(FILE* f) {
    if (f != stdin) {
        fclose(f);
    }
}


static void add_boot_sector(const char* path) {
    FILE* f = fopencheck(path);
    ssize_t n = fread(blocks[0], 1, blocksize, f);
    if (n > 510) {
        fprintf(stderr, "%s: boot sector too large: %s%u bytes (max 510)\n",
                path, (n == blocksize ? ">= " : ""), (unsigned) n);
        exit(1);
    }
    fclosecheck(f);

    // boot sector signature
    blocks[0][510] = 0x55;
    blocks[0][511] = 0xAA;
}

static void advance_blockno(const char* purpose) {
    if (freeb >= sb.nblocks) {
        fprintf(stderr, "%s: out of space on output disk\n", purpose);
        exit(1);
    }
    ++freeb;
}

static chickadeefs::inum_t
add_inode(chickadeefs::inum_t inum,
          unsigned type, size_t sz, unsigned nlink,
          blocknum_t first_block, const char* path,
          bool indirect_at_end = false) {
    assert(freeb >= first_block + (sz + blocksize - 1) / blocksize);

    if (inum == 0) {
        if (freeinode == sb.ninodes) {
            fprintf(stderr, "%s: out of inodes on output disk\n", path);
            exit(1);
        }
        inum = freeinode;
        ++freeinode;
    }

    chickadeefs::inode* ino = reinterpret_cast<chickadeefs::inode*>
        (&blocks[0][sb.inode_bn * blocksize + inum * inodesize]);
    ino->type = to_le(type);
    ino->size = to_le(sz);
    ino->nlink = to_le(nlink);

    // allocate indirect block
    if (sz > chickadeefs::maxindirectsize) {
        fprintf(stderr, "%s: file too big for indirect block\n", path);
        exit(1);
    }
    blocknum_t* indir = nullptr;
    if (sz > chickadeefs::maxdirectsize) {
        advance_blockno(path);
        indir = new blocknum_t[chickadeefs::nindirect];
        memset(indir, 0, blocksize);
        if (indirect_at_end) {
            ino->indirect = to_le(freeb - 1);
            blocks[freeb - 1] = reinterpret_cast<unsigned char*>(indir);
        } else {
            ++first_block;
            memmove(&blocks[first_block], &blocks[first_block - 1],
                    sizeof(unsigned char*) * (freeb - first_block));
            ino->indirect = to_le(first_block - 1);
            blocks[first_block - 1] = reinterpret_cast<unsigned char*>(indir);
        }
    }

    // assign block pointers
    for (size_t bidx = 0; bidx * blocksize < sz; ++bidx) {
        if (bidx < chickadeefs::ndirect) {
            ino->direct[bidx] = to_le(first_block + bidx);
        } else {
            indir[bidx - chickadeefs::ndirect] = to_le(first_block + bidx);
        }
    }

    return inum;
}

static void add_file(const char* path, const char* name,
                     bool indirect_at_end) {
    FILE* f = fopencheck(path);
    size_t sz = 0;
    uint32_t first_block = freeb;
    while (1) {
        size_t off = sz % blocksize;
        if (off == 0) {
            advance_blockno(path);
        }
        if (!blocks[freeb - 1]) {
            blocks[freeb - 1] = new unsigned char[blocksize];
            memset(blocks[freeb - 1], 0, blocksize);
        }
        size_t n = fread(blocks[freeb - 1] + off, 1, blocksize - off, f);
        if (n == 0) {
            break;
        }
        sz += n;
    }
    if (sz % blocksize == 0) {
        --freeb;
    }
    if (ferror(f)) {
        fprintf(stderr, "%s: %s\n", path, strerror(errno));
        exit(1);
    }
    fclosecheck(f);

    // allocate inode
    uint32_t ino = add_inode
        (0, chickadeefs::type_regular, sz, 1, first_block, path,
         indirect_at_end);

    // add to directory
    if (strcmp(name, ".") == 0
        || strcmp(name, "..") == 0
        || strchr(name, '/') != nullptr
        || strlen(name) > chickadeefs::maxnamelen) {
        fprintf(stderr, "%s: bad name\n", name);
        exit(1);
    }
    chickadeefs::dirent de;
    memset(&de, 0, sizeof(de));
    de.ino = to_le(ino);
    strcpy(de.name, name);
    root.push_back(std::move(de));
}


static void parse_uint32(const char* arg, uint32_t* val, int opt) {
    unsigned long n;
    char* endptr;
    if (!isdigit((unsigned char) *arg)
        || (n = strtoul(arg, &endptr, 0)) == 0
        || n > 0x7FFFFFFF
        || *endptr
        || *val) {
        fprintf(stderr, "bad `-%c` argument\n", opt);
        exit(1);
    }
    *val = n;
}

int main(int argc, char** argv) {
    uint32_t first_datab = 0;
    const char* bootsector = nullptr;
    const char* outfile = nullptr;

    int opt;
    while ((opt = getopt(argc, argv, "b:i:f:s:o:")) != -1) {
        switch (opt) {
        case 'b':
            parse_uint32(optarg, &sb.nblocks, 'b');
            break;
        case 'i':
            parse_uint32(optarg, &sb.ninodes, 'i');
            break;
        case 'w':
            parse_uint32(optarg, &sb.nswap, 'w');
            break;
        case 'f':
            parse_uint32(optarg, &first_datab, 'f');
            break;
        case 's':
            if (bootsector) {
                fprintf(stderr, "bad `-s` argument\n");
                exit(1);
            }
            bootsector = optarg;
            break;
        case 'o':
            if (outfile) {
                fprintf(stderr, "bad `-o` argument\n");
                exit(1);
            }
            outfile = optarg;
            break;
        default:
            fprintf(stderr, "unknown argument\n");
            exit(1);
        }
    }

    if (sb.nblocks == 0) {
        sb.nblocks = 1024;
    }

    // compute superblock (in host order)
    sb.magic = chickadeefs::magic;
    sb.swap_bn = 1;
    sb.fbb_bn = sb.swap_bn + sb.nswap;
    unsigned nfbb = (sb.nblocks + chickadeefs::bitsperblock - 1)
        / chickadeefs::bitsperblock;
    sb.inode_bn = sb.fbb_bn + nfbb;
    size_t inodeb_ninodes = blocksize / inodesize;
    if (sb.ninodes == 0) {
        if (first_datab && first_datab > sb.inode_bn) {
            sb.ninodes = (first_datab - sb.inode_bn) * inodeb_ninodes;
        } else if (sb.inode_bn <= 3) {
            sb.ninodes = (16 - sb.inode_bn) * inodeb_ninodes;
        } else {
            sb.ninodes = 16 * inodeb_ninodes;
        }
    }
    size_t ninodeb = (sb.ninodes * inodesize + blocksize - 1) / blocksize;
    sb.data_bn = sb.inode_bn + ninodeb;

    if (sb.data_bn > sb.nblocks) {
        fprintf(stderr, "too few blocks, need at least %zu\n",
                size_t(sb.data_bn));
        exit(1);
    }
    if (first_datab && first_datab != sb.data_bn) {
        fprintf(stderr, "expected first data block %zu, computed %zu\n",
                size_t(first_datab), size_t(sb.data_bn));
        exit(1);
    }

    // initialize blocks
    blocks = new unsigned char*[sb.nblocks];
    {
        unsigned char* initb = new unsigned char[blocksize * sb.data_bn];
        memset(initb, 0, blocksize * sb.data_bn);
        for (unsigned i = 0; i != sb.data_bn; ++i) {
            blocks[i] = initb + i * blocksize;
        }
    }
    for (size_t i = sb.data_bn; i != sb.nblocks; ++i) {
        blocks[i] = nullptr;
    }

    // initialize boot sector
    if (bootsector) {
        add_boot_sector(bootsector);
    }

    // initialize superblock
    {
        chickadeefs::superblock sb2;
        sb2.magic = to_le(sb.magic);
        sb2.nblocks = to_le(sb.nblocks);
        sb2.nswap = to_le(sb.nswap);
        sb2.ninodes = to_le(sb.ninodes);
        sb2.swap_bn = to_le(sb.swap_bn);
        sb2.fbb_bn = to_le(sb.fbb_bn);
        sb2.inode_bn = to_le(sb.inode_bn);
        sb2.data_bn = to_le(sb.data_bn);
        memcpy(&blocks[0][512], &sb2, sizeof(sb2));
    }

    // starting point
    freeb = sb.data_bn;
    freeinode = 2;

    // read files
    bool is_first = !!first_datab;
    for (; optind < argc; ++optind) {
        char* colon = strchr(argv[optind], ':');
        const char* name = argv[optind];
        if (colon) {
            *colon = 0;
            name = colon + 1;
        } else {
            if (strncmp(name, "obj/p-", 6) == 0) {
                name += 6;
            } else if (strncmp(name, "obj/", 4) == 0) {
                name += 4;
            } else if (strncmp(name, "initfs/", 7) == 0) {
                name += 7;
            }
        }
        add_file(argv[optind], name, is_first);
        is_first = false;
    }

    // add root directory
    chickadeefs::dirent zerodir;
    memset(&zerodir, 0, sizeof(zerodir));
    while (root.size() * sizeof(zerodir) % blocksize != 0
           || root.size() == 0) {
        root.push_back(zerodir);
    }
    size_t sz;
    blocknum_t first_block = freeb;
    for (sz = 0; sz < root.size() * sizeof(zerodir); sz += blocksize) {
        advance_blockno("root directory");
        blocks[freeb - 1] = reinterpret_cast<unsigned char*>(root.data()) + sz;
    }
    add_inode(1, chickadeefs::type_directory, sz, 1, first_block,
              "root directory");

    // mark free blocks
    memset(blocks[sb.fbb_bn], 0xFF, sb.nblocks / 8);
    memset(blocks[sb.fbb_bn], 0, freeb / 8);
    for (blocknum_t b = freeb; b % 8; ++b) {
        mark_free(b);
    }
    for (blocknum_t b = sb.nblocks; b % 8; ++b) {
        mark_free(b);
    }

    // write output
    FILE* f = stdout;
    if (outfile && strcmp(outfile, "-") != 0) {
        f = fopen(outfile, "wb");
        if (!f) {
            fprintf(stderr, "%s: %s\n", outfile, strerror(errno));
            exit(1);
        }
    } else if (!outfile) {
        outfile = "-";
    }
    for (blocknum_t b = 0; b != freeb; ++b) {
        size_t n = fwrite(blocks[b], 1, blocksize, f);
        if (n != blocksize) {
            fprintf(stderr, "%s: %s\n", outfile, strerror(errno));
            exit(1);
        }
    }
    for (blocknum_t b = freeb; b * blocksize < (1U << 19); ++b) {
        char zero[blocksize];
        memset(zero, 0, blocksize);
        size_t n = fwrite(zero, 1, blocksize, f);
        if (n != blocksize) {
            fprintf(stderr, "%s: %s\n", outfile, strerror(errno));
            exit(1);
        }
    }
    if (fclose(f) != 0) {
        fprintf(stderr, "%s: %s\n", outfile, strerror(errno));
        exit(1);
    } else {
        exit(0);
    }
}
