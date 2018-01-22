OUTPUT_FORMAT(elf64-x86-64)
ENTRY(kernel_entry)

PHDRS {
    low PT_LOAD;
    text PT_LOAD;
}

SECTIONS {
    /* Boot process uses 0x1000-0x3FFF */


    /* Low text and data must be at very low physical addresses */
    . = 0xFFFFFFFF80004000;
    _low_data_start = .;
    .lowtext : {
        KEEP (*(.lowtext))
    } :low
    .lowdata (NOLOAD) : {
        *(.lowdata)
    } :NONE
    _low_data_end = .;


    /* Text segment: instructions and read-only globals */
    . = 0xFFFFFFFF80100000;
    _kernel_start = .;
    .text : {
        *(.text.unlikely .text.*_unlikely .text.unlikely.*)
        *(.text.exit .text.exit.*)
        *(.text.startup .text.startup.*)
        *(.text.hot .text.hot.*)
        *(.text .stub .text.* .gnu.linkonce.t.*)
    } :text
    _etext = .;

    .rodata : {
        *(.rodata .rodata.* .gnu.linkonce.r.*)
        *(.data.rel.ro.local* .gnu.linkonce.d.rel.ro.local.*)
        *(.data.rel.ro .data.rel.ro.* .gnu.linkonce.d.rel.ro.*)
    } :text

    /* Constructors: these sections support global initialization
       functions, such as for global C++ objects with constructors. */
    .init_array : {
        PROVIDE(__init_array_start = .);
        KEEP (*(SORT_BY_INIT_PRIORITY(.init_array.*)
                SORT_BY_INIT_PRIORITY(.ctors.*)))
        KEEP (*(.init_array .ctors))
        PROVIDE(__init_array_end = .);
    } :text
    .ctors : {
        KEEP (*crtbegin.o(.ctors))
        KEEP (*(SORT(.ctors.*)))
        KEEP (*(.ctors))
    } :text

    /* Data segment: read/write and zero-initialized globals */
    . = ALIGN(4096);       /* Align to a page boundary */
    .data : {
        *(.data .data.* .gnu.linkonce.d.*)
        . = ALIGN(16);
        interrupt_descriptors = .;
        KEEP (*(.interrupt_descriptors))
    } :text

    .bss : {
        *(.bss .bss.* .gnu.linkonce.b.*)
    } :text
    PROVIDE(_kernel_end = .);

    PROVIDE(console = 0xFFFFFFFF800B8000);
    PROVIDE(cursorpos = 0xFFFFFFFF800B8FFC);

    PROVIDE(early_pagetable_low = early_pagetable & 0xFFFF);
    PROVIDE(early_gdt_low = early_gdt & 0xFFFF);
    PROVIDE(ap_rest_low = ap_rest & 0xFFFF);

    /DISCARD/ : { *(.eh_frame .note.GNU-stack) }
}
