#include "kernel.hh"
#include "k-apic.hh"

cpustate cpus[NCPU];
int ncpu;


// cpustate::init()
//    Initialize a `cpustate`. Should be called once per active CPU,
//    by the relevant CPU.

void cpustate::init() {
    // Note that the `cpu::cpu` constructor has already been called.

    {
        // check that this CPU is one of the expected CPUs
        uintptr_t addr = reinterpret_cast<uintptr_t>(this);
        assert((addr & PAGEOFFMASK) == 0);
        assert(this >= cpus && this < cpus + NCPU);
        assert(read_rsp() > addr && read_rsp() <= addr + CPUSTACK_SIZE);

        // ensure layout `k-exception.S` expects
        assert(reinterpret_cast<uintptr_t>(&self_) == addr);
        assert(reinterpret_cast<uintptr_t>(&current_) == addr + 8);
        assert(reinterpret_cast<uintptr_t>(&syscall_scratch_) == addr + 16);
    }

    assert(self_ == this && !current_);
    index_ = this - cpus;
    runq_lock_.clear();
    idle_task_ = nullptr;
    nschedule_ = 0;
    spinlock_depth_ = 0;

    // now initialize the CPU hardware
    init_cpu_hardware();
}


// cpustate::enable_irq(irqno)
//    Enable external interrupt `irqno`, delivering it to
//    this CPU.
void cpustate::enable_irq(int irqno) {
    assert(irqno >= IRQ_TIMER && irqno <= IRQ_SPURIOUS);
    auto& ioapic = ioapicstate::get();
    ioapic.enable_irq(irqno, INT_IRQ + irqno, lapic_id_);
}

// cpustate::disable_irq(irqno)
//    Disable external interrupt `irqno`.
void cpustate::disable_irq(int irqno) {
    assert(irqno >= IRQ_TIMER && irqno <= IRQ_SPURIOUS);
    auto& ioapic = ioapicstate::get();
    ioapic.disable_irq(irqno);
}


// cpustate::enqueue(p)
//    Enqueue `p` on this CPU's run queue. `this->runq_lock_` must
//    be locked. Does nothing if `p` is on a run queue or is currently
//    running on this CPU. Otherwise `p` must be resumable (or not
//    runnable).

void cpustate::enqueue(proc* p) {
    if (current_ != p && !p->runq_links_.is_linked()) {
        assert(p->resumable() || p->state_ != proc::runnable);
        runq_.push_back(p);
    }
}


// cpustate::schedule(yielding_from)
//    Run a process, or the current CPU's idle task if no runnable
//    process exists. If `yielding_from != nullptr`, then do not
//    run `yielding_from` unless no other runnable process exists.

void cpustate::schedule(proc* yielding_from) {
    assert(contains(read_rsp()));  // running on CPU stack
    assert(is_cli());              // interrupts are currently disabled
    assert(spinlock_depth_ == 0);  // no spinlocks are held

    // initialize idle task; don't re-run it
    if (!idle_task_) {
        init_idle_task();
    } else if (current_ == idle_task_) {
        yielding_from = idle_task_;
    }

    // increment schedule counter
    ++nschedule_;

    while (1) {
        // try to run `current`
        if (current_
            && current_->state_ == proc::runnable
            && current_ != yielding_from) {
            set_pagetable(current_->pagetable_);
            current_->resume();
        }

        // otherwise load the next process from the run queue
        runq_lock_.lock_noirq();
        if (proc* p = current_) {
            current_ = yielding_from = nullptr;
            // re-enqueue `p` at end of run queue if runnable
            if (p->state_ == proc::runnable) {
                enqueue(p);
            }
            // switch to a safe page table
            lcr3(ktext2pa(early_pagetable));
        }
        if (!runq_.empty()) {
            // pop head of run queue into `current_`
            current_ = runq_.pop_front();
        }
        runq_lock_.unlock_noirq();

        // if run queue was empty, run the idle task
        if (!current_) {
            current_ = idle_task_;
        }
    }
}


// cpustate::idle_task()
//    Every CPU has an *idle task*, which is a kernel task (i.e., a
//    `proc` that runs in kernel mode) that just stops the processor
//    until an interrupt is received. The idle task runs when a CPU
//    has nothing better to do.

void idle(proc*) {
    while (1) {
        asm volatile("hlt");
    }
}

void cpustate::init_idle_task() {
    assert(!idle_task_);
    idle_task_ = kalloc_proc();
    idle_task_->init_kernel(-1, idle);
}
