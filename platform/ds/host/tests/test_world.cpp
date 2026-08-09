// The actor simulation, checked against the SNES oracle rather than against my
// reading of world.s.
//
// Every other test in this suite asserts what I believe the assembly does.  This
// one asserts what the assembly ACTUALLY DID, because tools/snes_trace.py runs
// the frozen ROM headlessly and this fixture is its output.  That is the whole
// point of §M6 and it is worth the extra machinery: a mistake in my reading of
// world.s would sail past a hand-written expectation and be caught here.
//
// THE FIXTURE, and how to regenerate it:
//
//     tools/snes_trace.py --frames 150 --input <script> -o out.trace
//
// with the script below, then take frame/px/py/pdir/pstate from the trace.  The
// pad schedule here is that same script, so the two sides see identical input.
//
// WHAT IS DELIBERATELY NOT THE SAME: the ground.  The DS station is radius 92
// against the SNES's 110 (divergence 004), so its collision map differs on
// purpose and Sora would stop somewhere else.  This test therefore loads the
// SNES's own divecoll.bin, which isolates the MOVEMENT CODE -- the thing that
// must be identical -- from the CONTENT decision, which must not be.

#include <cstring>

#include "check.h"
#include "hostblob.h"
#include "world.h"

using namespace kh;

namespace {

// The pad, frame by frame, exactly as the trace was generated with.
struct Step { int frames; uint16_t held; };
constexpr uint16_t NONE = 0;
constexpr uint16_t A = raw(Button::A);
constexpr uint16_t B = raw(Button::B);
constexpr uint16_t R = raw(Button::Right);
constexpr uint16_t D = raw(Button::Down);
constexpr uint16_t LU = uint16_t(raw(Button::Left) | raw(Button::Up));

constexpr Step SCRIPT[] = {
    {10, NONE}, {3, A}, {8, NONE}, {3, A}, {8, NONE},
    {30, R}, {4, NONE}, {20, D}, {4, NONE}, {24, LU}, {6, NONE},
    {3, B}, {20, NONE},
};

// Frame, x, y, dir, state -- from the SNES.  Only the frames on which something
// changed, plus the frame after each change, because a fixture that listed all
// 128 would hide which lines carry information.
struct Expect { int frame; int x; int y; int dir; int state; };
constexpr Expect ORACLE[] = {
    // still, box just closed
    {24, 4224, 2944, 0, 0}, {31, 4224, 2944, 0, 0},
    // east: WALK_SPEED = 24 a frame, cardinal
    {32, 4248, 2944, 2, 1}, {33, 4272, 2944, 2, 1}, {44, 4536, 2944, 2, 1},
    // ...and stopped by the rim of the disc, still in ST_WALK against it
    {61, 4944, 2944, 2, 1}, {62, 4944, 2944, 2, 0},
    // south
    {66, 4944, 2968, 0, 1}, {80, 4944, 3304, 0, 1},
    // ...blocked again, and released
    {85, 4944, 3304, 0, 1}, {86, 4944, 3304, 0, 0},
    // north-west: 17 a frame on BOTH axes, the diagonal scaling
    {90, 4927, 3287, 5, 1}, {91, 4910, 3270, 5, 1}, {113, 4536, 2896, 5, 1},
    {114, 4536, 2896, 5, 0},
    // the swing: ST_ATTACK for ATTACK_FRAMES, no movement, then idle again
    {120, 4536, 2896, 5, 2}, {137, 4536, 2896, 5, 2}, {138, 4536, 2896, 5, 0},
    {149, 4536, 2896, 5, 0},
};

unsigned char collBuf[8192];
unsigned char heightBuf[8192];

}  // namespace

KH_TEST(world_sora_walks_exactly_as_the_snes_did) {
    // The SNES's own collision map, one directory up from the DS assets.
    Blob coll = khhost::loadSnes("divecoll.bin", collBuf, sizeof collBuf);
    if (coll.empty()) {
        CHECK(false);   // assets/gen/divecoll.bin is missing; run the pipeline
        return;
    }
    CHECK_EQ(coll.size, size_t(ORACLE_MAP_W * ORACLE_MAP_H));
    // A Station of Awakening is one flat plane -- LoadScene points heightPtr at
    // flatHeights for every scene but the island.
    std::memset(heightBuf, 0, sizeof heightBuf);

    SceneGround ground;
    ground.set(coll, Blob{heightBuf, coll.size}, ORACLE_MAP_W, ORACLE_MAP_H);
    CHECK(ground.valid());

    Actors actors;
    actors.clear();
    // Sora at CELL(16, 11), which is where the trace shows him once the opening
    // line is dismissed.
    const int sora = actors.spawn(ActType::Sora, tileCentre(16), tileCentre(11));
    CHECK_EQ(sora, 0);
    CHECK_EQ(actors.x[sora].raw(), 4224);
    CHECK_EQ(actors.y[sora].raw(), 2944);

    Dialogue dlg;
    Pad pad;
    Rng rng;
    WorldState w;
    ScreenFx fx;
    SceneView view{actors, dlg, pad, ground, rng};
    view.player = sora;

    // Replay the script.  The box is already closed here, so the first 24
    // frames of the SNES run -- the opening line and its two dismissing presses
    // -- are the frames this test starts after; the pad is still stepped through
    // them so the frame numbers line up with the fixture.
    int frame = 0, step = 0, left = SCRIPT[0].frames, checked = 0;
    uint16_t prevHeld = 0;
    for (; frame < 150; ++frame) {
        const uint16_t held = SCRIPT[step].held;
        pad.held = held;
        pad.pressed = uint16_t(held & ~prevHeld);
        prevHeld = held;
        if (--left == 0 && step + 1 < int(sizeof SCRIPT / sizeof *SCRIPT)) {
            ++step;
            left = SCRIPT[step].frames;
        }

        updateWorld(w, view, fx);
        view.frame = uint32_t(frame);

        for (const Expect& e : ORACLE) {
            if (e.frame != frame) continue;
            CHECK_EQ(actors.x[sora].raw(), e.x);
            CHECK_EQ(actors.y[sora].raw(), e.y);
            CHECK_EQ(int(actors.dir[sora]), e.dir);
            CHECK_EQ(int(actors.state[sora]), e.state);
            ++checked;
        }
    }
    // If the fixture and the loop ever disagree about frame numbering this is
    // the assertion that notices, rather than the test silently checking none.
    CHECK_EQ(checked, int(sizeof ORACLE / sizeof *ORACLE));
}

KH_TEST(world_hit_stop_freezes_every_actor_and_only_actors) {
    Actors actors;
    actors.clear();
    const int sora = actors.spawn(ActType::Sora, tileCentre(16), tileCentre(11));
    Dialogue dlg;
    Pad pad;
    Rng rng;
    SceneGround ground;
    WorldState w;
    ScreenFx fx;
    SceneView view{actors, dlg, pad, ground, rng};
    view.player = sora;

    // Three frames of freeze means three frames of nothing, and the counter
    // comes down on the frozen frame -- so the fourth frame runs.
    w.hitStop = 3;
    pad.held = raw(Button::Right);
    const World x0 = actors.x[sora];
    for (int i = 0; i < 3; ++i) {
        updateWorld(w, view, fx);
        CHECK_EQ(actors.x[sora].raw(), x0.raw());
        CHECK_EQ(int(actors.state[sora]), int(ActState::Idle));
    }
    CHECK_EQ(int(w.hitStop), 0);
    // ...and now it moves.  The ground is invalid here, so tryMoveActor refuses
    // and only the state and facing change -- which is exactly what is being
    // asserted: the freeze was lifted.
    updateWorld(w, view, fx);
    CHECK_EQ(int(actors.state[sora]), int(ActState::Walk));
    CHECK_EQ(int(actors.dir[sora]), int(Dir::E));
}

KH_TEST(world_opposite_buttons_do_not_cancel) {
    // ReadMoveDir tests left and then right and the second overwrites, so
    // left+right is RIGHT and up+down is DOWN.  Reproduced rather than tidied,
    // because a player rolling the d-pad through the centre would otherwise
    // diverge on one frame and never come back.
    Pad pad;
    Dir d{};
    pad.held = uint16_t(raw(Button::Left) | raw(Button::Right));
    CHECK(readMoveDir(pad, d));
    CHECK_EQ(int(d), int(Dir::E));
    pad.held = uint16_t(raw(Button::Up) | raw(Button::Down));
    CHECK(readMoveDir(pad, d));
    CHECK_EQ(int(d), int(Dir::S));
    pad.held = 0;
    CHECK(!readMoveDir(pad, d));
}

KH_TEST(world_a_shadow_in_the_deadzone_neither_moves_nor_touches) {
    // The single most consequential thing about a Shadow, and the reason the
    // night plays the way it does: HeartlessAimDir returning "stand still"
    // skips the move AND the touch test, so it can never damage Sora
    // point-blank or from directly north or south.  BEHAVIOUR.md §2.
    Actors actors;
    actors.clear();
    const int sora = actors.spawn(ActType::Sora, tileCentre(16), tileCentre(11));
    // Right on top of him: both axes inside the 208 deadzone.
    const int sh = actors.spawn(ActType::Shadow, tileCentre(16), tileCentre(11));
    CHECK(sh > 0);
    Dialogue dlg;
    Pad pad;
    Rng rng;
    SceneGround ground;
    WorldState w;
    ScreenFx fx;
    SceneView view{actors, dlg, pad, ground, rng};
    view.player = sora;

    const uint8_t hp0 = actors.hp[sora];
    const World sx = actors.x[sh], sy = actors.y[sh];
    for (int i = 0; i < 60; ++i) updateWorld(w, view, fx);
    CHECK_EQ(actors.hp[sora], hp0);             // never damaged
    CHECK_EQ(actors.x[sh].raw(), sx.raw());     // and never moved
    CHECK_EQ(actors.y[sh].raw(), sy.raw());
    // ...but it does still animate, which is what makes it read as alive.
    CHECK(actors.anim[sh] != 0 || actors.animT[sh] != 0);
}

KH_TEST(world_a_wooden_sword_goes_straight_through_a_shadow) {
    // keyGot gates BOTH directions: DoAttackHit skips Shadows without it, and
    // UpdateHeartless skips its touch test.  That pairing is what makes
    // NightRestart's pre-N_KAIRI branch unreachable -- audit finding 56.
    Actors actors;
    actors.clear();
    const int sora = actors.spawn(ActType::Sora, tileCentre(16), tileCentre(11));
    const int sh = actors.spawn(ActType::Shadow, tileCentre(17), tileCentre(11));
    Dialogue dlg;
    Pad pad;
    Rng rng;
    SceneGround ground;
    WorldState w;
    ScreenFx fx;
    SceneView view{actors, dlg, pad, ground, rng};
    view.player = sora;
    actors.dir[sora] = Dir::E;

    w.keyGot = false;
    const uint8_t hp0 = actors.hp[sh];
    pad.held = raw(Button::B);
    pad.pressed = raw(Button::B);
    for (int i = 0; i < ATTACK_FRAMES + 2; ++i) {
        updateWorld(w, view, fx);
        pad.pressed = 0;
    }
    CHECK_EQ(actors.hp[sh], hp0);
    CHECK_EQ(int(actors.type[sh]), int(ActType::Shadow));
}
