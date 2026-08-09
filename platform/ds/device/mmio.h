#pragma once
// The one seam between the device tier and the machine.
//
// WHY THIS EXISTS AND WHY IT IS NOT A LIBNDS STUB.  §M7 is blocked: devkitPro is
// not installed here and cannot be (tools/check_device.py is the gate).  The
// obvious way to write code anyway is to stub libnds -- declare videoSetMode(),
// vramSetBankA() and the rest, compile against them, and call it checked.  That
// is a trap, and a bad one: a stub is a CLAIM about somebody else's header, the
// claim is unverifiable while the real header is absent, and code that compiles
// against my declaration and not against theirs is a green light with nothing
// behind it.  Worse, it would be green in exactly the situation where nobody can
// tell -- which is the failure mode this whole project is built to refuse.
//
// So there is no libnds stub.  The initialisation below writes HARDWARE
// REGISTERS, and a hardware register is not somebody else's API: it is an
// address and a bit layout, quoted from GBATEK, and platform/ds/include/
// vram_map.h already transcribes and asserts the addresses that matter.  The
// only thing that has to differ between the device and the host is what a write
// to 0x04000240 DOES.
//
// That is this class.  Same header, same non-virtual signatures, two
// definitions:
//
//   mmio_device.cpp   a volatile store to the address.  Built only where
//                     devkitPro is, and containing nothing else, so there is
//                     almost nothing in it that can be wrong.
//   mmio_host.cpp     records the write.  This is what the host suite links,
//                     and it is what turns "the init programs the VRAM banks
//                     the way vram_map.h says" from a claim into a test.
//
// WHAT THIS BUYS AND WHAT IT DOES NOT.  It buys: the register VALUES are
// checked, the ORDER is checked, and the addresses NOT written are checked --
// which is how the WRAMCNT hazard (vram_map.h: 0x04000247 sits between
// VRAMCNT_G and VRAM CNT_H) stops being a comment and becomes an assertion over
// an actual write log.  It does not buy: any evidence whatsoever that the DS
// does what GBATEK says, that the values are the ones the hardware wants, or
// that anything appears on a screen.  A green host suite here means the code
// writes what this project believes it should write.  Nothing more, and the
// tests say so where they assert it.
//
// NO VIRTUALS.  platform/ds/README.md sanctions exactly one virtual in the
// port, GroundRenderer, and this is not it.  The two definitions are chosen at
// LINK time, so a write costs a call the linker can inline on device and there
// is no vtable in a path that runs a few hundred times at boot and never again.

#include <cstddef>
#include <cstdint>

namespace kh::device {

// A memory-mapped write.  Widths are separate functions rather than a template
// because the DS cares: VRAMCNT is a byte register and a 16-bit store to
// 0x04000240 also writes VRAMCNT_B, which is how one bank's setup silently
// undoes another's.
class Mmio {
public:
    void write8(uint32_t addr, uint8_t v);
    void write16(uint32_t addr, uint16_t v);
    void write32(uint32_t addr, uint32_t v);
};

// The host recorder's view of what happened.  Declared here rather than in the
// host .cpp so a test can name the type; on device these are never defined and
// nothing references them.
struct MmioWrite {
    uint32_t addr;
    uint32_t value;
    uint8_t width;      // 1, 2 or 4 bytes
};

// Host-only.  mmio_device.cpp does not define these, so a device build that
// accidentally calls one fails to link rather than quietly carrying a log
// around in a 4 MB machine.
constexpr int MMIO_LOG_MAX = 128;

const MmioWrite* mmioLog();
int mmioLogCount();
void mmioLogClear();
// True if the log filled up and stopped recording.  A test that asserts over a
// truncated log is a test that passes because it ran out of evidence.
bool mmioLogOverflowed();
// The last value written to `addr` at any width, or -1 if it was never written.
// -1 rather than 0 because 0 is a legal register value and "never written" has
// to be distinguishable from "written zero" -- the whole WRAMCNT check turns on
// that difference.
int64_t mmioLast(uint32_t addr);
// How many writes touched any byte of [addr, addr + width).  Overlap, not
// equality: a 16-bit store to VRAMCNT_G writes WRAMCNT too, and a check that
// compared addresses for equality would not see it.
int mmioTouches(uint32_t addr, int width = 1);

}  // namespace kh::device
