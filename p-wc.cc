#include "p-lib.hh"

void process_main() {
    sys_kdisplay(KDISPLAY_NONE);
    static char buf[4096];
    size_t nchars = 0, nwords = 0, nlines = 0;
    bool inword = false;

    while (1) {
        ssize_t n = sys_read(0, buf, sizeof(buf));
        if (n == 0 || (n < 0 && n != E_AGAIN)) {
            break;
        }
        for (ssize_t i = 0; i < n; ++i) {
            ++nchars;
            if (!inword && !isspace(buf[i])) {
                ++nwords;
            }
            inword = !isspace(buf[i]);
            if (buf[i] == '\n') {
                ++nlines;
            }
        }
    }

    int n = snprintf(buf, sizeof(buf), " %7zu %7zu %7zu\n",
                     nlines, nwords, nchars);
    sys_write(1, buf, n);

    sys_exit(0);
}
