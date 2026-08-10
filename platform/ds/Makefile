# The device build.  Produces platform/ds/kh.nds.
#
#     make -C platform/ds
#
# REQUIRES devkitPro.  tools/check_device.py is the gate and it names every
# missing piece; run that first if this fails in a way that mentions a compiler.
# Nothing in this file works without it and nothing in the rest of the port
# needs it -- the host tier is most of the port and all of the risk, and it
# builds and tests with the system g++ (platform/ds/host/Makefile.host).
#
# WHY THREE MAKEFILES.  The DS has two processors and they take different
# compilers: the ARM9 is an ARM946E-S (ARMv5TE) and the ARM7 an ARM7TDMI
# (ARMv4T), with different specs files and different libraries.  A .nds is the
# two ELFs wrapped in a cartridge header by ndstool.  devkitPro's own combined
# template has exactly this shape and this follows it rather than inventing one:
# a build layout that differs from the standard one differs in ways that surface
# as a link error nobody has seen before.
#
# THE ASSETS ARE LINKED IN, NOT LOADED.  arm9/Makefile points DATA at
# assets/gen/ds and devkitPro's ds_rules runs bin2s over every .bin there,
# turning each into an object with a `<name>_bin` symbol.  arm9/source/main.cpp
# declares those symbols and hands them to device/boot.cpp as Blobs -- which is
# the other half of the seam scene.h describes: "the two answers are 'read the
# file' on the host and 'link the .bin in' on the device."
#
# So the pipeline must have been run before this will build:
#
#     python3 tools/build_assets.py
#     make -C platform/ds
#
# A missing .bin is a LINK error naming the symbol, which is the right failure:
# there is no filesystem on a DS and a table that is not linked in cannot be
# found at run time.

.SUFFIXES:

# "NOT SET" HERE USUALLY MEANS "NOT EXPORTED", and the two look identical from
# the shell.  `echo $DEVKITARM` prints a plain shell variable just as happily as
# an exported one; make is a child process and only inherits the exported kind.
# So the message names that case first, because it is the one where the person
# reading it can see the value on their own screen and reasonably conclude the
# makefile is wrong.  Measured, on a devkitPro MSYS2 install.
# `define`, not `:=`, because $(error) prints its argument verbatim and make
# does NOT interpret a backslash-n inside one -- a help text written that way
# comes out as one long line with literal \n in it, which is worse than no help
# at all.  This was written the wrong way first and the difference was visible
# only by running it.
define KH_DEVKIT_HELP

  Set them for this shell -- note EXPORT, not merely assignment:
      export DEVKITPRO=/opt/devkitpro DEVKITARM=/opt/devkitpro/devkitARM
  ...or source devkitPro's own script, which also makes it stick:
      source /etc/profile.d/devkit-env.sh
  python3 tools/check_device.py names every missing piece
endef

ifeq ($(strip $(DEVKITARM)),)
$(error DEVKITARM is not set -- or is set and not EXPORTED, which make cannot see$(KH_DEVKIT_HELP))
endif
ifeq ($(strip $(DEVKITPRO)),)
$(error DEVKITPRO is not set or not exported; ndstool lives under $$DEVKITPRO/tools/bin$(KH_DEVKIT_HELP))
endif

TARGET  := kh
NDSTOOL := $(DEVKITPRO)/tools/bin/ndstool

# ---------------------------------------------------------------------------
# $(MAKE) CAN BE A PATH THAT MAKE'S OWN SHELL CANNOT EXECUTE.
#
# On devkitPro's MSYS2, /opt/devkitpro is a mount onto C:\devkitPro.  Make
# resolves the path to ITSELF back through that mount and $(MAKE) comes out as
#
#     /opt/devkitpro/msys2/usr/bin/make
#
# which the bash it then spawns reports as "No such file or directory" -- on a
# binary `which` finds perfectly well at /c/devkitPro/msys2/usr/bin/make, the
# same file by a different route.  Every recursion dies at the first sub-make
# with error 127, and the message accuses a file that exists.  Measured on a
# real install; it is why this block is here rather than in a bug report.
#
# TESTED, NOT ASSUMED.  The fallback fires only when $(MAKE) actually cannot
# run: if it contains a slash and `--version` fails through the same shell the
# recursion will use, the bare name is used instead, which PATH resolves -- and
# PATH is how make was invoked in the first place.  A caller who set MAKE
# deliberately to something that WORKS is left alone, which is the property that
# makes this safe to do unconditionally.
# ---------------------------------------------------------------------------
# `override`, because a plain assignment loses to a command-line MAKE= and this
# has to win even then: the guard only fires when the value cannot run, and a
# value that cannot run is not a preference worth respecting.  Written without
# it first, which printed the note and then went on using the broken path --
# visible only by running it with a deliberately bad MAKE.
ifneq ($(findstring /,$(MAKE)),)
ifeq ($(shell '$(MAKE)' --version >/dev/null 2>&1 && echo ok),)
KH_UNRUNNABLE_MAKE := $(MAKE)
override MAKE := $(notdir $(MAKE))
$(info note: $(KH_UNRUNNABLE_MAKE) cannot be executed by this shell; recursing \
as '$(MAKE)' through PATH instead -- see the note in platform/ds/Makefile)
endif
endif

# ...and the corrected value travels DOWN.  The sub-makefiles recurse again into
# their own build directories, so fixing it only here would move the failure one
# level deeper rather than removing it.  A command-line assignment is the form
# that propagates: it goes into MAKEFLAGS and survives every further level.
SUBMAKE := $(MAKE) --no-print-directory MAKE='$(MAKE)'

.PHONY: all arm7 arm9 clean

all: $(TARGET).nds

# PHONY, and both of them, because the targets share a name with the
# directories they build in -- without this make finds the directory, decides it
# is up to date, and never descends.
arm7:
	@$(SUBMAKE) -C arm7

arm9:
	@$(SUBMAKE) -C arm9

# No banner and no icon.  `-b` takes a bitmap and a three-line title, and
# inventing box art for something that is not being distributed is inventing
# work; a homebrew loader shows its own placeholder.
$(TARGET).nds: arm7 arm9
	$(NDSTOOL) -c $@ -9 arm9/arm9.elf -7 arm7/arm7.elf
	@echo "built $@"

clean:
	@$(SUBMAKE) -C arm7 clean
	@$(SUBMAKE) -C arm9 clean
	@rm -f $(TARGET).nds
