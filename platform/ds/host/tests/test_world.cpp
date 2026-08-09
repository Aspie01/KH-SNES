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

// ---------------------------------------------------------------------------
// The Guard Armor, against the oracle.  Reached the same way as Darkside:
//
//     tools/snes_trace.py --frames 400 --input <two A presses>
//         --poke sceneId=6 --poke townStage=5 --poke deadFlag=2 --no-strict
//
// TownRestart sets townTimer to 1 when townStage is T_BOSS, and the ROM brings
// the armour down from the top by itself.
// ---------------------------------------------------------------------------
KH_TEST(world_the_guard_armor_runs_its_whole_cycle_as_the_snes_did) {
    Actors actors;
    actors.clear();
    // Exactly the oracle's geometry: Sora at tile (16,10), the armour at (16,7),
    // so dx is zero and it never walks -- and the fist lands on him every time.
    const int sora = actors.spawn(ActType::Sora, tileCentre(16), tileCentre(10));
    const int armor = actors.spawn(ActType::Armor, World::fromRaw(4224),
                                   World::fromRaw(1920));
    const int left = actors.spawn(ActType::Gauntlet, World::fromRaw(4224),
                                  World::fromRaw(1920));
    const int right = actors.spawn(ActType::Gauntlet, World::fromRaw(4224),
                                   World::fromRaw(1920));
    actors.anim[left] = 0;              // SpawnHands numbers them 0 then 1
    actors.anim[right] = 1;
    actors.hp[armor] = uint8_t(GA_MAX_HP);
    actors.z[armor] = uint8_t(GA_DROP_Z);
    actors.timer[armor] = uint8_t(GA_DROP);     // state is Drop by being zero

    Dialogue dlg;
    Pad pad;
    Rng rng;
    SceneGround ground;
    WorldState w;
    ScreenFx fx;
    SceneView view{actors, dlg, pad, ground, rng};
    view.player = sora;

    CHECK_EQ(actors.x[sora].raw(), 4224);
    CHECK_EQ(actors.y[sora].raw(), 2688);

    // The oracle's state changes, as durations.  Every one is its constant plus
    // one, EXCEPT where a hit-stop lands inside it -- and the hit-stops are the
    // interesting part, because they are the boss reaching out of its own state
    // machine and freezing the world:
    //   Walk  = 97 + 8   the landing freeze, the heaviest in the game
    //   Slam  = 21 + 3   the fist connecting, which Darkside's never does
    struct Leg { int state; int frames; };
    const Leg LEGS[] = {
        {int(ArmorState::Drop), GA_DROP + 1},
        {int(ArmorState::Walk), GA_WALK_LEN + 1 + 8},
        {int(ArmorState::Wind), GA_SLAM_WIND + 1},
        {int(ArmorState::Slam), GA_SLAM_HOLD + 1 + 3},
        {int(ArmorState::Rest), GA_REST + 1},
        {int(ArmorState::Walk), GA_WALK_LEN + 1},       // no landing this time
    };

    int leg = 0, spent = 0;
    bool hitDuringSlam = false;
    for (int f = 0; f < 500 && leg < int(sizeof LEGS / sizeof *LEGS); ++f) {
        CHECK_EQ(int(actors.state[armor]), LEGS[leg].state);
        if (LEGS[leg].state == int(ArmorState::Wind) && spent == 0) {
            // The mark is taken on ENTRY to the wind-up, 40 frames early, and it
            // is where Sora is standing NOW.  The right hand goes there, and the
            // left keeps station -- which is the whole tell.
            CHECK_EQ(actors.vx[armor].raw(), 4224);
            CHECK_EQ(actors.vy[armor].raw(), 2688);
            CHECK_EQ(actors.x[right].raw(), 4224);
            CHECK_EQ(actors.y[right].raw(), 2688);
            CHECK_EQ(actors.x[left].raw(), 4224 - GA_HAND_R.raw());
            CHECK_EQ(actors.y[left].raw(), 1920 - GA_HAND_UP.raw());
            CHECK_EQ(int(actors.z[right]), GA_HAND_HIGH);   // wound up, clear
        }
        updateWorld(w, view, fx);
        if (LEGS[leg].state == int(ArmorState::Slam) && actors.hp[sora] < 20)
            hitDuringSlam = true;
        if (++spent == LEGS[leg].frames) { ++leg; spent = 0; }
    }
    CHECK_EQ(leg, int(sizeof LEGS / sizeof *LEGS));
    // The fist CONNECTS, where Darkside's cannot: ArmorSlam spawns nothing, so
    // the scratch it reads back is still the mark.  Audit finding 59.
    CHECK(hitDuringSlam);
    CHECK_EQ(actors.hp[sora], uint8_t(SORA_MAX_HP - 1));
}

KH_TEST(world_the_hands_are_not_placed_until_the_armour_lands) {
    // At the first frame of the oracle's trace both gauntlets are still at the
    // armour's spawn point, not at their stations -- because the Drop branch
    // returns without calling PlaceHands.  Every other branch calls it, so this
    // is the one frame the hands are anywhere else, and it is deliberate: they
    // arrive with the body rather than reaching out ahead of it.
    Actors actors;
    actors.clear();
    const int sora = actors.spawn(ActType::Sora, tileCentre(16), tileCentre(10));
    const int armor = actors.spawn(ActType::Armor, World::fromRaw(4224),
                                   World::fromRaw(1920));
    const int left = actors.spawn(ActType::Gauntlet, World::fromRaw(4224),
                                  World::fromRaw(1920));
    actors.anim[left] = 0;
    actors.hp[armor] = uint8_t(GA_MAX_HP);
    actors.timer[armor] = uint8_t(GA_DROP);

    Dialogue dlg;
    Pad pad;
    Rng rng;
    SceneGround ground;
    WorldState w;
    ScreenFx fx;
    SceneView view{actors, dlg, pad, ground, rng};
    view.player = sora;

    for (int f = 0; f < GA_DROP; ++f) {
        updateWorld(w, view, fx);
        CHECK_EQ(actors.x[left].raw(), 4224);        // still where it spawned
        CHECK_EQ(actors.y[left].raw(), 1920);
        // ...and the screen is shaking on a four-frame period the whole way.
        CHECK(fx.shakeX == 3 || fx.shakeX == -3);
        view.frame = uint32_t(f + 1);
    }
    // The landing places them, stops the shake and freezes the world hardest.
    updateWorld(w, view, fx);
    CHECK_EQ(int(fx.shakeX), 0);
    CHECK_EQ(int(actors.z[armor]), 0);
    CHECK_EQ(int(w.hitStop), 8);
    CHECK_EQ(actors.x[left].raw(), 4224 - GA_HAND_R.raw());
    CHECK_EQ(actors.y[left].raw(), 1920 - GA_HAND_UP.raw());
}

KH_TEST(world_the_armour_closes_horizontally_and_stops_two_tiles_short) {
    // StepArmor is the only walking AI in the game and it is one axis: it never
    // moves vertically at all, which is what makes the Second District's fight a
    // left-right dance rather than a chase.
    Actors actors;
    actors.clear();
    const int sora = actors.spawn(ActType::Sora, tileCentre(4), tileCentre(10));
    // Twelve tiles apart, which GA_WALK closes with frames to spare: at 20 a
    // frame it needs 77 of its 96 to get inside GA_STOP.  Starting it further
    // out would only prove the walk is slower than the timer, which it is.
    const int armor = actors.spawn(ActType::Armor, tileCentre(12), tileCentre(7));
    actors.hp[armor] = uint8_t(GA_MAX_HP);
    actors.state[armor] = static_cast<ActState>(uint8_t(ArmorState::Walk));
    actors.timer[armor] = uint8_t(GA_WALK_LEN);

    Dialogue dlg;
    Pad pad;
    Rng rng;
    // A wide open floor, so the walk is not what stops it.
    unsigned char coll[64 * 32], hgt[64 * 32];
    for (unsigned i = 0; i < sizeof coll; ++i) { coll[i] = 1; hgt[i] = 0; }
    SceneGround ground;
    ground.set(Blob{coll, sizeof coll}, Blob{hgt, sizeof hgt}, 64, 32);
    WorldState w;
    ScreenFx fx;
    SceneView view{actors, dlg, pad, ground, rng};
    view.player = sora;

    const World y0 = actors.y[armor];
    for (int f = 0; f < GA_WALK_LEN; ++f) {
        updateWorld(w, view, fx);
        CHECK_EQ(actors.y[armor].raw(), y0.raw());      // never vertically
    }
    // It closed westward and stopped short rather than standing on him.
    CHECK(actors.x[armor].raw() < tileCentre(12).raw());
    const World gap = actors.x[armor] - actors.x[sora];
    CHECK(gap.raw() > 0);
    CHECK(gap.raw() <= GA_STOP.raw());
    CHECK_EQ(actors.vx[armor].raw(), 0);                // and is not jittering
}

// ---------------------------------------------------------------------------
// Riku's race, against the oracle.  Reached with a two-stage poke:
//
//     tools/snes_trace.py --frames 700 --input <two A presses>
//         --poke sceneId=3 --poke deadFlag=2
//         --poke 30:questState=6 --poke 30:rikuWp=0 --no-strict
//
// The first pair restarts onto the island so InitWorld builds the cast; the
// second pair, thirty frames later, starts the race on top of that.  He is
// therefore running from where he SITS -- tile (27,8) on the small island --
// rather than from the start line, which is why this takes longer than the 615
// frames finding 58 computes from START_RIKU.  That is fine and in fact better:
// it exercises the waypoint walk from an arbitrary position.
// ---------------------------------------------------------------------------
KH_TEST(world_riku_runs_the_course_exactly_as_the_snes_did) {
    Actors actors;
    actors.clear();
    const int sora = actors.spawn(ActType::Sora, tileCentre(11), tileCentre(12));
    const int riku = actors.spawn(ActType::Riku, World::fromRaw(7040),
                                  World::fromRaw(2176));
    CHECK(riku > 0);

    Dialogue dlg;
    Pad pad;
    Rng rng;
    // NO GROUND AT ALL, deliberately.  Riku never calls tryMoveActor, so an
    // invalid SceneGround cannot affect him -- and if a future change made him
    // collide, this test would stop matching immediately.  Audit finding 9.
    SceneGround ground;
    WorldState w;
    ScreenFx fx;
    // updateRiku is driven directly here, not through updateWorld: he is the one
    // actor whose behaviour depends on nothing in SceneView at all -- not the
    // ground, not the player, not the RNG -- and calling him directly says so.
    (void)dlg; (void)pad; (void)ground; (void)rng; (void)fx; (void)sora;
    w.raceRunning = true;
    w.rikuWp = 0;

    // The oracle's positions.  Frames are relative to the race starting, so the
    // trace's frame 30 is this loop's frame 0.
    struct At { int frame; int x; int y; };
    const At ORACLE_RIKU[] = {
        {0, 7023, 2193},        // -17 / +17, the diagonal, on the very first frame
        {1, 7006, 2210},
        {30, 6513, 2703},
        {60, 6003, 3200},       // arrived on waypoint 0's row and turned
        {61, 5986, 3200},       // ...now purely eastward-of-west along it
        {90, 5493, 3200},
        {164, 4269, 3200},
    };

    // The oracle samples AFTER a frame's work, so trace frame 30 is one update
    // in.  Update first, then compare -- getting this phase wrong is the easiest
    // way to make a fixture that is off by one everywhere and looks like a
    // velocity bug.
    int checked = 0;
    for (int u = 1; u <= 165; ++u) {
        updateRiku(w, actors, riku);
        for (const At& e : ORACLE_RIKU) {
            if (e.frame + 1 != u) continue;
            CHECK_EQ(actors.x[riku].raw(), e.x);
            CHECK_EQ(actors.y[riku].raw(), e.y);
            ++checked;
        }
    }
    CHECK_EQ(checked, int(sizeof ORACLE_RIKU / sizeof *ORACLE_RIKU));
    // He got past the first marker and is working along the course.
    CHECK(w.rikuWp > 0);
    CHECK(w.rikuWp < uint8_t(RACE_WPS));
}

KH_TEST(world_riku_stands_still_unless_the_race_is_running) {
    // He is the only actor whose update is gated on a QUEST state rather than on
    // his own, which is why WorldState carries the mirror at all.
    Actors actors;
    actors.clear();
    const int sora = actors.spawn(ActType::Sora, tileCentre(11), tileCentre(12));
    const int riku = actors.spawn(ActType::Riku, tileCentre(13), tileCentre(12));
    Dialogue dlg;
    Pad pad;
    Rng rng;
    SceneGround ground;
    WorldState w;
    ScreenFx fx;
    SceneView view{actors, dlg, pad, ground, rng};
    view.player = sora;

    const World x0 = actors.x[riku], y0 = actors.y[riku];
    for (int f = 0; f < 120; ++f) updateWorld(w, view, fx);
    CHECK_EQ(actors.x[riku].raw(), x0.raw());
    CHECK_EQ(actors.y[riku].raw(), y0.raw());

    // ...and once he is home he stops dead rather than looping.
    w.raceRunning = true;
    w.rikuWp = uint8_t(RACE_WPS);
    for (int f = 0; f < 120; ++f) updateWorld(w, view, fx);
    CHECK_EQ(actors.x[riku].raw(), x0.raw());
    CHECK_EQ(actors.y[riku].raw(), y0.raw());
}

KH_TEST(world_a_fish_drifts_on_a_64_frame_cycle_and_never_collides) {
    // The one actor with no lifetime and a timer that COUNTS UP.  Two bits do
    // all the work: the low three pick the cel, bit 5 picks the direction -- so a
    // 64-frame there-and-back costs no state beyond the timer it already had.
    Actors actors;
    actors.clear();
    const int fish = actors.spawn(ActType::Fish, tileCentre(8), tileCentre(6));
    CHECK(fish >= 0);
    actors.timer[fish] = 0;
    const World x0 = actors.x[fish], y0 = actors.y[fish];

    // The timer is INCREMENTED FIRST and then tested, so the turn happens ON the
    // thirty-second frame rather than after it: frames 1-31 go east and frame 32
    // has already come back one.  Thirty-one out and one back is +30, not +32,
    // and a test that assumed the tidier number is what caught this.
    for (int f = 0; f < 32; ++f) updateFish(actors, fish);
    CHECK_EQ(actors.x[fish].raw(), x0.raw() + 30 * FISH_SWIM.raw());
    CHECK_EQ(actors.y[fish].raw(), y0.raw());       // and never any vertical drift
    CHECK(has(actors.flags[fish], ActFlags::HFlip));     // turned, at bit 5

    // Frames 33-63 continue west and 64 turns back east, so the cycle closes
    // exactly: 31 east, 32 west, 1 east.
    for (int f = 0; f < 32; ++f) updateFish(actors, fish);
    CHECK_EQ(actors.x[fish].raw(), x0.raw());
    CHECK(!has(actors.flags[fish], ActFlags::HFlip));

    // A full cycle is 64 frames and it returns to the start every time, which is
    // what keeps a fish inside the shallows without any collision at all.
    for (int cycle = 0; cycle < 4; ++cycle)
        for (int f = 0; f < 64; ++f) updateFish(actors, fish);
    CHECK_EQ(actors.x[fish].raw(), x0.raw());
    CHECK_EQ(int(actors.type[fish]), int(ActType::Fish));   // and never expires
}

KH_TEST(world_a_mote_rises_on_its_own_velocity_and_deletes_itself) {
    Actors actors;
    actors.clear();
    const int mote = actors.spawn(ActType::Mote, tileCentre(16), tileCentre(11));
    actors.timer[mote] = uint8_t(MOTE_LIFE);
    actors.vx[mote] = World::fromRaw(0);
    actors.vy[mote] = World::fromRaw(int16_t(-MOTE_RISE.raw()));   // upward
    const World y0 = actors.y[mote];

    for (int f = 0; f < MOTE_LIFE; ++f) {
        updateMote(actors, mote);
        CHECK_EQ(int(actors.type[mote]), int(ActType::Mote));
    }
    // MOTE_RISE a frame for MOTE_LIFE frames: 96 * 40 = 3840, or 240 px.
    CHECK_EQ(actors.y[mote].raw(), y0.raw() - MOTE_LIFE * MOTE_RISE.raw());
    // The frame that reads zero is the one that frees the slot -- so a mote
    // costs MOTE_LIFE frames of motion and one more to disappear.
    updateMote(actors, mote);
    CHECK_EQ(int(actors.type[mote]), int(ActType::None));
}
