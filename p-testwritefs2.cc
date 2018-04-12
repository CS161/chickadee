#include "p-lib.hh"

char bigbuf[4096];

size_t prepare_bigbuf() {
    for (unsigned i = 0; i != 46; ++i) {
        memcpy(&bigbuf[i * 88], "I should not talk so much about myself if there were any body else whom I knew as well.\n", 88);
    }
    return 46 * 88;
}

void process_main() {
    sys_kdisplay(KDISPLAY_NONE);

    printf("Starting testwritefs2 (assuming clean file system)...\n");

    // read file
    printf("%s:%d: read\n", __FILE__, __LINE__);

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


    // truncate file
    printf("%s:%d: truncate\n", __FILE__, __LINE__);

    f = sys_open("emerson.txt", OF_WRITE | OF_TRUNC);
    assert_gt(f, 2);

    n = sys_write(f, "CLARE ROJAS WAS HERE", 20);
    assert_eq(n, 20);

    sys_close(f);


    f = sys_open("emerson.txt", OF_READ);
    assert_gt(f, 2);

    memset(buf, 0, sizeof(buf));
    n = sys_read(f, buf, 200);
    assert_eq(n, 20);
    assert_memeq(buf, "CLARE ROJAS WAS HERE", 20);

    sys_close(f);


    // synchronize disk
    printf("%s:%d: sync\n", __FILE__, __LINE__);

    int r = sys_sync(1);
    assert_ge(r, 0);


    // read again, this time from disk
    printf("%s:%d: reread\n", __FILE__, __LINE__);

    f = sys_open("emerson.txt", OF_READ);
    assert_gt(f, 2);

    memset(buf, 0, sizeof(buf));
    n = sys_read(f, buf, 200);
    assert_eq(n, 20);
    assert_memeq(buf, "CLARE ROJAS WAS HERE", 20);

    sys_close(f);


    // seek within a file
    printf("%s:%d: lseek\n", __FILE__, __LINE__);

    f = sys_open("thoreau.txt", OF_READ);
    assert_gt(f, 2);

    n = sys_read(f, buf, 4);
    assert_eq(n, 4);
    assert_memeq(buf, "The ", 4);

    ssize_t sz = sys_lseek(f, 0, LSEEK_SIZE);
    assert_eq(sz, 685);

    n = sys_read(f, buf, 4);
    assert_eq(n, 4);
    assert_memeq(buf, "moon", 4);

    sz = sys_lseek(f, 0, LSEEK_SET);
    assert_eq(sz, 0);

    n = sys_read(f, buf, 8);
    assert_eq(n, 8);
    assert_memeq(buf, "The moon", 8);

    sz = sys_lseek(f, -4, LSEEK_CUR);
    assert_eq(sz, 4);

    n = sys_read(f, buf, 4);
    assert_eq(n, 4);
    assert_memeq(buf, "moon", 4);

    sys_close(f);


    // extend the file
    printf("%s:%d: extend\n", __FILE__, __LINE__);

    int wf = sys_open("thoreau.txt", OF_WRITE | OF_TRUNC);
    assert_gt(wf, 2);

    for (unsigned i = 0; i != 100; ++i) {
        dprintf(wf, "I should not talk so much about myself if there were any body else whom I knew as well.\n");
    }

    f = sys_open("thoreau.txt", OF_READ);
    assert_gt(f, 2);

    memset(buf, 'X', sizeof(buf));
    n = sys_read(f, buf, 9);
    assert_eq(n, 9);
    assert_memeq(buf, "I should X", 10);

    sz = sys_lseek(f, 4000, LSEEK_SET);
    assert_eq(sz, 4000);

    n = sys_read(f, buf, 9);
    assert_eq(n, 9);
    assert_memeq(buf, "f there wX", 10);

    sz = sys_lseek(f, 4090, LSEEK_SET);
    assert_eq(sz, 4090);

    n = sys_read(f, buf, 40);
    assert_eq(n, 40);
    assert_memeq(buf, "there were any body else whom I knew as ", 40);

    sys_close(f);

    sz = sys_lseek(wf, 0, LSEEK_SIZE);
    assert_eq(sz, 8800);

    sys_close(wf);


    // synchronize disk
    printf("%s:%d: sync\n", __FILE__, __LINE__);

    r = sys_sync(1);
    assert_ge(r, 0);


    // read again
    printf("%s:%d: reread\n", __FILE__, __LINE__);

    f = sys_open("thoreau.txt", OF_READ);
    assert_gt(f, 2);

    sz = sys_lseek(f, 0, LSEEK_SIZE);
    assert_eq(sz, 8800);

    memset(buf, 'X', sizeof(buf));
    n = sys_read(f, buf, 9);
    assert_eq(n, 9);
    assert_memeq(buf, "I should X", 10);

    sz = sys_lseek(f, 4000, LSEEK_SET);
    assert_eq(sz, 4000);

    n = sys_read(f, buf, 9);
    assert_eq(n, 9);
    assert_memeq(buf, "f there wX", 10);

    sz = sys_lseek(f, 4090, LSEEK_SET);
    assert_eq(sz, 4090);

    n = sys_read(f, buf, 40);
    assert_eq(n, 40);
    assert_memeq(buf, "there were any body else whom I knew as ", 40);

    sys_close(f);


    // extend the file to use an indirect block
    printf("%s:%d: extend to indirect", __FILE__, __LINE__);

    size_t bbsz = prepare_bigbuf();

    wf = sys_open("thoreau.txt", OF_WRITE);
    assert_gt(f, 2);

    for (int i = 0; i != 100; ++i) {
        dprintf(wf, "Chick%ddee\n", i);
        n = sys_write(wf, bigbuf, bbsz);
        assert_eq(size_t(n), bbsz);
        if (i % 10 == 0) {
            printf(".");
        }
    }

    printf("\n");

    sz = sys_lseek(f, 0, LSEEK_SIZE);
    assert_eq(sz, 405890);

    sys_close(wf);


    // compute crc32c
    printf("%s:%d: check checksum", __FILE__, __LINE__);

    f = sys_open("thoreau.txt", OF_READ);

    uint32_t crc = 0;
    sz = 0;
    ssize_t printsz = 0;
    while ((n = sys_read(f, bigbuf, sizeof(bigbuf))) > 0) {
        crc = crc32c(crc, bigbuf, n);
        sz += n;
        while (sz > printsz + 40000) {
            printf(".");
            printsz += 40000;
        }
    }
    printf("\n");

    assert_eq(sz, 405890);
    assert_eq(crc, 0x76954FBBU);


    // synchronize disk
    printf("%s:%d: sync\n", __FILE__, __LINE__);

    r = sys_sync(1);
    assert_ge(r, 0);


    // recompute crc32c
    printf("%s:%d: recheck checksum", __FILE__, __LINE__);

    f = sys_open("thoreau.txt", OF_READ);

    crc = 0;
    sz = 0;
    printsz = 0;
    while ((n = sys_read(f, bigbuf, sizeof(bigbuf))) > 0) {
        crc = crc32c(crc, bigbuf, n);
        sz += n;
        while (sz > printsz + 40000) {
            printf(".");
            printsz += 40000;
        }
    }
    printf("\n");

    assert_eq(sz, 405890);
    assert_eq(crc, 0x76954FBBU);


    printf("testwritefs2 succeeded.\n");
    sys_exit(0);
}
