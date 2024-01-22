#include "k-ahci.hh"
#include "k-apic.hh"
#include "k-pci.hh"

// k-ahci.cc
//
//    Functions for interacting with advanced SATA disks.


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
void ahcistate::push_buffer(int slot, void* buf, size_t sz) {
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
    auto irqs = lock_.lock();

    // block until ready for command
    waiter().block_until(wq_, [&] () {
            return !slots_outstanding_mask_;
        }, lock_, irqs);

    // send command, record buffer and status storage
    std::atomic<int> r = E_AGAIN;
    clear(0);
    push_buffer(0, buf, sz);
    issue_ncq(0, command, off / sectorsize);
    slot_status_[0] = &r;

    lock_.unlock(irqs);

    // wait for response
    waiter().block_until(wq_, [&] () {
            return r != E_AGAIN;
        });
    return r;
}


// FUNCTIONS FOR HANDLING INTERRUPTS

void ahcistate::handle_interrupt() {
    // obtain lock, read data
    auto irqs = lock_.lock();

    // check interrupt reason, clear interrupt
    bool is_error = (pr_->interrupt_status & interrupt_fatal_error_mask)
        || (pr_->rstatus & rstatus_error);
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
        auto dr = pa2kptr<volatile regs*>(pa);
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
