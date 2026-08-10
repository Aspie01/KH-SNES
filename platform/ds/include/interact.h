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
//
// WHO BUILDS THE ARRAY: tools/build_doors.py, out of assets/ds/town_doors.txt,
// into include/gen/doors.h -- six rows in <scene>doors.bin order, so the header
// and the binary are one table read twice.  Do not hand-write one outside a
// fixture; the generator is what checks the wiring against town.s's doorTable,
// against the maps and against the emitted collision planes.
//
// `landing` IS THE FAR SIDE -- the tile directly south of the door in the
// DESTINATION's map (town.s:1386-1389: "every landing is the tile directly
// south of the door on the far side, so a player who walks straight through
// comes out facing the square").  <scene>doors.bin ALSO has a land_i/land_j and
// it is a DIFFERENT TILE: the near side, beside the door in the door's own map,
// always (i, DOOR_ROW + 1), which test_scene.cpp's
// scene_reads_the_spot_and_door_tables asserts.  Nothing may
// copy one into the other.  It is derived by the generator from the reciprocal
// door rather than authored, so it cannot be typed wrong.
//
// `to == SceneId::Count` IS A DOOR WITH NO FAR SIDE.  The DS's districts carry
// two painted shop fronts the SNES never had -- the Accessory Shop and the
// Hotel -- and nothing is behind either of them: no fourth SceneId, no interior
// map, no cast, no .bin.  Count is the existing end-of-enum sentinel
// (constants.h:353) and it is tested BEFORE `needs`, because a shuttered door
// is not a gated one and must never fall into the bolted-door line.  Such a row
// is DECLARED rather than omitted so that "nothing happens" is on the record
// and can never be the silent result of a door somebody forgot to wire.
// ---------------------------------------------------------------------------
struct TownDoor {
    Tile at;
    SceneId to = SceneId::Town1;
    Tile landing;
    TownStage needs = TownStage::Arrive;
};

// ---------------------------------------------------------------------------
// A ROOM'S DOOR, which is a TownDoor with the two district things taken out.
//
// No GATE, because the island's rooms are always open.  A field that is never
// read is a rule pretending to be data, and the specific trap here is that
// TownStage::Arrive is ZERO -- so wiring a room with `needs = Arrive` to silence
// a compiler would give a comparison that is always true, which compiles, works,
// and type-launders a townStage into a scene that has no TownMachine.  This
// project's own rule is that a txtState cannot be compared against a txtMode; the
// same applies here.
//
// No SHUTTERED STATE either.  A shuttered door is a painted shop front the SNES
// never had, of which there are exactly two and both are in the town.  A room
// door always leads somewhere, so `to` needs no sentinel and the interaction
// layer needs no test before the gate it does not have.
//
// The LANDING is the far side, in the destination's map, on exactly the same
// terms as TownDoor::landing -- and <scene>doors.bin's land_i/land_j is still the
// NEAR side.  The two are different tiles and no code may copy one into the
// other; tools/build_doors.py derives the far one from the reciprocal row.
// ---------------------------------------------------------------------------
struct RoomDoor {
    Tile at;
    SceneId to = SceneId::Island;
    Tile landing;
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

// PlaceRacers: both of them onto the start line by Kairi.  OfferRace runs it
// between arming the countdown and saying the challenge line, and nothing in
// this port called it -- so the race began with Riku wherever he happened to be
// sitting, which §M3b's race fixture noticed and worked around rather than
// reported.  It clears their velocities too, so a racer caught mid-stride does
// not slide off the line.
void placeRacers(SceneView& view);

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

// THE DOORWAY HE HAS JUST STEPPED ONTO, or nullptr.
//
// Returns the door rather than performing it, and that is the whole reason it is
// a separate function.  Taking a room door needs a scene load and a placement,
// which is the CALLER's business -- Game::perform is the only thing in the tree
// that can enter a scene -- so this half is the part that is decidable without an
// asset source, and therefore the part a host test can drive.
//
// ONLY THE FRAME HE ARRIVES ON COUNTS, which is what `st` is for.  The town's
// CheckDoors has the same rule because a bolted door has a line and it would be
// said on every frame he stood in front of it.
//
// WHAT THE EDGE DOES *NOT* DO HERE, stated because a probe said so: it is not
// what stops a room door looping.  Removing it entirely leaves
// boot_standing_on_a_doorway_takes_it_once_and_not_every_frame passing, because
// taking a door relocates him off it -- the LANDING is beside its door and never
// on it, so there is no second frame on which to fire.  That is the invariant
// doing the work, and tools/build_doors.py enforces it as R3's landing half.
//
// The edge earns its place on the FAILED transition: if a scene load ever refuses,
// he is left standing on the doorway, and the difference is between reporting once
// and reporting on every frame until he moves.  Nothing in the shipped tree can
// fail one, so that is checked on this function directly --
// interact_a_room_door_fires_on_the_frame_he_arrives_and_not_after -- rather than
// through a load that cannot be made to break.
//
// Interact needs nothing scene-specific: Game::enter() calls interact_.reset() on
// every entry, so whichever tile Sora is put down on counts as a step onto it.
// That is safe BECAUSE a landing is beside its door and never on it -- the near
// landing is (i, j + 1) and the far landing is the reciprocal's near landing, so
// arriving never stands him on the door he would immediately take back.
const RoomDoor* roomDoorStepped(Interact& st, SceneView& view,
                                const RoomDoor* doors, int nDoors);

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
