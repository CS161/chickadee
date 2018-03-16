#include "p-lib.hh"

void process_main() {
    sys_kdisplay(KDISPLAY_NONE);

    const char msg1[] = "Type a few characters and press return:\n";
    ssize_t w = sys_write(1, msg1, strlen(msg1));
    assert_eq(static_cast<size_t>(w), strlen(msg1));

    char buf[200];
    ssize_t r = sys_read(0, buf, sizeof(buf));
    assert_gt(r, 0);


    // check invalid addresses
    w = sys_write(1, reinterpret_cast<const char*>(0x40000000UL), 0);
    assert_eq(w, 0);

    w = sys_write(1, reinterpret_cast<const char*>(VA_LOWEND), 0);
    assert_eq(w, 0);

    w = sys_write(1, msg1, 0xFFFFFFFFFFFFFFFFUL);
    assert_eq(w, E_FAULT);

    w = sys_write(1, reinterpret_cast<const char*>(10), 1);
    assert_eq(w, E_FAULT);

    w = sys_write(1, reinterpret_cast<const char*>(read_rbp()), 16384);
    assert_eq(w, E_FAULT);


    r = sys_read(0, reinterpret_cast<char*>(0x40000000UL), 0);
    assert_eq(r, 0);

    r = sys_read(0, reinterpret_cast<char*>(VA_LOWEND), 0);
    assert_eq(r, 0);

    r = sys_read(0, buf, 0xFFFFFFFFFFFFFFFFUL);
    assert_eq(r, E_FAULT);

    r = sys_read(0, reinterpret_cast<char*>(10), 1);
    assert_eq(r, E_FAULT);

    r = sys_read(0, reinterpret_cast<char*>(read_rbp()), 16384);
    assert_eq(r, E_FAULT);


    // ensure we can get right up to the end of low canonical memory
    char* pg = reinterpret_cast<char*>(VA_LOWEND - PAGESIZE);
    r = sys_page_alloc(pg);
    assert_eq(r, 0);

    memset(pg, '-', PAGESIZE);
    const char msg2[] = "Now type another few characters and press return:\n";
    char* pg_msg2 = pg + PAGESIZE - strlen(msg2);
    memcpy(pg_msg2, msg2, strlen(msg2));
    w = sys_write(1, pg_msg2, strlen(msg2));
    assert_eq(static_cast<size_t>(w), strlen(msg2));

    r = sys_read(0, pg_msg2, strlen(msg2));
    assert_gt(r, 0);


    console_printf("testrwaddr succeeded.\n");
    sys_exit(0);
}
