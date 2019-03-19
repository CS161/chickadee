#include "u-lib.hh"

void process_main() {
    printf("Starting testwritefs (assuming clean file system)...\n");

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


    // overwrite start of file
    printf("%s:%d: overwrite start...\n", __FILE__, __LINE__);

    f = sys_open("emerson.txt", OF_WRITE);
    assert_gt(f, 2);

    n = sys_write(f, "OLEK WAS HERE", 13);
    assert_eq(n, 13);

    sys_close(f);


    f = sys_open("emerson.txt", OF_READ);
    assert_gt(f, 2);

    memset(buf, 0, sizeof(buf));
    n = sys_read(f, buf, 200);
    assert_eq(n, 130);
    assert_memeq(buf, "OLEK WAS HEREtiny voice hard by,\n"
                 "Gay and polite, a cheerful cry,\n"
                 "Chic-chicadeedee", 81);

    sys_close(f);


    // read & write same file
    printf("%s:%d: read and write...\n", __FILE__, __LINE__);

    f = sys_open("emerson.txt", OF_READ);
    assert_gt(f, 2);

    int wf = sys_open("emerson.txt", OF_WRITE);
    assert_gt(wf, 2);
    assert_ne(f, wf);

    memset(buf, 0, sizeof(buf));
    n = sys_read(f, buf, 8);
    assert_eq(n, 8);
    assert_memeq(buf, "OLEK WAS", 8);

    n = sys_write(wf, "CLARE ROJAS WAS HERE", 20);
    assert_eq(n, 20);

    memset(buf, 0, sizeof(buf));
    n = sys_read(f, buf, 20);
    assert_eq(n, 20);
    assert_memeq(buf, "JAS WAS HEREice hard", 20);

    n = sys_write(wf, "!", 1);
    assert_eq(n, 1);

    memset(buf, 0, sizeof(buf));
    n = sys_read(f, buf, 4);
    assert_eq(n, 4);
    assert_memeq(buf, " by,", 4);

    // cannot write fd open only for reading
    n = sys_write(f, "!", 1);
    assert_eq(n, E_BADF);

    // cannot read fd open only for writing
    n = sys_read(wf, buf, 1);
    assert_eq(n, E_BADF);

    sys_close(f);
    sys_close(wf);


    // read & write same file with combination flags
    printf("%s:%d: read|write...\n", __FILE__, __LINE__);

    f = sys_open("emerson.txt", OF_READ);
    assert_gt(f, 2);

    wf = sys_open("emerson.txt", OF_READ | OF_WRITE);
    assert_gt(wf, 2);
    assert_ne(f, wf);

    memset(buf, 0, sizeof(buf));
    n = sys_read(f, buf, 8);
    assert_eq(n, 8);
    assert_memeq(buf, "CLARE RO", 8);

    memset(buf, 0, sizeof(buf));
    n = sys_read(wf, buf, 8);
    assert_eq(n, 8);
    assert_memeq(buf, "CLARE RO", 8);

    n = sys_write(wf, "OT Vegetables", 13);
    assert_eq(n, 13);

    memset(buf, 0, sizeof(buf));
    n = sys_read(wf, buf, 7);
    assert_eq(n, 7);
    assert_memeq(buf, "ce hard", 7);

    memset(buf, 0, sizeof(buf));
    n = sys_read(f, buf, 13);
    assert_eq(n, 13);
    assert_memeq(buf, "OT Vegetables", 13);

    sys_close(f);
    sys_close(wf);


    // synchronize disk
    printf("%s:%d: sync disk...\n", __FILE__, __LINE__);

    int r = sys_sync(2);
    assert_ge(r, 0);


    // read again, this time from disk
    printf("%s:%d: read...\n", __FILE__, __LINE__);

    f = sys_open("emerson.txt", OF_READ);
    assert_gt(f, 2);

    memset(buf, 0, sizeof(buf));
    n = sys_read(f, buf, 200);
    assert_eq(n, 130);
    assert_memeq(buf, "CLARE ROOT Vegetablesce hard by,\n"
                 "Gay and polite, a cheerful cry,\n"
                 "Chic-chicadeedee", 81);

    sys_close(f);


    printf("testwritefs succeeded.\n");
    sys_exit(0);
}
