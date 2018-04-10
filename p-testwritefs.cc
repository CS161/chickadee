#include "p-lib.hh"

void process_main() {
    sys_kdisplay(KDISPLAY_NONE);

    sys_write(1, "Starting testwritefs (assuming clean file system)...\n", 53);

    // read file
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
    int r = sys_sync(1);
    assert_ge(r, 0);


    // read again, this time from disk
    f = sys_open("emerson.txt", OF_READ);
    assert_gt(f, 2);

    memset(buf, 0, sizeof(buf));
    n = sys_read(f, buf, 200);
    assert_eq(n, 130);
    assert_memeq(buf, "CLARE ROOT Vegetablesce hard by,\n"
                 "Gay and polite, a cheerful cry,\n"
                 "Chic-chicadeedee", 81);

    sys_close(f);


    // truncate file
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


    // create file
    f = sys_open("geisel.txt", OF_WRITE);
    assert_lt(f, 0);
    assert_eq(f, E_NOENT);

    f = sys_open("geisel.txt", OF_WRITE);
    assert_lt(f, 0);

    f = sys_open("geisel.txt", OF_WRITE | OF_CREATE);
    assert_gt(f, 2);

    n = sys_write(f, "Why, girl, you're insane!\n"
                  "Elephants don't hatch chickadee eggs!\n", 64);
    assert_eq(n, 64);

    sys_close(f);


    f = sys_open("geisel.txt", OF_READ);
    assert_gt(f, 2);

    memset(buf, 0, sizeof(buf));
    n = sys_read(f, buf, 200);
    assert_eq(n, 64);
    assert_memeq(buf, "Why, girl, you're insane!\n"
                 "Elephants don't hatch chickadee eggs!\n", 64);

    sys_close(f);


    // read & write same file
    f = sys_open("geisel.txt", OF_READ);
    assert_gt(f, 2);

    wf = sys_open("geisel.txt", OF_WRITE);
    assert_gt(wf, 2);
    assert_ne(wf, f);

    memset(buf, 0, sizeof(buf));
    n = sys_read(f, buf, 4);
    assert_eq(n, 4);
    assert_memeq(buf, "Why,", 4);

    n = sys_write(wf, "Am I scaring you tonight?", 25);
    assert_eq(n, 25);

    memset(buf, 0, sizeof(buf));
    n = sys_read(f, buf, 25);
    assert_eq(n, 25);
    assert_memeq(buf, " scaring you tonight?\nEle", 25);

    n = sys_write(wf, "!", 1);
    assert_eq(n, 1);

    memset(buf, 0, sizeof(buf));
    n = sys_read(f, buf, 5);
    assert_eq(n, 5);
    assert_memeq(buf, "phant", 5);

    sys_close(f);
    sys_close(wf);


    console_printf("testwritefs succeeded.\n");
    sys_exit(0);
}
