#ifndef CHICKADEE_K_LOCK_HH
#define CHICKADEE_K_LOCK_HH
#include <atomic>
#include <utility>
#include "x86-64.h"

struct irqstate {
    irqstate()
        : flags_(0) {
    }
    irqstate(irqstate&& x)
        : flags_(x.flags_) {
        x.flags_ = 0;
    }
    irqstate(const irqstate&) = delete;
    irqstate& operator=(const irqstate&) = delete;
    ~irqstate() {
        assert(!flags_);
    }

    static irqstate get() {
        irqstate s;
        s.flags_ = read_eflags();
        return s;
    }
    void restore() {
        write_eflags(flags_);
        flags_ = 0;
    }
    void clear() {
        flags_ = 0;
    }

    void operator=(irqstate&& s) {
        assert(flags_ == 0);
        flags_ = s.flags_;
        s.flags_ = 0;
    }

    uint64_t flags_;
};

struct spinlock {
    irqstate lock() {
        irqstate s = irqstate::get();
        cli();
        lock_noirq();
        return s;
    }
    void unlock(irqstate& x) {
        unlock_noirq();
        x.restore();
    }

    void lock_noirq() {
        while (f_.test_and_set()) {
            pause();
        }
    }
    void unlock_noirq() {
        f_.clear();
    }

    void clear() {
        f_.clear();
    }

private:
    std::atomic_flag f_;
};

#endif
