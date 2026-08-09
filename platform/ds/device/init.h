#pragma once
// §M7's first step: bring both screens up on the allocation §M4 froze.
//
// This is the whole of the two-screen initialisation, and it is deliberately
// the ONLY part of the device tier written before a toolchain exists, because
// it is the only part that is fully determined by things already in the tree.
// Every value below is derived from platform/ds/include/vram_map.h or quoted
// from GBATEK; not one is a judgement about how the game should look. That is
// what makes it writable and testable with no hardware -- and it is also the
// boundary: the ground renderer, the sprites, the bottom screen's furniture and
// the 3D backend all need decisions that a screen would inform, and guessing at
// them here would be inventing work rather than doing it.
//
// WHAT "TESTED" MEANS HERE, precisely, because the word is doing less work than
// usual. The host suite links device/mmio_host.cpp, which RECORDS register
// writes instead of performing them, and asserts that this function writes the
// values vram_map.h implies, in a sane order, at the right widths, and touches
// nothing it must not. That is a real check with real teeth -- it is how the
// WRAMCNT hazard becomes an assertion over an actual write log rather than a
// paragraph. It is NOT evidence that a DS does anything at all. Nothing in this
// container has ever run an ARM instruction.

#include "mmio.h"

namespace kh::device {

// The registers this touches, quoted from GBATEK's I/O map. VRAMCNT's nine are
// not here: vram_map.h owns them (VRAMCNT_ADDR), because the gap at 0x04000247
// where WRAMCNT sits is a property of the bank allocation's hardware and
// belongs beside the banks.
constexpr uint32_t POWCNT1 = 0x04000304;    // 16-bit
constexpr uint32_t DISPCNT_MAIN = 0x04000000;   // 32-bit, engine A
constexpr uint32_t DISPCNT_SUB = 0x04001000;    // 32-bit, engine B
constexpr uint32_t BGCNT_MAIN = 0x04000008;     // 16-bit each, +2 per layer
constexpr uint32_t BGCNT_SUB = 0x04001008;

// BGxCNT is per layer and both engines lay it out identically -- which is the
// half of the engine-A/engine-B asymmetry that is NOT a trap. The trap is in
// DISPCNT, where engine A has base fields engine B does not, and vram_map.h
// keeps every region under 62 KiB precisely so both of engine A's are zero and
// the two engines share one arithmetic.
constexpr uint32_t bgcnt(uint32_t base, int layer) {
    return base + uint32_t(layer) * 2;
}

// Bring up both engines, program the nine banks, and configure the six layers
// vram_map.h assigns. Leaves the screens in forced blank OFF and displaying,
// with every reserved region pointed at but nothing uploaded into it -- so the
// first frame is whatever VRAM powered on holding, until a renderer loads
// something. That is deliberate and it is why this is step one of six.
//
// WHAT IT DOES NOT DO, each because it needs something that does not exist yet:
//   * upload any character, map or palette data -- that is the asset loader,
//     and extended palettes additionally need the bank switched to LCDC first
//     (vram_map.h's note: they are not CPU-visible while mapped);
//   * enable the 3D engines or select BG0 as the 3D layer -- POWCNT1 bits 2 and
//     3 and DISPCNT bit 3 are the 3D backend's to set when it is chosen, and
//     turning them on here would cost power and a geometry FIFO for a renderer
//     that may never run;
//   * touch OAM, the touchscreen, the ARM7, or interrupts.
void initScreens(Mmio& io);

// The values initScreens writes, exposed so a test can assert against the
// derivation rather than against a copy of it -- and so the eventual device
// bring-up can print them next to what the hardware read back.
//
// These are constexpr and computed from vram_map.h's Regions and Layers. A test
// that compared initScreens' log against a hand-typed table would be pinning
// the table; comparing it against these pins the DERIVATION, and the literals
// then get asserted once, here, where a human can read them.
uint16_t powcnt1Value();
uint32_t dispcntMainValue();
uint32_t dispcntSubValue();
uint16_t bgcntMainValue(int layer);
uint16_t bgcntSubValue(int layer);

}  // namespace kh::device
