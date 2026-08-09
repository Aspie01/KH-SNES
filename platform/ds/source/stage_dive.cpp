#include "stage.h"

namespace kh {

void DiveMachine::begin() {
    stage_ = DiveStage::Intro;
    shatter_ = 0;
    fall_ = 0;
    fade_ = 0;
    taken_ = ActType::None;
    given_ = ActType::None;
    pendActor_ = -1;
    pendWeapon_ = ActType::None;
}

// Shatter, and Shatter2: the same effect one station further down.
//
// Done with hardware rather than art -- MOSAIC coarsens the ground into ever
// larger blocks while the brightness falls and the screen shakes, which reads as
// glass breaking up without a second tileset for the debris.
StageStep DiveMachine::shatterStep(SceneView& view, ScreenFx& fx) {
    if (shatter_ == 0) {
        // Landed.  Which station the glass drops him onto depends on which broke.
        fx.shakeX = 0;
        fx.mosaic = 0;
        fx.forcedBlank = true;      // while VRAM is rewritten
        const bool second = (stage_ == DiveStage::Shatter2);
        stage_ = second ? DiveStage::S3Intro : DiveStage::S2Intro;
        return StageStep{second ? SceneAction::EnterStation3
                                : SceneAction::EnterStation2,
                         second ? ScriptId::DiveStation3 : ScriptId::DiveStation2};
    }
    --shatter_;

    // elapsed = SHATTER_LEN - remaining, scaled to the 0..15 the registers take.
    // The clamp is `cmp #16 / bcc` then 15, so 96/8 = 12 is the most it reaches:
    // the platform never fully blacks out before it lands.
    int step = (SHATTER_LEN - shatter_) >> 3;
    if (step >= 16) step = 15;
    fx.mosaic = uint8_t(step);
    fx.brightness = uint8_t(15 - step);
    // Alternating either side of centre on a four-frame period.  +/-2 here; the
    // night's Tear is +/-3.  Audit finding 6.
    fx.shakeX = (view.frame & 0x02) ? int8_t(-2) : int8_t(2);
    return StageStep{};
}

namespace {
// moteOfsX / moteOfsY, dive.s:919-924, verbatim and in the assembler's order.
// Q12.4: X spreads -115..+110 px across the screen -- NOT symmetric, and said
// exactly rather than as "+/- 115", because -1840 >> 4 is 115 and the largest
// positive is 1760 >> 4 = 110; Y is 120..160 px BELOW him
// (1920 >> 4 = 120, 2560 >> 4 = 160), so every speck starts under the bottom
// edge of either machine's viewport and rises into it.  Sora falls with py
// pinned at 2944 (184 px) throughout, so those are absolute screen facts and
// not just relative ones: the SNES's bottom edge is camY 16 + 224 = 240 and the
// DS's is 32 + 192 = 224, and the motes appear at 304..344 px on both.
//
// DELIBERATELY UNSIZED, and the static_assert below is why.  Writing
// `MOTE_OFS[MOTE_SPREAD]` looks safer and is weaker in both directions: a ninth
// row becomes "too many initializers", which is a compiler error about a bound
// rather than about the mask, and an eighth row DELETED becomes a silent
// zero-filled entry -- a speck of light spawning exactly on Sora, once every
// thirty-two frames, on a table nobody would think to reread.  Letting the
// initializer set the length makes the length a fact the assertion can check,
// and it catches both directions.
constexpr MoteOffset MOTE_OFS[] = {
    {World::fromRaw(-1600), World::fromRaw(2080)},
    {World::fromRaw( 1200), World::fromRaw(2320)},
    {World::fromRaw( -640), World::fromRaw(1920)},
    {World::fromRaw( 1760), World::fromRaw(2560)},
    {World::fromRaw(-1120), World::fromRaw(2160)},
    {World::fromRaw(  480), World::fromRaw(2400)},
    {World::fromRaw(-1840), World::fromRaw(2000)},
    {World::fromRaw(  960), World::fromRaw(2240)},
};

// `& (MOTE_SPREAD - 1)` is only the SNES's `and #$07` while the table is exactly
// eight long.  A ninth entry appended without touching the mask would be dead
// data and the spread would silently stay eight wide -- the specks would restack
// on a period nobody chose, and no trace column would name the table, because
// what the trace carries is a POSITION and a wrong position from a right formula
// looks exactly like a right position from a wrong one.
static_assert(MOTE_SPREAD == 8, "dive.s:515 masks with $07");
static_assert(int(sizeof MOTE_OFS / sizeof *MOTE_OFS) == MOTE_SPREAD,
              "the table and the mask must agree; see dive.s:919-924");
static_assert((MOTE_SPREAD & (MOTE_SPREAD - 1)) == 0,
              "the mask substitutes for a modulo only on a power of two");
static_assert((MOTE_SPAWN_EVERY & (MOTE_SPAWN_EVERY - 1)) == 0,
              "dive.s:480 is `and #$03`, not a division");

// dive.s:534's `bcc @out` skips a mote SILENTLY when the pool is full, and this
// port reproduces the silence rather than the failure -- see spawnMote() in
// trace_main.cpp.  Leaving that path untested is only honest while the fall
// cannot reach it, so the headroom is asserted here instead of asserted in
// prose: MOTE_LIFE / MOTE_SPAWN_EVERY = 10 motes are alive at the end of any
// frame, and ELEVEN exist momentarily, because SceneUpdate spawns the new one
// BEFORE UpdateWorld frees the expiring one (main.s's frame order; measured in
// the `fall` oracle trace as slots 7..17 recycling with nactors capped at 17).
// Eleven plus the first station's seven is eighteen, against the SNES's
// MAX_ACTORS = 32 (game.inc:164).  If any of those numbers moves, the silent
// skip stops being unreachable and divergence 002 -- the DS's pool of 128
// against the oracle's 32 -- begins to bite where nothing is watching, because
// the DS would keep spawning where the oracle had started dropping them.
constexpr int MOTE_PEAK = MOTE_LIFE / MOTE_SPAWN_EVERY + 1;
constexpr int STATION1_CAST = 7;                    // diveSpawns, dive.s:902-910
static_assert(STATION1_CAST + MOTE_PEAK <= SNES_MAX_ACTORS,
              "the fall would fill the oracle's pool and take dive.s:534's "
              "silent-skip path, which no scenario measures");
}  // namespace

MoteOffset moteOffset(uint32_t frame) {
    // dive.s:512-515.  The SNES read `lda frameCount` in A8, so the index came
    // off the LOW BYTE of a 16-bit counter (ram.s:30, incremented at
    // nmi.s:193).  Truncating to eight bits removes multiples of 256, and
    // 256 >> 2 = 64 is a multiple of 8 -- so bits 2..4 survive the truncation
    // and a 32-bit frame number gives the same answer for every frame the game
    // will ever run.  This is not a safe-because-the-scenario-is-short
    // argument; it is exact, and test_fall.cpp checks f against f + 256.
    return MOTE_OFS[(frame >> 2) & uint32_t(MOTE_SPREAD - 1)];
}

// Fall: specks of light stream up past him, then the light itself takes over.
StageStep DiveMachine::fallStep(SceneView& view, ScreenFx& fx) {
    (void)view;
    if (fall_ == 0) {
        // BeginFade.  BG1 stays off until the screen is white.
        stage_ = DiveStage::Fade;
        fade_ = FADE_LEN;
        fx.whiteout = 0;
        return StageStep{};
    }
    --fall_;
    // A new mote every fourth frame, tested AFTER the decrement.  The literal
    // 0x03 was the assembly's `and #$03` written out; it is MOTE_SPAWN_EVERY - 1
    // now so the cadence has ONE source and cannot drift from the spread table's
    // stride.  4 - 1 == 3, so no trace moves.
    if ((fall_ & (MOTE_SPAWN_EVERY - 1)) == 0)
        return StageStep{SceneAction::SpawnMote};
    return StageStep{};
}

// FadeOut: white out, swap to Destiny Islands at the midpoint, fade back in.
StageStep DiveMachine::fadeStep(ScreenFx& fx) {
    if (fade_ == 0) {
        stage_ = DiveStage::Arrived;
        fx.whiteout = 0;
        return StageStep{SceneAction::Say, ScriptId::DiveWake};
    }
    --fade_;
    if (fade_ == FADE_LEN / 2) {
        // Fully white, so nothing of the swap is visible.  The register takes
        // five bits, so the SNES's 0xFF was a clamp and not a value.
        fx.whiteout = 31;
        fx.forcedBlank = true;
        return StageStep{SceneAction::EnterIsland};
    }
    if (fade_ < FADE_LEN / 2) {
        fx.whiteout = uint8_t(fade_);           // the timer is already 31 down to 0
    } else {
        fx.whiteout = uint8_t(FADE_LEN - fade_); // 1 up to 31
    }
    return StageStep{};
}

StageStep DiveMachine::update(SceneView& view, ScreenFx& fx) {
    // A box is up; nothing else happens.  Every Dive beat waits for dialogue --
    // including Shatter, which is worth stating because the night's Tear and
    // EndFade deliberately run THROUGH the box.  Audit, "what a port must be told".
    if (view.dialogue.busy()) return StageStep{};

    switch (stage_) {
        case DiveStage::Intro:
            // The opening message has been dismissed: the pedestals are live.
            stage_ = DiveStage::Pick;
            return StageStep{};

        case DiveStage::Shatter:
        case DiveStage::Shatter2:
            return shatterStep(view, fx);

        case DiveStage::S2Intro:
            stage_ = DiveStage::S2Fight;
            return StageStep{};

        case DiveStage::S2Fight:
            // WatchShadows: once the last one is gone the floor gives out again.
            if (view.actors.count(ActType::Shadow) != 0) return StageStep{};
            stage_ = DiveStage::Shatter2;
            shatter_ = SHATTER_LEN;
            return StageStep{SceneAction::Say, ScriptId::DiveFloorGoes};

        case DiveStage::S3Intro:
            // BeginBoss: the third station's line has been dismissed.
            stage_ = DiveStage::Boss;
            return StageStep{SceneAction::SpawnBoss, ScriptId::DiveBoss};

        case DiveStage::Boss:
            if (view.actors.count(ActType::Darkside) != 0) return StageStep{};
            stage_ = DiveStage::Done;
            return StageStep{SceneAction::Say, ScriptId::DiveVictory};

        case DiveStage::Done: {
            // BeginFall: the last station goes with the shadow, and Sora drops.
            stage_ = DiveStage::Fall;
            fall_ = FALL_LEN;
            fx.shakeX = 0;
            fx.mosaic = 0;
            fx.bgVisible = false;   // the void needs no art; the backdrop is black
            return StageStep{SceneAction::BeginFall};
        }

        case DiveStage::Fall:
            return fallStep(view, fx);

        case DiveStage::Fade:
            return fadeStep(fx);

        case DiveStage::Pick:
        case DiveStage::Drop:
            // Pressing A next to a weapon asks about it.  Which weapon, and the
            // REACH test that finds it, is the caller's -- this machine's only
            // interest is that an answer moves Pick to Drop and Drop to Shatter.
            return StageStep{};

        case DiveStage::Arrived:
            return StageStep{};
    }
    return StageStep{};
}

// A death, and the checkpoint it rewinds to.  Which stage that is depends on
// which station is loaded, so this is a per-scene rewind and not a single one.
StageStep DiveMachine::restart(SceneId scene, ScreenFx& fx) {
    fx.mosaic = 0;
    fx.shakeX = 0;
    fx.whiteout = 0;
    fx.brightness = 15;
    fx.forcedBlank = false;
    fx.bgVisible = true;
    shatter_ = 0;
    fall_ = 0;
    fade_ = 0;

    if (scene == SceneId::Dive3) {
        stage_ = DiveStage::Boss;
        return StageStep{SceneAction::SpawnBoss};
    }
    if (scene == SceneId::Dive2) {
        stage_ = DiveStage::S2Fight;
        return StageStep{SceneAction::EnterStation2};
    }
    // The first station: back to choosing, with the pedestals live again.  The
    // choice is inert, so it is deliberately NOT cleared -- a retry does not
    // un-choose what was already picked.
    stage_ = DiveStage::Pick;
    return StageStep{};
}

}  // namespace kh
