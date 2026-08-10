# The device build.  Produces platform/ds/kh.nds.
#
#     make -C platform/ds
#
# REQUIRES devkitPro with libnds 2.0 or later.  tools/check_device.py names
# every missing piece; run it first if this fails in a way that mentions a
# compiler.  Nothing here is needed by the rest of the port -- the host tier is
# most of the port and all of the risk, and it builds and tests with the system
# g++ (platform/ds/host/Makefile.host).
#
# THERE IS ONE PROCESSOR TO COMPILE, NOT TWO.  The DS has two, and older
# devkitPro layouts had you build both; libnds 2.0 runs on Calico and supplies
# the ARM7 itself -- $(DEVKITARM)/ds_rules passes
# `-7 $(CALICO)/bin/ds7_maine.elf` to ndstool without being asked, and
# devkitPro's own combined template ships an EMPTY arm7/source directory to make
# the point.  This tree had an arm7/ with a hand-written core in it for exactly
# as long as it took a real toolchain to reject it; see the git history if the
# question ever comes back.
#
# ds_rules also builds the cartridge.  Its `%.nds: %.elf` rule adds the ARM7,
# the default icon and the banner text, rewrites -specs=ds_arm9.specs into
# Calico's own, and appends -lcalico_ds9 to the link.  A makefile that ran
# ndstool by hand -- as this one did -- would silently get none of it, so this
# one delegates to arm9/ and stays out of the way.
#
# THE ASSETS ARE LINKED IN, NOT LOADED.  arm9/Makefile points DATA at
# assets/gen/ds and bin2o turns every .bin there into an object carrying a
# `<name>_bin` symbol, which is how a table reaches a machine with no
# filesystem.  So the pipeline must have been run first:
#
#     python3 tools/build_assets.py
#     python3 tools/check_link.py      # the symbols, checked without a compiler
#     make -C platform/ds
#
# A missing .bin is a link error naming the symbol, which is the right failure.

.SUFFIXES:

ifeq ($(strip $(DEVKITARM)),)
$(error DEVKITARM is not set.  Put it on make's command line, which needs no \
environment at all -- make -C platform/ds DEVKITARM=/opt/devkitpro/devkitARM \
DEVKITPRO=/opt/devkitpro -- or export it.  python3 tools/check_device.py names \
every missing piece)
endif
ifeq ($(strip $(DEVKITPRO)),)
$(error DEVKITPRO is not set or not exported)
endif

# ---------------------------------------------------------------------------
# THE SHELL AND MAKE MUST BE THE SAME MSYS2, and on Windows they easily are not.
#
# devkitPro's installer puts its own MSYS2 at C:\devkitPro\msys2 and adds
# C:\devkitPro\msys2\usr\bin to the WINDOWS path, so devkitPro's make, bash and
# python are all reachable from Git Bash -- a DIFFERENT MSYS2, with its own
# msys-2.0.dll, root and mount table.  Run the build there and you get
# devkitPro's make driving Git Bash's shell, and the two disagree about what the
# filesystem is.
#
# It does not fail cleanly.  It fails as four unrelated-looking things: a path
# make can open that bash says does not exist; exported variables vanishing, so
# DEVKITARM looks unset while `env` lists it; the build-directory pass losing
# its hand-over and stopping with "No targets"; and gcc inheriting no TMP and
# asking Windows, which answers C:\WINDOWS\.  Each has a plausible local
# workaround and every one of them is wrong.
#
# So: one check instead of four fixes.  It asks THE SHELL something make already
# knows -- whether $(DEVKITARM) is a directory -- because the include in
# arm9/Makefile proves make can see it.  It cannot fire where there is only one
# runtime.
# ---------------------------------------------------------------------------
define KH_WRONG_SHELL
this shell cannot see $(DEVKITARM), but make can.

  That means make and the shell come from DIFFERENT MSYS2 installations -- the
  usual cause on Windows is running the build from Git Bash, which finds
  devkitPro's make on the Windows PATH while supplying its own runtime, root
  and mount table.

  Use devkitPro's own MSYS2 shell instead:

      Start Menu -> devkitPro -> MSYS2      (or C:\devkitPro\msys2\msys2_shell.cmd)
      cd /c/Users/<you>/path/to/KH-SNES     ( /home there is NOT Git Bash's /home )
      make -C platform/ds

  `which make` should print /usr/bin/make in that shell, not a /c/... path.
  Only make needs it: git, Python and an editor can be anywhere pointed at the
  same directory.  On Linux or macOS this check cannot fire.
endef

ifeq ($(shell test -d '$(DEVKITARM)' && echo yes),)
$(error $(KH_WRONG_SHELL))
endif

TARGET := kh

.PHONY: all clean

# arm9/ produces $(TARGET).nds in THIS directory -- see DIVERGENCE 4 there --
# so there is nothing to copy and nothing to wrap afterwards.
all:
	@$(MAKE) --no-print-directory -C arm9

clean:
	@$(MAKE) --no-print-directory -C arm9 clean
