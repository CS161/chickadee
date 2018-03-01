#include "kernel.hh"
#include "k-apic.hh"
#include "k-devices.hh"
#include "k-vmiter.hh"

// k-hardware.cc
//
//    Functions for interacting with x86 hardware.


// kalloc_pagetable
//    Allocate and initialize a new page table. The page is allocated
//    using `kallocpage()`. The page table's high memory is copied from
//    `early_pagetable`.

x86_64_pagetable* kalloc_pagetable() {
    x86_64_pagetable* pt = reinterpret_cast<x86_64_pagetable*>
        (kallocpage());
    if (pt) {
        memset(&pt->entry[0], 0, sizeof(x86_64_pageentry_t) * 256);
        memcpy(&pt->entry[256], &early_pagetable->entry[256],
               sizeof(x86_64_pageentry_t) * 256);
    }
    return pt;
}


// set_pagetable
//    Change page directory. lcr3() is the hardware instruction;
//    set_pagetable() additionally checks that important kernel procedures are
//    mappable in `pagetable`, and calls panic() if they aren't.

void set_pagetable(x86_64_pagetable* pagetable) {
    assert(pagetable != nullptr);          // must not be NULL
    assert(vmiter(pagetable, HIGHMEM_BASE).pa() == 0);
    assert(vmiter(pagetable, HIGHMEM_BASE).writable());
    assert(!vmiter(pagetable, HIGHMEM_BASE).user());
    assert(vmiter(pagetable, KTEXT_BASE).pa() == 0);
    assert(vmiter(pagetable, KTEXT_BASE).writable());
    assert(!vmiter(pagetable, KTEXT_BASE).user());
    auto pa = is_ktext(pagetable) ? ktext2pa(pagetable) : ka2pa(pagetable);
    lcr3(pa);
}


// pci_make_configaddr(bus, slot, func)
//    Construct a PCI configuration space address from parts.

static int pci_make_configaddr(int bus, int slot, int func) {
    return (bus << 16) | (slot << 11) | (func << 8);
}


// pci_config_readl(bus, slot, func, offset)
//    Read a 32-bit word in PCI configuration space.

#define PCI_HOST_BRIDGE_CONFIG_ADDR 0xCF8
#define PCI_HOST_BRIDGE_CONFIG_DATA 0xCFC

static uint32_t pci_config_readl(int configaddr, int offset) {
    outl(PCI_HOST_BRIDGE_CONFIG_ADDR, 0x80000000 | configaddr | offset);
    return inl(PCI_HOST_BRIDGE_CONFIG_DATA);
}


// pci_find_device
//    Search for a PCI device matching `vendor` and `device`. Return
//    the config base address or -1 if no device was found.

static int pci_find_device(int vendor, int device) {
    for (int bus = 0; bus != 256; ++bus) {
        for (int slot = 0; slot != 32; ++slot) {
            for (int func = 0; func != 8; ++func) {
                int configaddr = pci_make_configaddr(bus, slot, func);
                uint32_t vendor_device = pci_config_readl(configaddr, 0);
                if (vendor_device == (uint32_t) (vendor | (device << 16))) {
                    return configaddr;
                } else if (vendor_device == (uint32_t) -1 && func == 0) {
                    break;
                }
            }
        }
    }
    return -1;
}


// poweroff
//    Turn off the virtual machine. This requires finding a PCI device
//    that speaks ACPI; QEMU emulates a PIIX4 Power Management Controller.

#define PCI_VENDOR_ID_INTEL     0x8086
#define PCI_DEVICE_ID_PIIX4     0x7113

void poweroff() {
    int configaddr = pci_find_device(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_PIIX4);
    if (configaddr >= 0) {
        // Read I/O base register from controller's PCI configuration space.
        int pm_io_base = pci_config_readl(configaddr, 0x40) & 0xFFC0;
        // Write `suspend enable` to the power management control register.
        outw(pm_io_base + 4, 0x2000);
    }
    // No PIIX4; spin.
    console_printf(CPOS(24, 0), 0xC000, "Cannot power off!\n");
    while (1) {
    }
}


// reboot
//    Reboot the virtual machine.

void reboot() {
    outb(0x92, 3); // does not return
    while (1) {
    }
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

static void parallel_port_putc(printer* p, unsigned char c, int color) {
    static int initialized;
    (void) p, (void) color;
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

void log_vprintf(const char* format, va_list val) {
    printer p;
    p.putc = parallel_port_putc;
    printer_vprintf(&p, 0, format, val);
}

void log_printf(const char* format, ...) {
    va_list val;
    va_start(val, format);
    log_vprintf(format, val);
    va_end(val);
}


// log_backtrace(prefix)
//    Print a backtrace to `log.txt`, each line prefixed by `prefix`.

void log_backtrace(const char* prefix) {
    uintptr_t rsp = read_rsp(), rbp = read_rbp();
    uintptr_t stack_top = ROUNDUP(rsp, PAGESIZE);
    int frame = 1;
    while (rbp >= rsp && rbp < stack_top) {
        uintptr_t* rbpx = reinterpret_cast<uintptr_t*>(rbp);
        uintptr_t next_rbp = rbpx[0];
        uintptr_t ret_rip = rbpx[1];
        if (!ret_rip) {
            break;
        }
        log_printf("%s  #%d  %p\n", prefix, frame, ret_rip);
        rbp = next_rbp;
        ++frame;
    }
}


// error_printf, error_vprintf
//    Print debugging messages to the console and to the host's
//    `log.txt` file via `log_printf`.

int error_vprintf(int cpos, int color, const char* format, va_list val) {
    va_list val2;
    __builtin_va_copy(val2, val);
    log_vprintf(format, val2);
    va_end(val2);
    return console_vprintf(cpos, color, format, val);
}

int error_printf(int cpos, int color, const char* format, ...) {
    va_list val;
    va_start(val, format);
    cpos = error_vprintf(cpos, color, format, val);
    va_end(val);
    return cpos;
}

void error_printf(int color, const char* format, ...) {
    va_list val;
    va_start(val, format);
    error_vprintf(-1, color, format, val);
    va_end(val);
}

void error_printf(const char* format, ...) {
    va_list val;
    va_start(val, format);
    error_vprintf(-1, COLOR_ERROR, format, val);
    va_end(val);
}


// fail
//    Loop until user presses Control-C, then poweroff.

void __attribute__((noreturn)) fail() {
    auto& kbd = keyboardstate::get();
    kbd.state_ = kbd.fail;
    while (1) {
        kbd.handle_interrupt();
    }
}


// panic, assert_fail
//    Use console_printf() to print a failure message and then wait for
//    control-C. Also write the failure message to the log.

bool panicing;

void panic(const char* format, ...) {
    va_list val;
    va_start(val, format);
    panicing = true;

    if (format) {
        // Print panic message to both the screen and the log
        int cpos = error_printf(CPOS(23, 0), COLOR_ERROR, "PANIC: ");
        cpos = error_vprintf(cpos, COLOR_ERROR, format, val);
        if (CCOL(cpos)) {
            error_printf(cpos, COLOR_ERROR, "\n");
        }
    } else {
        error_printf(CPOS(23, 0), COLOR_ERROR, "PANIC");
        log_printf("\n");
    }

    va_end(val);
    fail();
}

void assert_fail(const char* file, int line, const char* msg) {
    cursorpos = 23 * CONSOLE_COLUMNS;
    error_printf("%s:%d: assertion '%s' failed\n", file, line, msg);

    uintptr_t rsp = read_rsp(), rbp = read_rbp();
    uintptr_t stack_top = ROUNDUP(rsp, PAGESIZE);
    int frame = 1;
    while (rbp >= rsp && rbp < stack_top) {
        uintptr_t* rbpx = reinterpret_cast<uintptr_t*>(rbp);
        uintptr_t next_rbp = rbpx[0];
        uintptr_t ret_rip = rbpx[1];
        if (!ret_rip) {
            break;
        }
        error_printf("  #%d  %p\n", frame, ret_rip);
        rbp = next_rbp;
        ++frame;
    }
    fail();
}


// C++ ABI functions

extern "C" {
// The __cxa_guard functions control the initialization of static variables.

// __cxa_guard_acquire(guard)
//    Return 0 if the static variables guarded by `*guard` are already
//    initialized. Otherwise lock `*guard` and return 1. The compiler
//    will initialize the statics, then call `__cxa_guard_release`.
int __cxa_guard_acquire(std::atomic<char>* guard) {
    if (guard->load(std::memory_order_relaxed) == 2) {
        return 0;
    }
    while (1) {
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
void __cxa_guard_release(std::atomic<char>* guard) {
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
