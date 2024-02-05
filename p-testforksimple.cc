#include "u-lib.hh"

void process_main() {
    pid_t initial_pid = sys_getpid();
    assert(initial_pid > 0);

    // Fork a total of three new copies, checking return values for simple
    // issues.
    pid_t p1 = sys_fork();
    assert(p1 >= 0);
    pid_t intermediate_pid = sys_getpid();
    if (p1 == 0) {
        assert(intermediate_pid != initial_pid);
    } else {
        assert(intermediate_pid == initial_pid);
        assert(p1 != initial_pid);
    }

    pid_t p2 = sys_fork();
    assert(p2 >= 0);
    pid_t final_pid = sys_getpid();
    if (p2 == 0) {
        assert(final_pid != initial_pid && final_pid != intermediate_pid);
    } else {
        assert(final_pid == intermediate_pid);
        assert(p2 != initial_pid && p2 != intermediate_pid && p2 != p1);
    }

    // console_printf("testforksimple succeeded.\n");
    // This test runs before `sys_exit` is implemented, so we can’t use it.
    while (true) {
    }
}
