#include "u-lib.hh"
#include <atomic>

extern uint8_t end[];

std::atomic_flag message_lock;
std::atomic<int> phase = 0;
int pfd[2] = {-1, -1};
const char* shared;

static void message(const char* x) {
    while (message_lock.test_and_set()) {
        pause();
    }
    console_printf("T%d (P%d): %s\n", sys_gettid(), sys_getpid(), x);
    message_lock.clear();
}


static int thread1a(void* x) {
    message("starting thread1a");

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

    // read from pipe, write to pipe
    char buf[100];
    memset(buf, 0, sizeof(buf));
    ssize_t n = sys_read(pfd[0], buf, sizeof(buf));
    assert_eq(n, 2);
    assert_memeq(buf, "Yo", 2);

    phase = 4;
    message("piping to main");
    n = sys_write(pfd[1], "Hi", 2);
    assert_eq(n, 2);

    sys_texit(0);
}


static int thread1b(void*) {
    // the argument to `sys_texit` is thrown away; we're just
    // checking that nothing goes badly wrong
    return 0;
}


static void test1() {
    // create thread
    message("clone");
    char* stack1 = reinterpret_cast<char*>(
        round_up(reinterpret_cast<uintptr_t>(end), PAGESIZE) + 16 * PAGESIZE
    );
    int r = sys_page_alloc(stack1);
    assert_eq(r, 0);

    pid_t t = sys_clone(thread1a, pfd, stack1 + PAGESIZE);
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
    phase = 3;

    r = sys_write(pfd[1], "Yo", 2);

    // enter phase 4
    while (phase != 4) {
        sys_yield();
    }
    char buf[100];
    memset(buf, 0, sizeof(buf));
    r = sys_read(pfd[0], buf, sizeof(buf));
    assert_eq(r, 2);
    assert_memeq(buf, "Hi", 2);

    // wait for thread to exit
    sys_msleep(10);
    message("simple thread tests succeed!");

    // start a new thread to check thread returning doesn't go wrong
    message("checking automated texit");
    t = sys_clone(thread1b, pfd, stack1 + PAGESIZE);
    assert_gt(t, 0);
    sys_msleep(10);
}


static int thread2a(void*) {
    // this blocks forever
    char buf[20];
    ssize_t n = sys_read(pfd[0], buf, sizeof(buf));
    assert_ne(n, 0);
    sys_yield();
    assert(false);
}

static void test2() {
    // create thread
    char* stack1 = reinterpret_cast<char*>(
        round_up(reinterpret_cast<uintptr_t>(end), PAGESIZE) + 16 * PAGESIZE
    );
    int r = sys_page_alloc(stack1);
    assert_eq(r, 0);

    pid_t t = sys_clone(thread2a, pfd, stack1 + PAGESIZE);
    assert_gt(t, 0);

    // this should quit the `thread2a` thread too
    sys_exit(161);
}


static int thread3a(void*) {
    sys_msleep(10);
    return 161;
}

static void test3() {
    // create thread
    char* stack1 = reinterpret_cast<char*>(
        round_up(reinterpret_cast<uintptr_t>(end), PAGESIZE) + 16 * PAGESIZE
    );
    int r = sys_page_alloc(stack1);
    assert_eq(r, 0);

    pid_t t = sys_clone(thread3a, pfd, stack1 + PAGESIZE);
    assert_gt(t, 0);

    // this should quit the `thread2a` thread too
    sys_texit(0);
}


void process_main() {
    // test1
    pid_t p = sys_fork();
    assert_ge(p, 0);
    if (p == 0) {
        test1();
        sys_exit(0);
    }
    pid_t ch = sys_waitpid(p);
    assert_eq(ch, p);


    // test2
    message("checking that exit exits all threads");
    int r = sys_pipe(pfd);
    assert_eq(r, 0);
    p = sys_fork();
    assert_ge(p, 0);
    if (p == 0) {
        test2();
    }
    int status = 0;
    ch = sys_waitpid(p, &status);
    assert_eq(ch, p);
    assert_eq(status, 161);

    // check that `thread2a` really exited; if it did not, then
    // the read end of the pipe will still be open (because `thread2a`
    // has the write end open)
    sys_close(pfd[1]);
    char buf[20];
    ssize_t n = sys_read(pfd[0], buf, sizeof(buf));
    assert_eq(n, 0);


    // test3
    message("checking that implicit texit sets status");
    p = sys_fork();
    assert_ge(p, 0);
    if (p == 0) {
        test3();
    }
    status = 0;
    ch = sys_waitpid(p, &status);
    assert_eq(ch, p);
    assert_eq(status, 161);


    console_printf("testthread succeeded.\n");
    sys_exit(0);
}
