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

# ...and the same reasoning for the toolchain variables, which did not reach the
# SECOND pass on their own: this makefile re-reads itself inside $(BUILD), and
# that read hit its own DEVKITARM guard.  Named on the recursion and exported,
# for the two different ways the automatic route can fail.  platform/ds/Makefile
# carries the full note.
export DEVKITPRO
export DEVKITARM

SUBMAKE := $(MAKE) --no-print-directory \
           DEVKITPRO='$(DEVKITPRO)' DEVKITARM='$(DEVKITARM)' MAKE='$(MAKE)'

# WHERE THE COMPILER PUTS ITS TEMPORARY FILES.
#
# devkitARM is a native Windows gcc.  It asks Windows for a temp directory, and
# Windows answers C:\WINDOWS\ when TMP and TEMP are both unset -- a directory no
# ordinary user may write to.  The build then dies with
#
#     Cannot create temporary file in C:\WINDOWS\: Permission denied
#
# on the first source file, which reads like a broken toolchain and is nothing of
# the sort.  Reported from a devkitPro MSYS2 shell where those variables were not
# set; it is the fourth thing on that install to arrive at a child process empty.
#
# So the build says where, rather than asking.  $(CURDIR) is the build directory
# in the pass that compiles, which is guaranteed to exist and be writable because
# make just made it.  gcc's temporaries are deleted as it goes.
#
# THE PATH HAS TO BE IN WINDOWS FORM.  A native gcc handed /home/... would take
# it as \home\... on whatever the current drive is.  cygpath -m gives the drive
# letter with forward slashes, which Windows accepts and which survives being
# pasted into a command line.  On a real Unix there is no cygpath and no problem,
# and the plain path is right.
KH_TMPDIR := $(CURDIR)
ifneq ($(shell command -v cygpath >/dev/null 2>&1 && echo yes),)
KH_TMPDIR := $(shell cygpath -m '$(CURDIR)')
endif
export TMPDIR := $(KH_TMPDIR)
export TMP    := $(KH_TMPDIR)
export TEMP   := $(KH_TMPDIR)

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


# EVERYTHING THE SECOND PASS NEEDS, NAMED ON ITS COMMAND LINE.
#
# devkitPro's template hands these over by `export`, i.e. through the
# environment, and on one devkitPro MSYS2 install the environment does not
# survive make spawning make: the second pass came back with "No targets.
# Stop.", which is what make says when it can find no default goal -- because
# $(OUTPUT) had arrived EMPTY and the only rule in that pass reads
# `$(OUTPUT).elf : $(OFILES)`, whose target then begins with a dot and is
# therefore skipped when choosing one.  A cryptic message for a lost variable.
#
# It is the same failure that made an exported DEVKITARM invisible and then lost
# the command-line copy one level down, so the same answer: say it out loud on
# the recursion, where nothing can drop it.  The `export`s above stay too --
# they cost nothing and they are what works everywhere else.
HANDOVER := OUTPUT='$(OUTPUT)' DEPSDIR='$(DEPSDIR)' VPATH='$(VPATH)' \
            OFILES='$(OFILES)' OFILES_BIN='$(OFILES_BIN)' \
            OFILES_SOURCES='$(OFILES_SOURCES)' HFILES='$(HFILES)' \
            INCLUDE='$(INCLUDE)' LIBPATHS='$(LIBPATHS)' LD='$(LD)'

.PHONY: $(BUILD) clean

$(BUILD):
	@[ -d $@ ] || mkdir -p $@
	@$(SUBMAKE) -C $(BUILD) -f $(CURDIR)/Makefile $(HANDOVER)

clean:
	@rm -fr $(BUILD) $(OUTPUT).elf $(OUTPUT).map

#---------------------------------------------------------------------------
else
#---------------------------------------------------------------------------

# ...and if it arrives empty anyway, say SO rather than "No targets".
#
# This pass defines exactly one rule, and its target is built from $(OUTPUT).
# With OUTPUT empty that target is ".elf", make skips dot-leading targets when
# picking a default goal, and the whole build stops with a message that names
# nothing.  Two lines to turn that into the actual problem.
ifeq ($(strip $(OUTPUT)),)
$(error the second pass received no OUTPUT.  The first pass hands it over on the \
recursion AND by export, so both routes failed -- see the HANDOVER note above)
endif

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
