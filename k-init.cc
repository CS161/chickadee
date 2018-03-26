#include "kernel.hh"
#include "lib.hh"
#include "k-apic.hh"
#include "k-devices.hh"
#include "elf.h"

// symtab: reference to kernel symbol table; useful for debugging.
// The `mkchickadeesymtab` function fills this structure in.
elf_symtabref symtab = {
    reinterpret_cast<elf_symbol*>(0xFFFFFFFF81000000), 0, nullptr, 0
};

// sata_disk: pointer to the first SATA disk found
ahcistate* sata_disk;


// init_hardware
//    Initialize hardware. Calls other functions below.

static void init_early_memory();
static void init_interrupts();
static void init_constructors();
static void init_physical_ranges();
static void init_other_processors();

void init_hardware() {
    // initialize early-stage virtual memory structures
    init_early_memory();

    // initialize console position
    cursorpos = 3 * CONSOLE_COLUMNS;

    // initialize interrupt descriptors and controller
    init_interrupts();

    // call C++ constructors for global objects
    // (NB none of these constructors may allocate memory)
    init_constructors();

    // initialize this CPU
    ncpu = 1;
    cpus[0].init();

    // initialize the `physical_ranges` object that tracks
    // kernel and reserved physical memory
    init_physical_ranges();

    // initialize kernel allocator
    init_kalloc();

    // initialize other CPUs
    init_other_processors();

#if HAVE_SANITIZERS
    // after CPUs initialize, enable address sanitization
    enable_asan();
#endif

    // enable interrupts
    cpus[0].enable_irq(IRQ_KEYBOARD);

    // initialize SATA drive
    /* sata_disk = ahcistate::find(); */
    if (sata_disk && sata_disk->irq_ > 0) {
        cpus[ncpu - 1].enable_irq(sata_disk->irq_);
    }
}


// init_early_memory
//    Set up early-stage segment registers and page table.
//
//    The early-stage segment registers and global descriptors are
//    used during hardware and secondary-processor initialization.
//    Once a CPU boots, it sets up its own segment registers and
//    global descriptors; see cpustate::init_cpu_hardware(). The
//    early-stage page table is used whenever no appropriate process
//    page table exists.
//
//    The interrupt descriptor table tells the processor where to jump
//    when an interrupt or exception happens. See k-interrupt.S.
//
//    The layouts of these types are defined by the hardware.

static void set_app_segment(uint64_t* segment, uint64_t type, int dpl) {
    *segment = type
        | X86SEG_S                    // code/data segment
        | ((uint64_t) dpl << 45)
        | X86SEG_P;                   // segment present
}

static void set_sys_segment(uint64_t* segment, uint64_t type, int dpl,
                            uintptr_t addr, size_t size) {
    segment[0] = ((addr & 0x0000000000FFFFFFUL) << 16)
        | ((addr & 0x00000000FF000000UL) << 32)
        | ((size - 1) & 0x0FFFFUL)
        | (((size - 1) & 0xF0000UL) << 48)
        | type
        | ((uint64_t) dpl << 45)
        | X86SEG_P;                   // segment present
    segment[1] = addr >> 32;
}

static void set_gate(x86_64_gatedescriptor* gate, int type, int dpl,
                     uintptr_t function) {
    assert(type >= 0 && type < 16);
    gate->gd_low = (function & 0x000000000000FFFFUL)
        | (SEGSEL_KERN_CODE << 16)
        | ((uint64_t) type << 40)
        | ((uint64_t) dpl << 45)
        | X86SEG_P
        | ((function & 0x00000000FFFF0000UL) << 32);
    gate->gd_high = function >> 32;
}

x86_64_pagetable __section(".lowdata") early_pagetable[2];
uint64_t __section(".lowdata") early_gdt_segments[3];
x86_64_pseudodescriptor __section(".lowdata") early_gdt;

void init_early_memory() {
    // initialize segment descriptors for kernel code and data
    early_gdt_segments[0] = 0;
    set_app_segment(&early_gdt_segments[SEGSEL_KERN_CODE >> 3],
                    X86SEG_X | X86SEG_L, 0);
    set_app_segment(&early_gdt_segments[SEGSEL_KERN_DATA >> 3],
                    X86SEG_W, 0);
    early_gdt.limit = sizeof(early_gdt_segments) - 1;
    early_gdt.base = (uint64_t) early_gdt_segments;

    asm volatile("lgdt %0" : : "m" (early_gdt.limit));


    // initialize early page table
    memset(early_pagetable, 0, sizeof(early_pagetable));
    // high canonical addresses
    early_pagetable->entry[256] = ktext2pa(&early_pagetable[1]) | PTE_P | PTE_W;
    for (uintptr_t p = 0; p < 510; ++p) {
        early_pagetable[1].entry[p] = (p << 30) | PTE_P | PTE_W | PTE_PS;
    }
    // kernel text addresses
    early_pagetable->entry[511] = early_pagetable->entry[256];
    early_pagetable[1].entry[510] = early_pagetable[1].entry[0];
    early_pagetable[1].entry[511] = early_pagetable[1].entry[1];
    // physically-mapped low canonical addresses
    early_pagetable->entry[0] = early_pagetable->entry[256];

    lcr3(ktext2pa(early_pagetable));


    // Now that boot-time structures (pagetable and global descriptor
    // table) have been replaced, we can reuse boot-time memory.
}


extern x86_64_gatedescriptor interrupt_descriptors[256];

void init_interrupts() {
    // initialize interrupt descriptors
    // Macros in `k-exception.S` initialized `interrupt_descriptors[]` with
    // function pointers in the `gd_low` members. We must change them to the
    // weird format x86-64 expects.
    for (int i = 0; i < 256; ++i) {
        uintptr_t addr = interrupt_descriptors[i].gd_low;
        set_gate(&interrupt_descriptors[i], X86GATE_INTERRUPT, 0, addr);
    }


    // ensure machine has an enabled APIC
    assert(cpuid(1).edx & (1 << 9));
    uint64_t apic_base = rdmsr(MSR_IA32_APIC_BASE);
    assert(apic_base & IA32_APIC_BASE_ENABLED);
    assert((apic_base & 0xFFFFFFFFF000) == lapicstate::lapic_pa);

    // ensure machine has an IOAPIC
    auto& ioapic = ioapicstate::get();
    uint32_t ioapic_ver = ioapic.read(ioapic.reg_ver);
    assert((ioapic_ver & 0xFF) == 0x11 || (ioapic_ver & 0xFF) == 0x20);
    assert((ioapic_ver >> 16) >= 0x17);

    // disable the old programmable interrupt controller
#define IO_PIC1         0x20    // Master (IRQs 0-7)
#define IO_PIC2         0xA0    // Slave (IRQs 8-15)
    outb(IO_PIC1 + 1, 0xFF);
    outb(IO_PIC2 + 1, 0xFF);
}


void init_constructors() {
    typedef void (*constructor_function)();
    extern constructor_function __init_array_start[];
    extern constructor_function __init_array_end[];
    for (auto fp = __init_array_start; fp != __init_array_end; ++fp) {
        (*fp)();
    }
}


memrangeset<16> physical_ranges(0x100000000UL);

void init_physical_ranges() {
    // [0, MEMSIZE_PHYSICAL) starts out available
    physical_ranges.set(0, MEMSIZE_PHYSICAL, mem_available);
    // 0 page is reserved (because nullptr)
    physical_ranges.set(0, PAGESIZE, mem_reserved);
    // I/O memory is reserved (except the console is `mem_console`)
    physical_ranges.set(PA_IOLOWMIN, PA_IOLOWEND, mem_reserved);
    physical_ranges.set(PA_IOHIGHMIN, PA_IOHIGHEND, mem_reserved);
    physical_ranges.set(ktext2pa(console), ktext2pa(console) + PAGESIZE,
                        mem_console);
    // kernel text and data is owned by the kernel
    extern char _low_data_start[], _low_data_end[];
    physical_ranges.set(ROUNDDOWN(ktext2pa(_low_data_start), PAGESIZE),
                        ROUNDUP(ktext2pa(_low_data_end), PAGESIZE),
                        mem_kernel);
    extern char _kernel_start[], _kernel_end[];
    physical_ranges.set(ROUNDDOWN(ktext2pa(_kernel_start), PAGESIZE),
                        ROUNDUP(ktext2pa(_kernel_end), PAGESIZE),
                        mem_kernel);
    // reserve memory for debugging facilities
    if (symtab.size) {
        auto sympa = ktext2pa(symtab.sym);
        physical_ranges.set(ROUNDDOWN(sympa, PAGESIZE),
                            ROUNDUP(sympa + symtab.size, PAGESIZE),
                            mem_kernel);
    }
#if HAVE_SANITIZERS
    init_sanitizers();
#endif

    // `physical_ranges` is constant after this point.
}


extern "C" { void syscall_entry(); }

void cpustate::init_cpu_hardware() {
    // initialize per-CPU segments
    gdt_segments_[0] = 0;
    set_app_segment(&gdt_segments_[SEGSEL_KERN_CODE >> 3],
                    X86SEG_X | X86SEG_L, 0);
    set_app_segment(&gdt_segments_[SEGSEL_KERN_DATA >> 3],
                    X86SEG_W, 0);
    set_app_segment(&gdt_segments_[SEGSEL_APP_CODE >> 3],
                    X86SEG_X | X86SEG_L, 3);
    set_app_segment(&gdt_segments_[SEGSEL_APP_DATA >> 3],
                    X86SEG_W, 3);
    set_sys_segment(&gdt_segments_[SEGSEL_TASKSTATE >> 3],
                    X86SEG_TSS, 0,
                    (uintptr_t) &task_descriptor_, sizeof(task_descriptor_));

    memset(&task_descriptor_, 0, sizeof(task_descriptor_));
    task_descriptor_.ts_rsp[0] = (uintptr_t) this + CPUSTACK_SIZE;

    x86_64_pseudodescriptor gdt, idt;
    gdt.limit = sizeof(gdt_segments_) - 1;
    gdt.base = (uint64_t) gdt_segments_;
    idt.limit = sizeof(interrupt_descriptors) - 1;
    idt.base = (uint64_t) interrupt_descriptors;


    // load segment descriptor tables
    asm volatile("lgdt %0; ltr %1; lidt %2"
                 :
                 : "m" (gdt.limit),
                   "r" ((uint16_t) SEGSEL_TASKSTATE),
                   "m" (idt.limit)
                 : "memory", "cc");

    // initialize segments, including `%gs`, which points at this cpustate
    asm volatile("movw %%ax, %%fs; movw %%ax, %%gs"
                 : : "a" ((uint16_t) SEGSEL_KERN_DATA));
    wrmsr(MSR_IA32_GS_BASE, reinterpret_cast<uint64_t>(this));


    // set up control registers
    uint32_t cr0 = rcr0();
    cr0 |= CR0_PE | CR0_PG | CR0_WP | CR0_AM | CR0_MP | CR0_NE;
    lcr0(cr0);


    // set up syscall/sysret
    wrmsr(MSR_IA32_KERNEL_GS_BASE, reinterpret_cast<uint64_t>(this));
    wrmsr(MSR_IA32_STAR, (uintptr_t(SEGSEL_KERN_CODE) << 32)
          | (uintptr_t(SEGSEL_APP_CODE) << 48));
    wrmsr(MSR_IA32_LSTAR, reinterpret_cast<uint64_t>(syscall_entry));
    wrmsr(MSR_IA32_FMASK, EFLAGS_TF | EFLAGS_DF | EFLAGS_IF
          | EFLAGS_IOPL_MASK | EFLAGS_AC | EFLAGS_NT);


    // initialize local APIC (interrupt controller)
    auto& lapic = lapicstate::get();
    lapic.enable_lapic(INT_IRQ + IRQ_SPURIOUS);

    lapic_id_ = lapic.id();

    // lapic timer goes off every 0.01s
    lapic.write(lapic.reg_timer_divide, lapic.timer_divide_1);
    lapic.write(lapic.reg_lvt_timer,
                lapic.timer_periodic | (INT_IRQ + IRQ_TIMER));
    lapic.write(lapic.reg_timer_initial_count, 1000000000 / HZ);

    // disable logical interrupt lines
    lapic.write(lapic.reg_lvt_lint0, lapic.lvt_masked);
    lapic.write(lapic.reg_lvt_lint1, lapic.lvt_masked);

    // 12. set LVT error handling entry
    lapic.write(lapic.reg_lvt_error, INT_IRQ + IRQ_ERROR);

    // clear error status by reading the error;
    // acknowledge any outstanding interrupts
    lapic.error();
    lapic.ack();
}


static void microdelay(int amount) {
    uint64_t x = rdtsc() + (uint64_t) amount * 10000;
    while ((int64_t) (x - rdtsc()) > 0) {
        asm volatile("pause");
    }
}

extern "C" {
extern void ap_entry();
extern spinlock ap_entry_lock;
extern bool ap_init_allowed;
}

void cpustate::init_ap() {
    init();
    ap_entry_lock.unlock_noirq();
    schedule(nullptr);
}

void init_other_processors() {
    // 10. convert entry point to an 8-bit vector
    uintptr_t ap_entry_pa = ktext2pa(ap_entry);
    assert((ap_entry_pa & 0xFFFFFFFFFFF00FFF) == 0);

    // record this CPU as CPU 0
    // mark APs as initializing
    ap_init_allowed = true;

    // XXX CMOS shutdown code, warm reset vector

    // 15. broadcast INIT-SIPI-SIPI
    auto& lapic = lapicstate::get();
    lapic.ipi_others(lapic.ipi_init);
    microdelay(10000);
    while (lapic.ipi_pending()) {
    }

    lapic.ipi_others(lapic.ipi_startup, ap_entry_pa >> 12);
    microdelay(200);
    while (lapic.ipi_pending()) {
    }

    lapic.ipi_others(lapic.ipi_startup, ap_entry_pa >> 12);
    // wait for processors to start up
    microdelay(20000);
    while (lapic.ipi_pending()) {
    }

    ap_entry_lock.lock_noirq();
    ap_init_allowed = false;
    ap_entry_lock.unlock_noirq();

    // Now that `ap_init_allowed` is false, no further CPUs will
    // initialize.
    for (int i = 0; i < ncpu; ++i) {
        log_printf("CPU %d: LAPIC ID %d\n", i, cpus[i].lapic_id_);
    }
}
