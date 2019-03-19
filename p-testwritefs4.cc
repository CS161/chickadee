#include "u-lib.hh"

void process_main() {
    printf("Starting testwritefs4 (assuming clean file system)...\n");

    // read file
    printf("%s:%d: read...\n", __FILE__, __LINE__);

    int f = sys_open("emerson.txt", OF_READ);
    assert_gt(f, 2);

    char buf[200];
    memset(buf, 0, sizeof(buf));
    ssize_t n = sys_read(f, buf, 200);
    assert_eq(n, 130);
    assert_memeq(buf, "When piped a tiny voice hard by,\n"
                 "Gay and polite, a cheerful cry,\n"
                 "Chic-chicadeedee", 81);

    sys_close(f);


    // create file
    printf("%s:%d: create...\n", __FILE__, __LINE__);

    f = sys_open("geisel.txt", OF_WRITE | OF_CREATE);
    assert_gt(f, 2);

    n = sys_write(f, "Why, girl, you're insane!\n"
                  "Elephants don't hatch chickadee eggs!\n", 64);
    assert_eq(n, 64);

    sys_close(f);


    // read back
    printf("%s:%d: read created...\n", __FILE__, __LINE__);

    f = sys_open("geisel.txt", OF_READ);
    assert_gt(f, 2);

    memset(buf, 0, sizeof(buf));
    n = sys_read(f, buf, 200);
    assert_eq(n, 64);
    assert_memeq(buf, "Why, girl, you're insane!\n"
                 "Elephants don't hatch chickadee eggs!\n", 64);

    sys_close(f);


    // remove file
    printf("%s:%d: unlink...\n", __FILE__, __LINE__);

    int r = sys_unlink("geisel.txt");
    assert_eq(r, 0);

    f = sys_open("geisel.txt", OF_READ);
    assert_eq(f, E_NOENT);


    // synchronize disk
    printf("%s:%d: sync...\n", __FILE__, __LINE__);

    r = sys_sync(1);
    assert_ge(r, 0);


    // read back
    printf("%s:%d: reread...\n", __FILE__, __LINE__);

    f = sys_open("geisel.txt", OF_READ);
    assert_eq(f, E_NOENT);


    // unlink while open
    printf("%s:%d: unlink while open...\n", __FILE__, __LINE__);

    f = sys_open("dickinson.txt", OF_READ);
    assert_gt(f, 2);

    memset(buf, 0, sizeof(buf));
    n = sys_read(f, buf, 34);
    assert_eq(n, 34);
    assert_memeq(buf, "The Birds begun at Four o'clock -\n", 34);

    r = sys_unlink("dickinson.txt");
    assert_eq(r, 0);

    n = sys_read(f, buf, 24);
    assert_eq(n, 24);
    assert_memeq(buf, "Their period for Dawn -\n", 24);

    r = sys_unlink("dickinson.txt");
    assert_eq(r, E_NOENT);


    // recreate
    printf("%s:%d: recreate...\n", __FILE__, __LINE__);

    int wf = sys_open("dickinson.txt", OF_READ | OF_WRITE | OF_CREATE);
    assert_gt(wf, 2);
    assert_ne(f, wf);

    n = sys_read(wf, buf, sizeof(buf));
    assert_eq(n, 0);

    n = sys_write(wf, "A Bird, came down the Walk -\n"
                  "He did not know I saw -\n", 53);
    assert_eq(n, 53);

    size_t sz = sys_lseek(wf, 0, LSEEK_SET);
    assert_eq(sz, 0U);


    // reread
    printf("%s:%d: read both...\n", __FILE__, __LINE__);

    n = sys_read(f, buf, 28);
    assert_eq(n, 28);
    assert_memeq(buf, "A Music numerous as space -\n", 28);

    n = sys_read(wf, buf, 29);
    assert_eq(n, 29);
    assert_memeq(buf, "A Bird, came down the Walk -\n", 29);


    // synchronize
    printf("%s:%d: sync...\n", __FILE__, __LINE__);

    r = sys_sync(1);
    assert_ge(r, 0);


    // keep reading
    printf("%s:%d: continue reading...\n", __FILE__, __LINE__);

    n = sys_read(f, buf, 26);
    assert_eq(n, 26);
    assert_memeq(buf, "But neighboring as Noon -\n", 26);

    n = sys_read(wf, buf, 24);
    assert_eq(n, 24);
    assert_memeq(buf, "He did not know I saw -\n", 24);


    // close (should free file data)
    printf("%s:%d: close...\n", __FILE__, __LINE__);

    r = sys_close(f);
    assert_ge(r, 0);

    r = sys_close(wf);
    assert_ge(r, 0);


    // synchronize
    printf("%s:%d: sync...\n", __FILE__, __LINE__);

    r = sys_sync(1);
    assert_ge(r, 0);


    printf("testwritefs4 succeeded.\n");
    sys_exit(0);
}
