#include "lib.hh"
#include "x86-64.h"
#if CHICKADEE_KERNEL
#include "k-devices.hh" /* for consolestate */
#endif

// lib.cc
//
//    Functions useful in both kernel and applications.


extern "C" {

// memcpy, memmove, memset, memcmp, memchr, strlen, strnlen, strcpy, strcmp,
// strncmp, strchr, strtoul, strtol
//    We must provide our own implementations.

void* memcpy(void* dst, const void* src, size_t n) {
    const char* s = (const char*) src;
    for (char* d = (char*) dst; n > 0; --n, ++s, ++d) {
        *d = *s;
    }
    return dst;
}

void* memmove(void* dst, const void* src, size_t n) {
    const char* s = (const char*) src;
    char* d = (char*) dst;
    if (s < d && s + n > d) {
        s += n, d += n;
        while (n-- > 0) {
            *--d = *--s;
        }
    } else {
        while (n-- > 0) {
            *d++ = *s++;
        }
    }
    return dst;
}

void* memset(void* v, int c, size_t n) {
    for (char* p = (char*) v; n > 0; ++p, --n) {
        *p = c;
    }
    return v;
}

int memcmp(const void* a, const void* b, size_t n) {
    const uint8_t* sa = reinterpret_cast<const uint8_t*>(a);
    const uint8_t* sb = reinterpret_cast<const uint8_t*>(b);
    for (; n > 0; ++sa, ++sb, --n) {
        if (*sa != *sb) {
            return (*sa > *sb) - (*sa < *sb);
        }
    }
    return 0;
}

void* memchr(const void* s, int c, size_t n) {
    const unsigned char* ss;
    for (ss = (const unsigned char*) s; n != 0; ++ss, --n) {
        if (*ss == (unsigned char) c) {
            return (void*) ss;
        }
    }
    return nullptr;
}

size_t strlen(const char* s) {
    size_t n;
    for (n = 0; *s != '\0'; ++s) {
        ++n;
    }
    return n;
}

size_t strnlen(const char* s, size_t maxlen) {
    size_t n;
    for (n = 0; n != maxlen && *s != '\0'; ++s) {
        ++n;
    }
    return n;
}

char* strcpy(char* dst, const char* src) {
    char* d = dst;
    do {
        *d++ = *src++;
    } while (d[-1]);
    return dst;
}

char* strncpy(char* dst, const char* src, size_t maxlen) {
    char* d = dst;
    while (maxlen > 0 && *src != '\0') {
        *d++ = *src++;
        --maxlen;
    }
    while (maxlen > 0) {
        *d++ = '\0';
        --maxlen;
    }
    return dst;
}

int strcmp(const char* a, const char* b) {
    while (true) {
        unsigned char ac = *a, bc = *b;
        if (ac == 0 || bc == 0 || ac != bc) {
            return (ac > bc) - (ac < bc);
        }
        ++a, ++b;
    }
}

int strncmp(const char* a, const char* b, size_t n) {
    while (true) {
        unsigned char ac = n ? *a : 0, bc = n ? *b : 0;
        if (ac == 0 || bc == 0 || ac != bc) {
            return (ac > bc) - (ac < bc);
        }
        ++a, ++b, --n;
    }
}

int strcasecmp(const char* a, const char* b) {
    while (true) {
        unsigned char ac = tolower((unsigned char) *a);
        unsigned char bc = tolower((unsigned char) *b);
        if (ac == 0 || bc == 0 || ac != bc) {
            return (ac > bc) - (ac < bc);
        }
        ++a, ++b;
    }
}

int strncasecmp(const char* a, const char* b, size_t n) {
    while (true) {
        unsigned char ac = n ? tolower((unsigned char) *a) : 0;
        unsigned char bc = n ? tolower((unsigned char) *b) : 0;
        if (ac == 0 || bc == 0 || ac != bc) {
            return (ac > bc) - (ac < bc);
        }
        ++a, ++b, --n;
    }
}

char* strchr(const char* s, int c) {
    while (*s && *s != (char) c) {
        ++s;
    }
    if (*s == (char) c) {
        return (char*) s;
    } else {
        return nullptr;
    }
}

unsigned long strtoul(const char* s, char** endptr, int base) {
    while (isspace(*s)) {
        ++s;
    }
    bool negative = *s == '-';
    s += negative || *s == '+';
    if (base == 0) {
        if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
            base = 16;
            s += 2;
        } else if (s[0] == '0') {
            base = 8;
        } else {
            base = 10;
        }
    } else if (base == 16 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        s += 2;
    }
    unsigned long x = 0;
    bool overflow = false;
    while (true) {
        unsigned digit;
        if (*s >= '0' && *s < '0' + base) {
            digit = *s - '0';
        } else if (base > 10 && *s >= 'a' && *s < 'a' + base - 10) {
            digit = *s - 'a' + 10;
        } else if (base > 10 && *s >= 'A' && *s < 'A' + base - 10) {
            digit = *s - 'A' + 10;
        } else {
            break;
        }
        if (x > (-1UL - digit) / base) {
            overflow = true;
        } else {
            x = x * base + digit;
        }
        ++s;
    }
    if (endptr) {
        *endptr = const_cast<char*>(s);
    }
    if (overflow) {
        x = -1UL;
    }
    if (negative) {
        x = -x;
    }
    return x;
}

long strtol(const char* s, char** endptr, int base) {
    while (isspace(*s)) {
        ++s;
    }
    bool negative = *s == '-';
    if (negative) {
        ++s;
    }
    unsigned long u = strtoul(s, endptr, base);
    unsigned long bound = (1UL << (8 * sizeof(unsigned long) - 1)) - !negative;
    if (u > bound) {
        u = bound;
    }
    if (negative) {
        u = -u;
    }
    return long(u);
}

} // extern "C"


// rand, srand

static int rand_seed_set;
static unsigned long rand_seed;

int rand() {
    if (!rand_seed_set) {
        srand(819234718U);
    }
    rand_seed = rand_seed * 6364136223846793005UL + 1;
    return (rand_seed >> 32) & RAND_MAX;
}

void srand(unsigned seed) {
    rand_seed = ((unsigned long) seed << 32) | seed;
    rand_seed_set = 1;
}

// rand(min, max)
//    Return a pseudorandom number roughly evenly distributed between
//    `min` and `max`, inclusive. Requires `min <= max` and
//    `max - min <= RAND_MAX`.
int rand(int min, int max) {
    assert(min <= max);
    assert(max - min <= RAND_MAX);

    unsigned long r = rand();
    return min + (r * (max - min + 1)) / ((unsigned long) RAND_MAX + 1);
}


// declare printfmt specializations (g++-6 requires)

constexpr char printfmt<bool>::spec[];
constexpr char printfmt<char>::spec[];
constexpr char printfmt<signed char>::spec[];
constexpr char printfmt<unsigned char>::spec[];
constexpr char printfmt<short>::spec[];
constexpr char printfmt<unsigned short>::spec[];
constexpr char printfmt<int>::spec[];
constexpr char printfmt<unsigned>::spec[];
constexpr char printfmt<long>::spec[];
constexpr char printfmt<unsigned long>::spec[];


// printer::vprintf
//    Format and print a string into a generic printer object.

static char* fill_numbuf(char* numbuf_end, unsigned long val, int base) {
    static const char upper_digits[] = "0123456789ABCDEF";
    static const char lower_digits[] = "0123456789abcdef";

    const char* digits = upper_digits;
    if (base < 0) {
        digits = lower_digits;
        base = -base;
    }

    *--numbuf_end = '\0';
    do {
        *--numbuf_end = digits[val % base];
        val /= base;
    } while (val != 0);
    return numbuf_end;
}

#define FLAG_ALT                (1<<0)
#define FLAG_ZERO               (1<<1)
#define FLAG_LEFTJUSTIFY        (1<<2)
#define FLAG_SPACEPOSITIVE      (1<<3)
#define FLAG_PLUSPOSITIVE       (1<<4)
static const char flag_chars[] = "#0- +";

#define FLAG_NUMERIC            (1<<5)
#define FLAG_SIGNED             (1<<6)
#define FLAG_NEGATIVE           (1<<7)
#define FLAG_ALT2               (1<<8)

void printer::vprintf(int color, const char* format, va_list val) {
#define NUMBUFSIZ 24
    char numbuf[NUMBUFSIZ];

    for (; *format; ++format) {
        if (*format != '%') {
            putc(*format, color);
            continue;
        }

        // process flags
        int flags = 0;
        for (++format; *format; ++format) {
            const char* flagc = strchr(flag_chars, *format);
            if (flagc) {
                flags |= 1 << (flagc - flag_chars);
            } else {
                break;
            }
        }

        // process width
        int width = -1;
        if (*format >= '1' && *format <= '9') {
            for (width = 0; *format >= '0' && *format <= '9'; ) {
                width = 10 * width + *format++ - '0';
            }
        } else if (*format == '*') {
            width = va_arg(val, int);
            ++format;
        }

        // process precision
        int precision = -1;
        if (*format == '.') {
            ++format;
            if (*format >= '0' && *format <= '9') {
                for (precision = 0; *format >= '0' && *format <= '9'; ) {
                    precision = 10 * precision + *format++ - '0';
                }
            } else if (*format == '*') {
                precision = va_arg(val, int);
                ++format;
            }
            if (precision < 0) {
                precision = 0;
            }
        }

        // process length
        int length = 0;
        switch (*format) {
        case 'l':
        case 't': // ptrdiff_t
        case 'z': // size_t, ssize_t
            length = 1;
            ++format;
            break;
        case 'h':
            ++format;
            break;
        }

        // process main conversion character
        int base = 10;
        unsigned long num = 0;
        const char* data = "";

        switch (*format) {
        case 'd':
        case 'i': {
            long x = length ? va_arg(val, long) : va_arg(val, int);
            int negative = x < 0 ? FLAG_NEGATIVE : 0;
            num = negative ? -x : x;
            flags |= FLAG_NUMERIC | FLAG_SIGNED | negative;
            break;
        }
        case 'u':
        format_unsigned:
            num = length ? va_arg(val, unsigned long) : va_arg(val, unsigned);
            flags |= FLAG_NUMERIC;
            break;
        case 'x':
            base = -16;
            goto format_unsigned;
        case 'X':
            base = 16;
            goto format_unsigned;
        case 'p':
            num = (uintptr_t) va_arg(val, void*);
            base = -16;
            flags |= FLAG_ALT | FLAG_ALT2 | FLAG_NUMERIC;
            break;
        case 's':
            data = va_arg(val, char*);
            break;
        case 'C':
            color = va_arg(val, int);
            continue;
        case 'c':
            data = numbuf;
            numbuf[0] = va_arg(val, int);
            numbuf[1] = '\0';
            break;
        default:
            data = numbuf;
            numbuf[0] = (*format ? *format : '%');
            numbuf[1] = '\0';
            if (!*format) {
                format--;
            }
            break;
        }

        if (flags & FLAG_NUMERIC) {
            data = fill_numbuf(numbuf + NUMBUFSIZ, num, base);
        }

        const char* prefix = "";
        if ((flags & FLAG_NUMERIC) && (flags & FLAG_SIGNED)) {
            if (flags & FLAG_NEGATIVE) {
                prefix = "-";
            } else if (flags & FLAG_PLUSPOSITIVE) {
                prefix = "+";
            } else if (flags & FLAG_SPACEPOSITIVE) {
                prefix = " ";
            }
        } else if ((flags & FLAG_NUMERIC) && (flags & FLAG_ALT)
                   && (base == 16 || base == -16)
                   && (num || (flags & FLAG_ALT2))) {
            prefix = (base == -16 ? "0x" : "0X");
        }

        int datalen;
        if (precision >= 0 && !(flags & FLAG_NUMERIC)) {
            datalen = strnlen(data, precision);
        } else {
            datalen = strlen(data);
        }

        int zeros;
        if ((flags & FLAG_NUMERIC)
            && precision >= 0) {
            zeros = precision > datalen ? precision - datalen : 0;
        } else if ((flags & FLAG_NUMERIC)
                   && (flags & FLAG_ZERO)
                   && !(flags & FLAG_LEFTJUSTIFY)
                   && datalen + (int) strlen(prefix) < width) {
            zeros = width - datalen - strlen(prefix);
        } else {
            zeros = 0;
        }

        width -= datalen + zeros + strlen(prefix);
        for (; !(flags & FLAG_LEFTJUSTIFY) && width > 0; --width) {
            putc(' ', color);
        }
        for (; *prefix; ++prefix) {
            putc(*prefix, color);
        }
        for (; zeros > 0; --zeros) {
            putc('0', color);
        }
        for (; datalen > 0; ++data, --datalen) {
            putc(*data, color);
        }
        for (; width > 0; --width) {
            putc(' ', color);
        }
    }
}


// vsnprintf, snprintf
//    Format a string into a sized buffer.

struct string_printer : public printer {
    char* s_;
    char* end_;
    size_t n_;
    string_printer(char* s, size_t size)
        : s_(s), end_(s + size), n_(0) {
    }
    void putc(unsigned char c, int) override {
        if (s_ < end_) {
            *s_++ = c;
        }
        ++n_;
    }
};

ssize_t vsnprintf(char* s, size_t size, const char* format, va_list val) {
    string_printer sp(s, size);
    sp.vprintf(0, format, val);
    if (size && sp.s_ < sp.end_) {
        *sp.s_ = 0;
    } else if (size) {
        sp.end_[-1] = 0;
    }
    return sp.n_;
}

ssize_t snprintf(char* s, size_t size, const char* format, ...) {
    va_list val;
    va_start(val, format);
    int n = vsnprintf(s, size, format, val);
    va_end(val);
    return n;
}


// console_clear
//    Erases the console and moves the cursor to the upper left (CPOS(0, 0)).

void console_clear() {
    for (int i = 0; i < CONSOLE_ROWS * CONSOLE_COLUMNS; ++i) {
        console[i] = ' ' | 0x0700;
    }
    cursorpos = 0;
#if CHICKADEE_KERNEL
    consolestate::get().cursor();
#endif
}


// console_puts
//    Put a string to the console, starting at the given cursor position.

struct console_printer : public printer {
    uint16_t* cell_;
    bool scroll_;
    console_printer(int cpos, bool scroll);
    inline void putc(unsigned char c, int color) override;
    void scroll();
    void move_cursor();
};

__noinline
console_printer::console_printer(int cpos, bool scroll)
    : cell_(console), scroll_(scroll) {
    if (cpos < 0) {
        cell_ += cursorpos;
    } else if (cpos <= CONSOLE_ROWS * CONSOLE_COLUMNS) {
        cell_ += cpos;
    }
}

__noinline
void console_printer::scroll() {
    assert(cell_ >= console + CONSOLE_ROWS * CONSOLE_COLUMNS);
    if (scroll_) {
        memmove(console, console + CONSOLE_COLUMNS,
                (CONSOLE_ROWS - 1) * CONSOLE_COLUMNS * sizeof(*console));
        memset(console + (CONSOLE_ROWS - 1) * CONSOLE_COLUMNS,
               0, CONSOLE_COLUMNS * sizeof(*console));
        cell_ -= CONSOLE_COLUMNS;
    } else {
        cell_ = console;
    }
}

__noinline
void console_printer::move_cursor() {
    cursorpos = cell_ - console;
#if CHICKADEE_KERNEL
    consolestate::get().cursor();
#endif
}

inline void console_printer::putc(unsigned char c, int color) {
    while (cell_ >= console + CONSOLE_ROWS * CONSOLE_COLUMNS) {
        scroll();
    }
    if (c == '\n') {
        int pos = (cell_ - console) % 80;
        while (pos != 80) {
            *cell_++ = ' ' | color;
            ++pos;
        }
    } else {
        *cell_++ = c | color;
    }
}

__noinline
int console_puts(int cpos, int color, const char* s, size_t len) {
    console_printer cp(cpos, cpos < 0);
    while (len > 0) {
        cp.putc(*s, color);
        ++s;
        --len;
    }
    if (cpos < 0) {
        cp.move_cursor();
    }
    return cp.cell_ - console;
}


// console_vprintf, console_printf
//    Print a message onto the console, starting at the given cursor position.

__noinline
int console_vprintf(int cpos, int color, const char* format, va_list val) {
    console_printer cp(cpos, cpos < 0);
    cp.vprintf(color, format, val);
    if (cpos < 0) {
        cp.move_cursor();
    }
    return cp.cell_ - console;
}

__noinline
int console_printf(int cpos, int color, const char* format, ...) {
    va_list val;
    va_start(val, format);
    cpos = console_vprintf(cpos, color, format, val);
    va_end(val);
    return cpos;
}

__noinline
void console_printf(int color, const char* format, ...) {
    va_list val;
    va_start(val, format);
    console_vprintf(-1, color, format, val);
    va_end(val);
}

__noinline
void console_printf(const char* format, ...) {
    va_list val;
    va_start(val, format);
    console_vprintf(-1, 0x700, format, val);
    va_end(val);
}


// k-hardware.cc/u-lib.cc supply error_vprintf

__noinline
int error_printf(int cpos, int color, const char* format, ...) {
    va_list val;
    va_start(val, format);
    cpos = error_vprintf(cpos, color, format, val);
    va_end(val);
    return cpos;
}

__noinline
void error_printf(int color, const char* format, ...) {
    va_list val;
    va_start(val, format);
    error_vprintf(-1, color, format, val);
    va_end(val);
}

__noinline
void error_printf(const char* format, ...) {
    va_list val;
    va_start(val, format);
    error_vprintf(-1, COLOR_ERROR, format, val);
    va_end(val);
}


// assertion support

void assert_memeq_fail(const char* file, int line, const char* msg,
                       const char* x, const char* y, size_t sz) {
    size_t pos = 0;
    while (pos < sz && x[pos] == y[pos]) {
        ++pos;
    }
    size_t spos = pos <= 10 ? 0 : pos - 10;
    size_t epos = pos + 10 < sz ? pos + 10 : sz;
    const char* ellipsis1 = spos > 0 ? "..." : "";
    const char* ellipsis2 = epos < sz ? "..." : "";
    error_printf(CPOS(22, 0), COLOR_ERROR,
                 "%s:%d: \"%s%.*s%s\" != \"%s%.*s%s\" @%zu\n",
                 file, line,
                 ellipsis1, int(epos - spos), x + spos, ellipsis2,
                 ellipsis1, int(epos - spos), y + spos, ellipsis2, pos);
    assert_fail(file, line, msg);
}


// Some static tests of our arithmetic functions

static_assert(msb(0) == 0, "msb failure");
static_assert(msb(1) == 1, "msb failure");
static_assert(msb(2) == 2, "msb failure");
static_assert(msb(3) == 2, "msb failure");
static_assert(msb(0x1FABC) == 17, "msb failure");
static_assert(msb(0x1FFFF) == 17, "msb failure");

static_assert(round_down_pow2(0U) == 0U, "round_down_pow2 failure");
static_assert(round_down_pow2(1U) == 1U, "round_down_pow2 failure");
static_assert(round_down_pow2(2U) == 2U, "round_down_pow2 failure");
static_assert(round_down_pow2(3U) == 2U, "round_down_pow2 failure");
static_assert(round_down_pow2(0x1FABCU) == 0x10000U, "round_down_pow2 failure");

static_assert(round_up_pow2(0U) == 0U, "round_up_pow2 failure");
static_assert(round_up_pow2(1U) == 1U, "round_up_pow2 failure");
static_assert(round_up_pow2(2U) == 2U, "round_up_pow2 failure");
static_assert(round_up_pow2(3U) == 4U, "round_up_pow2 failure");
static_assert(round_up_pow2(0x1FABCU) == 0x20000U, "round_up_pow2 failure");
static_assert(round_up_pow2(0x1FFFFU) == 0x20000U, "round_up_pow2 failure");
