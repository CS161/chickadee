#include "p-lib.hh"

void process_main() {
    sys_kdisplay(KDISPLAY_NONE);

    int f = sys_open("emerson.txt", OF_READ);
    assert(f >= 0);
    assert(f > 2);

    char buf[200];
    ssize_t n = sys_read(f, buf, 1);
    assert_eq(n, 1);
    assert_memeq(buf, "W", 1);

    n = sys_read(f, buf, 2);
    assert_eq(n, 2);
    assert_memeq(buf, "he", 2);

    n = sys_read(f, buf, 3);
    assert_eq(n, 3);
    assert_memeq(buf, "n p", 3);


    int f2 = sys_open("emerson.txt", OF_READ);
    assert(f2 > 2 && f2 != f);

    n = sys_read(f2, buf, 5);
    assert_eq(n, 5);
    assert_memeq(buf, "When ", 5);

    n = sys_read(f, buf, 5);
    assert_eq(n, 5);
    assert_memeq(buf, "iped ", 5);


    int f3 = 3;
    while (f3 == f || f3 == f2) {
        ++f3;
    }
    int r = sys_dup2(f, f3);
    assert(r >= 0);

    n = sys_read(f, buf, 10);
    assert_eq(n, 10);
    assert_memeq(buf, "a tiny voi", 10);

    n = sys_read(f3, buf, 10);
    assert_eq(n, 10);
    assert_memeq(buf, "ce hard by", 10);

    n = sys_read(f, buf, 10);
    assert_eq(n, 10);
    assert_memeq(buf, ",\nGay and ", 10);

    r = sys_close(f);
    assert_eq(r, 0);

    n = sys_read(f, buf, 10);
    assert_eq(n, E_BADF);

    n = sys_read(f2, buf, 10);
    assert_eq(n, 10);
    assert_memeq(buf, "piped a ti", 10);

    n = sys_read(f3, buf, 10);
    assert_eq(n, 10);
    assert_memeq(buf, "polite, a ", 10);


    n = sys_read(f3, buf, sizeof(buf));
    assert_eq(n, 79);
    assert_memeq(buf, "cheerful cry,\n", 14);
    assert_memeq(buf + 72, "throat\n", 7);

    n = sys_read(f3, buf, sizeof(buf));
    assert_eq(n, 0);

    n = sys_read(f3, buf, sizeof(buf));
    assert_eq(n, 0);


    r = sys_close(f2);
    assert_eq(r, 0);

    r = sys_close(f3);
    assert_eq(r, 0);


    r = sys_open(nullptr, OF_READ);
    assert_eq(r, E_FAULT);

    extern char end[];
    char* page = ROUNDUP((char*) end, PAGESIZE) + PAGESIZE;
    r = sys_page_alloc(page);
    assert_eq(r, 0);

    strcpy(page, "emerson.txt");
    f = sys_open(page, OF_READ);
    assert(f > 2);

    n = sys_read(f, buf, 4);
    assert_eq(n, 4);
    assert_memeq(buf, "When", 4);

    memcpy(page + PAGESIZE - 11, "emerson.txt", 11);
    f2 = sys_open(page + PAGESIZE - 11, OF_READ);
    assert_eq(f2, E_FAULT);


    console_printf("testmemfs succeeded.\n");
    sys_exit(0);
}
