# Initialize compiler flags so `config.mk` can augment them

# Set up dependency files
DEPCFLAGS = -MD -MF $(DEPSDIR)/$(@F).d -MP

# Flags for building programs that run on the host (not in Chickadee)
HOSTCPPFLAGS = $(DEFS) -I.
HOSTCFLAGS := -std=gnu23 $(CFLAGS) -Wall -W
HOSTCXXFLAGS := -std=gnu++23 $(CXXFLAGS) -Wall -W

# Flags for building Chickadee kernel and process code
# preprocessor flags
CPPFLAGS = $(DEFS) -I.
# flags common to C and C++, and to kernel and user code
CCOMMONFLAGS := -m64 -mno-mmx -mno-sse -mno-sse2 -mno-sse3 \
	-mno-3dnow -ffreestanding -fno-pic -fno-stack-protector \
	-Wall -W -Wshadow -Wno-format -Wno-unused-parameter
# flags for C
CFLAGS := -std=gnu23 $(CCOMMONFLAGS) $(CFLAGS)
# flags for C++
CXXFLAGS := -std=gnu++23 -fno-exceptions -fno-rtti -ffunction-sections \
       $(CXXFLAGS) $(CCOMMONFLAGS) $(CXXFLAGS)
# flags for debuggability (not used in boot loader)
DEBUGFLAGS := -gdwarf-4 -fno-omit-frame-pointer -fno-optimize-sibling-calls \
       -mno-omit-leaf-frame-pointer
# flags for kernel sources
KERNELCXXFLAGS := $(CXXFLAGS) -mno-red-zone $(DEBUGFLAGS) $(SANITIZEFLAGS)
# assembler flags
ASFLAGS := $(CCOMMONFLAGS)
# linker flags
LDFLAGS := $(LDFLAGS) -Os --gc-sections -z max-page-size=0x1000 \
	-z noexecstack -static -nostdlib
