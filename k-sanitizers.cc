#include "kernel.hh"

#define NUMBUFSZ 22

// UNDEFINED BEHAVIOR SANITIZERS

namespace {

struct source_location {
    const char* file;
    uint32_t line;
    uint32_t column;
    inline constexpr bool valid() const {
        return file != nullptr;
    }
};

struct type_descriptor {
    uint16_t kind;
    uint16_t info;
    char name[1];

    inline constexpr bool is_int() const {
        return kind == 0;
    }
    inline constexpr bool is_signed() const {
        return info & 1;
    }
    inline constexpr unsigned bit_width() const {
        return 1 << (info >> 1);
    }
    inline unsigned long value(unsigned long x) const {
        static_assert(sizeof(x) == 8, "unexpected sizeof(unsigned long)");
        if (info >= 14) {
            return ~0UL;
        } else if (info >= 12) {
            return x;
        } else {
            unsigned long mask = ~((1UL << bit_width()) - 1);
            if (is_signed() && (x & (mask >> 1))) {
                return x | mask;
            } else {
                return x & ~mask;
            }
        }
    }
    inline char* unparse_value(char* buf, size_t bufsz,
                                         unsigned long x) const {
        if (kind != 0 || info >= 14) {
            snprintf(buf, bufsz, "???");
        } else if (is_signed()) {
            snprintf(buf, bufsz, "%ld", value(x));
        } else {
            snprintf(buf, bufsz, "%lu", value(x));
        }
        return buf;
    }
};

struct type_mismatch_data {
    source_location location;
    const type_descriptor* type;
    unsigned long alignment;
    unsigned char type_check_kind;

    static const char* const type_check_kind_names[];
};

struct type_mismatch_data_v1 {
    source_location location;
    const type_descriptor* type;
    unsigned char log_alignment;
    unsigned char type_check_kind;
};

const char* const type_mismatch_data::type_check_kind_names[] = {
    "load of", "store to", "reference binding to", "member access within",
    "member call on", "constructor call on", "downcast of", "downcast of",
    "upcast of", "cast to virtual base of", "_Nonnull binding to"
};

struct type_data {
    source_location location;
    const type_descriptor* type;
};
using overflow_data = type_data;
using invalid_value_data = type_data;
using vla_bound_data = type_data;

struct out_of_bounds_data {
    source_location location;
    const type_descriptor* array_type;
    const type_descriptor* index_type;
};

struct shift_out_of_bounds_data {
    source_location location;
    const type_descriptor* lhs_type;
    const type_descriptor* rhs_type;
};

struct nonnull_arg_data {
    source_location location;
    source_location attr_location;
    int arg_index;
};

}

extern "C" {

static void handle_overflow(overflow_data* data, unsigned long lhs,
                            unsigned long rhs, char op) {
    char buf1[NUMBUFSZ], buf2[NUMBUFSZ];
    error_printf("!!! %s:%u: %s integer overflow\n"
                 "!!!  %s %c %s cannot be represented in type %s\n",
                 data->location.file, data->location.line,
                 data->type->is_signed() ? "signed" : "unsigned",
                 data->type->unparse_value(buf1, sizeof(buf1), lhs), op,
                 data->type->unparse_value(buf2, sizeof(buf2), rhs),
                 data->type->name);
}

void __ubsan_handle_add_overflow(overflow_data* data,
                                 unsigned long a, unsigned long b) {
    handle_overflow(data, a, b, '+');
}

void __ubsan_handle_sub_overflow(overflow_data* data,
                                 unsigned long a, unsigned long b) {
    handle_overflow(data, a, b, '-');
}

void __ubsan_handle_mul_overflow(overflow_data* data,
                                 unsigned long a, unsigned long b) {
    handle_overflow(data, a, b, '*');
}

void __ubsan_handle_negate_overflow(overflow_data* data,
                                    unsigned long a) {
    char buf[NUMBUFSZ];
    error_printf("!!! %s:%u: %s integer overflow\n"
                 "!!!   -(%s) cannot be represented in type %s\n",
                 data->location.file, data->location.line,
                 data->type->is_signed() ? "signed" : "unsigned",
                 data->type->unparse_value(buf, sizeof(buf), a),
                 data->type->name);
}

void __ubsan_handle_divrem_overflow(overflow_data* data,
                                    unsigned long a, unsigned long b) {
    char buf[NUMBUFSZ];
    if (data->type->is_signed() && long(data->type->value(b)) == -1L) {
        error_printf("!!! %s:%d: division of %s by -1 cannot be represented in type %s\n",
                     data->location.file, data->location.line,
                     data->type->unparse_value(buf, sizeof(buf), a),
                     data->type->name);
    } else {
        error_printf("!!! %s:%d: division by zero\n",
                     data->location.file, data->location.line);
    }
}

void __ubsan_handle_shift_out_of_bounds(shift_out_of_bounds_data* data,
                                        unsigned long a, unsigned long b) {
    char buf1[NUMBUFSZ], buf2[NUMBUFSZ];
    error_printf("!!! %s:%u: shift out of bounds\n",
                 data->location.file, data->location.line);
    if (data->rhs_type->is_signed()
        && long(data->rhs_type->value(b)) < 0) {
        error_printf("!!!   shift amount %s is negative\n",
                     data->rhs_type->unparse_value(buf2, sizeof(buf2), b));
    } else if (data->rhs_type->value(b) >= data->lhs_type->bit_width()) {
        error_printf("!!!   shift amount %s too large for type %s\n",
                     data->rhs_type->unparse_value(buf2, sizeof(buf2), b),
                     data->lhs_type->name);
    } else {
        error_printf("!!!   %s << %s cannot be represented in type %s\n",
                     data->lhs_type->unparse_value(buf1, sizeof(buf1), a),
                     data->rhs_type->unparse_value(buf2, sizeof(buf2), b),
                     data->lhs_type->name);
    }
}

static void handle_type_mismatch(const source_location& location,
                                 const type_descriptor* type,
                                 unsigned long alignment,
                                 unsigned char type_check_kind,
                                 unsigned long ptr) {
    if (!ptr) {
        error_printf("!!! %s:%d: %s null pointer of type %s\n",
                     location.file, location.line,
                     type_mismatch_data::type_check_kind_names[type_check_kind],
                     type->name);
    } else if (alignment && (ptr & (alignment - 1)) != 0) {
        error_printf("!!! %s:%d: %s misaligned address %p for type %s\n",
                     location.file, location.line,
                     type_mismatch_data::type_check_kind_names[type_check_kind],
                     ptr, type->name);
    } else {
        error_printf("!!! %s:%d: %s address %p\n"
                     "!!!   with insufficient space for an object with type %s\n",
                     location.file, location.line,
                     type_mismatch_data::type_check_kind_names[type_check_kind],
                     ptr, type->name);
    }
    log_backtrace("!!! ");
}

void __ubsan_handle_type_mismatch(type_mismatch_data* data,
                                  unsigned long ptr) {
    handle_type_mismatch(data->location, data->type, data->alignment,
                         data->type_check_kind, ptr);
}

void __ubsan_handle_type_mismatch_v1(type_mismatch_data_v1* data,
                                     unsigned long ptr) {
    handle_type_mismatch(data->location, data->type, 1UL << data->log_alignment,
                         data->type_check_kind, ptr);
}

void __ubsan_handle_out_of_bounds(out_of_bounds_data* data,
                                  unsigned long index) {
    char buf[NUMBUFSZ];
    error_printf("!!! %s:%d: index %s out of range for type %s\n",
                 data->location.file, data->location.line,
                 data->index_type->unparse_value(buf, sizeof(buf), index),
                 data->array_type->name);
}

void __ubsan_handle_builtin_unreachable(source_location* location) {
    error_printf("!!! %s:%d: execution reached a __builtin_unreachable() call\n",
                 location->file, location->line);
}

void __ubsan_handle_missing_return(source_location* location) {
    error_printf("!!! %s:%d: execution reached the end of a value-returning function\n"
                 "!!!   without returning a value\n",
                 location->file, location->line);
}

void __ubsan_handle_vla_bound_not_positive(vla_bound_data* data,
					   unsigned long bound) {
    char buf[NUMBUFSZ];
    error_printf("!!! %s:%d: variable length array bound evaluates to non-positive %s\n",
                 data->location.file, data->location.line,
                 data->type->unparse_value(buf, sizeof(buf), bound));
}

void __ubsan_handle_load_invalid_value(invalid_value_data* data,
                                       unsigned long val) {
    char buf[NUMBUFSZ];
    error_printf("!!! %s:%d: load value %s is not valid for type %s\n",
                 data->location.file, data->location.line,
                 data->type->unparse_value(buf, sizeof(buf), val),
                 data->type->name);
}

void __ubsan_handle_nonnull_arg(nonnull_arg_data* data) {
    error_printf("!!! %s:%d: null pointer passed as argument %d,\n"
                 "!!!   which is declared to never be null\n",
                 data->location.file, data->location.line,
                 data->arg_index);
    if (data->attr_location.valid()) {
        error_printf("!!! %s:%d: attribute or annotation here\n",
                     data->attr_location.file, data->attr_location.line);
    }
}

void __ubsan_handle_nonnull_return(source_location* location) {
    error_printf("!!! %s:%d: null pointer returned from function\n"
                 "!!!   which is declared to never be null\n",
                 location->file, location->line);
}

void __ubsan_handle_pointer_overflow(source_location* location,
                                     uintptr_t base, uintptr_t result) {
    if (((intptr_t) base >= 0) == ((intptr_t) result >= 0)) {
        error_printf("!!! %s:%d: %s of unsigned offset to %p overflowed to %p\n",
                     location->file, location->line,
                     base > result ? "addition" : "subtraction",
                     base, result);
    } else {
        error_printf("!!! %s:%d: pointer index expression with base %p overflowed to %p\n",
                     location->file, location->line, base, result);
    }
}

}


// ADDRESS SANITIZER

static volatile signed char* volatile asan_pagemap;
static signed char* asan_pagemap_storage;
static size_t asan_pagemap_sz;
static int asan_global_status;
static std::atomic<int> asan_enabled;

static void asan_access(unsigned long addr, size_t sz, bool write) {
    proc* p = current();
    int& status = p ? p->sanitizer_status_ : asan_global_status;
    volatile signed char* pagemap = asan_pagemap;

    if (addr < HIGHMEM_BASE || !pagemap || status) {
        return;
    }

    size_t lp, rp;
    if (addr >= KTEXT_BASE) {
        lp = (addr - KTEXT_BASE) / PAGESIZE;
        rp = (addr + sz - 1 - KTEXT_BASE) / PAGESIZE;
    } else {
        lp = (addr - HIGHMEM_BASE) / PAGESIZE;
        rp = (addr + sz - 1 - HIGHMEM_BASE) / PAGESIZE;
    }

    while (lp <= rp && lp < asan_pagemap_sz && pagemap[lp] >= 0) {
        ++lp;
    }

    if (lp <= rp && lp < PA_IOHIGHMIN / PAGESIZE && !status) {
        ++status;

        uintptr_t lpaddr = HIGHMEM_BASE + lp * PAGESIZE;
        if (addr >= KTEXT_BASE) {
            lpaddr += KTEXT_BASE - HIGHMEM_BASE;
        }
        uintptr_t xaddr = max(addr, lpaddr);
        auto type = lp < asan_pagemap_sz ? "poisoned" : "nonexistent";
        error_printf("!!! invalid %s of %s address %p\n",
                     write ? "write" : "read", type, xaddr);
        log_backtrace("!!! ");

        --status;
    }
}

extern "C" {
void __asan_load1_noabort(unsigned long addr) {
    asan_access(addr, 1, false);
}
void __asan_load2_noabort(unsigned long addr) {
    asan_access(addr, 2, false);
}
void __asan_load4_noabort(unsigned long addr) {
    asan_access(addr, 4, false);
}
void __asan_load8_noabort(unsigned long addr) {
    asan_access(addr, 8, false);
}
void __asan_loadN_noabort(unsigned long addr, size_t sz) {
    asan_access(addr, sz, false);
}

void __asan_store1_noabort(unsigned long addr) {
    asan_access(addr, 1, true);
}
void __asan_store2_noabort(unsigned long addr) {
    asan_access(addr, 2, true);
}
void __asan_store4_noabort(unsigned long addr) {
    asan_access(addr, 4, true);
}
void __asan_store8_noabort(unsigned long addr) {
    asan_access(addr, 8, true);
}
void __asan_storeN_noabort(unsigned long addr, size_t sz) {
    asan_access(addr, sz, true);
}

void __asan_handle_no_return() {
}
void __asan_before_dynamic_init(const char* module_name) {
}
void __asan_after_dynamic_init() {
}
}


// external sanitizer interface

void init_sanitizers() {
    assert(!asan_pagemap_sz);

    // reserve enough pagemap space to cover all allocatable
    // physical memory plus all kernel-accessible memory
    size_t top = MEMSIZE_PHYSICAL;
    for (auto& it : physical_ranges) {
        if (it.type() == mem_kernel || it.type() == mem_available) {
            top = round_up(it.last(), PAGESIZE);
        }
    }
    asan_pagemap_sz = round_up(top / PAGESIZE, PAGESIZE);

    // reserve the memory
    uintptr_t asan_pagemap_top_pa = 128 << 20; // 128MB
    uintptr_t asan_pagemap_pa = asan_pagemap_top_pa - asan_pagemap_sz;
    physical_ranges.set(asan_pagemap_pa, asan_pagemap_top_pa, mem_reserved);

    // initialize storage
    asan_pagemap_storage = pa2kptr<signed char*>(asan_pagemap_pa);
    signed char* s = const_cast<signed char*>(asan_pagemap_storage);
    memset(s, 0, MEMSIZE_PHYSICAL / PAGESIZE);
    memset(s + (MEMSIZE_PHYSICAL / PAGESIZE), 255,
           asan_pagemap_sz - (MEMSIZE_PHYSICAL / PAGESIZE));
    for (auto& it : physical_ranges) {
        if (it.type() == mem_kernel || it.type() == mem_available) {
            memset(s + it.first() / PAGESIZE, 0,
                   (round_up(it.last(), PAGESIZE) / PAGESIZE)
                   - it.first() / PAGESIZE);
        }
    }
}

void disable_asan() {
    if (--asan_enabled == 0) {
        asan_pagemap = nullptr;
    }
}

void enable_asan() {
    if (++asan_enabled == 1) {
        asan_pagemap = asan_pagemap_storage;
    }
}

void asan_mark_memory(unsigned long pa, size_t sz, bool poisoned) {
    size_t lp = pa / PAGESIZE;
    size_t rp = (pa + sz - 1) / PAGESIZE;
    while (lp <= rp && lp < asan_pagemap_sz) {
        asan_pagemap_storage[lp] = poisoned ? -1 : 0;
        ++lp;
    }
}
