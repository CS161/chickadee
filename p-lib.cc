#include "p-lib.hh"

// dprintf
//    Construct a string from `format` and pass it to `sys_write(fd)`.
//    Returns the number of characters printed, or E_2BIG if the string
//    could not be constructed.

int dprintf(int fd, const char* format, ...) {
    char buff[1025];
    va_list val;
    va_start(val, format);
    size_t n = vsnprintf(buff, sizeof(buff), format, val);
    if (n < sizeof(buff)) {
        return sys_write(fd, buff, n);
    } else {
        return E_2BIG;
    }
}


// printf
//    Like `printf(1, ...)`.

int printf(const char* format, ...) {
    char buff[1025];
    va_list val;
    va_start(val, format);
    size_t n = vsnprintf(buff, sizeof(buff), format, val);
    if (n < sizeof(buff)) {
        return sys_write(1, buff, n);
    } else {
        return E_2BIG;
    }
}


// panic, assert_fail
//     Call the SYSCALL_PANIC system call so the kernel loops until Control-C.

void panic(const char* format, ...) {
    va_list val;
    va_start(val, format);
    char buff[160];
    memcpy(buff, "PANIC: ", 7);
    int len = vsnprintf(&buff[7], sizeof(buff) - 7, format, val) + 7;
    va_end(val);
    if (len > 0 && buff[len - 1] != '\n') {
        strcpy(buff + len - (len == (int) sizeof(buff) - 1), "\n");
    }
    (void) console_printf(CPOS(23, 0), 0xC000, "%s", buff);
    sys_panic(NULL);
}

int error_vprintf(int cpos, int color, const char* format, va_list val) {
    return console_vprintf(cpos, color, format, val);
}

void assert_fail(const char* file, int line, const char* msg) {
    error_printf(CPOS(23, 0), COLOR_ERROR,
                 "%s:%d: user assertion '%s' failed\n",
                 file, line, msg);
    sys_panic(NULL);
}


// sys_clone
//    Create a new thread.

pid_t sys_clone(int (*function)(void*), void* arg, char* stack_top) {
    // Your code here
    return E_NOSYS;
}
