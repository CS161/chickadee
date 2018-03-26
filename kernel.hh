#ifndef CHICKADEE_KERNEL_HH
#define CHICKADEE_KERNEL_HH
#include "x86-64.h"
#include "lib.hh"
#include "k-list.hh"
#include "k-lock.hh"
#include "k-memrange.hh"
#if CHICKADEE_PROCESS
#error "kernel.hh should not be used by process code."
#endif
struct elf_program;
struct proc;
struct yieldstate;


// kernel.hh
//
//    Functions, constants, and definitions for the kernel.


// Process descriptor type
struct __attribute__((aligned(4096))) proc {
    // These three members must come first:
    pid_t pid_;                        // process ID
    regstate* regs_;                   // process's current registers
    yieldstate* yields_;               // process's current yield state
#if HAVE_SANITIZERS
    int sanitizer_status_ = 0;
#endif

    enum state_t {
        blank = 0, runnable, blocked, broken
    };
    state_t state_;                    // process state
    x86_64_pagetable* pagetable_;      // process's page table

    list_links runq_links_;


    proc();
    NO_COPY_OR_ASSIGN(proc);

    inline bool contains(uintptr_t addr) const;
    inline bool contains(void* ptr) const;

    void init_user(pid_t pid, x86_64_pagetable* pt);
    void init_kernel(pid_t pid, void (*f)(proc*));

    struct loader {
        x86_64_pagetable* pagetable_ = nullptr;
        uintptr_t entry_rip_ = 0;
        virtual ssize_t get_page(uint8_t** pg, size_t off) = 0;
        virtual void put_page(uint8_t* pg) = 0;
    };
    static int load(loader& ld);
    int load(const char* binary_name);

    void exception(regstate* reg);
    uintptr_t syscall(regstate* reg);

    void yield();
    void yield_noreturn() __attribute__((noreturn));
    void resume() __attribute__((noreturn));

    inline bool resumable() const;

    inline irqstate lock_pagetable_read();
    inline void unlock_pagetable_read(irqstate& irqs);

 private:
    static int load_segment(const elf_program& ph, loader& ld);
};

#define NPROC 16
extern proc* ptable[NPROC];
extern spinlock ptable_lock;
#define KTASKSTACK_SIZE  4096

// allocate a new `proc` and call its constructor
proc* kalloc_proc() __attribute__((malloc));


// CPU state type
struct __attribute__((aligned(4096))) cpustate {
    // These three members must come first:
    cpustate* self_;
    proc* current_;
    uint64_t syscall_scratch_;

    int index_;
    int lapic_id_;

    list<proc, &proc::runq_links_> runq_;
    spinlock runq_lock_;
    unsigned long nschedule_;
    proc* idle_task_;

    unsigned spinlock_depth_;

    uint64_t gdt_segments_[7];
    x86_64_taskstate task_descriptor_;



    inline cpustate()
        : self_(this), current_(nullptr) {
    }
    NO_COPY_OR_ASSIGN(cpustate);

    inline bool contains(uintptr_t addr) const;
    inline bool contains(void* ptr) const;

    void init();
    void init_ap();

    void exception(regstate* reg);

    void enqueue(proc* p);
    void schedule(proc* yielding_from) __attribute__((noreturn));

    void enable_irq(int irqno);
    void disable_irq(int irqno);

 private:
    void init_cpu_hardware();
    void init_idle_task();
};

#define NCPU 16
extern cpustate cpus[NCPU];
extern int ncpu;
#define CPUSTACK_SIZE 4096

inline cpustate* this_cpu();


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

#define HZ 100                           // number of ticks per second
extern volatile unsigned long ticks;     // number of ticks since boot


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
#define INT_IRQ                 32U
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

inline uint64_t kptr2pa(uint64_t kptr) {
    assert(kptr >= HIGHMEM_BASE);
    return kptr - (kptr >= KTEXT_BASE ? KTEXT_BASE : HIGHMEM_BASE);
}

template <typename T>
inline uint64_t kptr2pa(T* ptr) {
    return kptr2pa(reinterpret_cast<uint64_t>(ptr));
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


// kallocpage
//    Allocate and return a page. Returns `nullptr` on failure.
//    Returns a high canonical address.
x86_64_page* kallocpage() __attribute__((malloc));

// kalloc(sz)
//    Allocate and return a pointer to at least `sz` contiguous bytes
//    of memory. Returns `nullptr` if `sz == 0` or on failure.
void* kalloc(size_t sz) __attribute__((malloc));

// kfree(ptr)
//    Free a pointer previously returned by `kalloc`, `kallocpage`, or
//    `kalloc_pagetable`. Does nothing if `ptr == nullptr`.
void kfree(void* ptr);

// knew<T>()
//    Return a pointer to a newly-allocated object of type `T`. Calls
//    the new object's constructor. Returns `nullptr` on failure.
template <typename T>
inline T* knew() {
    if (void* mem = kalloc(sizeof(T))) {
        return new (mem) T;
    } else {
        return nullptr;
    }
}
template <typename T, typename... Args>
inline T* knew(Args&&... args) {
    if (void* mem = kalloc(sizeof(T))) {
        return new (mem) T(std::forward<Args>(args)...);
    } else {
        return nullptr;
    }
}

// kdelete(ptr)
//    Free an object allocated by `knew`. Calls the object's destructor.
template <typename T>
void kdelete(T* obj) {
    if (obj) {
        obj->~T();
        kfree(obj);
    }
}

// operator new, operator delete
//    Expressions like `new (std::nothrow) T(...)` and `delete x` work,
//    and call kalloc/kfree.
inline void* operator new(size_t sz, const std::nothrow_t&) noexcept {
    return kalloc(sz);
}
inline void* operator new[](size_t sz, const std::nothrow_t&) noexcept {
    return kalloc(sz);
}
inline void operator delete(void* ptr) noexcept {
    kfree(ptr);
}
inline void operator delete(void* ptr, size_t) noexcept {
    kfree(ptr);
}
inline void operator delete[](void* ptr) noexcept {
    kfree(ptr);
}
inline void operator delete[](void* ptr, size_t) noexcept {
    kfree(ptr);
}

// init_kalloc
//    Initialize stuff needed by `kalloc`. Called from `init_hardware`,
//    after `physical_ranges` is initialized.
void init_kalloc();

// run unit tests on the kalloc system
void test_kalloc();


// initialize hardware and CPUs
void init_hardware();

// query machine configuration
unsigned machine_ncpu();
unsigned machine_pci_irq(int pci_addr, int intr_pin);

struct ahcistate;
extern ahcistate* sata_disk;


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


// log_printf, log_vprintf
//    Print debugging messages to the host's `log.txt` file. We run QEMU
//    so that messages written to the QEMU "parallel port" end up in `log.txt`.
void log_printf(const char* format, ...) __attribute__((noinline));
void log_vprintf(const char* format, va_list val) __attribute__((noinline));


// log_backtrace
//    Print a backtrace to the host's `log.txt` file.
void log_backtrace(const char* prefix = "");


#if HAVE_SANITIZERS
// sanitizer functions
void init_sanitizers();
void disable_asan();
void enable_asan();
void asan_mark_memory(unsigned long pa, size_t sz, bool poisoned);
#else
inline void disable_asan() {}
inline void enable_asan() {}
inline void asan_mark_memory(unsigned long, size_t, bool) {}
#endif


// `panicking == true` iff some CPU has panicked
extern bool panicking;


// this_cpu
//    Return a pointer to the current CPU. Requires disabled interrupts.
inline cpustate* this_cpu() {
    assert(is_cli());
    cpustate* result;
    asm volatile ("movq %%gs:(0), %0" : "=r" (result));
    return result;
}

// current
//    Return a pointer to the current `struct proc`.
inline proc* current() {
    proc* result;
    asm volatile ("movq %%gs:(8), %0" : "=r" (result));
    return result;
}

// adjust_this_cpu_spinlock_depth(delta)
//    Adjust this CPU's spinlock_depth_ by `delta`. Does *not* require
//    disabled interrupts.
inline void adjust_this_cpu_spinlock_depth(int delta) {
    asm volatile ("addl %1, %%gs:%0"
                  : "+m" (*reinterpret_cast<int*>
                          (offsetof(cpustate, spinlock_depth_)))
                  : "er" (delta) : "cc", "memory");
}

// cpustate::contains(ptr)
//    Return true iff `ptr` lies within this cpustate's allocation.
inline bool cpustate::contains(void* ptr) const {
    return contains(reinterpret_cast<uintptr_t>(ptr));
}
inline bool cpustate::contains(uintptr_t addr) const {
    uintptr_t delta = addr - reinterpret_cast<uintptr_t>(this);
    return delta <= CPUSTACK_SIZE;
}

// proc::contains(ptr)
//    Return true iff `ptr` lies within this cpustate's allocation.
inline bool proc::contains(void* ptr) const {
    return contains(reinterpret_cast<uintptr_t>(ptr));
}
inline bool proc::contains(uintptr_t addr) const {
    uintptr_t delta = addr - reinterpret_cast<uintptr_t>(this);
    return delta <= KTASKSTACK_SIZE;
}

// proc::resumable()
//    Return true iff this `proc` can be resumed (`regs_` or `yields_`
//    is set). Also checks some assertions about `regs_` and `yields_`.
inline bool proc::resumable() const {
    assert(!(regs_ && yields_));            // at most one at a time
    assert(!regs_ || contains(regs_));      // `regs_` points within this
    assert(!yields_ || contains(yields_));  // same for `yields_`
    return regs_ || yields_;
}

// proc::lock_pagetable_read()
//    Obtain a “read lock” on this process’s page table. While the “read
//    lock” is held, it is illegal to remove or change existing valid
//    mappings in that page table, or to free page table pages.
inline irqstate proc::lock_pagetable_read() {
    return irqstate();
}
inline void proc::unlock_pagetable_read(irqstate&) {
}

#endif
