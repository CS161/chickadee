#define CHICKADEE_OPTIONAL_PROCESS 1
#include "u-lib.hh"

static const int order[] = {
    2, 7, 6, 4, 1, 8, 5, 3
};

void process_main() {
    // create 8 processes
    int my_idx = 0;
    for (int i = 0; i < 3; ++i) {
        pid_t f = sys_fork();
        assert_ge(f, 0);
        my_idx = (my_idx * 2) + (f == 0);
    }

    // each process sleeps for `100 * order[my_idx]` milliseconds
    int r = sys_msleep(100 * order[my_idx]);
    assert_eq(r, 0);

    // then prints its position
    console_printf("%d [pid %d]\n", order[my_idx], sys_getpid());

    if (my_idx != 0) {
        sys_msleep(1000);
        sys_exit(0);
    }

    // test results by examining console
    sys_msleep(800);
    console_printf("You should see lines numbered 1-8 in order.\n");
    for (int i = 0; i < 8; ++i) {
        assert((console[i * CONSOLE_COLUMNS] & 0xFF) == '1' + i);
    }
    console_printf(CS_SUCCESS "testmsleep succeeded!\n");
    sys_exit(0);
}
