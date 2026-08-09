#pragma once
// §M7 step three: the depth sort, and which sprites get an OAM slot.
//
// THIS IS A PORT OF platform/snes/src/oam.s AND NOT A DESIGN.  That file's own
// header says what the whole thing is for: "the ground is a background layer, so
// everything that stands up off it -- Sora, Heartless, palms, rocks -- is a
// sprite, and sprites are drawn strictly in OAM order: slot 0 is frontmost.
// Seen from three-quarters overhead, being further down the screen means being
// nearer, so sorting actors by world Y descending and writing them out in that
// order makes a character walk behind a palm when north of it and in front of it
// when south, with no per-object layer authoring at all."
//
// Every rule below is cited to the line that establishes it.  Unlike the ground
// streamer -- which had no oracle, because the SNES never streamed a 1024-pixel
// map -- this has one, and the SNES is right by definition.  The DS's job is to
// agree with it, and the four places where it cannot are named where they occur.
//
// WHAT IS HERE.  The depth logic: who is in front of whom, who is on screen at
// all, who gets one of the 128 slots, and where on the screen each one goes.
//
// WHAT IS NOT.  The attribute bytes -- attr0/attr1/attr2, the tile number, the
// palette and the flip bits.  Those are a mechanical packing of what this
// produces, they need gen/assets.h's dsTileFor() and the cel tables, and mixing
// them in here would bury the one part of sprite handling that is a BEHAVIOUR
// the oracle can adjudicate. `SpriteSlot` below is the boundary, and it carries
// everything the packing needs.

#include <cstdint>

#include "actor.h"
#include "grid.h"

namespace kh::device {

// The hardware ceiling, per engine.  vram_map.h owns the number; this is the
// consumer that runs into it, and on the DS it can be reached in a way the SNES
// could never manage -- divergence 002 gives the pool 128 slots where the oracle
// had 32, so a busy island frame has four times as many candidates for the same
// 128 entries.
constexpr int OAM_SLOTS = 128;

// One sprite the frame wants drawn, in the order it must be written.
//
// `slot` is implicit -- it is the index in the array -- because that IS the
// depth: "sprites are drawn strictly in OAM order: slot 0 is frontmost"
// (oam.s:6).  A SpriteSlot that carried its own priority would be inviting
// somebody to sort them again.
enum class SpriteKind : uint8_t {
    Actor,          // one ordinary actor, 16x16 or 32x32
    BossQuadrant,   // one quarter of a 64x64 boss; four of these, in order
    Shadow,         // the flattened blob under an actor
};

struct SpriteSlot {
    int16_t x;              // screen pixels, top-left of the sprite
    int16_t y;
    uint8_t actor;          // which actor it belongs to
    SpriteKind kind;
    uint8_t quadrant;       // 0-3 for BossQuadrant, else 0
    bool large;             // 32x32 rather than 16x16
};

// Why a frame stopped short of everything it wanted.  Reported rather than
// silent, because the SNES's `cmp #128 / bcs @done` (oam.s:161, :190, :308)
// drops the tail of the sort ON PURPOSE and a port that did it quietly would
// lose actors on exactly the busiest frames -- the ones nobody screenshots.
struct OamBuild {
    int used = 0;               // slots filled
    int actorsSorted = 0;       // live actors the sort saw
    int culled = 0;             // off screen, so free
    int droppedToCeiling = 0;   // wanted a slot, there were none left
};

// The sort: live actors by world Y DESCENDING, frontmost first.
//
// STABLE, and that is not a stylistic choice.  oam.s:132's `cmp tmp0 / bcs
// @place` stops shifting as soon as the actor already in the slot is at a Y at
// least as large, so an actor with an EQUAL Y keeps the earlier position -- and
// since BuildSortList scans slots 0..MAX_ACTORS-1 in order (oam.s:82-95), the
// tie-break is the actor's own slot number, ascending.
//
// Two actors on the same row is not a corner case, it is the island's palm rows
// and the Second District's wave arriving on one line.  std::sort would be free
// to swap them, differently on different frames, and the result is two sprites
// that flicker past each other while standing still.
//
// Returns how many were written into `out`, which must hold MAX_ACTORS.
int buildSortList(const Actors& a, uint8_t* out);

// The whole table for one frame, in OAM order: every actor front to back, then
// the boss, then every shadow.
//
// THE ORDER OF THE THREE PASSES IS THE DEPTH RULE, and each is cited:
//   actors   oam.s:148  in sorted order
//   boss     oam.s:213  "emitted after every sorted actor rather than inside
//                        the sort, so it always lands in higher OAM slots and
//                        therefore behind them.  That is the right answer
//                        nearly always: it towers over Sora and he fights at
//                        its feet."
//   shadows  oam.s:12   "emitted after every actor, so they land in higher OAM
//                        slots and therefore behind all of them, while still
//                        sitting above BG1."
//
// `out` must hold OAM_SLOTS entries.
OamBuild buildOam(const Actors& a, const Camera& cam, SpriteSlot* out);

}  // namespace kh::device
