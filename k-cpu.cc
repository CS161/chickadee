#include "kernel.hh"

cpustate cpus[NCPU];
int ncpu;


// cpustate::init()
//    Initialize a `cpustate`. Should be called once per active CPU,
//    by the relevant CPU.

void cpustate::init() {
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

    self_ = this;
    current_ = nullptr;
    index_ = this - cpus;
    runq_head_ = nullptr;
    runq_tail_ = nullptr;
    runq_lock_.clear();
    idle_task_ = nullptr;
    spinlock_depth_ = 0;

    // now initialize the CPU hardware
    init_cpu_hardware();
}


// cpustate::enqueue(p)
//    Enqueue `p` on this CPU's run queue. `p` must not be on any
//    run queue, it must be resumable (or not runnable), and
//    `this->runq_lock_` must be held.

void cpustate::enqueue(proc* p) {
    assert(p->resumable() || p->state_ != proc::runnable);
    assert(!p->runq_pprev_);
    p->runq_pprev_ = runq_head_ ? &runq_tail_->runq_next_ : &runq_head_;
    p->runq_next_ = nullptr;
    *p->runq_pprev_ = runq_tail_ = p;
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
        if (current_) {
            // re-enqueue `current_` at end of run queue if runnable
            if (current_->state_ == proc::runnable) {
                enqueue(current_);
            }
            current_ = yielding_from = nullptr;
            // switch to a safe page table
            lcr3(ktext2pa(early_pagetable));
        }
        if (runq_head_) {
            // pop head of run queue into `current_`
            current_ = runq_head_;
            runq_head_ = runq_head_->runq_next_;
            if (runq_head_) {
                runq_head_->runq_pprev_ = &runq_head_;
            } else {
                runq_tail_ = nullptr;
            }
            current_->runq_next_ = nullptr;
            current_->runq_pprev_ = nullptr;
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
    idle_task_ = reinterpret_cast<proc*>(kallocpage());
    idle_task_->init_kernel(-1, idle);
}
