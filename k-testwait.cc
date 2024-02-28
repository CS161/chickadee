#include "kernel.hh"
#include "k-wait.hh"

#define WQTEST_NPROC 5
#define WQTEST1_NOP 400
#define WQTEST2_NOP 20000
#define WQTEST3_NOP 300
#define WQTEST4_NOP 100
#define WQTEST5_NOP 1000

static std::atomic<int> phase;
static std::atomic<unsigned> n;
static wait_queue wq, wq2;
static spinlock wqdata_lock;
static unsigned wqdata_i;
static bool wqdata_flag;

static proc* wqt_proc[WQTEST_NPROC];
static std::atomic<unsigned>* wqt_idn[WQTEST_NPROC];
static unsigned long start_ticks;

namespace {
struct wq_reporter {
    const char* phase_;
    unsigned expected_;
    unsigned step_;
    unsigned last_n_ = 0;
    unsigned last_ticks_;
    unsigned report_trigger_;

    wq_reporter(const char* name, unsigned expected, unsigned step)
        : phase_(name), expected_(expected), step_(step),
          last_ticks_(ticks), report_trigger_(step) {
    }
    void check(unsigned n) {
        if (n != last_n_) {
            last_n_ = n;
            last_ticks_ = ticks;
        } else if (long(ticks - last_ticks_) > HZ) {
            panic("ktestwait appears to have failed (%s, %u/%u rounds)!\n",
                  phase_, last_n_, expected_);
        }
        if (last_n_ >= report_trigger_) {
            console_printf("done round %u/%u\n", last_n_, expected_);
            report_trigger_ = last_n_ + step_;
        }
    }
};
}

static void wq_tester() {
    cli();

    // determine own ID
    proc* p = current();
    int id = 0;
    while (p != wqt_proc[id]) {
        ++id;
    }
    assert(id < WQTEST_NPROC);

    // initialize counters
    n = 0;
    std::atomic<unsigned> idn;
    wqt_idn[id] = &idn;

    // Initialize random seed with some physical memory whose contents
    // seems to vary run to run.
    unsigned long* seedptr = pa2kptr<unsigned long*>(0xff8);
    rand_engine re(*seedptr + id * 192381 + ticks * 10123);

    ++phase;
    while (phase < 1 + WQTEST_NPROC) {
        p->yield();
    }

    // First test phase: run many wakeups
    if (id == 0) {
        start_ticks = ticks;
        console_printf("ktestwait phase 1 commencing\n");
        unsigned want = WQTEST1_NOP * (WQTEST_NPROC - 1);
        wq_reporter wqr("phase 1", want, 500);
        while (n < want) {
            unsigned r = re();
            if (r < re.max() / 4) {
                wq.wake_all();
            } else if (r < re.max() / 2) {
                p->yield();
            } else {
                pause();
            }
            if (r < re.max() / 512) {
                wqr.check(n);
            }
        }
        n = 0;
        phase = 100;
    } else {
        for (int i = 0; i < WQTEST1_NOP; ++i) {
            waiter w;
            w.prepare(wq);
            pause();
            w.maybe_block();
            w.clear();
            ++n;
            ++idn;
        }
    }

    while (phase < 100) {
        p->yield();
    }

    // Second test phase: run many non-blocking wakeups (test synchronization
    // of wait queue)
    if (id == 0) {
        start_ticks = ticks;
        console_printf("ktestwait phase 2 commencing\n");
    }
    for (int i = 0; i < WQTEST2_NOP; ++i) {
        unsigned r = re();
        if (r < re.max() / 512) {
            p->yield();
        } else if (r < re.max() / 16) {
            wq.wake_all();
            pause();
        } else {
            waiter w;
            w.prepare(wq);
            pause();
            ++idn;
            w.clear();
            pause();
        }
    }

    ++n;
    if (id == 0) {
        while (n < WQTEST_NPROC) {
            p->yield();
        }
        n = 0;
        phase = 200;
    }

    while (phase < 200) {
        p->yield();
    }

    // Third test phase: wake up waiters directly (not via wake_all)
    if (id == 0) {
        start_ticks = ticks;
        console_printf("ktestwait phase 3 commencing\n");
        unsigned want = WQTEST3_NOP * (WQTEST_NPROC - 1);
        wq_reporter wqr("phase 3", want, 250);
        unsigned i = 1;
        while (n < want) {
            wqt_proc[i]->unblock();
            if (++i == WQTEST_NPROC) {
                i = 1;
                p->yield();
            } else {
                pause();
            }
            if (re() < re.max() / 512) {
                wqr.check(n);
            }
        }
        n = 0;
        phase = 300;
    } else {
        for (int i = 0; i < WQTEST3_NOP; ++i) {
            int tried = 0;
            waiter w;
            // block exactly once
            w.block_until(wq, [&] () {
                pause();
                return tried++ != 0;
            });
            ++n;
        }
    }

    while (phase < 300) {
        p->yield();
    }

    // Fourth test phase: check for lost wakeups
    if (id == 0) {
        start_ticks = ticks;
        console_printf("ktestwait phase 4 commencing\n");
        wq_reporter wqr("phase 3", WQTEST4_NOP, 1);
        for (unsigned i = 0; i != WQTEST4_NOP; ++i) {
            assert(wqdata_flag == false);

            spinlock_guard guard(wqdata_lock);
            waiter w;
            w.block_until(wq2, [] () {
                return wqdata_i == WQTEST_NPROC - 1;
            }, guard);

            wqdata_flag = true;
            wq.wake_all();

            w.block_until(wq2, [] () {
                return wqdata_i == 0;
            }, guard);

            wqdata_flag = false;
            wq.wake_all();

            if (i % 32 == 31) {
                wqr.check(i);
            }
        }
        n = 0;
        phase = 400;
    } else {
        for (unsigned i = 0; i != WQTEST4_NOP; ++i) {
            spinlock_guard guard(wqdata_lock);
            if (++wqdata_i == WQTEST_NPROC - 1) {
                wq2.wake_all();
            }

            waiter w;
            w.block_until(wq, [] () {
                return wqdata_flag == true;
            }, guard);

            --wqdata_i;
            wq2.wake_all();

            if (re() < re.max() / 128) {
                guard.unlock();
                p->yield();
                guard.lock();
            } else {
                pause();
            }

            w.block_until(wq, [] () {
                return wqdata_flag == false;
            }, guard);
        }
    }

    while (phase < 400) {
        p->yield();
    }

    // Fifth test phase: attempt to trigger a sleep/wakeup race
    if (id == 0) {
        start_ticks = ticks;
        console_printf("ktestwait phase 5 commencing\n");
        wq_reporter wqr("phase 5", WQTEST5_NOP, 200);
        unsigned nn = 0;
        for (unsigned i = 0; i != WQTEST5_NOP; ++i) {
            while (n % 2 == 0) {
                pause();
                ++nn;
                if (nn % 1024 == 0) {
                    wqr.check(i);
                } else if (nn % 512 == 0) {
                    p->yield();
                }
            }
            spinlock_guard guard(wqdata_lock);
            wq.wake_all();
            ++n;
        }
    } else if (id == 1) {
        for (unsigned i = 0; i != WQTEST5_NOP; ++i) {
            waiter w;
            ++n;
            spinlock_guard guard(wqdata_lock);
            w.block_until(wq, [] () {
                return n % 2 == 0;
            }, guard);
        }
    }

    // That completes the test
    if (id == 0) {
        phase = 1000;
        console_printf(COLOR_SUCCESS, "ktestwait succeeded!\n");
    }

    // block forever as if faulted (but do not free memory)
    p->pstate_ = proc::ps_faulted;
    p->yield();
}

int ktest_wait_queues() {
    int start_phase = 0;
    if (phase.compare_exchange_strong(start_phase, 1)) {
        start_ticks = ticks;
        for (int i = 0; i < WQTEST_NPROC; ++i) {
            wqt_proc[i] = knew<proc>();
            wqt_proc[i]->init_kernel(-1, wq_tester);
            if (i == 0 || i == ncpu - 1 || ncpu == 1) {
                cpus[0].enqueue(wqt_proc[i]);
            } else {
                cpus[1 + (i - 1) % (ncpu - 1)].enqueue(wqt_proc[i]);
            }
        }
    }
    if (phase == 1000 || long(ticks - start_ticks) <= HZ * 45) {
        return phase;
    } else {
        return -1;
    }
}
