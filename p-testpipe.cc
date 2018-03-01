#include "p-lib.hh"

void process_main() {
    sys_kdisplay(KDISPLAY_NONE);

    console_printf("creating pipes...\n");

    int pfd[2] = {-1, -1};
    int x = sys_pipe(pfd);
    assert(x == 0 && pfd[0] > 2 && pfd[1] > 2 && pfd[0] != pfd[1]);

    int qfd[2] = {-1, -1};
    x = sys_pipe(qfd);
    assert(x == 0 && qfd[0] > 2 && qfd[1] > 2 && qfd[0] != qfd[1]);
    assert(pfd[0] != qfd[0] && pfd[0] != qfd[1]
           && pfd[1] != qfd[0] && pfd[1] != qfd[1]);

    console_printf("pfd %d,%d, qfd %d,%d\n", pfd[0], pfd[1], qfd[0], qfd[1]);


    // simple tests
    console_printf("simple tests...\n");

    ssize_t n = sys_write(pfd[1], "pfd1", 4);
    assert(n == 4);

    n = sys_write(qfd[1], "qfd1", 4);
    assert(n == 4);

    char buf[400];
    n = sys_read(pfd[0], buf, 2);
    assert(n == 2 && memcmp(buf, "pf", 2) == 0);

    n = sys_read(pfd[0], buf, 8);
    assert(n == 2 && memcmp(buf, "d1", 2) == 0);

    n = sys_read(qfd[0], buf, 100);
    assert(n == 4 && memcmp(buf, "qfd1", 4) == 0);


    // interleaving tests
    console_printf("interleaving tests...\n");

    n = sys_write(pfd[1], "W1", 2);
    assert(n == 2);

    n = sys_write(qfd[1], "w1", 2);
    assert(n == 2);

    n = sys_write(qfd[1], "w2", 2);
    assert(n == 2);

    n = sys_write(pfd[1], "W2", 2);
    assert(n == 2);

    n = sys_write(pfd[1], "W3!", 3);
    assert(n == 3);

    n = sys_write(qfd[1], "w3!", 3);
    assert(n == 3);

    n = sys_write(pfd[1], "W4!!!", 5);
    assert(n == 5);

    n = sys_write(qfd[1], "w4!!!", 5);
    assert(n == 5);

    n = sys_read(pfd[0], buf, 100);
    assert(n == 12 && memcmp(buf, "W1W2W3!W4!!!", 12) == 0);

    n = sys_read(qfd[0], buf, 100);
    assert(n == 12 && memcmp(buf, "w1w2w3!w4!!!", 12) == 0);


    // can't read from write end or write to read end
    console_printf("read/write error tests...\n");

    n = sys_read(pfd[1], buf, 8);
    assert(n == E_BADF);

    n = sys_write(qfd[0], buf, 1);
    assert(n == E_BADF);


    // inheritance tests
    console_printf("child tests...\n");

    pid_t p = sys_fork();
    assert(p >= 0);

    if (p == 0) {
        x = sys_close(pfd[1]);
        assert(x == 0);

        x = sys_close(qfd[0]);
        assert(x == 0);

        x = sys_close(pfd[1]);
        assert(x == E_BADF);

        n = sys_write(qfd[1], "hello mom", 9);
        assert(n == 9);

        n = sys_read(pfd[0], buf, 100);
        assert(n == 5 && memcmp(buf, "hello", 5) == 0);

        n = sys_read(pfd[0], buf, 100);
        assert(n == 5 && memcmp(buf, " babe", 5) == 0);

        x = sys_msleep(300);
        assert(x == 0);

        x = sys_close(pfd[0]);
        assert(x == 0);

        x = sys_close(qfd[1]);
        assert(x == 0);

        sys_exit(0);
    }

    x = sys_close(pfd[0]);
    assert(x == 0);

    x = sys_close(qfd[1]);
    assert(x == 0);

    n = sys_read(qfd[0], buf, 100);
    assert(n == 9 && memcmp(buf, "hello mom", 9) == 0);

    n = sys_write(pfd[1], "hello", 5);
    assert(n == 5);

    sys_msleep(300);

    n = sys_write(pfd[1], " babe", 5);
    assert(n == 5);

    n = sys_read(qfd[0], buf, 100);
    assert(n == 0);

    n = sys_write(pfd[1], "wharg", 5);
    assert(n == E_PIPE);

    x = sys_close(qfd[0]);
    assert(x == 0);

    x = sys_close(pfd[1]);
    assert(x == 0);


    // inheritance tests with close-on-exit
    console_printf("close-on-exit tests...\n");

    x = sys_pipe(pfd);
    assert(x == 0 && pfd[0] > 2 && pfd[1] > 2 && pfd[0] != pfd[1]);

    n = sys_write(pfd[1], "hello", 5);
    assert(n == 5);

    p = sys_fork();
    assert(p >= 0);

    if (p == 0) {
        n = sys_read(pfd[0], buf, 100);
        assert(n == 5 && memcmp(buf, "hello", 5) == 0);

        x = sys_msleep(400);
        assert(x == 0);

        sys_exit(0);
    }

    x = sys_msleep(100);
    assert(x == 0);

    x = sys_close(pfd[1]);
    assert(x == 0);

    n = sys_read(pfd[0], buf, 100);
    assert(n == 0);


    console_printf("testpipe succeeded.\n");
    sys_exit(0);
}
