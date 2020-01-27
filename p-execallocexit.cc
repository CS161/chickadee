#include "u-lib.hh"

void process_main() {
    for (int i = 5; i > 0; --i) {
        printf("Running memviewer in %d...\n", i);
        sys_msleep(250);
    }

    const char* args[] = {
        "allocexit", nullptr
    };
    int r = sys_execv("allocexit", args);
    assert_eq(r, 0);

    sys_exit(0);
}
