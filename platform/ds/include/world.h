#pragma once
// The per-actor simulation: `UpdateWorld` and everything it dispatches to.
//
// WHY THIS IS ITS OWN FILE AND ITS OWN MILESTONE.  §M3 delivered the movement
// PRIMITIVE -- tryMoveActor, stepOk, setActorZ -- and §M5 the scene-level state
// machines.  Neither delivered the thing in between: the code that reads the
// pad, decides Sora is walking, sets a velocity and calls the primitive.  So
// `tryMoveActor` sat in grid.cpp with no caller, and §M6 found it, because you
// cannot diff traces of a simulation that does not simulate.
//
// The order here is part of the specification, not an implementation detail:
//
//   * ACTORS UPDATE IN SLOT ORDER, 0 to MAX_ACTORS-1 (world.s:292-360), and
//     slots are claimed by first-free scan.  Spawn order therefore determines
//     update order, and for equal Y it determines draw order too.  A port that
//     used a list, or a pool with different reuse, would diverge on the frame a
//     slot was recycled.
//
//   * hitStopTimer FREEZES EVERY ACTOR and nothing else.  UpdateWorld returns
//     immediately (world.s:285-289) while scene scripts, the camera, the HUD and
//     OAM all keep running.  It is decremented on the frozen frame, so a value
//     of N costs N frames of simulation.
//
//   * UpdateSoraFrame runs ONCE, after the loop, not per actor.
//
// The dispatch is a chain of type comparisons in the assembly and a switch here;
// what matters is that the set of types with behaviour is closed, and every
// other type -- the props, the pickups, the islanders -- is inert by having no
// case rather than by being skipped.

#include <cstdint>

#include "actor.h"
#include "constants.h"
#include "grid.h"
#include "stage.h"

namespace kh {

// The two bytes of world state that are not in the actor table and not in
// ScreenFx.  They are here rather than as file statics because the host tests
// run scenarios back to back and a static would carry a hit-stop across them.
struct WorldState {
    uint8_t hitStop = 0;    // hitStopTimer: every actor frozen while non-zero
    uint8_t deadFlag = 0;   // 0 none, 1 just died, 2 the card is up
    // LoadScene sets keyGot to 1 in every scene; the night is the only one that
    // clears it, and it does so itself.  It lives here rather than on
    // NightMachine because UpdateHeartless and DoAttackHit both read it and
    // neither has a scene machine to hand.
    bool keyGot = true;
    uint8_t heartTile = 0;  // the Shadow's cel base; the night uses its own
    uint8_t bossHP = 0;     // mirrored out of the actor table for the gauge

    void reset() { *this = WorldState{}; }
};

// One frame of every actor.  `fx` is written only by the death dim, which is
// the one place an actor drives the screen directly.
void updateWorld(WorldState& w, SceneView& view, ScreenFx& fx);

// ---------------------------------------------------------------------------
// The pieces, exposed because the tests drive them individually and because the
// stage machines already call some of them.
// ---------------------------------------------------------------------------

// The eight-way direction the d-pad is asking for, or none.  dirTable in
// world.s: the nine pad combinations index a table, and the centre is $FF.
// Opposite pairs do NOT cancel -- holding left and right gives RIGHT, because
// the right test runs second and overwrites.
bool readMoveDir(const Pad& pad, Dir& out);

void setVelFull(Actors& a, int slot);
void setVelHalf(Actors& a, int slot);       // dirVel >> 1, arithmetic
void clearVelocity(Actors& a, int slot);

// The walk cel advances every 7th frame: the timer is set to 6 and counted to
// zero, so a cel is visible for 7 frames and the cycle is 28.
void animateWalk(Actors& a, int slot);

void updateSora(WorldState& w, SceneView& view, ScreenFx& fx, int slot);
void updateHeartless(WorldState& w, SceneView& view, int slot);
void updateSlash(Actors& a, int slot);

// Darkside.  It never walks: it rests, then either brings a fist down on where
// you were standing 44 frames ago, or fires three orbs -- and answers standing
// underneath with a sweep, ahead of that alternation.
void updateDarkside(WorldState& w, SceneView& view, int slot);

// An orb travels in a straight line at twice a walk, IGNORING THE GROUND, and
// bursts on contact or when its life runs out.
void updateOrb(WorldState& w, SceneView& view, int slot);

// The Guard Armor.  Unlike Darkside it WALKS -- horizontally only, closing on
// the player and stopping two tiles short -- and it carries its two hands as
// separate actors that it repositions every frame.  `fx` is written because its
// arrival shakes the screen.
void updateArmor(WorldState& w, SceneView& view, ScreenFx& fx, int slot);

// Put both gauntlets where the torso says they should be.  Called from every
// branch of the Armor's machine, including the ones that do nothing else, so a
// hand is never left behind by a frame.
void placeHands(Actors& a, int armor);

// Is the player inside the boss's sweep box?  Exposed because the resting
// state tests it before choosing an attack, and because it is the one boss
// range a test can check without driving the whole machine.
bool playerUnderBoss(const Actors& a, int boss, int player);

// Contact damage, and the knockback that goes with it.  `from` is the actor
// that connected; Sora is pushed along ITS facing, not away from it.
void damageSora(WorldState& w, SceneView& view, int from);

}  // namespace kh
