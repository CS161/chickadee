#include "u-lib.hh"

void process_main() {
    assert_eq(sys_getppid(), 1);
    pid_t fork1 = sys_fork();
    pid_t fork2 = sys_fork();

    // Original exits
    if (fork1 != 0 && fork2 != 0) {
        console_printf("0\n");
        sys_exit(0);
    } else if (fork1 == 0 && fork2 != 0) {
        sys_msleep(10);
        console_printf("1\n");
        sys_exit(0);
    } else if (fork1 != 0 && fork2 == 0) {
        sys_msleep(100);
        console_printf("2\n");
        sys_exit(0);
    } else {
        sys_msleep(300);
        console_printf("testhalt succeeds if you see 0-2 above and QEMU exits after a second\n");
        console_printf("(QEMU will only exit if you ran with `HALT=1`)\n");
        sys_msleep(1000);
        sys_exit(0);
    }
}
