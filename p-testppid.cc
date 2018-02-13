#include "p-lib.hh"

void yield_n(int n) {
    while (n > 0) {
        sys_yield();
        --n;
    }
}


void process_main() {
    sys_kdisplay(KDISPLAY_NONE);

    assert(sys_getppid() == 1);
    pid_t original = sys_getpid();

    // Fork two children
    pid_t fork1 = sys_fork();
    pid_t after1 = sys_getpid();
    pid_t after1_parent = sys_getppid();
    pid_t fork2 = sys_fork();
    pid_t after2 = sys_getpid();

    // Check their parents
    if (fork1 == 0 && fork2 == 0) {
        assert(original != after1);
        assert(original == after1_parent);
        assert(after1 != after2);
        assert(sys_getppid() == after1);
    } else if (fork1 == 0) {
        assert(original != after1);
        assert(original == after1_parent);
        assert(after1 == after2);
        assert(sys_getppid() == original);
    } else if (fork2 == 0) {
        assert(original == after1);
        assert(1 == after1_parent);
        assert(after1 != after2);
        assert(sys_getppid() == original);
    } else {
        assert(original == after1);
        assert(1 == after1_parent);
        assert(after1 == after2);
        assert(sys_getppid() == 1);
    }

    if (sys_getpid() != original) {
        sys_exit(0);
    }

    // Original process: Yield a bunch so others can run tests
    yield_n(1000);
    console_printf("ppid tests without exit succeeded\n");

    // Tests that implicate `exit` behavior
    assert(original != 1);
    fork1 = sys_fork();

    if (fork1 == 0) {
        after1 = sys_getpid();
        after1_parent = sys_getppid();
        fork2 = sys_fork();
    } else {
        fork2 = -1;
    }

    pid_t after2_parent, fork3, after3;
    if (fork2 == 0) {
        after2 = sys_getpid();
        after2_parent = sys_getppid();
        fork3 = sys_fork();
    } else {
        fork3 = -1;
    }
    after3 = sys_getpid();

    if (fork3 == 0) {
        assert(original != after1);
        assert(after1_parent == original);
        assert(after2_parent == after1);
        assert(sys_getppid() == after2);
        yield_n(100);
        assert(sys_getppid() == after2);
        yield_n(100);
        assert(sys_getppid() == 1);
        sys_exit(0);
    } else if (fork2 == 0) {
        assert(original != after1);
        assert(sys_getppid() == after1);
        yield_n(100);
        assert(sys_getppid() == 1);
        yield_n(50);
        sys_exit(0);
    } else if (fork1 == 0) {
        yield_n(50);
        sys_exit(0);
    }

    yield_n(100);
    console_printf("ppid tests with exit succeed\n");

    sys_exit(0);
}
