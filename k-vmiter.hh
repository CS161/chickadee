#ifndef CHICKADEE_K_VMITER_HH
#define CHICKADEE_K_VMITER_HH
#include "kernel.hh"
class ptiter;

// `vmiter` and `ptiter` are iterator types for x86-64 page tables.


// `vmiter` walks over virtual address mappings.
// `pa()` and `perm()` read current addresses and permissions;
// `map()` installs new mappings.

class vmiter {
  public:
    // Initialize a `vmiter` for `pt`, with initial virtual address `va`
    inline vmiter(x86_64_pagetable* pt, uintptr_t va);
    inline vmiter(const proc* p, uintptr_t va);

    // ADDRESS QUERIES
    // Return current virtual address
    inline uintptr_t va() const;
    // Return one past last virtual address in this mapping range
    inline uintptr_t last_va() const;
    // Return true iff `va() <= VA_LOWMAX` (is a low canonical address)
    inline bool low() const;
    // Return true iff iteration has completed (reached last va)
    inline bool done() const;
    // Return physical address mapped at `va()`,
    // or `(uintptr_t) -1` if `va()` is unmapped
    inline uint64_t pa() const;
    // Return a kernel-accessible pointer corresponding to `pa()`,
    // or `nullptr` if `va()` is unmapped
    template <typename T = void*>
    inline T kptr() const;

    // PERMISSIONS
    // Return permissions at `va()` (or 0 if `PTE_P` is not set)
    inline uint64_t perm() const;
    // Return true iff `va()` is present (`PTE_P`)
    inline bool present() const;
    // Return true iff `va()` is present and writable (`PTE_P|PTE_W`)
    inline bool writable() const;
    // Return true iff `va()` is present and unprivileged (`PTE_P|PTE_U`)
    inline bool user() const;

    // ADVANCED PERMISSIONS
    // Return true iff `(perm() & desired_perm) == desired_perm`
    inline bool perm(uint64_t desired_perm) const;
    // Return intersection of permissions in [va(), va() + sz)
    uint64_t range_perm(size_t sz) const;
    // Return true iff `(range_perm(sz) & desired_perm) == desired_perm`
    inline bool range_perm(size_t sz, uint64_t desired_perm) const;

    // TRAVERSAL
    // Move to virtual address `va`; return `*this`
    inline vmiter& find(uintptr_t va);
    // Advance to virtual address `va() + delta`; return `*this`
    inline vmiter& operator+=(intptr_t delta);
    // Advance to virtual address `va() + 1`; return `*this`
    inline vmiter& operator++();
    inline void operator++(int);
    // Advance to virtual address `va() - delta`; return `*this`
    inline vmiter& operator-=(intptr_t delta);
    // Advance to virtual address `va() - 1`; return `*this`
    inline vmiter& operator--();
    inline void operator--(int);
    // Move to next larger page-aligned virtual address, skipping large
    // non-present regions
    void next();
    // Move to `last_va()`
    void next_range();

    // MAPPING MODIFICATION
    // Map current virtual address to `pa` with permissions `perm`.
    // The current virtual address must be page-aligned. Calls `kalloc`
    // to allocate page table pages if necessary; returns 0 on success
    // and -1 on failure.
    [[gnu::warn_unused_result]] int try_map(uintptr_t pa, int perm);
    // Same, but map a kernel pointer
    [[gnu::warn_unused_result]] inline int try_map(void* kptr, int perm);
    [[gnu::warn_unused_result]] inline int try_map(volatile void* kptr, int perm);

    // Map current virtual address to `pa` with permissions `perm`.
    // The current virtual address must be page-aligned. Calls `kalloc`
    // to allocate page table pages if necessary; panics on failure.
    inline void map(uintptr_t pa, int perm);
    inline void map(void* kptr, int perm);
    inline void map(volatile void* kptr, int perm);

    // Free mapped page and clear mapping. Like `kfree(kptr()); map(0, 0)`
    inline void kfree_page();

  private:
    static constexpr int initial_lbits = PAGEOFFBITS + 3 * PAGEINDEXBITS;
    static constexpr int noncanonical_lbits = 47;
    static constexpr int done_lbits = 64;

    x86_64_pagetable* pt_;
    x86_64_pageentry_t* pep_;
    int lbits_;
    int perm_;
    uintptr_t va_;

    static constexpr int initial_perm = 0xFFF;
    static const x86_64_pageentry_t zero_pe;

    inline static constexpr uintptr_t lbits_mask(int lbits);
    void down();
    void real_find(uintptr_t va, bool stepping);
    friend class ptiter;
};


// `ptiter` walks over the page table pages in a page table,
// returning them in depth-first order.
// This is mainly useful when freeing a page table, as in:
// ```
// for (ptiter it(pt); it.low(); it.next()) {
//     it.kfree_ptp();
// }
// kfree(pt);
// ```
// Note that `ptiter` will never visit the root (level-4) page table page.

class ptiter {
  public:
    // Initialize a physical iterator for `pt` with initial virtual address 0
    ptiter(x86_64_pagetable* pt);
    inline ptiter(const proc* p);

    // Return true once `ptiter` has iterated over all page table pages
    // (not including the top-level page table page)
    inline bool done() const;

    // Return physical address of current page table page
    inline uintptr_t pa() const;
    // Return kernel-accessible pointer to the current page table page
    inline x86_64_pagetable* kptr() const;
    // Move to next page table page in depth-first order
    inline void next();

    // Return current virtual address
    inline uintptr_t va() const;
    // Return one past the last virtual address in this mapping range
    inline uintptr_t last_va() const;
    // Return true iff `va() <= VA_LOWMAX` (is low canonical)
    inline bool low() const;
    // Return level of current page table page (0-2)
    inline int level() const;

    // Free current page table page (`kptr()`) and unmap current entry
    inline void kfree_ptp();

  private:
    x86_64_pagetable* pt_;
    x86_64_pageentry_t* pep_;
    int lbits_;
    uintptr_t va_;

    void down(bool skip);
};


inline vmiter::vmiter(x86_64_pagetable* pt, uintptr_t va)
    : pt_(pt), pep_(&pt_->entry[0]), lbits_(initial_lbits),
      perm_(initial_perm), va_(0) {
    real_find(va, false);
}
inline vmiter::vmiter(const proc* p, uintptr_t va)
    : vmiter(p->pagetable_, va) {
}
inline uintptr_t vmiter::va() const {
    return va_;
}
inline bool vmiter::done() const {
    return lbits_ == done_lbits;
}
inline constexpr uintptr_t vmiter::lbits_mask(int lbits) {
    return ~(~uintptr_t(0) << lbits);
}
inline uintptr_t vmiter::last_va() const {
    if (lbits_ == noncanonical_lbits) {
        return VA_HIGHMIN;
    } else {
        return (va_ | lbits_mask(lbits_)) + 1;
    }
}
inline bool vmiter::low() const {
    return va_ <= VA_LOWMAX;
}
inline uint64_t vmiter::pa() const {
    if (*pep_ & PTE_P) {
        uintptr_t pa = *pep_ & PTE_PAMASK;
        if (lbits_ > PAGEOFFBITS) {
            pa &= ~0x1000UL;
        }
        return pa + (va_ & lbits_mask(lbits_));
    } else {
        return -1;
    }
}
template <typename T>
inline T vmiter::kptr() const {
    if (*pep_ & PTE_P) {
        return pa2kptr<T>(pa());
    } else {
        return nullptr;
    }
}
inline uint64_t vmiter::perm() const {
    // Returns 0-0xFFF. (XXX Does not track PTE_XD.)
    // Returns 0 unless `(*pep_ & perm_ & PTE_P) != 0`.
    uint64_t ph = *pep_ & perm_;
    return ph & -(ph & PTE_P);
}
inline bool vmiter::perm(uint64_t desired_perm) const {
    return (perm() & desired_perm) == desired_perm;
}
inline bool vmiter::present() const {
    return perm(PTE_P);
}
inline bool vmiter::writable() const {
    return perm(PTE_P | PTE_W);
}
inline bool vmiter::user() const {
    return perm(PTE_P | PTE_U);
}
inline bool vmiter::range_perm(size_t sz, uint64_t desired_perm) const {
    return (range_perm(sz) & desired_perm) == desired_perm;
}
inline vmiter& vmiter::find(uintptr_t va) {
    if (va != va_) {
        real_find(va, false);
    }
    return *this;
}
inline vmiter& vmiter::operator+=(intptr_t delta) {
    return find(va_ + delta);
}
inline vmiter& vmiter::operator++() {
    return find(va_ + 1);
}
inline void vmiter::operator++(int) {
    find(va_ + 1);
}
inline vmiter& vmiter::operator-=(intptr_t delta) {
    return find(va_ - delta);
}
inline vmiter& vmiter::operator--() {
    return find(va_ - 1);
}
inline void vmiter::operator--(int) {
    find(va_ - 1);
}
inline void vmiter::next_range() {
    real_find(last_va(), true);
}
inline int vmiter::try_map(void* kp, int perm) {
    return try_map(kptr2pa(kp), perm);
}
inline int vmiter::try_map(volatile void* kp, int perm) {
    return try_map(kptr2pa(kp), perm);
}
inline void vmiter::map(uintptr_t pa, int perm) {
    int r = try_map(pa, perm);
    assert(r == 0, "vmiter::map failed");
}
inline void vmiter::map(void* kp, int perm) {
    map(kptr2pa(kp), perm);
}
inline void vmiter::map(volatile void* kp, int perm) {
    map(kptr2pa(kp), perm);
}
inline void vmiter::kfree_page() {
    assert((va_ & (PAGESIZE - 1)) == 0);
    if (*pep_ & PTE_P) {
        kfree(kptr<void*>());
    }
    *pep_ = 0;
}

inline ptiter::ptiter(const proc* p)
    : ptiter(p->pagetable_) {
}
inline uintptr_t ptiter::va() const {
    return va_ & ~vmiter::lbits_mask(lbits_);
}
inline uintptr_t ptiter::last_va() const {
    return (va_ | vmiter::lbits_mask(lbits_)) + 1;
}
inline bool ptiter::low() const {
    return va_ <= VA_LOWMAX;
}
inline bool ptiter::done() const {
    return lbits_ == vmiter::done_lbits;
}
inline int ptiter::level() const {
    return (lbits_ - PAGEOFFBITS - PAGEINDEXBITS) / PAGEINDEXBITS;
}
inline void ptiter::next() {
    down(true);
}
inline uintptr_t ptiter::pa() const {
    return *pep_ & PTE_PAMASK;
}
inline x86_64_pagetable* ptiter::kptr() const {
    return pa2kptr<x86_64_pagetable*>(pa());
}
inline void ptiter::kfree_ptp() {
    kfree(kptr());
    *pep_ = 0;
}

#endif
