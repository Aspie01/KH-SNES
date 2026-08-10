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

# `define`, not `:=`: $(error) prints its argument verbatim and make does not
# interpret a backslash-n inside one, so a multi-line message has to be real
# lines or it arrives as one long string with literal escapes in it.
define KH_WRONG_SHELL
this shell cannot see $(DEVKITARM), but make can.

  That means make and the shell come from DIFFERENT MSYS2 installations -- the
  usual cause on Windows is running the build from Git Bash, which finds
  devkitPro's make on the Windows PATH while providing its own runtime, root
  and mount table.  Nothing downstream of that works reliably: paths, exported
  variables and the compiler's temp directory all disagree across the boundary.

  Use devkitPro's own MSYS2 shell instead:

      Start Menu -> devkitPro -> MSYS2      (or C:\devkitPro\msys2\msys2_shell.cmd)
      cd /c/Users/<you>/path/to/KH-SNES     ( /home there is NOT Git Bash's /home )
      make -C platform/ds

  `which make` should print /usr/bin/make in that shell, not a /c/... path.
  On Linux or macOS this check cannot fire; there is only one runtime.
endef

ifeq ($(strip $(DEVKITARM)),)
$(error DEVKITARM is not set.  Install devkitPro and export DEVKITARM, or run \
        python3 tools/check_device.py to be told exactly what is missing)
endif
ifeq ($(strip $(DEVKITPRO)),)
$(error DEVKITPRO is not set.  ndstool lives under $$DEVKITPRO/tools/bin)
endif

# ---------------------------------------------------------------------------
# THE SHELL AND MAKE MUST BE THE SAME MSYS2, and on Windows they easily are not.
#
# devkitPro's installer puts its own MSYS2 at C:\devkitPro\msys2 and adds
# C:\devkitPro\msys2\usr\bin to the WINDOWS path.  So devkitPro's make, bash,
# python and the rest are all reachable from Git Bash -- which is a DIFFERENT
# MSYS2, with its own msys-2.0.dll, its own root and its own mount table.  Run
# the build there and you get devkitPro's make driving Git Bash's shell, and the
# two disagree about what the filesystem looks like.
#
# It does not fail cleanly.  It fails as four unrelated-looking things, all of
# which were chased separately before the cause was found:
#
#   * make opens $(DEVKITARM)/ds_rules happily -- its runtime has the
#     /opt/devkitpro mount -- and then the bash it spawns says a path under that
#     same mount does not exist, naming a file that plainly does;
#   * exported variables do not survive the boundary, so DEVKITARM looks unset
#     to make even though `env` lists it;
#   * the build-directory pass gets none of the template's exported hand-over
#     and stops with "No targets", naming nothing;
#   * gcc inherits no TMP and asks Windows, which answers C:\WINDOWS\, which it
#     may not write to.
#
# Every one of those has a plausible local workaround, and every workaround is
# wrong, because the build is simply being run from the wrong shell.  So this is
# ONE CHECK INSTEAD OF FOUR FIXES.
#
# It works by asking THE SHELL something make already knows: whether $(DEVKITARM)
# is a directory.  Make can see it -- the include above succeeded, or we would
# not be here -- so a shell that cannot is a shell from another installation.
# ---------------------------------------------------------------------------
ifeq ($(shell test -d '$(DEVKITARM)' && echo yes),)
$(error $(KH_WRONG_SHELL))
endif

TARGET  := kh
NDSTOOL := $(DEVKITPRO)/tools/bin/ndstool

.PHONY: all arm7 arm9 clean

all: $(TARGET).nds

# PHONY, and both of them, because the targets share a name with the
# directories they build in -- without this make finds the directory, decides it
# is up to date, and never descends.
arm7:
	@$(MAKE) --no-print-directory -C arm7

arm9:
	@$(MAKE) --no-print-directory -C arm9

# No banner and no icon.  `-b` takes a bitmap and a three-line title, and
# inventing box art for something that is not being distributed is inventing
# work; a homebrew loader shows its own placeholder.
$(TARGET).nds: arm7 arm9
	$(NDSTOOL) -c $@ -9 arm9/arm9.elf -7 arm7/arm7.elf
	@echo "built $@"

clean:
	@$(MAKE) --no-print-directory -C arm7 clean
	@$(MAKE) --no-print-directory -C arm9 clean
	@rm -f $(TARGET).nds
