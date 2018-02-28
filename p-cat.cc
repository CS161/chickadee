#include "p-lib.hh"

void process_main() {
    sys_kdisplay(KDISPLAY_NONE);
    char buf[200];

    while (1) {
        ssize_t n = sys_read(0, buf, sizeof(buf));
        if (n == 0 || (n < 0 && n != E_AGAIN)) {
            break;
        }
        if (n > 0) {
            ssize_t w = sys_write(1, buf, n);
            if (w != n) {
                break;
            }
        }
    }

    sys_exit(0);
}
