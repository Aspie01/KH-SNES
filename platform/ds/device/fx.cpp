// ScreenFx -> five registers.

#include "fx.h"

#include "constants.h"
#include "init.h"
#include "vram_map.h"

namespace kh::device {
namespace {

// The ranges the machines write, named here so the scaling below can be read
// against them rather than against two magic numbers.
constexpr int BRIGHT_FULL = 15;         // ScreenFx::brightness at full daylight
constexpr int WHITEOUT_MAX = 31;        // coldataAmt's five bits, saturated

// The DISPCNT bits fx can reach.  Quoted from init.cpp's own transcription
// rather than re-derived: bit 7 is forced blank and bits 8-11 are the four BG
// enables.
constexpr uint32_t DISP_FORCED_BLANK = 1u << 7;
constexpr uint32_t DISP_BG_ENABLE = 1u << 8;

}  // namespace

uint16_t masterBrightValue(const ScreenFx& fx) {
    // Full daylight is not "factor 0 in down mode", it is the unit OFF.  The
    // two are the same picture and not the same register value, and the
    // difference matters for exactly one reason: a test that asserts the fade
    // reached its end wants to see the unit disarmed, not parked.
    if (fx.brightness >= BRIGHT_FULL) return 0;
    // 15 -> 0 and 0 -> 16, linear between, both endpoints exact.  The rounding
    // in the middle is the truncation, which loses at most one sixteenth of a
    // step on a fade that lasts thirty-two frames.
    const int factor = (BRIGHT_FULL - int(fx.brightness)) * BRIGHT_FACTOR_MAX
                       / BRIGHT_FULL;
    return uint16_t(uint16_t(factor) | uint16_t(BRIGHT_MODE_DOWN << BRIGHT_MODE_SHIFT));
}

uint16_t bldcntValue(const ScreenFx& fx) {
    if (fx.whiteout == 0)
        return uint16_t(BLD_EFFECT_NONE << BLD_EFFECT_SHIFT);
    return uint16_t(BLD_TARGET_ALL
                    | uint16_t(BLD_EFFECT_BRIGHTEN << BLD_EFFECT_SHIFT));
}

uint16_t bldyValue(const ScreenFx& fx) {
    // 0..31 onto 0..16.  (w + 1) / 2 rather than w / 2, so that the saturated
    // 31 the Dive's fade and the night's lightning both write reaches a full
    // white rather than stopping one step short of it -- which on a fade to
    // white is the one frame anybody would notice.
    const int y = (int(fx.whiteout) + 1) / 2;
    return uint16_t(y > BRIGHT_FACTOR_MAX ? BRIGHT_FACTOR_MAX : y);
}
static_assert(WHITEOUT_MAX / 2 + 1 == BRIGHT_FACTOR_MAX,
              "the whiteout range and the blend factor's no longer line up at "
              "the top, so a saturated fade would stop short of white");

uint16_t mosaicValue(const ScreenFx& fx) {
    const uint16_t n = uint16_t(fx.mosaic & 0x0F);
    return uint16_t(n | uint16_t(n << 4));      // BG H and V; OBJ stays zero
}

uint32_t dispcntWith(uint32_t base, const ScreenFx& fx) {
    uint32_t v = base;
    if (fx.forcedBlank) v |= DISP_FORCED_BLANK;
    // BG1 OFF DURING THE FALL, and it is the GROUND layer that goes.  dive.s
    // :449-451 writes the TM/TS pair to drop the background so the tumble
    // happens over the backdrop; ScreenFx::bgVisible is that pair, and on the
    // DS the ground is whichever layer vram_map.h assigned -- BG0 today.  Named
    // through the constant rather than typed, because the day the ground moves
    // is the day this would silently disable somebody else's layer.
    if (!fx.bgVisible)
        v &= ~(DISP_BG_ENABLE << unsigned(vram::MAIN_GROUND_LAYER));
    return v;
}

void applyFx(Mmio& io, uint32_t dispcntBase, const ScreenFx& fx) {
    // DISPCNT LAST of the five, and that is the only ordering constraint here.
    // Forced blank is what makes a half-applied frame invisible, so the frame
    // in which the other four change should be the frame that is still blanked
    // if this one is going to blank it at all.  With no forced blank the order
    // is immaterial -- every one of these takes effect at the next scanline --
    // but "immaterial today" is not a reason to write it in the order that
    // would be wrong tomorrow.
    io.write16(MOSAIC_MAIN, mosaicValue(fx));
    io.write16(BLDCNT_MAIN, bldcntValue(fx));
    io.write16(BLDY_MAIN, bldyValue(fx));
    io.write16(MASTER_BRIGHT_MAIN, masterBrightValue(fx));
    io.write32(DISPCNT_MAIN, dispcntWith(dispcntBase, fx));
}

}  // namespace kh::device
