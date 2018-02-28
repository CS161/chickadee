#include "k-vmiter.hh"

void vmiter::down() {
    while (level_ > 0 && (*pep_ & (PTE_P | PTE_PS)) == PTE_P) {
        perm_ &= *pep_;
        --level_;
        uintptr_t pa = *pep_ & PTE_PAMASK;
        x86_64_pagetable* pt = pa2ka<x86_64_pagetable*>(pa);
        pep_ = &pt->entry[pageindex(va_, level_)];
    }
}

void vmiter::real_find(uintptr_t va) {
    if ((va_ ^ va) & ~pageoffmask(level_ + 1)) {
        level_ = 3;
        pep_ = &pt_->entry[pageindex(va, level_)];
        perm_ = initial_perm;
    } else {
        int curidx = (reinterpret_cast<uintptr_t>(pep_) & PAGEOFFMASK) >> 3;
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

int vmiter::map(uintptr_t pa, int perm) {
    assert(!(va_ & PAGEOFFMASK));
    if (perm & PTE_P) {
        assert((pa & PTE_PAMASK) == pa);
    } else {
        assert(!(pa & PTE_P));
    }
    assert(!(perm & ~perm_ & (PTE_P | PTE_W | PTE_U)));

    while (level_ > 0 && perm) {
        assert(!(*pep_ & PTE_P));
        x86_64_pagetable* pt = reinterpret_cast<x86_64_pagetable*>
            (kallocpage());
        if (!pt) {
            return -1;
        }
        memset(pt, 0, PAGESIZE);
        *pep_ = ka2pa(pt) | PTE_P | PTE_W | PTE_U;
        down();
    }

    if (level_ == 0) {
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
    while (1) {
        if ((*pep_ & (PTE_P | PTE_PS)) == PTE_P && !skip) {
            if (level_ == stop_level) {
                break;
            } else {
                --level_;
                uintptr_t pa = *pep_ & PTE_PAMASK;
                x86_64_pagetable* pt = pa2ka<x86_64_pagetable*>(pa);
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
