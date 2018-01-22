#include "kernel.hh"
#include "k-vmiter.hh"

class memusage {
  public:
    static constexpr uintptr_t maxpa = 1024 * PAGESIZE;

    memusage()
        : v_(nullptr) {
    }

    static constexpr uintptr_t limit() {
        return maxpa;
    }

    void refresh();
    uint16_t symbol_at(uintptr_t pa) const;

    static constexpr unsigned f_kernel = 1;
    static constexpr unsigned f_process(int pid) {
        return 1U << (pid < 30 ? pid : 31);
    }

  private:
    unsigned* v_;

    void mark(uintptr_t pa, unsigned flags) {
        if (pa < maxpa) {
            v_[pa / PAGESIZE] |= flags;
        }
    }
};

void memusage::refresh() {
    if (!v_) {
        v_ = reinterpret_cast<unsigned*>(kallocpage());
        assert(v_ != nullptr);
    }

    memset(v_, 0, (maxpa / PAGESIZE) * sizeof(*v_));

    // must be called with `ptable_lock` held
    for (int pid = 1; pid < NPROC; ++pid) {
        proc* p = ptable[pid];
        if (!p) {
            continue;
        }
        mark(ka2pa(p), f_kernel | f_process(pid));
        if (!p->pagetable_) {
            continue;
        }
        for (ptiter it(p); it.low(); it.next()) {
            mark(it.ptp_pa(), f_kernel | f_process(pid));
        }
        mark(ka2pa(p->pagetable_), f_kernel | f_process(pid));
        for (vmiter it(p); it.low(); it.next()) {
            if (it.user()) {
                mark(it.pa(), f_process(pid));
            }
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
            return 'R' | 0x4000;
        }
    }

    if (range->type() == mem_console) {
        return 'C' | 0x4F00;
    } else if (range->type() == mem_reserved) {
        return 'R' | (v_[pa / PAGESIZE] ? 0xC000 : 0x4000);
    } else if (range->type() == mem_kernel) {
        return 'K' | (v_[pa / PAGESIZE] ? 0xCD00 : 0x4D00);
    } else if (range->type() == mem_nonexistent) {
        return ' ' | 0x0700;
    } else {
        if (v_[pa / PAGESIZE] == 0) {
            return '.' | 0x0700;
        } else if (v_[pa / PAGESIZE] == f_kernel) {
            return 'K' | 0x4000;
        } else {
            int pid = 1;
            while (!(v_[pa / PAGESIZE] & f_process(pid))) {
                ++pid;
            }
            static const char names[] = "K123456789ABCDEFGHIJKLMNOPQRST?";
            static const uint8_t colors[] = { 0xF, 0xC, 0xA, 0x9, 0xE };
            uint16_t ch = colors[pid % 5] << 8;
            if (v_[pa / PAGESIZE] & f_kernel) {
                ch |= 0x4000;
            }
            if (v_[pa / PAGESIZE] > (f_process(pid) | f_kernel)) {
                ch = (ch & 0x7700) | 'S';
            } else {
                ch |= names[pid];
            }
            return ch;
        }
    }
}

void console_memviewer(const proc* vmp) {
    static memusage mu;
    mu.refresh();

    console_printf(CPOS(0, 32), 0x0F00,
                   "PHYSICAL MEMORY                  @%d\n",
                   ticks);

    for (int pn = 0; pn * PAGESIZE < MEMSIZE_PHYSICAL; ++pn) {
        if (pn % 64 == 0) {
            console_printf(CPOS(1 + pn/64, 3), 0x0F00, "0x%06X", pn << 12);
        }
        console[CPOS(1 + pn/64, 12 + pn%64)] = mu.symbol_at(pn * PAGESIZE);
    }

    if (vmp) {
        console_printf(CPOS(10, 26), 0x0F00,
                       "VIRTUAL ADDRESS SPACE FOR %d\n", vmp->pid_);

        for (vmiter it(vmp->pagetable_, 0);
             it.va() < MEMSIZE_VIRTUAL;
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
    } else {
        console_printf(CPOS(10, 0), 0x0F00, "\n\n\n\n\n\n\n\n\n\n");
    }
}
