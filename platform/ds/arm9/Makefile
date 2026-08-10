# The ARM9: the whole game, plus every asset the pipeline emits.
#
# devkitPro's standard single-target template, with three things changed and
# each of them is worth naming:
#
#   SOURCES reaches OUT of this directory, to ../source and ../device.  The port
#   is laid out by TIER and not by processor -- the simulation is shared with
#   the host build and the device tier is shared with nothing -- and duplicating
#   either into arm9/source/ would be duplicating the thing both builds are
#   supposed to be compiling one copy of.
#
#   DATA points at assets/gen/ds.  ds_rules runs bin2s over every file there and
#   emits one object per .bin with a `<name>_bin` symbol, which is how a table
#   gets into a cartridge that has no filesystem.  arm9/source/main.cpp declares
#   those symbols; a .bin the pipeline did not emit is a link error naming it.
#
#   C++17, because include/fixed.h needs it and because the host build uses it.
#   A device build on an older standard would be compiling different code from
#   the one the tests pass on, which is the one difference that must not exist.
#
# Every object basename must be unique across SOURCES -- ds_rules flattens them
# with $(notdir) into one build directory.  source/, ../source/ and ../device/
# share no basename today and platform/ds/host/Makefile.host has the same
# constraint, so a collision would break both builds at once rather than only
# this one.

.SUFFIXES:

ifeq ($(strip $(DEVKITARM)),)
$(error DEVKITARM is not set; see platform/ds/Makefile)
endif

include $(DEVKITARM)/ds_rules

# THE SAME $(MAKE) GUARD THE TOP-LEVEL MAKEFILE CARRIES, and it is repeated here
# rather than inherited because inheriting it did not work.  The top level
# passes `MAKE=<corrected>` as a command-line assignment, which is the form that
# is supposed to propagate -- and on devkitPro's MSYS2 this make still recursed
# into its build directory through /opt/devkitpro/msys2/usr/bin/make and died
# with error 127.  Measured.  So each makefile checks for itself; three copies
# of a mechanical test are cheaper than one clever propagation that fails
# silently one level down.  The reasoning is in platform/ds/Makefile.
#
# AFTER the ds_rules include on purpose: $(shell) runs under $(SHELL), ds_rules
# sets SHELL, and the point of the test is to use the same shell the RECIPE will
# use.  Testing under /bin/sh a path that /usr/bin/bash cannot exec would be a
# check that passes and a build that fails.
ifneq ($(findstring /,$(MAKE)),)
ifeq ($(shell $(SHELL) -c "'$(MAKE)' --version" >/dev/null 2>&1 && echo ok),)
override MAKE := $(notdir $(MAKE))
endif
endif

TARGET   := $(notdir $(CURDIR))
BUILD    := build
SOURCES  := source ../source ../device
INCLUDES := ../include ../device source
DATA     := ../../../assets/gen/ds

#---------------------------------------------------------------------------
# The ARM946E-S.  -march=armv5te is the instruction set and -mtune says which
# implementation to schedule for; getting the first wrong is a link failure
# against libnds's multilib, and getting the second wrong is merely slower.
#
# -fno-exceptions and -fno-rtti are NOT an optimisation.  platform/ds/README.md
# calls them the contract -- "these are not preferences" -- and the host build
# passes the same two, so code that compiles there compiles here.
#---------------------------------------------------------------------------
ARCH     := -mthumb -mthumb-interwork

CFLAGS   := -g -Wall -O2 \
            -march=armv5te -mtune=arm946e-s \
            -fomit-frame-pointer -ffast-math \
            $(ARCH) $(INCLUDE) -DARM9
CXXFLAGS := $(CFLAGS) -std=c++17 -fno-rtti -fno-exceptions
ASFLAGS  := -g $(ARCH)
LDFLAGS   = -specs=ds_arm9.specs -g $(ARCH) -Wl,-Map,$(notdir $*.map)

LIBS     := -lnds9
LIBDIRS  := $(LIBNDS)

#---------------------------------------------------------------------------
ifneq ($(BUILD),$(notdir $(CURDIR)))
#---------------------------------------------------------------------------

export OUTPUT  := $(CURDIR)/$(TARGET)
export VPATH   := $(foreach dir,$(SOURCES),$(CURDIR)/$(dir)) \
                  $(foreach dir,$(DATA),$(CURDIR)/$(dir))
export DEPSDIR := $(CURDIR)/$(BUILD)

CFILES   := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
CPPFILES := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cpp)))
SFILES   := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.s)))
BINFILES := $(foreach dir,$(DATA),$(notdir $(wildcard $(dir)/*.bin)))

export LD := $(CXX)

export OFILES_BIN     := $(addsuffix .o,$(BINFILES))
export OFILES_SOURCES := $(CPPFILES:.cpp=.o) $(CFILES:.c=.o) $(SFILES:.s=.o)
export OFILES         := $(OFILES_BIN) $(OFILES_SOURCES)
export HFILES         := $(addsuffix .h,$(subst .,_,$(BINFILES)))

export INCLUDE  := $(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) \
                   $(foreach dir,$(LIBDIRS),-I$(dir)/include) \
                   -I$(CURDIR)/$(BUILD)
export LIBPATHS := $(foreach dir,$(LIBDIRS),-L$(dir)/lib)

.PHONY: $(BUILD) clean

$(BUILD):
	@[ -d $@ ] || mkdir -p $@
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

clean:
	@rm -fr $(BUILD) $(OUTPUT).elf $(OUTPUT).map

#---------------------------------------------------------------------------
else
#---------------------------------------------------------------------------

DEPENDS := $(OFILES:.o=.d)

# The ELF, and nothing else: the .nds is the top-level Makefile's, because it
# needs the ARM7's half too.
$(OUTPUT).elf : $(OFILES)

# Every source waits for every generated bin2s header.  Coarser than it needs to
# be -- only main.cpp includes them, and it declares its own symbols rather than
# including them at all -- and kept anyway, because the alternative is a
# dependency list that has to be right and this one cannot be wrong.
$(OFILES_SOURCES) : $(HFILES)

-include $(DEPENDS)

#---------------------------------------------------------------------------
endif
#---------------------------------------------------------------------------
