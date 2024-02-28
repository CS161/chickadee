#include "u-lib.hh"

void process_main(int argc, char** argv) {
    long ns;
    char* endptr;
    if (argc != 2
        || (ns = strtol(argv[1], &endptr, 0)) < 0
        || endptr == argv[1]
        || *endptr) {
        dprintf(2, "usage: sleep seconds\n");
        sys_exit(1);
    } else {
        sys_msleep(1000 * ns);
        sys_exit(0);
    }
}
