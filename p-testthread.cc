#include "p-lib.hh"
#include <atomic>

extern uint8_t end[];

std::atomic_flag message_lock;
std::atomic<int> phase = 0;
int pfd[2] = {-1, -1};
const char* shared;

static void message(const char* x) {
    while (!message_lock.test_and_set()) {
        pause();
    }
    console_printf("T%d (P%d): %s\n", sys_gettid(), sys_getpid(), x);
    message_lock.clear();
}


static void thread1(void* x) {
    message("starting thread1");

    // wait for phase 1
    while (phase != 1) {
        sys_yield();
    }
    assert_memeq(shared, "Message to child\n", 17);

    // enter phase 2
    message("sending to main");
    shared = "Message to parent\n";
    phase = 2;

    // wait for phase 3
    while (phase != 3) {
        sys_yield();
    }
    char buf[100];
    memset(buf, 0, sizeof(buf));
    ssize_t n = sys_read(pfd[0], buf, sizeof(buf));
    assert_eq(n, 2);
    assert_memeq(buf, "Yo", 2);

    // enter phase 4
    message("piping to main");
    n = sys_write(pfd[1], "Hi", 2);
    assert_eq(n, 2);
    phase = 4;

    // wait for phase 5 (which never comes)
    while (phase != 5) {
        sys_yield();
    }
}

void process_main() {
    sys_kdisplay(KDISPLAY_NONE);

    // create thread
    message("clone");
    char* stack1 = reinterpret_cast<char*>
        (ROUNDUP((char*) end, PAGESIZE) + 16 * PAGESIZE);
    int r = sys_page_alloc(stack1);
    assert_eq(r, 0);

    pid_t t = sys_clone(thread1, pfd, stack1 + PAGESIZE);
    assert_gt(t, 0);

    // enter phase 1
    message("sending to secondary");
    shared = "Message to child\n";
    phase = 1;

    // wait for phase 2
    while (phase != 2) {
        sys_yield();
    }
    assert_memeq(shared, "Message to parent\n", 18);

    // enter phase 3
    message("piping to secondary");
    r = sys_pipe(pfd);
    assert_eq(r, 0);
    assert(pfd[0] > 2 && pfd[1] > 2);
    assert(pfd[0] != pfd[1]);
    r = sys_write(pfd[1], "Yo", 2);
    phase = 3;

    // wait for phase 4
    while (phase != 4) {
        sys_yield();
    }
    char buf[100];
    memset(buf, 0, sizeof(buf));
    r = sys_read(pfd[0], buf, sizeof(buf));
    assert_eq(r, 2);
    assert_memeq(buf, "Hi", 2);


    console_printf("testthread succeeded.\n");
    sys_exit(0);
}
