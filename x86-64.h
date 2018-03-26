#ifndef CHICKADEE_X86_64_H
#define CHICKADEE_X86_64_H
#include "types.h"

// x86-64.h: C code to interface with x86 hardware and CPU.
//
//   Contents:
//   - Memory and interrupt constants.
//   - x86_registers: Used in process descriptors to store x86 registers.
//   - x86 functions: C function wrappers for useful x86 instructions.
//   - Hardware structures: C structures and constants for initializing
//     x86 hardware, including the interrupt descriptor table.

// Paged memory constants
#define PAGEOFFBITS     12                     // # bits in page offset
#define PAGEINDEXBITS   9                      // # bits in a page index level
#define PAGESIZE        (1UL << PAGEOFFBITS)   // Size of page in bytes
#define PAGEOFFMASK     (PAGESIZE - 1)

// Permission flags: define whether page is accessible
#define PTE_P           0x1UL    // entry is Present
#define PTE_W           0x2UL    // entry is Writeable
#define PTE_U           0x4UL    // entry is User-accessible
// Accessed flags: automatically turned on by processor
#define PTE_A           0x20UL   // entry was Accessed (read/written)
#define PTE_D           0x40UL   // entry was Dirtied (written)
// Other special-purpose flags
#define PTE_PS          0x80UL   // entry has a large Page Size
#define PTE_PWT         0x8UL
#define PTE_PCD         0x10UL
#define PTE_XD          0x8000000000000000UL // entry is eXecute Disabled
// There are other flags too!

#define PTE_PAMASK      0x0007FFFFFFFFF000UL // physical address in non-PS entry
#define PTE_PS_PAMASK   0x0007FFFFFFFFE000UL // physical address in PS entry

#define VA_LOWMIN       0UL                  // min low canonical address
#define VA_LOWMAX       0x00007FFFFFFFFFFFUL // max low canonical address
#define VA_LOWEND       0x0000800000000000UL // one past `VA_LOWMAX`
#define VA_HIGHMIN      0xFFFF800000000000UL // min high canonical address
#define VA_HIGHMAX      0xFFFFFFFFFFFFFFFFUL // max high canonical address
#define VA_NONCANONMAX  0x0000FFFFFFFFFFFFUL // max non-canonical address
#define VA_NONCANONEND  0x0001000000000000UL // one past `VA_NONCANONMAX`

#define PA_IOLOWMIN     0x00000000000A0000UL // min address of MMIO region 1
#define PA_IOLOWEND     0x0000000000100000UL // end address of MMIO region 1
#define PA_IOHIGHMIN    0x00000000C0000000UL // min address of MMIO region 2
#define PA_IOHIGHEND    0x0000000100000000UL // end address of MMIO region 2

// Parts of a paged address: page index, page offset
static inline int pageindex(uintptr_t addr, int level) {
    return (int) (addr >> (PAGEOFFBITS + level * PAGEINDEXBITS)) & 0x1FF;
}
static inline uintptr_t pageoffmask(int level) {
    return (1UL << (PAGEOFFBITS + level * PAGEINDEXBITS)) - 1;
}
static inline uintptr_t pageoffset(uintptr_t addr, int level) {
    return addr & pageoffmask(level);
}
static inline bool va_is_canonical(uintptr_t va) {
    return va <= VA_LOWMAX || va >= VA_HIGHMIN;
}


// Page table entry type and page table type
typedef struct __attribute__((aligned(PAGESIZE))) x86_64_page {
    uint8_t x[PAGESIZE];
} x86_64_page;

typedef uint64_t x86_64_pageentry_t;
typedef struct __attribute__((aligned(PAGESIZE))) x86_64_pagetable {
    x86_64_pageentry_t entry[1 << PAGEINDEXBITS];
} x86_64_pagetable;

// Page fault error flags
// These bits are stored in x86_registers::reg_err after a page fault trap.
#define PFERR_PRESENT   0x1             // Fault happened due to a protection
                                        //   violation (rather than due to a
                                        //   missing page)
#define PFERR_WRITE     0x2             // Fault happened on a write
#define PFERR_USER      0x4             // Fault happened in an application
                                        //   (user mode) (rather than kernel)


// Interrupt numbers
#define INT_DIVIDE      0x0         // Divide error (#DE)
#define INT_DEBUG       0x1         // Debug (#DB)
#define INT_BREAKPOINT  0x3         // Breakpoint (#BP)
#define INT_OVERFLOW    0x4         // Overflow (#OF)
#define INT_BOUNDS      0x5         // BOUND range exceeded (#BR)
#define INT_INVALIDOP   0x6         // Invalid opcode (#UD)
#define INT_DEVICE      0x7         // Device not available (#NM)
#define INT_DOUBLEFAULT 0x8         // Double fault (#DF)
#define INT_INVALIDTSS  0xA         // Invalid TSS (#TS)
#define INT_SEGMENT     0xB         // Segment not present (#NP)
#define INT_STACK       0xC         // Stack fault (#SS)
#define INT_GPF         0xD         // General protection (#GP)
#define INT_PAGEFAULT   0xE         // Page fault (#PF)


// struct regstate
//     A complete set of x86-64 general-purpose registers, plus some
//     special-purpose registers. The order and contents are defined to make
//     it more convenient to use important x86-64 instructions.

typedef struct regstate {
    // General-purpose registers
    uint64_t reg_rax;
    uint64_t reg_rcx;
    uint64_t reg_rdx;
    uint64_t reg_rbx;
    uint64_t reg_rbp;
    uint64_t reg_rsi;
    uint64_t reg_rdi;
    uint64_t reg_r8;
    uint64_t reg_r9;
    uint64_t reg_r10;
    uint64_t reg_r11;
    uint64_t reg_r12;
    uint64_t reg_r13;
    uint64_t reg_r14;
    uint64_t reg_r15;
    uint64_t reg_fs;
    uint64_t reg_gs;

    // Interrupt number, -1 for syscall
    uint64_t reg_intno;
    // Error code (supplied by hardware for some x86-64 exceptions)
    uint64_t reg_err;

    // Task status (pushed by exception mechanism, read by `iret`)
    uint64_t reg_rip;
    uint64_t reg_cs;
    uint64_t reg_rflags;
    uint64_t reg_rsp;
    uint64_t reg_ss;
} regstate;


// x86 functions: Inline C functions that execute useful x86 instructions.
//
//      Also some macros corresponding to x86 register flag bits.

#define DECLARE_X86_FUNCTION(function_prototype) \
        static inline function_prototype __attribute__((always_inline))

DECLARE_X86_FUNCTION(void       breakpoint());
DECLARE_X86_FUNCTION(uint8_t    inb(int port));
DECLARE_X86_FUNCTION(void       insb(int port, void* addr, int cnt));
DECLARE_X86_FUNCTION(uint16_t   inw(int port));
DECLARE_X86_FUNCTION(void       insw(int port, void* addr, int cnt));
DECLARE_X86_FUNCTION(uint32_t   inl(int port));
DECLARE_X86_FUNCTION(void       insl(int port, void* addr, int cnt));
DECLARE_X86_FUNCTION(void       outb(int port, uint8_t data));
DECLARE_X86_FUNCTION(void       outsb(int port, const void* addr, int cnt));
DECLARE_X86_FUNCTION(void       outw(int port, uint16_t data));
DECLARE_X86_FUNCTION(void       outsw(int port, const void* addr, int cnt));
DECLARE_X86_FUNCTION(void       outsl(int port, const void* addr, int cnt));
DECLARE_X86_FUNCTION(void       outl(int port, uint32_t data));
DECLARE_X86_FUNCTION(void       invlpg(void* addr));
DECLARE_X86_FUNCTION(void       lidt(void* p));
DECLARE_X86_FUNCTION(void       lldt(uint16_t sel));
DECLARE_X86_FUNCTION(void       ltr(uint16_t sel));
DECLARE_X86_FUNCTION(void       lcr0(uint32_t val));
DECLARE_X86_FUNCTION(uint32_t   rcr0());
DECLARE_X86_FUNCTION(uintptr_t  rcr2());
DECLARE_X86_FUNCTION(void       lcr3(uintptr_t val));
DECLARE_X86_FUNCTION(uintptr_t  rcr3());
DECLARE_X86_FUNCTION(void       lcr4(uint64_t val));
DECLARE_X86_FUNCTION(uint64_t   rcr4());
DECLARE_X86_FUNCTION(void       cli());
DECLARE_X86_FUNCTION(void       sti());
DECLARE_X86_FUNCTION(void       tlbflush());
DECLARE_X86_FUNCTION(uint32_t   read_eflags());
DECLARE_X86_FUNCTION(void       write_eflags(uint32_t eflags));
DECLARE_X86_FUNCTION(uintptr_t  read_rbp());
DECLARE_X86_FUNCTION(uintptr_t  read_rsp());
DECLARE_X86_FUNCTION(void       pause());
typedef struct x86_64_cpuid_t {
    uint32_t eax, ebx, ecx, edx;
} x86_64_cpuid_t;
DECLARE_X86_FUNCTION(x86_64_cpuid_t cpuid(uint32_t info));
DECLARE_X86_FUNCTION(uint64_t   rdtsc());
typedef struct x86_64_msr_t {
    union {
        struct {
            uint32_t eax;
            uint32_t edx;
        };
        uint64_t v;
    };
} x86_64_msr_t;
DECLARE_X86_FUNCTION(uint64_t rdmsr(uint32_t msr));
DECLARE_X86_FUNCTION(void     wrmsr(uint32_t msr, uint64_t v));

// %cr0 flag bits (useful for lcr0() and rcr0())
#define CR0_PE                  0x00000001      // Protection Enable
#define CR0_MP                  0x00000002      // Monitor coProcessor
#define CR0_EM                  0x00000004      // Emulation
#define CR0_TS                  0x00000008      // Task Switched
#define CR0_ET                  0x00000010      // Extension Type
#define CR0_NE                  0x00000020      // Numeric Errror
#define CR0_WP                  0x00010000      // Write Protect
#define CR0_AM                  0x00040000      // Alignment Mask
#define CR0_NW                  0x20000000      // Not Writethrough
#define CR0_CD                  0x40000000      // Cache Disable
#define CR0_PG                  0x80000000      // Paging

// %cr4 flag bits
#define CR4_PSE                 0x00000010      // Page Size Extensions
#define CR4_PAE                 0x00000020      // Physical Address Extensions

// eflags bits (useful for read_eflags() and write_eflags())
#define EFLAGS_CF               0x00000001      // Carry Flag
#define EFLAGS_PF               0x00000004      // Parity Flag
#define EFLAGS_AF               0x00000010      // Auxiliary carry Flag
#define EFLAGS_ZF               0x00000040      // Zero Flag
#define EFLAGS_SF               0x00000080      // Sign Flag
#define EFLAGS_TF               0x00000100      // Trap Flag
#define EFLAGS_IF               0x00000200      // Interrupt Flag
#define EFLAGS_DF               0x00000400      // Direction Flag
#define EFLAGS_OF               0x00000800      // Overflow Flag
#define EFLAGS_IOPL_MASK        0x00003000      // I/O Privilege Level bitmask
#define EFLAGS_IOPL_0           0x00000000      //   IOPL == 0
#define EFLAGS_IOPL_1           0x00001000      //   IOPL == 1
#define EFLAGS_IOPL_2           0x00002000      //   IOPL == 2
#define EFLAGS_IOPL_3           0x00003000      //   IOPL == 3
#define EFLAGS_NT               0x00004000      // Nested Task
#define EFLAGS_RF               0x00010000      // Resume Flag
#define EFLAGS_VM               0x00020000      // Virtual 8086 mode
#define EFLAGS_AC               0x00040000      // Alignment Check
#define EFLAGS_VIF              0x00080000      // Virtual Interrupt Flag
#define EFLAGS_VIP              0x00100000      // Virtual Interrupt Pending
#define EFLAGS_ID               0x00200000      // ID flag

static inline void breakpoint() {
    asm volatile("int3");
}

static inline uint8_t inb(int port) {
    uint8_t data;
    asm volatile("inb %w1,%0" : "=a" (data) : "d" (port));
    return data;
}

static inline void insb(int port, void* addr, int cnt) {
    asm volatile("cld\n\trep\n\tinsb"
                 : "+D" (addr), "+c" (cnt), "=m" (*(char (*)[cnt]) addr)
                 : "d" (port)
                 : "cc");
}

static inline uint16_t inw(int port) {
    uint16_t data;
    asm volatile("inw %w1,%0" : "=a" (data) : "d" (port));
    return data;
}

static inline void insw(int port, void* addr, int cnt) {
    asm volatile("cld\n\trep\n\tinsw"
                 : "+D" (addr), "+c" (cnt),
                   "=m" (*(unsigned short (*)[cnt]) addr)
                 : "d" (port)
                 : "cc");
}

static inline uint32_t inl(int port) {
    uint32_t data;
    asm volatile("inl %w1,%0" : "=a" (data) : "d" (port));
    return data;
}

static inline void insl(int port, void* addr, int cnt) {
    asm volatile("cld\n\trep\n\tinsl"
                 : "+D" (addr), "+c" (cnt),
                   "=m" (*(unsigned (*)[cnt]) addr)
                 : "d" (port)
                 : "cc");
}

static inline void outb(int port, uint8_t data) {
    asm volatile("outb %0,%w1" : : "a" (data), "d" (port));
}

static inline void outsb(int port, const void* addr, int cnt) {
    asm volatile("cld\n\trepne\n\toutsb"
                 : "+S" (addr), "+c" (cnt)
                 : "d" (port), "m" (*(const char (*)[cnt]) addr)
                 : "cc");
}

static inline void outw(int port, uint16_t data) {
    asm volatile("outw %0,%w1" : : "a" (data), "d" (port));
}

static inline void outsw(int port, const void* addr, int cnt) {
    asm volatile("cld\n\trep\n\toutsw"
                 : "+S" (addr), "+c" (cnt)
                 : "d" (port), "m" (*(const unsigned short (*)[cnt]) addr)
                 : "cc");
}

static inline void outsl(int port, const void* addr, int cnt) {
    asm volatile("cld\n\trep\n\toutsl"
                 : "+S" (addr), "+c" (cnt)
                 : "d" (port), "m" (*(const unsigned (*)[cnt]) addr)
                 : "cc");
}

static inline void outl(int port, uint32_t data) {
    asm volatile("outl %0,%w1" : : "a" (data), "d" (port));
}

static inline void invlpg(void* addr) {
    asm volatile("invlpg (%0)" : : "r" (addr) : "memory");
}

static inline void lidt(void* p) {
    asm volatile("lidt (%0)" : : "r" (p));
}

static inline void lldt(uint16_t sel) {
    asm volatile("lldt %0" : : "r" (sel));
}

static inline void ltr(uint16_t sel) {
    asm volatile("ltr %0" : : "r" (sel));
}

static inline void lcr0(uint32_t val) {
    uint64_t xval = val;
    asm volatile("movq %0,%%cr0" : : "r" (xval));
}

static inline uint32_t rcr0() {
    uint64_t val;
    asm volatile("movq %%cr0,%0" : "=r" (val));
    return val;
}

static inline uintptr_t rcr2() {
    uintptr_t val;
    asm volatile("movq %%cr2,%0" : "=r" (val));
    return val;
}

static inline void lcr3(uintptr_t val) {
    asm volatile("" : : : "memory");
    asm volatile("movq %0,%%cr3" : : "r" (val) : "memory");
}

static inline uintptr_t rcr3() {
    uintptr_t val;
    asm volatile("movq %%cr3,%0" : "=r" (val));
    return val;
}

static inline void lcr4(uint64_t val) {
    asm volatile("movq %0,%%cr4" : : "r" (val));
}

static inline uint64_t rcr4() {
    uint64_t cr4;
    asm volatile("movl %%cr4,%0" : "=r" (cr4));
    return cr4;
}

static inline void cli() {
    asm volatile("cli");
}

static inline void sti() {
    asm volatile("sti");
}

static inline bool is_cli() {
    return (read_eflags() & EFLAGS_IF) == 0;
}

static inline void tlbflush() {
    uint32_t cr3;
    asm volatile("movl %%cr3,%0" : "=r" (cr3));
    asm volatile("movl %0,%%cr3" : : "r" (cr3));
}

static inline uint32_t read_eflags() {
    uint64_t rflags;
    asm volatile("pushfq; popq %0" : "=rm" (rflags) : : "memory");
    return rflags;
}

static inline void write_eflags(uint32_t eflags) {
    uint64_t rflags = eflags; // really only lower 32 bits are used
    asm volatile("pushq %0; popfq" : : "r" (rflags) : "memory", "cc");
}

static inline uintptr_t read_rbp() {
    uintptr_t rbp;
    asm volatile("movq %%rbp,%0" : "=r" (rbp));
    return rbp;
}

static inline uintptr_t read_rsp() {
    uintptr_t rsp;
    asm volatile("movq %%rsp,%0" : "=r" (rsp));
    return rsp;
}

static inline struct x86_64_cpuid_t cpuid(uint32_t info) {
    x86_64_cpuid_t ret;
    asm volatile("cpuid"
                 : "=a" (ret.eax), "=b" (ret.ebx),
                   "=c" (ret.ecx), "=d" (ret.edx)
                 : "a" (info));
    return ret;
}

static inline uint64_t rdmsr(uint32_t msr) {
    uint64_t low, high;
    asm volatile("rdmsr" : "=a" (low), "=d" (high) : "c" (msr));
    return low | (high << 32);
}

static inline void wrmsr(uint32_t msr, uint64_t x) {
    asm volatile("wrmsr" : : "c" (msr), "a" ((uint32_t) x), "d" (x >> 32));
}

static inline uint64_t rdtsc() {
    uint64_t low, high;
    asm volatile("rdtsc" : "=a" (low), "=d" (high));
    return low | (high << 32);
}

static inline void pause() {
    asm volatile("pause" : : : "memory");
}


// Hardware definitions: C structures and constants for initializing x86
// hardware, particularly gate descriptors (loaded into the interrupt
// descriptor table) and segment descriptors.

// Pseudo-descriptors used for LGDT, LLDT, and LIDT instructions
typedef struct __attribute__((packed, aligned(8))) x86_64_pseudodescriptor {
    uint16_t reserved[3];
    uint16_t limit;            // Limit
    uint64_t base;             // Base address
} x86_64_pseudodescriptor;

// Task state structure defines kernel stack for interrupt handlers
typedef struct __attribute__((packed, aligned(8))) x86_64_taskstate {
    uint32_t ts_reserved0;
    uint64_t ts_rsp[3];
    uint64_t ts_ist[7];
    uint64_t ts_reserved1;
    uint16_t ts_reserved2;
    uint16_t ts_iomap_base;
} x86_64_taskstate;

// Gate descriptor structure defines interrupt handlers
typedef struct x86_64_gatedescriptor {
    uint64_t gd_low;
    uint64_t gd_high;
} x86_64_gatedescriptor;

// Segment bits
#define X86SEG_S        (1UL << 44)
#define X86SEG_P        (1UL << 47)
#define X86SEG_L        (1UL << 53)
#define X86SEG_DB       (1UL << 54)
#define X86SEG_G        (1UL << 55)

// Application segment type bits
#define X86SEG_A        (0x1UL << 40) // Accessed
#define X86SEG_R        (0x2UL << 40) // Readable (code segment)
#define X86SEG_W        (0x2UL << 40) // Writable (data segment)
#define X86SEG_C        (0x4UL << 40) // Conforming (code segment)
#define X86SEG_E        (0x4UL << 40) // Expand-down (data segment)
#define X86SEG_X        (0x8UL << 40) // Executable (== is code segment)

// System segment/interrupt descriptor types
#define X86SEG_TSS              (0x9UL << 40)
#define X86GATE_CALL            0xC
#define X86GATE_INTERRUPT       0xE
#define X86GATE_TRAP            0xF

// Keyboard programmed I/O
#define KEYBOARD_STATUSREG      0x64
#define KEYBOARD_STATUS_READY   0x01
#define KEYBOARD_DATAREG        0x60

// BIOS Data Area
#define X86_BDA_PA		     0x400
#define X86_BDA_EBDA_BASE_ADDRESS_PA 0x40E
#define X86_BDA_BASE_MEMORY_SIZE_PA  0x413

#define MSR_IA32_APIC_BASE           0x1B
#define MSR_IA32_MTRR_CAP            0xFE
#define MSR_IA32_MTRR_BASE           0x200
#define MSR_IA32_MTRR_FIX64K_00000   0x250
#define MSR_IA32_MTRR_FIX16K_80000   0x258
#define MSR_IA32_MTRR_FIX16K_A0000   0x259
#define MSR_IA32_MTRR_FIX4K_C0000    0x268
#define MSR_IA32_MTRR_DEF_TYPE       0x2FF
#define MSR_IA32_EFER                0xC0000080U
#define MSR_IA32_FS_BASE             0xC0000100U
#define MSR_IA32_GS_BASE             0xC0000101U
#define MSR_IA32_KERNEL_GS_BASE      0xC0000102U
#define MSR_IA32_STAR                0xC0000081U
#define MSR_IA32_LSTAR               0xC0000082U
#define MSR_IA32_FMASK               0xC0000084U

#define IA32_EFER_SCE                0x1             // enable syscall/sysret
#define IA32_EFER_LME                0x100           // enable 64-bit mode
#define IA32_EFER_NXE                0x800           // enable PTE_XD

#define IA32_APIC_BASE_ENABLED       0x800           // enable LAPIC

static inline uint64_t x86_64_skip_reserved_pa(uint64_t pa) {
    if (pa >= 0xA0000 && pa < 0x100000) {
        return 0x100000;
    } else if (pa >= 0xC0000000 && pa < 0x100000000) {
        return 0x100000000;
    } else {
        return pa;
    }
}

#endif /* !CHICKADEE_X86_64_H */
