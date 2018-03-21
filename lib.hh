#ifndef CHICKADEE_LIB_HH
#define CHICKADEE_LIB_HH
#include "types.h"
#include <new>              // for placement new
#include <type_traits>

// lib.hh
//
//    Functions, constants, and definitions useful in both the kernel
//    and applications.
//
//    Contents: (1) C library subset, (2) system call numbers, (3) console.

extern "C" {
void* memcpy(void* dst, const void* src, size_t n);
void* memmove(void* dst, const void* src, size_t n);
void* memset(void* s, int c, size_t n);
int memcmp(const void* a, const void* b, size_t n);
size_t strlen(const char* s);
size_t strnlen(const char* s, size_t maxlen);
char* strcpy(char* dst, const char* src);
int strcmp(const char* a, const char* b);
int strncmp(const char* a, const char* b, size_t maxlen);
char* strchr(const char* s, int c);
long strtol(const char* s, char** endptr = nullptr, int base = 0);
unsigned long strtoul(const char* s, char** endptr = nullptr, int base = 0);
int snprintf(char* s, size_t size, const char* format, ...);
int vsnprintf(char* s, size_t size, const char* format, va_list val);
inline bool isspace(unsigned char c);
}

#define RAND_MAX 0x7FFFFFFF
int rand();
void srand(unsigned seed);
int rand(int min, int max);


// Return the offset of `member` relative to the beginning of a struct type
#ifndef offsetof
#define offsetof(type, member)  __builtin_offsetof(type, member)
#endif

// Return the number of elements in an array
#define arraysize(array)        (sizeof(array) / sizeof(array[0]))


// Type information

// printfmt<T>
//    `printfmt<T>::spec` defines a printf specifier for type T.
//    E.g., `printfmt<int>::spec` is `"d"`.

template <typename T> struct printfmt {};
template <> struct printfmt<bool>           { static constexpr char spec[] = "d"; };
template <> struct printfmt<char>           { static constexpr char spec[] = "c"; };
template <> struct printfmt<signed char>    { static constexpr char spec[] = "d"; };
template <> struct printfmt<unsigned char>  { static constexpr char spec[] = "u"; };
template <> struct printfmt<short>          { static constexpr char spec[] = "d"; };
template <> struct printfmt<unsigned short> { static constexpr char spec[] = "u"; };
template <> struct printfmt<int>            { static constexpr char spec[] = "d"; };
template <> struct printfmt<unsigned>       { static constexpr char spec[] = "u"; };
template <> struct printfmt<long>           { static constexpr char spec[] = "ld"; };
template <> struct printfmt<unsigned long>  { static constexpr char spec[] = "lu"; };
template <typename T> struct printfmt<T*>   { static constexpr char spec[] = "p"; };

template <typename T> constexpr char printfmt<T*>::spec[];


// Min, max, and rounding operations

#define MIN(_a, _b) ({                                          \
            typeof(_a) __a = (_a); typeof(_b) __b = (_b);       \
            __a <= __b ? __a : __b; })
#define MAX(_a, _b) ({                                          \
            typeof(_a) __a = (_a); typeof(_b) __b = (_b);       \
            __a >= __b ? __a : __b; })

// Round down to the nearest multiple of n
#define ROUNDDOWN(a, n) ({                                      \
        uint64_t __a = (uint64_t) (a);                          \
        (typeof(a)) (__a - __a % (n)); })
// Round up to the nearest multiple of n
#define ROUNDUP(a, n) ({                                        \
        uint64_t __n = (uint64_t) (n);                          \
        (typeof(a)) (ROUNDDOWN((uint64_t) (a) + __n - 1, __n)); })

template <typename T>
inline constexpr T min(T a, T b) {
    return a < b ? a : b;
}
template <typename T>
inline constexpr T max(T a, T b) {
    return b < a ? a : b;
}

// msb(x)
//    Return index of most significant bit in `x`, plus one.
//    Returns 0 if `x == 0`.
inline constexpr int msb(int x) {
    return x ? sizeof(x) * 8 - __builtin_clz(x) : 0;
}
inline constexpr int msb(unsigned x) {
    return x ? sizeof(x) * 8 - __builtin_clz(x) : 0;
}
inline constexpr int msb(long x) {
    return x ? sizeof(x) * 8 - __builtin_clzl(x) : 0;
}
inline constexpr int msb(unsigned long x) {
    return x ? sizeof(x) * 8 - __builtin_clzl(x) : 0;
}
inline constexpr int msb(long long x) {
    return x ? sizeof(x) * 8 - __builtin_clzll(x) : 0;
}
inline constexpr int msb(unsigned long long x) {
    return x ? sizeof(x) * 8 - __builtin_clzll(x) : 0;
}

// rounddown_pow2(x)
//    Round x down to the nearest power of 2.
template <typename T>
inline constexpr T rounddown_pow2(T x) {
    static_assert(std::is_unsigned<T>::value, "T must be unsigned");
    return x ? T(1) << (msb(x) - 1) : 0;
}

// roundup_pow2(x)
//    Round x up to the nearest power of 2.
template <typename T>
inline constexpr T roundup_pow2(T x) {
    static_assert(std::is_unsigned<T>::value, "T must be unsigned");
    return x ? T(1) << msb(x - 1) : 0;
}


// Character traits

inline bool isspace(unsigned char ch) {
    return (ch >= '\t' && ch <= '\r') || ch == ' ';
}


// Checksums

uint32_t crc32c(uint32_t crc, const void* buf, size_t sz);
inline uint32_t crc32c(const void* buf, size_t sz) {
    return crc32c(0, buf, sz);
}


// System call numbers: an application calls `int NUM` to call a system call

#define SYSCALL_GETPID          1
#define SYSCALL_YIELD           2
#define SYSCALL_PAUSE           3
#define SYSCALL_PANIC           4
#define SYSCALL_PAGE_ALLOC      5
#define SYSCALL_FORK            6
#define SYSCALL_EXIT            7
#define SYSCALL_KDISPLAY        100
#define SYSCALL_READ            101
#define SYSCALL_WRITE           102
#define SYSCALL_CLOSE           103
#define SYSCALL_DUP2            104
#define SYSCALL_PIPE            105
#define SYSCALL_EXECV           106
#define SYSCALL_OPEN            107
#define SYSCALL_UNLINK          108
#define SYSCALL_READDISKFILE    109


// System call error return values

#define E_AGAIN         -11        // Try again
#define E_BADF          -9         // Bad file number
#define E_CHILD         -10        // No child processes
#define E_FAULT         -14        // Bad address
#define E_FBIG          -27        // File too large
#define E_INTR          -4         // Interrupted system call
#define E_INVAL         -22        // Invalid argument
#define E_IO            -5         // I/O error
#define E_MFILE         -24        // Too many open files
#define E_NFILE         -23        // File table overflow
#define E_NOENT         -2         // No such file or directory
#define E_NOEXEC        -8         // Exec format error
#define E_NOMEM         -12        // Out of memory
#define E_NOSPC         -28        // No space left on device
#define E_NOSYS         -38        // Invalid system call number
#define E_NXIO          -6         // No such device or address
#define E_PERM          -1         // Operation not permitted
#define E_PIPE          -32        // Broken pipe
#define E_SRCH          -3         // No such process
#define E_TXTBSY        -26        // Text file busy
#define E_2BIG          -7         // Argument list too long

#define E_MINERROR      -100

inline bool is_error(uintptr_t r) {
    return r >= static_cast<uintptr_t>(E_MINERROR);
}


// System call constants

// sys_kdisplay() types
#define KDISPLAY_NONE           0
#define KDISPLAY_MEMVIEWER      1

// sys_waitpid() options
#define W_NOHANG                1

// sys_open() flags
#define OF_READ                 1
#define OF_WRITE                2
#define OF_CREATE               4
#define OF_TRUNC                8


// Console printing

#define CPOS(row, col)  ((row) * 80 + (col))
#define CROW(cpos)      ((cpos) / 80)
#define CCOL(cpos)      ((cpos) % 80)

#define CONSOLE_COLUMNS 80
#define CONSOLE_ROWS    25
extern uint16_t console[CONSOLE_ROWS * CONSOLE_COLUMNS];

// current position of the cursor (80 * ROW + COL)
extern volatile int cursorpos;

// console_clear
//    Erases the console and moves the cursor to the upper left (CPOS(0, 0)).
void console_clear();

#define COLOR_ERROR 0xC000

// console_printf(cursor, color, format, ...)
//    Format and print a message to the x86 console.
//
//    The `format` argument supports some of the C printf function's escapes:
//    %d (to print an integer in decimal notation), %u (to print an unsigned
//    integer in decimal notation), %x (to print an unsigned integer in
//    hexadecimal notation), %c (to print a character), and %s (to print a
//    string). It also takes field widths and precisions like '%10s'.
//
//    The `cursor` argument is a cursor position, such as `CPOS(r, c)` for
//    row number `r` and column number `c`.
//
//    The `color` argument is the initial color used to print. 0x0700 is a
//    good choice (grey on black). The `format` escape %C changes the color
//    being printed.  It takes an integer from the parameter list.
//
//    Returns the final position of the cursor.
int console_printf(int cpos, int color, const char* format, ...)
    __attribute__((noinline));

// console_vprintf(cpos, color, format val)
//    The vprintf version of console_printf.
int console_vprintf(int cpos, int color, const char* format, va_list val)
    __attribute__((noinline));

// Helper versions that default to printing white-on-black at the cursor.
void console_printf(int color, const char* format, ...)
    __attribute__((noinline));
void console_printf(const char* format, ...)
    __attribute__((noinline));

// Generic print library

typedef struct printer printer;
struct printer {
    void (*putc)(printer* p, unsigned char c, int color);
};

void printer_vprintf(printer* p, int color, const char* format, va_list val);


// error_printf(cursor, color, format, ...)
//    Like `console_printf`, but `color` defaults to `COLOR_ERROR`, and
//    in the kernel, the message is also printed to the log.
int error_printf(int cpos, int color, const char* format, ...)
    __attribute__((noinline, cold));
int error_vprintf(int cpos, int color, const char* format, va_list val)
    __attribute__((noinline, cold));
void error_printf(int color, const char* format, ...)
    __attribute__((noinline, cold));
void error_printf(const char* format, ...)
    __attribute__((noinline, cold));


// Assertions

// assert(x)
//    If `x == 0`, print a message and fail.
#define assert(x)           do {                                        \
        if (!(x)) {                                                     \
            assert_fail(__FILE__, __LINE__, #x);                        \
        }                                                               \
    } while (0)
void __attribute__((noinline, noreturn, cold))
assert_fail(const char* file, int line, const char* msg);

// assert_eq(x, y)
//    Like `assert(x == y)`, but on failure prints the values of `x` and `y`.
#define assert_eq(x, y)     do {                                        \
        auto __x = (x); auto __y = (y);                                 \
        using __t = std::common_type<typeof(__x), typeof(__y)>::type;   \
        if (__x != __y) {                                               \
            assert_eq_fail<__t>(__FILE__, __LINE__, #x " == " #y, __x, __y); \
        }                                                               \
    } while (0)
template <typename T>
void __attribute__((noinline, noreturn, cold))
assert_eq_fail(const char* file, int line, const char* msg, T x, T y) {
    char fmt[32];
    snprintf(fmt, sizeof(fmt), "%%s:%%d: %%%s != %%%s\n",
             printfmt<T>::spec, printfmt<T>::spec);
    error_printf(CPOS(22, 0), COLOR_ERROR, fmt, file, line, x, y);
    assert_fail(file, line, msg);
}

// assert_ne(x, y)
//    Like `assert(x != y)`, but on failure prints the values of `x` and `y`.
#define assert_ne(x, y)     do {                                        \
        auto __x = (x); auto __y = (y);                                 \
        using __t = std::common_type<typeof(__x), typeof(__y)>::type;   \
        if (__x == __y) {                                               \
            assert_ne_fail<__t>(__FILE__, __LINE__, #x " != " #y, __x, __y); \
        }                                                               \
    } while (0)
template <typename T>
void __attribute__((noinline, noreturn, cold))
assert_ne_fail(const char* file, int line, const char* msg, T x, T y) {
    char fmt[32];
    snprintf(fmt, sizeof(fmt), "%%s:%%d: %%%s == %%%s\n",
             printfmt<T>::spec, printfmt<T>::spec);
    error_printf(CPOS(22, 0), COLOR_ERROR, fmt, file, line, x, y);
    assert_fail(file, line, msg);
}

// assert_gt(x, y)
//    Like `assert(x > y)`, but on failure prints the values of `x` and `y`.
#define assert_gt(x, y)     do {                                        \
        auto __x = (x); auto __y = (y);                                 \
        using __t = std::common_type<typeof(__x), typeof(__y)>::type;   \
        if (__x <= __y) {                                               \
            assert_gt_fail<__t>(__FILE__, __LINE__, #x " > " #y, __x, __y); \
        }                                                               \
    } while (0)
template <typename T>
void __attribute__((noinline, noreturn, cold))
assert_gt_fail(const char* file, int line, const char* msg, T x, T y) {
    char fmt[32];
    snprintf(fmt, sizeof(fmt), "%%s:%%d: %%%s <= %%%s\n",
             printfmt<T>::spec, printfmt<T>::spec);
    error_printf(CPOS(22, 0), COLOR_ERROR, fmt, file, line, x, y);
    assert_fail(file, line, msg);
}

// assert_memeq(x, y, sz)
//    If `memcmp(x, y, sz) != 0`, print a message and fail.
#define assert_memeq(x, y, sz)    do {                                  \
        auto __x = (x); auto __y = (y); size_t __sz = (sz);             \
        if (memcmp(__x, __y, __sz) != 0) {                              \
            assert_memeq_fail(__FILE__, __LINE__, "memcmp(" #x ", " #y ", " #sz ") == 0", __x, __y, __sz); \
        }                                                               \
    } while (0)
void __attribute__((noinline, noreturn, cold))
assert_memeq_fail(const char* file, int line, const char* msg,
                  const char* x, const char* y, size_t sz);


// panic(format, ...)
//    Print the message determined by `format` and fail.
void __attribute__((noinline, noreturn, cold))
panic(const char* format, ...);

#endif /* !CHICKADEE_LIB_H */
