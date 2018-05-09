#include "p-lib.hh"

void process_main() {
    sys_kdisplay(KDISPLAY_NONE);

    int f = sys_open("emerson.txt", OF_READ);
    assert_gt(f, 2);

    char buff[200];
    ssize_t n = sys_read(f, buff, 1);
    assert_eq(n, 1);
    assert_memeq(buff, "W", 1);

    n = sys_read(f, buff, 2);
    assert_eq(n, 2);
    assert_memeq(buff, "he", 2);

    n = sys_read(f, buff, 3);
    assert_eq(n, 3);
    assert_memeq(buff, "n p", 3);


    int f2 = sys_open("emerson.txt", OF_READ);
    assert(f2 > 2 && f2 != f);

    n = sys_read(f2, buff, 5);
    assert_eq(n, 5);
    assert_memeq(buff, "When ", 5);

    n = sys_read(f, buff, 5);
    assert_eq(n, 5);
    assert_memeq(buff, "iped ", 5);


    int f3 = 3;
    while (f3 == f || f3 == f2) {
        ++f3;
    }
    int r = sys_dup2(f, f3);
    assert_ge(r, 0);

    n = sys_read(f, buff, 10);
    assert_eq(n, 10);
    assert_memeq(buff, "a tiny voi", 10);

    n = sys_read(f3, buff, 10);
    assert_eq(n, 10);
    assert_memeq(buff, "ce hard by", 10);

    n = sys_read(f, buff, 10);
    assert_eq(n, 10);
    assert_memeq(buff, ",\nGay and ", 10);

    r = sys_close(f);
    assert_eq(r, 0);

    n = sys_read(f, buff, 10);
    assert_eq(n, E_BADF);

    n = sys_read(f2, buff, 10);
    assert_eq(n, 10);
    assert_memeq(buff, "piped a ti", 10);

    n = sys_read(f3, buff, 10);
    assert_eq(n, 10);
    assert_memeq(buff, "polite, a ", 10);


    n = sys_read(f3, buff, sizeof(buff));
    assert_eq(n, 79);
    assert_memeq(buff, "cheerful cry,\n", 14);
    assert_memeq(buff + 72, "throat\n", 7);

    n = sys_read(f3, buff, sizeof(buff));
    assert_eq(n, 0);

    n = sys_read(f3, buff, sizeof(buff));
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
    assert_gt(f, 2);

    n = sys_read(f, buff, 4);
    assert_eq(n, 4);
    assert_memeq(buff, "When", 4);

    memcpy(page + PAGESIZE - 11, "emerson.txt", 11);
    f2 = sys_open(page + PAGESIZE - 11, OF_READ);
    assert_eq(f2, E_FAULT);


    console_printf("testmemfs succeeded.\n");
    sys_exit(0);
}
