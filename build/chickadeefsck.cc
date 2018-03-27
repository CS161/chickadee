#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <inttypes.h>
#include <vector>
#include <deque>
#include <unordered_set>
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <assert.h>
#include "chickadeefs.hh"
#include "cbyteswap.hh"

static bool verbose = false;
static size_t nerrors = 0;
static size_t nwarnings = 0;

static void eprintf(const char* format, ...) {
    va_list val;
    va_start(val, format);
    vfprintf(stdout, format, val);
    va_end(val);
    ++nerrors;
}

static void ewprintf(const char* format, ...) {
    va_list val;
    va_start(val, format);
    vfprintf(stdout, format, val);
    va_end(val);
    ++nwarnings;
}

static void exprintf(const char* format, ...) {
    va_list val;
    va_start(val, format);
    vfprintf(stdout, format, val);
    va_end(val);
}


static constexpr size_t blocksize = chickadeefs::blocksize;
static constexpr size_t inodesize = sizeof(chickadeefs::inode);
using blocknum_t = chickadeefs::blocknum_t;
using inum_t = chickadeefs::inum_t;

static unsigned char* data;
static unsigned char* fbb;
static chickadeefs::superblock sb;

enum blocktype {
    bunused = 0, bsuperblock, bswap, bfbb, binode,
    bjournal, bfree, bdirectory, bregular, bindirect, bindirect2
};

static const char* const typenames[] = {
    "unused", "superblock", "swap", "fbb", "inode",
    "journal", "free", "directory", "regular", "indirect", "indirect2"
};

struct blockinfo {
    int type_;
    const char* ref_;
    size_t blockidx_;

    static std::vector<blockinfo> blocks;

    blockinfo()
        : type_(bunused), ref_(nullptr) {
    }
    blocknum_t bnum() const {
        if (this >= blocks.data() && this < blocks.data() + blocks.size()) {
            return this - blocks.data();
        } else {
            return -1;
        }
    }
    void visit(int type, const char* ref, size_t blockidx = -1) {
        auto bn = bnum();
        if (type_ != bunused) {
            eprintf("block %u: reusing block for %s%s as %s\n",
                    bn, ref, unparse_blockidx(blockidx), typenames[type]);
            exprintf("block %u: originally used for %s%s as %s\n",
                     bn, ref_, unparse_blockidx(blockidx_), typenames[type_]);
        } else {
            type_ = type;
            ref_ = ref;
            blockidx_ = blockidx;
            if (fbb[bn / 8] & (1 << (bn % 8))) {
                eprintf("block %u @%s (%s): used block is marked free\n",
                        bn, ref, typenames[type]);
            }
        }
    }
    static const char* unparse_blockidx(size_t blockidx) {
        static char buf[40];
        if (blockidx == size_t(-1)) {
            return "";
        } else {
            sprintf(buf, "[%zu]", blockidx);
            return buf;
        }
    }
};

std::vector<blockinfo> blockinfo::blocks;


struct inodeinfo {
    unsigned visits_;
    int type_;
    const char* ref_;
    std::unordered_set<std::string>* contents_;

    static std::vector<inodeinfo> inodes;

    inodeinfo()
        : visits_(0), type_(bunused), ref_(nullptr), contents_(nullptr) {
    }
    inline inum_t get_inum() const;
    inline chickadeefs::inode* get_inode() const;
    void visit(const char* ref);
    void scan();
    void finish_visit();
    void visit_data(blocknum_t b, size_t idx, size_t sz);
    void visit_indirect(blocknum_t b, size_t idx, size_t sz);
    void visit_indirect2(blocknum_t b, size_t idx, size_t sz);
    void visit_directory_data(blocknum_t b, size_t pos, size_t sz);
};

std::vector<inodeinfo> inodeinfo::inodes;
std::deque<inodeinfo*> inodeq;

void clear_inodeq() {
    while (!inodeq.empty()) {
        auto x = inodeq.front();
        inodeq.pop_front();
        x->finish_visit();
    }
}


inline inum_t inodeinfo::get_inum() const {
    assert(this >= inodes.data() && this < inodes.data() + inodes.size());
    return this - inodes.data();
}
inline chickadeefs::inode* inodeinfo::get_inode() const {
    return reinterpret_cast<chickadeefs::inode*>
        (data + sb.inode_bn * blocksize
         + get_inum() * sizeof(chickadeefs::inode));
}

void inodeinfo::visit(const char* ref) {
    ++visits_;

    if (this == inodes.data()) {
        eprintf("%s: refers to inode number 0\n", ref);
    } else if (visits_ == 1) {
        chickadeefs::inode* in = get_inode();
        type_ = bregular;
        if (from_le(in->type) == chickadeefs::type_directory) {
            type_ = bdirectory;
        } else if (from_le(in->type) != chickadeefs::type_regular) {
            eprintf("inode %u @%s: unknown type %u\n", get_inum(), ref,
                    from_le(in->type));
        }
        ref_ = ref;
        inodeq.push_back(this);
    } else if (type_ == bdirectory) {
        eprintf("inode %u @%s: more than one link to directory\n",
                get_inum(), ref_);
        exprintf("inode %u @%s: link #%u from %s\n",
                 get_inum(), ref_, visits_, ref);
    }
}

void inodeinfo::scan() {
    if (!visits_) {
        chickadeefs::inode* in = get_inode();
        if (from_le(in->type) != 0) {
            eprintf("inode %u: lost inode appears live\n", get_inum());
            visit("lost inode");
            clear_inodeq();
        }
    }
}

void inodeinfo::finish_visit() {
    auto inum = get_inum();
    auto in = get_inode();
    size_t sz = from_le(in->size);

    if (verbose) {
        const char* type;
        char typebuf[20];
        if (from_le(in->type) == chickadeefs::type_directory
            || from_le(in->type) == chickadeefs::type_regular) {
            type = typenames[type_];
        } else {
            sprintf(typebuf, "<type %d>", from_le(in->type));
            type = typebuf;
        }
        printf("inode %u @%s: size %zu, type %s, nlink %u\n",
               inum, ref_, sz, type, from_le(in->nlink));
    }

    if (sz > chickadeefs::maxindirect2size) {
        eprintf("inode %u @%s: size %zu too big (max %zu)\n",
                inum, ref_, sz, chickadeefs::maxindirect2size);
    }
    if (type_ == bdirectory) {
        contents_ = new std::unordered_set<std::string>();
        if (sz % sizeof(chickadeefs::dirent) != 0) {
            eprintf("inode %u @%s: directory size %zu not multiple of %zu\n",
                    inum, ref_, sz, sizeof(chickadeefs::dirent));
        }
    }

    for (size_t i = 0; i != chickadeefs::ndirect; ++i) {
        visit_data(from_le(in->direct[i]), i, sz);
    }
    visit_indirect(from_le(in->indirect), chickadeefs::ndirect, sz);
    visit_indirect2(from_le(in->indirect2),
                    chickadeefs::ndirect + chickadeefs::nindirect, sz);

    delete contents_;
}

void inodeinfo::visit_data(blocknum_t b, size_t idx, size_t sz) {
    if (b != 0) {
        if (verbose) {
            ewprintf("  [%zu]: data block %u\n", idx, b);
        }
        if (idx * blocksize >= sz) {
            ewprintf("inode %u @%s [%zu]: warning: dangling block reference\n",
                     get_inum(), ref_, idx);
        }
        if (b < blockinfo::blocks.size()) {
            blockinfo::blocks[b].visit(type_, ref_, idx);
            if (type_ == bdirectory) {
                visit_directory_data(b, idx * blocksize, sz);
            }
        } else {
            eprintf("inode %u @%s [%zu]: block number %u out of range\n",
                    get_inum(), ref_, idx, b);
        }
    } else {
        if (idx * blocksize < sz) {
            ewprintf("inode %u @%s [%zu]: warning: hole in file\n",
                     get_inum(), ref_, idx);
        }
    }
}

void inodeinfo::visit_directory_data(blocknum_t b, size_t pos, size_t sz) {
    chickadeefs::dirent* dir = reinterpret_cast<chickadeefs::dirent*>
        (data + b * blocksize);
    size_t doff = pos / sizeof(*dir);
    for (size_t i = 0;
         i != blocksize / sizeof(*dir)
             && pos + (i + 1) * sizeof(*dir) <= sz;
         ++i) {
        inum_t inum = from_le(dir[i].inum);
        if (inum != 0) {
            size_t namelen = strnlen(dir[i].name, chickadeefs::maxnamelen + 1);
            if (namelen == 0) {
                eprintf("inode %u @%s [%u]: dirent #%zu empty name\n",
                        get_inum(), ref_, b, doff + i);
            } else if (namelen > chickadeefs::maxnamelen) {
                eprintf("inode %u @%s [%u]: dirent #%zu name too long\n",
                        get_inum(), ref_, b, doff + i);
                exprintf("inode %u @%s [%u]: name is \"%.*s\"\n",
                         get_inum(), ref_, b, int(namelen), dir[i].name);
                dir[i].name[namelen - 1] = '\0';
            }
            std::string name(dir[i].name, namelen);
            if (name == "."
                || name == ".."
                || name.find('/') != name.npos) {
                eprintf("inode %u @%s [%u]: dirent #%zu name \"%s\" reserved\n",
                        get_inum(), ref_, b, doff + i, name.c_str());
            }

            if (verbose) {
                printf("    #%zu \"%s\": inode %u\n",
                       doff + i, name.c_str(), inum);
            }

            if (contents_->count(name)) {
                eprintf("inode %u @%s [%u]: dirent #%zu reuses name \"%s\"\n",
                        get_inum(), ref_, b, doff + i, name.c_str());
            } else {
                contents_->insert(name);
            }
            if (inum < sb.ninodes) {
                inodeinfo::inodes[inum].visit(dir[i].name);
            } else {
                eprintf("inode %u @%s [%u]: directory entry #%zu inode %u out of range\n",
                        get_inum(), ref_, b, doff + i, inum);
            }
        }
    }
}

void inodeinfo::visit_indirect(blocknum_t b, size_t idx, size_t sz) {
    if (b != 0) {
        if (verbose) {
            ewprintf("  [%zu]: indirect block %u\n", idx, b);
        }
        if (idx * blocksize >= sz) {
            ewprintf("inode %u @%s [%zu]: warning: dangling indirect block reference\n",
                     get_inum(), ref_, idx);
        }
        if (b < blockinfo::blocks.size()) {
            blockinfo::blocks[b].visit(bindirect, ref_, idx);
            blocknum_t* bdata = reinterpret_cast<blocknum_t*>
                (data + b * blocksize);
            for (size_t i = 0; i != chickadeefs::nindirect; ++i) {
                visit_data(from_le(bdata[i]), idx + i, sz);
            }
        } else {
            eprintf("inode %u @%s [%zu]: block number %u out of range\n",
                    get_inum(), ref_, idx, b);
        }
    } else {
        if (idx * blocksize < sz) {
            ewprintf("inode %u @%s [%zu]: warning: %s\n",
                     get_inum(), ref_, idx,
                     idx == chickadeefs::ndirect ? "missing indirect block"
                     : "hole in file");
        }
    }
}

void inodeinfo::visit_indirect2(blocknum_t b, size_t idx, size_t sz) {
    if (b != 0) {
        if (verbose) {
            ewprintf("  [%zu]: indirect2 block %u\n", idx, b);
        }
        if (idx * blocksize >= sz) {
            ewprintf("inode %u @%s [%zu]: warning: dangling indirect2 block reference\n",
                     get_inum(), ref_, idx);
        }
        if (b < blockinfo::blocks.size()) {
            blockinfo::blocks[b].visit(bindirect2, ref_, idx);
            blocknum_t* bdata = reinterpret_cast<blocknum_t*>
                (data + b * blocksize);
            for (size_t i = 0; i != chickadeefs::nindirect; ++i) {
                visit_indirect(from_le(bdata[i]), idx + i * chickadeefs::nindirect, sz);
            }
        } else {
            eprintf("inode %u @%s [%zu]: block number %u out of range\n",
                    get_inum(), ref_, idx, b);
        }
    } else {
        if (idx * blocksize < sz) {
            ewprintf("inode %u @%s [%zu]: warning: %s\n",
                     get_inum(), ref_, idx,
                     idx == chickadeefs::ndirect + chickadeefs::nindirect
                     ? "missing indirect2 block"
                     : "hole in file");
        }
    }
}


struct ujournalreplayer : public chickadeefs::journalreplayer {
    unsigned char* disk_;


    ujournalreplayer(unsigned char* disk);

    void error(unsigned bi, const char* text) override;
    void write_block(unsigned bn, unsigned char* buf) override;
    void write_replay_complete() override;
};

ujournalreplayer::ujournalreplayer(unsigned char* disk) {
    disk_ = disk;
}

void ujournalreplayer::error(unsigned bi, const char* text) {
    eprintf("journal block %u/%u: %s\n", bi, sb.nblocks - sb.journal_bn,
            text);
}

void ujournalreplayer::write_block(unsigned bn, unsigned char* buf) {
    memcpy(disk_ + bn * blocksize, buf, blocksize);
}

void ujournalreplayer::write_replay_complete() {
    memset(disk_ + sb.journal_bn * blocksize, 0,
           (sb.nblocks - sb.journal_bn) * blocksize);
    msync(disk_, blocksize * sb.journal_bn, MS_ASYNC);
}


static void replay_journal() {
    // copy journal
    size_t jsz = (sb.nblocks - sb.journal_bn) * blocksize;
    unsigned char* jcopy = new unsigned char[jsz];
    memcpy(jcopy, data + sb.nblocks * blocksize, jsz);

    // replay it
    ujournalreplayer ujr(data);
    if (ujr.analyze(jcopy, sb.nblocks - sb.journal_bn)) {
        ujr.run();
    }
}


static void usage() {
    fprintf(stderr, "Usage: chickadeefsck [-V] [-r] [IMAGE]\n");
    exit(1);
}

int main(int argc, char** argv) {
    bool replay = false;

    int opt;
    while ((opt = getopt(argc, argv, "Vr")) != -1) {
        switch (opt) {
        case 'V':
            verbose = true;
            break;
        case 'r':
            replay = true;
            break;
        default:
            usage();
        }
    }
    if (optind != argc && optind + 1 != argc) {
        usage();
    }

    // open and read disk image
    const char* filename = "<stdin>";
    int fd = STDIN_FILENO;
    if (optind + 1 == argc && strcmp(argv[optind], "-") != 0) {
        filename = argv[optind];
        fd = open(filename, O_RDONLY);
        if (fd == -1) {
            fprintf(stderr, "%s: %s\n", filename, strerror(errno));
            exit(1);
        }
    }

    struct stat s;
    int r = fstat(fd, &s);
    assert(r == 0);

    size_t size = s.st_size;
    data = reinterpret_cast<unsigned char*>(MAP_FAILED);
    if (S_ISREG(s.st_mode) && size > 0) {
        data = reinterpret_cast<unsigned char*>
            (mmap(nullptr, size, PROT_READ | PROT_WRITE,
                  replay ? MAP_SHARED : MAP_PRIVATE, fd, 0));
    }
    if (data == reinterpret_cast<unsigned char*>(MAP_FAILED)) {
        if (replay) {
            fprintf(stderr, "can't modify file to replay journal\n");
            exit(1);
        }
        size = 0;
        size_t capacity = 16384;
        data = new unsigned char[capacity];
        while (1) {
            if (size == capacity) {
                capacity *= 2;
                unsigned char* ndata = new unsigned char[capacity];
                memcpy(ndata, data, size);
                delete[] data;
                data = ndata;
            }
            ssize_t r = read(fd, &data[size], capacity - size);
            if (r == 0) {
                break;
            } else if (r == -1 && errno != EAGAIN) {
                fprintf(stderr, "%s: %s\n", filename, strerror(errno));
                exit(1);
            } else if (r > 0) {
                size += r;
            }
        }
    }

    // check superblock
    if (size % blocksize != 0) {
        eprintf("unexpected size %zu is not a multiple of blocksize %zu\n",
                size, blocksize);
    }
    if (size < blocksize) {
        eprintf("file size %zu too small\n", size);
        exit(1);
    }
    memcpy(&sb, &data[chickadeefs::superblock_offset], sizeof(sb));
    sb.magic = from_le(sb.magic);
    sb.nblocks = from_le(sb.nblocks);
    sb.nswap = from_le(sb.nswap);
    sb.ninodes = from_le(sb.ninodes);
    sb.njournal = from_le(sb.njournal);
    sb.swap_bn = from_le(sb.swap_bn);
    sb.fbb_bn = from_le(sb.fbb_bn);
    sb.inode_bn = from_le(sb.inode_bn);
    sb.data_bn = from_le(sb.data_bn);
    sb.journal_bn = from_le(sb.journal_bn);
    if (sb.magic != chickadeefs::magic) {
        eprintf("bad magic number 0x%" PRIX64 "\n", sb.magic);
    }
    if (sb.nblocks <= 2 || sb.nblocks >= 0x10000000) {
        eprintf("bad number of blocks %u\n", sb.nblocks);
    }
    if (sb.swap_bn != 1) {
        eprintf("unexpected swap_bn %u (expected %u)\n",
                sb.swap_bn, 1);
    }
    if (sb.swap_bn + sb.nswap > sb.nblocks) {
        eprintf("too many swap blocks %u\n", sb.nswap);
    }
    if (sb.swap_bn + sb.nswap != sb.fbb_bn) {
        eprintf("unexpected fbb_bn %u (expected %u)\n",
                sb.fbb_bn, sb.swap_bn + sb.nswap);
    }
    size_t nfbb = (sb.nblocks + chickadeefs::bitsperblock - 1)
        / chickadeefs::bitsperblock;
    if (sb.fbb_bn + nfbb != sb.inode_bn) {
        eprintf("unexpected inode_bn %u (expected %u)\n",
                sb.inode_bn, sb.fbb_bn + nfbb);
    }
    if (sb.ninodes < 10) {
        eprintf("expected at least 10 inodes (have %d)\n", sb.ninodes);
    }
    size_t inodeperblock = blocksize / sizeof(chickadeefs::inode);
    size_t ninodeb = (sb.ninodes + inodeperblock - 1) / inodeperblock;
    if (sb.inode_bn + ninodeb > sb.data_bn) {
        eprintf("unexpected data_bn %u (expected at least %u)\n",
                sb.data_bn, sb.inode_bn + ninodeb);
    }
    if (sb.data_bn >= sb.nblocks) {
        eprintf("data_bn %u too large for disk (nblocks %u)\n",
                sb.data_bn, sb.nblocks);
    }
    if (sb.journal_bn < sb.data_bn || sb.journal_bn > sb.nblocks) {
        eprintf("unexpected journal_bn %u\n", sb.journal_bn);
    }
    if (sb.njournal > sb.nblocks - sb.journal_bn) {
        eprintf("unexpected njournal %u (expected at least %u)\n",
                sb.njournal, sb.nblocks + sb.journal_bn);
    }
    if (nerrors > 0) {
        exit(1);
    }
    fbb = data + sb.fbb_bn * blocksize;

    // check journal
    if (sb.journal_bn < sb.nblocks) {
        replay_journal();
    }

    // mark blocks
    blockinfo::blocks.resize(sb.nblocks);
    blockinfo::blocks[0].visit(bsuperblock, "superblock");
    for (blocknum_t b = sb.swap_bn; b != sb.fbb_bn; ++b) {
        blockinfo::blocks[b].visit(bswap, "swap space", b - sb.swap_bn);
    }
    for (blocknum_t b = sb.fbb_bn; b != sb.inode_bn; ++b) {
        blockinfo::blocks[b].visit(bfbb, "fbb", b - sb.fbb_bn);
    }
    for (blocknum_t b = sb.inode_bn; b != sb.data_bn; ++b) {
        blockinfo::blocks[b].visit(binode, "inode", b - sb.inode_bn);
    }
    for (blocknum_t b = sb.journal_bn; b != sb.nblocks; ++b) {
        blockinfo::blocks[b].visit(bjournal, "journal", b - sb.journal_bn);
    }

    // mark inodes
    inodeinfo::inodes.resize(sb.ninodes);

    // visit root directory
    inodeinfo::inodes[1].visit("root directory");
    clear_inodeq();

    // check all inodes
    for (chickadeefs::inum_t inum = 0; inum != sb.ninodes; ++inum) {
        inodeinfo::inodes[inum].scan();
    }

    // check link counts
    // XXX

    // check for garbage
    for (size_t b = sb.data_bn; b != sb.journal_bn; ++b) {
        if (!(fbb[b / 8] & (1 << (b % 8)))
            && blockinfo::blocks[b].type_ == bunused) {
            ewprintf("block %u: unreferenced block is marked allocated\n", b);
        }
    }

    exit(nerrors ? 1 : 0);
}
