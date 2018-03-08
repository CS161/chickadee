#include "p-lib.hh"
#define ANDAND  1
#define OROR    2

static pid_t create_child(char** words, char nextch,
                          char** redir, int* pipein);

void process_main() {
    static char buf[4096];      // static so stack size is small
    static char* words[256];

    sys_kdisplay(KDISPLAY_NONE);

    while (1) {
        // print prompt, read line
        ssize_t n = sys_write(1, "sh$ ", 4);
        assert(n == 4);

        n = sys_read(0, buf, sizeof(buf) - 1);
        if (n <= 0) {
            break;
        }
        buf[n] = 0;

        char* s = buf;
        char* end = buf + n;
        char** word = words;

        int pipein = 0;
        int skip_status = -1;
        char* redir[] = { nullptr, nullptr, nullptr };
        int redir_status = -1;

        while (s != end || word != words) {
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
            *s = 0;
            if (s != end) {
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

        while (sys_waitpid(0, nullptr, W_NOHANG) > 0) {
        }
    }

    sys_exit(0);
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
        char buf[120];
        int n = snprintf(buf, sizeof(buf), "%s: error %d\n",
                         pathname, r);
        sys_write(2, buf, n);
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
        int r = sys_execv(words[0], words);
        sys_exit(1);
    }

    if (*pipein != 0) {
        sys_close(*pipein);
        *pipein = 0;
    }
    if (nextch == '|') {
        *pipein = pfd[0];
        sys_close(pfd[1]);
    }
    return child;
}
