#ifndef CHICKADEE_P_LIB_H
#define CHICKADEE_P_LIB_H
#include "lib.hh"
#include "x86-64.h"
#if CHICKADEE_KERNEL
#error "u-lib.hh should not be used by kernel code."
#endif

// u-lib.hh
//
//    Support code for Chickadee user-level code.


// SYSTEM CALLS

inline uintptr_t make_syscall(int syscallno) {
    register uintptr_t rax asm("rax") = syscallno;
    asm volatile ("syscall"
                  : "+a" (rax)
                  :
                  : "cc", "rcx", "rdx", "rsi", "rdi",
                    "r8", "r9", "r10", "r11");
    return rax;
}

inline uintptr_t make_syscall(int syscallno, uintptr_t arg0) {
    register uintptr_t rax asm("rax") = syscallno;
    asm volatile ("syscall"
                  : "+a" (rax), "+D" (arg0)
                  :
                  : "cc", "rcx", "rdx", "rsi", "r8", "r9", "r10", "r11");
    return rax;
}

inline uintptr_t make_syscall(int syscallno, uintptr_t arg0,
                              uintptr_t arg1) {
    register uintptr_t rax asm("rax") = syscallno;
    asm volatile ("syscall"
                  : "+a" (rax), "+D" (arg0), "+S" (arg1)
                  :
                  : "cc", "rcx", "rdx", "r8", "r9", "r10", "r11");
    return rax;
}

inline uintptr_t make_syscall(int syscallno, uintptr_t arg0,
                              uintptr_t arg1, uintptr_t arg2) {
    register uintptr_t rax asm("rax") = syscallno;
    asm volatile ("syscall"
                  : "+a" (rax), "+D" (arg0), "+S" (arg1), "+d" (arg2)
                  :
                  : "cc", "rcx", "r8", "r9", "r10", "r11");
    return rax;
}

inline uintptr_t make_syscall(int syscallno, uintptr_t arg0,
                              uintptr_t arg1, uintptr_t arg2,
                              uintptr_t arg3) {
    register uintptr_t rax asm("rax") = syscallno;
    register uintptr_t r10 asm("r10") = arg3;
    asm volatile ("syscall"
                  : "+a" (rax), "+D" (arg0), "+S" (arg1), "+d" (arg2),
                    "+r" (r10)
                  :
                  : "cc", "rcx", "r8", "r9", "r11");
    return rax;
}

inline void clobber_memory(void* ptr) {
    asm volatile ("" : "+m" (*(char*) ptr));
}

inline void access_memory(const void* ptr) {
    asm volatile ("" : : "m" (*(const char*) ptr));
}


// sys_getpid
//    Return current process ID.
inline pid_t sys_getpid() {
    return make_syscall(SYSCALL_GETPID);
}

// sys_yield
//    Yield control of the CPU to the kernel. The kernel will pick another
//    process to run, if possible.
inline void sys_yield() {
    make_syscall(SYSCALL_YIELD);
}

// sys_pause()
//    A version of `sys_yield` that spins in the kernel long enough
//    for kernel timer interrupts to occur.
inline void sys_pause() {
    make_syscall(SYSCALL_PAUSE);
}

// sys_kdisplay(display_type)
//    Set the display type (one of the KDISPLAY constants).
inline int sys_kdisplay(int display_type) {
    return make_syscall(SYSCALL_KDISPLAY, display_type);
}

// sys_panic(msg)
//    Panic.
[[noreturn]] inline void sys_panic(const char* msg) {
    make_syscall(SYSCALL_PANIC, reinterpret_cast<uintptr_t>(msg));
    while (1) {
    }
}

// sys_page_alloc(addr)
//    Allocate a page of memory at address `addr`. `Addr` must be page-aligned
//    (i.e., a multiple of PAGESIZE == 4096). Returns 0 on success and -1
//    on failure.
inline int sys_page_alloc(void* addr) {
    return make_syscall(SYSCALL_PAGE_ALLOC, reinterpret_cast<uintptr_t>(addr));
}

// sys_fork()
//    Fork the current process. On success, return the child's process ID to
//    the parent, and return 0 to the child. On failure, return -1.
inline pid_t sys_fork() {
    return make_syscall(SYSCALL_FORK);
}

#endif
