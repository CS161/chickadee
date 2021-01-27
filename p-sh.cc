#include "u-lib.hh"
#define ANDAND  1
#define OROR    2

static void run_list(char* list);

void process_main() {
    static char buf[4096];      // static so stack size is small
    size_t pos = 0;
    bool done = false;

    while (!done) {
        // exit if too-long command line
        if (pos == sizeof(buf) - 1) {
            dprintf(2, "Command line too long\n");
            sys_exit(1);
        }

        // print prompt
        if (pos == 0) {
            ssize_t n = sys_write(1, "sh$ ", 4);
            assert(n == 4);
        }

        // read from input
        ssize_t n = sys_read(0, buf + pos, sizeof(buf) - pos - 1);
        if (n < 0) {
            dprintf(2, "Error reading from stdin\n");
            sys_exit(1);
        } else if (n == 0) {
            buf[pos + n] = '\n';
            done = true;
            ++n;
        }
        pos += n;
        buf[pos] = 0;
        // null characters not allowed
        assert(memchr(buf, 0, pos) == nullptr);

        // process lists from input
        size_t i = 0;
        while (true) {
            // skip whitespace
            while (isspace((unsigned char) buf[i])) {
                ++i;
            }

            // scan ahead to next separator
            size_t j = i;
            while (true) {
                if (buf[j] == ';' || buf[j] == '\n' || buf[j] == 0) {
                    break;
                } else if (buf[j] == '&') {
                    if (buf[j + 1] == 0) {
                        ++j;
                    } else if (buf[j + 1] == '&') {
                        j += 2;
                    } else {
                        break;
                    }
                } else {
                    ++j;
                }
            }

            // process separated list
            char sep = buf[j];
            if (sep == '&') {
                pid_t p = sys_fork();
                assert(p >= 0);
                if (p == 0) {
                    buf[j] = 0;
                    run_list(buf + i);
                    sys_exit(0);
                }
                i = j + 1;
            } else if (sep == ';' || sep == '\n') {
                buf[j] = 0;
                run_list(buf + i);
                i = j + 1;
            } else {
                break;
            }
        }

        // move remaining text to start of `buf`
        memmove(buf, buf + i, n - i);
        pos = n - i;

        // reap zombies
        while (sys_waitpid(0, nullptr, W_NOHANG) > 0) {
        }
    }

    sys_exit(0);
}

static pid_t create_child(char** words, char nextch,
                          char** redir, int* pipein);

static void run_list(char* s) {
    static char* words[512];
    char** word = words;
    int pipein = 0;
    int skip_status = -1;
    char* redir[] = { nullptr, nullptr, nullptr };
    int redir_status = -1;

    while (*s || word != words) {
        // read a word
        while (isspace((unsigned char) *s)
               && *s != '\n') {
            ++s;
        }
        char* wordstart = s;
        while (*s != 0
               && !isspace((unsigned char) *s)
               && *s != ';'
               && *s != '&'
               && *s != '|'
               && *s != '<'
               && *s != '>') {
            ++s;
        }
        char nextch = *s;
        if (*s != 0) {
            *s = 0;
            ++s;
        }
        if (nextch == '&' && *s == '&') {
            nextch = ANDAND;
            ++s;
        } else if (nextch == '|' && *s == '|') {
            nextch = OROR;
            ++s;
        }

        // add to word list
        if (*wordstart != 0) {
            if (redir_status != -1) {
                redir[redir_status] = wordstart;
                redir_status = -1;
            } else {
                *word = wordstart;
                ++word;
                assert(word < words + arraysize(words));
            }
        } else {
            if (word == words
                && redir_status == -1
                && pipein == 0) {
                break;
            }
            assert(word != words);
            assert(redir_status == -1);
        }

        // maybe execute
        if (nextch == '<') {
            redir_status = 0;
        } else if (nextch == '>') {
            redir_status = 1;
        } else if (!isspace((unsigned char) nextch)) {
            *word = nullptr;
            if (skip_status < 0) {
                pid_t child = create_child(words, nextch,
                                           redir, &pipein);
                if (nextch != '|'
                    && nextch != '&'
                    && child > 0) {
                    int r, status = -1;
                    while ((r = sys_waitpid(child, &status)) == E_AGAIN) {
                    }
                    assert(r == child);
                    if ((status == 0 && nextch == OROR)
                        || (status != 0 && nextch == ANDAND)) {
                        skip_status = status != 0;
                    }
                }
                redir[0] = redir[1] = redir[2] = nullptr;
            } else {
                if ((skip_status == 0 && nextch == ANDAND)
                    || (skip_status != 0 && nextch == OROR)) {
                    skip_status = -1;
                }
            }
            word = words;
        }
    }
}

static void child_redirect(int fd, char* pathname) {
    int flags;
    if (fd == 0) {
        flags = OF_READ;
    } else {
        flags = OF_WRITE | OF_CREATE | OF_TRUNC;
    }
    int r = sys_open(pathname, flags);
    if (r < 0) {
        dprintf(2, "%s: error %d\n", pathname, r);
        sys_exit(1);
    }
    if (r != fd) {
        sys_dup2(r, fd);
        sys_close(r);
    }
}

static pid_t create_child(char** words, char nextch,
                          char** redir, int* pipein) {
    int pfd[2] = {0, 0};
    if (nextch == '|') {
        int r = sys_pipe(pfd);
        assert_gt(r, -1);
    }

    // handle `exit [N]`
    int exit_status = -1;
    if (strcmp(words[0], "exit") == 0) {
        if (words[1] && isdigit((unsigned char) words[1][0])) {
            char* endl;
            exit_status = strtol(words[1], &endl, 10);
            if (*endl != '\0' || exit_status < 0) {
                exit_status = -2;
            }
        } else if (words[1]) {
            exit_status = -2;
        }
    }

    pid_t child = sys_fork();
    if (child == 0) {
        if (*pipein != 0) {
            sys_dup2(*pipein, 0);
            sys_close(*pipein);
        }
        for (int fd = 0; fd != 3; ++fd) {
            if (redir[fd] != nullptr) {
                child_redirect(fd, redir[fd]);
            }
        }
        if (nextch == '|') {
            sys_close(pfd[0]);
            sys_dup2(pfd[1], 1);
            sys_close(pfd[1]);
        }
        if (exit_status == -1) {
            // normal command execution
            int r = sys_execv(words[0], words);
            dprintf(2, "%s: execv failed (error %d)\n", words[0], r);
            exit_status = 1;
        } else {
            // `exit` command
            if (exit_status == -2) {
                dprintf(2, "exit: numeric argument required\n");
                exit_status = 1;
            }
        }
        sys_exit(exit_status);
    }

    if (*pipein != 0) {
        sys_close(*pipein);
        *pipein = 0;
    }
    if (nextch == '|') {
        *pipein = pfd[0];
        sys_close(pfd[1]);
    }
    if (exit_status != -1) {
        sys_exit(exit_status < 0 ? 1 : exit_status);
    }
    return child;
}
