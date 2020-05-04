Chickadee OS
============

This is Chickadee, a teaching operating system built for Harvard’s
[CS 161].

Quickstart: `make run` or `make run-PROGRAM`

Make targets
------------

Run `make NCPU=N run` to run with `N` virtual CPUs (default is 2).

Run `make SAN=1 run` to run with sanitizers.

Normally Chickadee’s debug log is written to `log.txt`. Run `make LOG=stdio
run` to redirect the debug log to the standard output, or `make
LOG=file:FILENAME run` to redirect it to `FILENAME`.

Run `make D=1 run` to ask QEMU to print verbose information about interrupts
and CPU resets to the standard error. This setting will also cause QEMU to
quit after encountering a [triple fault][] (normally it will reboot).

`make run-PROGRAM` runs `p-PROGRAM.cc` as the first non-init process. The
default is `allocator`.

`make HALT=1 run-PROGRAM` should make QEMU exit once all processes are done.

Troubleshooting
---------------

If you experience runtime errors involving `obj/libqemu-nograb.so.1`, put
`QEMU_PRELOAD_LIBRARY=` in `config.mk`. This disables a shim we use that
prevents QEMU from grabbing the mouse.

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

### Kernel core

| File                | Description                          |
| ------------------- | ------------------------------------ |
| `kernel.hh`         | Kernel declarations                  |
| `k-exception.S`     | Kernel entry points                  |
| `k-init.cc`         | Kernel initialization                |
| `k-lock.hh`         | Kernel spinlock                      |
| `k-vmiter.hh/cc`    | Page table iterators                 |
| `k-cpu.cc`          | Kernel `cpustate` type               |
| `k-proc.cc`         | Kernel `proc` type                   |
| `kernel.cc`         | Kernel exception handlers            |
| `k-memviewer.cc`    | Kernel memory viewer                 |
| `kernel.ld`         | Kernel linker script                 |

### Kernel libraries

| File                | Description                          |
| ------------------- | ------------------------------------ |
| `k-memrange.hh`     | Memory range type tracker            |
| `k-hardware.cc`     | General hardware access              |
| `k-devices.hh/cc`   | Keyboard, console, memory files      |
| `k-apic.hh/cc`      | Interrupt controller hardware        |
| `k-pci.hh`          | PCI bus hardware                     |
| `k-mpspec.cc`       | Boot-time configuration              |
| `k-sanitizers.cc`   | Sanitizer support                    |

### Processes

| File              | Description                                      |
| ----------------- | ------------------------------------------------ |
| `u-lib.cc/hh`     | Process library and system call implementations  |
| `p-allocator.cc`  | Allocator process                                |
| `process.ld`      | Process binary linker script                     |

### File system

| File                  | Description                                      |
| --------------------- | ------------------------------------------------ |
| `chickadeefs.hh`      | Defines chkfs (ChickadeeFS) layout               |
| `journalreplayer.cc`  | Logic for replaying chkfs journals               |

Build files
-----------

The main output of the build process is a disk image,
`chickadeeos.img`. QEMU “boots” off this disk image, but the image
could conceivably boot on real hardware! The build process also
produces other files that can be useful to examine.

| File                         | Description                          |
| ---------------------------- | ------------------------------------ |
| `obj/kernel.asm`             | Kernel assembly (with addresses)     |
| `obj/kernel.sym`             | Kernel defined symbols               |
| `obj/p-allocator.asm`, `sym` | Same for process binaries            |

[CS 161]: https://read.seas.harvard.edu/cs161/2020/
[triple fault]: https://en.wikipedia.org/wiki/Triple_fault
