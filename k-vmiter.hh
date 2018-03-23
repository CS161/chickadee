#ifndef CHICKADEE_K_VMITER_HH
#define CHICKADEE_K_VMITER_HH
#include "kernel.hh"

// `vmiter` and `ptiter` are iterator types for x86-64 page tables.


// `vmiter` walks over virtual address mappings.
// `pa()` and `perm()` read current addresses and permissions;
// `map()` installs new mappings.

class vmiter {
  public:
    // initialize a `vmiter` for `pt` pointing at `va`
    inline vmiter(x86_64_pagetable* pt, uintptr_t va = 0);
    inline vmiter(const proc* p, uintptr_t va = 0);

    inline uintptr_t va() const;      // current virtual address
    inline uintptr_t last_va() const; // one past last va with same perm/pa
    inline bool low() const;          // is va low?
    inline uint64_t pa() const;       // current physical address
    template <typename T = void*>
    inline T ka() const;              // kernel version of pa()
    inline uint64_t perm() const;     // current permissions
    inline bool perm(uint64_t p) const; // are all permissions `p` enabled?
    inline bool present() const;      // is va present?
    inline bool writable() const;     // is va writable?
    inline bool user() const;         // is va user-accessible (unprivileged)?

    inline vmiter& find(uintptr_t va);   // change virtual address to `va`
    inline vmiter& operator+=(intptr_t delta);  // advance `va` by `delta`
    inline vmiter& operator-=(intptr_t delta);

    // move to next page-aligned va, skipping large empty regions
    // Never skips present pages.
    void next();

    // move to next page-aligned va with different perm/pa (i.e., `last_va()`)
    // Like next(), but also skips present pages.
    void step();

    // map current va to `pa` with permissions `perm`
    // Current va must be page-aligned. Calls knewpage() to allocate
    // page table pages if necessary. Returns 0 on success,
    // negative on failure.
    int map(uintptr_t pa, int perm = PTE_P | PTE_W | PTE_U)
        __attribute__((warn_unused_result));
    // free mapped page and clear mapping. Like `kfree(ka()); map(0, 0)`
    inline void kfree_page();

  private:
    x86_64_pagetable* pt_;
    x86_64_pageentry_t* pep_;
    int level_;
    int perm_;
    uintptr_t va_;

    static constexpr int initial_perm = 0xFFF;

    void down();
    void real_find(uintptr_t va);
};


// `ptiter` walks over the page table pages in a page table,
// returning them in depth-first order.
// This is mainly useful when freeing a page table, as in:
// ```
// for (ptiter it(pt); it.low(); it.next()) {
//     freepage(it.ptp());
// }
// freepage(pt);
// ```
// Note that `ptiter` will never visit the level 4 page table page.

class ptiter {
  public:
    // initialize a `ptiter` for `pt` pointing at `va`
    inline ptiter(x86_64_pagetable* pt, uintptr_t va = 0);
    inline ptiter(const proc* p, uintptr_t va = 0);

    inline uintptr_t va() const;            // current virtual address
    inline uintptr_t last_va() const;       // one past last va covered by ptp
    inline bool active() const;             // does va exist?
    inline bool low() const;                // is va low?
    inline int level() const;               // current level (0-2)
    inline x86_64_pagetable* ptp() const;   // current page table page
    inline uintptr_t ptp_pa() const;        // physical address of ptp
    inline void kfree_ptp();                // `kfree(ptp())` + clear mapping

    // move to next page table page in depth-first order
    inline void next();

  private:
    x86_64_pagetable* pt_;
    x86_64_pageentry_t* pep_;
    int level_;
    uintptr_t va_;

    void go(uintptr_t va);
    void down(bool skip);
};


inline vmiter::vmiter(x86_64_pagetable* pt, uintptr_t va)
    : pt_(pt), pep_(&pt_->entry[0]), level_(3), perm_(initial_perm), va_(0) {
    real_find(va);
}
inline vmiter::vmiter(const proc* p, uintptr_t va)
    : vmiter(p->pagetable_, va) {
}
inline uintptr_t vmiter::va() const {
    return va_;
}
inline uintptr_t vmiter::last_va() const {
    return (va_ | pageoffmask(level_)) + 1;
}
inline bool vmiter::low() const {
    return va_ <= VA_LOWMAX;
}
inline uint64_t vmiter::pa() const {
    if (*pep_ & PTE_P) {
        uintptr_t pa = *pep_ & PTE_PAMASK;
        if (level_ > 0) {
            pa &= ~0x1000UL;
        }
        return pa + (va_ & pageoffmask(level_));
    } else {
        return -1;
    }
}
template <typename T>
inline T vmiter::ka() const {
    assert(*pep_ & PTE_P);
    return pa2ka<T>(pa());
}
inline uint64_t vmiter::perm() const {
    if (*pep_ & PTE_P) {
        return *pep_ & perm_;
    } else {
        return 0;
    }
}
inline bool vmiter::perm(uint64_t p) const {
    return (*pep_ & perm_ & p) == p;
}
inline bool vmiter::present() const {
    return (*pep_ & PTE_P) != 0;
}
inline bool vmiter::writable() const {
    return perm(PTE_P | PTE_W);
}
inline bool vmiter::user() const {
    return perm(PTE_P | PTE_U);
}
inline vmiter& vmiter::find(uintptr_t va) {
    real_find(va);
    return *this;
}
inline vmiter& vmiter::operator+=(intptr_t delta) {
    return find(va_ + delta);
}
inline vmiter& vmiter::operator-=(intptr_t delta) {
    return find(va_ - delta);
}
inline void vmiter::step() {
    real_find(last_va());
}
inline void vmiter::kfree_page() {
    assert((va_ & (PAGESIZE - 1)) == 0);
    if (*pep_ & PTE_P) {
        kfree(ka<void*>());
    }
    *pep_ = 0;
}

inline ptiter::ptiter(x86_64_pagetable* pt, uintptr_t va)
    : pt_(pt) {
    go(va);
}
inline ptiter::ptiter(const proc* p, uintptr_t va)
    : ptiter(p->pagetable_, va) {
}
inline uintptr_t ptiter::va() const {
    return va_ & ~pageoffmask(level_);
}
inline uintptr_t ptiter::last_va() const {
    return (va_ | pageoffmask(level_)) + 1;
}
inline bool ptiter::active() const {
    return va_ <= VA_NONCANONMAX;
}
inline bool ptiter::low() const {
    return va_ <= VA_LOWMAX;
}
inline int ptiter::level() const {
    return level_ - 1;
}
inline void ptiter::next() {
    down(true);
}
inline uintptr_t ptiter::ptp_pa() const {
    return *pep_ & PTE_PAMASK;
}
inline x86_64_pagetable* ptiter::ptp() const {
    return pa2ka<x86_64_pagetable*>(ptp_pa());
}
inline void ptiter::kfree_ptp() {
    kfree(ptp());
    *pep_ = 0;
}

#endif
