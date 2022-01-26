#include "kernel.hh"
#include "k-apic.hh"
#include "k-devices.hh"
#include "k-pci.hh"
#include "k-vmiter.hh"
#include "elf.h"


// k-hardware.cc
//
//    Functions for interacting with x86 hardware.

pcistate pcistate::state;


// kalloc_pagetable
//    Allocate, initialize, and return a new, empty page table. Memory is
//    allocated using `kalloc()`. The page table's high memory is copied
//    from `early_pagetable`.

x86_64_pagetable* kalloc_pagetable() {
    x86_64_pagetable* pt = knew<x86_64_pagetable>();
    if (pt) {
        memset(&pt->entry[0], 0, sizeof(x86_64_pageentry_t) * 256);
        memcpy(&pt->entry[256], &early_pagetable->entry[256],
               sizeof(x86_64_pageentry_t) * 256);
    }
    return pt;
}


// set_pagetable(pagetable)
//    Change page table using wrcr3(), a hardware instruction. set_pagetable()
//    additionally checks that important kernel procedures are mapped in the
//    new page table, and calls panic() if they aren't.

void set_pagetable(x86_64_pagetable* pagetable) {
    assert(pagetable != nullptr);          // must not be nullptr
    assert(vmiter(pagetable, HIGHMEM_BASE).pa() == 0);
    assert(vmiter(pagetable, HIGHMEM_BASE).writable());
    assert(!vmiter(pagetable, HIGHMEM_BASE).user());
    assert(vmiter(pagetable, KTEXT_BASE).pa() == 0);
    assert(vmiter(pagetable, KTEXT_BASE).writable());
    assert(!vmiter(pagetable, KTEXT_BASE).user());
    auto pa = is_ktext(pagetable) ? ktext2pa(pagetable) : ka2pa(pagetable);
    wrcr3(pa);
}


// pcistate::next(addr)
//    Return the next valid PCI function after `addr`, or -1 if there
//    is none.
int pcistate::next(int addr) const {
    uint32_t x = readl(addr + config_lthb);
    while (true) {
        if (addr_func(addr) == 0
            && (x == uint32_t(-1) || !(x & 0x800000))) {
            addr += make_addr(0, 1, 0);
        } else {
            addr += make_addr(0, 0, 1);
        }
        if (addr >= addr_end) {
            return -1;
        }
        x = readl(addr + config_lthb);
        if (x != uint32_t(-1)) {
            return addr;
        }
    }
}

void pcistate::enable(int addr) {
    // enable I/O (0x01), memory (0x02), and bus master (0x04)
    writew(addr + config_command, 0x0007);
}


// poweroff
//    Turn off the virtual machine. This requires finding a PCI device
//    that speaks ACPI.

void poweroff() {
    auto& pci = pcistate::get();
    int addr = pci.find([&] (int a) {
            uint32_t vd = pci.readl(a + pci.config_vendor);
            return vd == 0x71138086U /* PIIX4 Power Management Controller */
                || vd == 0x29188086U /* ICH9 LPC Interface Controller */;
        });
    if (addr >= 0) {
        // Read I/O base register from controller's PCI configuration space.
        int pm_io_base = pci.readl(addr + 0x40) & 0xFFC0;
        // Write `suspend enable` to the power management control register.
        outw(pm_io_base + 4, 0x2000);
    }
    // No known ACPI controller; spin.
    console_printf(CPOS(24, 0), 0xC000, "Cannot power off!\n");
    while (true) {
    }
}


// reboot
//    Reboot the virtual machine.

void reboot() {
    outb(0x92, 3); // does not return
    while (true) {
    }
}


// process_halt
//    Called when the last user process exits. This will turn off the virtual
//    machine if `HALT=N` was specified during kernel build.

void process_halt() {
    // change keyboard state, hide cursor
    auto& kbd = keyboardstate::get();
    kbd.state_ = keyboardstate::boot;
    consolestate::get().cursor(false);
    // decide when to power off
    unsigned long halt_at = 0;
    int haltidx = memfile::initfs_lookup(".halt");
    if (haltidx >= 0) {
        memfile& mf = memfile::initfs[haltidx];
        char* data = reinterpret_cast<char*>(mf.data_);
        unsigned long halt_after;
        auto [p, ec] = from_chars(data, data + mf.len_, halt_after);
        while (p != data + mf.len_ && isspace(*p)) {
            ++p;
        }
        if (p == data + mf.len_ && ec == 0 && halt_after != 0) {
            halt_at = ticks + halt_after;
        }
    }
    // yield until halt time
    while (halt_at == 0 || long(halt_at - ticks) > 0) {
        current()->yield();
    }
    // turn off machine
    poweroff();
}


// log_printf, log_vprintf
//    Print debugging messages to the host's `log.txt` file. We run QEMU
//    so that messages written to the QEMU "parallel port" end up in `log.txt`.

#define IO_PARALLEL1_DATA       0x378
#define IO_PARALLEL1_STATUS     0x379
# define IO_PARALLEL_STATUS_BUSY        0x80
#define IO_PARALLEL1_CONTROL    0x37A
# define IO_PARALLEL_CONTROL_SELECT     0x08
# define IO_PARALLEL_CONTROL_INIT       0x04
# define IO_PARALLEL_CONTROL_STROBE     0x01

static void delay() {
    (void) inb(0x84);
    (void) inb(0x84);
    (void) inb(0x84);
    (void) inb(0x84);
}

static void parallel_port_putc(unsigned char c) {
    static int initialized;
    if (!initialized) {
        outb(IO_PARALLEL1_CONTROL, 0);
        initialized = 1;
    }

    for (int i = 0;
         i < 12800 && (inb(IO_PARALLEL1_STATUS) & IO_PARALLEL_STATUS_BUSY) == 0;
         ++i) {
        delay();
    }
    outb(IO_PARALLEL1_DATA, c);
    outb(IO_PARALLEL1_CONTROL, IO_PARALLEL_CONTROL_SELECT
         | IO_PARALLEL_CONTROL_INIT | IO_PARALLEL_CONTROL_STROBE);
    outb(IO_PARALLEL1_CONTROL, IO_PARALLEL_CONTROL_SELECT
         | IO_PARALLEL_CONTROL_INIT);
}

namespace {
struct log_printer : public printer {
    void putc(unsigned char c, int) override {
        parallel_port_putc(c);
    }
};
}

void log_vprintf(const char* format, va_list val) {
    log_printer p;
    p.vprintf(0, format, val);
}

void log_printf(const char* format, ...) {
    va_list val;
    va_start(val, format);
    log_vprintf(format, val);
    va_end(val);
}


// symtab: reference to kernel symbol table; useful for debugging.
// The `mkchickadeesymtab` program fills this structure in.
elf_symtabref symtab = {
    reinterpret_cast<elf_symbol*>(0xFFFFFFFF81000000), 0, nullptr, 0
};

// lookup_symbol(addr, name, start)
//    Use the debugging symbol table to look up `addr`. Return the
//    corresponding symbol name (usually a function name) in `*name`
//    and the first address in that symbol in `*start`.

__no_asan
bool lookup_symbol(uintptr_t addr, const char** name, uintptr_t* start) {
    extern elf_symtabref symtab;
    size_t l = 0;
    size_t r = symtab.nsym;
    while (l < r) {
        size_t m = l + ((r - l) >> 1);
        auto& sym = symtab.sym[m];
        if (sym.st_value <= addr
            && (m + 1 == symtab.nsym
                ? addr < sym.st_value + 0x1000
                : addr < (&sym)[1].st_value)
            && (sym.st_size == 0 || addr <= sym.st_value + sym.st_size)) {
            if (!sym.st_value) {
                return false;
            }
            if (name) {
                *name = symtab.strtab + sym.st_name;
            }
            if (start) {
                *start = sym.st_value;
            }
            return true;
        } else if (sym.st_value < addr) {
            l = m + 1;
        } else {
            r = m;
        }
    }
    return false;
}


namespace {
struct backtracer {
    backtracer(const regstate& regs, x86_64_pagetable* pt)
        : backtracer(regs, round_up(regs.reg_rsp, PAGESIZE), pt) {
    }
    backtracer(const regstate& regs, uintptr_t stack_top,
               x86_64_pagetable* pt)
        : rbp_(regs.reg_rbp), rsp_(regs.reg_rsp), stack_top_(stack_top),
          pt_(pt) {
        check();
    }
    bool ok() const {
        return rsp_ != 0;
    }
    uintptr_t rbp() const {
        return rbp_;
    }
    uintptr_t rsp() const {
        return rsp_;
    }
    uintptr_t ret_rip() const {
        return deref_rbp(8);
    }
    void step() {
        rsp_ = rbp_ + 16;
        rbp_ = deref_rbp(0);
        check();
    }

private:
    uintptr_t rbp_;
    uintptr_t rsp_;
    uintptr_t stack_top_;
    x86_64_pagetable* pt_;

    void check() {
        if (rbp_ < rsp_
            || stack_top_ - 16 < rbp_
            || !pt_
            || (vmiter(pt_, rbp_).range_perm(16) & PTE_P) == 0
            || (rbp_ & 7) != 0) {
            rbp_ = rsp_ = 0;
        }
    }

    uintptr_t deref_rbp(uintptr_t off) const {
        return *vmiter(pt_, rbp_ + off).kptr<const uintptr_t*>();
    }
};
}


// log_backtrace([proc,] prefix)
//    Print a backtrace to `log.txt`, each line prefixed by `prefix`.

static void log_backtrace(backtracer& bt, const char* prefix) {
    if (bt.rsp() != bt.rbp()
        && round_up(bt.rsp(), PAGESIZE) == round_down(bt.rbp(), PAGESIZE)) {
        log_printf("%s  warning: possible stack overflow (rsp %p, rbp %p)\n",
                   prefix, bt.rsp(), bt.rbp());
    }
    for (int frame = 1; bt.ok(); bt.step(), ++frame) {
        uintptr_t ret_rip = bt.ret_rip();
        const char* name;
        if (lookup_symbol(ret_rip, &name, nullptr)) {
            log_printf("%s  #%d  %p  <%s>\n", prefix, frame, ret_rip, name);
        } else if (ret_rip) {
            log_printf("%s  #%d  %p\n", prefix, frame, ret_rip);
        }
    }
}

__always_inline const regstate& backtrace_current_regs() {
    // static so we don't use stack space; stack might be full
    static regstate backtrace_kernel_regs;
    backtrace_kernel_regs.reg_rsp = rdrsp();
    backtrace_kernel_regs.reg_rbp = rdrbp();
    backtrace_kernel_regs.reg_rip = 0;
    return backtrace_kernel_regs;
}

__always_inline x86_64_pagetable* backtrace_current_pagetable() {
    return pa2kptr<x86_64_pagetable*>(rdcr3());
}

void log_backtrace(const char* prefix) {
    backtracer bt(backtrace_current_regs(), backtrace_current_pagetable());
    log_backtrace(bt, prefix);
}

void log_backtrace(const proc* p, const char* prefix) {
    backtracer bt(*p->regs_, p->pagetable_);
    log_backtrace(bt, prefix);
}


// error_vprintf
//    Print debugging messages to the console and to the host's
//    `log.txt` file via `log_printf`.

__noinline
int error_vprintf(int cpos, int color, const char* format, va_list val) {
    va_list val2;
    __builtin_va_copy(val2, val);
    log_vprintf(format, val2);
    va_end(val2);
    return console_vprintf(cpos, color, format, val);
}


// fail
//    Loop until user presses Control-C, then poweroff.

[[noreturn]] void fail() {
    auto& kbd = keyboardstate::get();
    kbd.state_ = kbd.fail;
    while (true) {
        kbd.handle_interrupt();
    }
}


// panic, assert_fail
//    Use console_printf() to print a failure message and then wait for
//    control-C. Also write the failure message to the log.

std::atomic<bool> panicking;

static void error_print_backtrace(const regstate& regs,
                                  x86_64_pagetable* pt,
                                  bool include_rip) {
    if (include_rip && regs.reg_rip) {
        const char* name;
        if (lookup_symbol(regs.reg_rip, &name, nullptr)) {
            error_printf("  #0  %p  <%s>\n", regs.reg_rip, name);
        } else {
            error_printf("  #0  %p\n", regs.reg_rip);
        }
    }
    backtracer bt(regs, pt);
    for (int frame = 1; bt.ok(); bt.step(), ++frame) {
        uintptr_t ret_rip = bt.ret_rip();
        const char* name;
        if (lookup_symbol(ret_rip, &name, nullptr)) {
            error_printf("  #%d  %p  <%s>\n", frame, ret_rip, name);
        } else {
            error_printf("  #%d  %p\n", frame, ret_rip);
        }
    }
}

static void vpanic(const regstate& regs, x86_64_pagetable* pt,
                   const char* format, va_list val) {
    panicking = true;

    // Print panic message to both the screen and the log
    if (!format) {
        format = "PANIC";
    }
    if (consoletype != CONSOLE_NORMAL) {
        cursorpos = CPOS(24, 80);
    }
    if (strstr(format, "PANIC") == nullptr) {
        error_printf(-1, COLOR_ERROR, "PANIC: ");
    }
    error_vprintf(-1, COLOR_ERROR, format, val);
    if (CCOL(cursorpos)) {
        error_printf(-1, COLOR_ERROR, "\n");
    }

    error_print_backtrace(regs, pt, true);
}

void panic(const char* format, ...) {
    va_list val;
    va_start(val, format);
    vpanic(backtrace_current_regs(), backtrace_current_pagetable(),
           format, val);
    va_end(val);
    fail();
}

void panic_at(const regstate& regs, const char* format, ...) {
    va_list val;
    va_start(val, format);
    vpanic(regs, backtrace_current_pagetable(), format, val);
    va_end(val);
    fail();
}

void assert_fail(const char* file, int line, const char* msg,
                 const char* description) {
    if (consoletype != CONSOLE_NORMAL) {
        cursorpos = CPOS(23, 0);
    }
    if (description) {
        error_printf("%s:%d: %s\n", file, line, description);
    }
    error_printf("%s:%d: kernel assertion '%s' failed\n", file, line, msg);
    error_print_backtrace(backtrace_current_regs(),
                          backtrace_current_pagetable(), false);
    fail();
}


// C++ ABI functions

namespace std {
const nothrow_t nothrow;
}

extern "C" {
// The __cxa_guard functions control the initialization of static variables.

// __cxa_guard_acquire(guard)
//    Return 0 if the static variables guarded by `*guard` are already
//    initialized. Otherwise lock `*guard` and return 1. The compiler
//    will initialize the statics, then call `__cxa_guard_release`.
int __cxa_guard_acquire(long long* arg) {
    std::atomic<char>* guard = reinterpret_cast<std::atomic<char>*>(arg);
    if (guard->load(std::memory_order_relaxed) == 2) {
        return 0;
    }
    while (true) {
        char old_value = guard->exchange(1);
        if (old_value == 2) {
            guard->exchange(2);
            return 0;
        } else if (old_value == 1) {
            pause();
        } else {
            return 1;
        }
    }
}

// __cxa_guard_release(guard)
//    Mark `guard` to indicate that the static variables it guards are
//    initialized.
void __cxa_guard_release(long long* arg) {
    std::atomic<char>* guard = reinterpret_cast<std::atomic<char>*>(arg);
    guard->store(2);
}

// __cxa_pure_virtual()
//    Used as a placeholder for pure virtual functions.
void __cxa_pure_virtual() {
    panic("pure virtual function called in kernel!\n");
}

// __dso_handle, __cxa_atexit
//    Used to destroy global objects at "program exit". We don't bother.
void* __dso_handle;
void __cxa_atexit(...) {
}

}
