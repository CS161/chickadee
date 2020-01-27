#ifndef CHICKADEE_K_AHCI_HH
#define CHICKADEE_K_AHCI_HH
#include "kernel.hh"
#include "k-wait.hh"

// achistate: communication with modern SATA disks

struct ahcistate {
    // interesting IDE commands
    // Specification: "ATA/ATAPI Command Set" for ATA-8
    enum idecommand {
        cmd_identify_device = 0xEC,
        cmd_set_features = 0xEF,
        cmd_read_fpdma_queued = 0x60,
        cmd_write_fpdma_queued = 0x61
    };

    // memory-mapped I/O structures for device communication
    // Specification/comment field names: "Serial ATA AHCI 1.3.1 Specification"
    //    The kernel finds the MMIO address of a `struct regs` using PCI.
    //    This contains up to 32 `struct portregs`, each corresponding to
    //    a different disk; most communication with the disk uses the
    //    `portregs`. One `ahcistate` corresponds to one disk.
    struct portregs {
        uint64_t cmdlist_pa;       // PxCLB: physical addr of `cmdheader` array
        uint64_t rfis_pa;          // PxRFIS: physical address of `rfisstate`
        uint32_t interrupt_status; // PxIS
        uint32_t interrupt_enable; // PxIE
        uint32_t command;          // PxCMD
        uint32_t reserved0;
        uint32_t rstatus;          // PxTFD
        uint32_t psig;             // PxSIG
        uint32_t sstatus;          // PxSSTS: 0 means port doesn't exist
        uint32_t scontrol;         // PxSCTL
        uint32_t serror;           // PxSERR [R/clear]
        uint32_t ncq_active_mask;  // PxSACT
        uint32_t command_mask;     // PxCI
        uint32_t sata_notification; // PxSNTF
        uint32_t pfbs, sleep;
        uint32_t reserved[14];
    };
    struct regs {
        uint32_t capabilities;     // CAP: device capabilities [R]
        uint32_t ghc;              // GHC: global [adapter] control [R/W]
        uint32_t interrupt_status; // IS: interrupt status [R/clear]
        uint32_t port_mask;        // PI: addressable ports (may not exist)
        uint32_t ahci_version;     // VS
        uint32_t ccc_control;      // CCC_CTL
        uint32_t ccc_port_mask;    // CCC_PORTS
        uint32_t em_loc, em_ctl, cap2, bohc, reserved[53];
        portregs p[32];
    };
    enum {
        pcmd_interface_mask = 0xF0000000U,
        pcmd_interface_active = 0x10000000U, pcmd_interface_idle = 0U,
        pcmd_command_running = 0x8000, pcmd_rfis_running = 0x4000,
        pcmd_rfis_enable = 0x0010,
        pcmd_rfis_clear = 0x8, pcmd_power_up = 0x6, pcmd_start = 0x1U,

        rstatus_busy = 0x80U, rstatus_datareq = 0x08U, rstatus_error = 0x01U,

        interrupt_device_to_host = 0x1U,
        interrupt_ncq_complete = 0x8U,
        interrupt_error_mask = 0x7D800010U,
        interrupt_fatal_error_mask = 0x78000000U, // HBFS|HBDS|IFS|TFES

        ghc_ahci_enable = 0x80000000U, ghc_interrupt_enable = 0x2U
    };
    static inline bool sstatus_active(uint32_t sstatus) {
        return (sstatus & 0x03) == 3
            || ((1U << ((sstatus & 0xF00) >> 8)) & 0x144) != 0;
    }

    // DMA structures for device communication
    //    Contains data buffers and commands for disk communication.
    //    This structure lives in host memory; the device finds it via
    //    physical-memory pointers in `portregs`.
    struct bufstate {             // initialized by `push_buffer`
        uint64_t pa;
        uint32_t reserved;
        uint32_t maxbyte;         // size of buffer minus 1
    };
    struct __attribute__((aligned(128))) cmdtable {
        uint32_t cfis[16];        // command definition; set by `issue_*`
        uint32_t acmd[4];
        uint32_t reserved[12];
        bufstate buf[16];         // called PRD in specifications
    };
    struct cmdheader {
        uint16_t flags;
        uint16_t nbuf;
        uint32_t buf_byte_pos;
        uint64_t cmdtable_pa;     // physical address of `cmdtable`
        uint64_t reserved[2];
    };
    struct __attribute__((aligned(256))) rfisstate {
        uint32_t rfis[64];
    };
    struct __attribute__((aligned(1024))) dmastate {
        cmdheader ch[32];
        volatile rfisstate rfis;
        cmdtable ct[32];
    };

    enum {
        cfis_command = 0x8027,
        ch_clear_flag = 0x400, ch_write_flag = 0x40
    };

    static constexpr size_t sectorsize = 512;


    // DMA and memory-mapped I/O state
    dmastate dma_;
    int pci_addr_;
    int sata_port_;
    volatile regs* dr_;
    volatile portregs* pr_;

    // metadata read from disk at startup (constant thereafter)
    unsigned irq_;                      // interrupt number
    size_t nsectors_;                   // # sectors on disk
    unsigned nslots_;                   // # NCQ slots
    unsigned slots_full_mask_;          // mask with each valid slot set to 1

    // modifiable state
    spinlock lock_;
    wait_queue wq_;
    unsigned nslots_available_;         // # slots available for commands
    uint32_t slots_outstanding_mask_;   // 1 == that slot is used
    std::atomic<int>* slot_status_[32]; // ptrs to status storage, one per slot


    ahcistate(int pci_addr, int sata_port, volatile regs* mr);
    NO_COPY_OR_ASSIGN(ahcistate);
    static ahcistate* find(int pci_addr = 0, int sata_port = 0);

    // high-level functions (they block)
    inline int read(void* buf, size_t sz, size_t off);
    inline int write(const void* buf, size_t sz, size_t off);
    int read_or_write(idecommand cmd, void* buf, size_t sz, size_t off);

    // interrupt handlers
    void handle_interrupt();
    void handle_error_interrupt();

    // internal functions
    void clear(int slot);
    void push_buffer(int slot, void* data, size_t sz);
    void issue_meta(int slot, idecommand cmd, int features, int count = -1);
    void issue_ncq(int slot, idecommand cmd, size_t sector,
                   bool fua = false, int priority = 0);
    void acknowledge(int slot, int status = 0);
    void await_basic(int slot);
};

inline int ahcistate::read(void* buf, size_t sz, size_t off) {
    return read_or_write(cmd_read_fpdma_queued, buf, sz, off);
}
inline int ahcistate::write(const void* buf, size_t sz, size_t off) {
    return read_or_write(cmd_write_fpdma_queued, const_cast<void*>(buf),
                         sz, off);
}

#endif
