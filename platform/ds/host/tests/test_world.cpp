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

// ---------------------------------------------------------------------------
// Darkside, against the oracle again -- but reached a different way.
//
// The boss is on the THIRD Station of Awakening and playing to it would be a
// four-hundred-frame input script full of guesses.  Instead the oracle pokes two
// WRAM bytes -- sceneId and deadFlag -- and the ROM walks its OWN retry path
// into the fight: RestartScene loads SCENE_DIVE3, spawns soraOnlySpawns, calls
// SpawnBoss and sets DIVE_BOSS.  So the setup is the game's code and not a
// hand-built state, which is the only kind of fixture worth having.
//
//     tools/snes_trace.py --frames 300 --input <two A presses>
//         --poke sceneId=2 --poke deadFlag=2 --no-strict
//
// --no-strict is needed and is sound HERE specifically: the scene load overruns
// a frame (see the note in snes_trace.py), which corrupts frameCount's parity --
// and nothing in Darkside's state machine reads frameCount.  It would not be
// sound for anything that drives shakeX or a flash palette.
// ---------------------------------------------------------------------------
KH_TEST(world_darkside_alternates_exactly_as_the_snes_did) {
    Actors actors;
    actors.clear();
    // Sora far enough away that he is never under the boss: the sweep would
    // pre-empt the alternation, which is a separate test below.
    const int sora = actors.spawn(ActType::Sora, tileCentre(16), tileCentre(13));
    const int boss = actors.spawn(ActType::Darkside, World::fromRaw(4224),
                                  World::fromRaw(1920));
    CHECK(boss > 0);
    actors.hp[boss] = uint8_t(DS_MAX_HP);
    actors.state[boss] = ActState::Idle;        // DSS_REST shares the byte
    actors.timer[boss] = uint8_t(DS_REST);

    Dialogue dlg;
    Pad pad;
    Rng rng;
    SceneGround ground;
    WorldState w;
    ScreenFx fx;
    SceneView view{actors, dlg, pad, ground, rng};
    view.player = sora;

    // Every interval the SNES produced, as a duration rather than an absolute
    // frame -- the oracle run began mid-scene, and a duration is what actually
    // has to match.  Each is its constant PLUS ONE, because these are the
    // lda/beq/dec shape: the frame that reads zero is spent on the transition.
    struct Leg { int state; int frames; };
    const Leg LEGS[] = {
        {int(BossState::Rest),    DS_REST + 1},
        {int(BossState::SlamUp),  DS_SLAM_WIND + 1},
        {int(BossState::SlamHit), DS_SLAM_HOLD + 1},
        {int(BossState::Rest),    DS_REST + 1},
        {int(BossState::OrbUp),   DS_ORB_WIND + 1},
        {int(BossState::OrbFire), DS_ORB_REST + 1},
    };

    int leg = 0, spent = 0, shadowsAfterSlam = -1, orbsAfterVolley = -1;
    for (int f = 0; f < 400 && leg < int(sizeof LEGS / sizeof *LEGS); ++f) {
        CHECK_EQ(int(actors.state[boss]), LEGS[leg].state);
        updateWorld(w, view, fx);
        if (++spent == LEGS[leg].frames) {
            // The slam leaves a Shadow behind; the volley leaves three orbs.
            if (LEGS[leg].state == int(BossState::SlamUp))
                shadowsAfterSlam = actors.count(ActType::Shadow);
            if (LEGS[leg].state == int(BossState::OrbUp))
                orbsAfterVolley = actors.count(ActType::Orb);
            ++leg;
            spent = 0;
        }
    }
    CHECK_EQ(leg, int(sizeof LEGS / sizeof *LEGS));   // it got through them all
    CHECK_EQ(shadowsAfterSlam, 1);      // "a Shadow crawls out of the impact"
    CHECK_EQ(orbsAfterVolley, 3);       // centre, and one either side
}

KH_TEST(world_standing_underneath_pre_empts_the_alternation) {
    // The sweep is the punish for hugging its feet, and it is chosen AHEAD of
    // the fist/orb toggle -- so a boss that would have fired orbs sweeps
    // instead, and the toggle does not advance.
    Actors actors;
    actors.clear();
    const int sora = actors.spawn(ActType::Sora, World::fromRaw(4224),
                                  World::fromRaw(2100));
    const int boss = actors.spawn(ActType::Darkside, World::fromRaw(4224),
                                  World::fromRaw(1920));
    actors.hp[boss] = uint8_t(DS_MAX_HP);
    actors.timer[boss] = 0;             // about to choose

    Dialogue dlg;
    Pad pad;
    Rng rng;
    SceneGround ground;
    WorldState w;
    ScreenFx fx;
    SceneView view{actors, dlg, pad, ground, rng};
    view.player = sora;

    CHECK(playerUnderBoss(actors, boss, sora));
    const uint8_t toggle = actors.anim[boss];
    updateWorld(w, view, fx);
    CHECK_EQ(int(actors.state[boss]), int(BossState::SweepUp));
    CHECK_EQ(int(actors.timer[boss]), DS_SWEEP_WIND);
    CHECK_EQ(actors.anim[boss], toggle);        // the alternation did not turn
}

KH_TEST(world_the_slam_mark_is_taken_at_the_start_of_the_wind_up) {
    // The single most consequential error in the original specification, which
    // said the mark was taken at the END of the telegraph and so inverted the
    // dodge window.  AimAtPlayer runs on ENTRY to DSS_SLAM_UP, 44 frames early,
    // and parks the position in actVX/actVY -- fields the boss never uses as a
    // velocity, because it never walks.  Audit finding 4.
    Actors actors;
    actors.clear();
    const int sora = actors.spawn(ActType::Sora, World::fromRaw(6000),
                                  World::fromRaw(2600));
    const int boss = actors.spawn(ActType::Darkside, World::fromRaw(4224),
                                  World::fromRaw(1920));
    actors.hp[boss] = uint8_t(DS_MAX_HP);
    actors.timer[boss] = 0;
    actors.anim[boss] = 0;              // so the toggle picks the fist

    Dialogue dlg;
    Pad pad;
    Rng rng;
    SceneGround ground;
    WorldState w;
    ScreenFx fx;
    SceneView view{actors, dlg, pad, ground, rng};
    view.player = sora;

    CHECK(!playerUnderBoss(actors, boss, sora));
    updateWorld(w, view, fx);
    CHECK_EQ(int(actors.state[boss]), int(BossState::SlamUp));
    // The mark is where he was standing NOW.
    CHECK_EQ(actors.vx[boss].raw(), 6000);
    CHECK_EQ(actors.vy[boss].raw(), 2600);

    // Walk him a long way off; the mark must not follow.
    actors.x[sora] = World::fromRaw(3000);
    actors.y[sora] = World::fromRaw(3000);
    for (int f = 0; f < DS_SLAM_WIND + 1; ++f) updateWorld(w, view, fx);
    CHECK_EQ(int(actors.state[boss]), int(BossState::SlamHit));
    CHECK_EQ(actors.vx[boss].raw(), 6000);      // still the old mark
    CHECK_EQ(actors.vy[boss].raw(), 2600);
    // ...and the Shadow crawled out THERE, not where he is now.
    int shadow = -1;
    for (int i = 0; i < MAX_ACTORS; ++i)
        if (actors.type[i] == ActType::Shadow) shadow = i;
    CHECK(shadow >= 0);
    CHECK_EQ(actors.x[shadow].raw(), 6000);
    // ...and he is unhurt -- which he would be even standing ON the mark, see
    // world_the_fist_can_never_actually_hit_anyone below.
    CHECK_EQ(actors.hp[sora], uint8_t(SORA_MAX_HP));
}

KH_TEST(world_a_boss_cannot_be_hit_twice_inside_its_flinch) {
    // HurtBoss is gated on actHitT where HurtHeartless is not: a Shadow can be
    // hit again while it recoils and a boss cannot, which is what makes a boss
    // fight a rhythm rather than a mash.
    Actors actors;
    actors.clear();
    const int sora = actors.spawn(ActType::Sora, World::fromRaw(4224),
                                  World::fromRaw(2400));
    const int boss = actors.spawn(ActType::Darkside, World::fromRaw(4224),
                                  World::fromRaw(1920));
    actors.hp[boss] = uint8_t(DS_MAX_HP);
    actors.timer[boss] = 200;           // parked in Rest, out of the way
    actors.dir[sora] = Dir::N;

    Dialogue dlg;
    Pad pad;
    Rng rng;
    SceneGround ground;
    WorldState w;
    ScreenFx fx;
    SceneView view{actors, dlg, pad, ground, rng};
    view.player = sora;

    int swings = 0;
    for (int f = 0; f < 200; ++f) {
        // Swing whenever he is idle and the freeze has lifted.
        const bool canSwing = w.hitStop == 0
                           && actors.state[sora] == ActState::Idle;
        pad.held = canSwing ? raw(Button::B) : uint16_t(0);
        pad.pressed = pad.held;
        if (canSwing) ++swings;
        updateWorld(w, view, fx);
    }
    CHECK(swings > 4);                          // he really did swing repeatedly
    // Every landed hit costs exactly one HP and arms a ten-frame flinch, so the
    // damage is bounded by the swings and not by the frames.
    CHECK(actors.hp[boss] < uint8_t(DS_MAX_HP));
    CHECK_EQ(int(w.bossHP), int(actors.hp[boss]));
    CHECK(int(DS_MAX_HP) - int(actors.hp[boss]) <= swings);
}

KH_TEST(world_the_fist_can_never_actually_hit_anyone) {
    // A BUG IN THE SNES BUILD, reproduced deliberately.  DarksideSlam holds the
    // impact point in tmp0/tmp1 and spawns a Shadow there -- and SpawnActor
    // ends with SetActorZ, which leaves the new actor's position SHIFTED DOWN
    // BY FOUR in those same two slots.  The damage test that follows therefore
    // compares a Q12.4 position against one sixteenth of one, and misses by
    // three thousand nine hundred and sixty units.
    //
    // Found by the oracle, not by reading: a fist landing exactly on Sora left
    // him at full HP with hitStopTimer zero, and the DS port -- which did not
    // have the bug -- ran its SlamHit three frames long because of the hit-stop
    // the SNES never incurred.  That three-frame difference is the whole of the
    // evidence, and no hand-written test would have produced it.
    Actors actors;
    actors.clear();
    const int sora = actors.spawn(ActType::Sora, World::fromRaw(6000),
                                  World::fromRaw(2600));
    const int boss = actors.spawn(ActType::Darkside, World::fromRaw(4224),
                                  World::fromRaw(1920));
    actors.hp[boss] = uint8_t(DS_MAX_HP);
    actors.timer[boss] = 0;
    actors.anim[boss] = 0;

    Dialogue dlg;
    Pad pad;
    Rng rng;
    SceneGround ground;
    WorldState w;
    ScreenFx fx;
    SceneView view{actors, dlg, pad, ground, rng};
    view.player = sora;

    updateWorld(w, view, fx);                   // aims, enters SlamUp
    CHECK_EQ(actors.vx[boss].raw(), 6000);
    // Sora does not move an inch: the fist comes down exactly on him.
    for (int f = 0; f < DS_SLAM_WIND + 1; ++f) updateWorld(w, view, fx);
    CHECK_EQ(int(actors.state[boss]), int(BossState::SlamHit));
    CHECK_EQ(actors.hp[sora], uint8_t(SORA_MAX_HP));    // ...and nothing happens
    CHECK_EQ(int(actors.state[sora]), int(ActState::Idle));
    CHECK_EQ(int(w.hitStop), 0);                        // no connect, no freeze

    // The sweep, by contrast, reads no scratch after a spawn and does connect.
    actors.x[sora] = actors.x[boss];
    actors.y[sora] = actors.y[boss] + World::fromRaw(200);
    actors.state[boss] = ActState::Idle;
    actors.timer[boss] = 0;
    updateWorld(w, view, fx);
    CHECK_EQ(int(actors.state[boss]), int(BossState::SweepUp));
    for (int f = 0; f < DS_SWEEP_WIND + 1; ++f) updateWorld(w, view, fx);
    CHECK(actors.hp[sora] < uint8_t(SORA_MAX_HP));
}
