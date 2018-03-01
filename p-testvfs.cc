#include "p-lib.hh"

int fdprintf(int fd, const char* format, ...) {
    va_list val;
    va_start(val, format);
    char buf[1000];
    int l = vsnprintf(buf, sizeof(buf), format, val);
    va_end(val);
    return sys_write(fd, buf, l);
}

void process_main() {
    sys_kdisplay(KDISPLAY_NONE);

    int r = fdprintf(0, "0\n");
    assert(r > 0);

    r = fdprintf(1, "1\n");
    assert(r > 0);

    r = fdprintf(2, "2\n");
    assert(r > 0);

    r = fdprintf(3, "Nope\n");
    assert(r == E_BADF);

    r = fdprintf(-1, "Nope\n");
    assert(r == E_BADF);

    r = sys_dup2(2, 3);
    assert(r == 0);

    r = fdprintf(3, "3\n");
    assert(r > 0);

    r = sys_close(3);
    assert(r == 0);

    r = fdprintf(3, "Nope\n");
    assert(r == E_BADF);

    r = sys_dup2(2, 3);
    assert(r == 0);

    r = fdprintf(3, "4\n");
    assert(r > 0);

    r = sys_dup2(4, 3);
    assert(r == E_BADF);

    r = fdprintf(3, "5\n");
    assert(r > 0);

    r = sys_dup2(3, 3);
    assert(r == 0);

    r = fdprintf(3, "6\n");
    assert(r > 0);

    r = sys_close(0);
    assert(r == 0);

    r = sys_close(1);
    assert(r == 0);

    r = fdprintf(0, "Nope\n");
    assert(r == E_BADF);

    r = sys_close(2);
    assert(r == 0);

    r = sys_dup2(3, 2);
    assert(r == 0);

    r = sys_dup2(2, 0);
    assert(r == 0);

    r = fdprintf(2, "7\n");
    assert(r > 0);

    r = fdprintf(3, "8\n");
    assert(r > 0);

    r = fdprintf(0, "9\n");
    assert(r > 0);


    console_printf("If you see 0-9 in sequence, testvfs succeeded.\n");
    sys_exit(0);
}
