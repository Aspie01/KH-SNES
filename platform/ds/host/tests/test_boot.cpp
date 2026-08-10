// The frame loop and the second performer, driven against the real .bin files.
//
// WHY THIS FILE MATTERS MORE THAN ITS SIZE SUGGESTS.  tools/trace_check.py
// proves the SIMULATION against the frozen ROM, and it does so through
// trace_main.cpp -- a scenario, which is "enough of a game loop to make a
// trace, and no more".  device/boot.cpp is the other thing: the whole game,
// every scene, the interaction layer, and the performer that turns a machine's
// action into a scene load.  None of that is on the oracle's route.
//
// So these cases are the only check that exists on the wiring, and they are
// written accordingly: against the real asset files rather than fixtures,
// because the failure being guarded against is a scene whose bytes do not
// arrive, and a fixture would supply bytes that always do.

#include <initializer_list>

#include "boot.h"
#include "check.h"
#include "constants.h"
#include "gen/assets.h"
#include "gen/doors.h"
#include "hostblob.h"

using namespace kh;
using namespace kh::device;

namespace {

// The host's answer to "where do the bytes come from": read the file.  The
// device's answer is a linked symbol, and the two meet at SceneSource -- which
// is §M2's whole seam and the reason boot.cpp can be tested at all.
//
// One buffer set per scene, all static, because a SceneBlobs holds POINTERS and
// a source that filled a scratch buffer would hand out blobs that stopped being
// valid the moment the next scene loaded.  That is the bug this shape prevents,
// and it is the same one that would bite a device provider handing out a
// decompression buffer.
struct Slot {
    unsigned char coll[8192];
    unsigned char height[8192];
    unsigned char chars[16384];
    unsigned char cast[2048];
    unsigned char day1[512];
    unsigned char day2[512];
    unsigned char boss[64];
    unsigned char pair[64];
    unsigned char spots[256];
    unsigned char doors[64];
    SceneBlobs blobs;
    bool loaded = false;
    bool ok = false;
};

Slot g_slots[int(SceneId::Count)];

const SceneAsset* assetFor(SceneId s) {
    // gen/assets.h's table is in SceneId order by construction -- station1..3,
    // island, night, fragment, town1..3 -- and constants.h's enum is the same
    // nine in the same order.  Asserted rather than assumed, because an
    // off-by-one here would load the island's ground for the night, which is a
    // legal thing to do (they share it) and would look almost right.
    const int i = int(s);
    if (i < 0 || i >= int(SceneId::Count)) return nullptr;
    return &SCENE_ASSETS[i];
}

bool sameName(const char* a, const char* b) {
    while (*a && *a == *b) { ++a; ++b; }
    return *a == *b;
}

kh::Blob loadInto(const char* stem, const char* suffix, unsigned char* buf,
                  size_t cap) {
    char name[64];
    int n = 0;
    for (const char* p = stem; *p && n < 40; ++p) name[n++] = *p;
    for (const char* p = suffix; *p && n < 60; ++p) name[n++] = *p;
    name[n] = '\0';
    return khhost::load(name, buf, cap);
}

const SceneBlobs* source(SceneId s) {
    const int i = int(s);
    if (i < 0 || i >= int(SceneId::Count)) return nullptr;
    Slot& sl = g_slots[i];
    if (sl.loaded) return sl.ok ? &sl.blobs : nullptr;
    sl.loaded = true;

    const SceneAsset* a = assetFor(s);
    if (!a) return nullptr;
    // A scene that borrows another's ground -- the night borrows the island's,
    // and gen/assets.h's `groundFrom` is the only place that says so.
    const char* ground = a->groundFrom ? a->groundFrom : a->name;

    sl.blobs.collision = loadInto(ground, "coll.bin", sl.coll, sizeof sl.coll);
    sl.blobs.height = loadInto(ground, "height.bin", sl.height, sizeof sl.height);
    sl.blobs.chars = loadInto(ground, "map.bin", sl.chars, sizeof sl.chars);
    sl.blobs.cast = loadInto(a->name, "cast.bin", sl.cast, sizeof sl.cast);
    if (has(a->tables, SceneTable::Day1))
        sl.blobs.day1 = loadInto(a->name, "day1.bin", sl.day1, sizeof sl.day1);
    if (has(a->tables, SceneTable::Day2))
        sl.blobs.day2 = loadInto(a->name, "day2.bin", sl.day2, sizeof sl.day2);
    if (has(a->tables, SceneTable::Boss))
        sl.blobs.boss = loadInto(a->name, "boss.bin", sl.boss, sizeof sl.boss);
    if (has(a->tables, SceneTable::Pair))
        sl.blobs.pair = loadInto(a->name, "pair.bin", sl.pair, sizeof sl.pair);
    if (has(a->tables, SceneTable::Spots))
        sl.blobs.spots = loadInto(a->name, "spots.bin", sl.spots, sizeof sl.spots);
    if (has(a->tables, SceneTable::Doors))
        sl.blobs.doors = loadInto(a->name, "doors.bin", sl.doors, sizeof sl.doors);
    sl.blobs.tilesW = a->tilesW;
    sl.blobs.tilesH = a->tilesH;

    sl.ok = !sl.blobs.collision.empty() && !sl.blobs.chars.empty();
    return sl.ok ? &sl.blobs : nullptr;
}

// A pad with nothing held, which is what most of these frames want: the point
// is that the world runs, not that it is driven.
const Pad IDLE{};

}  // namespace

KH_TEST(boot_the_scene_table_and_the_scene_enum_are_the_same_nine) {
    CHECK_EQ(int(sizeof SCENE_ASSETS / sizeof *SCENE_ASSETS), int(SceneId::Count));
    CHECK(sameName(assetFor(SceneId::Dive)->name, "station1"));
    CHECK(sameName(assetFor(SceneId::Dive3)->name, "station3"));
    CHECK(sameName(assetFor(SceneId::Island)->name, "island"));
    CHECK(sameName(assetFor(SceneId::Night)->name, "night"));
    CHECK(sameName(assetFor(SceneId::Fragment)->name, "fragment"));
    CHECK(sameName(assetFor(SceneId::Town3)->name, "town3"));
    // The night borrows the island's ground, which is the one row where the
    // scene's name and its ground's name differ.
    CHECK(assetFor(SceneId::Night)->groundFrom != nullptr);
    CHECK(sameName(assetFor(SceneId::Night)->groundFrom, "island"));
}

KH_TEST(boot_every_shipped_scene_loads) {
    // THE PIPELINE'S OUTPUT, NOT A FIXTURE.  A scene whose .bin is missing, or
    // whose collision table disagrees with the extents the generator recorded,
    // fails here rather than on hardware -- and on hardware the symptom is a
    // door that does nothing.
    for (int i = 0; i < int(SceneId::Count); ++i) {
        Game g(source);
        const SceneId s = SceneId(i);
        const bool ok = g.enter(s);
        if (!ok) ktest::fail(__FILE__, __LINE__, g.error());
        ++ktest::checks;
        CHECK_EQ(int(g.scene()), i);
        CHECK(g.ground().valid());
        CHECK(g.charMap().valid());
        // Every scene has a Sora in its [base] table.  A scene without one is
        // not a scene the player can be in, and the symptom would be a camera
        // pinned at the origin rather than a crash.
        CHECK(g.player() >= 0);
        // ...and the collision map and the character map describe one map.
        CHECK_EQ(g.charMap().wChars, g.ground().width() * 2);
        CHECK_EQ(g.charMap().hChars, g.ground().height() * 2);
    }
}

KH_TEST(boot_a_missing_scene_is_a_named_refusal_and_not_an_empty_world) {
    Game g(nullptr);
    CHECK(!g.begin(SceneId::Dive));
    CHECK(g.error() != nullptr);
    // The previous scene stands.  A renderer handed a half-loaded world would
    // draw a map with no collision under it, which is Sora walking on air.
    CHECK(!g.ground().valid());
}

KH_TEST(boot_only_the_stations_pin_the_camera) {
    // The oracle settled this for the three stations -- every dive scenario in
    // trace_check.py runs against pinnedBounds and agrees frame for frame.
    for (SceneId s : {SceneId::Dive, SceneId::Dive2, SceneId::Dive3}) {
        const CameraBounds b = boundsFor(s, 32, 16);
        CHECK_EQ(b.loX, b.hiX);
        CHECK_EQ(b.loY, b.hiY);
        CHECK_EQ(b.loX, DIVE_CAM_X);
        CHECK_EQ(b.loY, DIVE_CAM_Y);
    }
    // ...and everything else scrolls to its own edges, including the fragment,
    // which is the one judgement call in boundsFor().
    const CameraBounds isle = boundsFor(SceneId::Island, 64, 32);
    CHECK_EQ(isle.hiX, 64 * TILE_PX - SCREEN_W);
    CHECK_EQ(isle.hiY, 32 * TILE_PX - SCREEN_H);
    CHECK(boundsFor(SceneId::Fragment, 32, 16).hiX > 0);
}

KH_TEST(boot_the_camera_is_on_the_player_before_the_first_frame) {
    // main.s:120 runs UpdateCamera once on the reset path.  Leaving it out puts
    // frame 0 at the origin, which §M6 caught the moment the trace grew camera
    // columns -- (0,0) against the oracle's (128,16).
    Game g(source);
    CHECK(g.begin(SceneId::Island));
    CHECK(g.camera().x != 0 || g.camera().y != 0);
    const Camera before = g.camera();
    g.frame(IDLE);
    CHECK_EQ(g.camera().x, before.x);       // nobody moved
    CHECK_EQ(g.camera().y, before.y);
}

KH_TEST(boot_the_dive_gates_every_scene_and_not_just_its_own) {
    // main.s:622-628 dispatches on diveStage BEFORE sceneId: until the Dive says
    // DIVE_ARRIVED, DiveUpdate runs even standing on the island.  §M6 caught a
    // port that dispatched on the scene alone as 670 frames of a single
    // differing column.
    Game g(source);
    CHECK(g.begin(SceneId::Island));
    CHECK_EQ(int(g.dive().stage()), int(DiveStage::Arrived));
    // A build that starts on a station leaves it at Intro, and the island's
    // machine must not be the one running.
    Game d(source);
    CHECK(d.begin(SceneId::Dive));
    CHECK_EQ(int(d.dive().stage()), int(DiveStage::Intro));
    CHECK_EQ(int(d.island().state()), int(QuestState::Idle));
    for (int i = 0; i < 60; ++i) d.frame(IDLE);
    CHECK_EQ(int(d.island().state()), int(QuestState::Idle));
}

KH_TEST(boot_the_town_reseeds_and_the_world_does_not) {
    // Two seeds, and only TownBegin writes the second.  A build that reached the
    // Second District without it would run every Shadow off the wrong sequence,
    // which is §M6's named determinism hazard.
    Game island(source);
    CHECK(island.begin(SceneId::Island));
    Game town(source);
    CHECK(town.begin(SceneId::Town2));
    CHECK_EQ(int(town.town().stage()), int(TownStage::Arrive));
    // The two started from different seeds, so a scene that consumed the same
    // number of draws would still be at different states.  Proving the seeds
    // differ is what this is for; the sequences themselves are the oracle's.
    CHECK(Rng::WORLD_SEED != Rng::TOWN_SEED);
}

KH_TEST(boot_the_night_takes_its_density_from_the_map) {
    // divergence 003: the DS island is four times the SNES's ground and wants
    // twenty Shadows at a 42-frame gap; the fragment is untouched and still
    // wants the SNES's six at seventy.  The MACHINE has one default and the
    // SCENE is what chooses -- so a build that never chose would run the
    // fragment at island density and swamp a 32x16 platform.
    Game n(source);
    CHECK(n.begin(SceneId::Night));
    CHECK_EQ(n.night().shadowMax(), SHADOW_MAX_NIGHT);
    CHECK_EQ(n.night().shadowGap(), SHADOW_GAP);

    Game f(source);
    CHECK(f.begin(SceneId::Fragment));
    CHECK_EQ(f.night().shadowMax(), SNES_SHADOW_MAX);
    CHECK_EQ(f.night().shadowGap(), SNES_SHADOW_GAP);
}

KH_TEST(boot_a_scene_change_is_announced_exactly_once) {
    // The caller owes a scene change three things -- re-upload the characters
    // and palettes, re-hand the CharMap, re-load the ground -- and a flag that
    // stayed set would do all three every frame, which at 8 KiB of map is most
    // of a frame's budget spent on nothing.
    Game g(source);
    CHECK(g.begin(SceneId::Dive));
    CHECK(g.takeSceneChanged());
    CHECK(!g.takeSceneChanged());
    g.frame(IDLE);
    CHECK(!g.takeSceneChanged());
    CHECK(g.enter(SceneId::Dive2));
    CHECK(g.takeSceneChanged());
}

KH_TEST(boot_the_hud_finds_the_boss_by_its_flag_and_not_by_its_type) {
    // device/hud.h refuses to answer "which actor is the boss" twice, so this is
    // the one place that answers it -- the AF_HUGE actor, of which there is
    // never more than one.  Both bosses carry the flag and nothing else does.
    Game g(source);
    CHECK(g.begin(SceneId::Dive3));
    CHECK_EQ(g.hudState().boss, -1);            // it has not risen yet
    CHECK_EQ(g.hudState().player, g.player());
}

KH_TEST(boot_spawning_the_boss_arms_it_resting_and_fills_the_gauge) {
    Game g(source);
    CHECK(g.begin(SceneId::Dive3));
    // The third station's [boss] table exists precisely so that Darkside is NOT
    // placed on entry: "placing Darkside on entry would have it standing there
    // waiting instead of rising" (station3_cast.txt).
    CHECK(source(SceneId::Dive3) != nullptr);
    CHECK(!source(SceneId::Dive3)->boss.empty());

    // Reach it the way the machine does rather than by poking the actor pool.
    // A test that spawned the boss itself would be testing its own arm.
    bool raised = false;
    for (int i = 0; i < 4000 && !raised; ++i) {
        Pad p;
        p.held = p.pressed = raw(Button::A);    // walk the intro along
        g.frame(i % 8 == 0 ? p : IDLE);
        if (g.lastAction() == SceneAction::SpawnBoss) {
            raised = true;
            if (!g.lastActionPerformed())
                ktest::fail(__FILE__, __LINE__, g.error());
            ++ktest::checks;
        }
    }
    if (!raised) {
        // Not a silent skip: the third station's whole content is the boss, and
        // a run that never reached it has learned nothing.
        ktest::fail(__FILE__, __LINE__,
                    "the third station never asked for its boss in 4000 frames");
    }
    ++ktest::checks;
    if (raised) {
        CHECK_EQ(g.world().bossHP, DS_MAX_HP);
        CHECK(g.hudState().boss >= 0);
        CHECK(!g.hudState().armor);             // Darkside, not the Guard Armor
    }
}

KH_TEST(boot_an_unported_beat_is_refused_by_name_rather_than_ignored) {
    // The arm this is about is the block of actions boot.cpp cannot perform yet.
    // What matters is not that they are missing -- that is honest -- but that a
    // caller can TELL, because a machine whose beat silently did nothing runs its
    // timer out over a scene that never changed.
    //
    // THE LIST IS SIX NOW, NOT TEN.  The night's three column beats and the door
    // were on it and are ported: perform.h's standColumn(), clearColumns() and
    // openTheDoor(), tested in test_stage2.cpp and called by BOTH performers.
    // ColumnForRiku used to be the example on this line, and naming a ported
    // action as the specimen of an unported one is how a list like this rots --
    // so the examples are two of the six that are still genuinely missing.
    for (SceneAction a : {SceneAction::DropPair, SceneAction::LowerPair,
                          SceneAction::RestartScene}) {
        const char* n = actionName(a);
        CHECK(n != nullptr);
        CHECK(!sameName(n, "?"));
    }
    // ...and every action in the enum has a name, so a refusal can always say
    // which one.  The count is the enum's own last member plus one.
    for (int i = 0; i <= int(SceneAction::RestartScene); ++i)
        CHECK(!sameName(actionName(SceneAction(i)), "?"));
}

KH_TEST(boot_runs_the_island_for_a_thousand_frames_without_faulting) {
    // A soak, and the only case here that is about nothing in particular.  The
    // island is the biggest map, the fullest cast and the only scene with real
    // heights, and a thousand frames of it exercises the actor loop, the camera
    // clamp at both edges and the ground streamer's slack.
    Game g(source);
    CHECK(g.begin(SceneId::Island));
    Pad walk;
    walk.held = raw(Button::Right);
    for (int i = 0; i < 1000; ++i) {
        walk.pressed = i == 0 ? walk.held : uint16_t(0);
        g.frame(walk);
        // The camera never leaves the map, at either edge, on any frame.
        CHECK(g.camera().x >= 0);
        CHECK(g.camera().x <= g.ground().width() * TILE_PX - SCREEN_W);
        CHECK(g.camera().y >= 0);
        CHECK(g.camera().y <= g.ground().height() * TILE_PX - SCREEN_H);
    }
    // He got somewhere.  Without this the loop above passes on a build where
    // the pad is never read.
    CHECK(g.actors().x[g.player()].raw() > tileCentre(24).raw());
    // ...and the cel the sprite packer needs came out.
    CHECK(g.soraCel() >= 0);
    CHECK(g.soraCel() < SORA_CELS);
}

// ===========================================================================
// The doorway between the island and the Secret Place
// ===========================================================================

namespace {

// Walk him until he is standing on (i,j), or give up.  Returns the frame count,
// or -1.  Deliberately drives Game::frame() rather than moving the actor: the
// door fires out of interactStep, which only runs inside a frame, and a test that
// teleported him would prove the performer works and the trigger does not.
int walkTo(Game& g, int i, int j, Button dir, int cap = 400) {
    Pad p;
    p.held = raw(dir);
    for (int n = 1; n <= cap; ++n) {
        p.pressed = n == 1 ? p.held : uint16_t(0);
        g.frame(p);
        const int who = g.player();
        if (who < 0) return -1;
        if (tileOf(g.actors().x[who]) == i && tileOf(g.actors().y[who]) == j)
            return n;
    }
    return -1;
}

// Hold `dir` until the scene becomes `want`.
//
// THE TRANSITION LANDS ONE FRAME AFTER HE STEPS ON THE DOORWAY, and that is the
// SNES's frame order rather than a lag worth fixing: SceneUpdate runs before
// UpdateWorld (main.s), so interactStep sees where he was at the START of the
// frame and updateWorld moves him afterwards.  He arrives on the door during
// frame N and the door is noticed on frame N+1.  A helper that stopped the moment
// tileOf matched would therefore always read the scene one frame too early --
// which is exactly how the first draft of these cases failed, with every
// assertion about the destination wrong and the mechanism perfectly fine.
int walkUntilScene(Game& g, SceneId want, Button dir, int cap = 400) {
    Pad p;
    p.held = raw(dir);
    for (int n = 1; n <= cap; ++n) {
        p.pressed = n == 1 ? p.held : uint16_t(0);
        g.frame(p);
        if (g.scene() == want) return n;
    }
    return -1;
}

}  // namespace

KH_TEST(boot_the_doorway_carries_him_into_the_secret_place_and_back) {
    // THE ROUND TRIP, driven with the pad, because every half of it can be wrong
    // on its own: the trigger (interactStep sees the tile), the action (EnterRoom
    // reaches perform), the load (enter() finds the room's bytes) and the
    // placement (he arrives on the reciprocal's landing and not wherever the
    // cast file happens to put him).
    //
    // STARTED IN THE CAVE rather than on the island, and that is the only reason
    // this is a short test: the chamber puts Sora down at (21,12) and its doorway
    // is at (23,11), so the walk is two tiles east and one north.  The island's
    // doorway is at the far end of a cliff pocket reached by wading round a
    // waterfall, which is a pathfinding exercise and not a door test.
    Game g(source);
    CHECK(g.begin(SceneId::Cave));
    CHECK(g.scene() == SceneId::Cave);
    CHECK(g.player() >= 0);
    CHECK_EQ(tileOf(g.actors().x[g.player()]), 21);
    CHECK_EQ(tileOf(g.actors().y[g.player()]), 12);

    // East along the passage floor to the tile below the door.  Standing there
    // must NOT fire anything -- the landing is beside its door and never on it,
    // which is what stops an arrival from immediately taking the door back.
    CHECK(walkTo(g, 23, 12, Button::Right) > 0);
    CHECK(g.scene() == SceneId::Cave);
    CHECK(g.lastAction() != SceneAction::EnterRoom);

    // ...and north onto the doorway itself.
    CHECK(walkUntilScene(g, SceneId::Island, Button::Up, 120) > 0);
    // He is on the island now, on the reciprocal door's own landing: the island's
    // door is (6,5) and its near landing is (6,6).  gen/doors.h derives that from
    // the reciprocal row and never authors it.
    CHECK(g.scene() == SceneId::Island);
    CHECK(g.lastActionPerformed());
    CHECK(g.error() == nullptr);
    CHECK(g.player() >= 0);
    CHECK_EQ(tileOf(g.actors().x[g.player()]), 6);
    CHECK_EQ(tileOf(g.actors().y[g.player()]), 6);
    // HIS FACING AND VELOCITY ARE NOT ASSERTED HERE, and the reason is the frame
    // order rather than an oversight.  The performer does clear both and face him
    // south -- the same five statements EnterDistrict's arm runs -- but
    // updateWorld() runs LATER IN THE SAME FRAME with the direction still held,
    // so by the time a test can look he has correctly turned north and started
    // walking again.  Asserting Dir::S here fails against working code, which is
    // how the first draft of this case read.  What survives the frame, and is
    // therefore what is worth checking, is where he was put down.
    // The island's cast is up, which is what proves the scene really loaded
    // rather than the placement having been applied to the cave's table.
    CHECK_EQ(g.actors().count(ActType::Kairi), 1);
    CHECK_EQ(g.actors().count(ActType::Faces), 0);      // those are in the room

    // AND BACK.  He is one tile south of the island's doorway, so north again.
    CHECK(walkUntilScene(g, SceneId::Cave, Button::Up, 120) > 0);
    CHECK(g.lastActionPerformed());
    CHECK_EQ(tileOf(g.actors().x[g.player()]), 23);
    CHECK_EQ(tileOf(g.actors().y[g.player()]), 12);
    CHECK_EQ(g.actors().count(ActType::Faces), 1);      // the chamber, again
    CHECK_EQ(g.actors().count(ActType::Kairi), 0);
}

KH_TEST(boot_standing_on_a_doorway_takes_it_once_and_not_every_frame) {
    // THE EDGE IS NOT AN OPTIMISATION HERE.  A town door that fired every frame
    // would repeat a bolted-door line; a room door that fired every frame would
    // re-enter the scene it just loaded, for ever.  So this drives him onto the
    // doorway and then holds still for a hundred frames.
    Game g(source);
    CHECK(g.begin(SceneId::Cave));
    CHECK(walkTo(g, 23, 12, Button::Right) > 0);
    CHECK(walkUntilScene(g, SceneId::Island, Button::Up, 120) > 0);

    Pad still;
    for (int i = 0; i < 100; ++i) g.frame(still);
    // One transition, not a hundred: he is still on the island and still on its
    // landing, which he could not be if the door had taken him back and forth.
    CHECK(g.scene() == SceneId::Island);
    CHECK_EQ(tileOf(g.actors().x[g.player()]), 6);
    CHECK_EQ(tileOf(g.actors().y[g.player()]), 6);
    CHECK(g.error() == nullptr);
}

KH_TEST(boot_the_room_doors_are_reciprocal_and_land_beside_each_other) {
    // The table itself, read straight out of gen/doors.h.  Every number here is
    // DERIVED by tools/build_doors.py from the reciprocal row -- the far landing
    // is the other door's own near landing -- so this is the assertion that the
    // derivation produced the pair somebody authored and not two halves of two
    // different pairs.
    const RoomDoors isl = roomDoorsFor(SceneId::Island);
    const RoomDoors cav = roomDoorsFor(SceneId::Cave);
    CHECK_EQ(isl.count, 1);
    CHECK_EQ(cav.count, 1);
    CHECK(isl.rows != nullptr && cav.rows != nullptr);

    CHECK(isl.rows[0].to == SceneId::Cave);
    CHECK(cav.rows[0].to == SceneId::Island);
    // Each one's landing is the OTHER one's tile, one row south of it.
    CHECK_EQ(int(isl.rows[0].landing.i), int(cav.rows[0].at.i));
    CHECK_EQ(int(isl.rows[0].landing.j), int(cav.rows[0].at.j) + 1);
    CHECK_EQ(int(cav.rows[0].landing.i), int(isl.rows[0].at.i));
    CHECK_EQ(int(cav.rows[0].landing.j), int(isl.rows[0].at.j) + 1);

    // A district has no room doors and a room has no district doors.  Both
    // accessors return an empty span rather than a default, so the interaction
    // layer walks zero rows -- which is the correct number.
    CHECK_EQ(roomDoorsFor(SceneId::Town1).count, 0);
    CHECK(roomDoorsFor(SceneId::Town1).rows == nullptr);
    CHECK_EQ(townDoorsFor(SceneId::Island).count, 0);
    CHECK_EQ(townDoorsFor(SceneId::Cave).count, 0);
}
