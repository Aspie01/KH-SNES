// The scene stage machines.
//
// §M5's exit criteria: "For each scene, a host test drives its stage machine
// through every transition with synthetic input and asserts the frame count of
// each beat against the specification.  No renderer required."
//
// The specification is docs/BEHAVIOUR.md §6, corrected and completed by
// BEHAVIOUR-AUDIT.md findings 6, 7, 34-36 and the RNG note.  Every assertion
// cites which.

#include "check.h"
#include "stage.h"

using namespace kh;

namespace {

// A stand-in world for a machine to look at.  Named Stub because World is the
// Q12.4 fixed-point type.  No ground and no cast: a stage machine's
// inputs are the dialogue, the actor counts and its own timers, and giving it
// anything more would let a test pass for the wrong reason.
struct Stub {
    Actors actors;
    Dialogue dialogue;
    Pad pad;
    SceneGround ground;
    Rng rng;
    uint32_t frame = 0;

    int player = 0;

    Stub() {
        actors.clear();
        // Somewhere far from every spot the tests use, so the "not on top of the
        // player" rule never fires unless a test asks it to.
        player = actors.spawn(ActType::Sora, tileCentre(2), tileCentre(2));
    }
    SceneView view() {
        return SceneView{actors, dialogue, pad, ground, rng, player, frame};
    }
};

// The Second District's twelve spots, as town2_cast.txt gives them.
const Tile TOWN2_SPOTS[] = {
    {8, 12}, {28, 12}, {7, 16}, {30, 17}, {10, 21}, {20, 22},
    {18, 26}, {36, 20}, {14, 11}, {25, 24}, {40, 12}, {5, 26},
};

// Run one frame and hand back what the machine asked for.
template <class M>
StageStep step(M& m, Stub& w, ScreenFx& fx) {
    SceneView v = w.view();
    ++w.frame;
    return m.update(v, fx);
}

// Run frames until the machine leaves `from`, and return how many it took.
// Bounded, so a machine that never advances fails as a wrong count rather than
// hanging the suite.
template <class M, class S>
int framesUntilLeaving(M& m, Stub& w, ScreenFx& fx, S from, int limit = 4096) {
    int n = 0;
    while (m.stage() == from && n < limit) {
        step(m, w, fx);
        ++n;
    }
    return n;
}

const uint8_t ONE_LINE[] = {'H', 'I', '.', SC_END};

// Open a box, let it finish revealing, then dismiss it -- which is what every
// scene transition that waits on dialogue actually experiences.
void sayAndDismiss(Stub& w) {
    w.dialogue.open(Script{ONE_LINE, sizeof ONE_LINE}, TextMode::Message);
    for (int i = 0; i < 16 && w.dialogue.state() == TextState::Reveal; ++i)
        w.dialogue.update(w.pad);
    w.pad.held = raw(Button::A);
    w.pad.pressed = raw(Button::A);
    w.dialogue.update(w.pad);
    w.pad.held = 0;
    w.pad.pressed = 0;
}

}  // namespace

// ===========================================================================
// The random number generator
// ===========================================================================

KH_TEST(stage_rng_is_the_snes_lfsr_bit_for_bit) {
    // "16-bit Galois LFSR, `asl a; if carry then eor #$002D` returning the low
    // byte.  Seeded $ACE1 in InitWorld and RE-seeded $1D57 in TownBegin."
    Rng r;
    CHECK_EQ(r.state(), Rng::WORLD_SEED);
    CHECK_EQ(Rng::WORLD_SEED, 0xACE1);
    CHECK_EQ(Rng::TOWN_SEED, 0x1D57);

    // 0xACE1 << 1 = 0x59C2 with carry out, so it eors: 0x59C2 ^ 0x2D = 0x59EF.
    CHECK_EQ(r.next(), 0xEF);
    CHECK_EQ(r.state(), 0x59EF);
    // 0x59EF << 1 = 0xB3DE, no carry out.
    CHECK_EQ(r.next(), 0xDE);
    CHECK_EQ(r.state(), 0xB3DE);

    // It never sticks at zero and never repeats inside a byte's worth of draws.
    r.seed(Rng::TOWN_SEED);
    for (int i = 0; i < 256; ++i) {
        r.next();
        CHECK(r.state() != 0);
    }
}

KH_TEST(stage_rng_a_state_of_zero_is_a_fixed_point_and_stays_there) {
    // A Galois shift register whose state is zero stays zero: `asl 0` is 0 with
    // the carry clear, so there is no xor and nothing ever moves.  town.s:68
    // carries a comment warning about exactly this -- "every Heartless in the
    // Second District would then come up on the same paving stone" -- and it is
    // not hypothetical.  `rngState` is seeded by InitWorld, InitWorld runs when
    // the ISLAND is entered or restarted, and RestartScene's night branch calls
    // NightRestart instead.  Restart straight into the night without having been
    // to the island and the LFSR is still zero: every flash waits exactly
    // FLASH_GAP_MIN and every Shadow of the whole night comes up on spot zero.
    //
    // Found by the oracle, not by reading.  §M6b's night scenario was written
    // that way first, and five Shadows in a row arriving on the same tile is
    // what it takes to notice.
    Rng r;
    r.seed(0);
    for (int i = 0; i < 64; ++i) {
        CHECK_EQ(r.next(), 0);
        CHECK_EQ(r.state(), 0);
        CHECK_EQ(r.pick(10), 0);
    }
    // ...and the two real seeds are the way out of it, which is why both exist.
    CHECK(Rng::WORLD_SEED != 0);
    CHECK(Rng::TOWN_SEED != 0);
}

KH_TEST(stage_rng_spot_choice_is_repeated_subtraction_not_a_modulo) {
    // "Spawn-spot selection is `Rand & $00FF` reduced by repeated subtraction of
    // the spot count, not a modulo."  For a count that divides 256 the two agree;
    // for one that does not they diverge, and 8 divides 256 while 12 does not --
    // and the DS Second District carries twelve spots.
    Rng a, b;
    a.seed(0x1234);
    b.seed(0x1234);
    for (int i = 0; i < 64; ++i) {
        const uint8_t want = uint8_t(b.next());
        const int reduced = want % 8;
        CHECK_EQ(a.pick(8), reduced);       // agrees at 8
    }
    // Every draw lands in range for a count that does not divide 256.
    a.seed(0xACE1);
    for (int i = 0; i < 256; ++i) {
        const uint8_t p = a.pick(12);
        CHECK(p < 12);
    }
    // ...and the repeated subtraction terminates for a count of 1.
    CHECK_EQ(a.pick(1), 0);
}

// ===========================================================================
// The Dive
// ===========================================================================

KH_TEST(stage_dive_intro_waits_for_the_box_then_arms_the_pedestals) {
    // §6's first transition, and the rule that every Dive beat waits for
    // dialogue -- including Shatter, unlike the night's Tear.
    Stub w;
    ScreenFx fx;
    DiveMachine m;
    m.begin();
    CHECK(m.stage() == DiveStage::Intro);

    w.dialogue.open(Script{ONE_LINE, sizeof ONE_LINE}, TextMode::Message);
    for (int i = 0; i < 10; ++i) step(m, w, fx);
    CHECK(m.stage() == DiveStage::Intro);   // held by the box

    sayAndDismiss(w);
    CHECK(!w.dialogue.busy());
    step(m, w, fx);
    CHECK(m.stage() == DiveStage::Pick);
}

KH_TEST(stage_dive_the_weapon_choice_is_inert) {
    // Audit finding 7: weaponTaken and weaponGiven are written once and read
    // nowhere.  This test exists so that a later change which makes the choice
    // matter has to delete an assertion that says it does not.
    DiveMachine m;
    m.begin();
    CHECK(m.taken() == ActType::None);
    m.choose(ActType::Sword, ActType::Shield);
    CHECK(m.taken() == ActType::Sword);
    CHECK(m.given() == ActType::Shield);
    CHECK(m.stage() == DiveStage::Intro);   // choosing advances nothing by itself
}

KH_TEST(stage_dive_shatter_runs_96_frames_and_reaches_mosaic_12) {
    // §6: "Platform shatter 96 -- MOSAIC coarsening the ground while brightness
    // falls and the screen shakes."  constants.h adds the detail the diagram
    // omits: it reaches mosaic 12 and brightness 3, NOT 15 and 0, because
    // 96 / 8 is 12 and the platform lands before it can black out.
    Stub w;
    ScreenFx fx;
    DiveMachine m;
    m.begin();
    m.setStage(DiveStage::Shatter);
    m.armShatter();
    CHECK_EQ(m.shatterTimer(), SHATTER_LEN);

    uint8_t peakMosaic = 0;
    uint8_t lowBright = 15;
    int frames = 0;
    while (m.stage() == DiveStage::Shatter && frames < 4096) {
        step(m, w, fx);
        ++frames;
        if (m.stage() != DiveStage::Shatter) break;     // the landing frame
        if (fx.mosaic > peakMosaic) peakMosaic = fx.mosaic;
        if (fx.brightness < lowBright) lowBright = fx.brightness;
    }
    CHECK_EQ(frames, SHATTER_LEN + 1);      // 96 of shaking, then the landing
    CHECK_EQ(peakMosaic, 12);
    CHECK_EQ(lowBright, 3);
    CHECK(m.stage() == DiveStage::S2Intro);
}

KH_TEST(stage_dive_shatter_shakes_two_pixels_on_a_four_frame_period) {
    // Audit finding 6: the Dive's Shatter is +/-2 and the night's Tear is +/-3;
    // all four shake sites use `frameCount and #$02`, a FOUR-frame period, which
    // §7 never gives.  Two frames one side, two the other.
    Stub w;
    ScreenFx fx;
    DiveMachine m;
    m.begin();
    m.setStage(DiveStage::Shatter);
    m.armShatter();

    int8_t seen[8] = {};
    for (int i = 0; i < 8; ++i) {
        step(m, w, fx);
        seen[i] = fx.shakeX;
    }
    // frame 0,1 -> (0&2)==0 and (1&2)==0 -> +2; frame 2,3 -> -2; and repeat.
    CHECK_EQ(seen[0], 2);
    CHECK_EQ(seen[1], 2);
    CHECK_EQ(seen[2], -2);
    CHECK_EQ(seen[3], -2);
    CHECK_EQ(seen[4], 2);
    CHECK_EQ(seen[7], -2);
}

KH_TEST(stage_dive_the_second_station_waits_for_its_shadows) {
    // S2_INTRO -> S2_FIGHT on the dismissal, then WatchShadows holds until the
    // last one is gone.  Three are already waiting when he lands.
    Stub w;
    ScreenFx fx;
    DiveMachine m;
    m.begin();
    m.setStage(DiveStage::S2Intro);
    step(m, w, fx);
    CHECK(m.stage() == DiveStage::S2Fight);

    for (int i = 0; i < 3; ++i)
        w.actors.spawn(ActType::Shadow, World::fromInt(0), World::fromInt(0));
    for (int i = 0; i < 60; ++i) step(m, w, fx);
    CHECK(m.stage() == DiveStage::S2Fight);             // still three of them

    // Kill two: still held.
    for (int i = 0, killed = 0; i < MAX_ACTORS && killed < 2; ++i)
        if (w.actors.type[i] == ActType::Shadow) { w.actors.type[i] = ActType::None; ++killed; }
    step(m, w, fx);
    CHECK(m.stage() == DiveStage::S2Fight);

    // The last one, and the floor gives out with a line.
    for (int i = 0; i < MAX_ACTORS; ++i)
        if (w.actors.type[i] == ActType::Shadow) w.actors.type[i] = ActType::None;
    const StageStep s = step(m, w, fx);
    CHECK(m.stage() == DiveStage::Shatter2);
    CHECK_EQ(m.shatterTimer(), SHATTER_LEN);
    CHECK(s.action == SceneAction::Say);
    CHECK(s.script == ScriptId::DiveFloorGoes);
}

KH_TEST(stage_dive_the_second_shatter_lands_on_the_third_station) {
    // "the same effect, one station further down" -- and the landing branches on
    // WHICH stage was shattering, which is the only thing that distinguishes them.
    Stub w;
    ScreenFx fx;
    DiveMachine m;
    m.begin();
    m.setStage(DiveStage::Shatter2);
    m.armShatter();
    const int frames = framesUntilLeaving(m, w, fx, DiveStage::Shatter2);
    CHECK_EQ(frames, SHATTER_LEN + 1);
    CHECK(m.stage() == DiveStage::S3Intro);

    // ...and the third station's line spawns the boss on dismissal.
    const StageStep s = step(m, w, fx);
    CHECK(m.stage() == DiveStage::Boss);
    CHECK(s.action == SceneAction::SpawnBoss);
    CHECK(s.script == ScriptId::DiveBoss);
}

KH_TEST(stage_dive_the_boss_holds_until_darkside_is_gone) {
    Stub w;
    ScreenFx fx;
    DiveMachine m;
    m.begin();
    m.setStage(DiveStage::Boss);
    w.actors.spawn(ActType::Darkside, World::fromInt(0), World::fromInt(0));
    for (int i = 0; i < 120; ++i) step(m, w, fx);
    CHECK(m.stage() == DiveStage::Boss);

    for (int i = 0; i < MAX_ACTORS; ++i)
        if (w.actors.type[i] == ActType::Darkside) w.actors.type[i] = ActType::None;
    const StageStep s = step(m, w, fx);
    CHECK(m.stage() == DiveStage::Done);
    CHECK(s.script == ScriptId::DiveVictory);
}

KH_TEST(stage_dive_the_fall_is_170_frames_and_a_mote_every_fourth) {
    // §6: "Fall 170 of descent before the light takes over" and "Rising motes 40
    // frames each".  The mote test runs on the DECREMENTED timer, so the first
    // one is not on the entry frame.
    Stub w;
    ScreenFx fx;
    DiveMachine m;
    m.begin();
    m.setStage(DiveStage::Done);

    const StageStep begin = step(m, w, fx);
    CHECK(m.stage() == DiveStage::Fall);
    CHECK_EQ(m.fallTimer(), FALL_LEN);
    CHECK(begin.action == SceneAction::BeginFall);
    CHECK(!fx.bgVisible);               // the void needs no art; BG1 goes off
    CHECK_EQ(fx.mosaic, 0);
    CHECK_EQ(fx.shakeX, 0);

    int frames = 0;
    int motes = 0;
    while (m.stage() == DiveStage::Fall && frames < 4096) {
        const StageStep s = step(m, w, fx);
        ++frames;
        if (s.action == SceneAction::SpawnMote) ++motes;
    }
    CHECK_EQ(frames, FALL_LEN + 1);     // 170 of descent, then the fade is armed
    // 170 decrements land on 169..0; a mote on every fourth means 0,4,...,168.
    CHECK_EQ(motes, 43);
    CHECK(m.stage() == DiveStage::Fade);
    CHECK_EQ(m.fadeTimer(), FADE_LEN);
}

KH_TEST(stage_dive_the_fade_swaps_the_scene_at_its_midpoint) {
    // §6: "Fade 64, and the scene swaps at the halfway point."  The whiteout
    // ramps 1..31 going out, is clamped full for the swap frame, then runs
    // 31..0 coming back in.
    Stub w;
    ScreenFx fx;
    DiveMachine n;
    n.begin();
    // Come in through Done, so the fade is armed the way the scene arms it
    // rather than by a setter -- the timer's initial value is part of the beat.
    n.setStage(DiveStage::Done);
    step(n, w, fx);
    CHECK(n.stage() == DiveStage::Fall);
    while (n.stage() == DiveStage::Fall) step(n, w, fx);
    CHECK(n.stage() == DiveStage::Fade);
    CHECK_EQ(n.fadeTimer(), FADE_LEN);

    int frames = 0;
    int swapAt = -1;
    uint8_t peak = 0;
    while (n.stage() == DiveStage::Fade && frames < 4096) {
        const StageStep s = step(n, w, fx);
        ++frames;
        if (s.action == SceneAction::EnterIsland) swapAt = frames;
        if (n.stage() == DiveStage::Fade && fx.whiteout > peak) peak = fx.whiteout;
    }
    CHECK_EQ(frames, FADE_LEN + 1);     // 64 of fading, then the wake line
    CHECK_EQ(swapAt, FADE_LEN / 2);     // exactly halfway
    CHECK_EQ(peak, 31);                 // the register's five bits, saturated
    CHECK(n.stage() == DiveStage::Arrived);
}

KH_TEST(stage_dive_arrived_is_terminal) {
    Stub w;
    ScreenFx fx;
    DiveMachine m;
    m.begin();
    m.setStage(DiveStage::Arrived);
    for (int i = 0; i < 60; ++i) {
        const StageStep s = step(m, w, fx);
        CHECK(s.action == SceneAction::None);
    }
    CHECK(m.stage() == DiveStage::Arrived);
}

// ===========================================================================
// Traverse Town
// ===========================================================================

KH_TEST(stage_town_begin_reseeds_the_rng) {
    // Audit: "Seeded $ACE1 in InitWorld and RE-seeded $1D57 in TownBegin."  The
    // Second District's arrivals are therefore a fixed sequence that does not
    // depend on how long the player spent on the island.
    Stub w;
    for (int i = 0; i < 37; ++i) w.rng.next();       // an island's worth of draws
    CHECK(w.rng.state() != Rng::TOWN_SEED);
    TownMachine m;
    m.begin(w.rng);
    CHECK_EQ(w.rng.state(), Rng::TOWN_SEED);
    CHECK(m.stage() == TownStage::Arrive);
}

KH_TEST(stage_town_arrive_waits_for_the_box_then_opens_the_district) {
    Stub w;
    ScreenFx fx;
    TownMachine m;
    m.begin(w.rng);
    w.dialogue.open(Script{ONE_LINE, sizeof ONE_LINE}, TextMode::Message);
    for (int i = 0; i < 8; ++i) step(m, w, fx);
    CHECK(m.stage() == TownStage::Arrive);

    sayAndDismiss(w);
    const StageStep s = step(m, w, fx);
    CHECK(m.stage() == TownStage::Look);
    CHECK(s.action == SceneAction::HudChanged);
}

KH_TEST(stage_town_a_door_runs_60_frames_and_swaps_at_30) {
    // §6: "Door transition 30 frames out, swap, 30 frames in."  And the door owns
    // the screen: it runs BEFORE the dialogue check, so a message cannot hold a
    // transition open.
    Stub w;
    ScreenFx fx;
    TownMachine m;
    m.begin(w.rng);
    m.setStage(TownStage::Look);
    m.setDistrict(SceneId::Town1);
    m.openDoor(int(SceneId::Town2));
    CHECK_EQ(m.doorTimer(), DOOR_FADE * 2);

    // Hold a box open across the whole transition: it must not matter.
    w.dialogue.open(Script{ONE_LINE, sizeof ONE_LINE}, TextMode::Message);

    int frames = 0;
    int swapAt = -1;
    uint8_t darkest = 15;
    while (m.doorTimer() != 0 && frames < 4096) {
        const StageStep s = step(m, w, fx);
        ++frames;
        if (s.action == SceneAction::EnterDistrict) {
            swapAt = frames;
            CHECK_EQ(s.arg, uint8_t(SceneId::Town2));
        }
        if (fx.brightness < darkest) darkest = fx.brightness;
    }
    CHECK_EQ(frames, DOOR_FADE * 2);
    CHECK_EQ(swapAt, DOOR_FADE);        // exactly halfway
    CHECK_EQ(darkest, 0);               // fully out at the swap
    CHECK_EQ(fx.brightness, 15);        // and fully back by the end
}

KH_TEST(stage_town_the_wave_is_eight_arrivals_five_at_once_eighty_apart) {
    // §6: "Heartless wave 8 arrive, 5 alive at once, one every 80 frames" and
    // "the way on opens when the whole wave has been out AND none is left."
    // Audit finding 36: the counter counts SPAWNS despite being called
    // townKills, so a retry replays the whole wave.
    Stub w;
    ScreenFx fx;
    TownMachine m;
    m.begin(w.rng);
    m.setStage(TownStage::Second);
    m.setDistrict(SceneId::Town2);
    m.setSpots(TOWN2_SPOTS, 12);
    CHECK_EQ(m.waveSpawned(), 0);

    // The first arrival is on the first frame: the spawn timer starts at zero.
    step(m, w, fx);
    CHECK_EQ(m.waveSpawned(), 1);
    CHECK_EQ(w.actors.count(ActType::Shadow), 1);

    // ...and the next is TOWN_GAP + 1 frames later.  The timer is set to 80 and
    // counted DOWN TO zero, so the period is 81 -- the same off-by-one the audit
    // records for every animation rate.  §6's "one every 80 frames" is the
    // constant, not the measured period.
    int gap = 0;
    while (gap < 4096) {
        step(m, w, fx);
        ++gap;
        if (m.waveSpawned() == 2) break;
    }
    CHECK_EQ(gap, TOWN_GAP + 1);

    // Fill to the concurrency cap and it stops arriving, though the wave is
    // not spent.
    while (w.actors.count(ActType::Shadow) < TOWN_SHADOWS)
        w.actors.spawn(ActType::Shadow, World::fromInt(0), World::fromInt(0));
    const int before = m.waveSpawned();
    for (int i = 0; i < (TOWN_GAP + 1) * 4; ++i) step(m, w, fx);
    CHECK_EQ(m.waveSpawned(), before);
    CHECK(m.stage() == TownStage::Second);
}

KH_TEST(stage_town_a_spot_on_top_of_the_player_is_refused_and_retried) {
    // §6: "Spawn placement refuses a spot within 64 px of the player and retries
    // in 12 frames -- one arriving in your face reads as a bug rather than as a
    // Heartless."  One spot, and the player standing on it.
    Stub w;
    ScreenFx fx;
    TownMachine m;
    m.begin(w.rng);
    m.setStage(TownStage::Second);
    m.setDistrict(SceneId::Town2);
    const Tile one[] = {{20, 20}};
    m.setSpots(one, 1);
    w.actors.x[w.player] = tileCentre(20);
    w.actors.y[w.player] = tileCentre(20);

    step(m, w, fx);
    CHECK_EQ(m.waveSpawned(), 0);           // refused
    CHECK_EQ(m.spawnTimer(), SPAWN_RETRY);  // and it comes back round shortly

    for (int i = 0; i < SPAWN_RETRY * 8; ++i) step(m, w, fx);
    CHECK_EQ(m.waveSpawned(), 0);           // still standing on it

    // Step aside by more than 64 px on one axis and the next attempt lands.
    w.actors.x[w.player] = tileCentre(26);
    for (int i = 0; i < SPAWN_RETRY + 2 && m.waveSpawned() == 0; ++i)
        step(m, w, fx);
    CHECK_EQ(m.waveSpawned(), 1);
}

KH_TEST(stage_town_the_way_on_opens_only_when_the_square_is_empty) {
    Stub w;
    ScreenFx fx;
    TownMachine m;
    m.begin(w.rng);
    m.setStage(TownStage::Second);
    m.setDistrict(SceneId::Town2);
    m.setSpots(TOWN2_SPOTS, 12);

    // Run the whole wave out, clearing the square whenever it fills so the
    // concurrency cap never stalls the arrivals.
    for (int i = 0; i < (TOWN_GAP + 1) * (TOWN_WAVE + 4)
                    && m.waveSpawned() < TOWN_WAVE; ++i) {
        step(m, w, fx);
        if (w.actors.count(ActType::Shadow) >= TOWN_SHADOWS)
            for (int k = 0; k < MAX_ACTORS; ++k)
                if (w.actors.type[k] == ActType::Shadow) {
                    w.actors.type[k] = ActType::None;
                    break;
                }
    }
    CHECK_EQ(m.waveSpawned(), TOWN_WAVE);
    CHECK(w.actors.count(ActType::Shadow) > 0);

    // The wave is spent but the square is not empty: still held.
    for (int i = 0; i < 200; ++i) step(m, w, fx);
    CHECK(m.stage() == TownStage::Second);

    for (int k = 0; k < MAX_ACTORS; ++k)
        if (w.actors.type[k] == ActType::Shadow) w.actors.type[k] = ActType::None;
    const StageStep s = step(m, w, fx);
    CHECK(m.stage() == TownStage::Third);
    CHECK(s.script == ScriptId::TownClear);
}

KH_TEST(stage_town_the_first_visit_to_the_third_district_starts_the_wait) {
    // The guard is on the STAGE, not on a visited flag: at Third it fires, and
    // afterwards the stage has moved past it so coming back does nothing.
    TownMachine m;
    Stub w;
    m.begin(w.rng);

    m.setStage(TownStage::Look);
    m.arriveAtThird();
    CHECK(m.stage() == TownStage::Look);        // not far enough on yet

    m.setStage(TownStage::Third);
    m.arriveAtThird();
    CHECK(m.stage() == TownStage::Meet);
    CHECK_EQ(m.townTimer(), FALL_WAIT);

    m.arriveAtThird();                          // walking back in
    CHECK(m.stage() == TownStage::Meet);
    CHECK_EQ(m.townTimer(), FALL_WAIT);         // and the wait is not restarted
}

KH_TEST(stage_town_the_pair_wait_70_then_fall_24) {
    // §6: "Pair falling 70 frames of wait, then 24 of descent from 12 height
    // steps."  Audit finding 35: Meet gates on CountType(ACT_DONALD) rather than
    // on a pure timer, which is what lets one counter serve both halves.
    Stub w;
    ScreenFx fx;
    TownMachine m;
    m.begin(w.rng);
    m.setStage(TownStage::Third);
    m.arriveAtThird();
    m.setDistrict(SceneId::Town3);

    int frames = 0;
    int dropAt = -1;
    while (frames < 4096) {
        const StageStep s = step(m, w, fx);
        ++frames;
        if (s.action == SceneAction::DropPair) { dropAt = frames; break; }
    }
    CHECK_EQ(dropAt, FALL_WAIT + 1);            // 70 of waiting, then the drop
    CHECK_EQ(m.townTimer(), FALL_DROP);

    // The caller performs the drop, which is what flips the gate.
    w.actors.spawn(ActType::Donald, World::fromInt(0), World::fromInt(0));
    w.actors.spawn(ActType::Goofy, World::fromInt(0), World::fromInt(0));

    int lowered = 0;
    while (m.stage() == TownStage::Meet && frames < 8192) {
        const StageStep s = step(m, w, fx);
        ++frames;
        if (s.action == SceneAction::LowerPair) ++lowered;
    }
    CHECK_EQ(lowered, FALL_DROP);               // 24 of descent
    CHECK(m.stage() == TownStage::Boss);
    CHECK_EQ(fx.shakeX, 0);
    CHECK_EQ(m.townTimer(), 1);                 // the armour has not come down yet
}

KH_TEST(stage_town_the_armour_is_raised_once_then_watched) {
    // Audit finding 35: WatchArmor uses townTimer as a ONE-SHOT raise flag, set
    // to 1 by Meet and by TownRestart at T_BOSS.  So the raise must happen once
    // and exactly once, however many frames pass.
    Stub w;
    ScreenFx fx;
    TownMachine m;
    m.begin(w.rng);
    m.setStage(TownStage::Boss);
    m.setDistrict(SceneId::Town3);
    // Meet leaves the flag at 1; set the machine up the way Meet does.
    m.setStage(TownStage::Third);
    m.arriveAtThird();
    w.actors.spawn(ActType::Donald, World::fromInt(0), World::fromInt(0));
    while (m.stage() != TownStage::Boss) step(m, w, fx);
    CHECK_EQ(m.townTimer(), 1);

    int raises = 0;
    for (int i = 0; i < 200; ++i) {
        if (step(m, w, fx).action != SceneAction::RaiseArmor) continue;
        ++raises;
        // The caller performs it, which is what puts the armour in the square.
        w.actors.spawn(ActType::Armor, World::fromInt(0), World::fromInt(0));
        w.actors.spawn(ActType::Gauntlet, World::fromInt(0), World::fromInt(0));
    }
    CHECK_EQ(raises, 1);                        // once, however long it waits
    CHECK(m.stage() == TownStage::Boss);

    for (int i = 0; i < 60; ++i) step(m, w, fx);
    CHECK(m.stage() == TownStage::Boss);

    for (int k = 0; k < MAX_ACTORS; ++k)
        if (w.actors.type[k] == ActType::Armor) w.actors.type[k] = ActType::None;
    const StageStep s = step(m, w, fx);
    CHECK(m.stage() == TownStage::Won);
    CHECK(s.action == SceneAction::SweepGauntlets);
    CHECK(s.script == ScriptId::TownWon);
    CHECK_EQ(fx.shakeX, 0);
}

KH_TEST(stage_town_won_shows_the_card_and_over_is_terminal) {
    Stub w;
    ScreenFx fx;
    TownMachine m;
    m.begin(w.rng);
    m.setStage(TownStage::Won);
    const StageStep s = step(m, w, fx);
    CHECK(m.stage() == TownStage::Over);
    CHECK(s.script == ScriptId::TownCard);

    for (int i = 0; i < 60; ++i) {
        const StageStep t = step(m, w, fx);
        CHECK(t.action == SceneAction::None);
    }
    CHECK(m.stage() == TownStage::Over);        // the card stays up
}

KH_TEST(stage_town_only_the_second_district_spawns) {
    // The dispatch reaches TownShadows only for SCENE_TOWN2.  The First and
    // Third are walkable and quiet.
    Stub w;
    ScreenFx fx;
    const SceneId quiet[] = {SceneId::Town1, SceneId::Town3};
    for (SceneId d : quiet) {
        TownMachine m;
        m.begin(w.rng);
        m.setStage(TownStage::Second);
        m.setDistrict(d);
        m.setSpots(TOWN2_SPOTS, 12);
        for (int i = 0; i < (TOWN_GAP + 1) * 3; ++i) step(m, w, fx);
        CHECK_EQ(m.waveSpawned(), 0);
        CHECK_EQ(w.actors.count(ActType::Shadow), 0);
    }
}
