Chickadee OS
============

This is Chickadee, a teaching operating system built for Harvard’s
[CS 161] in 2018.

Quickstart: `make run`

Source files
------------

### Common files

| File            | Description                  |
| --------------- | ---------------------------- |
| `types.h`       | Type definitions             |
| `lib.hh/cc`     | Chickadee C library          |
| `x86-64.h`      | x86-64 hardware definitions  |
| `elf.h`         | ELF64 structures             |

### Boot loader

| File            | Description                  |
| --------------- | ---------------------------- |
| `bootentry.S`   | Boot loader entry point      |
| `boot.cc`       | Boot loader main code        |
| `boot.ld`       | Boot loader linker script    |

### Kernel libraries

| File                | Description                          |
| ------------------- | ------------------------------------ |
| `k-lock.hh`         | Kernel spinlock                      |
| `k-memrange.hh`     | Memory range type tracker            |
| `k-vmiter.hh/cc`    | Page table iterators                 |
| `k-apic.hh`         | Access interrupt controller hardware |

### Kernel core

| File                | Description                          |
| ------------------- | ------------------------------------ |
| `kernel.hh`         | Kernel declarations                  |
| `k-exception.S`     | Kernel entry points                  |
| `k-init.cc`         | Kernel initialization                |
| `k-hardware.cc`     | Hardware access                      |
| `k-cpu.cc`          | Kernel `cpustate` type               |
| `k-proc.cc`         | Kernel `proc` type                   |
| `kernel.cc`         | Kernel exception handlers            |
| `k-memviewer.cc`    | Kernel memory viewer component       |
| `kernel.ld`         | Kernel linker script                 |

### Processes

| File              | Description                                      |
| ----------------- | ------------------------------------------------ |
| `p-lib.cc/hh`     | Process library and system call implementations  |
| `p-allocator.cc`  | Allocator process                                |
| `process.ld`      | Process binary linker script                     |

Build files
-----------

The main output of the build process is a disk image,
`chickadeeos.img`. QEMU “boots” off this disk image, but the image
could conceivably boot on real hardware! The build process also
produces other files that can be useful to examine.

| File                        | Description                          |
| --------------------------- | ------------------------------------ |
| `obj/kernel.asm`            | Kernel assembly (with addresses)     |
| `obj/kernel.sym`            | Kernel defined symbols               |
| `obj/p-allocator.asm/sym`   | Same for process binaries            |

[CS 161]: https://read.seas.harvard.edu/cs161-18/
