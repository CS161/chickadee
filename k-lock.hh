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
        s.flags_ = rdeflags();
        return s;
    }
    void restore() {
        wreflags(flags_);
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
        f_.f.clear();
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
        while (f_.f.test_and_set()) {
            pause();
        }
    }
    bool trylock_noirq() {
        return !f_.f.test_and_set();
    }
    void unlock_noirq() {
        f_.f.clear();
    }

    void clear() {
        f_.f.clear();
    }

    bool is_locked() const {
        static_assert(sizeof(f_) == 1, "expect atomic_flag to occupy 1 byte");
        return f_.alias.load(std::memory_order_relaxed) != 0;
    }

private:
    union {
        std::atomic_flag f;
        std::atomic<unsigned char> alias;
    } f_;
};


struct spinlock_guard {
    explicit spinlock_guard(spinlock& lock)
        : lock_(lock), irqs_(lock_.lock()), locked_(true) {
    }
    ~spinlock_guard() {
        if (locked_) {
            lock_.unlock(irqs_);
        }
    }
    NO_COPY_OR_ASSIGN(spinlock_guard);

    void unlock() {
        assert(locked_);
        lock_.unlock(irqs_);
        locked_ = false;
    }
    void lock() {
        assert(!locked_);
        irqs_ = lock_.lock();
        locked_ = true;
    }

    constexpr bool is_locked() {
        return locked_;
    }


    spinlock& lock_;
    irqstate irqs_;
    bool locked_;
};

#endif
