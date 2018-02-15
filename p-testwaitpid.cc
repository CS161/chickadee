#include "p-lib.hh"

static const int order[] = {
    2, 7, 6, 4, 1, 8, 5, 3
};

static void make_children(pid_t* children) {
    for (size_t i = 0; i != arraysize(order); ++i) {
        pid_t p = sys_fork();
        if (p == 0) {
            sys_msleep(order[i] * 100);
            sys_exit(order[i]);
        }
        children[i] = p;
    }
}

void process_main() {
    sys_kdisplay(KDISPLAY_NONE);

    console_printf("waitpid(0, W_NOHANG) tests...\n");
    pid_t children[arraysize(order)];
    make_children(children);
    for (size_t i = 0; i != arraysize(order); ++i) {
        pid_t ch;
        int status = 0;
        while ((ch = sys_waitpid(0, &status, W_NOHANG)) == E_AGAIN) {
            sys_yield();
        }
        assert(ch > 0);

        size_t idx = 0;
        while (idx != arraysize(order) && children[idx] != ch) {
            ++idx;
        }
        assert(idx < arraysize(order));
        children[idx] = 0;

        console_printf("%d @%lu: exit status %d\n", ch, idx, status);
        assert(order[idx] == status);
    }
    assert(sys_waitpid(0, nullptr, W_NOHANG) == E_CHILD);
    console_printf("waitpid(0, W_NOHANG) tests succeed.\n");


    console_printf("waitpid(pid, W_NOHANG) tests...\n");
    make_children(children);
    for (size_t i = 0; i != arraysize(order); ++i) {
        pid_t ch;
        int status = 0;
        while ((ch = sys_waitpid(children[i], &status, W_NOHANG)) == E_AGAIN) {
            sys_yield();
        }
        assert(ch == children[i]);

        console_printf("%d @%lu: exit status %d\n", ch, i, status);
        assert(order[i] == status);
    }
    assert(sys_waitpid(0, nullptr, W_NOHANG) == E_CHILD);
    console_printf("waitpid(pid, W_NOHANG) tests succeed.\n");


    console_printf("waitpid(0) blocking tests...\n");
    make_children(children);
    for (size_t i = 0; i != arraysize(order); ++i) {
        int status = 0;
        pid_t ch = sys_waitpid(0, &status);
        assert(ch > 0);

        size_t idx = 0;
        while (idx != arraysize(order) && children[idx] != ch) {
            ++idx;
        }
        assert(idx < arraysize(order));
        children[idx] = 0;

        console_printf("%d @%lu: exit status %d\n", ch, idx, status);
        assert(order[idx] == status);
    }
    assert(sys_waitpid(0) == E_CHILD);
    console_printf("waitpid(0) blocking tests succeed.\n");


    console_printf("waitpid(pid) blocking tests...\n");
    make_children(children);
    for (size_t i = 0; i != arraysize(order); ++i) {
        int status = 0;
        pid_t ch = sys_waitpid(children[i], &status);
        assert(ch == children[i]);

        console_printf("%d @%lu: exit status %d\n", ch, i, status);
        assert(order[i] == status);
    }
    assert(sys_waitpid(0) == E_CHILD);
    console_printf("waitpid(pid) blocking tests succeed.\n");


    console_printf("testwaitpid succeeded.\n");
    sys_exit(0);
}
