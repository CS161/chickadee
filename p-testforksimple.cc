#define CHICKADEE_OPTIONAL_PROCESS 1
#include "u-lib.hh"

void process_main() {
    pid_t initial_pid = sys_getpid();
    assert_gt(initial_pid, 0);

    // Fork a total of three new copies, checking return values for simple
    // issues.
    pid_t p1 = sys_fork();
    assert_ge(p1, 0);
    pid_t intermediate_pid = sys_getpid();
    if (p1 == 0) {
        assert_ne(intermediate_pid, initial_pid);
    } else {
        assert_eq(intermediate_pid, initial_pid);
        assert_ne(p1, initial_pid);
    }

    pid_t p2 = sys_fork();
    assert_ge(p2, 0);
    pid_t final_pid = sys_getpid();
    if (p2 == 0) {
        assert_ne(final_pid, initial_pid);
        assert_ne(final_pid, intermediate_pid);
    } else {
        assert_eq(final_pid, intermediate_pid);
        assert_ne(p2, initial_pid);
        assert_ne(p2, intermediate_pid);
        assert_ne(p2, p1);
    }

    console_printf(CPOS(final_pid - 1, 0),
                   CS_SUCCESS "testforksimple %d [%d] %d [%d] %d succeeded!\n",
                   initial_pid, p1, intermediate_pid, p2, final_pid);

    // This test runs before `sys_exit` is implemented, so we can’t use it.
    while (true) {
    }
}
