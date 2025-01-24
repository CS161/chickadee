#ifndef CHICKADEE_K_WAIT_HH
#define CHICKADEE_K_WAIT_HH
#include "k-list.hh"
struct proc;
struct wait_queue;
struct spinlock;
struct irqstate;
struct spinlock_guard;

// k-wait.hh
//    Chickadee wait queues.
//
//    Unlike many header files, `k-wait.hh` is #included twice. When it is
//    #included by `kernel.hh`, it declares `struct waiter` and `struct
//    wait_queue`, which `struct proc` requires, but does not define inline
//    functions like `waiter::wait_until` (it cannot define these yet because
//    they depend on methods in `struct proc`). When #included a second time
//    (for instance, by `kernel.cc`), it defines those inline functions.


struct waiter {
    proc* p_;
    wait_queue* wq_;
    list_links links_;

    explicit inline waiter();
    inline ~waiter();
    NO_COPY_OR_ASSIGN(waiter);
    inline void prepare(wait_queue& wq);
    inline void maybe_block();
    inline void clear();
    inline void notify();

    template <typename F>
    inline void wait_until(wait_queue& wq, F predicate);
    template <typename F>
    inline void wait_until(wait_queue& wq, F predicate,
                           spinlock& lock, irqstate& irqs);
    template <typename F>
    inline void wait_until(wait_queue& wq, F predicate,
                           spinlock_guard& guard);

    inline void wait_once(wait_queue& wq);
    inline void wait_once(wait_queue& wq,
                          spinlock& lock, irqstate& irqs);
    inline void wait_once(wait_queue& wq,
                          spinlock_guard& guard);
};


struct wait_queue {
    list<waiter, &waiter::links_> q_;
    mutable spinlock lock_;

    // you might want to provide some convenience methods here
    inline void notify_all();
};


// End of structure definitions (first inclusion)
#endif /* CHICKADEE_K_WAIT_HH */
#if !CHICKADEE_WAIT_FUNCTIONS && CHICKADEE_PROC_DECLARATION
#define CHICKADEE_WAIT_FUNCTIONS 1
// Beginning of inline functions (second inclusion)


inline waiter::waiter()
    : p_(current()) {
}

inline waiter::~waiter() {
    assert(!links_.is_linked());
    // Feel free to add more sanity checks if you’d like!
}

inline void waiter::prepare(wait_queue& wq) {
    assert(p_ == current());
    assert(!links_.is_linked());
    wq_ = &wq;
    // your code here
}

inline void waiter::maybe_block() {
    assert(p_ == current() && wq_ != nullptr);
    // Thanks to concurrent wakeups, `p_->pstate_` might or might not equal
    // `proc::ps_blocked`, and `links_` might or might not be linked.
    // When the function returns, `p_->pstate_` MUST NOT equal
    // `proc::ps_blocked`, and `links_` MUST NOT be linked.
    // your code here
}

inline void waiter::clear() {
    assert(p_ == current());
    // your code here
}

inline void waiter::notify() {
    assert(!links_.is_linked());
    p_->unblock();
}


// waiter::wait_until(wq, predicate)
//    Block on `wq` until `predicate()` returns true.
template <typename F>
inline void waiter::wait_until(wait_queue& wq, F predicate) {
    while (true) {
        prepare(wq);
        if (predicate()) {
            break;
        }
        maybe_block();
    }
    clear();
}

// waiter::wait_until(wq, predicate, lock, irqs)
//    Block on `wq` until `predicate()` returns true. The `lock`
//    must be locked; it is unlocked before blocking (if blocking
//    is necessary). All calls to `predicate` have `lock` locked,
//    and `lock` is locked on return.
template <typename F>
inline void waiter::wait_until(wait_queue& wq, F predicate,
                               spinlock& lock, irqstate& irqs) {
    while (true) {
        prepare(wq);
        if (predicate()) {
            break;
        }
        lock.unlock(irqs);
        maybe_block();
        irqs = lock.lock();
    }
    clear();
}

// waiter::wait_until(wq, predicate, guard)
//    Block on `wq` until `predicate()` returns true. The `guard`
//    must be locked on entry; it is unlocked before blocking (if
//    blocking is necessary) and locked on return.
template <typename F>
inline void waiter::wait_until(wait_queue& wq, F predicate,
                               spinlock_guard& guard) {
    wait_until(wq, predicate, guard.lock_, guard.irqs_);
}


// waiter::wait_once(wq)
//    Block on `wq` at most once.
inline void waiter::wait_once(wait_queue& wq) {
    prepare(wq);
    maybe_block();
    clear();
}

// waiter::wait_once(wq, lock, irqs)
//    Block on `wq` at most once. The `lock` must be locked; it is
//    unlocked before blocking (if blocking is necessary), but locked
//    on return.
inline void waiter::wait_once(wait_queue& wq,
                              spinlock& lock, irqstate& irqs) {
    prepare(wq);
    lock.unlock(irqs);
    maybe_block();
    irqs = lock.lock();
    clear();
}

// waiter::wait_once(wq, guard)
//    Block on `wq` at most once. The `guard` must be locked on entry;
//    it is unlocked before blocking (if blocking is necessary) and
//    locked on return.
inline void waiter::wait_once(wait_queue& wq,
                              spinlock_guard& guard) {
    wait_once(wq, guard.lock_, guard.irqs_);
}


// wait_queue::notify_all()
//    Lock the wait queue, then clear it by waking all waiters.
inline void wait_queue::notify_all() {
    spinlock_guard guard(lock_);
    while (auto w = q_.pop_front()) {
        w->notify();
    }
}

#endif /* CHICKADEE_WAIT_FUNCTIONS */
