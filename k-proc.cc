#include "kernel.hh"
#include "elf.h"
#include "k-vmiter.hh"
#include "k-devices.hh"

proc* ptable[NPROC];            // array of process descriptor pointers
spinlock ptable_lock;           // protects `ptable`


// proc::proc()
//    The constructor initializes the `proc` to empty.

proc::proc()
    : pid_(0), regs_(nullptr), yields_(nullptr),
      state_(blank), pagetable_(nullptr) {
}


// kalloc_proc()
//    Allocate and return a new `proc`. Calls the constructor.

proc* kalloc_proc() {
    void* ptr;
    if (sizeof(proc) <= PAGESIZE) {
        ptr = kallocpage();
    } else {
        ptr = kalloc(sizeof(proc));
    }
    if (ptr) {
        return new (ptr) proc;
    } else {
        return nullptr;
    }
}


// proc::init_user(pid, pt)
//    Initialize this `proc` as a new runnable user process with PID `pid`
//    and initial page table `pt`.

void proc::init_user(pid_t pid, x86_64_pagetable* pt) {
    uintptr_t addr = reinterpret_cast<uintptr_t>(this);
    assert(!(addr & PAGEOFFMASK));
    // ensure layout `k-exception.S` expects
    assert(reinterpret_cast<uintptr_t>(&pid_) == addr);
    assert(reinterpret_cast<uintptr_t>(&regs_) == addr + 8);
    assert(reinterpret_cast<uintptr_t>(&yields_) == addr + 16);
    // ensure initialized page table
    assert(!(reinterpret_cast<uintptr_t>(pt) & PAGEOFFMASK));
    assert(pt->entry[256] == early_pagetable->entry[256]);
    assert(pt->entry[510] == early_pagetable->entry[510]);
    assert(pt->entry[511] == early_pagetable->entry[511]);

    pid_ = pid;

    regs_ = reinterpret_cast<regstate*>(addr + KTASKSTACK_SIZE) - 1;
    memset(regs_, 0, sizeof(regstate));
    regs_->reg_cs = SEGSEL_APP_CODE | 3;
    regs_->reg_fs = SEGSEL_APP_DATA | 3;
    regs_->reg_gs = SEGSEL_APP_DATA | 3;
    regs_->reg_ss = SEGSEL_APP_DATA | 3;
    regs_->reg_rflags = EFLAGS_IF;

    yields_ = nullptr;

    state_ = proc::runnable;

    pagetable_ = pt;
}


// proc::init_kernel(pid)
//    Initialize this `proc` as a new kernel process with PID `pid`,
//    starting at function `f`.

void proc::init_kernel(pid_t pid, void (*f)(proc*)) {
    uintptr_t addr = reinterpret_cast<uintptr_t>(this);
    assert(!(addr & PAGEOFFMASK));

    pid_ = pid;

    regs_ = reinterpret_cast<regstate*>(addr + KTASKSTACK_SIZE) - 1;
    memset(regs_, 0, sizeof(regstate));
    regs_->reg_cs = SEGSEL_KERN_CODE;
    regs_->reg_fs = SEGSEL_KERN_DATA;
    regs_->reg_gs = SEGSEL_KERN_DATA;
    regs_->reg_ss = SEGSEL_KERN_DATA;
    regs_->reg_rflags = EFLAGS_IF;
    regs_->reg_rsp = addr + KTASKSTACK_SIZE;
    regs_->reg_rip = reinterpret_cast<uintptr_t>(f);
    regs_->reg_rdi = addr;

    yields_ = nullptr;
    state_ = proc::runnable;

    pagetable_ = early_pagetable;
}


// PROCESS LOADING FUNCTIONS

namespace {
struct memfile_loader : public proc::loader {
    memfile* mf_;
    ssize_t get_page(uint8_t** pg, size_t off) override;
    void put_page(uint8_t* pg) override;
};

// loader::get_page(pg, off)
//    Load a page at offset `off`, which is page-aligned. Set `*pg`
//    to the address of the loaded page and return the number of
//    valid bytes in that page, or negative on error.

ssize_t memfile_loader::get_page(uint8_t** pg, size_t off) {
    if (!mf_) {
        *pg = nullptr;
        return E_NOENT;
    } else if (off >= mf_->len_) {
        *pg = nullptr;
        return 0;
    } else {
        *pg = mf_->data_ + off;
        return mf_->len_ - off;
    }
}

// loader::put_page(pg)
//    Called to indicate that `proc::load` is done with `pg`,
//    which is a page returned by a previous call to `get_page`.

void memfile_loader::put_page(uint8_t* pg) {
}
}


// proc::load(binary_name)
//    Load the code corresponding to program `binary_name` into this process
//    and set `regs_->reg_rip` to its entry point. Calls `kallocpage()`.
//    Returns 0 on success and negative on failure (e.g. out-of-memory).

int proc::load(const char* binary_name) {
    memfile_loader ml;
    ml.pagetable_ = pagetable_;
    ml.mf_ = memfile::initfs_lookup(binary_name);
    int r = load(ml);
    if (r >= 0) {
        regs_->reg_rip = ml.entry_rip_;
    }
    return r;
}


// proc::load(loader)
//    Generic version of `proc::load(binary_name)`.

int proc::load(loader& ld) {
    union {
        elf_header eh;
        elf_program ph;
    } u;
    size_t len;
    unsigned nph, phoff;

    // validate the binary
    uint8_t* headerpg;
    ssize_t r = ld.get_page(&headerpg, 0);
    if (r < 0) {
        goto exit;
    } else if (size_t(r) < sizeof(elf_header)) {
        r = E_NOEXEC;
        goto exit;
    }

    len = r;
    memcpy(&u.eh, headerpg, sizeof(elf_header));
    if (u.eh.e_magic != ELF_MAGIC
        || u.eh.e_type != ELF_ET_EXEC
        || u.eh.e_phentsize != sizeof(elf_program)
        || u.eh.e_shentsize != sizeof(elf_section)
        || u.eh.e_phoff > PAGESIZE
        || u.eh.e_phoff > len
        || u.eh.e_phnum == 0
        || u.eh.e_phnum > (PAGESIZE - u.eh.e_phoff) / sizeof(elf_program)
        || u.eh.e_phnum > (len - u.eh.e_phoff) / sizeof(elf_program)) {
        r = E_NOEXEC;
        goto exit;
    }
    nph = u.eh.e_phnum;
    phoff = u.eh.e_phoff;
    ld.entry_rip_ = u.eh.e_entry;

    // load each loadable program segment into memory
    for (unsigned i = 0; i != nph; ++i) {
        memcpy(&u.ph, headerpg + phoff + i * sizeof(u.ph), sizeof(u.ph));
        if (u.ph.p_type == ELF_PTYPE_LOAD
            && (r = load_segment(u.ph, ld)) < 0) {
            goto exit;
        }
    }

    // set the entry point from the ELF header
    r = 0;

 exit:
    ld.put_page(headerpg);
    return r;
}


// proc::load_segment(ph, ld)
//    Load an ELF segment at virtual address `ph->p_va` into this process.
//    Loads pages `[src, src + ph->p_filesz)` to `dst`, then clears
//    `[ph->p_va + ph->p_filesz, ph->p_va + ph->p_memsz)` to 0.
//    Calls `kallocpage` to allocate pages and uses `vmiter::map`
//    to map them in `pagetable_`. Returns 0 on success and an error
//    code on failure.

int proc::load_segment(const elf_program& ph, loader& ld) {
    uintptr_t va = (uintptr_t) ph.p_va;
    uintptr_t end_file = va + ph.p_filesz;
    uintptr_t end_mem = va + ph.p_memsz;
    if (va > VA_LOWEND
        || VA_LOWEND - va < ph.p_memsz
        || ph.p_memsz < ph.p_filesz) {
        return E_NOEXEC;
    }

    // allocate memory
    for (vmiter it(ld.pagetable_, ROUNDDOWN(va, PAGESIZE));
         it.va() < end_mem;
         it += PAGESIZE) {
        x86_64_page* pg = kallocpage();
        if (!pg || it.map(ka2pa(pg)) < 0) {
            return E_NOMEM;
        }
    }

    // load binary data into allocated memory
    size_t off = ph.p_offset;
    size_t sz;
    for (vmiter it(ld.pagetable_, va);
         it.va() < end_file;
         it += sz, off += sz) {
        uint8_t* datapg;
        ssize_t r = ld.get_page(&datapg, ROUNDDOWN(off, PAGESIZE));
        size_t last_off = ROUNDDOWN(off, PAGESIZE) + r;
        if (r < 0) {
            return r;
        } else if (last_off <= off) {
            ld.put_page(datapg);
            return E_NOEXEC;
        } else {
            sz = min(last_off - off, min(it.last_va(), end_file) - it.va());
            memcpy(it.ka<uint8_t*>(), datapg + (off % PAGESIZE), sz);
            ld.put_page(datapg);
        }
    }

    // set initialized memory to zero
    for (vmiter it(ld.pagetable_, end_file); it.va() < end_mem; it += sz) {
        sz = min(it.last_va(), end_mem) - it.va();
        memset(it.ka<uint8_t*>(), 0, sz);
    }

    return 0;
}
