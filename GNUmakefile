IMAGE = chickadeeos.img
all: $(IMAGE)

# Place local configuration options, such as `CC=clang`, in
# `config.mk` so you don't have to list them every time.
-include config.mk

# `$(V)` controls whether the lab makefiles print verbose commands (the
# actual shell commands run by Make) or brief commands (like `COMPILE`).
# For brief commands, run `make all`.
# For verbose commands, run `make V=1 all`.
V = 0
ifeq ($(V),1)
compile = $(CC) $(CPPFLAGS) $(CFLAGS) $(DEPCFLAGS) $(1)
cxxcompile = $(CXX) $(CPPFLAGS) $(CXXFLAGS) $(DEPCFLAGS) $(1)
assemble = $(CC) $(CPPFLAGS) $(ASFLAGS) $(DEPCFLAGS) $(1)
link = $(LD) $(LDFLAGS) $(1)
run = $(1) $(3)
else
compile = @/bin/echo " " $(2) && $(CC) $(CPPFLAGS) $(CFLAGS) $(DEPCFLAGS) $(1)
cxxcompile = @/bin/echo " " $(2) && $(CXX) $(CPPFLAGS) $(CXXFLAGS) $(DEPCFLAGS) $(1)
assemble = @/bin/echo " " $(2) && $(CC) $(CPPFLAGS) $(ASFLAGS) $(DEPCFLAGS) $(1)
link = @/bin/echo " " $(2) $(patsubst %.full,%,$@) && $(LD) $(LDFLAGS) $(1)
run = @$(if $(2),/bin/echo " " $(2) $(3) &&,) $(1) $(3)
endif

# `$(D)` controls how QEMU responds to faults. Run `make D=1 run` to
# ask QEMU to print debugging information about interrupts and CPU resets,
# and to quit after the first triple fault instead of rebooting.
#
# `$(NCPU)` controls the number of CPUs QEMU should use. It defaults to 2.
NCPU = 2
QEMUOPT = -net none -parallel file:log.txt -smp $(NCPU)
ifneq ($(D),)
QEMUOPT += -d int,cpu_reset -no-reboot
endif

-include build/rules.mk


# Sets of object files

BOOT_OBJS = $(OBJDIR)/bootentry.o $(OBJDIR)/boot.o

KERNEL_OBJS = $(OBJDIR)/k-exception.ko \
	$(OBJDIR)/kernel.ko $(OBJDIR)/k-alloc.ko $(OBJDIR)/k-vmiter.ko \
	$(OBJDIR)/k-init.ko $(OBJDIR)/k-hardware.ko \
	$(OBJDIR)/k-cpu.ko $(OBJDIR)/k-proc.ko \
	$(OBJDIR)/k-memviewer.ko $(OBJDIR)/lib.ko

PROCESS_LIB_OBJS = $(OBJDIR)/lib.o $(OBJDIR)/p-lib.o
PROCESS_OBJS = $(PROCESS_LIB_OBJS) \
	$(OBJDIR)/p-allocator.o \
	$(OBJDIR)/p-allocexit.o \
	$(OBJDIR)/p-testmsleep.o \
	$(OBJDIR)/p-testppid.o

FLATFS_CONTENTS = obj/p-allocator \
	obj/p-allocexit \
	obj/p-testmsleep \
	obj/p-testppid


# Define `CHICKADEE_FIRST_PROCESS` if appropriate
ifneq ($(filter run-%,$(MAKECMDGOALS)),)
ifeq ($(words $(MAKECMDGOALS)),1)
RUNCMD_LASTWORD := $(lastword $(subst -, ,$(MAKECMDGOALS)))
ifneq ($(filter obj/p-$(RUNCMD_LASTWORD),$(FLATFS_CONTENTS)),)
CPPFLAGS += -DCHICKADEE_FIRST_PROCESS='"p-$(RUNCMD_LASTWORD)"'
DEFAULTIMAGE = $(IMAGE)
$(OBJDIR)/kernel.ko: always
endif
endif
endif


# How to make object files

$(PROCESS_OBJS): $(OBJDIR)/%.o: %.cc $(BUILDSTAMPS)
	$(call cxxcompile,-O1 -DCHICKADEE_PROCESS -c $< -o $@,COMPILE $<)

$(OBJDIR)/%.ko: %.cc $(BUILDSTAMPS)
	$(call cxxcompile,-O2 -DCHICKADEE_KERNEL -mcmodel=kernel -c $< -o $@,COMPILE $<)

$(OBJDIR)/%.ko: %.S $(OBJDIR)/k-asm.h $(BUILDSTAMPS)
	$(call assemble,-O2 -mcmodel=kernel -c $< -o $@,ASSEMBLE $<)

$(OBJDIR)/boot.o: $(OBJDIR)/%.o: boot.cc $(BUILDSTAMPS)
	$(call cxxcompile,-Os -fomit-frame-pointer -c $< -o $@,COMPILE $<)

$(OBJDIR)/bootentry.o: $(OBJDIR)/%.o: \
	bootentry.S $(OBJDIR)/k-asm.h $(BUILDSTAMPS)
	$(call assemble,-Os -fomit-frame-pointer -c $< -o $@,ASSEMBLE $<)


# How to make supporting source files

$(OBJDIR)/k-asm.h: kernel.hh build/mkkernelasm.awk $(BUILDSTAMPS)
	$(call cxxcompile,-dM -E kernel.hh | awk -f build/mkkernelasm.awk | sort > $@,CREATE $@)
	@if test ! -s $@; then echo '* Error creating $@!' 1>&2; exit 1; fi

$(OBJDIR)/k-flatfs.c: \
	build/mkflatfs.awk $(FLATFS_CONTENTS) $(BUILDSTAMPS) GNUmakefile
	$(call run,echo $(FLATFS_CONTENTS) | awk -f build/mkflatfs.awk >,CREATE,$@)

$(OBJDIR)/k-proc.ko: $(OBJDIR)/k-flatfs.c


# How to make binaries and disk images

$(OBJDIR)/kernel.full: $(KERNEL_OBJS) $(FLATFS_CONTENTS) kernel.ld
	$(call link,-T kernel.ld -o $@ $(KERNEL_OBJS) -b binary $(FLATFS_CONTENTS),LINK)
	@if $(OBJDUMP) -p $@ | grep off | grep -iv 'off[ 0-9a-fx]*000 ' >/dev/null 2>&1; then echo "* Warning: Some sections of kernel object file are not page-aligned." 1>&2; fi

$(OBJDIR)/p-%.full: $(OBJDIR)/p-%.o $(PROCESS_LIB_OBJS) process.ld
	$(call link,-T process.ld -o $@ $< $(PROCESS_LIB_OBJS),LINK)

$(OBJDIR)/%: $(OBJDIR)/%.full
	$(call run,$(OBJDUMP) -C -S -j .lowtext -j .text -j .ctors $< >$@.asm)
	$(call run,$(NM) -n $< >$@.sym)
	$(call run,$(OBJCOPY) -j .lowtext -j .lowdata -j .text -j .rodata -j .data -j .bss -j .ctors -j .init_array $<,STRIP,$@)

$(OBJDIR)/bootsector: $(BOOT_OBJS) boot.ld
	$(call link,-T boot.ld -o $@.full $(BOOT_OBJS),LINK)
	$(call run,$(OBJDUMP) -C -S $@.full >$@.asm)
	$(call run,$(NM) -n $@.full >$@.sym)
	$(call run,$(OBJCOPY) -S -O binary -j .text $@.full $@)

$(OBJDIR)/mkbootdisk: build/mkbootdisk.c $(BUILDSTAMPS)
	$(call run,$(HOSTCC) -I. -o $(OBJDIR)/mkbootdisk,HOSTCOMPILE,build/mkbootdisk.c)

chickadeeos.img: $(OBJDIR)/mkbootdisk $(OBJDIR)/bootsector $(OBJDIR)/kernel
	$(call run,$(OBJDIR)/mkbootdisk $(OBJDIR)/bootsector $(OBJDIR)/kernel > $@,CREATE $@)


DEFAULTIMAGE ?= %.img
run-%: run-qemu-%
	@:
run-qemu-%: run-$(QEMUDISPLAY)-%
	@:
run-graphic-%: $(DEFAULTIMAGE) check-qemu
	$(call run,$(QEMU_PRELOAD) $(QEMU) $(QEMUOPT) $(QEMUIMG),QEMU $<)
run-console-%: $(DEFAULTIMAGE) check-qemu
	$(call run,$(QEMU_PRELOAD) $(QEMU) $(QEMUOPT) -curses $(QEMUIMG),QEMU $<)
run-monitor-%: $(DEFAULTIMAGE) check-qemu
	$(call run,$(QEMU_PRELOAD) $(QEMU) $(QEMUOPT) -monitor stdio $(QEMUIMG),QEMU $<)
run-gdb-%: run-gdb-$(QEMUDISPLAY)-%
	@:
run-gdb-graphic-%: $(DEFAULTIMAGE) check-qemu
	$(call run,$(QEMU_PRELOAD) $(QEMU) $(QEMUOPT) -gdb tcp::1234 $(QEMUIMG) &,QEMU $<)
	$(call run,sleep 0.5; gdb -x build/chickadee.gdb,GDB)
run-gdb-console-%: $(DEFAULTIMAGE) check-qemu
	$(call run,$(QEMU_PRELOAD) $(QEMU) $(QEMUOPT) -curses -gdb tcp::1234 $(QEMUIMG),QEMU $<)

run: run-qemu-$(basename $(IMAGE))
run-qemu: run-qemu-$(basename $(IMAGE))
run-graphic: run-graphic-$(basename $(IMAGE))
run-console: run-console-$(basename $(IMAGE))
run-monitor: run-monitor-$(basename $(IMAGE))
run-gdb: run-gdb-$(basename $(IMAGE))
run-gdb-graphic: run-gdb-graphic-$(basename $(IMAGE))
run-gdb-console: run-gdb-console-$(basename $(IMAGE))
run-graphic-gdb: run-gdb-graphic-$(basename $(IMAGE))
run-console-gdb: run-gdb-console-$(basename $(IMAGE))


# Kill all my qemus
kill:
	-killall -u $$(whoami) $(QEMU)
	@sleep 0.2; if ps -U $$(whoami) | grep $(QEMU) >/dev/null; then killall -9 -u $$(whoami) $(QEMU); fi
