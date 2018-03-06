#include "kernel.hh"
#include "k-vmiter.hh"

class memusage {
  public:
    // tracks physical addresses in the range [0, maxpa)
    static constexpr uintptr_t maxpa = 1024 * PAGESIZE;
    // shows physical addresses in the range [0, max_view_pa)
    static constexpr uintptr_t max_view_pa = 512 * PAGESIZE;
    // shows virtual addresses in the range [0, max_view_va)
    static constexpr uintptr_t max_view_va = 768 * PAGESIZE;

    memusage()
        : v_(nullptr) {
    }

    // Flag bits for memory types:
    static constexpr unsigned f_kernel = 1;     // kernel-restricted
    static constexpr unsigned f_user = 2;       // user-accessible
    // `f_process(pid)` is for memory associated with process `pid`
    static constexpr unsigned f_process(int pid) {
        if (pid >= 30) {
            return 2U << 31;
        } else if (pid >= 1) {
            return 2U << pid;
        } else {
            return 0;
        }
    }
    // Pages such as process page tables and `struct proc` are counted
    // both as kernel-only and process-associated.


    // refresh the memory map from current state
    void refresh();

    // return the symbol (character & color) associated with `pa`
    uint16_t symbol_at(uintptr_t pa) const;

  private:
    unsigned* v_;

    // add `flags` to the page containing `pa`
    // This is safe to call even if `pa >= maxpa`.
    void mark(uintptr_t pa, unsigned flags) {
        if (pa < maxpa) {
            v_[pa / PAGESIZE] |= flags;
        }
    }
};


// memusage::refresh()
//    Calculate the current physical usage map, using the current process
//    table.

void memusage::refresh() {
    if (!v_) {
        v_ = reinterpret_cast<unsigned*>(kallocpage());
        assert(v_ != nullptr);
    }

    memset(v_, 0, (maxpa / PAGESIZE) * sizeof(*v_));

    // mark kernel ranges of physical memory
    // We handle reserved ranges of physical memory separately.
    for (auto range = physical_ranges.begin();
         range != physical_ranges.end();
         ++range) {
        if (range->type() == mem_kernel) {
            for (uintptr_t pa = range->first();
                 pa != range->last();
                 pa += PAGESIZE) {
                mark(pa, f_kernel);
            }
        }
    }

    // must be called with `ptable_lock` held
    for (int pid = 1; pid < NPROC; ++pid) {
        proc* p = ptable[pid];
        if (p) {
            mark(ka2pa(p), f_kernel | f_process(pid));

            auto irqs = p->lock_pagetable_read();
            if (p->pagetable_ && p->pagetable_ != early_pagetable) {
                for (ptiter it(p); it.low(); it.next()) {
                    mark(it.ptp_pa(), f_kernel | f_process(pid));
                }
                mark(ka2pa(p->pagetable_), f_kernel | f_process(pid));

                for (vmiter it(p); it.low(); it.next()) {
                    if (it.user()) {
                        mark(it.pa(), f_user | f_process(pid));
                    }
                }
            }
            p->unlock_pagetable_read(irqs);
        }
    }
}


uint16_t memusage::symbol_at(uintptr_t pa) const {
    auto range = physical_ranges.find(pa);
    if (range == physical_ranges.end()
        || (pa >= maxpa && range->type() == mem_available)) {
        return '?' | 0xF000;
    }

    if (pa >= maxpa) {
        if (range->type() == mem_kernel) {
            return 'K' | 0x4000;
        } else {
            return '?' | 0x4000;
        }
    }

    auto v = v_[pa / PAGESIZE];
    if (range->type() == mem_console) {
        return 'C' | 0x4F00;
    } else if (range->type() == mem_reserved) {
        return 'R' | (v ? 0xC000 : 0x4000);
    } else if (range->type() == mem_kernel) {
        return 'K' | (v > f_kernel ? 0xCD00 : 0x4D00);
    } else if (range->type() == mem_nonexistent) {
        return ' ' | 0x0700;
    } else {
        if (v == 0) {
            return '.' | 0x0700;
        } else if (v == f_kernel) {
            return 'K' | 0x4000;
        } else if ((v & f_kernel) && (v & f_user)) {
            // kernel-restricted + user-accessible = error
            return 'E' | 0xF400;
        } else {
            // find lowest process involved with this page
            int pid = 1;
            while (!(v & f_process(pid))) {
                ++pid;
            }
            // foreground color is that associated with `pid`
            static const uint8_t colors[] = { 0xF, 0xC, 0xA, 0x9, 0xE };
            uint16_t ch = colors[pid % 5] << 8;
            if (v & f_kernel) {
                // kernel page: dark red background
                ch |= 0x4000;
            }
            if (v > (f_process(pid) | f_kernel | f_user)) {
                // shared page
                ch = (ch & 0x7700) | 'S';
            } else {
                // non-shared page
                static const char names[] = "K123456789ABCDEFGHIJKLMNOPQRST??";
                ch |= names[pid];
            }
            return ch;
        }
    }
}


static void console_memviewer_virtual(memusage& mu, proc* vmp) {
    console_printf(CPOS(10, 26), 0x0F00,
                   "VIRTUAL ADDRESS SPACE FOR %d\n", vmp->pid_);

    for (vmiter it(vmp);
         it.va() < memusage::max_view_va;
         it += PAGESIZE) {
        unsigned long pn = it.va() / PAGESIZE;
        if (pn % 64 == 0) {
            console_printf(CPOS(11 + pn / 64, 3), 0x0F00,
                           "0x%06X ", it.va());
        }
        uint16_t ch;
        if (!it.present()) {
            ch = ' ';
        } else {
            ch = mu.symbol_at(it.pa());
            if (it.user()) { // switch foreground & background colors
                uint16_t z = (ch & 0x0F00) ^ ((ch & 0xF000) >> 4);
                ch ^= z | (z << 4);
            }
        }
        console[CPOS(11 + pn/64, 12 + pn%64)] = ch;
    }
}


void console_memviewer(proc* vmp) {
    static memusage mu;
    mu.refresh();
    // must be called with `ptable_lock` held

    // print physical memory
    console_printf(CPOS(0, 32), 0x0F00,
                   "PHYSICAL MEMORY                  @%lu\n",
                   ticks);

    for (int pn = 0; pn * PAGESIZE < memusage::max_view_pa; ++pn) {
        if (pn % 64 == 0) {
            console_printf(CPOS(1 + pn/64, 3), 0x0F00, "0x%06X", pn << 12);
        }
        console[CPOS(1 + pn/64, 12 + pn%64)] = mu.symbol_at(pn * PAGESIZE);
    }

    // print virtual memory
    bool need_clear = true;
    if (vmp) {
        auto irqs = vmp->lock_pagetable_read();
        if (vmp->pagetable_ && vmp->pagetable_ != early_pagetable) {
            console_memviewer_virtual(mu, vmp);
            need_clear = false;
        }
        vmp->unlock_pagetable_read(irqs);
    }
    if (need_clear) {
        console_printf(CPOS(10, 0), 0x0F00, "\n\n\n\n\n\n\n\n\n\n");
    }
}
