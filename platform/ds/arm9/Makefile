# The ARM9: the whole game, plus every asset the pipeline emits.
#
# THIS IS devkitPro's OWN nds/templates/arm9/Makefile, DIVERGING IN FOUR PLACES
# AND NOWHERE ELSE.  It was written from memory first, against libnds 1.x, and
# every detail that memory got wrong cost a round trip against a real toolchain:
# an ARCH that still carried -mthumb-interwork, a missing bin2o rule, an ndstool
# invocation that bypassed Calico, and an ARM7 nobody writes any more.  So this
# file now tracks the installed template, and the four differences are marked
# DIVERGENCE so the next person can diff it against a newer one and see at a
# glance what is ours.
#
# LIBNDS 2.0 RUNS ON CALICO, which is the thing that made the old shape wrong
# rather than merely dated.  $(DEVKITARM)/ds_rules supplies the ARM7 itself
# (-7 $(CALICO)/bin/ds7_maine.elf), rewrites -specs=ds_arm9.specs into Calico's
# own, and adds -lcalico_ds9 whenever -lnds9 is present.  A build that calls
# ndstool by hand gets none of that.  Let ds_rules make the .nds.
#
#   DIVERGENCE 1  SOURCES and INCLUDES reach OUT of this directory, to
#                 ../source and ../device.  The port is laid out by TIER, not by
#                 processor: the simulation is shared with the host build and
#                 the device tier is shared with nothing, and copying either
#                 into arm9/source/ would duplicate the thing both builds exist
#                 to compile one copy of.
#   DIVERGENCE 2  DATA points at assets/gen/ds, so bin2o turns every .bin there
#                 into an object with a `<name>_bin` symbol.  That is how a
#                 table reaches a cartridge with no filesystem; main.cpp
#                 declares the symbols and tools/check_link.py checks the names
#                 against the files without needing a compiler.
#   DIVERGENCE 3  C++17, which include/fixed.h needs and which the host build
#                 uses.  Compiling the device at a different standard from the
#                 one the tests pass on is the single difference that must not
#                 exist.
#   DIVERGENCE 4  OUTPUT is the PARENT directory, so the cartridge lands at
#                 platform/ds/kh.nds rather than inside arm9/.
#
# Every object basename must be unique across SOURCES -- the template flattens
# them with $(notdir) into one build directory.  source/, ../source/ and
# ../device/ share no basename today, and platform/ds/host/Makefile.host has the
# same constraint, so a collision would break both builds rather than only this.

.SUFFIXES:

ifeq ($(strip $(DEVKITARM)),)
$(error DEVKITARM is not set; see platform/ds/Makefile)
endif

include $(DEVKITARM)/ds_rules

# THE SHELL AND MAKE MUST BE THE SAME MSYS2.  make can see $(DEVKITARM) -- the
# include above proves it -- so a shell that cannot is a shell from another
# installation, which on Windows means Git Bash rather than devkitPro's own
# MSYS2.  platform/ds/Makefile explains what that does to a build; this copy is
# for anyone who runs this makefile directly.
ifeq ($(shell test -d '$(DEVKITARM)' && echo yes),)
$(error this shell cannot see $(DEVKITARM), but make can -- use devkitPro's own \
MSYS2 shell, not Git Bash.  platform/ds/Makefile explains why)
endif

TARGET   := kh
BUILD    := build
SOURCES  := source ../source ../device        # DIVERGENCE 1
INCLUDES := ../include ../device              # DIVERGENCE 1
DATA     := ../../../assets/gen/ds            # DIVERGENCE 2

#---------------------------------------------------------------------------
# Code generation, verbatim from the template.
#
# NO -mthumb AND NO -mthumb-interwork: libnds 1.x's template carried both and
# 2.0's carries neither.  Left in by memory, they are the kind of difference
# that links and then behaves oddly, so they are called out here rather than
# quietly dropped.
#
# -fno-exceptions and -fno-rtti are the template's AND the port's: see
# platform/ds/README.md, where they are a contract rather than a preference,
# and platform/ds/host/Makefile.host, which passes the same two.
#---------------------------------------------------------------------------
ARCH := -march=armv5te -mtune=arm946e-s

CFLAGS   := -g -Wall -O2 -ffunction-sections -fdata-sections\
            $(ARCH) $(INCLUDE) -DARM9
CXXFLAGS := $(CFLAGS) -std=c++17 -fno-rtti -fno-exceptions   # DIVERGENCE 3
ASFLAGS  := -g $(ARCH)
LDFLAGS   = -specs=ds_arm9.specs -g $(ARCH) -Wl,-Map,$(notdir $*.map)

# ds_rules turns -lnds9 into -lnds9 -lcalico_ds9 and points the specs at
# Calico's, so this stays exactly what the template says.
LIBS    := -lnds9
LIBDIRS := $(LIBNDS) $(PORTLIBS)

#---------------------------------------------------------------------------
ifneq ($(BUILD),$(notdir $(CURDIR)))
#---------------------------------------------------------------------------

# DIVERGENCE 4: the cartridge belongs beside the makefile that documents it,
# not inside the processor's build directory.
export OUTPUT := $(abspath $(CURDIR)/..)/$(TARGET)

export VPATH := $(foreach dir,$(SOURCES),$(CURDIR)/$(dir))\
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

export INCLUDE  := $(foreach dir,$(INCLUDES),-iquote $(CURDIR)/$(dir))\
                   $(foreach dir,$(LIBDIRS),-I$(dir)/include)\
                   -I$(CURDIR)/$(BUILD)
export LIBPATHS := $(foreach dir,$(LIBDIRS),-L$(dir)/lib)

.PHONY: $(BUILD) clean

#---------------------------------------------------------------------------
$(BUILD):
	@mkdir -p $@
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

#---------------------------------------------------------------------------
clean:
	@echo clean ...
	@rm -fr $(BUILD) $(OUTPUT).elf $(OUTPUT).nds $(OUTPUT).map

#---------------------------------------------------------------------------
else
#---------------------------------------------------------------------------
# The .nds is ds_rules' own %.nds: %.elf rule, which is where the ARM7 and the
# icon come from.  Naming it as the default goal is all this has to do.
#---------------------------------------------------------------------------
$(OUTPUT).nds: $(OUTPUT).elf $(GAME_ICON)
$(OUTPUT).elf: $(OFILES)

# Source files wait for the generated bin2o headers.
$(OFILES_SOURCES): $(HFILES)

#---------------------------------------------------------------------------
# THE bin2o RULE IS THE TEMPLATE'S, NOT ds_rules'.  Leaving it out is what makes
# a build fail with "No rule to make target 'armorpal.bin.o'" -- the DATA glob
# names 78 objects and nothing knows how to produce them.
#---------------------------------------------------------------------------
%.bin.o %_bin.h : %.bin
#---------------------------------------------------------------------------
	@echo $(notdir $<)
	@$(bin2o)

-include $(DEPSDIR)/*.d

#---------------------------------------------------------------------------
endif
#---------------------------------------------------------------------------
