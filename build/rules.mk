OBJDIR := obj
comma = ,
export LC_ALL = C

# Compiler toolchain
CCPREFIX ?=

ifeq ($(CCPREFIX),)
ifeq ($(origin CC),default)
CC      := $(shell build/findgcc.sh $(CC))
endif
ifeq ($(origin CXX),default)
CXX     := $(shell build/findgcc.sh $(CXX))
endif
else
CC      = $(CCPREFIX)cc
CXX     = $(CCPREFIX)c++
endif
LD      = $(CCPREFIX)ld
OBJCOPY = $(CCPREFIX)objcopy
OBJDUMP = $(CCPREFIX)objdump
NM      = $(CCPREFIX)nm
STRIP   = $(CCPREFIX)strip

# Native commands
HOSTCC  = cc
HOSTCXX = c++
TAR     = tar
PERL    = perl
HOSTCFLAGS := $(CFLAGS) -std=gnu11 -Wall -W
HOSTCXXFLAGS := $(CXXFLAGS) -std=gnu++1z -Wall -W

# Compiler flags
# -Os is required for the boot loader to fit within 512 bytes;
# -ffreestanding means there is no standard library.
CPPFLAGS := $(DEFS) -I.

CCOMMONFLAGS := -m64 -mno-mmx -mno-sse -mno-sse2 -mno-sse3 \
	-mno-3dnow -ffreestanding -fno-omit-frame-pointer -fno-pic \
	-fno-stack-protector \
	-Wall -W -Wshadow -Wno-format -Wno-unused-parameter

ASFLAGS := $(CCOMMONFLAGS)
ASFLAGS += $(shell $(CXX) -no-integrated-as -E -x c /dev/null >/dev/null 2>&1 && echo -no-integrated-as)
CFLAGS := $(CFLAGS) $(CCOMMONFLAGS) -std=gnu11 -gdwarf
CXXFLAGS := $(CXXFLAGS) $(CCOMMONFLAGS) -std=gnu++1z \
	-fno-exceptions -fno-rtti -gdwarf -ffunction-sections
DEPCFLAGS = -MD -MF $(DEPSDIR)/$(@F).d -MP

KERNELCXXFLAGS = $(CXXFLAGS) -mno-red-zone $(SANITIZEFLAGS)
ifeq ($(filter 1,$(SAN) $(UBSAN)),1)
KERNEL_OBJS += $(OBJDIR)/k-sanitizers.ko
KERNELCXXFLAGS += -DHAVE_SANITIZERS
SANITIZEFLAGS := -fsanitize=undefined -fsanitize=kernel-address
$(OBJDIR)/k-alloc.ko $(OBJDIR)/k-sanitizers.ko: SANITIZEFLAGS :=
endif

# Linker flags
LDFLAGS := $(LDFLAGS) -Os --gc-sections -z max-page-size=0x1000 \
	-static -nostdlib -nostartfiles
LDFLAGS	+= $(shell $(LD) -m elf_x86_64 --help >/dev/null 2>&1 && echo -m elf_x86_64)

QUIETOBJCOPY = sh build/quietobjcopy.sh $(OBJCOPY)


# Dependencies
DEPSDIR := .deps
BUILDSTAMP := $(DEPSDIR)/rebuildstamp
KERNELBUILDSTAMP := $(DEPSDIR)/krebuildstamp
DEPFILES := $(wildcard $(DEPSDIR)/*.d)
ifneq ($(DEPFILES),)
include $(DEPFILES)
endif

ifneq ($(DEP_CC),$(CC) $(CPPFLAGS) $(CFLAGS) $(DEPCFLAGS) $(O) _ $(LDFLAGS))
DEP_CC := $(shell mkdir -p $(DEPSDIR); echo >$(BUILDSTAMP); echo "DEP_CC:=$(CC) $(CPPFLAGS) $(CFLAGS) $(DEPCFLAGS) $(O) _ $(LDFLAGS)" >$(DEPSDIR)/_cc.d; echo "DEP_PREFER_GCC:=$(PREFER_GCC)" >>$(DEPSDIR)/_cc.d)
endif
ifneq ($(DEP_CXX),$(CXX) $(CPPFLAGS) $(DEPCFLAGS) $(CXXFLAGS) $(O) _ $(HOSTCXXFLAGS))
DEP_CXX := $(shell mkdir -p $(DEPSDIR); echo >$(BUILDSTAMP); echo "DEP_CXX:=$(CXX) $(CPPFLAGS) $(DEPCFLAGS) $(CXXFLAGS) $(O) _ $(HOSTCXXFLAGS)" >$(DEPSDIR)/_cxx.d)
endif
ifneq ($(DEP_KERNELCXX),$(CXX) $(CPPFLAGS) $(DEPCFLAGS) $(KERNELCXXFLAGS) $(O))
DEP_KERNELCXX := $(shell mkdir -p $(DEPSDIR); echo >$(KERNELBUILDSTAMP); echo "DEP_KERNELCXX:=$(CXX) $(CPPFLAGS) $(DEPCFLAGS) $(KERNELCXXFLAGS) $(O)" >$(DEPSDIR)/_kernelcxx.d)
endif

BUILDSTAMPS = $(OBJDIR)/stamp $(BUILDSTAMP)
KERNELBUILDSTAMPS = $(OBJDIR)/stamp $(KERNELBUILDSTAMP)

$(OBJDIR)/stamp $(BUILDSTAMP):
	$(call run,mkdir -p $(@D))
	$(call run,touch $@)

ifneq ($(strip $(INITFS_CONTENTS) $(INITFS_PARAMS)),$(DEP_INITFS_CONTENTS))
INITFS_BUILDSTAMP := $(shell echo "DEP_INITFS_CONTENTS:=$(strip $(INITFS_CONTENTS) $(INITFS_PARAMS))" > $(DEPSDIR)/_initfs.d; echo always)
endif

ifneq ($(strip $(DISKFS_CONTENTS)),$(DEP_DISKFS_CONTENTS))
DISKFS_BUILDSTAMP := $(shell echo "DEP_DISKFS_CONTENTS:=$(strip $(DISKFS_CONTENTS))" > $(DEPSDIR)/_diskfs.d; echo always)
endif


# Qemu emulator
QEMU ?= qemu-system-x86_64
QEMUCONSOLE ?= $(if $(or $(DISPLAY),$(filter Darwin,$(shell uname))),,1)
QEMUDISPLAY ?= $(if $(filter 1 y yes,$(QEMUCONSOLE)),console,graphic)

$(OBJDIR)/libqemu-nograb.so.1: build/qemu-nograb.c
	$(call run,mkdir -p $(@D))
	-$(call run,$(HOSTCC) -fPIC -shared -Wl$(comma)-soname$(comma)$(@F) -ldl -o $@ $<)

ifeq ($(origin QEMU_PRELOAD_LIBRARY),undefined)
ifneq ($(strip $(shell uname)),Darwin)
QEMU_PRELOAD_LIBRARY = $(OBJDIR)/libqemu-nograb.so.1
endif
endif

ifneq ($(QEMU_PRELOAD_LIBRARY),)
QEMU_PRELOAD = $(shell if test -r $(QEMU_PRELOAD_LIBRARY); then echo LD_PRELOAD=$(QEMU_PRELOAD_LIBRARY); fi)
endif


# Run the emulator
check-qemu-console:
	@if test -z "$$(which $(QEMU) 2>/dev/null)"; then \
	    echo 1>&2; echo "***" 1>&2; \
	    echo "*** Cannot run $(QEMU). You may not have installed it yet." 1>&2; \
	    if test -x /usr/bin/apt-get; then \
	        cmd="apt-get -y install"; else cmd="yum install -y"; fi; \
	    if test $$(whoami) = jharvard; then \
	        echo "*** I am going to try to install it for you." 1>&2; \
	        echo "***" 1>&2; echo 1>&2; \
	        echo sudo $$cmd qemu-system-x86; \
	        sudo $$cmd qemu-system-x86 || exit 1; \
	    else echo "*** Try running this command to install it:" 1>&2; \
	        echo sudo $$cmd qemu-system-x86 1>&2; \
	        echo 1>&2; exit 1; fi; \
	else :; fi

check-qemu: $(QEMU_PRELOAD_LIBRARY) check-qemu-console


# Delete the build
clean:
	$(call run,rm -rf $(DEPSDIR) $(OBJDIR) *.img core *.core,CLEAN)

realclean: clean
	$(call run,rm -rf $(DISTDIR)-$(USER).tar.gz $(DISTDIR)-$(USER))

distclean: realclean
	@:


# Boilerplate
always:
	@:

# These targets don't correspond to files
.PHONY: all always clean realclean distclean cleanfs fsck \
	run run-graphic run-console run-monitor \
	run-gdb run-gdb-graphic run-gdb-console run-gdb-report \
	check-qemu-console check-qemu kill \
	run-% run-graphic-% run-console-% run-monitor-% \
	run-gdb-% run-gdb-graphic-% run-gdb-console-%

# Eliminate default suffix rules
.SUFFIXES:

# Keep intermediate files
.SECONDARY:

# Delete target files if there is an error (or make is interrupted)
.DELETE_ON_ERROR:

# But no intermediate .o files should be deleted
.PRECIOUS: %.o $(OBJDIR)/%.o $(OBJDIR)/%.full $(OBJDIR)/bootsector
