#pragma once
// The game, minus the hardware.  §M7's frame loop, and the thing that ties the
// four stage machines together.
//
// WHAT WAS MISSING, in one sentence: trace_main.cpp's header says it, and has
// said it since §M6.  "There is no equivalent here, because the thing that
// would tie the four stage machines together -- load a scene, spawn its cast,
// perform the action a machine returned -- is the device tier, and the device
// tier is §M7 and blocked on a toolchain."  A scenario was that tying-together
// for one situation.  This is it for all of them.
//
// WHY IT IS IN device/ AND NOT IN arm9/source/main.cpp.  Because otherwise it
// could not be tested, and it is the largest untested thing the port would have
// acquired.  main.cpp includes nds.h, so nothing in this container can compile
// it; every line that ends up there is a line no host run will ever see again.
// So the split is drawn as far toward this file as it will go:
//
//   HERE      the scene table, the frame order, which machine runs, what an
//             interaction returns, what performing an action does to the actor
//             pool -- all of it plain C++ over data already in the tree, and
//             all of it exercised by host/tests/test_boot.cpp against the real
//             .bin files.
//   THERE     scanKeys, touchRead, DMA, swiWaitForVBlank, and the addresses
//             asset bytes are uploaded to.  Under two hundred lines, none of
//             which decides anything.
//
// WHAT THE ORACLE DOES AND DOES NOT SAY ABOUT THIS FILE, and it matters.
// tools/trace_check.py drives trace_main.cpp, not this -- so every SIMULATION
// call below is oracle-checked (they are the same functions, in the same order)
// and the WIRING is not.  In particular the interaction layer is called here
// and nowhere in the trace corpus, because no scenario presses A next to
// anything.  That is a real gap and it is stated rather than papered over: the
// scenarios prove the physics and the stage timings, and what proves the wiring
// is test_boot.cpp plus a person holding the machine.

#include <cstdint>

#include "grid.h"
#include "ground.h"
#include "hud.h"
#include "interact.h"
#include "perform.h"
#include "scene.h"
#include "stage.h"
#include "text.h"
#include "world.h"

namespace kh::device {

// Everything one scene needs, however the bytes were obtained.
//
// `chars` is the only field the simulation never looks at -- it is what the 2D
// ground renderer draws with -- and it is here anyway, because a scene is one
// thing and splitting it across two providers is how the collision map and the
// character map come to be different scenes' (device/ground.cpp already refuses
// that, and this is what stops it being possible to arrange).
//
// An empty optional table is not an error: gen/assets.h's SceneTable says which
// scenes have spots and doors, and most have neither.
// The optional tables are gen/assets.h's SceneTable set, one field each, and
// they are all here rather than only the ones a scene has: "the DEVICE cannot
// find out at run time -- there is no filesystem, a .bin is a linked symbol",
// so the generator says which exist and an absent one arrives empty.
struct SceneBlobs {
    Blob collision;
    Blob height;
    Blob chars;
    Blob cast;             // [base]: placed on entry, and again after a death
    Blob day1;             // the island's raft errand
    Blob day2;             // ...and its provisions; never both
    Blob boss;             // [boss]: placed by SpawnBoss, not on entry
    Blob pair;             // Donald and Goofy, the Third District
    Blob spots;
    Blob doors;
    int tilesW = 0;
    int tilesH = 0;
};

// Where a scene's bytes come from: a file on the host, a linked symbol on the
// device.  §M2's whole seam, as one function pointer.
//
// A function pointer and not a virtual, because platform/ds/README.md sanctions
// exactly one virtual in the port and it is GroundRenderer.  Returns nullptr for
// a scene it has no bytes for, which Game turns into a named error rather than
// an empty world.
using SceneSource = const SceneBlobs* (*)(SceneId);

// How many doors any one district has.  gen/doors.h ships two, three and one;
// four is the buffer, and readDoors() returns -1 rather than overrunning it.
constexpr int MAX_DOORS = 8;

class Game {
public:
    explicit Game(SceneSource source);

    // Bring the world up.  Seeds the LFSR the way InitWorld does, loads the
    // scene, and starts the machine that owns it.
    bool begin(SceneId first);

    // ONE FRAME, in the SNES's order (main.s's MainLoop):
    //
    //     ReadPad -> TextUpdate -> SceneUpdate -> UpdateWorld -> UpdateCamera
    //
    // The pad arrives already read, because reading it is the two lines of
    // libnds this file exists to not contain.  Everything downstream of here is
    // the same code the trace emitter runs, in the same order, which is what
    // makes the oracle's verdict on the simulation transfer to the device.
    void frame(const Pad& pad);

    // Enter a scene: its ground, its characters, its cast, its spots and doors.
    // Does NOT start a machine -- a district swap keeps the town's stage, which
    // is the whole of what "townStage is progress, not location" means.
    bool enter(SceneId s);

    // --- what the renderer needs -------------------------------------------
    const Actors& actors() const { return actors_; }
    const Camera& camera() const { return cam_; }
    const ScreenFx& fx() const { return fx_; }
    const Dialogue& dialogue() const { return dialogue_; }
    const CharMap& charMap() const { return chars_; }
    const SceneGround& ground() const { return ground_; }
    SceneId scene() const { return scene_; }
    int player() const { return player_; }
    uint32_t frameCount() const { return frame_; }
    // The cel updateSoraFrame() returned this frame, or -1.  oam_pack.h calls
    // this "the first consumer the return value has ever had"; it is this one.
    int soraCel() const { return soraCel_; }
    HudState hudState() const;

    // TRUE ON THE FRAME A SCENE WAS ENTERED, and the caller owes it three
    // things: re-upload the scene's characters and palettes, hand the new
    // CharMap to the ground renderer, and load the new ground.  Cleared by
    // taking it, because a flag that has to be cleared by hand is a flag that
    // gets cleared twice or never.
    bool takeSceneChanged();

    // Why begin() or enter() refused, or nullptr.  A scene that failed to load
    // leaves the previous one standing, so without this the symptom is a door
    // that does nothing.
    const char* error() const { return error_; }

    // --- what a test asks ---------------------------------------------------
    const DiveMachine& dive() const { return dive_; }
    const IslandMachine& island() const { return island_; }
    const NightMachine& night() const { return night_; }
    const TownMachine& town() const { return town_; }
    const WorldState& world() const { return world_; }
    const Inventory& inventory() const { return inv_; }
    // The last action the machines produced, and whether it could be carried
    // out.  A refused action is the one failure mode that is otherwise silent:
    // the beat simply does not happen and the timer runs on regardless.
    SceneAction lastAction() const { return lastAction_; }
    bool lastActionPerformed() const { return lastOk_; }

private:
    SceneView view();
    StageStep machineStep();
    StageStep interactStep();
    bool perform(const StageStep& step);
    void findPlayer();
    void applyDensity();

    SceneSource source_;

    Actors actors_{};
    Dialogue dialogue_{};
    Pad pad_{};
    SceneGround ground_{};
    Rng rng_{};
    WorldState world_{};
    ScreenFx fx_{};
    Inventory inv_{};
    Interact interact_{};

    DiveMachine dive_{};
    IslandMachine island_{};
    NightMachine night_{};
    TownMachine town_{};

    Camera cam_{};
    CameraBounds bounds_{};
    CharMap chars_{};

    Tile spots_[MAX_SPOTS]{};
    int nSpots_ = 0;
    Door doors_[MAX_DOORS]{};
    int nDoors_ = 0;

    SceneId scene_ = SceneId::Dive;
    int player_ = -1;
    uint32_t frame_ = 0;
    int soraCel_ = -1;
    bool sceneChanged_ = false;
    SceneAction lastAction_ = SceneAction::None;
    bool lastOk_ = true;
    const char* error_ = nullptr;
};

// The name of an action, for the message a refusal prints.  A refusal that said
// only "the scene could not do that" would send whoever reads it to the wrong
// file; this is what makes the bottom screen's error worth photographing.
const char* actionName(SceneAction a);

// Which scenes are still 32x16 platforms in a void and pin the camera, and
// which scroll.  Exposed because it is a claim about content that a test can
// check against the map files rather than against this file's opinion.
CameraBounds boundsFor(SceneId s, int tilesW, int tilesH);

}  // namespace kh::device
