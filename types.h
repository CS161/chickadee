#ifndef CHICKADEE_TYPES_H
#define CHICKADEE_TYPES_H

// types.h
//
//    Functions, constants, and definitions useful in both the kernel
//    and applications.
//
//    Contents: (1) C library subset, (2) system call numbers, (3) console.

#if __cplusplus
#define NULL nullptr
#else
#define NULL ((void*) 0)
#endif

typedef __builtin_va_list va_list;
#define va_start(val, last) __builtin_va_start(val, last)
#define va_arg(val, type) __builtin_va_arg(val, type)
#define va_end(val) __builtin_va_end(val)

typedef signed char int8_t;
typedef unsigned char uint8_t;
typedef short int16_t;
typedef unsigned short uint16_t;
typedef int int32_t;
typedef unsigned int uint32_t;
typedef long int64_t;
typedef unsigned long uint64_t;

typedef long intptr_t;                // ints big enough to hold pointers
typedef unsigned long uintptr_t;

typedef unsigned long size_t;         // sizes and offsets
typedef long ssize_t;
typedef long off_t;

typedef int pid_t;                    // process IDs

#ifdef __cplusplus
#define NO_COPY_OR_ASSIGN(t) \
    t(const t&) = delete; t(t&&) = delete; \
    t& operator=(const t&) = delete; \
    t& operator=(t&&) = delete;
#endif

#define __section(x) __attribute__((section(x)))
#define __no_asan    __attribute__((no_sanitize_address))

#endif /* !CHICKADEE_TYPES_H */
