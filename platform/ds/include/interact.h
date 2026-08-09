#pragma once
// interact.h -- who is standing next to what, and what a button press does.
//
// WHY THIS IS ITS OWN FILE AND WHY IT WAS MISSING.  §M5 delivered the scene
// STAGE MACHINES and stated the rule that made them testable: a machine "reads
// the world, advances its stage and its timers, writes the frame's screen
// effects, and returns an ACTION for the caller to perform".  What it did not
// deliver was the caller.  So `IslandMachine::talkToKairi`, `talkToRiku`,
// `openDoor`, `arriveAtThird`, `tagPaopu`, `reachHome` and `DiveMachine::choose`
// were all public with nothing calling them, `Item` and `NEED[]` were data with
// no consumer, and the island's quest was unreachable: you could not pick up a
// log, so the raft never got finished, so the race never started.  It is the
// same hole §M6 found in §M3 -- `tryMoveActor` with no caller -- one layer up.
//
// On the SNES this code is spread through the four scene files and called from
// each scene's update: CheckPickups, SwingAt, FindTalker, FindProp, LookAt,
// TalkTo (island.s), CheckDoors, TalkTown (town.s), TalkTarget (night.s),
// FindWeapon, AskAbout, HandleAnswer (dive.s), and GameOverUpdate, which runs
// INSTEAD of all of them.
//
// It keeps §M5's rule.  Nothing here opens a dialogue box, loads a scene or
// touches a register: every entry point returns a StageStep, exactly as a stage
// machine does, and the caller performs it.  That is what lets the whole file be
// tested with no renderer.

#include <cstdint>

#include "actor.h"
#include "constants.h"
#include "scene.h"
#include "stage.h"
#include "world.h"

namespace kh {

// ---------------------------------------------------------------------------
// itemCount, and the list it is checked against
// ---------------------------------------------------------------------------
struct Inventory {
    uint8_t count[static_cast<int>(Item::Count)] = {};

    void reset() { *this = Inventory{}; }
    uint8_t of(Item i) const { return count[static_cast<int>(i)]; }
    void add(Item i) {
        uint8_t& n = count[static_cast<int>(i)];
        if (n < 255) ++n;       // the SNES's `inc` would wrap; nothing can reach it
    }

    // HaveAll, island.s:695.  THE TWO DAYS ASK FOR DIFFERENT THINGS and the
    // lists do not overlap: day one is timber -- logs, cloth, rope -- and day
    // two is provisions.  A port that checked all eight on both days would make
    // day one unfinishable.
    bool haveAll(int day) const;
};

// ---------------------------------------------------------------------------
// A door, as the town needs one
//
// The SNES's doorTable is seven bytes: which map it is in, where in that map,
// where it leads, where to stand on the far side, and THE STAGE IT WANTS.  The
// DS's <scene>doors.bin is four -- (i, j, land_i, land_j) -- because scene.h
// decided that "which district it leads to is scene logic and the table
// deliberately does not say".  That decision stands, and this is its
// consequence: the destination and the gate are supplied by whoever builds this
// array, not read out of the file.
//
// `needs` is what makes townStage PROGRESS AND NOT LOCATION.  A door carries
// the stage the town must have reached; below it the door has a line instead of
// an opening, and above it the player may walk back through freely.  No lock
// flags anywhere.
// ---------------------------------------------------------------------------
struct TownDoor {
    Tile at;
    SceneId to = SceneId::Town1;
    Tile landing;
    TownStage needs = TownStage::Arrive;
};

// The one piece of state the interaction layer keeps: lastTileI / lastTileJ,
// which is what makes a door fire on the frame he arrives rather than on every
// frame he stands in front of it.  $FF means nowhere, so whichever tile he
// lands on next counts as a step onto it -- LoadDistrict sets exactly that.
struct Interact {
    int16_t lastI = -1;
    int16_t lastJ = -1;

    void reset() { lastI = -1; lastJ = -1; }
};

// ---------------------------------------------------------------------------
// One frame of interaction, per scene
//
// Each is the `@play` tail of its scene's update: the part that runs when no
// beat is in progress and the world is the player's.  Each returns what to say
// or do, and StageStep{} for "nothing happened".
// ---------------------------------------------------------------------------

// PICK and DROP: A next to a dream weapon asks about it, and the answer to that
// prompt is what moves the Dive on.  `answer` is Dialogue::result(), 1 for yes
// and 2 for no, 0 for "no prompt has closed" -- the caller clears it after,
// exactly as DiveUpdate does with txtResult.
StageStep diveInteract(DiveMachine& m, SceneView& view, int answer);

// The island, gated on the quest being live: walk into a pickup, swing at a
// fish or a coconut palm, press A to talk or to look at the cave wall.
StageStep islandInteract(IslandMachine& m, SceneView& view, Inventory& inv);

// Sora's two legs of the race, which are tested where he IS STANDING rather
// than where he went -- RaceRun runs after UpdateWorld, so a player who crossed
// the line during the frame is credited on it.
void islandRaceCheck(IslandMachine& m, SceneView& view);

// The night: A next to whoever the current beat is waiting for.
StageStep nightInteract(NightMachine& m, SceneView& view);

// The town: doors first, then talking.  A conversation that just opened holds
// the door shut for that frame, which is the order TownUpdate uses and not an
// accident -- talking on a doorway would otherwise change district mid-sentence.
StageStep townInteract(TownMachine& m, Interact& st, SceneView& view,
                       const TownDoor* doors, int nDoors);

// GameOverUpdate, dive.s:324, which runs INSTEAD of the scene's own update for
// every scene -- "being out of HP takes priority over whatever the scene was
// doing" (main.s:602).  Two passes: the first puts the card up, the second asks
// the caller to restart.  The caller clears `deadFlag` when it has.
StageStep gameOverStep(WorldState& w, SceneView& view);

// ---------------------------------------------------------------------------
// The pieces, exposed because the tests drive them individually
// ---------------------------------------------------------------------------

// The nearest actor of a type range within a box of the player, or -1.  This is
// the SNES's NearPlayer scan, and every one of the four searches below is it
// with different bounds and a different range of types.
int nearestOfRange(const Actors& a, int player, ActType lo, ActType hi,
                   World rx, World ry);

// FindProp is the one search that does NOT take the first hit: three drawings
// hang within arm's reach of each other on the cave wall, so it takes whichever
// is closest by |dx| + |dy|.  Both axes are the same scale, which is what makes
// that sum meaningful.
int nearestProp(const Actors& a, int player);

}  // namespace kh
