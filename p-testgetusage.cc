#define CHICKADEE_OPTIONAL_PROCESS 1
#include "u-lib.hh"

extern uint8_t end[];

void process_main() {
    usage u;
    int r = sys_getusage(&u);
    assert_eq(r, 0);

    assert_ge(static_cast<long>(u.time), 0);

    size_t total_pages = u.free_pages + u.allocated_pages;
    assert_gt(total_pages, 0UL);
    assert_le(total_pages, 0x100000UL);
    assert_gt(u.free_pages, 3UL);

    // allocate a page and recheck
    uint8_t* ptr = reinterpret_cast<uint8_t*>(
        round_up(reinterpret_cast<uintptr_t>(end), PAGESIZE)
    );
    r = sys_page_alloc(ptr);
    assert_eq(r, 0);

    usage u1;
    r = sys_getusage(&u1);
    assert_eq(r, 0);
    assert_ge(u1.time, u.time);
    assert_eq(total_pages, u1.free_pages + u1.allocated_pages);
    assert_le(u1.free_pages, u.free_pages - 1);

    // check illegal accesses
    r = sys_getusage(nullptr);
    assert_eq(r, E_FAULT);

    r = sys_getusage(reinterpret_cast<usage*>(0xFFFF'FFFF'FFFF'FFFCUL));
    assert_eq(r, E_FAULT);

    r = sys_getusage(reinterpret_cast<usage*>(0x1000));
    assert_eq(r, E_FAULT);

    r = sys_getusage(reinterpret_cast<usage*>(&ptr[PAGESIZE - 4]));
    assert_eq(r, E_FAULT);

    // allocate two more pages
    r = sys_page_alloc(&ptr[PAGESIZE * 2]);
    assert_eq(r, 0);
    r = sys_page_alloc(&ptr[PAGESIZE]);
    assert_eq(r, 0);

    usage* uptr = reinterpret_cast<usage*>(&ptr[PAGESIZE * 2 - 8]);
    r = sys_getusage(uptr);
    assert_eq(r, 0);
    assert_ge(uptr->time, u1.time);
    assert_eq(total_pages, uptr->free_pages + uptr->allocated_pages);
    assert_le(uptr->free_pages, u.free_pages - 3);

    console_printf(CS_SUCCESS "testgetusage succeeded!\n");

    // This test runs before `sys_exit` is implemented, so we can’t use it.
    while (true) {
    }
}
