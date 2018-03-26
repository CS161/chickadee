#include "k-devices.hh"
#include "k-apic.hh"
#include "k-pci.hh"

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

#define CKEY(cn)        0x80 + cn

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

keyboardstate::keyboardstate()
    : pos_(0), len_(0), eol_(0), state_(boot) {
}

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

done:
    bool wake = eol_ != 0;
    lock_.unlock(irqs);
    lapicstate::get().ack();

    if (wake) {
        wq_.wake_all();
    }
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

// console_show_cursor(cpos)
//    Move the console cursor to position `cpos`, which should be between 0
//    and 80 * 25.

void console_show_cursor(int cpos) {
    if (cpos < 0 || cpos > CONSOLE_ROWS * CONSOLE_COLUMNS) {
        cpos = 0;
    }
    outb(0x3D4, 14);
    outb(0x3D5, cpos / 256);
    outb(0x3D4, 15);
    outb(0x3D5, cpos % 256);
}


// memfile functions and initial contents

#include "obj/k-initfs.cc"

memfile* memfile::initfs_lookup(const char* name, size_t namelen) {
    for (memfile* f = initfs; f != initfs + initfs_size; ++f) {
        if (!f->empty()
            && memcmp(f->name_, name, namelen) == 0
            && f->name_[namelen] == 0) {
            return f;
        }
    }
    return nullptr;
}


// ahcistate: functions for dealing with SATA disks

// HELPER FUNCTIONS FOR PREPARING, ISSUING, AND ACKNOWLEDGING COMMANDS

// ahcistate::clear(slot)
//    Prepare `slot` to receive a command.
inline void ahcistate::clear(int slot) {
    assert(unsigned(slot) < unsigned(nslots_));
    dma_.ch[slot].nbuf = 0;
    dma_.ch[slot].buf_byte_pos = 0;
}

// ahcistate::push_buffer(slot, buf, sz)
//    Append a data buffer to the buffers relevant for the next command.
inline void ahcistate::push_buffer(int slot, void* buf, size_t sz) {
    // check requirements on address and size
    uint64_t pa = kptr2pa(buf);
    assert((pa & 1) == 0 && (sz & 1) == 0);       // word-aligned
    assert(sz > 0 && sz <= (64U << 10));          // >0, <64KiB
    assert(pa <= 0x100000000UL);                  // low physical memory
    assert((pa ^ (pa + sz - 1)) < (64U << 10));   // within aligned 64KiB

    // check slot availability
    assert(unsigned(slot) < unsigned(nslots_));
    assert(dma_.ch[slot].nbuf < arraysize(dma_.ct[slot].buf));

    int nbuf = dma_.ch[slot].nbuf;
    dma_.ct[slot].buf[nbuf].pa = pa;
    dma_.ct[slot].buf[nbuf].maxbyte = sz - 1;
    dma_.ch[slot].nbuf = nbuf + 1;
    dma_.ch[slot].buf_byte_pos += sz;
}

// ahcistate::issue_ncq(slot, cmd, sector, fua = false, priority = 0)
//    Issue an NCQ command (`cmd_read/write_fpdma_queued`) to the disk.
//    Must be preceded by `clear(slot); push_buffer(slot...)*`.
//    `fua`: If true, then do not acknowledge the write until data has
//    been durably written to the disk.
//    `priority`: 0 is normal; 2 means high priority.
void ahcistate::issue_ncq(int slot, idecommand cmd, size_t sector,
                          bool fua, int priority) {
    assert(unsigned(slot) < unsigned(nslots_));
    assert(!(slots_outstanding_mask_ & (1U << slot)));
    assert(cmd == cmd_read_fpdma_queued || cmd == cmd_write_fpdma_queued);
    assert(unsigned(priority) < 3);
    assert(dma_.ch[slot].buf_byte_pos > 0);
    size_t nsectors = dma_.ch[slot].buf_byte_pos / sectorsize;

    dma_.ct[slot].cfis[0] = cfis_command | (unsigned(cmd) << 16)
        | ((nsectors & 0xFF) << 24);
    dma_.ct[slot].cfis[1] = (sector & 0xFFFFFF)
        | (uint32_t(fua) << 31) | 0x40000000U;
    dma_.ct[slot].cfis[2] = (sector >> 24) | ((nsectors & 0xFF00) << 16);
    dma_.ct[slot].cfis[3] = (slot << 3) | (priority << 14);

    dma_.ch[slot].flags = 4 /* # words in `cfis` */
        | ch_clear_flag
        | (cmd == cmd_write_fpdma_queued ? ch_write_flag : 0);
    dma_.ch[slot].buf_byte_pos = 0;

    // ensure all previous writes have made it out to memory
    std::atomic_thread_fence(std::memory_order_release);

    pr_->ncq_active_mask = 1U << slot;  // tell interface NCQ slot used
    pr_->command_mask = 1U << slot;     // tell interface command available
    // The write to `command_mask` wakes up the device.

    slots_outstanding_mask_ |= 1U << slot;   // remember slot
    --nslots_available_;
}

// ahcistate::acknowledge(slot, result)
//    Acknowledge a waiting command in `slot`.
inline void ahcistate::acknowledge(int slot, int result) {
    assert(unsigned(slot) < unsigned(nslots_));
    assert(slots_outstanding_mask_ & (1U << slot));
    slots_outstanding_mask_ ^= 1U << slot;
    ++nslots_available_;
    if (slot_status_[slot]) {
        *slot_status_[slot] = result;
        slot_status_[slot] = nullptr;
    }
}


// FUNCTIONS FOR READING AND WRITING BLOCKS

// ahcistate::read_or_write(command, buf, sz, off)
//    Issue an NCQ read or write command `command`. Read or write
//    `sz` bytes of data to or from `buf`, starting at disk offset
//    `off`. `sz` and `off` are measured in bytes, but must be
//    sector-aligned (i.e., multiples of `ahcistate::sectorsize`).
//    Can block. Returns 0 on success and an error code on failure.

int ahcistate::read_or_write(idecommand command, void* buf, size_t sz,
                             size_t off) {
    // `sz` and `off` must be sector-aligned
    assert(sz % sectorsize == 0 && off % sectorsize == 0);

    // obtain lock
    proc* p = current();
    auto irqs = lock_.lock();

    // block until ready for command
    waiter(p).block_until(wq_, [&] () {
            return !slots_outstanding_mask_;
        }, lock_, irqs);

    // send command, record buffer and status storage
    volatile int r = E_AGAIN;
    clear(0);
    push_buffer(0, buf, sz);
    issue_ncq(0, command, off / sectorsize);
    slot_status_[0] = &r;

    lock_.unlock(irqs);

    // wait for response
    waiter(p).block_until(wq_, [&] () {
            return r != E_AGAIN;
        });
    return r;
}


// FUNCTIONS FOR HANDLING INTERRUPTS

void ahcistate::handle_interrupt() {
    // obtain lock, read data
    auto irqs = lock_.lock();

    // check interrupt reason, clear interrupt
    bool is_error = pr_->interrupt_status & interrupt_fatal_error_mask;
    pr_->interrupt_status = ~0U;
    dr_->interrupt_status = ~0U;

    // acknowledge completed commands
    uint32_t acks = slots_outstanding_mask_ & ~pr_->ncq_active_mask;
    for (int slot = 0; acks != 0; ++slot, acks >>= 1) {
        if (acks & 1) {
            acknowledge(slot, 0);
        }
    }

    // acknowledge errored commands
    if (is_error) {
        handle_error_interrupt();
    }

    lock_.unlock(irqs);

    // wake waiters
    lapicstate::get().ack();
    wq_.wake_all();
}

void ahcistate::handle_error_interrupt() {
    for (int slot = 0; slot < 32; ++slot) {
        if (slots_outstanding_mask_ & (1U << slot)) {
            acknowledge(slot, E_IO);
        }
    }
    // SATA AHCI 1.3.1 §6.2.2.2
    pr_->command &= ~pcmd_start;
    while (pr_->command & pcmd_command_running) {
        pause();
    }
    pr_->serror = ~0U;
    pr_->command |= pcmd_start;
    // XXX must `READ LOG EXT` to clear error
    panic("SATA disk error");
}


// INITIALIZATION FUNCTIONS

void ahcistate::issue_meta(int slot, idecommand cmd, int features,
                           int count) {
    assert(unsigned(slot) < unsigned(nslots_));
    assert(unsigned(features) < 0x10000);
    assert(!(slots_outstanding_mask_ & (1U << slot)));
    assert(cmd == cmd_identify_device || cmd == cmd_set_features);
    size_t nsectors = dma_.ch[slot].buf_byte_pos / sectorsize;
    if (cmd == cmd_set_features && count != -1) {
        nsectors = count;
    }

    dma_.ct[slot].cfis[0] = cfis_command | (unsigned(cmd) << 16)
        | (unsigned(features) << 24);
    dma_.ct[slot].cfis[1] = 0;
    dma_.ct[slot].cfis[2] = (unsigned(features) & 0xFF00) << 16;
    dma_.ct[slot].cfis[3] = nsectors;

    dma_.ch[slot].flags = 4 /* # words in `cfis` */ | ch_clear_flag;
    dma_.ch[slot].buf_byte_pos = 0;

    // ensure all previous writes have made it out to memory
    std::atomic_thread_fence(std::memory_order_release);

    pr_->command_mask = 1U << slot;    // tell interface command is available

    slots_outstanding_mask_ |= 1U << slot;
    --nslots_available_;
}

void ahcistate::await_basic(int slot) {
    assert(unsigned(slot) < unsigned(nslots_));
    while (pr_->command_mask & (1U << slot)) {
        pause();
    }
    acknowledge(slot, 0);
}

ahcistate::ahcistate(int pci_addr, int sata_port, volatile regs* dr)
    : pci_addr_(pci_addr), sata_port_(sata_port),
      dr_(dr), pr_(&dr->p[sata_port]), nslots_(1),
      nslots_available_(1), slots_outstanding_mask_(0) {
    for (int i = 0; i < 32; ++i) {
        slot_status_[i] = nullptr;
    }

    auto& pci = pcistate::get();
    pci.enable(pci_addr_);

    // place port in idle state
    pr_->command &= ~uint32_t(pcmd_rfis_enable | pcmd_start);
    while (pr_->command & (pcmd_command_running | pcmd_rfis_running)) {
        pause();
    }

    // set up DMA area
    memset(&dma_, 0, sizeof(dma_));
    for (int i = 0; i != 32; ++i) {
        dma_.ch[i].cmdtable_pa = ka2pa(&dma_.ct[i]);
    }
    pr_->cmdlist_pa = ka2pa(&dma_.ch[0]);
    pr_->rfis_pa = ka2pa(&dma_.rfis);

    // clear errors and power up
    pr_->serror = ~0U;
    pr_->command |= pcmd_power_up;

    // configure interrupts
    pr_->interrupt_status = ~0U; // clear pending interrupts
    dr_->interrupt_status = ~0U;
    pr_->interrupt_enable = interrupt_device_to_host
        | interrupt_ncq_complete
        | interrupt_error_mask;
    dr_->ghc |= ghc_interrupt_enable;

    // wait for functional device
    pr_->command |= pcmd_rfis_enable;
    while ((pr_->rstatus & (rstatus_busy | rstatus_datareq)) != 0
           || !sstatus_active(pr_->sstatus)) {
        pause();
    }

    // wake up port
    pr_->command = (pr_->command & ~pcmd_interface_mask)
        | pcmd_interface_active;
    while ((pr_->command & pcmd_interface_mask) != pcmd_interface_idle) {
        pause();
    }
    // NB QEMU does not implement PxCMD.CLO/pcmd_rfis_clear
    pr_->command |= pcmd_start;

    // send IDENTIFY DEVICE command and check results
    uint16_t devid[256];
    memset(devid, 0, sizeof(devid));

    clear(0);
    push_buffer(0, devid, sizeof(devid));
    issue_meta(0, cmd_identify_device, 0);
    await_basic(0);

    assert(!(devid[0] & 0x0004));                  // response complete
    assert((devid[53] & 0x0006) == 0x0006);        // all words set
    // valid SATA device
    assert((devid[49] & 0x0300) == 0x0300);        // LBA + DMA
    assert(devid[76] != 0 && devid[76] != 0xFFFF); // Serial ATA device
    assert(devid[83] & 0x0400);                    // 48-bit addresses
    assert(!(devid[106] & 0x1000));                // 512-byte logical sectors
    assert(devid[76] & 0x0100);                    // NCQ supported
    // fast transfer modes
    assert(devid[64] & 0x0002);                    // PIO mode 4 supported
    assert((devid[88] & 0x2020) == 0x2020          // Ultra DMA 5 or 6 selected
           || (devid[88] & 0x4040) == 0x4040);

    // count sectors
    nsectors_ = devid[100] | (devid[101] << 16) | (uint64_t(devid[102]) << 32)
        | (uint64_t(devid[103]) << 48);
    assert(nsectors_ < 0x1000000000000UL);

    // count slots
    assert(dr_->capabilities & 0x40000000U);         // NCQ supported
    nslots_ = ((dr_->capabilities >> 8) & 0x1F) + 1; // slots per controller
    if ((devid[75] & 0x1F) + 1U < nslots_) {         // slots per disk
        nslots_ = (devid[75] & 0x1F) + 1;
    }
    slots_full_mask_ = (nslots_ == 32 ? 0xFFFFFFFFU : (1U << nslots_) - 1);
    nslots_available_ = nslots_;

    // set features
    clear(0);
    issue_meta(0, cmd_set_features, 0x02);  // write cache enable
    await_basic(0);

    clear(0);
    issue_meta(0, cmd_set_features, 0xAA);  // read lookahead enable
    await_basic(0);

    // determine IRQ
    int intr_pin = ((pci.readl(pci_addr + 0x3C) >> 8) & 0xFF) - 1;
    irq_ = machine_pci_irq(pci_addr_, intr_pin);

    // finally, clear pending interrupts again
    pr_->interrupt_status = ~0U;
    dr_->interrupt_status = ~0U;
}

// ahcistate::find(addr, port)
//    Find and initialize an SATA disk. Return a pointer to the `ahcistate`,
//    or `nullptr` if the search fails. The search begins at PCI address
//    `addr` and SATA port number `port`.
ahcistate* ahcistate::find(int addr, int port) {
    auto& pci = pcistate::get();
    for (; addr >= 0; addr = pci.next(addr), port = 0) {
        if (pci.readw(addr + pci.config_subclass) != 0x0106) {
            continue;
        }
        uint32_t pa = pci.readl(addr + pci.config_bar5);
        if (pa == 0) {
            continue;
        }
        auto dr = pa2ka<volatile regs*>(pa);
        // "Indicate that system software is AHCI aware"
        if (!(dr->ghc & ghc_ahci_enable)) {
            dr->ghc = ghc_ahci_enable;
        }
        for (; port < 32; ++port) {
            if ((dr->port_mask & (1U << port))
                && dr->p[port].sstatus) {
                return knew<ahcistate>(addr, port, dr);
            }
        }
    }
    return nullptr;
}

static_assert(sizeof(ahcistate::portregs) == 128,
              "ahcistate::portregs size");
static_assert(sizeof(ahcistate::regs) == 256 + 32 * 128,
              "ahcistate::regs size");
