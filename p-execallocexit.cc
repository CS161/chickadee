#include "p-lib.hh"

void process_main() {
    sys_kdisplay(KDISPLAY_NONE);

    for (int i = 5; i > 0; --i) {
        char buf[80];
        int n = snprintf(buf, sizeof(buf),
                         "Running memviewer in %d...\n", i);
        sys_write(1, buf, n);
        sys_msleep(250);
    }

    const char* args[] = {
        "allocexit", nullptr
    };
    int r = sys_execv("allocexit", args);
    assert_eq(r, 0);

    sys_exit(0);
}
