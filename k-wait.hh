#ifndef CHICKADEE_K_WAIT_HH
#define CHICKADEE_K_WAIT_HH
#include "kernel.hh"
#include "k-list.hh"
struct wait_queue;

struct waiter {
    proc* p_;
    wait_queue* wq_;
    list_links links_;

    inline waiter(proc* p);
    inline ~waiter();
    inline void prepare(wait_queue* wq);
    inline void block();
    inline void clear();
    inline void wake();
};


struct wait_queue {
    list<waiter, &waiter::links_> q_;
    spinlock lock_;

    // you might want to provide some convenience methods here
};


inline waiter::waiter(proc* p)
    : p_(p), wq_(nullptr) {
}

inline waiter::~waiter() {
    // optional error-checking code
}

inline void waiter::prepare(wait_queue* wq) {
    // your code here
}

inline void waiter::block() {
    // your code here
}

inline void waiter::clear() {
    // your code here
}

inline void waiter::wake() {
    // your code here
}

#endif
