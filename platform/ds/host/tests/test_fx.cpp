// ScreenFx, applied.  The five registers, and the one decision worth checking.
//
// The decision is the two-units one: brightness takes MASTER_BRIGHT and
// whiteout takes the blend unit, because the game writes both at once and the
// DS's MASTER_BRIGHT can only fade one way at a time.  Half these cases exist
// to hold that apart -- a future simplification that folded whiteout onto
// MASTER_BRIGHT would pass every "does it write a register" test and lose the
// night's lightning behind the Tear's fade.

#include "check.h"
#include "fx.h"
#include "init.h"
#include "mmio.h"
#include "vram_map.h"

using namespace kh;
using namespace kh::device;

namespace {

int factorOf(uint16_t v) { return v & 0x1F; }
int modeOf(uint16_t v) { return (v >> BRIGHT_MODE_SHIFT) & 3; }

}  // namespace

KH_TEST(fx_full_daylight_disarms_the_brightness_unit) {
    ScreenFx fx;                                    // brightness defaults to 15
    CHECK_EQ(fx.brightness, 15);
    // Not "factor 0 in down mode".  The same picture, a different register
    // value, and the difference is what lets a test say the fade has ENDED
    // rather than merely arrived at its last step.
    CHECK_EQ(masterBrightValue(fx), 0);
    CHECK_EQ(modeOf(masterBrightValue(fx)), 0);
}

KH_TEST(fx_black_is_a_full_down_fade) {
    ScreenFx fx;
    fx.brightness = 0;
    CHECK_EQ(modeOf(masterBrightValue(fx)), BRIGHT_MODE_DOWN);
    CHECK_EQ(factorOf(masterBrightValue(fx)), BRIGHT_FACTOR_MAX);
}

KH_TEST(fx_brightness_is_monotone_and_inverted) {
    // ScreenFx counts UP to daylight and MASTER_BRIGHT counts up to black, so
    // the mapping is a reflection.  A sign error here is not a crash: it is a
    // day fade that goes bright at midnight, which reads as an art bug.
    int prev = -1;
    for (int b = 15; b >= 0; --b) {
        ScreenFx fx;
        fx.brightness = uint8_t(b);
        const int f = b == 15 ? 0 : factorOf(masterBrightValue(fx));
        CHECK(f >= prev);
        prev = f;
    }
    CHECK_EQ(prev, BRIGHT_FACTOR_MAX);
}

KH_TEST(fx_whiteout_saturates_to_a_full_white) {
    ScreenFx fx;
    fx.whiteout = 31;                               // the register's five bits
    CHECK_EQ(bldyValue(fx), BRIGHT_FACTOR_MAX);
    CHECK((bldcntValue(fx) & BLD_TARGET_ALL) == BLD_TARGET_ALL);
    CHECK_EQ((bldcntValue(fx) >> BLD_EFFECT_SHIFT) & 3, BLD_EFFECT_BRIGHTEN);
}

KH_TEST(fx_no_whiteout_leaves_the_blend_unit_unarmed) {
    const ScreenFx fx;
    CHECK_EQ(fx.whiteout, 0);
    CHECK_EQ(bldyValue(fx), 0);
    // Disarmed rather than armed at zero strength: an effect selected over
    // every layer is a live global, and the next person to want alpha blending
    // would find BLDCNT already claimed by something that is doing nothing.
    CHECK_EQ((bldcntValue(fx) >> BLD_EFFECT_SHIFT) & 3, BLD_EFFECT_NONE);
}

KH_TEST(fx_a_fade_to_black_and_a_flash_do_not_share_a_register) {
    // THE CASE THE WHOLE FILE IS FOR.  The night's Tear drags brightness down
    // while a lightning strike writes whiteout (stage_night.cpp:42 and :119),
    // and the Dive's fade writes whiteout 31 with brightness still at 15
    // (stage_dive.cpp:154).  Both must survive.
    ScreenFx fx;
    fx.brightness = 4;
    fx.whiteout = 20;
    CHECK_EQ(modeOf(masterBrightValue(fx)), BRIGHT_MODE_DOWN);
    CHECK(factorOf(masterBrightValue(fx)) > 0);
    CHECK(bldyValue(fx) > 0);
    CHECK_EQ((bldcntValue(fx) >> BLD_EFFECT_SHIFT) & 3, BLD_EFFECT_BRIGHTEN);
}

KH_TEST(fx_mosaic_is_the_snes_encoding_unchanged) {
    ScreenFx fx;
    fx.mosaic = 15;
    // Bits 0-3 BG H, 4-7 BG V, and the OBJ fields stay clear: the Shatter and
    // the Tear coarsen the GROUND, and Sora stays sharp while the floor comes
    // apart.  Mosaicking the sprites too is a different effect.
    CHECK_EQ(mosaicValue(fx) & 0x000F, 15);
    CHECK_EQ((mosaicValue(fx) >> 4) & 0x000F, 15);
    CHECK_EQ(mosaicValue(fx) >> 8, 0);
    fx.mosaic = 0;
    CHECK_EQ(mosaicValue(fx), 0);
}

KH_TEST(fx_forced_blank_and_the_ground_layer) {
    const uint32_t base = dispcntMainValue();
    const uint32_t groundBit = 1u << (8 + unsigned(vram::MAIN_GROUND_LAYER));
    CHECK((base & groundBit) != 0);                 // init enables it

    ScreenFx fx;
    CHECK_EQ(dispcntWith(base, fx), base);          // a quiet frame changes nothing

    fx.forcedBlank = true;
    CHECK((dispcntWith(base, fx) & (1u << 7)) != 0);

    // dive.s:449-451 drops the background for the tumble.  It must take the
    // GROUND layer and nothing else -- a version that hard-coded BG1 would
    // disable the overlay and leave the ground up, which is the same picture
    // upside down.
    ScreenFx off;
    off.bgVisible = false;
    CHECK_EQ(dispcntWith(base, off), base & ~groundBit);
}

KH_TEST(fx_applyfx_writes_all_five_at_the_right_widths) {
    mmioLogClear();
    Mmio io;
    ScreenFx fx;
    fx.brightness = 7;
    fx.whiteout = 9;
    fx.mosaic = 3;
    applyFx(io, dispcntMainValue(), fx);

    CHECK(!mmioLogOverflowed());
    CHECK_EQ(mmioLogCount(), 5);
    CHECK_EQ(mmioLast(MOSAIC_MAIN), mosaicValue(fx));
    CHECK_EQ(mmioLast(BLDCNT_MAIN), bldcntValue(fx));
    CHECK_EQ(mmioLast(BLDY_MAIN), bldyValue(fx));
    CHECK_EQ(mmioLast(MASTER_BRIGHT_MAIN), masterBrightValue(fx));
    CHECK_EQ(mmioLast(DISPCNT_MAIN), dispcntWith(dispcntMainValue(), fx));

    // DISPCNT LAST.  Forced blank is what makes a half-applied frame invisible,
    // so the frame the other four change in must still be the blanked one.
    CHECK_EQ(mmioLog()[mmioLogCount() - 1].addr, DISPCNT_MAIN);
    CHECK_EQ(mmioLog()[mmioLogCount() - 1].width, 4);
    for (int i = 0; i < 4; ++i) CHECK_EQ(mmioLog()[i].width, 2);
}

KH_TEST(fx_touches_no_register_the_initialisation_owns) {
    // BGxCNT, VRAMCNT, POWCNT1: none of them is a per-frame register, and a
    // frame path that wrote one would be re-deciding the allocation §M4 froze
    // sixty times a second.
    mmioLogClear();
    Mmio io;
    const ScreenFx fx;
    applyFx(io, dispcntMainValue(), fx);
    CHECK_EQ(mmioTouches(0x04000304, 2), 0);            // POWCNT1
    CHECK_EQ(mmioTouches(vram::WRAMCNT_ADDR), 0);
    for (unsigned b = 0; b < unsigned(vram::Bank::Count); ++b)
        CHECK_EQ(mmioTouches(vram::vramcntAddr(vram::Bank(b))), 0);
    for (int layer = 0; layer < 4; ++layer)
        CHECK_EQ(mmioTouches(0x04000008 + uint32_t(layer) * 2, 2), 0);
}
