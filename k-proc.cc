#include "kernel.hh"
#include "elf.h"
#include "k-vmiter.hh"
#include "k-devices.hh"

proc* ptable[NPROC];            // array of process descriptor pointers
spinlock ptable_lock;           // protects `ptable`


// proc::proc()
//    The constructor initializes the `proc` to empty.

proc::proc()
    : id_(0), regs_(nullptr), yields_(nullptr),
      state_(blank), pagetable_(nullptr) {
}


// proc::init_user(pid, pt)
//    Initialize this `proc` as a new blocked user process with PID `pid`
//    and initial page table `pt`. Use `proc::wake()` to schedule the
//    process.

void proc::init_user(pid_t pid, x86_64_pagetable* pt) {
    uintptr_t addr = reinterpret_cast<uintptr_t>(this);
    assert(!(addr & PAGEOFFMASK));
    // ensure layout `k-exception.S` expects
    assert(reinterpret_cast<uintptr_t>(&id_) == addr);
    assert(reinterpret_cast<uintptr_t>(&regs_) == addr + 8);
    assert(reinterpret_cast<uintptr_t>(&yields_) == addr + 16);
    // ensure initialized page table
    assert(!(reinterpret_cast<uintptr_t>(pt) & PAGEOFFMASK));
    assert(pt->entry[256] == early_pagetable->entry[256]);
    assert(pt->entry[511] == early_pagetable->entry[511]);

    id_ = pid;
    pagetable_ = pt;
    state_ = proc::blocked;

    regs_ = reinterpret_cast<regstate*>(addr + PROCSTACK_SIZE) - 1;
    memset(regs_, 0, sizeof(regstate));
    regs_->reg_cs = SEGSEL_APP_CODE | 3;
    regs_->reg_ss = SEGSEL_APP_DATA | 3;
    regs_->reg_rflags = EFLAGS_IF;
    regs_->reg_swapgs = 1;
}


// proc::init_kernel(pid)
//    Initialize this `proc` as a new blocked kernel process with
//    PID `pid`, starting at function `f`.

void proc::init_kernel(pid_t pid, void (*f)(proc*)) {
    uintptr_t addr = reinterpret_cast<uintptr_t>(this);
    assert(!(addr & PAGEOFFMASK));

    id_ = pid;
    pagetable_ = early_pagetable;
    state_ = proc::blocked;

    regs_ = reinterpret_cast<regstate*>(addr + PROCSTACK_SIZE) - 1;
    memset(regs_, 0, sizeof(regstate));
    regs_->reg_cs = SEGSEL_KERN_CODE;
    regs_->reg_ss = SEGSEL_KERN_DATA;
    regs_->reg_rflags = EFLAGS_IF;
    regs_->reg_rsp = addr + PROCSTACK_SIZE;
    regs_->reg_rip = reinterpret_cast<uintptr_t>(f);
    regs_->reg_rdi = addr;
}


// PROCESS LOADING FUNCTIONS

// Process loading uses `proc_loader` objects. A `proc_loader`
// abstracts the way an a executable is stored. For example, it can
// be stored in an initial-ramdisk file (`memfile_loader`, defined
// in `k-devices.cc`), or on a disk (you'll write such a loader
// later).
//
// `proc::load` and its helpers call two functions on `proc_loader`:
// proc_loader::get_page(pg_ptr, off)
//    Called to obtain data from the executable starting at offset `off`.
//    `off` is page-aligned. On success, the loader should set `*pg_ptr`
//    to the address of the data and return the number of valid bytes
//    starting there. On failure, it should return a negative error code.
// proc_loader::put_page(pg)
//    Called to indicate that `proc::load` is done with `pg`,
//    which is a page returned by a previous call to `get_page`.
//    This might, for example, decrement a reference count.
// Note that data returned by `get_page` is *copied* into `ld.pagetable_`.


// proc::load(proc_loader& ld)
//    Load the executable specified by the `proc_loader` into `ld.pagetable_`
//    and set `ld.entry_rip_` to its entry point. Calls `kalloc` and maps
//    memory. Returns 0 on success and a negative error code on failure,
//    such as `E_NOMEM` for out of memory or `E_NOEXEC` for not an executable.

int proc::load(proc_loader& ld) {
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
//    Calls `kalloc` to allocate pages and uses `vmiter::map`
//    to map them in `pagetable_`. Returns 0 on success and an error
//    code on failure.

int proc::load_segment(const elf_program& ph, proc_loader& ld) {
    uintptr_t va = (uintptr_t) ph.p_va;
    uintptr_t end_file = va + ph.p_filesz;
    uintptr_t end_mem = va + ph.p_memsz;
    if (va > VA_LOWEND
        || VA_LOWEND - va < ph.p_memsz
        || ph.p_memsz < ph.p_filesz) {
        return E_NOEXEC;
    }
    if (!ld.pagetable_) {
        return E_NOMEM;
    }

    // allocate memory
    for (vmiter it(ld.pagetable_, round_down(va, PAGESIZE));
         it.va() < end_mem;
         it += PAGESIZE) {
        void* pg = kalloc(PAGESIZE);
        if (!pg || it.map(ka2pa(pg)) < 0) {
            kfree(pg);
            return E_NOMEM;
        }
    }

    // load binary data into just-allocated memory
    size_t off = ph.p_offset;
    for (vmiter it(ld.pagetable_, va); it.va() < end_file; ) {
        // obtain data
        uint8_t* datapg = nullptr;
        size_t req_off = round_down(off, PAGESIZE);
        ssize_t r = ld.get_page(&datapg, req_off);
        size_t last_off = req_off + r;
        if (r < 0) {
            return r;
        } else if (last_off <= off) {
            // error: not enough data in page!
            ld.put_page(datapg);
            return E_NOEXEC;
        }

        // copy one page at a time
        while (off < last_off && it.va() < end_file) {
            size_t datapg_sz = last_off - off;
            size_t va_sz = min(it.last_va(), end_file) - it.va();
            size_t copy_sz = min(datapg_sz, va_sz);
            memcpy(it.ka<uint8_t*>(), datapg + (off - req_off), copy_sz);
            it += copy_sz;
            off += copy_sz;
        }

        // release data
        ld.put_page(datapg);
    }

    // set initialized, but not copied, memory to zero
    for (vmiter it(ld.pagetable_, end_file); it.va() < end_mem; ) {
        size_t sz = min(it.last_va(), end_mem) - it.va();
        memset(it.ka<uint8_t*>(), 0, sz);
        it += sz;
    }

    return 0;
}


// A `proc` cannot be smaller than a page.
static_assert(PROCSTACK_SIZE >= sizeof(proc), "PROCSTACK_SIZE too small");
