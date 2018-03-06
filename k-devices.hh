#ifndef CHICKADEE_K_DEVICES_HH
#define CHICKADEE_K_DEVICES_HH
#include "kernel.hh"
#include "k-wait.hh"

// console_show_cursor(cpos)
//    Move the console cursor to position `cpos`, which should be between 0
//    and 80 * 25.
void console_show_cursor(int cpos);


#define KEY_UP          0xC0
#define KEY_RIGHT       0xC1
#define KEY_DOWN        0xC2
#define KEY_LEFT        0xC3
#define KEY_HOME        0xC4
#define KEY_END         0xC5
#define KEY_PAGEUP      0xC6
#define KEY_PAGEDOWN    0xC7
#define KEY_INSERT      0xC8
#define KEY_DELETE      0xC9

struct keyboardstate {
    spinlock lock_;
    char buf_[256];
    unsigned pos_;      // next position to read
    unsigned len_;      // number of characters in buffer
    unsigned eol_;      // position in buffer of most recent \n
    wait_queue wq_;     // reading queue
    enum { boot, input, fail } state_;

    static keyboardstate& get() {
        return kbd;
    }

    void check_invariants() {
        assert(pos_ < sizeof(buf_));
        assert(len_ <= sizeof(buf_));
        assert(eol_ <= len_);
    }

    // called from proc::exception(); read characters from device
    void handle_interrupt();

    // consume `n` characters from buffer (0 <= n <= len_)
    void consume(size_t n);

 private:
    static keyboardstate kbd;
    keyboardstate();

    void maybe_echo(int ch);
};


struct consolestate {
    spinlock lock_;

    static consolestate& get() {
        return console;
    }

 private:
    static consolestate console;
    consolestate() = default;
};


struct memfile {
    static constexpr unsigned namesize = 64;
    char name_[namesize];                // name of file
    unsigned char* data_;                // file data (nullptr if empty)
    size_t len_;                         // length of file data
    size_t capacity_;                    // # bytes available in `data_`

    inline memfile();
    inline memfile(const char* name, unsigned char* first,
                   unsigned char* last);
    inline bool empty() const;           // test if empty
    inline bool is_kernel_data() const;  // test if in kernel data segment


    // memfile::initfs[] is the init file system built in to the kernel.
    static constexpr unsigned initfs_size = 64;
    static memfile initfs[initfs_size];
    static inline memfile* initfs_lookup(const char* name);
    static memfile* initfs_lookup(const char* name, size_t namelen);
};


inline memfile::memfile()
    : name_(""), data_(nullptr), len_(0), capacity_(0) {
}
inline memfile::memfile(const char* name, unsigned char* first,
                        unsigned char* last)
    : data_(first) {
    size_t namelen = strlen(name);
    ssize_t datalen = reinterpret_cast<uintptr_t>(last)
        - reinterpret_cast<uintptr_t>(first);
    assert(namelen < namesize && datalen >= 0);
    strcpy(name_, name);
    len_ = capacity_ = datalen;
}
inline bool memfile::empty() const {
    return data_ == nullptr;
}
inline bool memfile::is_kernel_data() const {
    extern unsigned char _kernel_start[], _kernel_end[];
    uintptr_t data = reinterpret_cast<uintptr_t>(data_);
    return data >= reinterpret_cast<uintptr_t>(_kernel_start)
        && data < reinterpret_cast<uintptr_t>(_kernel_end);
}

inline memfile* memfile::initfs_lookup(const char* name) {
    return initfs_lookup(name, strlen(name));
}

#endif
