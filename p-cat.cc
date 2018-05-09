#include "p-lib.hh"

void process_main(int argc, char** argv) {
    sys_kdisplay(KDISPLAY_NONE);
    char buff[256];

    for (int i = 1; i == 1 || i < argc; ++i) {
        int f = 0;
        if (i < argc && strcmp(argv[i], "-") != 0) {
            f = sys_open(argv[i], OF_READ);
            if (f < 0) {
                dprintf(2, "%s: error %d\n", argv[i], f);
                sys_exit(1);
            }
        }
        while (1) {
            ssize_t n = sys_read(f, buff, sizeof(buff));
            if (n == 0 || (n < 0 && n != E_AGAIN)) {
                break;
            }
            if (n > 0) {
                ssize_t w = sys_write(1, buff, n);
                if (w != n) {
                    break;
                }
            }
        }
        if (f != 0) {
            sys_close(f);
        }
    }

    sys_exit(0);
}
