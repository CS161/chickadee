#include "p-lib.hh"

static int unparse_counts(char* buf, size_t bufsz, int mode,
                          const size_t* counts, const char* fname) {
    const char* sep = strlen(fname) ? " " : "";
    if (mode < 0) {
        return snprintf(buf, bufsz, "%8zu %7zu %7zu%s%s\n",
                        counts[0], counts[1], counts[2], sep, fname);
    } else if (*sep) {
        return snprintf(buf, bufsz, "%8zu%s%s\n",
                        counts[mode], sep, fname);
    } else {
        return snprintf(buf, bufsz, "%zu\n", counts[mode]);
    }
}

void process_main(int argc, char** argv) {
    sys_kdisplay(KDISPLAY_NONE);
    static char buf[125]; // to catch alignment bugs

    // read `kernel` `testpipe` `kernel` `testpipe` `sh` by default
    if (argc <= 1) {
        static const char* const default_argv[] = {
            "wcdiskfile", "kernel", "testpipe", "kernel", "testpipe",
            "sh", nullptr
        };
        argc = 6;
        argv = const_cast<char**>(default_argv);
    }

    int mode = -1;
    int argno = 1;
    if (argno < argc) {
        if (strcmp(argv[argno], "-c") == 0) {
            mode = 0;
            ++argno;
        } else if (strcmp(argv[argno], "-w") == 0) {
            mode = 1;
            ++argno;
        } else if (strcmp(argv[argno], "-l") == 0) {
            mode = 2;
            ++argno;
        } else if (argv[argno][0] == '-' && argv[argno][1]) {
            sys_write(2, "wc: bad argument\n", 17);
            sys_exit(1);
        }
    }

    size_t totals[3] = {0, 0, 0};
    unsigned nfiles = 0;

    while (argno < argc) {
        size_t counts[3] = {0, 0, 0};
        bool inword = false;
        size_t off = 0;
        while (1) {
            ssize_t n = sys_readdiskfile(argv[argno], buf, sizeof(buf), off);
            if (n == 0 || (n < 0 && n != E_AGAIN)) {
                break;
            }
            for (ssize_t i = 0; i < n; ++i) {
                ++counts[0];
                if (!inword && !isspace(buf[i])) {
                    ++counts[1];
                }
                inword = !isspace(buf[i]);
                if (buf[i] == '\n') {
                    ++counts[2];
                }
            }
            off += n;
        }

        int n = unparse_counts(buf, sizeof(buf), mode, counts,
                               argno < argc ? argv[argno] : "");
        ssize_t w = sys_write(1, buf, n);
        assert(w == n);

        for (int i = 0; i < 3; ++i) {
            totals[i] += counts[i];
        }
        ++nfiles;
        ++argno;
    }

    if (nfiles > 1) {
        int n = unparse_counts(buf, sizeof(buf), mode, totals, "total");
        ssize_t w = sys_write(1, buf, n);
        assert(w == n);
    }

    sys_exit(0);
}
