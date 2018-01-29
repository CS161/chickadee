#ifndef CHICKADEE_KERNEL_H
#define CHICKADEE_KERNEL_H
#include "x86-64.h"
#include "lib.hh"
#include "k-lock.hh"
#include "k-memrange.hh"
#if CHICKADEE_PROCESS
#error "kernel.hh should not be used by process code."
#endif
struct elf_program;
struct proc;
struct yieldstate;


// kernel.h
//
//    Functions, constants, and definitions for the kernel.


// CPU state type
struct __attribute__((aligned(4096))) cpustate {
    // These three members must come first:
    cpustate* self_;
    proc* current_;
    uint64_t syscall_scratch_;

    int index_;
    int lapic_id_;

    proc* runq_head_;
    proc* runq_tail_;
    spinlock runq_lock_;
    proc* idle_task_;

    unsigned spinlock_depth_;

    uint64_t gdt_segments_[7];
    x86_64_taskstate task_descriptor_;


    cpustate() = default;
    NO_COPY_OR_ASSIGN(cpustate);

    inline bool contains(void* ptr) const;

    void init();
    void init_ap();

    void exception(regstate* reg);

    void enqueue(proc* p);
    void schedule(proc* yielding_from) __attribute__((noreturn));
    proc* idle_task();

 private:
    void init_cpu_hardware();
};

#define NCPU 16
extern cpustate cpus[NCPU];
extern int ncpu;
#define CPUSTACK_SIZE 4096

inline cpustate* this_cpu();


// Process descriptor type
struct __attribute__((aligned(4096))) proc {
    // These three members must come first:
    pid_t pid_;                        // process ID
    regstate* regs_;                   // process's current registers
    yieldstate* yields_;               // process's current yield state

    enum state_t {
        blank = 0, runnable, blocked, broken
    };
    state_t state_;                    // process state
    x86_64_pagetable* pagetable_;      // process's page table

    proc** runq_pprev_;
    proc* runq_next_;


    proc() = default;
    NO_COPY_OR_ASSIGN(proc);

    inline bool contains(void* ptr) const;

    void init_user(pid_t pid, x86_64_pagetable* pt);
    void init_kernel(pid_t pid, void (*f)(proc*));
    int load(const char* binary_name);

    void exception(regstate* reg);
    uintptr_t syscall(regstate* reg);

    void yield();
    void yield_noreturn() __attribute__((noreturn));
    void resume() __attribute__((noreturn));

 private:
    int load_segment(const elf_program* ph, const uint8_t* data);
};

#define NPROC 16
extern proc* ptable[NPROC];
extern spinlock ptable_lock;
#define KTASKSTACK_SIZE  4096


// yieldstate: callee-saved registers that must be preserved across
// proc::yield()

struct yieldstate {
    uintptr_t reg_rbp;
    uintptr_t reg_rbx;
    uintptr_t reg_r12;
    uintptr_t reg_r13;
    uintptr_t reg_r14;
    uintptr_t reg_r15;
    uintptr_t reg_rflags;
};


// timekeeping

#define HZ 100                  // number of ticks per second
extern unsigned long ticks;     // number of ticks since boot


// Segment selectors
#define SEGSEL_BOOT_CODE        0x8             // boot code segment
#define SEGSEL_KERN_CODE        0x8             // kernel code segment
#define SEGSEL_KERN_DATA        0x10            // kernel data segment
#define SEGSEL_APP_CODE         0x18            // application code segment
#define SEGSEL_APP_DATA         0x20            // application data segment
#define SEGSEL_TASKSTATE        0x28            // task state segment


// Physical memory size
#define MEMSIZE_PHYSICAL        0x200000
// Virtual memory size
#define MEMSIZE_VIRTUAL         0x300000

enum memtype_t {
    mem_nonexistent = 0, mem_available = 1, mem_kernel = 2, mem_reserved = 3,
    mem_console = 4
};
extern memrangeset<16> physical_ranges;


// Hardware interrupt numbers
#define INT_IRQ                 32
#define IRQ_TIMER               0
#define IRQ_KEYBOARD            1
#define IRQ_IDE                 14
#define IRQ_ERROR               19
#define IRQ_SPURIOUS            31

#define KTEXT_BASE              0xFFFFFFFF80000000UL
#define HIGHMEM_BASE            0xFFFF800000000000UL

inline uint64_t pa2ktext(uint64_t pa) {
    assert(pa < -KTEXT_BASE);
    return pa + KTEXT_BASE;
}

template <typename T>
inline T pa2ktext(uint64_t pa) {
    return reinterpret_cast<T>(pa2ktext(pa));
}

inline uint64_t ktext2pa(uint64_t ka) {
    assert(ka >= KTEXT_BASE);
    return ka - KTEXT_BASE;
}

template <typename T>
inline uint64_t ktext2pa(T* ptr) {
    return ktext2pa(reinterpret_cast<uint64_t>(ptr));
}


inline uint64_t pa2ka(uint64_t pa) {
    assert(pa < -HIGHMEM_BASE);
    return pa + HIGHMEM_BASE;
}

template <typename T>
inline T pa2ka(uint64_t pa) {
    return reinterpret_cast<T>(pa2ka(pa));
}

inline uint64_t ka2pa(uint64_t ka) {
    assert(ka >= HIGHMEM_BASE && ka < KTEXT_BASE);
    return ka - HIGHMEM_BASE;
}

template <typename T>
inline uint64_t ka2pa(T* ptr) {
    return ka2pa(reinterpret_cast<uint64_t>(ptr));
}

template <typename T>
inline bool is_kptr(T* ptr) {
    uintptr_t va = reinterpret_cast<uint64_t>(ptr);
    return va >= HIGHMEM_BASE;
}

template <typename T>
inline bool is_ktext(T* ptr) {
    uintptr_t va = reinterpret_cast<uint64_t>(ptr);
    return va >= KTEXT_BASE;
}


template <typename T> T read_unaligned(const uint8_t* ptr);

template <>
inline uint16_t read_unaligned<uint16_t>(const uint8_t* ptr) {
    return ptr[0] | (ptr[1] << 8);
}

template <>
inline int16_t read_unaligned<int16_t>(const uint8_t* ptr) {
    return ptr[0] | (ptr[1] << 8);
}

template <>
inline uint32_t read_unaligned<uint32_t>(const uint8_t* ptr) {
    return ptr[0] | (ptr[1] << 8) | (ptr[2] << 16) | (ptr[3] << 24);
}

template <>
inline int32_t read_unaligned<int32_t>(const uint8_t* ptr) {
    return ptr[0] | (ptr[1] << 8) | (ptr[2] << 16) | (ptr[3] << 24);
}

template <typename T>
inline T read_unaligned_pa(uint64_t pa) {
    return read_unaligned<T>(pa2ka<const uint8_t*>(pa));
}


// hardware_init
//    Initialize x86 hardware, including memory, interrupts, and segments.
//    All accessible physical memory is initially mapped as readable
//    and writable to both kernel and application code.
void hardware_init();

// timer_init(rate)
//    Set the timer interrupt to fire `rate` times a second. Disables the
//    timer interrupt if `rate <= 0`.
void timer_init(int rate);


// kernel page table (used for virtual memory)
extern x86_64_pagetable early_pagetable[2];

// allocate and initialize a new page table
x86_64_pagetable* kalloc_pagetable();

// change current page table
void set_pagetable(x86_64_pagetable* pagetable);

// turn off the virtual machine
void poweroff() __attribute__((noreturn));

// reboot the virtual machine
void reboot() __attribute__((noreturn));

extern "C" {
void kernel_start(const char* command);
}


// console_show_cursor(cpos)
//    Move the console cursor to position `cpos`, which should be between 0
//    and 80 * 25.
void console_show_cursor(int cpos);


// keyboard_readc
//    Read a character from the keyboard. Returns -1 if there is no character
//    to read, and 0 if no real key press was registered but you should call
//    keyboard_readc() again (e.g. the user pressed a SHIFT key). Otherwise
//    returns either an ASCII character code or one of the special characters
//    listed below.
int keyboard_readc();

#define KEY_UP          0300
#define KEY_RIGHT       0301
#define KEY_DOWN        0302
#define KEY_LEFT        0303
#define KEY_HOME        0304
#define KEY_END         0305
#define KEY_PAGEUP      0306
#define KEY_PAGEDOWN    0307
#define KEY_INSERT      0310
#define KEY_DELETE      0311

// check_keyboard
//    Check for the user typing a control key. 'a', 'f', and 'e' cause a soft
//    reboot where the kernel runs the allocator programs, "fork", or
//    "forkexit", respectively. Control-C or 'q' exit the virtual machine.
//    Returns key typed or -1 for no key.
int check_keyboard();


// program_load(p, programnumber)
//    Load the code corresponding to program `programnumber` into the process
//    `p` and set `p->p_reg.reg_eip` to its entry point. Calls
//    `assign_physical_page` as required. Returns 0 on success and
//    -1 on failure (e.g. out-of-memory). `allocator` is passed to
//    `vm_map`.
int program_load(proc* p, int programnumber);

x86_64_page* kallocpage();
void kfree(x86_64_page*);

// log_printf, log_vprintf
//    Print debugging messages to the host's `log.txt` file. We run QEMU
//    so that messages written to the QEMU "parallel port" end up in `log.txt`.
void log_printf(const char* format, ...) __attribute__((noinline));
void log_vprintf(const char* format, va_list val) __attribute__((noinline));


// log_backtrace
//    Print a backtrace to the host's `log.txt` file.
void log_backtrace(const char* prefix = "");


// error_printf, error_vprintf
//    Print debugging messages to the console and to the host's
//    `log.txt` file via `log_printf`.
int error_printf(int cpos, int color, const char* format, ...)
    __attribute__((noinline));
void error_printf(int color, const char* format, ...) __attribute__((noinline));
void error_printf(const char* format, ...) __attribute__((noinline));
int error_vprintf(int cpos, int color, const char* format, va_list val)
    __attribute__((noinline));


inline cpustate* this_cpu() {
    assert(is_cli());
    cpustate* result;
    asm volatile ("movq %%gs:(0), %0" : "=r" (result));
    return result;
}

inline void adjust_this_cpu_spinlock_depth(int delta) {
    asm volatile ("addl %1, %%gs:%0"
                  : "+m" (*reinterpret_cast<int*>
                          (offsetof(cpustate, spinlock_depth_)))
                  : "er" (delta) : "cc", "memory");
}

inline bool cpustate::contains(void* ptr) const {
    uintptr_t delta =
        reinterpret_cast<uintptr_t>(ptr) - reinterpret_cast<uintptr_t>(this);
    return delta < CPUSTACK_SIZE;
}

inline bool proc::contains(void* ptr) const {
    uintptr_t delta =
        reinterpret_cast<uintptr_t>(ptr) - reinterpret_cast<uintptr_t>(this);
    return delta < KTASKSTACK_SIZE;
}

#endif
