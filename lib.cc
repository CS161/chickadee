#include "lib.hh"
#include "x86-64.h"

// lib.c
//
//    Functions useful in both kernel and applications.


extern "C" {

// memcpy, memmove, memset, strcmp, strlen, strnlen
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

int strcmp(const char* a, const char* b) {
    while (*a && *b && *a == *b) {
        ++a, ++b;
    }
    return ((unsigned char) *a > (unsigned char) *b)
        - ((unsigned char) *a < (unsigned char) *b);
}

char* strchr(const char* s, int c) {
    while (*s && *s != (char) c) {
        ++s;
    }
    if (*s == (char) c) {
        return (char*) s;
    } else {
        return NULL;
    }
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
    rand_seed = seed;
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


// console_vprintf, console_printf
//    Print a message onto the console, starting at the given cursor position.

// snprintf, vsnprintf
//    Format a string into a buffer.

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

void printer_vprintf(printer* p, int color, const char* format, va_list val) {
#define NUMBUFSIZ 24
    char numbuf[NUMBUFSIZ];

    for (; *format; ++format) {
        if (*format != '%') {
            p->putc(p, *format, color);
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

        // process main conversion character
        int base = 10;
        unsigned long num = 0;
        int length = 0;
        const char* data = "";

    again:
        switch (*format) {
        case 'l':
        case 'z':
            length = 1;
            ++format;
            goto again;
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
        if ((flags & FLAG_NUMERIC) && precision >= 0) {
            zeros = precision > datalen ? precision - datalen : 0;
        } else if ((flags & FLAG_NUMERIC) && (flags & FLAG_ZERO)
                   && !(flags & FLAG_LEFTJUSTIFY)
                   && datalen + (int) strlen(prefix) < width) {
            zeros = width - datalen - strlen(prefix);
        } else {
            zeros = 0;
        }

        width -= datalen + zeros + strlen(prefix);
        for (; !(flags & FLAG_LEFTJUSTIFY) && width > 0; --width) {
            p->putc(p, ' ', color);
        }
        for (; *prefix; ++prefix) {
            p->putc(p, *prefix, color);
        }
        for (; zeros > 0; --zeros) {
            p->putc(p, '0', color);
        }
        for (; datalen > 0; ++data, --datalen) {
            p->putc(p, *data, color);
        }
        for (; width > 0; --width) {
            p->putc(p, ' ', color);
        }
    }
}


typedef struct console_printer {
    printer p;
    uint16_t* cursor;
    int scroll;
} console_printer;

static void console_putc(printer* p, unsigned char c, int color) {
    console_printer* cp = (console_printer*) p;
    while (cp->cursor >= console + CONSOLE_ROWS * CONSOLE_COLUMNS) {
        if (cp->scroll) {
            memmove(console, console + CONSOLE_COLUMNS,
                    (CONSOLE_ROWS - 1) * CONSOLE_COLUMNS * sizeof(*console));
            memset(console + (CONSOLE_ROWS - 1) * CONSOLE_COLUMNS,
                   0, CONSOLE_COLUMNS * sizeof(*console));
            cp->cursor -= CONSOLE_COLUMNS;
        } else {
            cp->cursor = console;
        }
    }
    if (c == '\n') {
        int pos = (cp->cursor - console) % 80;
        for (; pos != 80; pos++) {
            *cp->cursor++ = ' ' | color;
        }
    } else {
        *cp->cursor++ = c | color;
    }
}

int console_vprintf(int cpos, int color, const char* format, va_list val) {
    struct console_printer cp;
    cp.p.putc = console_putc;
    cp.cursor = console;
    cp.scroll = cpos < 0;
    if (cpos < 0) {
        cp.cursor += cursorpos;
    } else if (cpos <= CONSOLE_ROWS * CONSOLE_COLUMNS) {
        cp.cursor += cpos;
    }
    printer_vprintf(&cp.p, color, format, val);
    if (cpos < 0) {
        cursorpos = cp.cursor - console;
#if CHICKADEE_KERNEL
        extern void console_show_cursor(int);
        console_show_cursor(cp.cursor - console);
#endif
    }
    return cp.cursor - console;
}

int console_printf(int cpos, int color, const char* format, ...) {
    va_list val;
    va_start(val, format);
    cpos = console_vprintf(cpos, color, format, val);
    va_end(val);
    return cpos;
}

void console_printf(int color, const char* format, ...) {
    va_list val;
    va_start(val, format);
    console_vprintf(-1, color, format, val);
    va_end(val);
}

void console_printf(const char* format, ...) {
    va_list val;
    va_start(val, format);
    console_vprintf(-1, 0x700, format, val);
    va_end(val);
}


typedef struct string_printer {
    printer p;
    char* s;
    char* end;
} string_printer;

static void string_putc(printer* p, unsigned char c, int color) {
    string_printer* sp = (string_printer*) p;
    if (sp->s < sp->end) {
        *sp->s++ = c;
    }
    (void) color;
}

int vsnprintf(char* s, size_t size, const char* format, va_list val) {
    string_printer sp;
    sp.p.putc = string_putc;
    sp.s = s;
    if (size) {
        sp.end = s + size - 1;
        printer_vprintf(&sp.p, 0, format, val);
        *sp.s = 0;
    }
    return sp.s - s;
}

int snprintf(char* s, size_t size, const char* format, ...) {
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
}


// Some static tests of our arithmetic functions

static_assert(msb(0) == 0);
static_assert(msb(1) == 1);
static_assert(msb(2) == 2);
static_assert(msb(3) == 2);
static_assert(msb(0x1FABC) == 17);
static_assert(msb(0x1FFFF) == 17);

static_assert(rounddown_pow2(0U) == 0U);
static_assert(rounddown_pow2(1U) == 1U);
static_assert(rounddown_pow2(2U) == 2U);
static_assert(rounddown_pow2(3U) == 2U);
static_assert(rounddown_pow2(0x1FABCU) == 0x10000U);

static_assert(roundup_pow2(0U) == 0U);
static_assert(roundup_pow2(1U) == 1U);
static_assert(roundup_pow2(2U) == 2U);
static_assert(roundup_pow2(3U) == 4U);
static_assert(roundup_pow2(0x1FABCU) == 0x20000U);
static_assert(roundup_pow2(0x1FFFFU) == 0x20000U);
