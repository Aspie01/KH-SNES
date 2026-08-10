# The ARM7 core

**This is devkitPro's template, copied, not code this project wrote.**

The DS's second processor samples the touchscreen and the buttons over SPI and
pushes them across the FIFO, reads the touchscreen calibration out of firmware,
and services sound and power. The port has no custom ARM7 work: without this the
pen does nothing and `touchRead()` returns zeros, and that is the whole of what
it is for.

Some libnds versions ship a prebuilt core and `platform/ds/Makefile` prefers it
when it finds one. This directory is the fallback for the versions that do not —
`$DEVKITPRO/libnds` on one install holds nothing but `include/`, `lib/` and
three licence files.

## If it does not compile

Do not fix it by hand. It is a copy, and a copy that has drifted from the libnds
it is compiled against produces undeclared-function errors that look like a
broken toolchain. Take a fresh one:

```sh
cp $DEVKITPRO/examples/nds/templates/combined/arm7/source/*.c \
   platform/ds/arm7/source/
```

A hand-written version of this file was tried first and rejected by the first
real compiler with five undeclared functions — `inputGetAndSend`,
`readUserSettings`, `irqInit`, `fifoInit`, `installSystemFIFO` — none of which
appear anywhere in that install's headers. Writing it from memory is guessing at
somebody else's API, which `platform/ds/device/mmio.h` refuses at length for the
same reason.
