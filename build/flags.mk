# Initialize compiler flags so `config.mk` can augment them

# Set up dependency files
DEPCFLAGS = -MD -MF $(DEPSDIR)/$(@F).d -MP

# Flags for building programs that run on the host (not in Chickadee)
HOSTCPPFLAGS = $(DEFS) -I.
HOSTCFLAGS := $(CFLAGS) -std=gnu2x -Wall -W
HOSTCXXFLAGS := $(CXXFLAGS) -std=gnu++2a -Wall -W

# Flags for building Chickadee kernel and process code
CPPFLAGS = $(DEFS) -I.

CCOMMONFLAGS := -m64 -mno-mmx -mno-sse -mno-sse2 -mno-sse3 \
       -mno-3dnow -ffreestanding -fno-omit-frame-pointer -fno-pic \
       -fno-stack-protector \
       -Wall -W -Wshadow -Wno-format -Wno-unused-parameter

CFLAGS := $(CFLAGS) $(CCOMMONFLAGS) -std=gnu2x -gdwarf-4

CXXFLAGS := $(CXXFLAGS) $(CCOMMONFLAGS) -std=gnu++2a \
       -fno-exceptions -fno-rtti -gdwarf-4 -ffunction-sections
KERNELCXXFLAGS := $(CXXFLAGS) -mno-red-zone $(SANITIZEFLAGS)

ASFLAGS := $(CCOMMONFLAGS)

LDFLAGS := $(LDFLAGS) -Os --gc-sections -z max-page-size=0x1000 \
	-static -nostdlib
