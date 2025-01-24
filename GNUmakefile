QEMUIMAGEFILES = chickadeeboot.img chickadeefs.img
all: $(QEMUIMAGEFILES) obj/chickadeefsck $(GDBFILES)
include build/flags.mk

# Place local configuration options, such as `CC=clang`, in
# `config.mk` so you don't have to list them every time.
CONFIG ?= config.mk
-include $(CONFIG)

# `$(V)` controls whether the makefiles print verbose commands (the shell
# commands run by Make) or brief commands (like `COMPILE`).
# For brief commands, run `make all`.
# For verbose commands, run `make V=1 all`.
V = 0
ifeq ($(V),1)
cxxcompile = $(CXX) $(CPPFLAGS) $(DEPCFLAGS) $(1)
assemble = $(CXX) $(CPPFLAGS) $(DEPCFLAGS) $(ASFLAGS) $(1)
link = $(LD) $(LDFLAGS) $(1)
run = $(1) $(3)
else
cxxcompile = @/bin/echo " " $(2) && $(CXX) $(CPPFLAGS) $(DEPCFLAGS) $(1)
assemble = @/bin/echo " " $(2) && $(CXX) $(CPPFLAGS) $(DEPCFLAGS) $(ASFLAGS) $(1)
link = @/bin/echo " " $(2) $(patsubst %.full,%,$@) && $(LD) $(LDFLAGS) $(1)
run = @$(if $(2),/bin/echo " " $(2) $(3) &&,) $(1) $(3)
endif

# `$(D)` controls how QEMU responds to faults. Run `make D=1 run` to
# ask QEMU to print debugging information about interrupts and CPU resets,
# and to quit after the first triple fault instead of rebooting.
#
# `$(NCPU)` controls the number of CPUs QEMU should use. It defaults to 2.
NCPU = 2
LOG ?= file:log.txt
QEMUOPT = -net none -parallel $(LOG) -smp $(NCPU)
ifeq ($(D),1)
QEMUOPT += -d int,cpu_reset,guest_errors -no-reboot -D qemu.log
else ifeq ($(D),2)
QEMUOPT += -d guest_errors -no-reboot -D qemu.log
endif
ifneq ($(NOGDB),1)
QEMUGDB ?= -gdb tcp::12949
endif
ifeq ($(STOP),1)
QEMUOPT += -S
endif

QEMURANDSEED := $(strip $(shell od -N8 -tu8 -An /dev/urandom))


# Object files

BOOT_OBJS = $(OBJDIR)/bootentry.o $(OBJDIR)/boot.o

KERNEL_OBJS = $(OBJDIR)/k-exception.ko \
	$(OBJDIR)/kernel.ko $(OBJDIR)/k-cpu.ko $(OBJDIR)/k-proc.ko \
	$(OBJDIR)/k-alloc.ko $(OBJDIR)/k-vmiter.ko $(OBJDIR)/k-devices.ko \
	$(OBJDIR)/k-init.ko $(OBJDIR)/k-hardware.ko $(OBJDIR)/k-mpspec.ko \
	$(OBJDIR)/lib.ko $(OBJDIR)/crc32c.ko \
	$(OBJDIR)/k-ahci.ko $(OBJDIR)/k-chkfs.ko $(OBJDIR)/k-chkfsiter.ko \
	$(OBJDIR)/k-memviewer.ko $(OBJDIR)/k-testwait.ko \
	$(OBJDIR)/k-initfs.ko

# Add your own kernel object files, if any, here:


# CHICKADEE_FIRST_PROCESS

RUNCMD_LASTWORD := $(filter run-%,$(MAKECMDGOALS))
ifeq ($(words $(RUNCMD_LASTWORD)),1)
RUNCMD_LASTWORD := $(lastword $(subst -, ,$(RUNCMD_LASTWORD)))
ifneq ($(filter p-$(RUNCMD_LASTWORD).cc,$(wildcard p-*.cc)),)
CHICKADEE_FIRST_PROCESS := $(RUNCMD_LASTWORD)
endif
endif
CHICKADEE_FIRST_PROCESS ?= allocator


FIND_PROCESSES_OPTIONS := -v MIN=$(MIN) -v CHICKADEE_FIRST_PROCESS=$(CHICKADEE_FIRST_PROCESS)
INIT_PROCESSES := $(shell awk $(FIND_PROCESSES_OPTIONS) -v DISK=0 -f build/findprocesses.awk p-*.cc)
DISK_PROCESSES := $(shell awk $(FIND_PROCESSES_OPTIONS) -v DISK=1 -f build/findprocesses.awk p-*.cc)

PROCESS_LIB_OBJS = $(OBJDIR)/lib.uo $(OBJDIR)/u-lib.uo $(OBJDIR)/crc32c.uo


# File system contents

INITFS_CONTENTS := \
	$(shell find initfs -type f -not -name '\#*\#' -not -name '*~' 2>/dev/null) \
	$(patsubst %,obj/%,$(INIT_PROCESSES))

INITFS_PARAMS ?=
ifneq ($(HALT),)
INITFS_PARAMS += .halt="$(HALT)"
endif

DISKFS_CONTENTS := \
	$(shell find initfs -type f -not -name '\#*\#' -not -name '*~' 2>/dev/null) \
	$(shell find diskfs -type f -not -name '\#*\#' -not -name '*~' 2>/dev/null) \
	$(patsubst %,obj/%,$(DISK_PROCESSES))


-include build/rules.mk


ifneq ($(strip $(CHICKADEE_FIRST_PROCESS)),$(DEP_CHICKADEE_FIRST_PROCESS))
FIRST_PROCESS_BUILDSTAMP := $(shell echo "DEP_CHICKADEE_FIRST_PROCESS:=$(CHICKADEE_FIRST_PROCESS)" > $(DEPSDIR)/_first_process.d)
$(OBJDIR)/k-firstprocess.h: always
$(OBJDIR)/firstprocess.gdb: always
else ifeq ($(wildcard $(OBJDIR)/k-firstprocess.h),)
$(OBJDIR)/k-firstprocess.h: always
$(OBJDIR)/firstprocess.gdb: always
endif
$(OBJDIR)/kernel.ko: $(OBJDIR)/k-firstprocess.h


# How to make object files

$(OBJDIR)/%.ko: %.cc $(KERNELBUILDSTAMPS)
	$(call cxxcompile,$(O) $(KERNELCXXFLAGS) -DCHICKADEE_KERNEL -mcmodel=kernel -c $< -o $@,COMPILE $<)

$(OBJDIR)/%.ko: %.S $(OBJDIR)/k-asm.h $(KERNELBUILDSTAMPS)
	$(call assemble,$(O) -mcmodel=kernel -c $< -o $@,ASSEMBLE $<)

$(OBJDIR)/boot.o: $(OBJDIR)/%.o: boot.cc $(KERNELBUILDSTAMPS)
	$(call cxxcompile,-Os $(CXXFLAGS) $(DEBUGFLAGS) -c $< -o $@,COMPILE $<)

$(OBJDIR)/bootentry.o: $(OBJDIR)/%.o: \
	bootentry.S $(OBJDIR)/k-asm.h $(KERNELBUILDSTAMPS)
	$(call assemble,-Os -c $< -o $@,ASSEMBLE $<)

$(OBJDIR)/%.uo: %.cc $(BUILDSTAMPS)
	$(call cxxcompile,$(O) $(CXXFLAGS) $(DEBUGFLAGS) -DCHICKADEE_PROCESS -c $< -o $@,COMPILE $<)

$(OBJDIR)/%.uo: %.S $(OBJDIR)/u-asm.h $(BUILDSTAMPS)
	$(call assemble,$(O) -c $< -o $@,ASSEMBLE $<)


# How to make supporting source files

$(OBJDIR)/k-asm.h: $(KERNELBUILDSTAMPS)
	$(call run,$(k_asm_h_input_command) | $(asm_h_build_command) > $@,CREATE $@)
	@if test ! -s $@; then echo '* Error creating $@!' 1>&2; exit 1; fi

$(OBJDIR)/u-asm.h: $(BUILDSTAMPS)
	$(call run,$(u_asm_h_input_command) | $(asm_h_build_command) > $@,CREATE $@)
	@if test ! -s $@; then echo '* Error creating $@!' 1>&2; exit 1; fi

$(OBJDIR)/k-firstprocess.h:
	$(call run,echo '#ifndef CHICKADEE_FIRST_PROCESS' >$@; echo '#define CHICKADEE_FIRST_PROCESS "$(CHICKADEE_FIRST_PROCESS)"' >>$@; echo '#endif' >>$@,CREATE $@)

$(OBJDIR)/k-initfs.cc: build/mkinitfs.awk \
	$(INITFS_CONTENTS) $(INITFS_BUILDSTAMP) $(KERNELBUILDSTAMPS)
	$(call run,echo $(INITFS_CONTENTS) $(INITFS_PARAMS) | awk -f build/mkinitfs.awk >,CREATE,$@)

$(OBJDIR)/k-initfs.ko: $(OBJDIR)/k-initfs.cc
	$(call cxxcompile,$(KERNELCXXFLAGS) -O2 -DCHICKADEE_KERNEL -mcmodel=kernel -c $< -o $@,COMPILE $<)

$(OBJDIR)/firstprocess.gdb:
	$(call run,echo "add-symbol-file obj/p-$(CHICKADEE_FIRST_PROCESS).full 0x100000" > $@,CREATE $@)


# How to make binaries and the boot sector

$(OBJDIR)/kernel.full: $(KERNEL_OBJS) $(INITFS_CONTENTS) kernel.ld
	$(call link,-T kernel.ld -z noexecstack -o $@ $(KERNEL_OBJS) -b binary $(INITFS_CONTENTS),LINK)

$(OBJDIR)/p-%.full: $(OBJDIR)/p-%.uo $(PROCESS_LIB_OBJS) process.ld
	$(call link,-T process.ld -o $@ $< $(PROCESS_LIB_OBJS),LINK)

$(OBJDIR)/kernel: $(OBJDIR)/kernel.full $(OBJDIR)/mkchickadeesymtab
	$(call run,$(OBJDUMP) -C -S -j .lowtext -j .text -j .ctors $< >$@.asm)
	$(call run,$(NM) -n $< >$@.sym)
	$(call run,$(OBJCOPY) -j .lowtext -j .lowdata -j .text -j .rodata -j .data -j .bss -j .ctors -j .init_array $<,STRIP,$@)
	@if $(OBJDUMP) -p $@ | grep off | grep -iv 'off[ 0-9a-fx]*000 ' >/dev/null 2>&1; then echo "* Warning: Some sections of kernel object file are not page-aligned." 1>&2; fi
	$(call run,$(OBJDIR)/mkchickadeesymtab $@)

$(OBJDIR)/%: $(OBJDIR)/%.full
	$(call run,$(OBJDUMP) -C -S -j .text -j .ctors $< >$@.asm)
	$(call run,$(NM) -n $< >$@.sym)
	$(call run,$(QUIETOBJCOPY) -j .text -j .rodata -j .data -j .bss -j .ctors -j .init_array $<,STRIP,$@)

$(OBJDIR)/bootsector: $(BOOT_OBJS) boot.ld
	$(call link,-T boot.ld -z noexecstack -o $@.full $(BOOT_OBJS),LINK)
	$(call run,$(OBJDUMP) -C -S $@.full >$@.asm)
	$(call run,$(NM) -n $@.full >$@.sym)
	$(call run,$(OBJCOPY) -S -O binary -j .text $@.full $@)


# How to make host program for ensuring a loaded symbol table

$(OBJDIR)/mkchickadeesymtab: build/mkchickadeesymtab.cc $(BUILDSTAMPS)
	$(call run,$(HOSTCXX) -O3 $(HOSTCPPFLAGS) $(HOSTCXXFLAGS) $(DEPCFLAGS) -g -o $@,HOSTCOMPILE,$<)


# How to make host programs for constructing & checking file systems

$(OBJDIR)/%.o: %.cc $(BUILDSTAMPS)
	$(call run,$(HOSTCXX) -O3 $(HOSTCPPFLAGS) $(HOSTCXXFLAGS) $(DEPCFLAGS) -c -o $@,HOSTCOMPILE,$<)

$(OBJDIR)/%.o: build/%.cc $(BUILDSTAMPS)
	$(call run,$(HOSTCXX) -O3 $(HOSTCPPFLAGS) $(HOSTCXXFLAGS) $(DEPCFLAGS) -c -o $@,HOSTCOMPILE,$<)

$(OBJDIR)/mkchickadeefs: build/mkchickadeefs.cc $(BUILDSTAMPS)
	$(call run,$(HOSTCXX) -O3 $(HOSTCPPFLAGS) $(HOSTCXXFLAGS) $(DEPCFLAGS) -g -o $@,HOSTCOMPILE,$<)

CHICKADEEFSCK_OBJS = $(OBJDIR)/chickadeefsck.o \
	$(OBJDIR)/journalreplayer.o \
	$(OBJDIR)/crc32c.o
$(OBJDIR)/chickadeefsck: $(CHICKADEEFSCK_OBJS) $(BUILDSTAMPS)
	$(call run,$(HOSTCXX) -O3 $(HOSTCPPFLAGS) $(HOSTCXXFLAGS) $(DEPCFLAGS) $(CHICKADEEFSCK_OBJS) -o,HOSTLINK,$@)


# How to make disk images

# If you change the `-f` argument, also change `boot.cc:KERNEL_START_SECTOR`
chickadeeboot.img: $(OBJDIR)/mkchickadeefs $(OBJDIR)/bootsector $(OBJDIR)/kernel
	$(call run,$(OBJDIR)/mkchickadeefs -b 4096 -f 16 -s $(OBJDIR)/bootsector $(OBJDIR)/kernel > $@,CREATE $@)

chickadeefs.img: $(OBJDIR)/mkchickadeefs \
	$(OBJDIR)/bootsector $(OBJDIR)/kernel $(DISKFS_CONTENTS) \
	$(DISKFS_BUILDSTAMP)
	$(call run,$(OBJDIR)/mkchickadeefs -b 32768 -f 16 -j 64 -s $(OBJDIR)/bootsector $(OBJDIR)/kernel $(DISKFS_CONTENTS) > $@,CREATE $@)

cleanfs:
	$(call run,rm -f chickadeefs.img,RM chickadeefs.img)

fsck: chickadeefs.img $(OBJDIR)/chickadeefsck
	$(call run,$(OBJDIR)/chickadeefsck $< && echo "* File system OK",FSCK $<)


# How to run QEMU

QEMUIMG = -M q35 \
	-device piix4-ide,bus=pcie.0,id=piix4-ide \
	-drive file=chickadeeboot.img,if=none,format=raw,id=bootdisk \
	-device ide-hd,drive=bootdisk,bus=piix4-ide.0 \
	-drive file=chickadeefs.img,if=none,format=raw,id=maindisk \
	-device ide-hd,drive=maindisk,bus=ide.0 \
	-device loader,addr=0xff8,data=$(QEMURANDSEED),data-len=8

run: run-$(QEMUDISPLAY)
	@:
run-gdb-report:
	@if test "$(QEMUGDB)" = "-gdb tcp::12949"; then echo '* Run `gdb -ix build/chickadee.gdb` to connect gdb to qemu.' 1>&2; fi
run-graphic: $(QEMUIMAGEFILES) $(GDBFILES) check-qemu run-gdb-report
	$(call run,$(QEMU_PRELOAD) $(QEMU) $(QEMUOPT) $(QEMUGDB) $(QEMUIMG),QEMU $<)
run-console: $(QEMUIMAGEFILES) $(GDBFILES) check-qemu-console run-gdb-report
	$(call run,$(QEMU) $(QEMUOPT) -display curses $(QEMUGDB) $(QEMUIMG),QEMU $<)
run-monitor: $(QEMUIMAGEFILES) $(GDBFILES) check-qemu
	$(call run,$(QEMU_PRELOAD) $(QEMU) $(QEMUOPT) -monitor stdio $(QEMUIMG),QEMU $<)
run-gdb: run-gdb-$(QEMUDISPLAY)
	@:
run-gdb-graphic: $(QEMUIMAGEFILES) $(GDBFILES) check-qemu
	$(call run,$(QEMU_PRELOAD) $(QEMU) $(QEMUOPT) -gdb tcp::12949 $(QEMUIMG) &,QEMU $<)
	$(call run,sleep 0.5; gdb -ix build/chickadee.gdb,GDB)
run-gdb-console: $(QEMUIMAGEFILES) $(GDBFILES) check-qemu-console
	$(call run,$(QEMU) $(QEMUOPT) -display curses -gdb tcp::12949 $(QEMUIMG),QEMU $<)

ifneq ($(filter p-$(RUNCMD_LASTWORD),$(INIT_PROCESSES)),)
RUNSUFFIX := $(RUNCMD_LASTWORD)
endif

run-$(RUNSUFFIX): run
run-graphic-$(RUNSUFFIX): run-graphic
run-console-$(RUNSUFFIX): run-console
run-monitor-$(RUNSUFFIX): run-monitor
run-gdb-$(RUNSUFFIX): run-gdb
run-gdb-graphic-$(RUNSUFFIX): run-gdb-graphic
run-gdb-console-$(RUNSUFFIX): run-gdb-console

# Stop all my qemus
stop kill:
	-killall -u $$(whoami) $(QEMU)
	@sleep 0.2; if ps -U $$(whoami) | grep $(QEMU) >/dev/null; then killall -9 -u $$(whoami) $(QEMU); fi
