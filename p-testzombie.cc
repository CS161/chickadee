#include "p-lib.hh"

void process_main() {
    sys_kdisplay(KDISPLAY_NONE);

    for (int i = 0; i < 8; ++i) {
        console_printf("making zombies, round %d...\n", i);
        pid_t child = sys_fork();
        assert(child >= 0);
        if (child == 0) {
            for (int g = 0; g < 10; ++g) {
                pid_t grandchild = sys_fork();
                assert(grandchild >= 0);
                if (grandchild == 0) {
                    sys_msleep(100 + 10 * g);
                    sys_exit(0);
                }
            }
            sys_exit(0);
        }

        pid_t waited = sys_waitpid(child);
        assert_eq(waited, child);
        console_printf("zombies hopefully being reaped\n");
        sys_msleep(200);
    }

    console_printf("testzombie succeeded.\n");
    sys_exit(0);
}
