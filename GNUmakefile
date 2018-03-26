QEMUIMAGEFILES = chickadeeboot.img chickadeefs.img
all: $(QEMUIMAGEFILES)

# Place local configuration options, such as `CC=clang`, in
# `config.mk` so you don't have to list them every time.
-include config.mk

# `$(V)` controls whether the lab makefiles print verbose commands (the
# actual shell commands run by Make) or brief commands (like `COMPILE`).
# For brief commands, run `make all`.
# For verbose commands, run `make V=1 all`.
V = 0
ifeq ($(V),1)
cxxcompile = $(CXX) $(CPPFLAGS) $(DEPCFLAGS) $(1)
assemble = $(CC) $(CPPFLAGS) $(DEPCFLAGS) $(ASFLAGS) $(1)
link = $(LD) $(LDFLAGS) $(1)
run = $(1) $(3)
else
cxxcompile = @/bin/echo " " $(2) && $(CXX) $(CPPFLAGS) $(DEPCFLAGS) $(1)
assemble = @/bin/echo " " $(2) && $(CC) $(CPPFLAGS) $(DEPCFLAGS) $(ASFLAGS) $(1)
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
ifneq ($(D),)
QEMUOPT += -d int,cpu_reset -no-reboot
endif


# Sets of object files

BOOT_OBJS = $(OBJDIR)/bootentry.o $(OBJDIR)/boot.o

KERNEL_OBJS = $(OBJDIR)/k-exception.ko \
	$(OBJDIR)/kernel.ko $(OBJDIR)/k-alloc.ko $(OBJDIR)/k-vmiter.ko \
	$(OBJDIR)/k-init.ko $(OBJDIR)/k-hardware.ko $(OBJDIR)/k-mpspec.ko \
	$(OBJDIR)/k-devices.ko $(OBJDIR)/k-cpu.ko $(OBJDIR)/k-proc.ko \
	$(OBJDIR)/crc32c.ko $(OBJDIR)/k-chkfs.ko \
	$(OBJDIR)/k-memviewer.ko $(OBJDIR)/lib.ko

PROCESS_LIB_OBJS = $(OBJDIR)/lib.o $(OBJDIR)/p-lib.o
PROCESS_OBJS = $(PROCESS_LIB_OBJS) \
	$(OBJDIR)/p-allocator.o \
	$(OBJDIR)/p-allocexit.o \
	$(OBJDIR)/p-cat.o \
	$(OBJDIR)/p-echo.o \
	$(OBJDIR)/p-execallocexit.o \
	$(OBJDIR)/p-exececho.o \
	$(OBJDIR)/p-false.o \
	$(OBJDIR)/p-readdiskfile.o \
	$(OBJDIR)/p-sh.o \
	$(OBJDIR)/p-testeintr.o \
	$(OBJDIR)/p-testmemfs.o \
	$(OBJDIR)/p-testmsleep.o \
	$(OBJDIR)/p-testpipe.o \
	$(OBJDIR)/p-testppid.o \
	$(OBJDIR)/p-testrwaddr.o \
	$(OBJDIR)/p-testvfs.o \
	$(OBJDIR)/p-testwaitpid.o \
	$(OBJDIR)/p-testzombie.o \
	$(OBJDIR)/p-true.o \
	$(OBJDIR)/p-wc.o \
	$(OBJDIR)/p-wcdiskfile.o

INITFS_CONTENTS = $(shell find initfs -type f -not -name '\#*\#' -not -name '*~' 2>/dev/null) \
	obj/p-allocator \
	obj/p-allocexit \
	obj/p-cat \
	obj/p-echo \
	obj/p-execallocexit \
	obj/p-exececho \
	obj/p-false \
	obj/p-readdiskfile \
	obj/p-sh \
	obj/p-testeintr \
	obj/p-testmemfs \
	obj/p-testmsleep \
	obj/p-testpipe \
	obj/p-testppid \
	obj/p-testrwaddr \
	obj/p-testvfs \
	obj/p-testwaitpid \
	obj/p-testzombie \
	obj/p-true \
	obj/p-wc \
	obj/p-wcdiskfile

DISKFS_CONTENTS = $(shell find diskfs -type f -not -name '\#*\#' -not -name '*~' 2>/dev/null) \
	$(INITFS_CONTENTS)


-include build/rules.mk


# Define `CHICKADEE_FIRST_PROCESS` if appropriate
ifneq ($(filter run-%,$(MAKECMDGOALS)),)
ifeq ($(words $(MAKECMDGOALS)),1)
RUNCMD_LASTWORD := $(lastword $(subst -, ,$(MAKECMDGOALS)))
ifneq ($(filter obj/p-$(RUNCMD_LASTWORD),$(INITFS_CONTENTS)),)
RUNSUFFIX := $(RUNCMD_LASTWORD)
CHICKADEE_FIRST_PROCESS := $(RUNCMD_LASTWORD)
CPPFLAGS += -DCHICKADEE_FIRST_PROCESS='"$(CHICKADEE_FIRST_PROCESS)"'
endif
endif
endif

CHICKADEE_FIRST_PROCESS ?= allocator
ifneq ($(strip $(CHICKADEE_FIRST_PROCESS)),$(DEP_CHICKADEE_FIRST_PROCESS))
FIRST_PROCESS_BUILDSTAMP := $(shell echo "DEP_CHICKADEE_FIRST_PROCESS:=$(CHICKADEE_FIRST_PROCESS)" > $(DEPSDIR)/_first_process.d)
obj/kernel.ko: always
endif


# How to make object files

$(PROCESS_OBJS): $(OBJDIR)/%.o: %.cc $(BUILDSTAMPS)
	$(call cxxcompile,$(CXXFLAGS) -O1 -DCHICKADEE_PROCESS -c $< -o $@,COMPILE $<)

$(OBJDIR)/%.ko: %.cc $(KERNELBUILDSTAMPS)
	$(call cxxcompile,$(KERNELCXXFLAGS) -O2 -DCHICKADEE_KERNEL -mcmodel=kernel -c $< -o $@,COMPILE $<)

$(OBJDIR)/%.ko: %.S $(OBJDIR)/k-asm.h $(KERNELBUILDSTAMPS)
	$(call assemble,-O2 -mcmodel=kernel -c $< -o $@,ASSEMBLE $<)

$(OBJDIR)/boot.o: $(OBJDIR)/%.o: boot.cc $(KERNELBUILDSTAMPS)
	$(call cxxcompile,$(CXXFLAGS) -Os -fomit-frame-pointer -c $< -o $@,COMPILE $<)

$(OBJDIR)/bootentry.o: $(OBJDIR)/%.o: \
	bootentry.S $(OBJDIR)/k-asm.h $(KERNELBUILDSTAMPS)
	$(call assemble,-Os -fomit-frame-pointer -c $< -o $@,ASSEMBLE $<)


# How to make supporting source files

$(OBJDIR)/k-asm.h: kernel.hh build/mkkernelasm.awk $(KERNELBUILDSTAMPS)
	$(call cxxcompile,-dM -E kernel.hh | awk -f build/mkkernelasm.awk | sort > $@,CREATE $@)
	@if test ! -s $@; then echo '* Error creating $@!' 1>&2; exit 1; fi

$(OBJDIR)/k-initfs.cc: build/mkinitfs.awk \
	$(INITFS_CONTENTS) $(INITFS_BUILDSTAMP) $(KERNELBUILDSTAMPS)
	$(call run,echo $(INITFS_CONTENTS) | awk -f build/mkinitfs.awk >,CREATE,$@)

$(OBJDIR)/k-devices.ko: $(OBJDIR)/k-initfs.cc


# How to make binaries and the boot sector

$(OBJDIR)/kernel.full: $(KERNEL_OBJS) $(INITFS_CONTENTS) kernel.ld
	$(call link,-T kernel.ld -o $@ $(KERNEL_OBJS) -b binary $(INITFS_CONTENTS),LINK)

$(OBJDIR)/p-%.full: $(OBJDIR)/p-%.o $(PROCESS_LIB_OBJS) process.ld
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
	$(call run,$(OBJCOPY) -j .text -j .rodata -j .data -j .bss -j .ctors -j .init_array $<,STRIP,$@)

$(OBJDIR)/bootsector: $(BOOT_OBJS) boot.ld
	$(call link,-T boot.ld -o $@.full $(BOOT_OBJS),LINK)
	$(call run,$(OBJDUMP) -C -S $@.full >$@.asm)
	$(call run,$(NM) -n $@.full >$@.sym)
	$(call run,$(OBJCOPY) -S -O binary -j .text $@.full $@)


# How to make host program for ensuring a loaded symbol table

$(OBJDIR)/mkchickadeesymtab: build/mkchickadeesymtab.cc $(BUILDSTAMPS)
	$(call run,$(HOSTCXX) $(CPPFLAGS) $(HOSTCXXFLAGS) $(DEPCFLAGS_AT) -g -o $@,HOSTCOMPILE,$<)


# How to make host programs for constructing & checking file systems

$(OBJDIR)/%.o: %.cc $(BUILDSTAMPS)
	$(call run,$(HOSTCXX) $(CPPFLAGS) $(HOSTCXXFLAGS) $(DEPCFLAGS_AT) -c -o $@,HOSTCOMPILE,$<)

$(OBJDIR)/%.o: build/%.cc $(BUILDSTAMPS)
	$(call run,$(HOSTCXX) $(CPPFLAGS) $(HOSTCXXFLAGS) $(DEPCFLAGS_AT) -c -o $@,HOSTCOMPILE,$<)

$(OBJDIR)/mkchickadeefs: build/mkchickadeefs.cc $(BUILDSTAMPS)
	$(call run,$(HOSTCXX) $(CPPFLAGS) $(HOSTCXXFLAGS) $(DEPCFLAGS_AT) -o $@,HOSTCOMPILE,$<)

CHICKADEEFSCK_OBJS = $(OBJDIR)/chickadeefsck.o \
	$(OBJDIR)/journalreplayer.o \
	$(OBJDIR)/crc32c.o
$(OBJDIR)/chickadeefsck: $(CHICKADEEFSCK_OBJS) $(BUILDSTAMPS)
	$(call run,$(HOSTCXX) $(CPPFLAGS) $(HOSTCXXFLAGS) $(DEPCFLAGS_AT) $(CHICKADEEFSCK_OBJS) -o,HOSTLINK,$@)


# How to make disk images

# If you change the `-f` argument, also change `boot.cc:KERNEL_START_SECTOR`
chickadeeboot.img: $(OBJDIR)/mkchickadeefs $(OBJDIR)/bootsector $(OBJDIR)/kernel
	$(call run,$(OBJDIR)/mkchickadeefs -b 4096 -f 16 -s $(OBJDIR)/bootsector $(OBJDIR)/kernel > $@,CREATE $@)

chickadeefs.img: $(OBJDIR)/mkchickadeefs \
	$(OBJDIR)/bootsector $(OBJDIR)/kernel $(DISKFS_CONTENTS) \
	$(DISKFS_BUILDSTAMP)
	$(call run,$(OBJDIR)/mkchickadeefs -b 32768 -f 16 -s $(OBJDIR)/bootsector $(OBJDIR)/kernel $(DISKFS_CONTENTS) > $@,CREATE $@)


# How to run QEMU

QEMUIMG = -M q35 \
        -device piix4-ide,bus=pcie.0,id=piix4-ide \
	-drive file=chickadeeboot.img,if=none,format=raw,id=bootdisk \
	-device ide-drive,drive=bootdisk,bus=piix4-ide.0 \
	-drive file=chickadeefs.img,if=none,format=raw,id=maindisk \
	-device ide-drive,drive=maindisk,bus=ide.0

run: run-$(QEMUDISPLAY)
	@:
run-graphic: $(QEMUIMAGEFILES) check-qemu
	@echo '* Run `gdb -x build/chickadee.gdb` to connect gdb to qemu.' 1>&2
	$(call run,$(QEMU_PRELOAD) $(QEMU) $(QEMUOPT) -gdb tcp::12949 $(QEMUIMG),QEMU $<)
run-console: $(QEMUIMAGEFILES) check-qemu
	@echo '* Run `gdb -x build/chickadee.gdb` to connect gdb to qemu.' 1>&2
	$(call run,$(QEMU_PRELOAD) $(QEMU) $(QEMUOPT) -curses -gdb tcp::12949 $(QEMUIMG),QEMU $<)
run-monitor: $(QEMUIMAGEFILES) check-qemu
	$(call run,$(QEMU_PRELOAD) $(QEMU) $(QEMUOPT) -monitor stdio $(QEMUIMG),QEMU $<)
run-gdb: run-gdb-$(QEMUDISPLAY)
	@:
run-gdb-graphic: $(QEMUIMAGEFILES) check-qemu
	$(call run,$(QEMU_PRELOAD) $(QEMU) $(QEMUOPT) -gdb tcp::12949 $(QEMUIMG) &,QEMU $<)
	$(call run,sleep 0.5; gdb -x build/chickadee.gdb,GDB)
run-gdb-console: $(QEMUIMAGEFILES) check-qemu
	$(call run,$(QEMU_PRELOAD) $(QEMU) $(QEMUOPT) -curses -gdb tcp::12949 $(QEMUIMG),QEMU $<)

run-$(RUNSUFFIX): run
run-graphic-$(RUNSUFFIX): run-graphic
run-console-$(RUNSUFFIX): run-console
run-monitor-$(RUNSUFFIX): run-monitor
run-gdb-$(RUNSUFFIX): run-gdb
run-gdb-graphic-$(RUNSUFFIX): run-gdb-graphic
run-gdb-console-$(RUNSUFFIX): run-gdb-console

# Kill all my qemus
kill:
	-killall -u $$(whoami) $(QEMU)
	@sleep 0.2; if ps -U $$(whoami) | grep $(QEMU) >/dev/null; then killall -9 -u $$(whoami) $(QEMU); fi
