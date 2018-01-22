#include "kernel.hh"
#include "k-lock.hh"

static spinlock page_lock;
static uintptr_t next_free_pa;

x86_64_page* kallocpage() {
    auto irqs = page_lock.lock();

    x86_64_page* p = nullptr;

    // skip over reserved and kernel memory
    auto range = physical_ranges.find(next_free_pa);
    while (range != physical_ranges.end()) {
        if (range->type() == mem_available) {
            // use this page
            p = pa2ka<x86_64_page*>(next_free_pa);
            next_free_pa += PAGESIZE;
            break;
        } else {
            // move to next range
            next_free_pa = range->last();
            ++range;
        }
    }

    page_lock.unlock(irqs);
    return p;
}
