#ifndef CHICKADEE_K_DEVICES_HH
#define CHICKADEE_K_DEVICES_HH
#include "kernel.hh"

// console_show_cursor(cpos)
//    Move the console cursor to position `cpos`, which should be between 0
//    and 80 * 25.
void console_show_cursor(int cpos);


// keyboardstate: keyboard buffer and keyboard interrupts

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


// consolestate: lock for console access

struct consolestate {
    spinlock lock_;

    static consolestate& get() {
        return console;
    }

 private:
    static consolestate console;
    consolestate() = default;
};


// memfile: in-memory file system of contiguous files

struct memfile {
    static constexpr unsigned namesize = 64;
    char name_[namesize];                // name of file
    unsigned char* data_;                // file data (nullptr if empty)
    size_t len_;                         // length of file data
    size_t capacity_;                    // # bytes available in `data_`

    inline memfile();
    inline memfile(const char* name, unsigned char* first,
                   unsigned char* last);

    // return true iff this `memfile` is not being used
    inline bool empty() const;

    // set file length to `len`; return 0 or an error like `E_NOSPC` on failure
    int set_length(size_t len);

    // memfile::initfs[] is the init file system built in to the kernel.
    static constexpr unsigned initfs_size = 64;
    static memfile initfs[initfs_size];

    // look up the memfile named `name`, creating it if it does not exist
    // and `create == true`. Return an index into `initfs` or an error code.
    static int initfs_lookup(const char* name, bool create = false);
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
    return name_[0] == 0;
}


// memfile::loader: loads a `proc` from a `memfile`

struct memfile_loader : public proc_loader {
    memfile* memfile_;
    inline memfile_loader(memfile* mf, x86_64_pagetable* pt)
        : proc_loader(pt), memfile_(mf) {
    }
    inline memfile_loader(int mf_index, x86_64_pagetable* pt)
        : proc_loader(pt) {
        assert(mf_index >= 0 && unsigned(mf_index) < memfile::initfs_size);
        memfile_ = &memfile::initfs[mf_index];
    }
    ssize_t get_page(uint8_t** pg, size_t off) override;
    void put_page(uint8_t* pg) override;
};

#endif
