// The DS half of the oracle diff: run the Tier-1 simulation and write a trace.
//
//     platform/ds/host/build/dstrace --scenario station --frames 130
//         --input traces/station.txt -o out/ds.trace
//
// ...or, and this is the way to run it, through tools/ds_trace.py, which stamps
// the git revision and checks the header against tools/snes_trace.py's byte for
// byte before it hands the file over.
//
// WHY A SCENARIO AND NOT A GAME.  The oracle boots the ROM and the ROM does the
// rest.  There is no equivalent here, because the thing that would tie the four
// stage machines together -- load a scene, spawn its cast, perform the action a
// machine returned -- is the device tier, and the device tier is §M7 and blocked
// on a toolchain that is not in this container.  A scenario is that tying-together
// for exactly one situation: enough of a game loop to make a trace, and no more.
//
// The frame loop is the SNES's, in the SNES's order:
//
//     ReadPad -> TextUpdate -> SceneUpdate -> UpdateWorld
//
// UpdateCamera and BuildOam follow on the real machine and write nothing the
// trace reads, so they are not here.  Where the sample is taken is not a free
// choice either: the oracle samples immediately BEFORE the NMI fires, which is
// the end of that frame's work, so this samples after UpdateWorld returns.
//
// TWO FAMILIES OF SCENARIO, and the difference between them is the whole reason
// there is more than one:
//
//   * "the oracle's geometry" -- the SNES's own collision map and the SNES's own
//     cast positions.  Content is held equal so that ANY difference is a
//     difference in the CODE, and the diff should be empty under --strict.
//   * "as the DS ships it" -- the DS's map and the DS's cast.  Differences are
//     expected and the point is that every one of them is a divergence somebody
//     wrote down.
//
// A scenario that cannot perform a SceneAction stops the run and names it.  A
// trace that quietly skipped a scene transition would diff against the oracle
// somewhere far downstream, and the frame it named would be the wrong one.

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "hostblob.h"
#include "stage.h"
#include "trace.h"
#include "world.h"

using namespace kh;

namespace {

// ---------------------------------------------------------------------------
// The input script
//
// `keys:frames` a line, `none` for nothing, `+` between simultaneous buttons --
// the vocabulary tools/playtest.sh and tools/snes_trace.py already use.  ONE
// script file drives both emitters, and it does so with no translation table
// because the DS Pad's bits are the SNES's own; the static_assert below is what
// stops that quietly ceasing to be true.
// ---------------------------------------------------------------------------
struct ButtonName {
    const char* name;
    uint16_t mask;
};

constexpr ButtonName BUTTON_NAMES[] = {
    {"b", raw(Button::B)},         {"y", raw(Button::Y)},
    {"select", raw(Button::Select)}, {"start", raw(Button::Start)},
    {"up", raw(Button::Up)},       {"down", raw(Button::Down)},
    {"left", raw(Button::Left)},   {"right", raw(Button::Right)},
    {"a", raw(Button::A)},         {"x", raw(Button::X)},
    {"l", raw(Button::L)},         {"r", raw(Button::R)},
};

static_assert(raw(Button::B) == 0x8000 && raw(Button::Y) == 0x4000
                  && raw(Button::Select) == 0x2000 && raw(Button::Start) == 0x1000
                  && raw(Button::Up) == 0x0800 && raw(Button::Down) == 0x0400
                  && raw(Button::Left) == 0x0200 && raw(Button::Right) == 0x0100
                  && raw(Button::A) == 0x0080 && raw(Button::X) == 0x0040
                  && raw(Button::L) == 0x0020 && raw(Button::R) == 0x0010,
              "the pad bits are the SNES's own -- see pad.h.  That is what lets "
              "one input script drive both emitters, and tools/snes_trace.py's "
              "BUTTONS table is these same twelve numbers");

constexpr int MAX_FRAMES = 20000;
uint16_t g_pads[MAX_FRAMES];

// Case-insensitive compare of a word against a name, both NUL-terminated.
bool sameWord(const char* a, const char* b) {
    for (;; ++a, ++b) {
        const char x = (*a >= 'A' && *a <= 'Z') ? char(*a + 32) : *a;
        if (x != *b) return false;
        if (!x) return true;
    }
}

// Parse one `a+b` key list into a mask.  Returns false and names the offender
// on an unknown button, because a typo that silently pressed nothing would make
// a trace that diverges for a reason nothing in it records.
bool parseKeys(char* keys, uint16_t& out) {
    out = 0;
    char* p = keys;
    while (*p) {
        char* end = p;
        while (*end && *end != '+') ++end;
        const char saved = *end;
        *end = '\0';
        if (*p && !sameWord(p, "none")) {
            bool found = false;
            for (const ButtonName& b : BUTTON_NAMES) {
                if (sameWord(p, b.name)) {
                    out = uint16_t(out | b.mask);
                    found = true;
                    break;
                }
            }
            if (!found) {
                std::fprintf(stderr, "unknown button '%s'\n", p);
                return false;
            }
        }
        *end = saved;
        p = *end ? end + 1 : end;
    }
    return true;
}

bool readScript(const char* path, int frames) {
    std::memset(g_pads, 0, sizeof g_pads);
    if (!path) return true;
    std::FILE* f = std::fopen(path, "r");
    if (!f) {
        std::fprintf(stderr, "cannot open input script %s\n", path);
        return false;
    }
    char line[512];
    int i = 0;
    bool ok = true;
    while (std::fgets(line, sizeof line, f)) {
        char* hash = std::strchr(line, '#');
        if (hash) *hash = '\0';
        char* s = line;
        while (*s == ' ' || *s == '\t') ++s;
        char* e = s + std::strlen(s);
        while (e > s && (e[-1] == '\n' || e[-1] == '\r' || e[-1] == ' '
                         || e[-1] == '\t'))
            *--e = '\0';
        if (!*s) continue;

        char* colon = std::strchr(s, ':');
        int n = 1;
        if (colon) {
            *colon = '\0';
            n = std::atoi(colon + 1);
            if (n < 1) n = 1;
        }
        uint16_t mask = 0;
        if (!parseKeys(s, mask)) { ok = false; break; }
        for (int k = 0; k < n; ++k, ++i)
            if (i < frames) g_pads[i] = mask;
    }
    std::fclose(f);
    if (ok && i > frames)
        std::fprintf(stderr, "note: the script is %d frames and only %d were "
                             "asked for\n", i, frames);
    return ok;
}

// ---------------------------------------------------------------------------
// The simulation a scenario configures
// ---------------------------------------------------------------------------
enum class Machine : uint8_t { None, Dive, Island, Night, Town };

unsigned char g_coll[8192];
unsigned char g_height[8192];
unsigned char g_cast[2048];

struct Sim {
    Actors actors;
    Dialogue dialogue;
    Pad pad;
    SceneGround ground;
    Rng rng;
    WorldState world;
    ScreenFx fx;

    DiveMachine dive;
    IslandMachine island;
    NightMachine night;
    TownMachine town;

    Machine machine = Machine::None;
    SceneId scene = SceneId::Dive;
    int player = 0;
    uint32_t frame = 0;

    SceneView view() {
        return SceneView{actors, dialogue, pad, ground, rng, player, frame};
    }

    TraceStage stage() const {
        TraceStage s{};
        s.diveStage = uint8_t(dive.stage());
        s.questState = uint8_t(island.state());
        s.nightStage = uint8_t(night.stage());
        s.townStage = uint8_t(town.stage());
        s.sceneId = uint8_t(scene);
        s.bossHP = world.bossHP;
        return s;
    }
};

Sim g_sim;

// The ground.  `heightName` is null for the flat plane LoadScene pointed
// heightPtr at for every scene but the island, and named for the one that
// wasn't -- getting that wrong would put Sora at the wrong height on a beach
// and nothing in the trace would say why.
bool loadGround(bool snesSide, const char* collName, const char* heightName,
                int w, int h) {
    Blob coll = snesSide ? khhost::loadSnes(collName, g_coll, sizeof g_coll)
                         : khhost::load(collName, g_coll, sizeof g_coll);
    if (coll.empty()) {
        std::fprintf(stderr, "%s is missing; run tools/build_assets.py\n",
                     collName);
        return false;
    }
    if (coll.size != size_t(w) * size_t(h)) {
        std::fprintf(stderr, "%s is %zu bytes, expected %d\n", collName,
                     coll.size, w * h);
        return false;
    }
    if (heightName) {
        Blob hgt = snesSide
                       ? khhost::loadSnes(heightName, g_height, sizeof g_height)
                       : khhost::load(heightName, g_height, sizeof g_height);
        if (hgt.size != coll.size) {
            std::fprintf(stderr, "%s is %zu bytes and %s is %zu\n", heightName,
                         hgt.size, collName, coll.size);
            return false;
        }
    } else {
        std::memset(g_height, 0, coll.size);
    }
    g_sim.ground.set(coll, Blob{g_height, coll.size}, w, h);
    return true;
}

// ---------------------------------------------------------------------------
// Performing what a machine asked for
//
// Only the actions a shipped scenario can actually reach are implemented.  The
// rest stop the run BY NAME rather than being ignored: an unperformed scene
// transition would put the trace on a different timeline from the oracle's and
// the diff would name a frame hundreds later with no hint of why.
// ---------------------------------------------------------------------------
// RaiseArmor, town.s:866.  It comes down in the middle of the square, with its
// hands -- and it MOVES SORA FIRST, to tile (16,10), which is the staging: the
// armour lands two tiles above him, between him and the door he came in by.
// PlaceSora also clears his velocity, faces him south and resets his state and
// timer, so a player caught mid-swing by the retry is put down standing.
bool raiseArmor() {
    const int p = g_sim.player;
    if (p < 0 || p >= MAX_ACTORS) return false;
    g_sim.actors.x[p] = tileCentre(16);
    g_sim.actors.y[p] = tileCentre(10);
    g_sim.actors.vx[p] = World::fromRaw(0);
    g_sim.actors.vy[p] = World::fromRaw(0);
    g_sim.actors.dir[p] = Dir::S;
    g_sim.actors.state[p] = ActState::Idle;
    g_sim.actors.timer[p] = 0;
    g_sim.actors.z[p] = g_sim.ground.heightAt(16, 10);

    const int armor = g_sim.actors.spawn(ActType::Armor, tileCentre(16),
                                         tileCentre(7));
    if (armor < 0) return false;
    g_sim.actors.z[armor] = uint8_t(GA_DROP_Z);
    g_sim.actors.timer[armor] = uint8_t(GA_DROP);   // Drop, by the state being 0
    g_sim.world.bossHP = uint8_t(GA_MAX_HP);
    // SpawnHands: both start ON the torso, numbered 0 then 1.  The Drop branch
    // is the one branch that does not call PlaceHands, so they arrive with the
    // body rather than reaching out ahead of it.
    for (int hand = 0; hand < 2; ++hand) {
        const int h = g_sim.actors.spawn(ActType::Gauntlet, tileCentre(16),
                                         tileCentre(7));
        if (h < 0) return false;
        g_sim.actors.anim[h] = uint8_t(hand);
    }
    return true;
}

const char* actionName(SceneAction a) {
    switch (a) {
        case SceneAction::None: return "None";
        case SceneAction::Say: return "Say";
        case SceneAction::Ask: return "Ask";
        case SceneAction::EnterStation2: return "EnterStation2";
        case SceneAction::EnterStation3: return "EnterStation3";
        case SceneAction::SpawnBoss: return "SpawnBoss";
        case SceneAction::BeginFall: return "BeginFall";
        case SceneAction::SpawnMote: return "SpawnMote";
        case SceneAction::EnterIsland: return "EnterIsland";
        case SceneAction::EnterDistrict: return "EnterDistrict";
        case SceneAction::DropPair: return "DropPair";
        case SceneAction::LowerPair: return "LowerPair";
        case SceneAction::RaiseArmor: return "RaiseArmor";
        case SceneAction::SweepGauntlets: return "SweepGauntlets";
        case SceneAction::HudChanged: return "HudChanged";
        case SceneAction::RebuildForDayTwo: return "RebuildForDayTwo";
        case SceneAction::BeginNight: return "BeginNight";
        case SceneAction::ColumnForRiku: return "ColumnForRiku";
        case SceneAction::ColumnForKairi: return "ColumnForKairi";
        case SceneAction::ClearColumns: return "ClearColumns";
        case SceneAction::OpenTheDoor: return "OpenTheDoor";
        case SceneAction::EnterFragment: return "EnterFragment";
        case SceneAction::SweepCraterShadows: return "SweepCraterShadows";
        case SceneAction::EnterTown: return "EnterTown";
        case SceneAction::RespawnNightCast: return "RespawnNightCast";
        case SceneAction::RespawnFragment: return "RespawnFragment";
        case SceneAction::RespawnDistrict: return "RespawnDistrict";
        case SceneAction::RestartScene: return "RestartScene";
    }
    return "?";
}

bool perform(const StageStep& step) {
    switch (step.action) {
        case SceneAction::None:
            return true;
        case SceneAction::Say:
            g_sim.dialogue.open(scriptFor(step.script), TextMode::Message);
            return true;
        case SceneAction::HudChanged:
            // The HUD is not in the trace and redrawing it changes no state the
            // simulation can see.  This is the one action it is correct to drop.
            return true;
        case SceneAction::RespawnDistrict:
            // SpawnDistrict, and the scenario's own setup IS it: the rows it
            // spawned are the district's table.  Saying so here rather than
            // silently ignoring the action is the difference between a
            // scenario that has performed it and one that has forgotten to.
            return true;
        case SceneAction::RaiseArmor:
            return raiseArmor();
        case SceneAction::SweepGauntlets:
            // Its hands go with it, and ONLY its hands.  town.s:826-834 scans
            // for ACT_GAUNTLET alone.
            for (int i = 0; i < MAX_ACTORS; ++i)
                if (g_sim.actors.type[i] == ActType::Gauntlet)
                    g_sim.actors.type[i] = ActType::None;
            g_sim.world.bossHP = 0;
            g_sim.fx.shakeX = 0;
            if (step.script != ScriptId::None)
                g_sim.dialogue.open(scriptFor(step.script), TextMode::Message);
            return true;
        default:
            std::fprintf(stderr,
                         "frame %u: the scenario cannot perform SceneAction::%s.\n"
                         "  Nothing after this frame would be on the oracle's "
                         "timeline, so the run stops here rather than emitting a "
                         "trace whose first reported divergence would be in the "
                         "wrong place.\n",
                         g_sim.frame, actionName(step.action));
            return false;
    }
}

bool stepMachine() {
    SceneView v = g_sim.view();
    StageStep s{};
    switch (g_sim.machine) {
        case Machine::None: return true;
        case Machine::Dive:   s = g_sim.dive.update(v, g_sim.fx); break;
        case Machine::Island:
            // The race is the one place two owners hold one number.  On the SNES
            // `rikuWp` was a global that UpdateRiku advanced and RaceRun read;
            // here the machine owns it and WorldState carries the copy the actor
            // layer advances, so somebody has to hand it back -- once a frame,
            // BEFORE the machine looks, or Riku crosses the line and nothing
            // notices until the frame after.
            g_sim.island.setRikuWaypoint(g_sim.world.rikuWp);
            s = g_sim.island.update(v, g_sim.fx);
            break;
        case Machine::Night:
            s = g_sim.night.update(v, g_sim.fx);
            // keyGot is one global on the SNES that NightUpdate writes and
            // UpdateWorld reads, in that order within a frame -- so the copy
            // has to be handed over HERE, between the two, and not at the top
            // of the next frame.  It gates both directions: a wooden sword
            // cannot hurt a Shadow and a Shadow cannot hurt Sora.
            g_sim.world.keyGot = g_sim.night.keyGot();
            break;
        case Machine::Town:   s = g_sim.town.update(v, g_sim.fx); break;
    }
    return perform(s);
}

// ---------------------------------------------------------------------------
// The scenarios
// ---------------------------------------------------------------------------
// What the FIRST emitted frame was on the oracle, which is never simply "a
// frame".  Getting this wrong is an off-by-one in the only direction that
// matters, and the night is what found it: its machine has a live timer, so one
// extra update at the start put every Shadow of the next four hundred frames one
// frame early.
enum class FirstFrame : uint8_t {
    // The oracle's frame 0: the reset path.  State exists and no game update has
    // run at all, so the line is emitted before any work.
    Reset,
    // The frame RestartScene ran.  GameOverUpdate runs INSTEAD of the scene
    // script (main.s dispatches on deadFlag), so on that frame the world updated
    // and the stage machine did not -- the scene it rebuilt gets its first
    // update on the frame AFTER.
    Retry,
};

struct Scenario {
    const char* name;
    const char* what;
    const char* oracle;         // the snes_trace.py run this pairs with
    int firstFrame;             // what the first emitted line is numbered
    FirstFrame first;
    bool (*setup)();
};

// The SNES's own cast for the first Station of Awakening, as the oracle's frame
// 0 records it.  Written out here rather than read from a table because there is
// no table to read: the SNES spawns its stations from a list in dive.s, and the
// DS's equivalent data (assets/ds/station1_cast.txt) deliberately holds
// different numbers -- see divergence 004.  The whole point of this scenario is
// to hold the content equal and compare the code, and that means the SNES's.
struct CastRow { ActType type; int i; int j; };
constexpr CastRow ORACLE_STATION1[] = {
    {ActType::Sora, 16, 11},
    {ActType::Pedestal, 11, 8},  {ActType::Sword, 11, 8},
    {ActType::Pedestal, 19, 5},  {ActType::Shield, 19, 5},
    {ActType::Pedestal, 19, 11}, {ActType::Staff, 19, 11},
};

bool spawnRows(const CastRow* rows, int n) {
    for (int k = 0; k < n; ++k) {
        const int slot = g_sim.actors.spawn(rows[k].type, tileCentre(rows[k].i),
                                            tileCentre(rows[k].j));
        if (slot < 0) {
            std::fprintf(stderr, "the actor pool refused row %d\n", k);
            return false;
        }
        g_sim.actors.z[slot] = has(g_sim.actors.flags[slot], ActFlags::Flat)
                                   ? uint8_t(0)
                                   : g_sim.ground.heightAt(rows[k].i, rows[k].j);
        if (rows[k].type == ActType::Sora) g_sim.player = slot;
    }
    return true;
}

// --- station: the disc, with the opening line already dismissed --------------
bool setupStation() {
    if (!loadGround(true, "divecoll.bin", nullptr, ORACLE_MAP_W, ORACLE_MAP_H))
        return false;
    if (!spawnRows(ORACLE_STATION1,
                   int(sizeof ORACLE_STATION1 / sizeof *ORACLE_STATION1)))
        return false;
    g_sim.dive.begin();
    g_sim.machine = Machine::Dive;
    g_sim.scene = SceneId::Dive;
    // No box.  The oracle's counterpart pokes txtState to zero, which is the
    // same statement made to the other machine: the intro has been read.
    return true;
}

// --- dive: the same scene, as the DS actually ships it -----------------------
bool setupDive() {
    if (!loadGround(false, "station1coll.bin", "station1height.bin",
                    ORACLE_MAP_W, ORACLE_MAP_H))
        return false;
    Blob cast = khhost::load("station1cast.bin", g_cast, sizeof g_cast);
    if (cast.empty()) {
        std::fprintf(stderr, "station1cast.bin is missing; run the pipeline\n");
        return false;
    }
    const SpawnResult r = spawnCast(g_sim.actors, cast, g_sim.ground);
    if (!r.complete()) {
        std::fprintf(stderr, "the cast did not load: %d refused, %s\n", r.refused,
                     r.malformed ? "malformed" : "well formed");
        return false;
    }
    for (int i = 0; i < MAX_ACTORS; ++i)
        if (g_sim.actors.type[i] == ActType::Sora) { g_sim.player = i; break; }
    g_sim.dive.begin();
    g_sim.machine = Machine::Dive;
    g_sim.scene = SceneId::Dive;
    g_sim.dialogue.open(scriptFor(ScriptId::DiveIntro), TextMode::Message);
    return true;
}

// --- darkside: the third station, reached the way the oracle reaches it ------
//
// The oracle pokes sceneId and deadFlag and lets the ROM's OWN retry path run:
// RestartScene loads SCENE_DIVE3, spawns soraOnlySpawns -- which is Sora and
// nothing else -- calls SpawnBoss and sets DIVE_BOSS.  So the cast to reproduce
// is two actors, and the frame it exists from is 15.
bool setupDarkside() {
    if (!loadGround(true, "dive3coll.bin", nullptr, ORACLE_MAP_W, ORACLE_MAP_H))
        return false;
    const CastRow rows[] = {{ActType::Sora, 16, 11}};
    if (!spawnRows(rows, 1)) return false;
    const int boss = g_sim.actors.spawn(ActType::Darkside, World::fromRaw(4224),
                                        World::fromRaw(1920));
    if (boss < 0) return false;
    g_sim.actors.hp[boss] = uint8_t(DS_MAX_HP);
    g_sim.actors.state[boss] = ActState::Idle;      // DSS_REST shares the byte
    g_sim.actors.timer[boss] = uint8_t(DS_REST);
    g_sim.world.bossHP = uint8_t(DS_MAX_HP);
    g_sim.dive.begin();
    g_sim.dive.setStage(DiveStage::Boss);
    g_sim.machine = Machine::Dive;
    g_sim.scene = SceneId::Dive3;
    return true;
}

// --- armor: the Guard Armor, reached the way the oracle reaches it -----------
//
// TownRestart sets townTimer to 1 when townStage is T_BOSS and the ROM brings
// the armour down from the top by itself, onto the district's ordinary cast --
// so unlike Darkside's soraOnlySpawns there are eight bystanders to reproduce,
// and they are not decoration: they occupy slots, and a slot is a trace column.
constexpr CastRow ORACLE_TOWN1[] = {
    // town1Spawns, town.s:1404.  Sora is at (14,12) -- "face down in the middle
    // of the square" -- and NOT at (16,10): (16,10) is where RaiseArmor moves
    // him on the next frame, and transcribing the position after the move
    // instead of the one before it was how this scenario hid a whole action.
    {ActType::Sora, 14, 12},
    {ActType::Cid, 23, 6},
    {ActType::TownMan, 8, 9},
    {ActType::TownWoman, 18, 13},
    {ActType::Lamp, 3, 7},
    {ActType::Lamp, 27, 7},
};

bool setupArmor() {
    if (!loadGround(true, "town1coll.bin", "town1height.bin", ORACLE_MAP_W,
                    ORACLE_MAP_H))
        return false;
    if (!spawnRows(ORACLE_TOWN1,
                   int(sizeof ORACLE_TOWN1 / sizeof *ORACLE_TOWN1)))
        return false;

    // THE ARMOUR IS NOT SPAWNED HERE, and an earlier version of this scenario
    // spawning it is why frame 15 was mislabelled as frame 16.  TownRestart
    // arms `townTimer` to ONE, and it is WatchArmor on the NEXT frame that turns
    // that into RaiseArmor -- so the retry re-enters the fight through the same
    // door the fight came in by, and the trace's first frame has six actors and
    // not nine.  Setting up the outcome instead of the route made the two agree
    // for the wrong reason.
    g_sim.town.begin(g_sim.rng);
    g_sim.town.setDistrict(SceneId::Town1);
    g_sim.town.setStage(TownStage::Boss);
    g_sim.town.restart(g_sim.fx);
    g_sim.machine = Machine::Town;
    g_sim.scene = SceneId::Town1;
    return true;
}

// --- town: the Second District's wave, and the second seed ------------------
//
// The other half of §M6's determinism hazard.  The night proved the $ACE1
// sequence; this proves the $1D57 one, and a different consumer of it:
// TownShadows has a wave counter and an alive cap where SpawnShadows has only a
// cap, and it increments the counter ONLY on a spawn that succeeded -- so a spot
// refused for being on top of the player costs a draw and no progress.
//
// WHY THE SEED IS POKED RATHER THAN REACHED.  Only TownBegin writes $1D57
// (town.s:73), and TownBegin runs when the night hands over -- which is an
// entire night away.  TownRestart deliberately does not re-seed.  But nothing
// between TownBegin and the Second District draws from the LFSR: the First
// District spawns nothing.  So $1D57 with no draws IS the state the player
// arrives in the square with, and --poke16 says so exactly.
constexpr CastRow ORACLE_TOWN2[] = {
    {ActType::Sora, 26, 5},
    {ActType::Lamp, 3, 7},
    {ActType::Lamp, 28, 7},
};

// townSpots, town.s:1447.  Eight, and eight divides 256 -- so unlike the
// night's ten, repeated subtraction and a modulo agree here and the reduction
// cannot be what a divergence is about.
constexpr Tile ORACLE_TOWN_SPOTS[] = {
    {6, 6}, {20, 6}, {9, 9}, {22, 9}, {6, 12}, {24, 12}, {16, 13}, {11, 11},
};

bool setupTown() {
    if (!loadGround(true, "town2coll.bin", "town2height.bin", ORACLE_MAP_W,
                    ORACLE_MAP_H))
        return false;
    if (!spawnRows(ORACLE_TOWN2,
                   int(sizeof ORACLE_TOWN2 / sizeof *ORACLE_TOWN2)))
        return false;

    g_sim.rng.seed(Rng::TOWN_SEED);
    g_sim.town.setSpots(ORACLE_TOWN_SPOTS,
                        int(sizeof ORACLE_TOWN_SPOTS
                            / sizeof *ORACLE_TOWN_SPOTS));
    g_sim.town.setDistrict(SceneId::Town2);
    g_sim.town.setStage(TownStage::Second);
    // NOT begin(): that is TownBegin, which re-seeds and rewinds to T_ARRIVE.
    // The ROM ran TownRestart here, which keeps the stage and the seed.
    g_sim.town.restart(g_sim.fx);
    g_sim.machine = Machine::Town;
    g_sim.scene = SceneId::Town2;
    return true;
}

// --- race: Riku running the course, and the island standing still ------------
//
// A two-stage poke on the oracle side: restart onto the island so InitWorld
// builds the cast, then thirty frames later start the race on top of it.  He is
// therefore running from where he SITS rather than from the start line, which
// exercises the waypoint walk from an arbitrary position -- see §M3b.
constexpr CastRow ORACLE_ISLAND[] = {
    {ActType::Sora, 11, 12},
    {ActType::Palm, 15, 4},   {ActType::Palm, 6, 7},
    {ActType::Palm, 16, 10},  {ActType::Palm, 27, 7},
    {ActType::PalmC, 9, 10},  {ActType::PalmC, 13, 10},
    {ActType::RockBig, 15, 9}, {ActType::Rock, 8, 12},
    {ActType::Kairi, 12, 12}, {ActType::Riku, 27, 8},
    {ActType::Tidus, 7, 4},   {ActType::Selphie, 14, 13},
    {ActType::Wakka, 19, 12},
    {ActType::Faces, 1, 6},   {ActType::Door, 2, 6},
    {ActType::Scribble, 3, 6},
};

bool setupRace() {
    // THE ONE SCENE WITH REAL HEIGHTS.  Everything else stands on a plane.
    if (!loadGround(true, "collmap.bin", "heightmap.bin", ORACLE_MAP_W,
                    ORACLE_MAP_H))
        return false;
    if (!spawnRows(ORACLE_ISLAND,
                   int(sizeof ORACLE_ISLAND / sizeof *ORACLE_ISLAND)))
        return false;
    g_sim.island.begin();
    // diveStage IS THE GATE, not decoration.  SceneUpdate runs IslandUpdate only
    // when diveStage is DIVE_ARRIVED and otherwise runs DiveUpdate even on the
    // island (main.s:622-628), so the ROM's own restart path writes it on the
    // way in (dive.s:388).  A scenario that left it at Intro would be running a
    // machine the SNES was not -- and the trace found exactly that, as 670
    // frames of a single differing column.
    g_sim.dive.begin();
    g_sim.dive.setStage(DiveStage::Arrived);
    // NOT beginRace(): that arms the 192-frame countdown, and the oracle pokes
    // questState straight to Q_RACE_RUN with rikuWp zero, which is the state the
    // countdown would have arrived at.  Reproduce the state, not the route.
    g_sim.island.setState(QuestState::RaceRun);
    g_sim.island.setRikuWaypoint(0);
    g_sim.machine = Machine::Island;
    g_sim.scene = SceneId::Island;
    // questState and rikuWp are globals on the SNES that both UpdateRiku and
    // RaceRun reach; here the machine owns them and WorldState carries the copy
    // the actor layer advances.  The device tier synchronises the two once a
    // frame and so does the loop below.
    g_sim.world.raceRunning = true;
    g_sim.world.rikuWp = 0;
    return true;
}

// --- night: the storm, the search, and the LFSR they both come out of --------
//
// THIS IS THE SCENARIO THE RNG IS FOR.  The brief names one determinism hazard
// by name: "the RNG is a 16-bit Galois LFSR seeded to $ACE1 by InitWorld and to
// $1D57 by TownBegin -- seed yours identically and advance it at the same
// points, or the Heartless spawn positions diverge immediately and the diff is
// worthless."  Nothing before this scenario tested it, because nothing before
// this scenario drew from it: the stations, the bosses and the race are all
// deterministic.  The night draws from two places at once -- the flash wait and
// the spawn spot -- so a trace that matches proves the sequence AND the order.
//
// Reaching it takes a TWO-STAGE poke, and the reason is the whole point.
// Restarting straight into the night looks like it works and is worthless:
// `rngState` is seeded by InitWorld (world.s:52), InitWorld is called when the
// ISLAND is entered or restarted, and RestartScene's night branch calls
// NightRestart instead -- so on that route the LFSR is still zero, and a Galois
// shift register whose state is zero STAYS ZERO.  Every flash waits exactly
// FLASH_GAP_MIN and every Shadow comes up on the same tile, for ever.  TownBegin
// has a comment warning about precisely this (town.s:68).  So: restart onto the
// island first, which runs InitWorld and seeds $ACE1, and only then restart into
// the night.  The setup is the game's own code, twice over.
//
// The cast is the island's after dark -- the island's minus the coconuts and the
// other three children, with Riku out on the small island and Kairi at the cave.
constexpr CastRow ORACLE_NIGHT[] = {
    {ActType::Sora, 12, 12},        // where she was standing this afternoon
    {ActType::Riku, 27, 8},         // the small island, past the raised bridge
    {ActType::Kairi, 2, 7},         // the Secret Place, with her back to it
    {ActType::Palm, 15, 4},   {ActType::Palm, 6, 7},
    {ActType::Palm, 16, 10},  {ActType::Palm, 27, 7},
    {ActType::Palm, 9, 10},   {ActType::Palm, 13, 10},
    {ActType::RockBig, 15, 9}, {ActType::Rock, 8, 12},
    {ActType::Faces, 1, 6},   {ActType::Door, 2, 6},
    {ActType::Scribble, 3, 6},
};

// nightSpots, night.s:1057.  Ten of them, against the DS's thirty-five --
// divergence 003 -- and the SNES's ten are what an oracle fixture needs,
// because rng.pick() reduces modulo the COUNT and a different count is a
// different sequence of spots from the same LFSR.
constexpr Tile ORACLE_NIGHT_SPOTS[] = {
    {7, 9}, {17, 9}, {10, 10}, {5, 10}, {14, 11},
    {19, 11}, {12, 8}, {26, 8}, {9, 6}, {15, 7},
};

bool setupNight() {
    // The night is the island after dark: the same collision, the same heights,
    // byte for byte.  What makes it night is one palette upload.
    if (!loadGround(true, "collmap.bin", "heightmap.bin", ORACLE_MAP_W,
                    ORACLE_MAP_H))
        return false;
    if (!spawnRows(ORACLE_NIGHT,
                   int(sizeof ORACLE_NIGHT / sizeof *ORACLE_NIGHT)))
        return false;

    g_sim.night.setSpots(ORACLE_NIGHT_SPOTS,
                         int(sizeof ORACLE_NIGHT_SPOTS
                             / sizeof *ORACLE_NIGHT_SPOTS));
    // The SNES's density, not the island's.  Six alive at a seventy-frame gap.
    g_sim.night.setDensity(SNES_SHADOW_MAX, SNES_SHADOW_GAP);

    // THE SEED, AND THE FIRST DRAW.  InitWorld seeds $ACE1 at reset and nothing
    // in the Dive touches the LFSR, so its state when RestartScene runs is still
    // the seed -- and NightRestart's ArmLightning is the first draw of the game.
    // restart() makes that draw, which is why it takes the Rng.
    g_sim.rng.seed(Rng::WORLD_SEED);
    g_sim.night.restart(g_sim.rng, false, g_sim.fx);
    // ...and diveStage is the gate again: the island restart that seeded the
    // LFSR also wrote DIVE_ARRIVED (dive.s:388), and it stays written.
    g_sim.dive.begin();
    g_sim.dive.setStage(DiveStage::Arrived);
    // Its RespawnNightCast is what the rows above are: the cast is already up.
    g_sim.world.keyGot = g_sim.night.keyGot();      // a wooden sword, for now
    g_sim.machine = Machine::Night;
    g_sim.scene = SceneId::Night;
    return true;
}

constexpr Scenario SCENARIOS[] = {
    {"station",
     "the first Station of Awakening, the oracle's ground and cast, the "
     "opening line already dismissed",
     "tools/snes_trace.py --frames 130 --input traces/station.txt "
     "--poke txtState=0",
     0, FirstFrame::Reset, setupStation},
    {"dive",
     "the same scene as the DS ships it: its own smaller disc, its own cast "
     "positions, and the intro box open",
     "tools/snes_trace.py --frames 150 --input traces/dive.txt",
     0, FirstFrame::Reset, setupDive},
    {"darkside",
     "the third station and the boss, the oracle's ground, nobody interfering",
     "tools/snes_trace.py --frames 300 --input traces/idle.txt --poke sceneId=2 "
     "--poke deadFlag=2 --no-strict",
     15, FirstFrame::Retry, setupDarkside},
    {"armor",
     "the Guard Armor coming down on the First District, the oracle's ground "
     "and cast",
     "tools/snes_trace.py --frames 400 --input traces/idle.txt --poke sceneId=6 "
     "--poke townStage=5 --poke deadFlag=2 --no-strict",
     15, FirstFrame::Retry, setupArmor},
    {"town",
     "the Second District's wave: eight spots, five alive at once, and the "
     "$1D57 seed the night's does not reach",
     "tools/snes_trace.py --frames 900 --input traces/town.txt --poke sceneId=7 "
     "--poke townStage=2 --poke deadFlag=2 --poke16 rngState=0x1D57 --no-strict",
     15, FirstFrame::Retry, setupTown},
    {"night",
     "the storm, the search, and the LFSR the flash wait and the spawn spots "
     "both come out of -- the one scenario that tests the RNG",
     "tools/snes_trace.py --frames 400 --input traces/idle.txt --poke sceneId=3 "
     "--poke deadFlag=2 --poke 30:sceneId=4 --poke 30:deadFlag=2 --no-strict",
     30, FirstFrame::Retry, setupNight},
    {"race",
     "Riku running the course while the island stands still, the oracle's "
     "ground and cast",
     "tools/snes_trace.py --frames 700 --input traces/idle.txt --poke sceneId=3 "
     "--poke deadFlag=2 --poke 30:questState=6 --poke 30:rikuWp=0 --no-strict",
     30, FirstFrame::Retry, setupRace},
};

const Scenario* findScenario(const char* name) {
    for (const Scenario& s : SCENARIOS)
        if (sameWord(name, s.name)) return &s;
    return nullptr;
}

void listScenarios() {
    for (const Scenario& s : SCENARIOS) {
        std::printf("%-10s %s\n", s.name, s.what);
        std::printf("%-10s   pairs with: %s\n", "", s.oracle);
    }
}

int usage() {
    std::fprintf(stderr,
                 "usage: dstrace --scenario NAME [--frames N] [--input FILE]\n"
                 "               [-o FILE] [--rev TEXT] [--list]\n");
    return 2;
}

}  // namespace

int main(int argc, char** argv) {
    const char* scenarioName = nullptr;
    const char* input = nullptr;
    const char* outPath = nullptr;
    const char* rev = "unknown";
    int frames = 150;

    for (int i = 1; i < argc; ++i) {
        const char* a = argv[i];
        const bool hasNext = i + 1 < argc;
        if (!std::strcmp(a, "--list")) { listScenarios(); return 0; }
        else if (!std::strcmp(a, "--scenario") && hasNext) scenarioName = argv[++i];
        else if (!std::strcmp(a, "--input") && hasNext) input = argv[++i];
        else if (!std::strcmp(a, "--rev") && hasNext) rev = argv[++i];
        else if ((!std::strcmp(a, "-o") || !std::strcmp(a, "--out")) && hasNext)
            outPath = argv[++i];
        else if (!std::strcmp(a, "--frames") && hasNext) frames = std::atoi(argv[++i]);
        else return usage();
    }
    if (!scenarioName) return usage();
    if (frames < 1 || frames > MAX_FRAMES) {
        std::fprintf(stderr, "--frames must be 1..%d\n", MAX_FRAMES);
        return 2;
    }
    const Scenario* sc = findScenario(scenarioName);
    if (!sc) {
        std::fprintf(stderr, "no scenario '%s'; --list says what there is\n",
                     scenarioName);
        return 2;
    }
    // The script is read out to the LAST frame this run will label, not the
    // first `frames` of it -- see the pad read in the loop below.
    const int lastLabel = sc->firstFrame + frames;
    if (lastLabel > MAX_FRAMES) {
        std::fprintf(stderr, "%s starts at frame %d, so --frames must be at "
                             "most %d\n", sc->name, sc->firstFrame,
                     MAX_FRAMES - sc->firstFrame);
        return 2;
    }
    if (!readScript(input, lastLabel)) return 2;

    g_sim.actors.clear();
    if (!sc->setup()) return 1;

    std::FILE* out = outPath ? std::fopen(outPath, "w") : stdout;
    if (!out) {
        std::fprintf(stderr, "cannot write %s\n", outPath);
        return 1;
    }

    char buf[TRACE_LINE_MAX];
    if (!traceHeader(buf, sizeof buf, "ds", rev)) {
        std::fprintf(stderr, "the header did not fit; raise the buffer\n");
        return 1;
    }
    std::fputs(buf, out);

    uint16_t prevHeld = 0;
    int status = 0;
    for (int n = 0; n < frames; ++n) {
        const int label = sc->firstFrame + n;
        // The oracle's frame 0 is the reset path: state exists, no update has
        // run.  Every other frame is one iteration of MainLoop.
        const bool doWork = !(sc->first == FirstFrame::Reset && n == 0);
        if (doWork) {
            g_sim.frame = uint32_t(label);
            // ReadPad.  padPressed is (now & ~last), and `last` starts at zero
            // because that is what it is after the SNES clears WRAM.
            //
            // Indexed by the FRAME NUMBER and not by the loop, which matters for
            // a scenario that starts partway in: the oracle spent its first
            // fifteen frames loading a scene with the script still running, so
            // its frame 15 saw the script's frame 15.  Reading g_pads[0] there
            // would be a different run of the same file.
            const uint16_t held = (label >= 0 && label < MAX_FRAMES)
                                      ? g_pads[label] : uint16_t(0);
            g_sim.pad.held = held;
            g_sim.pad.pressed = uint16_t(held & ~prevHeld);
            prevHeld = held;

            g_sim.dialogue.update(g_sim.pad);
            // ...except on the retry frame, where the scene script did not run.
            if (!(sc->first == FirstFrame::Retry && n == 0)) {
                if (!stepMachine()) { status = 1; break; }
            }
            SceneView v = g_sim.view();
            updateWorld(g_sim.world, v, g_sim.fx);
        }
        const TraceStage st = g_sim.stage();
        if (!traceLine(buf, sizeof buf, uint32_t(label), g_sim.actors,
                       g_sim.player, st)) {
            std::fprintf(stderr, "frame %d: the line did not fit; raise "
                                 "TRACE_LINE_MAX\n", label);
            status = 1;
            break;
        }
        std::fputs(buf, out);
        std::fputc('\n', out);
    }

    if (outPath) std::fclose(out);
    if (status == 0) {
        int live = 0;
        for (int i = 0; i < MAX_ACTORS; ++i)
            if (g_sim.actors.type[i] != ActType::None) ++live;
        std::fprintf(stderr, "%s: %d frames %d..%d, %d live actors at the end\n",
                     sc->name, frames, sc->firstFrame,
                     sc->firstFrame + frames - 1, live);
    }
    return status;
}
