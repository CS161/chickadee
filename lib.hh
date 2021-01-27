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
void* memchr(const void* s, int c, size_t n);
size_t strlen(const char* s);
size_t strnlen(const char* s, size_t maxlen);
char* strcpy(char* dst, const char* src);
char* strncpy(char* dst, const char* src, size_t maxlen);
int strcmp(const char* a, const char* b);
int strncmp(const char* a, const char* b, size_t maxlen);
int strcasecmp(const char* a, const char* b);
int strncasecmp(const char* a, const char* b, size_t maxlen);
char* strchr(const char* s, int c);
long strtol(const char* s, char** endptr = nullptr, int base = 0);
unsigned long strtoul(const char* s, char** endptr = nullptr, int base = 0);
ssize_t snprintf(char* s, size_t size, const char* format, ...);
ssize_t vsnprintf(char* s, size_t size, const char* format, va_list val);
inline bool isspace(int c);
inline bool isdigit(int c);
inline bool islower(int c);
inline bool isupper(int c);
inline bool isalpha(int c);
inline bool isalnum(int c);
inline int tolower(int c);
inline int toupper(int c);
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


// Arithmetic

// min(a, b, ...)
//    Return the minimum of the arguments.
template <typename T>
inline constexpr T min(T a, T b) {
    return a < b ? a : b;
}
template <typename T, typename... Rest>
inline constexpr T min(T a, T b, Rest... c) {
    return min(min(a, b), c...);
}

// max(a, b, ...)
//    Return the maximum of the arguments.
template <typename T>
inline constexpr T max(T a, T b) {
    return b < a ? a : b;
}
template <typename T, typename... Rest>
inline constexpr T max(T a, T b, Rest... c) {
    return max(max(a, b), c...);
}

// msb(x)
//    Return index of most significant one bit in `x`, plus one.
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

// lsb(x)
//    Return index of least significant one bit in `x`, plus one.
//    Returns 0 if `x == 0`.
inline constexpr int lsb(int x) {
    return __builtin_ffs(x);
}
inline constexpr int lsb(unsigned x) {
    return __builtin_ffs(x);
}
inline constexpr int lsb(long x) {
    return __builtin_ffsl(x);
}
inline constexpr int lsb(unsigned long x) {
    return __builtin_ffsl(x);
}
inline constexpr int lsb(long long x) {
    return __builtin_ffsll(x);
}
inline constexpr int lsb(unsigned long long x) {
    return __builtin_ffsll(x);
}

// round_down(x, m)
//    Return the largest multiple of `m` less than or equal to `x`.
//    Equivalently, round `x` down to the nearest multiple of `m`.
template <typename T>
inline constexpr T round_down(T x, unsigned m) {
    static_assert(std::is_unsigned<T>::value, "T must be unsigned");
    return x - (x % m);
}

// round_up(x, m)
//    Return the smallest multiple of `m` greater than or equal to `x`.
//    Equivalently, round `x` up to the nearest multiple of `m`.
template <typename T>
inline constexpr T round_up(T x, unsigned m) {
    static_assert(std::is_unsigned<T>::value, "T must be unsigned");
    return round_down(x + m - 1, m);
}

// round_down_pow2(x)
//    Return the largest power of 2 less than or equal to `x`.
//    Equivalently, round `x` down to the nearest power of 2.
//    Returns 0 if `x == 0`.
template <typename T>
inline constexpr T round_down_pow2(T x) {
    static_assert(std::is_unsigned<T>::value, "T must be unsigned");
    return x ? T(1) << (msb(x) - 1) : 0;
}

// round_up_pow2(x)
//    Return the smallest power of 2 greater than or equal to `x`.
//    Equivalently, round `x` up to the nearest power of 2.
//    Returns 0 if `x == 0`.
template <typename T>
inline constexpr T round_up_pow2(T x) {
    static_assert(std::is_unsigned<T>::value, "T must be unsigned");
    return x ? T(1) << msb(x - 1) : 0;
}


// Character traits

inline bool isspace(int c) {
    return (c >= '\t' && c <= '\r') || c == ' ';
}
inline bool isdigit(int c) {
    return (unsigned(c) - unsigned('0')) < 10;
}
inline bool islower(int c) {
    return (unsigned(c) - unsigned('a')) < 26;
}
inline bool isupper(int c) {
    return (unsigned(c) - unsigned('A')) < 26;
}
inline bool isalpha(int c) {
    return ((unsigned(c) | 0x20) - unsigned('a')) < 26;
}
inline bool isalnum(int c) {
    return isalpha(c) || isdigit(c);
}

inline int tolower(int c) {
    return isupper(c) ? c + 'a' - 'A' : c;
}
inline int toupper(int c) {
    return islower(c) ? c + 'A' - 'a' : c;
}


// Checksums

uint32_t crc32c(uint32_t crc, const void* buf, size_t sz);
inline uint32_t crc32c(const void* buf, size_t sz) {
    return crc32c(0, buf, sz);
}


// Bit arrays

struct bitset_view {
    uint64_t* v_;
    size_t n_;

    struct bit {
        uint64_t& v_;
        uint64_t m_;

        inline constexpr bit(uint64_t& v, uint64_t m);
        NO_COPY_OR_ASSIGN(bit);
        inline constexpr operator bool() const;
        inline bit& operator=(bool x);
        inline bit& operator|=(bool x);
        inline bit& operator&=(bool x);
        inline bit& operator^=(bool x);
    };


    // initialize a bitset_view for the `n` bits starting at `v`
    inline bitset_view(uint64_t* v, size_t n)
        : v_(v), n_(n) {
    }

    // return size of the view
    inline constexpr size_t size() const;

    // return bit `i`, which can be examined or assigned
    inline bool operator[](size_t i) const;
    inline bit operator[](size_t i);

    // return minimum index of a 1-valued bit with index >= `i`, examining at
    // most `n` bits
    inline size_t find_lsb(size_t i = 0, size_t n = -1) const;

    // return minimum index of a 0-valued bit with index >= `i`, examining at
    // most `n` bits
    inline size_t find_lsz(size_t i = 0, size_t n = -1) const;
};


// System call numbers (passed in `%rax` at `syscall` time)

// Used in pset 1:
#define SYSCALL_GETPID          1
#define SYSCALL_YIELD           2
#define SYSCALL_PAUSE           3
#define SYSCALL_KDISPLAY        4
#define SYSCALL_PANIC           5
#define SYSCALL_PAGE_ALLOC      6
#define SYSCALL_FORK            7
// Used in later psets:
#define SYSCALL_EXIT            8
#define SYSCALL_READ            9
#define SYSCALL_WRITE           10
#define SYSCALL_CLOSE           11
#define SYSCALL_DUP2            12
#define SYSCALL_PIPE            13
#define SYSCALL_EXECV           14
#define SYSCALL_OPEN            15
#define SYSCALL_UNLINK          16
#define SYSCALL_READDISKFILE    17
#define SYSCALL_SYNC            18
#define SYSCALL_LSEEK           19
#define SYSCALL_FTRUNCATE       20
#define SYSCALL_RENAME          21
#define SYSCALL_GETTID          22
#define SYSCALL_CLONE           23
#define SYSCALL_TEXIT           24

// Add new system calls here.
// Your numbers should be >=128 to avoid conflicts.


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
#define E_NAMETOOLONG   -36        // File name too long
#define E_NFILE         -23        // File table overflow
#define E_NOENT         -2         // No such file or directory
#define E_NOEXEC        -8         // Exec format error
#define E_NOMEM         -12        // Out of memory
#define E_NOSPC         -28        // No space left on device
#define E_NOSYS         -38        // Invalid system call number
#define E_NXIO          -6         // No such device or address
#define E_PERM          -1         // Operation not permitted
#define E_PIPE          -32        // Broken pipe
#define E_SPIPE         -29        // Illegal seek
#define E_SRCH          -3         // No such process
#define E_TXTBSY        -26        // Text file busy
#define E_2BIG          -7         // Argument list too long

#define E_MINERROR      -100

inline bool is_error(uintptr_t r) {
    return r >= static_cast<uintptr_t>(E_MINERROR);
}


// System call constants

// sys_kdisplay() types
#define KDISPLAY_CONSOLE        0
#define KDISPLAY_MEMVIEWER      1

// sys_waitpid() options
#define W_NOHANG                1

// sys_open() flags
#define OF_READ                 1
#define OF_WRITE                2
#define OF_CREATE               4
#define OF_CREAT                OF_CREATE     // ¯\_(ツ)_/¯
#define OF_TRUNC                8

// sys_lseek() origins
#define LSEEK_SET               0    // Seek from beginning of file
#define LSEEK_CUR               1    // Seek from current position
#define LSEEK_END               2    // Seek from end of file
#define LSEEK_SIZE              3    // Do not seek; return file size


// CGA console printing

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


// console_puts(cursor, color, s, len)
//    Write a string to the CGA console. Writes exactly `len` characters.
//
//    The `cursor` argument is a cursor position, such as `CPOS(r, c)`
//    for row number `r` and column number `c`. The `color` argument
//    is the initial color used to print; 0x0700 is a good choice
//    (grey on black).
//
//    Returns the final position of the cursor.
int console_puts(int cpos, int color, const char* s, size_t len);


// console_printf(cursor, color, format, ...)
//    Format and print a message to the CGA console.
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
//    being printed; it takes an integer from the parameter list.
//
//    Returns the final position of the cursor.
int console_printf(int cpos, int color, const char* format, ...);

// console_vprintf(cpos, color, format val)
//    The vprintf version of console_printf.
int console_vprintf(int cpos, int color, const char* format, va_list val);

// Helper versions that default to printing white-on-black at the cursor.
void console_printf(int color, const char* format, ...);
void console_printf(const char* format, ...);


// Generic print library

struct printer {
    virtual void putc(unsigned char c, int color) = 0;
    void vprintf(int color, const char* format, va_list val);
};


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


// Assertions

// assert(x)
//    If `x == 0`, print a message and fail.
#define assert(x, ...)       do {                                       \
        if (!(x)) {                                                     \
            assert_fail(__FILE__, __LINE__, #x, ## __VA_ARGS__);        \
        }                                                               \
    } while (false)
__attribute__((noinline, noreturn, cold))
void assert_fail(const char* file, int line, const char* msg,
                 const char* description = nullptr);


// assert_[eq, ne, lt, le, gt, ge](x, y)
//    Like `assert(x OP y)`, but also prints the values of `x` and `y` on
//    failure.
#define assert_op(x, op, y) do {                                        \
        auto __x = (x); auto __y = (y);                                 \
        using __t = typename std::common_type<typeof(__x), typeof(__y)>::type; \
        if (!(__x op __y)) {                                            \
            assert_op_fail<__t>(__FILE__, __LINE__, #x " " #op " " #y,  \
                                __x, #op, __y);                         \
        } } while (0)
#define assert_eq(x, y) assert_op(x, ==, y)
#define assert_ne(x, y) assert_op(x, !=, y)
#define assert_lt(x, y) assert_op(x, <, y)
#define assert_le(x, y) assert_op(x, <=, y)
#define assert_gt(x, y) assert_op(x, >, y)
#define assert_ge(x, y) assert_op(x, >=, y)

template <typename T>
void __attribute__((noinline, noreturn, cold))
assert_op_fail(const char* file, int line, const char* msg,
               const T& x, const char* op, const T& y) {
    char fmt[48];
    snprintf(fmt, sizeof(fmt), "%%s:%%d: expected %%%s %s %%%s\n",
             printfmt<T>::spec, op, printfmt<T>::spec);
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

#if CHICKADEE_KERNEL
void __attribute__((noinline, noreturn, cold))
panic_at(uintptr_t rsp, uintptr_t rbp, uintptr_t rip,
         const char* format, ...);
#endif


// bitset_view inline functions

inline constexpr bitset_view::bit::bit(uint64_t& v, uint64_t m)
    : v_(v), m_(m) {
}
inline constexpr bitset_view::bit::operator bool() const {
    return (v_ & m_) != 0;
}
inline auto bitset_view::bit::operator=(bool x) -> bit& {
    if (x) {
        v_ |= m_;
    } else {
        v_ &= ~m_;
    }
    return *this;
}
inline auto bitset_view::bit::operator|=(bool x) -> bit& {
    if (x) {
        v_ |= m_;
    }
    return *this;
}
inline auto bitset_view::bit::operator&=(bool x) -> bit& {
    if (!x) {
        v_ &= ~m_;
    }
    return *this;
}
inline auto bitset_view::bit::operator^=(bool x) -> bit& {
    if (x) {
        v_ ^= m_;
    }
    return *this;
}
inline constexpr size_t bitset_view::size() const {
    return n_;
}
inline bool bitset_view::operator[](size_t i) const {
    assert(i < n_);
    return (v_[i / 64] & (1UL << (i % 64))) != 0;
}
inline auto bitset_view::operator[](size_t i) -> bit {
    assert(i < n_);
    return bit(v_[i / 64], 1UL << (i % 64));
}
inline size_t bitset_view::find_lsb(size_t i, size_t n) const {
    unsigned off = i % 64;
    uint64_t mask = ~(off ? (uint64_t(1) << off) - 1 : uint64_t(0));
    n = min(n_ - i, n) + i;
    i -= off;
    unsigned b = 0;
    while (i < n && !(b = lsb(v_[i / 64] & mask))) {
        i += 64;
        mask = -1;
    }
    return b ? min(n, i + b - 1) : n;
}
inline size_t bitset_view::find_lsz(size_t i, size_t n) const {
    unsigned off = i % 64;
    uint64_t mask = ~(off ? (uint64_t(1) << off) - 1 : uint64_t(0));
    n = min(n_ - i, n) + i;
    i -= off;
    unsigned b = 0;
    while (i < n && !(b = lsb(~v_[i / 64] & mask))) {
        i += 64;
        mask = -1;
    }
    return b ? min(n, i + b - 1) : n;
}

#endif /* !CHICKADEE_LIB_HH */
