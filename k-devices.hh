#ifndef CHICKADEE_K_DEVICES_HH
#define CHICKADEE_K_DEVICES_HH
#include "kernel.hh"
#include "k-wait.hh"

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
    inline bool empty() const;           // test if empty
    inline bool is_kernel_data() const;  // test if in kernel data segment


    // memfile::initfs[] is the init file system built in to the kernel.
    static constexpr unsigned initfs_size = 64;
    static memfile initfs[initfs_size];
    static inline memfile* initfs_lookup(const char* name);
    static memfile* initfs_lookup(const char* name, size_t namelen);
};


// idestate: IDE (ATA/ATAPI) disk access

struct idestate {
    spinlock lock_;
    wait_queue wq_;
    int* status_;           // pointer to store status
    void* read_buf_;        // if read, destination for read data
    size_t read_nwords_;    // if read, # words to read
    // drive is available for new command iff `status_ == nullptr`

    // registers
    enum {
        reg_data = 0x1F0,
        reg_sector_count = 0x1F2,
        reg_sector_number = 0x1F3,
        reg_cylinder_low = 0x1F4,
        reg_cylinder_high = 0x1F5,
        reg_sdh = 0x1F6,
        reg_command = 0x1F7,
        reg_status = 0x1F7,
        reg_irq_enable = 0x3F6
    };

    // reg_sdh flags
    enum {
        sdh_drive0 = 0xE0, sdh_drive1 = 0xF0
    };

    // reg_status flags
    enum {
        status_busy = 0x80, status_ready = 0x40,
        status_disk_fault = 0x20, status_error = 0x01
    };

    // commands
    enum {
        cmd_read = 0x20, cmd_read_multiple = 0xC4,
        cmd_write = 0x30, cmd_write_multiple = 0xC5
    };

    static constexpr size_t sectorsize = 512;

    static idestate& get() {
        return ide;
    }

    // read `nsectors` sectors into `buf`, starting at `sector`; blocks
    int read(void* buf, size_t sector, size_t nsectors);
    // write `nsectors` sectors from `buf`, starting at `sector`; blocks
    int write(const void* buf, size_t sector, size_t nsectors);
    // interrupt handler
    void handle_interrupt();

    bool await_disk();
    inline void sector_command(int command, size_t sector, size_t nsectors);

 private:
    static idestate ide;
    idestate();
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
