#include "kernel.hh"
#include "k-pci.hh"

namespace {

static uint8_t sum_bytes(const uint8_t* x, size_t len) {
    uint8_t sum = 0;
    for (; len > 0; ++x, --len) {
        sum += *x;
    }
    return sum;
}

struct mpfloat {
    char signature[4];
    uint32_t mpconfig_pa;
    uint8_t len;
    uint8_t spec_rev;
    uint8_t checksum;
    uint8_t feature[5];

    bool check() const {
        return memcmp(signature, "_MP_", 4) == 0
            && len == 1
            && (spec_rev == 1 || spec_rev == 4)
            && sum_bytes(reinterpret_cast<const uint8_t*>(this),
                         sizeof(*this)) == 0;
    }
};

struct mpconfig {
    char signature[4];
    uint16_t len;
    uint8_t spec_rev;
    uint8_t checksum;
    char oem_id[8];
    char product_id[12];
    uint32_t oemt_pa;
    uint16_t oemt_length;
    uint16_t entry_count;
    uint32_t lapic_pa;
    uint16_t ext_len;
    uint8_t ext_checksum;
    uint8_t reserved;

    bool check() const {
        return memcmp(signature, "PCMP", 4) == 0
            && len >= sizeof(*this)
            && (spec_rev == 1 || spec_rev == 4)
            && sum_bytes(reinterpret_cast<const uint8_t*>(this), len) == 0;
    }
    const uint8_t* first() const {
        return reinterpret_cast<const uint8_t*>(this) + sizeof(*this);
    }
    const uint8_t* last() const {
        return reinterpret_cast<const uint8_t*>(this) + len;
    }
    inline const uint8_t* next(const uint8_t* e) const;

    static const mpconfig* find();
};

struct proc_config {
    static constexpr int id = 0;
    uint8_t entry_type;
    uint8_t lapic_id;
    uint8_t lapic_version;
    uint8_t cpu_flags;
    uint32_t cpu_signature;
    uint32_t feature_flags;
    uint32_t reserved[2];
};

struct bus_config {
    static constexpr int id = 1;
    uint8_t entry_type;
    uint8_t bus_id;
    uint8_t bus_type[6];
};

struct ioapic_config {
    static constexpr int id = 2;
    uint8_t entry_type;
    uint8_t ioapic_id;
    uint8_t ioapic_version;
    uint8_t ioapic_flags;
    uint32_t ioapic_pa;

    bool unusable() const {
        return ioapic_flags & 1;
    }
};

struct int_config {
    static constexpr int ioint_id = 3;
    static constexpr int lint_id = 4;
    uint8_t entry_type;
    uint8_t int_type;
    uint16_t int_flags;
    uint8_t bus_id;
    uint8_t bus_irq;
    uint8_t apic_id;
    uint8_t apic_intno;
};

inline const uint8_t* mpconfig::next(const uint8_t* e) const {
    switch (*e) {
    case proc_config::id:
        return e + sizeof(proc_config);
    case bus_config::id:
        return e + sizeof(bus_config);
    case ioapic_config::id:
        return e + sizeof(ioapic_config);
    case int_config::ioint_id:
    case int_config::lint_id:
        return e + sizeof(int_config);
    default:
        return last();
    }
}

static const mpfloat* find_float(const uint8_t* x, size_t len) {
    const uint8_t* end = x + len - sizeof(mpfloat);
    for (; x <= end; x += sizeof(mpfloat)) {
        auto mpf = reinterpret_cast<const mpfloat*>(x);
        if (mpf->check()) {
            return mpf;
        }
    }
    return nullptr;
}

const mpconfig* mpconfig::find() {
    static const mpconfig* config;
    static bool initialized = false;
    if (!initialized) {
        const mpfloat* mpf;
        disable_asan();
        if (uint16_t ebda_base = read_unaligned_pa<uint16_t>
                (X86_BDA_EBDA_BASE_ADDRESS_PA)) {
            mpf = find_float(pa2ka<const uint8_t*>(ebda_base << 4), 1024);
        } else {
            // `basemem` is reported in KiB - 1KiB
            uint16_t basemem = read_unaligned_pa<uint16_t>
                (X86_BDA_BASE_MEMORY_SIZE_PA);
            mpf = find_float(pa2ka<const uint8_t*>(basemem << 10), 1024);
        }
        if (!mpf) {
            mpf = find_float(pa2ka<const uint8_t*>(0xF0000), 0x10000);
        }
        if (mpf && mpf->mpconfig_pa) {
            const mpconfig* c = pa2ka<const mpconfig*>(mpf->mpconfig_pa);
            if (c->check()) {
                config = c;
            }
        }
        enable_asan();
        initialized = true;
    }
    return config;
}

}


unsigned machine_ncpu() {
    auto mpc = mpconfig::find();
    if (!mpc) {
        return 0;
    }

    size_t n = 0;
    for (auto e = mpc->first(); e < mpc->last(); e = mpc->next(e)) {
        if (*e == proc_config::id) {
            ++n;
        }
    }
    return n;
}

unsigned machine_pci_irq(int pci_addr, int intr_pin) {
    auto mpc = mpconfig::find();
    if (!mpc) {
        return 0;
    }

    int bus_id = -1;
    int bus_irq = intr_pin | (pcistate::addr_slot(pci_addr) << 2);
    for (auto e = mpc->first(); e < mpc->last(); e = mpc->next(e)) {
        if (*e == bus_config::id) {
            auto x = reinterpret_cast<const bus_config*>(e);
            if (bus_id == -1 && memcmp(x->bus_type, "PCI   ", 6) == 0) {
                bus_id = x->bus_id;
            }
        } else if (*e == int_config::ioint_id) {
            auto x = reinterpret_cast<const int_config*>(e);
            if (x->bus_id == bus_id
                && x->bus_irq == bus_irq
                && x->apic_id == 0) {
                return x->apic_intno;
            }
        }
    }

    return 0;
}
