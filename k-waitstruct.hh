#ifndef CHICKADEE_K_WAITSTRUCT_HH
#define CHICKADEE_K_WAITSTRUCT_HH
#include "k-list.hh"
struct proc;
struct wait_queue;
struct spinlock;
struct irqstate;
struct spinlock_guard;

// k-waitstruct.hh
//    Includes the struct definitions for `waiter` and `wait_queue`.
//    The inline functions declared here are defined in `k-wait.hh`.


struct waiter {
    proc* p_ = nullptr;
    wait_queue* wq_;
    list_links links_;

    explicit inline waiter();
    inline ~waiter();
    NO_COPY_OR_ASSIGN(waiter);
    inline void prepare(wait_queue& wq);
    inline void block();
    inline void clear();
    inline void wake();

    template <typename F>
    inline void block_until(wait_queue& wq, F predicate);
    template <typename F>
    inline void block_until(wait_queue& wq, F predicate,
                            spinlock& lock, irqstate& irqs);
    template <typename F>
    inline void block_until(wait_queue& wq, F predicate,
                            spinlock_guard& guard);
};


struct wait_queue {
    list<waiter, &waiter::links_> q_;
    mutable spinlock lock_;

    // you might want to provide some convenience methods here
    inline void wake_all();
};

#endif
