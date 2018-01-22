#include "kernel.hh"
#include "k-apic.hh"
#include "k-vmiter.hh"

// k-hardware.cc
//
//    Functions for interacting with x86 hardware.


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


// console_show_cursor(cpos)
//    Move the console cursor to position `cpos`, which should be between 0
//    and 80 * 25.

void console_show_cursor(int cpos) {
    if (cpos < 0 || cpos > CONSOLE_ROWS * CONSOLE_COLUMNS) {
        cpos = 0;
    }
    outb(0x3D4, 14);
    outb(0x3D5, cpos / 256);
    outb(0x3D4, 15);
    outb(0x3D5, cpos % 256);
}



// keyboard_readc
//    Read a character from the keyboard. Returns -1 if there is no character
//    to read, and 0 if no real key press was registered but you should call
//    keyboard_readc() again (e.g. the user pressed a SHIFT key). Otherwise
//    returns either an ASCII character code or one of the special characters
//    listed in kernel.h.

// Unfortunately mapping PC key codes to ASCII takes a lot of work.

#define MOD_SHIFT       (1 << 0)
#define MOD_CONTROL     (1 << 1)
#define MOD_CAPSLOCK    (1 << 3)

#define KEY_SHIFT       0372
#define KEY_CONTROL     0373
#define KEY_ALT         0374
#define KEY_CAPSLOCK    0375
#define KEY_NUMLOCK     0376
#define KEY_SCROLLLOCK  0377

#define CKEY(cn)        0x80 + cn

static const uint8_t keymap[256] = {
    /*0x00*/ 0, 033, CKEY(0), CKEY(1), CKEY(2), CKEY(3), CKEY(4), CKEY(5),
        CKEY(6), CKEY(7), CKEY(8), CKEY(9), CKEY(10), CKEY(11), '\b', '\t',
    /*0x10*/ 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i',
        'o', 'p', CKEY(12), CKEY(13), CKEY(14), KEY_CONTROL, 'a', 's',
    /*0x20*/ 'd', 'f', 'g', 'h', 'j', 'k', 'l', CKEY(15),
        CKEY(16), CKEY(17), KEY_SHIFT, CKEY(18), 'z', 'x', 'c', 'v',
    /*0x30*/ 'b', 'n', 'm', CKEY(19), CKEY(20), CKEY(21), KEY_SHIFT, '*',
        KEY_ALT, ' ', KEY_CAPSLOCK, 0, 0, 0, 0, 0,
    /*0x40*/ 0, 0, 0, 0, 0, KEY_NUMLOCK, KEY_SCROLLLOCK, '7',
        '8', '9', '-', '4', '5', '6', '+', '1',
    /*0x50*/ '2', '3', '0', '.', 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0,
    /*0x60*/ 0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0,
    /*0x70*/ 0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0,
    /*0x80*/ 0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0,
    /*0x90*/ 0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, CKEY(14), KEY_CONTROL, 0, 0,
    /*0xA0*/ 0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0,
    /*0xB0*/ 0, 0, 0, 0, 0, '/', 0, 0,  KEY_ALT, 0, 0, 0, 0, 0, 0, 0,
    /*0xC0*/ 0, 0, 0, 0, 0, 0, 0, KEY_HOME,
        KEY_UP, KEY_PAGEUP, 0, KEY_LEFT, 0, KEY_RIGHT, 0, KEY_END,
    /*0xD0*/ KEY_DOWN, KEY_PAGEDOWN, KEY_INSERT, KEY_DELETE, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0,
    /*0xE0*/ 0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0,
    /*0xF0*/ 0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0
};

static const struct keyboard_key {
    uint8_t map[4];
} complex_keymap[] = {
    /*CKEY(0)*/ {{'1', '!', 0, 0}},  /*CKEY(1)*/ {{'2', '@', 0, 0}},
    /*CKEY(2)*/ {{'3', '#', 0, 0}},  /*CKEY(3)*/ {{'4', '$', 0, 0}},
    /*CKEY(4)*/ {{'5', '%', 0, 0}},  /*CKEY(5)*/ {{'6', '^', 0, 036}},
    /*CKEY(6)*/ {{'7', '&', 0, 0}},  /*CKEY(7)*/ {{'8', '*', 0, 0}},
    /*CKEY(8)*/ {{'9', '(', 0, 0}},  /*CKEY(9)*/ {{'0', ')', 0, 0}},
    /*CKEY(10)*/ {{'-', '_', 0, 037}},  /*CKEY(11)*/ {{'=', '+', 0, 0}},
    /*CKEY(12)*/ {{'[', '{', 033, 0}},  /*CKEY(13)*/ {{']', '}', 035, 0}},
    /*CKEY(14)*/ {{'\n', '\n', '\r', '\r'}},
    /*CKEY(15)*/ {{';', ':', 0, 0}},
    /*CKEY(16)*/ {{'\'', '"', 0, 0}},  /*CKEY(17)*/ {{'`', '~', 0, 0}},
    /*CKEY(18)*/ {{'\\', '|', 034, 0}},  /*CKEY(19)*/ {{',', '<', 0, 0}},
    /*CKEY(20)*/ {{'.', '>', 0, 0}},  /*CKEY(21)*/ {{'/', '?', 0, 0}}
};

int keyboard_readc() {
    static uint8_t modifiers;
    static uint8_t last_escape;

    if ((inb(KEYBOARD_STATUSREG) & KEYBOARD_STATUS_READY) == 0) {
        return -1;
    }

    uint8_t data = inb(KEYBOARD_DATAREG);
    uint8_t escape = last_escape;
    last_escape = 0;

    if (data == 0xE0) {         // mode shift
        last_escape = 0x80;
        return 0;
    } else if (data & 0x80) {   // key release: matters only for modifier keys
        int ch = keymap[(data & 0x7F) | escape];
        if (ch >= KEY_SHIFT && ch < KEY_CAPSLOCK) {
            modifiers &= ~(1 << (ch - KEY_SHIFT));
        }
        return 0;
    }

    int ch = (unsigned char) keymap[data | escape];

    if (ch >= 'a' && ch <= 'z') {
        if (modifiers & MOD_CONTROL) {
            ch -= 0x60;
        } else if (!(modifiers & MOD_SHIFT) != !(modifiers & MOD_CAPSLOCK)) {
            ch -= 0x20;
        }
    } else if (ch >= KEY_CAPSLOCK) {
        modifiers ^= 1 << (ch - KEY_SHIFT);
        ch = 0;
    } else if (ch >= KEY_SHIFT) {
        modifiers |= 1 << (ch - KEY_SHIFT);
        ch = 0;
    } else if (ch >= CKEY(0) && ch <= CKEY(21)) {
        ch = complex_keymap[ch - CKEY(0)].map[modifiers & 3];
    } else if (ch < 0x80 && (modifiers & MOD_CONTROL)) {
        ch = 0;
    }

    return ch;
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


// check_keyboard
//    Check for the user typing a control key. 'a', 'f', and 'e' cause a soft
//    reboot where the kernel runs the allocator programs, "fork", or
//    "forkexit", respectively. Control-C or 'q' exit the virtual machine.
//    Returns key typed or -1 for no key.

int check_keyboard() {
    // XXX THIS WILL FAIL MISERABLY ON MULTICORE!!!!!!
    int c = keyboard_readc();
    if (c == 'a' || c == 'f' || c == 'e') {
        // Install a temporary page table to carry us through the
        // process of reinitializing memory. This replicates work the
        // bootloader does.
        x86_64_pagetable* pt = pa2ktext<x86_64_pagetable*>(0x1000);
        memset(pt, 0, PAGESIZE * 2);
        pt[0].entry[0] = pt[0].entry[511] = 0x2000 | PTE_P | PTE_W | PTE_U;
        pt[1].entry[0] = pt[0].entry[510] = PTE_P | PTE_W | PTE_U | PTE_PS;
        lcr3(ktext2pa(pt));
        // The soft reboot process doesn't modify memory, so it's
        // safe to pass `multiboot_info` on the kernel stack, even
        // though it will get overwritten as the kernel runs.
        uint32_t multiboot_info[5];
        multiboot_info[0] = 4;
        const char* argument = "fork";
        if (c == 'a') {
            argument = "allocator";
        } else if (c == 'e') {
            argument = "forkexit";
        }
        uintptr_t argument_ptr = ktext2pa(argument);
        assert(argument_ptr < 0x100000000L);
        multiboot_info[4] = (uint32_t) argument_ptr;
        asm volatile("movl $0x2BADB002, %%eax; jmp kernel_entry"
                     : : "b" (multiboot_info) : "memory");
    } else if (c == 0x03 || c == 'q') {
        poweroff();
    }
    return c;
}


// fail
//    Loop until user presses Control-C, then poweroff.

static void fail() __attribute__((noreturn));
static void fail() {
    while (1) {
        check_keyboard();
    }
}


// panic, assert_fail
//    Use console_printf() to print a failure message and then wait for
//    control-C. Also write the failure message to the log.

void panic(const char* format, ...) {
    va_list val;
    va_start(val, format);

    if (format) {
        // Print panic message to both the screen and the log
        int cpos = error_printf(CPOS(23, 0), COLOR_ERROR, "PANIC: ");
        cpos = error_vprintf(cpos, COLOR_ERROR, format, val);
        if (CCOL(cpos)) {
            error_printf(cpos, COLOR_ERROR, "\n");
        }
    } else {
        error_printf(CPOS(23, 0), COLOR_ERROR, "PANIC");
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
}
