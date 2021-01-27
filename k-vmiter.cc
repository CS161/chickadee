#include "k-vmiter.hh"

const x86_64_pageentry_t vmiter::zero_pe = 0;

uint64_t vmiter::range_perm(size_t sz) const {
    uint64_t p = perm();
    size_t rsz = pageoffmask(level_) + 1;
    if ((p & PTE_P) != 0 && sz > rsz) {
        if (sz > ((int64_t) va() < 0 ? 0 : VA_LOWEND) - va()) {
            return 0;
        }
        vmiter it(*this);
        sz += va() & (rsz - 1);
        do {
            sz -= rsz;
            it.next_range();
            p &= it.perm();
            rsz = pageoffmask(it.level_) + 1;
        } while ((p & PTE_P) != 0 && sz > rsz);
    }
    if ((p & PTE_P) != 0) {
        return p;
    } else {
        return 0;
    }
}

void vmiter::down() {
    while (level_ > 0 && (*pep_ & (PTE_P | PTE_PS)) == PTE_P) {
        perm_ &= *pep_ | ~(PTE_P | PTE_W | PTE_U);
        --level_;
        uintptr_t pa = *pep_ & PTE_PAMASK;
        x86_64_pagetable* pt = pa2kptr<x86_64_pagetable*>(pa);
        pep_ = &pt->entry[pageindex(va_, level_)];
    }
    if ((*pep_ & PTE_PAMASK) >= 0x100000000UL) {
        panic("Page table %p may contain uninitialized memory!\n"
              "(Page table contents: %p)\n", pt_, *pep_);
    }
}

void vmiter::real_find(uintptr_t va) {
    if (level_ == 3 || ((va_ ^ va) & ~pageoffmask(level_ + 1)) != 0) {
        level_ = 3;
        if (va_is_canonical(va)) {
            perm_ = initial_perm;
            pep_ = &pt_->entry[pageindex(va, level_)];
        } else {
            perm_ = 0;
            pep_ = const_cast<x86_64_pageentry_t*>(&zero_pe);
        }
    } else {
        int curidx = (reinterpret_cast<uintptr_t>(pep_) % PAGESIZE) >> 3;
        pep_ += pageindex(va, level_) - curidx;
    }
    va_ = va;
    down();
}

void vmiter::next() {
    int level = 0;
    if (level_ > 0 && !perm()) {
        level = level_;
    }
    real_find((va_ | pageoffmask(level)) + 1);
}

int vmiter::try_map(uintptr_t pa, int perm) {
    if (pa == (uintptr_t) -1 && perm == 0) {
        pa = 0;
    }
    // virtual address is page-aligned
    assert((va_ % PAGESIZE) == 0, "vmiter::try_map va not aligned");
    if (perm & PTE_P) {
        // if mapping present, physical address is page-aligned
        assert(pa != (uintptr_t) -1, "vmiter::try_map mapping nonexistent pa");
        assert((pa & PTE_PAMASK) == pa, "vmiter::try_map pa not aligned");
    } else {
        assert((pa & PTE_P) == 0, "vmiter::try_map invalid pa");
    }
    // new permissions (`perm`) cannot be less restrictive than permissions
    // imposed by higher-level page tables (`perm_`)
    assert(!(perm & ~perm_ & (PTE_P | PTE_W | PTE_U)));

    while (level_ > 0 && perm) {
        assert(!(*pep_ & PTE_P));
        x86_64_pagetable* pt = knew<x86_64_pagetable>();
        if (!pt) {
            return -1;
        }
        memset(pt, 0, PAGESIZE);
        std::atomic_thread_fence(std::memory_order_release);
        *pep_ = ka2pa(pt) | PTE_P | PTE_W | PTE_U;
        down();
    }

    if (level_ == 0) {
        std::atomic_thread_fence(std::memory_order_release);
        *pep_ = pa | perm;
    }
    return 0;
}


void ptiter::go(uintptr_t va) {
    level_ = 3;
    pep_ = &pt_->entry[pageindex(va, level_)];
    va_ = va;
    down(false);
}

void ptiter::down(bool skip) {
    int stop_level = 1;
    while (true) {
        if ((*pep_ & (PTE_P | PTE_PS)) == PTE_P && !skip) {
            if (level_ == stop_level) {
                break;
            } else {
                --level_;
                uintptr_t pa = *pep_ & PTE_PAMASK;
                x86_64_pagetable* pt = pa2kptr<x86_64_pagetable*>(pa);
                pep_ = &pt->entry[pageindex(va_, level_)];
            }
        } else {
            uintptr_t va = (va_ | pageoffmask(level_)) + 1;
            if ((va ^ va_) & ~pageoffmask(level_ + 1)) {
                // up one level
                if (level_ == 3) {
                    va_ = VA_NONCANONMAX + 1;
                    return;
                }
                stop_level = level_ + 1;
                level_ = 3;
                pep_ = &pt_->entry[pageindex(va_, level_)];
            } else {
                ++pep_;
                va_ = va;
            }
            skip = false;
        }
    }
}
