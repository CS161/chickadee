#define CHICKADEE_OPTIONAL_PROCESS 1
#include "u-lib.hh"

void process_main() {
    int r;
    while (true) {
        r = make_syscall(SYSCALL_KTEST, 1);
        if (r < 0) {
            console_printf(CS_ERROR "ktestwait appears to have failed!\n");
            console_printf(CS_ERROR "(However, this could be due to a slow computer.)\n");
            sys_exit(1);
        } else if (r >= 1000) {
            sys_exit(0);
        }
        sys_msleep(500);
    }
}
