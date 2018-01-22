#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <getopt.h>
#include <inttypes.h>
#include <vector>
#include <deque>
#include <unordered_set>
#include <algorithm>
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


static constexpr size_t blocksize = chkfs::blocksize;
static constexpr size_t inodesize = sizeof(chkfs::inode);
using blocknum_t = chkfs::blocknum_t;
using inum_t = chkfs::inum_t;

static unsigned char* data;
static unsigned char* fbb;
static chkfs::superblock sb;

enum blocktype {
    bunused = 0, bsuperblock, bswap, bfbb, binode,
    bjournal, bfree, bdirectory, bdata, bindirect, bindirect2
};

static const char* const typenames[] = {
    "unused", "superblock", "swap", "fbb", "inode",
    "journal", "free", "directory", "data", "indirect", "indirect2"
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
    inline chkfs::inode* get_inode() const;
    void visit(const char* ref);
    void scan();
    void finish_visit();
    void visit_data(blocknum_t b, size_t idx, size_t sz);
    void visit_indirect(blocknum_t b, size_t idx, size_t sz);
    void visit_indirect2(blocknum_t b, size_t idx, size_t sz);
    void visit_directory_data(blocknum_t b, size_t pos, size_t sz);
    unsigned char* get_data_block(unsigned bi);
    inum_t lookup(const char* name);
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
inline chkfs::inode* inodeinfo::get_inode() const {
    return reinterpret_cast<chkfs::inode*>
        (data + sb.inode_bn * blocksize
         + get_inum() * sizeof(chkfs::inode));
}

void inodeinfo::visit(const char* ref) {
    ++visits_;

    if (this == inodes.data()) {
        eprintf("%s: refers to inode number 0\n", ref);
    } else if (visits_ == 1) {
        chkfs::inode* in = get_inode();
        type_ = bdata;
        if (from_le(in->type) == chkfs::type_directory) {
            type_ = bdirectory;
        } else if (from_le(in->type) != chkfs::type_regular) {
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
        chkfs::inode* in = get_inode();
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
        if (from_le(in->type) == chkfs::type_directory
            || from_le(in->type) == chkfs::type_regular) {
            type = typenames[type_];
        } else {
            sprintf(typebuf, "<type %d>", from_le(in->type));
            type = typebuf;
        }
        printf("inode %u @%s: size %zu, type %s, nlink %u\n",
               inum, ref_, sz, type, from_le(in->nlink));
    }

    if (sz > chkfs::maxindirect2size) {
        eprintf("inode %u @%s: size %zu too big (max %zu)\n",
                inum, ref_, sz, chkfs::maxindirect2size);
    }
    if (type_ == bdirectory) {
        contents_ = new std::unordered_set<std::string>();
        if (sz % sizeof(chkfs::dirent) != 0) {
            eprintf("inode %u @%s: directory size %zu not multiple of %zu\n",
                    inum, ref_, sz, sizeof(chkfs::dirent));
        }
    }

    for (size_t i = 0; i != chkfs::ndirect; ++i) {
        visit_data(from_le(in->direct[i]), i, sz);
    }
    visit_indirect(from_le(in->indirect), chkfs::ndirect, sz);
    visit_indirect2(from_le(in->indirect2),
                    chkfs::ndirect + chkfs::nindirect, sz);

    delete contents_;
}

void inodeinfo::visit_data(blocknum_t b, size_t idx, size_t sz) {
    if (b != 0) {
        if (verbose) {
            printf("  [%zu]: data block %u\n", idx, b);
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
    chkfs::dirent* dir = reinterpret_cast<chkfs::dirent*>
        (data + b * blocksize);
    size_t doff = pos / sizeof(*dir);
    for (size_t i = 0;
         i != blocksize / sizeof(*dir)
             && pos + (i + 1) * sizeof(*dir) <= sz;
         ++i) {
        inum_t inum = from_le(dir[i].inum);
        if (inum != 0) {
            size_t namelen = strnlen(dir[i].name, chkfs::maxnamelen + 1);
            if (namelen == 0) {
                eprintf("inode %u @%s [%u]: dirent #%zu empty name\n",
                        get_inum(), ref_, b, doff + i);
            } else if (namelen > chkfs::maxnamelen) {
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
            printf("  [%zu]: indirect block %u\n", idx, b);
        }
        if (idx * blocksize >= sz) {
            ewprintf("inode %u @%s [%zu]: warning: dangling indirect block reference\n",
                     get_inum(), ref_, idx);
        }
        if (b < blockinfo::blocks.size()) {
            blockinfo::blocks[b].visit(bindirect, ref_, idx);
            blocknum_t* bdata = reinterpret_cast<blocknum_t*>
                (data + b * blocksize);
            for (size_t i = 0; i != chkfs::nindirect; ++i) {
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
                     idx == chkfs::ndirect ? "missing indirect block"
                     : "hole in file");
        }
    }
}

void inodeinfo::visit_indirect2(blocknum_t b, size_t idx, size_t sz) {
    if (b != 0) {
        if (verbose) {
            printf("  [%zu]: indirect2 block %u\n", idx, b);
        }
        if (idx * blocksize >= sz) {
            ewprintf("inode %u @%s [%zu]: warning: dangling indirect2 block reference\n",
                     get_inum(), ref_, idx);
        }
        if (b < blockinfo::blocks.size()) {
            blockinfo::blocks[b].visit(bindirect2, ref_, idx);
            blocknum_t* bdata = reinterpret_cast<blocknum_t*>
                (data + b * blocksize);
            for (size_t i = 0; i != chkfs::nindirect; ++i) {
                visit_indirect(from_le(bdata[i]), idx + i * chkfs::nindirect, sz);
            }
        } else {
            eprintf("inode %u @%s [%zu]: block number %u out of range\n",
                    get_inum(), ref_, idx, b);
        }
    } else {
        if (idx * blocksize < sz) {
            ewprintf("inode %u @%s [%zu]: warning: %s\n",
                     get_inum(), ref_, idx,
                     idx == chkfs::ndirect + chkfs::nindirect
                     ? "missing indirect2 block"
                     : "hole in file");
        }
    }
}


unsigned char* inodeinfo::get_data_block(unsigned bi) {
    auto in = get_inode();

    blocknum_t indir;
    if (bi >= chkfs::ndirect + chkfs::nindirect) {
        if (in->indirect2 < sb.data_bn
            || in->indirect2 >= sb.journal_bn) {
            return nullptr;
        }
        blocknum_t* indir2d = reinterpret_cast<blocknum_t*>
            (data + in->indirect2 * blocksize);
        indir = indir2d[chkfs::bi_indirect_index(bi)];
    } else if (bi >= chkfs::ndirect) {
        indir = in->indirect;
    } else {
        indir = 0;
    }

    blocknum_t dir;
    if (bi >= chkfs::ndirect) {
        if (indir < sb.data_bn
            || indir >= sb.journal_bn) {
            return nullptr;
        }
        blocknum_t* indird = reinterpret_cast<blocknum_t*>
            (data + indir * blocksize);
        dir = indird[chkfs::bi_direct_index(bi)];
    } else {
        dir = in->direct[bi];
    }

    if (dir < sb.data_bn
        || dir >= sb.journal_bn) {
        return nullptr;
    } else {
        return data + dir * blocksize;
    }
}

inum_t inodeinfo::lookup(const char* name) {
    auto in = get_inode();
    assert(in->type == chkfs::type_directory);
    for (size_t off = 0; off < in->size; off += blocksize) {
        auto dirsd = get_data_block(off / blocksize);
        for (unsigned x = 0;
             x < blocksize && off + x < in->size;
             x += chkfs::direntsize) {
            auto de = reinterpret_cast<chkfs::dirent*>(dirsd + x);
            if (de->inum
                && strncmp(de->name, name, chkfs::maxnamelen) == 0) {
                return de->inum;
            }
        }
    }
    return 0;
}


struct ujournalreplayer : public chkfs::journalreplayer {
    unsigned char* disk_;


    ujournalreplayer(unsigned char* disk);

    void message(unsigned bi, const char* format, ...) override;
    void error(unsigned bi, const char* format, ...) override;
    void write_block(uint16_t tid, unsigned bn, unsigned char* buf) override;
    void write_replay_complete() override;
};

ujournalreplayer::ujournalreplayer(unsigned char* disk) {
    disk_ = disk;
}

void ujournalreplayer::message(unsigned bi, const char* format, ...) {
    if (verbose) {
        va_list val;
        va_start(val, format);
        printf("journal: ");
        if (bi != -1U) {
            printf("block %u/%u: ", bi, sb.njournal);
        }
        vprintf(format, val);
        printf("\n");
        va_end(val);
    }
}

void ujournalreplayer::error(unsigned bi, const char* format, ...) {
    printf("journal: ");
    if (bi != -1U) {
        printf("block %u/%u: ", bi, sb.njournal);
    }
    va_list val;
    va_start(val, format);
    vprintf(format, val);
    printf("\n");
    va_end(val);
    ++nerrors;
}

void ujournalreplayer::write_block(uint16_t tid, unsigned bn, unsigned char* buf) {
    if (verbose) {
        printf("journal transaction %u: replaying block %u\n", tid, bn);
    }
    memcpy(disk_ + bn * blocksize, buf, blocksize);
}

void ujournalreplayer::write_replay_complete() {
    memset(disk_ + sb.journal_bn * blocksize, 0,
           (sb.nblocks - sb.journal_bn) * blocksize);
    msync(disk_, blocksize * sb.journal_bn, MS_ASYNC);
}


static void replay_journal() {
    // copy journal
    size_t jsz = sb.njournal * blocksize;
    unsigned char* jcopy = new unsigned char[jsz];
    memcpy(jcopy, data + sb.journal_bn * blocksize, jsz);

    // replay it
    ujournalreplayer ujr(data);
    if (ujr.analyze(jcopy, sb.nblocks - sb.journal_bn)) {
        ujr.run();
    }
}


static void __attribute__((noreturn)) usage() {
    fprintf(stderr, "Usage: chickadeefsck [-V] [-s | --no-journal] [IMAGE]\n");
    exit(2);
}

static void __attribute__((noreturn)) help() {
    printf("Usage: chickadeefsck [-V] [-s | --no-journal] [-e FILE] [IMAGE]\n\
Check the ChickadeeFS IMAGE for errors and exit with a status code\n\
indicating success.\n\
\n\
  --verbose, -V          print information about IMAGE\n\
  --extract, e FILE      print FILE to stdout\n\
  --save-journal, -s     replay journal into IMAGE\n\
  --no-journal           do not replay journal before checking image\n\
  --help                 display this help and exit\n");
    exit(0);
}

static struct option options[] = {
    { "verbose", no_argument, nullptr, 'V' },
    { "save", no_argument, nullptr, 's' },
    { "save-journal", no_argument, nullptr, 's' },
    { "no-journal", no_argument, nullptr, 'x' },
    { "extract", required_argument, nullptr, 'e' },
    { "help", no_argument, nullptr, 'h' },
    { nullptr, 0, nullptr, 0 }
};

int main(int argc, char** argv) {
    bool replay = false;
    bool no_journal = false;
    const char* extract = nullptr;

    int opt;
    while ((opt = getopt_long(argc, argv, "Vse:", options, nullptr)) != -1) {
        switch (opt) {
        case 'V':
            verbose = true;
            break;
        case 's':
            replay = true;
            break;
        case 'x':
            no_journal = true;
            break;
        case 'e':
            if (extract) {
                usage();
            }
            extract = optarg;
            break;
        case 'h':
            help();
        default:
            usage();
        }
    }
    if ((optind != argc && optind + 1 != argc)
        || (replay && no_journal)) {
        usage();
    }

    // open and read disk image
    const char* filename = "<stdin>";
    int fd = STDIN_FILENO;
    if (optind + 1 == argc && strcmp(argv[optind], "-") != 0) {
        filename = argv[optind];
        fd = open(filename, replay ? O_RDWR : O_RDONLY);
        if (fd == -1) {
            fprintf(stderr, "%s: %s\n", filename, strerror(errno));
            exit(2);
        }
    }
    if (isatty(fd)) {
        fprintf(stderr, "%s: Is a terminal\n", filename);
        usage();
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
            fprintf(stderr, "%s: %s (cannot save journal)\n",
                    filename, strerror(errno));
            exit(2);
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
                exit(2);
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
    memcpy(&sb, &data[chkfs::superblock_offset], sizeof(sb));
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
    if (sb.magic != chkfs::magic) {
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
    size_t nfbb = (sb.nblocks + chkfs::bitsperblock - 1)
        / chkfs::bitsperblock;
    if (sb.fbb_bn + nfbb != sb.inode_bn) {
        eprintf("unexpected inode_bn %u (expected %u)\n",
                sb.inode_bn, sb.fbb_bn + nfbb);
    }
    if (sb.ninodes < 10) {
        eprintf("expected at least 10 inodes (have %d)\n", sb.ninodes);
    }
    size_t inodeperblock = blocksize / sizeof(chkfs::inode);
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
    if (sb.journal_bn < sb.nblocks && !no_journal) {
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
    for (chkfs::inum_t inum = 0; inum != sb.ninodes; ++inum) {
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

    // print file
    if (extract) {
        inum_t i = inodeinfo::inodes[1].lookup(extract);
        if (i > 0 && i < sb.ninodes) {
            auto& ini = inodeinfo::inodes[i];
            auto in = ini.get_inode();
            unsigned char* zeros = new unsigned char[blocksize];
            memset(zeros, 0, blocksize);
            for (size_t off = 0; off < in->size; ) {
                size_t delta = std::min(blocksize, in->size - off);
                auto d = ini.get_data_block(off / blocksize);
                fwrite(d ? d : zeros, 1, delta, stdout);
                off += delta;
            }
            delete[] zeros;
        } else {
            ewprintf("%s: No such file or directory\n", extract);
        }
    }

    exit(nerrors ? 1 : 0);
}
