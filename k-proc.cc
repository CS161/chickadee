#include "elf.h"
#include "kernel.hh"
#include "k-vmiter.hh"

proc* ptable[NPROC];            // array of process descriptor pointers
spinlock ptable_lock;           // protects `ptable`


// proc::init_user(pid, pg)
//    Initialize this `proc` as a new user process with PID `pid` and
//    initial page table `pg`. `pg` is initialized from `early_pagetable`.

void proc::init_user(pid_t pid, x86_64_pagetable* pg) {
    uintptr_t addr = reinterpret_cast<uintptr_t>(this);
    assert(!(addr & PAGEOFFMASK));
    assert(!(reinterpret_cast<uintptr_t>(pg) & PAGEOFFMASK));
    // ensure layout `k-exception.S` expects
    assert(reinterpret_cast<uintptr_t>(&pid_) == addr);
    assert(reinterpret_cast<uintptr_t>(&regs_) == addr + 8);
    assert(reinterpret_cast<uintptr_t>(&yields_) == addr + 16);

    pid_ = pid;

    regs_ = reinterpret_cast<regstate*>(addr + KTASKSTACK_SIZE) - 1;
    memset(regs_, 0, sizeof(regstate));
    regs_->reg_cs = SEGSEL_APP_CODE | 3;
    regs_->reg_fs = SEGSEL_APP_DATA | 3;
    regs_->reg_gs = SEGSEL_APP_DATA | 3;
    regs_->reg_ss = SEGSEL_APP_DATA | 3;
    regs_->reg_rflags = EFLAGS_IF;

    yields_ = nullptr;
    state_ = proc::blank;

    // initialize pagetable from `early_pagetable`, but zero out low memory
    pagetable_ = pg;
    memcpy(pg, early_pagetable, sizeof(x86_64_pagetable));
    memset(pg, 0, sizeof(x86_64_pageentry_t) * 256);

    runq_pprev_ = nullptr;
    runq_next_ = nullptr;
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

    runq_pprev_ = nullptr;
    runq_next_ = nullptr;
}


#define SECTORSIZE 512

struct flatfs_file {
    const char* name;
    const unsigned char* first;
    const unsigned char* last;
};

// define the `flatfs_files` array
#include "obj/k-flatfs.c"


// proc::load(binary_name)
//    Load the code corresponding to program `binary_name` into this process
//    and set `regs_->reg_rip` to its entry point. Calls `kallocpage()`.
//    Returns 0 on success and negative on failure (e.g. out-of-memory).

int proc::load(const char* binary_name) {
    // find `flatfs_file` for `binary_name`
    const flatfs_file* fs = flatfs_files;
    size_t nfiles = sizeof(flatfs_files) / sizeof(flatfs_files[0]);
    const flatfs_file* end_fs = fs + nfiles;
    while (fs < end_fs && strcmp(binary_name, fs->name) != 0) {
        ++fs;
    }
    if (fs == end_fs) {
        return -1;
    }

    // validate the binary
    assert(size_t(fs->last - fs->first) >= sizeof(elf_header));
    const elf_header* eh = reinterpret_cast<const elf_header*>(fs->first);
    assert(eh->e_magic == ELF_MAGIC);
    assert(eh->e_phentsize == sizeof(elf_program));
    assert(eh->e_shentsize == sizeof(elf_section));

    // load each loadable program segment into memory
    const elf_program* ph = reinterpret_cast<const elf_program*>
        (fs->first + eh->e_phoff);
    for (int i = 0; i < eh->e_phnum; ++i) {
        if (ph[i].p_type == ELF_PTYPE_LOAD) {
            int r = load_segment(&ph[i], fs->first + ph[i].p_offset);
            if (r < 0) {
                return r;
            }
        }
    }

    // set the entry point from the ELF header
    regs_->reg_rip = eh->e_entry;
    return 0;
}


// proc::load_segment(ph, src)
//    Load an ELF segment at virtual address `ph->p_va` into this process.
//    Copies `[src, src + ph->p_filesz)` to `dst`, then clears
//    `[ph->p_va + ph->p_filesz, ph->p_va + ph->p_memsz)` to 0.
//    Calls `kallocpage` to allocate pages and uses `vmiter::map`
//    to map them in `pagetable_`. Returns 0 on success and -1 on failure.

int proc::load_segment(const elf_program* ph, const uint8_t* data) {
    uintptr_t va = (uintptr_t) ph->p_va;
    uintptr_t end_file = va + ph->p_filesz;
    uintptr_t end_mem = va + ph->p_memsz;

    // allocate memory
    for (vmiter it(this, va & ~(PAGESIZE - 1));
         it.va() < end_mem;
         it += PAGESIZE) {
        x86_64_page* pg = kallocpage();
        if (!pg || it.map(ka2pa(pg)) < 0) {
            return -1;
        }
        assert(it.pa() == ka2pa(pg));
    }

    // ensure new memory mappings are active
    set_pagetable(pagetable_);

    // copy data from executable image into process memory
    memcpy((uint8_t*) va, data, end_file - va);
    memset((uint8_t*) end_file, 0, end_mem - end_file);

    // restore early pagetable
    set_pagetable(early_pagetable);

    return 0;
}
