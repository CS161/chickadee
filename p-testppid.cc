#include "p-lib.hh"

void process_main() {
    sys_kdisplay(KDISPLAY_NONE);

    assert_eq(sys_getppid(), 1);
    pid_t original = sys_getpid();

    // Fork two children
    pid_t fork1 = sys_fork();
    pid_t after1 = sys_getpid();
    pid_t after1_parent = sys_getppid();
    pid_t fork2 = sys_fork();
    pid_t after2 = sys_getpid();

    // Check their parents
    if (fork1 == 0 && fork2 == 0) {
        assert_ne(original, after1);
        assert_eq(original, after1_parent);
        assert_ne(after1, after2);
        assert_eq(sys_getppid(), after1);
    } else if (fork1 == 0) {
        assert_ne(original, after1);
        assert_eq(original, after1_parent);
        assert_eq(after1, after2);
        assert_eq(sys_getppid(), original);
    } else if (fork2 == 0) {
        assert_eq(original, after1);
        assert_eq(1, after1_parent);
        assert_ne(after1, after2);
        assert_eq(sys_getppid(), original);
    } else {
        assert_eq(original, after1);
        assert_eq(1, after1_parent);
        assert_eq(after1, after2);
        assert_eq(sys_getppid(), 1);
    }

    sys_msleep(50);
    if (sys_getpid() == original) {
        console_printf("ppid tests without exit succeed\n");
    } else {
        sys_exit(0);
    }


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
        assert_ne(original, after1);
        assert_eq(after1_parent, original);
        assert_eq(after2_parent, after1);
        assert_eq(sys_getppid(), after2);
        sys_msleep(100);
        assert_eq(sys_getppid(), after2);
        sys_msleep(100);
        assert_eq(sys_getppid(), 1);
        sys_exit(0);
    } else if (fork2 == 0) {
        assert_ne(original, after1);
        assert_eq(sys_getppid(), after1);
        sys_msleep(100);
        assert_eq(sys_getppid(), 1);
        sys_msleep(50);
        sys_exit(0);
    } else if (fork1 == 0) {
        sys_msleep(50);
        sys_exit(0);
    }

    for (int i = 0; i != 6; ++i) {
        sys_msleep(50); // loop because a long `msleep` could be interrupted
    }
    console_printf("ppid tests with exit succeed\n");
    console_printf("testppid succeeded.\n");
    sys_exit(0);
}
