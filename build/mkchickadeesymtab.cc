#define _LARGEFILE_SOURCE 1
#define _FILE_OFFSET_BITS 64
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <inttypes.h>
#include <fcntl.h>
#include "elf.h"
#include "chickadeefs.hh"
#include "cbyteswap.hh"
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <errno.h>
#include <assert.h>
#include <vector>
#include <random>
#include <algorithm>
#if defined(_MSDOS) || defined(_WIN32)
# include <fcntl.h>
# include <io.h>
#endif

struct elf_info {
    const char* filename_;
    char* data_ = nullptr;
    size_t size_ = 0;
    size_t capacity_ = 0;
    bool changed_ = false;

    bool ok_ = false;
    elf_header* eh_;
    elf_program* pht_;
    elf_section* sht_;
    mutable elf_symbol* symtab_ = nullptr;
    mutable unsigned nsymtab_ = 0;
    mutable const char* symstrtab_;

    void grow(size_t capacity);

    bool validate();
    unsigned find_section(const char* name) const;
    const char* shstrtab() const;
    uint64_t first_offset() const;
    elf_symbol* symtab() const;
    elf_symbol* find_symbol(const char* name, elf_symbol* after = nullptr) const;
    void sort_symtab();

    void shift_sections(unsigned idx, ptrdiff_t diff);
};


void elf_info::grow(size_t capacity) {
    if (capacity > capacity_) {
        capacity = std::max(capacity, capacity_ + 32768);
        char* data = new char[capacity];
        if (capacity_ != 0) {
            memcpy(data, data_, capacity_);
        }
        delete[] data_;
        data_ = data;
        capacity_ = capacity;
        if (ok_) {
            fprintf(stderr, "!\n");
            eh_ = reinterpret_cast<elf_header*>(data_);
            pht_ = reinterpret_cast<elf_program*>(data_ + eh_->e_phoff);
            sht_ = reinterpret_cast<elf_section*>(data_ + eh_->e_shoff);
            symtab_ = nullptr;
            nsymtab_ = 0;
        }
    }
}

bool elf_info::validate() {
    if (ok_) {
        return true;
    }

    eh_ = reinterpret_cast<elf_header*>(data_);
    if (size_ < sizeof(*eh_)
        || eh_->e_magic != ELF_MAGIC) {
        fprintf(stderr, "%s: not an ELF file\n", filename_);
        return false;
    }
    if (eh_->e_phentsize != sizeof(elf_program)
        || eh_->e_shentsize != sizeof(elf_section)) {
        fprintf(stderr, "%s: unexpected component sizes\n", filename_);
        return false;
    }
    if (!eh_->e_phnum || !eh_->e_shnum) {
        fprintf(stderr, "%s: empty components\n", filename_);
        return false;
    }
    pht_ = reinterpret_cast<elf_program*>(data_ + eh_->e_phoff);
    sht_ = reinterpret_cast<elf_section*>(data_ + eh_->e_shoff);
    if (eh_->e_phoff >= size_
        || (size_ - eh_->e_phoff) / sizeof(*pht_) < eh_->e_phnum
        || eh_->e_shoff >= size_
        || (size_ - eh_->e_shoff) / sizeof(*sht_) < eh_->e_shnum) {
        fprintf(stderr, "%s: bad offsets\n", filename_);
        return false;
    }
    uint64_t last_offset = 0;
    for (unsigned i = 0; i != eh_->e_shnum; ++i) {
        auto& sh = sht_[i];
        if (sh.sh_type != ELF_SHT_NULL
            && sh.sh_type != ELF_SHT_NOBITS) {
            if (sh.sh_offset >= size_
                || sh.sh_size > size_ - sh.sh_offset) {
                fprintf(stderr, "%s (section %u): bad offset/size\n",
                        filename_, i);
                return false;
            } else if (sh.sh_offset < last_offset) {
                fprintf(stderr, "%s (section %u): offsets out of order\n",
                        filename_, i);
                return false;
            } else {
                last_offset = sh.sh_offset + sh.sh_size;
            }
        }
    }

    if (eh_->e_shstrndx == 0
        || eh_->e_shstrndx >= eh_->e_shnum
        || sht_[eh_->e_shstrndx].sh_type != ELF_SHT_STRTAB
        || sht_[eh_->e_shstrndx].sh_size == 0) {
        fprintf(stderr, "%s: no section header string table\n", filename_);
        return false;
    }
    char* shstrtab = &data_[sht_[eh_->e_shstrndx].sh_offset];
    size_t shstrtab_size = sht_[eh_->e_shstrndx].sh_size;
    if (shstrtab[0] != 0
        || shstrtab[shstrtab_size - 1] != 0) {
        fprintf(stderr, "%s: bad section header string table\n", filename_);
        return false;
    }
    for (unsigned i = 0; i != eh_->e_shnum; ++i) {
        if (i == 0 && sht_[i].sh_type != ELF_SHT_NULL) {
            fprintf(stderr, "%s: should start with null section\n", filename_);
            return false;
        }
        if (sht_[i].sh_name >= shstrtab_size) {
            fprintf(stderr, "%s <#%u>: bad section name\n",
                    filename_, i);
            return false;
        }
        if (sht_[i].sh_type == ELF_SHT_SYMTAB
            && (sht_[i].sh_link >= eh_->e_shnum
                || sht_[sht_[i].sh_link].sh_type != ELF_SHT_STRTAB)) {
            fprintf(stderr, "%s <%s>: bad linked string table\n",
                    filename_, shstrtab + sht_[i].sh_name);
            return false;
        }
        if (sht_[i].sh_type == ELF_SHT_SYMTAB) {
            elf_symbol* sym = reinterpret_cast<elf_symbol*>
                (data_ + sht_[i].sh_offset);
            size_t size = sht_[i].sh_size / sizeof(*sym);
            size_t strsize = sht_[sht_[i].sh_link].sh_size;
            for (size_t j = 0; j != size; ++j) {
                if (sym[j].st_name >= strsize) {
                    fprintf(stderr, "%s <%s>: symbol name out of range\n",
                            filename_, shstrtab + sht_[i].sh_name);
                    return false;
                }
            }
        }
        if (sht_[i].sh_type == ELF_SHT_STRTAB
            && (sht_[i].sh_size == 0
                || data_[sht_[i].sh_offset] != 0
                || data_[sht_[i].sh_offset + sht_[i].sh_size - 1] != 0)) {
            fprintf(stderr, "%s <%s>: bad string table contents\n",
                    filename_, shstrtab + sht_[i].sh_name);
            return false;
        }
    }

    ok_ = true;
    return true;
}

const char* elf_info::shstrtab() const {
    assert(ok_);
    return &data_[sht_[eh_->e_shstrndx].sh_offset];
}

unsigned elf_info::find_section(const char* name) const {
    assert(ok_);
    auto shstrtab = this->shstrtab();
    for (unsigned i = 0; i != eh_->e_shnum; ++i) {
        if (strcmp(&shstrtab[sht_[i].sh_name], name) == 0) {
            return i;
        }
    }
    return 0;
}

uint64_t elf_info::first_offset() const {
    uint64_t o = ~uint64_t(0);
    for (unsigned i = 0; i != eh_->e_shnum; ++i) {
        if (sht_[i].sh_type != ELF_SHT_NULL
            && sht_[i].sh_type != ELF_SHT_NOBITS) {
            o = std::min(o, sht_[i].sh_offset);
        }
    }
    return o;
}

elf_symbol* elf_info::symtab() const {
    if (!symtab_) {
        unsigned i = find_section(".symtab");
        if (i != 0 && sht_[i].sh_type == ELF_SHT_SYMTAB) {
            symtab_ = reinterpret_cast<elf_symbol*>(data_ + sht_[i].sh_offset);
            nsymtab_ = sht_[i].sh_size / sizeof(elf_symbol);
            auto strtabndx = sht_[i].sh_link;
            symstrtab_ = data_ + sht_[strtabndx].sh_offset;
        }
    }
    return symtab_;
}

void elf_info::sort_symtab() {
    unsigned i = 1;
    symtab();
    while (i < nsymtab_
           && symtab_[i].st_value >= symtab_[i - 1].st_value) {
        ++i;
    }
    if (i < nsymtab_) {
        std::sort(&symtab_[i - 1], &symtab_[nsymtab_],
                  [] (const elf_symbol& a, const elf_symbol& b) {
                      return (a.st_value < b.st_value)
                          || (a.st_value == b.st_value
                              && a.st_size < b.st_size);
                  });
        changed_ = true;
    }
}

elf_symbol* elf_info::find_symbol(const char* name,
                                  elf_symbol* after) const {
    unsigned i = 0;
    if (after) {
        i = (after - symtab_) + 1;
    }
    symtab();
    while (i < nsymtab_
           && strcmp(symstrtab_ + symtab_[i].st_name, name) != 0) {
        ++i;
    }
    if (i >= nsymtab_) {
        return nullptr;
    } else {
        return &symtab_[i];
    }
}

void elf_info::shift_sections(unsigned idx, ptrdiff_t diff) {
    assert(idx < eh_->e_shnum && diff >= 0);

    // find offset shift
    uint64_t soff = sht_[idx].sh_offset;
    for (unsigned i = idx; i != eh_->e_shnum; ++i) {
        if (sht_[i].sh_type != ELF_SHT_NULL
            && sht_[i].sh_type != ELF_SHT_NOBITS) {
            soff = sht_[i].sh_offset;
            break;
        }
    }

    // update program headers
    for (unsigned i = 0; i != eh_->e_phnum; ++i) {
        if (pht_[i].p_offset >= soff) {
            pht_[i].p_offset += diff;
        } else if (pht_[i].p_offset + pht_[i].p_filesz > soff) {
            fprintf(stderr, "%s (program %u): warning: spans alignment boundary\n", filename_, i);
            fprintf(stderr, "  shifting %zu + %zu, program %zu + %zu\n", soff, diff, pht_[i].p_offset, pht_[i].p_filesz);
            pht_[i].p_filesz += diff;
        }
    }

    // update section headers
    for (unsigned i = idx; i != eh_->e_shnum; ++i) {
        if ((sht_[i].sh_type != ELF_SHT_NULL
             && sht_[i].sh_type != ELF_SHT_NOBITS)
            || sht_[i].sh_offset >= soff) {
            sht_[i].sh_offset += diff;
        }
    }

    // move data
    if (diff) {
        grow(size_ + diff);
        memmove(&data_[soff + diff], &data_[soff], size_ - soff);
        memset(&data_[soff], 0, diff);
        size_ += diff;
        changed_ = true;

        // update main offsets
        if (eh_->e_shoff >= soff) {
            eh_->e_shoff += diff;
            sht_ = reinterpret_cast<elf_section*>(data_ + eh_->e_shoff);
        }
    }
}


static void usage() {
    fprintf(stderr, "Usage: mkchickadeesymtab [-s SYMTABREF] [IMAGE]\n");
    exit(1);
}

static unsigned rewrite_symtabref(elf_info& ei, const char* name,
                                  uint64_t& loadaddr, size_t strtab_off,
                                  size_t size) {
    auto sym = ei.find_symbol(name);
    unsigned nfound = 0;
    while (sym) {
        if ((sym->st_info & ELF_STT_MASK) == ELF_STT_OBJECT
            && sym->st_shndx < ei.eh_->e_shnum) {
            if (sym->st_size != sizeof(elf_symtabref)) {
                fprintf(stderr, "%s: `%s` symbol @0x%lx has size %lu (expected %lu)\n",
                        ei.filename_, name, sym->st_value,
                        sym->st_size, sizeof(elf_symtabref));
                exit(1);
            }

            auto& sh = ei.sht_[sym->st_shndx];
            if (sh.sh_type != ELF_SHT_PROGBITS
                || sym->st_value < sh.sh_addr
                || sym->st_value >= sh.sh_addr + sh.sh_size - sizeof(elf_symtabref)) {
                fprintf(stderr, "%s: `%s` symbol @0x%lx bad reference\n",
                        ei.filename_, name, sym->st_value);
                fprintf(stderr, "  %s addresses [0x%lx, 0x%lx)\n",
                        ei.shstrtab() + sh.sh_name,
                        sh.sh_addr, sh.sh_addr + sh.sh_size);
                exit(1);
            }

            size_t stref_off = sh.sh_offset + (sym->st_value - sh.sh_addr);
            if (!loadaddr) {
                memcpy(&loadaddr, ei.data_ + stref_off, sizeof(loadaddr));
            }
            elf_symtabref stref;
            memcpy(&stref, ei.data_ + stref_off, sizeof(stref));

            elf_symtabref xstref = {
                reinterpret_cast<elf_symbol*>(loadaddr),
                ei.nsymtab_,
                reinterpret_cast<char*>(loadaddr + strtab_off),
                size
            };
            if (memcmp(ei.data_ + stref_off, &xstref, sizeof(xstref)) != 0) {
                memcpy(ei.data_ + stref_off, &xstref, sizeof(xstref));
                ei.changed_ = true;
            }
            ++nfound;
        }
        sym = ei.find_symbol(name, sym);
    }
    return nfound;
}

int main(int argc, char** argv) {
    uint64_t loadaddr = 0;
    const char* lsymtab_name = "symtab";
    bool lsymtab_set = false;

    int opt;
    while ((opt = getopt(argc, argv, "a:s:")) != -1) {
        switch (opt) {
        case 'a': {
            char* end;
            loadaddr = strtoul(optarg, &end, 0);
            if (*end != 0) {
                fprintf(stderr, "bad `-a` argument\n");
                exit(1);
            }
            break;
        }
        case 's': {
            lsymtab_name = optarg;
            lsymtab_set = true;
            break;
        }
        default:
            usage();
        }
    }

    if (optind + 2 < argc) {
        usage();
    }

    // open and read ELF file
    elf_info ei;

    ei.filename_ = "<stdin>";
    int fd = STDIN_FILENO;
    if (optind < argc && strcmp(argv[optind], "-") != 0) {
        ei.filename_ = argv[optind];
        fd = open(ei.filename_, O_RDONLY);
        if (fd == -1) {
            fprintf(stderr, "%s: %s\n", ei.filename_, strerror(errno));
            exit(1);
        }
    }

    {
        struct stat s;
        int r = fstat(fd, &s);
        assert(r == 0);
        ei.grow(S_ISREG(s.st_mode) ? (s.st_size + 32767) & ~32767 : 262144);
    }

    while (1) {
        if (ei.size_ == ei.capacity_) {
            ei.grow(ei.capacity_ * 2);
        }
        ssize_t r = read(fd, &ei.data_[ei.size_], ei.capacity_ - ei.size_);
        if (r == 0) {
            break;
        } else if (r == -1 && errno != EAGAIN) {
            fprintf(stderr, "%s: %s\n", ei.filename_, strerror(errno));
            exit(1);
        } else if (r > 0) {
            ei.size_ += r;
        }
    }

    // check contents
    if (!ei.validate()) {
        exit(1);
    }

    // align symbol table
    auto symtabndx = ei.find_section(".symtab");
    if (symtabndx == 0
        || ei.sht_[symtabndx].sh_type != ELF_SHT_SYMTAB) {
        fprintf(stderr, "%s: no .symtab section\n", ei.filename_);
        exit(1);
    }

    {
        auto& symtab = ei.sht_[symtabndx];
        if (symtab.sh_link >= ei.eh_->e_shnum
            || symtab.sh_link != symtabndx + 1
            || ei.sht_[symtab.sh_link].sh_type != ELF_SHT_STRTAB) {
            fprintf(stderr, "%s: .symtab has unexpected links\n", ei.filename_);
            exit(1);
        }

        // align symbol table on a page boundary
        if (symtab.sh_offset & 0xFFF) {
            ei.shift_sections(symtabndx, 0x1000 - (symtab.sh_offset & 0xFFF));
        }
    }

    // figure out allocation range
    uint64_t first_offset = ei.sht_[symtabndx].sh_offset;
    uint64_t strtab_offset = ei.sht_[symtabndx + 1].sh_offset;
    uint64_t last_offset = ei.sht_[symtabndx + 1].sh_offset
        + ei.sht_[symtabndx + 1].sh_size;

    // find `lsymtab_name`
    if (!rewrite_symtabref(ei, lsymtab_name, loadaddr,
                           strtab_offset - first_offset,
                           last_offset - first_offset)
        && lsymtab_set) {
        fprintf(stderr, "%s: no `%s` symbol found\n", ei.filename_, lsymtab_name);
        exit(1);
    }

    // sort symbol table by address
    ei.sort_symtab();

    // mark symbol table as allocated
    if (loadaddr && !(ei.sht_[symtabndx].sh_flags & ELF_SHF_ALLOC)) {
        ei.sht_[symtabndx].sh_flags |= ELF_SHF_ALLOC;
        ei.sht_[symtabndx].sh_addr = loadaddr;
        ei.changed_ = true;
    }
    if (loadaddr && !(ei.sht_[symtabndx + 1].sh_flags & ELF_SHF_ALLOC)) {
        ei.sht_[symtabndx + 1].sh_flags |= ELF_SHF_ALLOC;
        ei.sht_[symtabndx + 1].sh_addr = loadaddr
            + strtab_offset - first_offset;
        ei.changed_ = true;
    }

    // find or add a program header
    if (loadaddr) {
        unsigned i = 0;
        while (i != ei.eh_->e_phnum
               && ei.pht_[i].p_va + ei.pht_[i].p_filesz < loadaddr) {
            ++i;
        }

        bool found = false;
        if (i != ei.eh_->e_phnum
            && ei.pht_[i].p_va == loadaddr
            && ei.pht_[i].p_offset == first_offset
            && ei.pht_[i].p_filesz >= last_offset - first_offset) {
            found = true;
        }

        if (!found) {
            uint64_t offset0 = ei.first_offset();
            if (ei.eh_->e_phoff + (ei.eh_->e_phnum + 1) * sizeof(*ei.pht_)
                    > offset0
                || ei.eh_->e_shoff <= offset0) {
                fprintf(stderr, "%s: unexpected program headers\n", ei.filename_);
                exit(1);
            }
            auto& ph = ei.pht_[i];
            memmove(&ph + 1, &ph, (ei.eh_->e_phnum - i) * sizeof(ph));
            ++ei.eh_->e_phnum;
            ph.p_type = ELF_PTYPE_LOAD;
            ph.p_flags = ELF_PFLAG_READ;
            ph.p_offset = first_offset;
            ph.p_va = ph.p_pa = loadaddr;
            ph.p_filesz = last_offset - first_offset;
            ph.p_memsz = ph.p_filesz;
            ph.p_align = 0x1000;
            ei.changed_ = true;
        }
    }

    // write output
    if (!ei.changed_ && optind + 1 == argc && fd != STDIN_FILENO) {
        exit(0);
    }

    const char* ofn = "-";
    if (optind + 1 == argc) {
        ofn = argv[optind];
    } else if (optind + 2 == argc) {
        ofn = argv[optind + 1];
    }
    int ofd = STDOUT_FILENO;
    if (strcmp(ofn, "-") != 0) {
        ofd = open(ofn, O_WRONLY | O_CREAT | O_TRUNC);
        if (ofd == -1) {
            fprintf(stderr, "%s: %s\n", ofn, strerror(errno));
            exit(1);
        }
    }

    size_t off = 0;
    while (off != ei.size_) {
        ssize_t w = write(ofd, &ei.data_[off], ei.size_ - off);
        if (w > 0) {
            off += w;
        } else if (w == -1 && errno != EINTR && errno != EAGAIN) {
            fprintf(stderr, "%s: %s\n", ofn, strerror(errno));
            exit(1);
        }
    }
    exit(0);
}


static_assert(sizeof(chickadeefs::inode) == chickadeefs::inodesize,
              "inodesize valid");
