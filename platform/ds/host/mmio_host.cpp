// The host half of the MMIO seam: record the write instead of performing it.
//
// This is the file that makes §M7's initialisation testable with no toolchain
// and no hardware.  It lives in host/ rather than device/ because it is host
// scaffolding -- a device build must not contain it, and the surest way to
// guarantee that is for it not to be in the directory the device build globs.
//
// WHAT A TEST CAN ASK, and why each one needed a log rather than a flag:
//
//   * WHAT was written, and to WHERE.  The nine VRAMCNT bytes are fully
//     determined by vram_map.h's frozen table, so "the init programs the banks
//     correctly" is a comparison and not a judgement.
//   * WHAT WAS NOT WRITTEN.  0x04000247 is WRAMCNT, sitting between VRAMCNT_G
//     and VRAMCNT_H, and writing it repartitions the RAM the two CPUs share.
//     Until now that hazard could only be asserted about the ADDRESS TABLE; a
//     log lets it be asserted about the actual sequence of stores, which is the
//     thing that would really go wrong.
//   * AT WHAT WIDTH.  VRAMCNT is eight bits.  A 16-bit store to 0x04000240 sets
//     bank A and *also* bank B, so a plausible-looking loop over uint16_t would
//     configure four banks correctly and clobber four others -- and the values
//     would all still be "right" if you only compared the ones you meant to
//     write.  mmioTouches() answers by overlap for exactly this reason.
//
// The log is FIXED SIZE and reports its own overflow.  A growable one would
// make a test that overran it pass quietly with less evidence than it thought
// it had, and "the assertion held over the first 128 writes" is not the same
// statement as "the assertion held".

#include "mmio.h"

namespace kh::device {
namespace {

MmioWrite g_log[MMIO_LOG_MAX];
int g_count = 0;
bool g_overflow = false;

void record(uint32_t addr, uint32_t value, uint8_t width) {
    if (g_count >= MMIO_LOG_MAX) {
        g_overflow = true;
        return;
    }
    g_log[g_count++] = MmioWrite{addr, value, width};
}

}  // namespace

void Mmio::write8(uint32_t addr, uint8_t v) { record(addr, v, 1); }
void Mmio::write16(uint32_t addr, uint16_t v) { record(addr, v, 2); }
void Mmio::write32(uint32_t addr, uint32_t v) { record(addr, v, 4); }

const MmioWrite* mmioLog() { return g_log; }
int mmioLogCount() { return g_count; }
bool mmioLogOverflowed() { return g_overflow; }

void mmioLogClear() {
    g_count = 0;
    g_overflow = false;
}

int64_t mmioLast(uint32_t addr) {
    for (int i = g_count - 1; i >= 0; --i)
        if (g_log[i].addr == addr) return int64_t(g_log[i].value);
    return -1;
}

int mmioTouches(uint32_t addr, int width) {
    const uint32_t lo = addr;
    const uint32_t hi = addr + uint32_t(width);
    int n = 0;
    for (int i = 0; i < g_count; ++i) {
        const uint32_t wlo = g_log[i].addr;
        const uint32_t whi = wlo + g_log[i].width;
        if (wlo < hi && lo < whi) ++n;
    }
    return n;
}

}  // namespace kh::device
