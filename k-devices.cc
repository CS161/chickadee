#include "k-devices.hh"
#include "k-apic.hh"

// k-devices.cc
//
//    Functions for interacting with x86 devices.


// keyboard_readc
//    Read a character from the keyboard. Returns -1 if there is no
//    character to read, and 0 if no real key press was
//    registered but you should call keyboard_readc() again (e.g. the
//    user pressed a SHIFT key). Otherwise returns either an ASCII
//    character code or one of the special characters listed in
//    kernel.h.

// Unfortunately mapping PC key codes to ASCII takes a lot of work.

#define MOD_SHIFT       (1 << 0)
#define MOD_CONTROL     (1 << 1)
#define MOD_CAPSLOCK    (1 << 3)

#define KEY_SHIFT       0xFA
#define KEY_CONTROL     0xFB
#define KEY_ALT         0xFC
#define KEY_CAPSLOCK    0xFD
#define KEY_NUMLOCK     0xFE
#define KEY_SCROLLLOCK  0xFF

#define CKEY(cn)        (0x80 + cn)

static const uint8_t keymap[256] = {
    /*0x00*/ 0, 033, CKEY(0), CKEY(1), CKEY(2), CKEY(3), CKEY(4), CKEY(5),
        CKEY(6), CKEY(7), CKEY(8), CKEY(9), CKEY(10), CKEY(11), '\b', '\t',
    /*0x10*/ 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i',
        'o', 'p', CKEY(12), CKEY(13), CKEY(14), KEY_CONTROL, 'a', 's',
    /*0x20*/ 'd', 'f', 'g', 'h', 'j', 'k', 'l', CKEY(15),
        CKEY(16), CKEY(17), KEY_SHIFT, CKEY(18), 'z', 'x', 'c', 'v',
    /*0x30*/ 'b', 'n', 'm', CKEY(19), CKEY(20), CKEY(21), KEY_SHIFT, '*',
        KEY_ALT, ' ', KEY_CAPSLOCK, 0, 0, 0, 0, 0,
    /*0x40*/ 0, 0, 0, 0, 0, KEY_NUMLOCK, KEY_SCROLLLOCK, '7',
        '8', '9', '-', '4', '5', '6', '+', '1',
    /*0x50*/ '2', '3', '0', '.', 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0,
    /*0x60*/ 0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0,
    /*0x70*/ 0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0,
    /*0x80*/ 0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0,
    /*0x90*/ 0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, CKEY(14), KEY_CONTROL, 0, 0,
    /*0xA0*/ 0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0,
    /*0xB0*/ 0, 0, 0, 0, 0, '/', 0, 0,  KEY_ALT, 0, 0, 0, 0, 0, 0, 0,
    /*0xC0*/ 0, 0, 0, 0, 0, 0, 0, KEY_HOME,
        KEY_UP, KEY_PAGEUP, 0, KEY_LEFT, 0, KEY_RIGHT, 0, KEY_END,
    /*0xD0*/ KEY_DOWN, KEY_PAGEDOWN, KEY_INSERT, KEY_DELETE, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0,
    /*0xE0*/ 0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0,
    /*0xF0*/ 0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0
};

static const struct keyboard_key {
    uint8_t map[4];
} complex_keymap[] = {
    /*CKEY(0)*/ {{'1', '!', 0, 0}},      /*CKEY(1)*/ {{'2', '@', 0, 0}},
    /*CKEY(2)*/ {{'3', '#', 0, 0}},      /*CKEY(3)*/ {{'4', '$', 0, 0}},
    /*CKEY(4)*/ {{'5', '%', 0, 0}},      /*CKEY(5)*/ {{'6', '^', 0, 0x1E}},
    /*CKEY(6)*/ {{'7', '&', 0, 0}},      /*CKEY(7)*/ {{'8', '*', 0, 0}},
    /*CKEY(8)*/ {{'9', '(', 0, 0}},      /*CKEY(9)*/ {{'0', ')', 0, 0}},
    /*CKEY(10)*/ {{'-', '_', 0, 0x1F}},  /*CKEY(11)*/ {{'=', '+', 0, 0}},
    /*CKEY(12)*/ {{'[', '{', 0x1B, 0}},  /*CKEY(13)*/ {{']', '}', 0x1D, 0}},
    /*CKEY(14)*/ {{'\n', '\n', '\r', '\r'}},
    /*CKEY(15)*/ {{';', ':', 0, 0}},
    /*CKEY(16)*/ {{'\'', '"', 0, 0}},    /*CKEY(17)*/ {{'`', '~', 0, 0}},
    /*CKEY(18)*/ {{'\\', '|', 0x1C, 0}}, /*CKEY(19)*/ {{',', '<', 0, 0}},
    /*CKEY(20)*/ {{'.', '>', 0, 0}},     /*CKEY(21)*/ {{'/', '?', 0, 0}}
};

int keyboard_readc() {
    static uint8_t modifiers;
    static uint8_t last_escape;

    if ((inb(KEYBOARD_STATUSREG) & KEYBOARD_STATUS_READY) == 0) {
        return -1;
    }

    uint8_t data = inb(KEYBOARD_DATAREG);
    uint8_t escape = last_escape;
    last_escape = 0;

    if (data == 0xE0) {         // mode shift
        last_escape = 0x80;
        return 0;
    } else if (data & 0x80) {   // key release: matters only for modifier keys
        int ch = keymap[(data & 0x7F) | escape];
        if (ch >= KEY_SHIFT && ch < KEY_CAPSLOCK) {
            modifiers &= ~(1 << (ch - KEY_SHIFT));
        }
        return 0;
    }

    int ch = (unsigned char) keymap[data | escape];

    if (ch >= 'a' && ch <= 'z') {
        if (modifiers & MOD_CONTROL) {
            ch -= 0x60;
        } else if (!(modifiers & MOD_SHIFT) != !(modifiers & MOD_CAPSLOCK)) {
            ch -= 0x20;
        }
    } else if (ch >= KEY_CAPSLOCK) {
        modifiers ^= 1 << (ch - KEY_SHIFT);
        ch = 0;
    } else if (ch >= KEY_SHIFT) {
        modifiers |= 1 << (ch - KEY_SHIFT);
        ch = 0;
    } else if (ch >= CKEY(0) && ch <= CKEY(21)) {
        ch = complex_keymap[ch - CKEY(0)].map[modifiers & 3];
    } else if (ch < 0x80 && (modifiers & MOD_CONTROL)) {
        ch = 0;
    }

    return ch;
}


// the global `keyboardstate` singleton
keyboardstate keyboardstate::kbd;

void keyboardstate::handle_interrupt() {
    auto irqs = lock_.lock();

    int ch;
    while ((ch = keyboard_readc()) >= 0) {
        bool want_eol = false;
        switch (ch) {
        case 0: // try again
            break;

        case 0x08: // Ctrl-H
            if (eol_ < len_) {
                --len_;
                maybe_echo(ch);
            }
            break;

        case 0x11: // Ctrl-Q
            poweroff();
            break;

        case 0x03: // Ctrl-C
        case 'q':
            if (state_ != input) {
                poweroff();
            }
            goto normal_char;

        case '\r':
            ch = '\n';
            want_eol = true;
            goto normal_char;

        case 0x04: // Ctrl-D
        case '\n':
            want_eol = true;
            goto normal_char;

        default:
        normal_char:
            if (len_ < sizeof(buf_)) {
                unsigned slot = (pos_ + len_) % sizeof(buf_);
                buf_[slot] = ch;
                ++len_;
                if (want_eol) {
                    eol_ = len_;
                }
                maybe_echo(ch);
            }
            break;
        }
    }

    lock_.unlock(irqs);
    lapicstate::get().ack();
}

void keyboardstate::maybe_echo(int ch) {
    if (state_ == input) {
        consolestate::get().lock_.lock_noirq();
        if (ch == 0x08) {
            if (cursorpos > 0) {
                --cursorpos;
                console_printf(" ");
                --cursorpos;
            }
        } else if (ch != 0x04) {
            console_printf(0x0300, "%c", ch);
        }
        consolestate::get().lock_.unlock_noirq();
    }
}

void keyboardstate::consume(size_t n) {
    assert(n <= len_);
    pos_ = (pos_ + n) % sizeof(buf_);
    len_ -= n;
    eol_ -= n;
}


consolestate consolestate::console;

// consolestate::cursor()
//    Displays the console cursor at the current `cursorpos` position.

void consolestate::cursor() {
    int cpos = cursorpos;
    if (cpos >= 0
        && cpos < CONSOLE_ROWS * CONSOLE_COLUMNS
        && cursor_show_.load(std::memory_order_relaxed)
        && displayed_cpos_.load(std::memory_order_relaxed) != cpos) {
        spinlock_guard guard(cursor_lock_);
        outb(0x3D4, 15);
        outb(0x3D5, cpos % 256);
        outb(0x3D4, 14);
        outb(0x3D5, cpos / 256);
        displayed_cpos_ = cpos;
    }
}


// consolestate::cursor(show)
//    Enables or disables the cursor depending on `show`.

void consolestate::cursor(bool show) {
    {
        spinlock_guard guard(cursor_lock_);
        cursor_show_.store(show, std::memory_order_relaxed);
        outb(0x3D4, 10);
        outb(0x3D5, show ? 0x0E : 0x20);
        outb(0x3D4, 11);
        outb(0x3D5, show ? 0x0F : 0x20);
    }
    if (show) {
        cursor();
    }
}


// memfile functions

// memfile::initfs_lookup(name, namelen, create)
//    Search `memfile::initfs` for a file named `name`. Return the
//    index of that `memfile` if found; this will be >= 0 and <
//    `memfile::initfs_size`. If not found, and `create == true`,
//    attempt to create and initialize a new file and return its
//    index. Return an error code on failure.
int memfile::initfs_lookup(const char* name, bool create) {
    memfile* empty = nullptr;
    size_t namelen = min(strlen(name), size_t(namesize) - 1);

    // search for a file named `name`
    for (memfile* f = initfs; f != initfs + initfs_size; ++f) {
        if (!f->empty()
            && memcmp(f->name_, name, namelen) == 0
            && f->name_[namelen] == 0) {
            return f - initfs;
        } else if (f->empty()
                   && !empty) {
            empty = f;
        }
    }

    if (!create) {
        // file not found
        return E_NOENT;
    } else if (!empty) {
        // no space in directory
        return E_NOSPC;
    } else if (namelen >= namesize) {
        // name too long for `memfile::name_`
        return E_NAMETOOLONG;
    } else {
        memcpy(empty->name_, name, namelen);
        empty->name_[namelen] = 0;
        empty->data_ = nullptr;
        empty->len_ = 0;
        empty->capacity_ = 0;
        return empty - initfs;
    }
}

// memfile::set_length(len)
//    Set the length of this `memfile` to `len`. This might require
//    extending this `memfile`’s capacity; allocates memory if so.
//    Returns 0 on success and an error code such as `E_NOSPC` on
//    failure.
int memfile::set_length(size_t len) {
    // grow file if necessary
    if (len > capacity_) {
        // allocate new data
        if (len > size_t(SSIZE_MAX)) { // too large for safe round_up_pow2
            return E_NOSPC;
        }
        size_t new_capacity = round_up_pow2(len);
        unsigned char* new_data = new(std::nothrow) unsigned char[new_capacity];
        if (!new_data) {
            return E_NOSPC;
        }

        // copy old data over
        if (len_ != 0) {
            memcpy(new_data, data_, len_);
        }

        // delete old data, unless it is kernel static data
        if (data_ && physical_ranges.type(kptr2pa(data_)) != mem_kernel) {
            delete[] data_;
        }

        data_ = new_data;
        capacity_ = new_capacity;
    }

    len_ = len;
    return 0;
}


// memfile_loader functions

// These functions fulfill the requirements of `proc_loader` using a
// `memfile`. See `k-proc.cc` for more on `proc_loader`s.

ssize_t memfile_loader::get_page(uint8_t** pg, size_t off) {
    if (!memfile_) {
        return E_NOENT;
    } else if (off >= memfile_->len_) {
        return 0;
    } else {
        *pg = memfile_->data_ + off;
        return memfile_->len_ - off;
    }
}

void memfile_loader::put_page() {
    // no need to do anything
}
