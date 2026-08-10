#pragma once
// The four SceneActions that are ROUTINES rather than scene loads.
//
// WHY THIS FILE EXISTS, and it is the same reason moteOffset() is in stage.h
// rather than in whoever calls it.  A stage machine returns a StageStep and
// somebody performs it; there were exactly one performer for a long time --
// perform() in platform/ds/host/trace_main.cpp -- and §M7's device tier is the
// second.  Four of the actions are not "load a scene and spawn its cast" but
// pieces of the SNES's own code with citations attached:
//
//     RaiseArmor       town.s:866      the armour comes down, with its hands
//     SweepGauntlets   town.s:826-834  ...and they go with it
//     BeginFall        dive.s:439-465  three of Sora's bytes, and no more
//     SpawnMote        dive.s:494-550  one speck of light off the spread
//
// Two copies of those, in two performers, is the shape of a bug that cannot be
// caught: the oracle diff only ever runs the trace emitter's copy, so the
// device's copy could drift arbitrarily far and every scenario would stay
// green.  That is this project's recurring defect -- something exists and
// nothing checks it -- with the added twist that here the CHECK exists and is
// simply pointed at the wrong object.
//
// So there is one copy, it lives in the simulation tier where both performers
// can reach it, and tools/trace_check.py exercises it eight times per run.
//
// WHAT IS NOT HERE.  Anything that loads a scene.  EnterStation2, EnterIsland,
// EnterDistrict and the rest need an asset source, and the two performers get
// their bytes from genuinely different places -- a file on the host, a linked
// symbol on the device.  That difference is §M2's whole seam and it stays where
// it is; these four touch nothing but the actor table.

#include "grid.h"
#include "stage.h"
#include "world.h"

namespace kh {

// RaiseArmor, town.s:866.  It comes down in the middle of the square, with its
// hands -- and it MOVES SORA FIRST, to tile (16,10), which is the staging: the
// armour lands two tiles above him, between him and the door he came in by.
// PlaceSora also clears his velocity, faces him south and resets his state and
// timer, so a player caught mid-swing by the retry is put down standing.
//
// False means the actor pool refused a row, which is a performer's problem and
// not a scene's: the armour with one hand is worse than no armour at all.
bool raiseArmor(SceneView& view, WorldState& world);

// SweepGauntlets, town.s:826-834.  Its hands go with it, and ONLY its hands:
// the scan is for ACT_GAUNTLET alone, so the district's bystanders stay.
//
// It does NOT open the step's script -- the passenger is the performer's to
// carry, because "do the action, then open `script` if it is set" is a rule
// about every action and not about this one.  See stage.h's block comment.
void sweepGauntlets(SceneView& view, WorldState& world, ScreenFx& fx);

// BeginFall, dive.s:439-465.  Only the three player bytes.  The stage byte, the
// fall timer, shakeX, mosaicAmt and BG1 are the MACHINE's -- DiveStage::Done
// wrote all five before this is called, and writing them again here would be a
// second author for one number.
//
// It does NOT clear his velocity and it does NOT change his facing.  UpdateSora's
// ST_FALL branch does both, on this same frame, AFTER SceneUpdate --
// world.s:456-471.  That ordering is the whole reason the oracle's frame 2
// already reads pdir 1 and not 0, and a port that faced him here as well would
// turn him twice on the opening frame.
bool beginFall(SceneView& view);

// SpawnMote, dive.s:494-550.  One speck of light, off to one side of Sora and
// below the bottom of the screen, travelling up.
//
// TRUE ON A FULL POOL, deliberately.  dive.s:534's `bcc @out` just loses the
// mote, and an action the ROM performs by doing nothing has been performed --
// refusing here would diverge from the oracle rather than follow it.
//
// view.frame IS THE ONLY EXTERNAL INPUT THE FALL HAS: moteOffset() indexes the
// eight-way spread with it.  An off-by-one does not fail loudly -- every mote
// comes off the neighbouring slot and lands 200 to 3400 raw units away, for
// ever, with no bound violated and no assertion to trip.
bool spawnMote(SceneView& view);

// ---------------------------------------------------------------------------
// The night's three actor beats
//
// TakeRiku (night.s:534-598), LoseKairi (night.s:692-730), GiveKeyblade
// (night.s:604-628) and OpenTheDoor (night.s:670-688).  Four routines, three
// functions: the two columns are the same first pass with a different ActType,
// and saying so once is what stops them drifting.
// ---------------------------------------------------------------------------

// The column of darkness comes up where somebody was standing.
//
// THE ORDER IS OBSERVABLE AND IS THEREFORE THE SNES'S.  night.s:551-568 reads
// the position, THEN clears the type, THEN spawns -- and Actors::spawn takes the
// first free slot scanning from zero, exactly as SpawnActor does, so the column
// lands in the slot the person just vacated.  Spawning before the clear would
// put it in the next free slot instead and shift every actor after it, which is
// a trace diff in every column from that frame on.
//
// FALSE MEANS THE SPAWN WAS REFUSED, and that is a report rather than a
// divergence.  night.s ignores SpawnActor's carry, so on a full table the ROM
// deletes Riku and stands nothing in his place; this returns false so the
// bottom screen says so.  The state is identical either way -- the difference is
// only whether anybody is told -- and it cannot be reached on a 128-slot pool
// with the night's seventeen actors and six Shadows anyway.
//
// A MISSING `who` IS TRUE, NOT FALSE.  night.s:553-560's scan falls out to `rts`
// when nobody matches, having already spent the timer, so an absent Riku is a
// beat the ROM performed by doing nothing -- the same reading spawnMote() takes
// of dive.s:534, and refusing here would diverge from the oracle rather than
// follow it.
bool standColumn(SceneView& view, ActType who);

// GiveKeyblade's first half, night.s:610-620: the dark thins out.  Every column,
// with no early exit -- both of them are up by the time the Keyblade arrives if
// the player has been quick, and clearing one would leave the other standing for
// the rest of the night.
void clearColumns(SceneView& view);

// OpenTheDoor, night.s:674-686.  The door on the wall is RETYPED and not
// replaced: a second actor would leave the old one standing behind it, and
// ActType::DoorOpen falls outside the examinable range so it can no longer be
// looked at.  The SNES writes actTile as well; here tileFor() derives it from
// the type, so setting the type is setting the tile.
//
// FALSE MEANS THERE WAS NO DOOR TO OPEN.  The ROM's scan simply finds nothing,
// which is state-identical -- but the night's whole last beat is this door, and
// a cast file that lost it would otherwise present as Kairi's line playing over
// a wall that never opens.  The oracle's night always has one, so false cannot
// diverge from it.
bool openTheDoor(SceneView& view);

}  // namespace kh
