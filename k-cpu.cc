#include "kernel.hh"
#include "k-apic.hh"

cpustate cpus[MAXCPU];
int ncpu;


// cpustate::init()
//    Initialize a `cpustate`. Should be called once per active CPU,
//    by the relevant CPU.

void cpustate::init() {
    assert(this_cpu() == this);
    assert(current() == nullptr);
    // Note that the `cpu::cpu` constructor has already been called.

    {
        // check that this CPU is one of the expected CPUs
        uintptr_t addr = reinterpret_cast<uintptr_t>(this);
        assert((addr & PAGEOFFMASK) == 0);
        assert(this >= cpus && this < cpus + MAXCPU);
        assert(rdrsp() > addr && rdrsp() <= addr + CPUSTACK_SIZE);

        // ensure layout `k-exception.S` expects
        assert(reinterpret_cast<uintptr_t>(&self_) == addr);
        assert(reinterpret_cast<uintptr_t>(&current_) == addr + 8);
        assert(reinterpret_cast<uintptr_t>(&syscall_scratch_) == addr + 16);
    }

    cpuindex_ = this - cpus;
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


// cpustate::schedule()
//    Run a process, or the current CPU's idle task if no runnable
//    process exists. Prefers to run a process that is not the most
//    recent process.

void cpustate::schedule() {
    assert(contains(rdrsp()));     // running on CPU stack
    assert(is_cli());              // interrupts are currently disabled
    assert(spinlock_depth_ == 0);  // no spinlocks are held

    // initialize idle task
    if (!idle_task_) {
        init_idle_task();
    }

    // increment schedule counter
    ++nschedule_;

    // find a runnable process (preferring one different from `current_`)
    bool first_try = true;
    while (!current_
           || current_->pstate_ != proc::ps_runnable
           || first_try) {
        first_try = false;

        proc* prev = current_;

        runq_lock_.lock_noirq();

        // reschedule old current if necessary
        if (prev && prev->pstate_ == proc::ps_runnable) {
            assert(prev->resumable());
            if (!prev->runq_links_.is_linked()) {
                runq_.push_back(prev);
            }
        }

        // run idle task as last resort
        current_ = runq_.empty() ? idle_task_ : runq_.pop_front();

        runq_lock_.unlock_noirq();
    }

    // run `current_`
    set_pagetable(current_->pagetable_);
    current_->resume(); // does not return
}


// cpustate::enqueue(p)
//    Claim new task `p` for this CPU and enqueue it on this CPU's run queue.
//    Acquires `runq_lock_`. `p` must belong to any CPU, and must be resumable
//    (or not runnable).

void cpustate::enqueue(proc* p) {
    assert(p->runq_cpu_ == -1);
    assert(!p->runq_links_.is_linked());
    assert(p->resumable() || p->pstate_ != proc::ps_runnable);
    spinlock_guard guard(runq_lock_);
    p->runq_cpu_ = cpuindex_;
    runq_.push_back(p);
}


// cpustate::reenqueue(p)
//    Enqueue `p` on this CPU's run queue. Acquires `runq_lock_`. `p` must
//    belong to this CPU (its `runq_cpu_` must equal `cpuindex_`). Does
//    nothing if `p` is currently running or is already scheduled on this
//    CPU's run queue; otherwise `p` must be resumable (or not runnable).

void cpustate::reenqueue(proc* p) {
    assert(p->runq_cpu_ == cpuindex_);
    spinlock_guard guard(runq_lock_);
    if (current_ != p && !p->runq_links_.is_linked()) {
        assert(p->resumable() || p->pstate_ != proc::ps_runnable);
        runq_.push_back(p);
    }
}


// cpustate::idle_task
//    Every CPU has an *idle task*, which is a kernel task (i.e., a
//    `proc` that runs in kernel mode) that just stops the processor
//    until an interrupt is received. The idle task runs when a CPU
//    has nothing better to do.

void idle() {
    sti();
    while (true) {
        asm volatile("hlt");
    }
}

void cpustate::init_idle_task() {
    assert(!idle_task_);
    idle_task_ = knew<proc>();
    idle_task_->init_kernel(idle);
    idle_task_->runq_cpu_ = cpuindex_;
    // Note that the idle task is not actually on the run queue.
}
