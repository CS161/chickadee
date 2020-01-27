#include "u-lib.hh"

void process_main(int argc, char** argv) {
    char buf[1];

    const char* filename = "emerson.txt";
    if (argc > 1) {
        filename = argv[1];
    }

    size_t off = 0;
    while (true) {
        ssize_t n = sys_readdiskfile(filename, buf, sizeof(buf), off);
        if (n < 0) {
            dprintf(2, "%s: error %d\n", filename, int(n));
            sys_exit(1);
        } else if (n == 0) {
            break;
        }
        sys_write(1, buf, n);
        off += n;
    }

    sys_exit(0);
}
