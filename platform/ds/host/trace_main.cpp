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
        case Machine::Night:  s = g_sim.night.update(v, g_sim.fx); break;
        case Machine::Town:   s = g_sim.town.update(v, g_sim.fx); break;
    }
    return perform(s);
}

// ---------------------------------------------------------------------------
// The scenarios
// ---------------------------------------------------------------------------
struct Scenario {
    const char* name;
    const char* what;
    const char* oracle;         // the snes_trace.py run this pairs with
    int firstFrame;             // what the first emitted line is numbered
    // The oracle's frame 0 is the RESET PATH, which runs no game update -- so a
    // scenario that starts from reset emits its first line before doing any
    // work.  One reached by --poke does not: by the frame its state exists, the
    // ROM has already updated the world that frame.
    bool sampleAtReset;
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
    {ActType::Sora, 16, 10},
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

    const int armor = g_sim.actors.spawn(ActType::Armor, World::fromRaw(4224),
                                         World::fromRaw(1920));
    if (armor < 0) return false;
    g_sim.actors.hp[armor] = uint8_t(GA_MAX_HP);
    g_sim.actors.z[armor] = uint8_t(GA_DROP_Z);
    g_sim.actors.timer[armor] = uint8_t(GA_DROP);   // Drop, by the state being 0
    // SpawnHands numbers them 0 then 1, and both start ON the torso rather than
    // at their stations: the Drop branch is the one branch that does not call
    // PlaceHands, so they arrive with the body.
    for (int hand = 0; hand < 2; ++hand) {
        const int h = g_sim.actors.spawn(ActType::Gauntlet, World::fromRaw(4224),
                                         World::fromRaw(1920));
        if (h < 0) return false;
        g_sim.actors.anim[h] = uint8_t(hand);
    }
    g_sim.world.bossHP = uint8_t(GA_MAX_HP);

    g_sim.town.begin(g_sim.rng);
    g_sim.town.setStage(TownStage::Boss);
    g_sim.machine = Machine::Town;
    g_sim.scene = SceneId::Town1;
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

constexpr Scenario SCENARIOS[] = {
    {"station",
     "the first Station of Awakening, the oracle's ground and cast, the "
     "opening line already dismissed",
     "tools/snes_trace.py --frames 130 --input traces/station.txt "
     "--poke txtState=0",
     0, true, setupStation},
    {"dive",
     "the same scene as the DS ships it: its own smaller disc, its own cast "
     "positions, and the intro box open",
     "tools/snes_trace.py --frames 150 --input traces/dive.txt",
     0, true, setupDive},
    {"darkside",
     "the third station and the boss, the oracle's ground, nobody interfering",
     "tools/snes_trace.py --frames 300 --input traces/idle.txt --poke sceneId=2 "
     "--poke deadFlag=2 --no-strict",
     15, false, setupDarkside},
    {"armor",
     "the Guard Armor coming down on the First District, the oracle's ground "
     "and cast",
     "tools/snes_trace.py --frames 400 --input traces/idle.txt --poke sceneId=6 "
     "--poke townStage=5 --poke deadFlag=2 --no-strict",
     16, false, setupArmor},
    {"race",
     "Riku running the course while the island stands still, the oracle's "
     "ground and cast",
     "tools/snes_trace.py --frames 700 --input traces/idle.txt --poke sceneId=3 "
     "--poke deadFlag=2 --poke 30:questState=6 --poke 30:rikuWp=0 --no-strict",
     30, false, setupRace},
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
        const bool doWork = !(sc->sampleAtReset && n == 0);
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
            if (!stepMachine()) { status = 1; break; }
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
