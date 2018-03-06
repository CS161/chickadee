#include "p-lib.hh"

void process_main() {
    sys_kdisplay(KDISPLAY_NONE);

    sys_write(1, "About to greet you...\n", 22);

    const char* args[] = {
        "echo", "hello,", "world", nullptr
    };
    int r = sys_execv("echo", args);
    assert_eq(r, 0);

    sys_exit(0);
}
