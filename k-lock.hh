#ifndef CHICKADEE_K_LOCK_HH
#define CHICKADEE_K_LOCK_HH
#include <atomic>
#include <utility>
#include "x86-64.h"
inline void adjust_this_cpu_spinlock_depth(int delta);

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
        assert(!flags_ && "forgot to unlock a spinlock");
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
    spinlock() {
        f_.clear();
    }

    irqstate lock() {
        irqstate irqs = irqstate::get();
        cli();
        lock_noirq();
        adjust_this_cpu_spinlock_depth(1);
        return irqs;
    }
    bool trylock(irqstate& irqs) {
        irqs = irqstate::get();
        cli();
        bool r = trylock_noirq();
        if (r) {
            adjust_this_cpu_spinlock_depth(1);
        } else {
            irqs.clear();
        }
        return r;
    }
    void unlock(irqstate& irqs) {
        adjust_this_cpu_spinlock_depth(-1);
        unlock_noirq();
        irqs.restore();
    }

    void lock_noirq() {
        while (f_.test_and_set()) {
            pause();
        }
    }
    bool trylock_noirq() {
        return !f_.test_and_set();
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
