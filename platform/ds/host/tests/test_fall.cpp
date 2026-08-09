// The fall's spread of light: moteOffset(), and the arithmetic that indexes it.
//
// WHY THIS IS A SEPARATE FILE FROM test_stage.cpp.  §0.7: "Tests are per-task
// files."  test_stage.cpp already owns the Dive MACHINE -- it drives the fall
// through its 171 frames and counts the 43 SpawnMote actions
// (stage_dive_the_fall_is_170_frames_and_a_mote_every_fourth).  What it cannot
// see, and what nothing in the suite could see before this file, is WHERE each
// of those 43 specks goes: SceneAction::SpawnMote carries no position, on
// purpose, because a machine here is pure logic.  The position comes out of
// moteOffset(), which is a free function of the frame counter, and that is what
// is checked here.
//
// The failure this guards is a quiet one.  A wrong entry in the table, or a
// wrong shift in the index, produces a perfectly well-formed mote at a wrong
// place: nothing is out of bounds, no pool overflows, no assertion trips, and
// the sprite is still a speck of light drifting upward.  The only witness is the
// oracle -- the `fall` scenario in tools/trace_check.py, which pins all 43 of
// them against the frozen ROM -- and the oracle is a minute of 65816
// interpretation that is deliberately not in Gate 0.  This file is the part of
// that check which runs in the ordinary build.

#include "check.h"
#include "stage.h"

using namespace kh;

namespace {

// moteOfsX / moteOfsY, dive.s:919-924, transcribed HERE A SECOND TIME and on
// purpose.  A test that read the table it is testing would be a tautology; this
// is the assembly re-read into the expectation, so the two copies must agree and
// the assembly is the tiebreak.
//
//     moteOfsX: .word .loword(-1600), 1200, .loword(-640), 1760
//               .word .loword(-1120), 480, .loword(-1840), 960
//     moteOfsY: .word 2080, 2320, 1920, 2560
//               .word 2160, 2400, 2000, 2240
constexpr int32_t WANT_DX[8] = {-1600, 1200, -640, 1760, -1120, 480, -1840, 960};
constexpr int32_t WANT_DY[8] = {2080, 2320, 1920, 2560, 2160, 2400, 2000, 2240};

}  // namespace

KH_TEST(fall_the_mote_spread_walks_all_eight_offsets) {
    // dive.s:512-515: `lda frameCount / lsr a / lsr a / and #$07`.  Frames 0, 4,
    // 8 ... 28 are one full lap of the spread, and every frame in a group of
    // four gives the same answer because the two shifts throw the low bits away
    // -- which is what makes the cadence at dive.s:479-481 and the stride here
    // the matched pair stage.h describes.
    for (int k = 0; k < MOTE_SPREAD; ++k) {
        for (int sub = 0; sub < MOTE_SPAWN_EVERY; ++sub) {
            const uint32_t f = uint32_t(k * MOTE_SPAWN_EVERY + sub);
            CHECK_EQ(moteOffset(f).dx.raw(), WANT_DX[k]);
            CHECK_EQ(moteOffset(f).dy.raw(), WANT_DY[k]);
        }
    }
}

KH_TEST(fall_the_spread_is_the_same_after_the_snes_counter_wraps_a_byte) {
    // THE SNES READ THE LOW BYTE AND THIS PORT READS ALL 32 BITS, and that is
    // only safe because of an arithmetic accident worth pinning rather than
    // trusting.  `lda frameCount` in A8 (dive.s:512) truncates a 16-bit counter
    // (ram.s:30) to eight bits, so the SNES's index is ((f & 0xFF) >> 2) & 7 and
    // this port's is (f >> 2) & 7.  They agree for every f because truncation
    // removes multiples of 256, 256 >> 2 = 64, and 64 is a multiple of 8 -- so
    // bits 2..4, which are the only bits the index reads, survive it untouched.
    //
    // If that ever stopped being true the divergence would begin at frame 256,
    // which is PAST the end of every scenario in the corpus: the `fall` oracle
    // runs 200 frames because frame 205 is where the fade would load the island.
    // So this is precisely the property the trace cannot check, which is why it
    // is checked here.
    for (uint32_t f = 0; f < 64; ++f) {
        CHECK_EQ(moteOffset(f).dx.raw(), moteOffset(f + 256).dx.raw());
        CHECK_EQ(moteOffset(f).dy.raw(), moteOffset(f + 256).dy.raw());
        CHECK_EQ(moteOffset(f).dx.raw(), moteOffset(f + 4096).dx.raw());
        CHECK_EQ(moteOffset(f).dy.raw(), moteOffset(f + 4096).dy.raw());
    }
}

KH_TEST(fall_the_forty_three_spawn_frames_are_the_ones_the_oracle_recorded) {
    // The `fall` scenario's spawn frames, derived the way the ROM derives them
    // rather than copied off the trace.  BeginFall runs on frame 2 and arms
    // fallTimer to FALL_LEN; Fall decrements THEN tests (dive.s:477-481), so on
    // frame n > 2 the timer reads FALL_LEN - (n - 2) and a mote appears whenever
    // that is a multiple of four.  FALL_LEN = 170 is not, and 170 - 2 = 168 is,
    // so the first mote is on frame 4 and the last is on 172 where the timer
    // reaches zero -- which is also the frame the fade is armed on the NEXT one.
    int seen[MOTE_SPREAD] = {0, 0, 0, 0, 0, 0, 0, 0};
    int total = 0;
    for (int n = 3; n <= 172; ++n) {
        const int timer = FALL_LEN - (n - 2);
        if ((timer & (MOTE_SPAWN_EVERY - 1)) != 0) continue;
        ++total;
        const int k = (n >> 2) & (MOTE_SPREAD - 1);
        ++seen[k];
        // ...and the offset that frame gets is the table's k'th row.
        CHECK_EQ(moteOffset(uint32_t(n)).dx.raw(), WANT_DX[k]);
        CHECK_EQ(moteOffset(uint32_t(n)).dy.raw(), WANT_DY[k]);
    }
    CHECK_EQ(total, 43);
    // 43 = 5 * 8 + 3, and the three extra land on the residues frames 4, 8 and
    // 12 start from -- so the distribution is 5 everywhere except slots 1, 2
    // and 3.  An index built from the wrong shift would still total 43 and would
    // NOT distribute like this, which is the point of asserting the shape and
    // not just the count.
    const int want[MOTE_SPREAD] = {5, 6, 6, 6, 5, 5, 5, 5};
    for (int k = 0; k < MOTE_SPREAD; ++k) CHECK_EQ(seen[k], want[k]);
}

KH_TEST(fall_a_mote_starts_below_both_machines_bottom_edges) {
    // docs/WORLD_SIZES.md's claim about the fall, made arithmetic.  Sora's py is
    // pinned at 2944 raw (184 px) for the whole drop -- he does not move, the
    // camera is pinned, and UpdateSora's ST_FALL branch clears his velocity
    // every frame (world.s:457).  So each speck's absolute start is a fixed
    // number and can be compared with each machine's viewport.
    //
    // The station's camera is pinned at DIVE_CAM_Y, 16 on the SNES and 32 here
    // (divergence 001), so the bottom edges are 16 + 224 = 240 and 32 + 192 =
    // 224.  Every mote starts below both, and 16 px further below the DS's --
    // which is the whole of divergence 001's effect on this scene.
    constexpr int SORA_Y_PX = 184;              // 2944 >> 4, the oracle's py
    constexpr int SNES_BOTTOM = 16 + 224;
    constexpr int DS_BOTTOM = 32 + 192;
    for (int k = 0; k < MOTE_SPREAD; ++k) {
        const int y = SORA_Y_PX + (WANT_DY[k] >> 4);
        CHECK(y > SNES_BOTTOM);
        CHECK(y > DS_BOTTOM);
        // ...and it is off the bottom of the 32x16 MAP as well, which is why
        // both machines leave its z at zero: TileIndex rejects any row at or
        // past MAP_H (grid.s:157-163) and TileHeight answers zero for a
        // rejected index (grid.s:235-237).
        CHECK(y >= ORACLE_MAP_H * TILE_PX);
    }
    // It dies MOTE_LIFE frames later, MOTE_RISE per frame higher -- 40 * 6 = 240
    // px -- so it expires at y 64..104, which is 80..120 px ABOVE Sora at 184
    // and inside both viewports, clipping on neither.  (`y` below is an absolute
    // screen row and not a distance from him; this comment said "64..104 px
    // above Sora" and that was the two confused, which is exactly the sort of
    // number this project must not leave lying about for the next reader to
    // derive a real constant from.)  If it did clip, the two machines'
    // pictures would differ and no trace column would say so.
    constexpr int RISE = MOTE_LIFE * 6;         // MOTE_RISE is 96 raw = 6 px
    CHECK_EQ(MOTE_RISE.raw(), 96);
    for (int k = 0; k < MOTE_SPREAD; ++k) {
        const int y = SORA_Y_PX + (WANT_DY[k] >> 4) - RISE;
        CHECK(y > 16);                          // below the SNES's top edge
        CHECK(y > 32);                          // ...and below the DS's
        CHECK(y < DS_BOTTOM);
    }
}
