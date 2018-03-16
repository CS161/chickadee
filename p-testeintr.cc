#include "p-lib.hh"

static const int order[] = {
    2, 7, 6, 4, 1, 8, 5, 3
};

static void make_children(pid_t* children) {
    for (size_t i = 0; i != arraysize(order); ++i) {
        pid_t p = sys_fork();
        if (p == 0) {
            sys_msleep(order[i] * 500);
            sys_exit(order[i]);
        }
        children[i] = p;
    }
}

void process_main() {
    sys_kdisplay(KDISPLAY_NONE);

    pid_t children[arraysize(order)];
    make_children(children);
    for (size_t i = 0; i != arraysize(order); ++i) {
        int r = sys_msleep(1000);
        assert_eq(r, E_INTR);

        int status = 0;
        pid_t ch = sys_waitpid(0, &status, W_NOHANG);
        assert(ch > 0);

        size_t idx = 0;
        while (idx != arraysize(order) && children[idx] != ch) {
            ++idx;
        }
        assert(idx < arraysize(order));
        children[idx] = 0;

        console_printf("%d @%lu: exit status %d\n", ch, idx, status);
        assert_eq(order[idx], status);
    }
    assert_eq(sys_waitpid(0), E_CHILD);
    assert_eq(sys_msleep(1000), 0);


    console_printf("testeintr succeeded.\n");
    sys_exit(0);
}
