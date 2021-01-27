###############################################################################
# Exception handlers
#
#   Assembly code defining kernel exception handlers
#   (for interrupts, traps, and faults).

// import constants from kernel.hh and x86-64.h
#include "obj/k-asm.h"

.text

// kernel_entry
//    The bootloader jumps here after loading the kernel.
//    The code initializes `%rsp` to the top of the `cpus[0]`
//    cpustate page, then jumps to `kernel_start`.
//    (NB: `cpus[0]` itself is not initialized yet!)
.p2align 12
.globl kernel_entry
kernel_entry:
        // initialize stack pointer and base pointer
        movq $(cpus + CPUSTACK_SIZE - 8), %rsp
        movq %rsp, %rbp
        // clear `%rflags`
        pushq $0
        popfq
        // check for multiboot command line; if found pass it along
        cmpl $0x2BADB002, %eax
        jne 1f
        testl $4, (%rbx)
        je 1f
        movl 16(%rbx), %edi
        jmp 2f
1:      movq $0, %rdi
2:      // call kernel_start()
        jmp _Z12kernel_startPKc



// Exception handlers and interrupt descriptor table
//    This code creates an exception handler for all 256 possible
//    exceptions, and initializes a table in the
//    `.interrupt_descriptors` section containing those handlers.
//    The `init_hardware` kernel function installs this table.

// The `exception_handler` macro creates one exception handler
.altmacro
.macro exception_handler num
exception_entry_\num:
        // push zero error code, unless exception did so already
    .if \num != INT_DF && (\num < INT_TS || \num > INT_PF) && \num != INT_AC
        pushq $0
    .endif
        // push exception number
        pushq $\num
        // jump to exception entry point
    .if \num != INT_DB && \num != INT_NM && \num != INT_MC
        jmp exception_entry
    .else
        jmp alt_exception_entry
    .endif

        // add that handler to the `.interrupt_descriptors` section
    .pushsection .interrupt_descriptors, "aw", @progbits
        .quad exception_entry_\num
        .quad 0
    .popsection
.endm

// now create all 256 exception handlers and table
.set exception_number, 0
.rept 256
exception_handler %exception_number
.set exception_number, exception_number + 1
.endr


// Exception entry point
//    Most exception handlers jump here.
exception_entry:
        /* On entry, the stack looks like this:
           +-----------------------+ <- %rsp
           | reg_intno, reg_swapgs |    0(%rsp)
           | reg_errcode           |    8(%rsp)
           | %rip                  |    16(%rsp)
           | %cs                   |    24(%rsp)
           | %rflags               |    32(%rsp)
           | %rsp                  |    40(%rsp)
           | %ss                   |    48(%rsp)
           ...                   ...

           This data should be stored on the current kernel task stack.
           It’s already there if the exception happened in kernel mode.
           Otherwise, we must move it there. */

        // Exception happened in user mode if `(%cs & 3) != 0`.
        testb $3, 24(%rsp)
        jz exception_entry_finish
        orl $1, 4(%rsp)                // tell resume to swapgs

        // change %rsp to the top of the kernel task stack
        swapgs
        movq %gs:(8), %rsp
        addq $PROCSTACK_SIZE, %rsp

        // copy data from CPU stack to kernel task stack
        pushq %gs:(CPUSTACK_SIZE - 8)  // %ss
        pushq %gs:(CPUSTACK_SIZE - 16) // %rsp
        pushq %gs:(CPUSTACK_SIZE - 24) // %rflags
        pushq %gs:(CPUSTACK_SIZE - 32) // %cs
        pushq %gs:(CPUSTACK_SIZE - 40) // %rip
        pushq %gs:(CPUSTACK_SIZE - 48) // error code
        pushq %gs:(CPUSTACK_SIZE - 56) // interrupt number

exception_entry_finish:
        // complete `struct regstate`
        push %gs
        push %fs
        pushq %r15
        pushq %r14
        pushq %r13
        pushq %r12
        pushq %r11
        pushq %r10
        pushq %r9
        pushq %r8
        pushq %rdi
        pushq %rsi
        pushq %rbp
        pushq %rbx
        pushq %rdx
        pushq %rcx
        pushq %rax

1:      testb $1, panicking            // loop if panicking
        jne 1b

        // call `proc::exception(regstate*)`
        // load current `proc` from the current cpustate,
        // which is accessible via `%gs`
        movq %gs:(8), %rdi
        // the second argument, `regstate`, is the current `%rsp`
        movq %rsp, %rsi
        call _ZN4proc9exceptionEP8regstate

restore_and_iret:
        testb $1, 140(%rsp)            // `reg_swapgs & 1`
        jz 1f
        // resuming in user mode: assert process is runnable
        movq %gs:(8), %rdi
        cmpl $PROC_RUNNABLE, 24(%rdi)
        jne panic_nonrunnable

1:      // restore `regstate` registers
        popq %rax
        popq %rcx
        popq %rdx
        popq %rbx
        popq %rbp
        popq %rsi
        popq %rdi
        popq %r8
        popq %r9
        popq %r10
        popq %r11
        popq %r12
        popq %r13
        popq %r14
        popq %r15
        pop %fs

        // must `swapgs` if required
        testb $1, 12(%rsp)             // `reg_swapgs & 1`
        jnz 1f

        // return to kernel mode
        addq $24, %rsp
        iretq

1:      // return to user mode
        cli                            // prevent interrupts after `swapgs`
        swapgs
        pop %gs
        addq $16, %rsp
        iretq


// syscall_entry
//    Kernel entry point for the `syscall` instruction

.globl syscall_entry
syscall_entry:
        swapgs
        movq %rsp, %gs:(16)            // save entry %rsp in scratch space
        movq %gs:(8), %rsp             // change to kernel task stack
        addq $PROCSTACK_SIZE, %rsp

        pushq $(SEGSEL_APP_DATA + 3)   // %ss
        pushq %gs:(16)                 // %rsp
        pushq %r11                     // %rflags
        pushq $(SEGSEL_APP_CODE + 3)   // %cs
        pushq %rcx                     // %rip

        subq $8, %rsp                  // error code unused
        pushq $-1                      // reg_swapgs, reg_intno
        push %gs   // must restore
        push %fs   // must restore
        pushq %r15 // callee saved
        pushq %r14 // callee saved
        pushq %r13 // callee saved
        pushq %r12 // callee saved
        subq $8, %rsp                  // %r11 clobbered by `syscall`
        pushq %r10
        pushq %r9
        pushq %r8
        pushq %rdi
        pushq %rsi
        pushq %rbp // callee saved
        pushq %rbx // callee saved
        pushq %rdx
        subq $8, %rsp                  // %rcx clobbered by `syscall`
        pushq %rax

1:      testb $1, panicking            // loop if panicking
        jne 1b

        movq %gs:(8), %rdi
        movq %rsp, %rsi
        call _ZN4proc7syscallEP8regstate

        // When `proc::syscall` returns, `%rax` holds the return value.
        // Preserve that, plus all callee-saved registers.

        movq %gs:(8), %rdi             // assert process is runnable
        cmpl $PROC_RUNNABLE, 24(%rdi)
        jne panic_nonrunnable

        addq $(8 * 15), %rsp           // skip general-purpose registers

        pop %fs                        // restore `%fs` and `%gs`
        cli                            // prevent interrupts after `swapgs`
        swapgs
        pop %gs

        addq $(8 * 2), %rsp
        iretq


panic_nonrunnable:
        call _ZN4proc17panic_nonrunnableEv


// proc::yield()
.globl _ZN4proc5yieldEv
_ZN4proc5yieldEv:
        // only save callee-saved registers and rflags
        pushfq
        pushq %r15
        pushq %r14
        pushq %r13
        pushq %r12
        pushq %rbx
        pushq %rbp

        // check if interrupts are disabled
        testq $EFLAGS_IF, 48(%rsp)
        jnz 1f
        // if interrupts are disabled, momentarily enable them.
        // This bounds interrupt delay to the time it takes a
        // single kernel task to yield.
        // Note that `sti; cli` would not work! `sti` only enables
        // external, maskable interrupts at the end of the *next*
        // instruction. A no-op instruction is required.
        sti
        movq (%rsp), %rax     // any delayed interrupts will happen here

1:      // disable interrupts, store yieldstate pointer
        cli
        movq %rsp, 16(%rdi)
        // jump to `proc::yield_noreturn`
        jmp _ZN4proc14yield_noreturnEv



// proc::yield_noreturn()
.globl _ZN4proc14yield_noreturnEv
_ZN4proc14yield_noreturnEv:
        // switch to cpustack
        movq %rdi, %rsi
        movq %gs:(0), %rdi
        leaq CPUSTACK_SIZE(%rdi), %rsp

        // jump to scheduler
        jmp _ZN8cpustate8scheduleEP4proc



// proc::resume()
.globl _ZN4proc6resumeEv
_ZN4proc6resumeEv:
1:      testb $1, panicking            // loop if panicking
        jne 1b

        // do we have yieldstate? (is `this->yields_ != nullptr`?)
        movq 16(%rdi), %rax
        testq %rax, %rax
        jnz resume_yieldstate

resume_regstate: // no yieldstate, jump to the regstate
        // assert(this->contains(regs_))
        movq 8(%rdi), %rax
        subq %rdi, %rax
        cmpq $PROCSTACK_SIZE, %rax
        jae resume_regstate_fail
        // restore stack pointer, clear regstate, pop regs
        movq 8(%rdi), %rsp
        movq $0, 8(%rdi)
        jmp restore_and_iret

resume_yieldstate: // jump to the yieldstate
        // assert(this->contains(yields_))
        subq %rdi, %rax
        cmpq $PROCSTACK_SIZE, %rax
        jae resume_yieldstate_fail
        // restore stack pointer, clear yieldstate, pop callee-saved regs
        movq 16(%rdi), %rsp
        movq $0, 16(%rdi)
        popq %rbp
        popq %rbx
        popq %r12
        popq %r13
        popq %r14
        popq %r15
        popfq
        // return to the `proc::yield()` caller
        retq

resume_regstate_fail:
        movq $proc_contains_regs_assert, %rdx
        jmp 1f
resume_yieldstate_fail:
        movq $proc_contains_yields_assert, %rdx
1:      xorl %ecx, %ecx
        xorl %esi, %esi
        movq $k_exception_str, %rdi
        call _Z11assert_failPKciS0_S0_
1:      jmp 1b


.section .rodata.str1.1
k_exception_str:
        .asciz "k-exception.S"
proc_contains_regs_assert:
        .asciz "this->contains(regs_)"
proc_contains_yields_assert:
        .asciz "this->contains(yields_)"


// Alternate exception entry point
//    Some exceptions can happen at weird times, either because a process
//    does something sneaky (http://everdox.net/popss.pdf) or because they
//    cannot be masked. They must use an alternate stack.
alt_exception_entry:
        // swapgs unless current GS base is negative (= kernel GS base)
        pushq %rax
        pushq %rcx
        pushq %rdx
        movl $MSR_IA32_GS_BASE, %ecx
        rdmsr
        testl %edx, %edx
        js 1f
        orl $1, 28(%rsp)
        swapgs

1:      // test whether old stack pointer was in kernel task stack
        movq 64(%rsp), %rax        // old %rsp
        subq %gs:(8), %rax
        cmpq $PROCSTACK_SIZE, %rax
        jbe 1f

        // it wasn’t: exception data belongs at top of kernel task stack
        movq %gs:(8), %rsp
        addq $PROCSTACK_SIZE, %rsp
        jmp 2f

1:      // it was: exception data belongs below previous kernel task stack
        movq 64(%rsp), %rsp

2:      // copy data from CPU stack to kernel task stack
        pushq %gs:(CPUALTSTACK_SIZE - 8)
        pushq %gs:(CPUALTSTACK_SIZE - 16)
        pushq %gs:(CPUALTSTACK_SIZE - 24)
        pushq %gs:(CPUALTSTACK_SIZE - 32)
        pushq %gs:(CPUALTSTACK_SIZE - 40)
        pushq %gs:(CPUALTSTACK_SIZE - 48)
        pushq %gs:(CPUALTSTACK_SIZE - 56)
        movq %gs:(CPUALTSTACK_SIZE - 64), %rax
        movq %gs:(CPUALTSTACK_SIZE - 72), %rcx
        movq %gs:(CPUALTSTACK_SIZE - 80), %rdx
        jmp exception_entry_finish


// ap_entry
//    This function initializes an Application Processor.
//    It must be located at a page-aligned physical address < 0x100000.
.section .lowtext, "ax"
.p2align 12
.globl ap_entry
ap_entry:
        // This is called by the STARTUP inter-processor interrupt
        // (IPI) that enabled this processor. The processor is
        // in real mode (virtual 8086 mode), like bootentry.S.
        .code16

        // disable interrupts, zero segment registers
        cli
        xorw %ax, %ax
        movw %ax, %ds
        movw %ax, %es
        movw %ax, %ss

        // turn on page size extensions and load early_pagetable
        movl %cr4, %eax
        orl $(CR4_PSE | CR4_PAE), %eax
        movl %eax, %cr4
        movl $early_pagetable_low, %edi
        movl %edi, %cr3

        // turn on 64-bit mode
        movl $MSR_IA32_EFER, %ecx
        rdmsr
        orl $(IA32_EFER_LME | IA32_EFER_SCE | IA32_EFER_NXE), %eax
        wrmsr

        // turn on paging
        movl %cr0, %eax
        orl $(CR0_PE | CR0_WP | CR0_PG), %eax
        movl %eax, %cr0

        // load 64-bit segment descriptors and switch to 64-bit code
        // (but using low virtual addresses)
        lgdt early_gdt_low + 6

        ljmp $SEGSEL_KERN_CODE, $ap_rest_low

.globl ap_rest
ap_rest:
        .code64
        // renew segments
        xorw %ax, %ax
        movw %ax, %ds
        movw %ax, %es
        movw %ax, %fs
        movw %ax, %gs
        movw %ax, %ss

        // acquire `ap_entry_lock`
        mov $1, %al
1:      lock xchgb %al, ap_entry_lock
        testb %al, %al
        jz 2f
        pause
        jmp 1b

2:      // `ap_entry_lock` is acquired!
        // check `ap_init_allowed`
        testb $1, ap_init_allowed
        jz ap_entry_failed

        // load the current CPU number from `ncpu`
        movl ncpu, %edi
        // The kernel only supports `MAXCPU` CPUs. If the machine has
        // too many, the extras will stop themselves here
        cmpl $MAXCPU, %edi
        jge ap_entry_failed

        // increment `ncpu`
        incl ncpu
        // compute `&cpus[my_CPU_number]`
        shll $12, %edi
        addq $cpus, %rdi
        // initialize %rsp to the top if that cpustate
        leaq CPUSTACK_SIZE(%rdi), %rsp
        // call `cpus[my_CPU_number].init_ap()`.
        // This two-stage jump switches to high virtual addresses.
        movabsq $_ZN8cpustate7init_apEv, %rbx
        jmp *%rbx

ap_entry_failed:
        movb $0, ap_entry_lock
3:      hlt
        jmp 3b

// `ap_entry_lock` is a spinlock.
// It controls access to `ncpu` and `ap_init_allowed`.
.p2align 2
.globl ap_entry_lock
ap_entry_lock:
        .byte 0
// AP initialization is allowed only when `ap_init_allowed` is true.
.globl ap_init_allowed
ap_init_allowed:
        .byte 0
