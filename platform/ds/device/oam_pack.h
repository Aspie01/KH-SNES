#pragma once
// §M7 step three, second half: turning a SpriteSlot into three OAM halfwords.
//
// device/oam.cpp decided WHO is drawn, in what order, and where. This decides
// what the hardware is actually told. It is the last mechanical step of the
// sprite path and the first one where the SNES's answer cannot simply be
// copied -- the two machines lay an OAM entry out differently enough that this
// is a translation rather than a transcription, and the four places they part
// company are named below.
//
// THE SNES ENTRY, from oam.s's WriteOamRaw (oam.s:604-687), is four bytes plus
// two bits in a separate high table:
//
//     byte 0   X, low 8 bits
//     byte 1   Y
//     byte 2   tile, "all our sprite tiles live below $100"
//     byte 3   v h oo ppp N   -- vflip, hflip, priority, palette, name page
//     high     X's sign bit and the size bit, two bits per sprite
//
// THE DS ENTRY is three halfwords and no high table. Four differences, each of
// which changes what the port must store rather than merely where:
//
//   1. THERE IS NO HIGH TABLE. X is nine bits inside attr1 and the size is two
//      more, so the whole `oamHigh` apparatus -- the shift-by-slot, the
//      read-modify-write, the 32 extra bytes DMAd every frame -- simply goes
//      away. This is the one place the DS is straightforwardly simpler.
//   2. THERE IS NO NAME-PAGE BIT. The SNES reached its second 256-tile page
//      through bit 0 of the attribute byte (game.inc:101); a DS tile number is
//      ten flat bits, and gen/assets.h has already re-serialised every page
//      cel-contiguous. So AF_PAGE1 stops being a bit that is WRITTEN and
//      becomes a bit that SELECTS A BASE -- see PAGE1_TILE below.
//   3. THE PALETTE FIELD IS FOUR BITS, not three. The SNES had eight OBJ
//      sub-palettes and the DS has sixteen (vram_map.h's PAL_SUBPALETTES), so
//      every id in constants.h's `pal` namespace fits with room over. Nothing
//      here uses the extra eight, and that is the correct amount of use for
//      them until something needs one.
//   4. Y IS EIGHT BITS AND X IS NINE, on both machines, and the wrap is load
//      bearing rather than defensive -- see the note on packSprite().

#include <cstdint>

#include "actor.h"
#include "oam.h"

namespace kh::device {

// One hardware sprite. Three halfwords, in the order OAM holds them; the
// fourth is the affine matrix interleave vram_map.h documents, and nothing
// here writes it because nothing here rotates.
struct OamEntry {
    uint16_t attr0;
    uint16_t attr1;
    uint16_t attr2;
};

// GBATEK, "OAM - OBJ Attributes":
//   attr0  0-7 Y, 8 rot/scale, 9 disable, 10-11 mode, 12 mosaic,
//          13 colours (0 = 16/16), 14-15 shape
//   attr1  0-8 X, 12 H flip, 13 V flip, 14-15 size
//   attr2  0-9 tile, 10-11 priority, 12-15 palette
constexpr uint16_t OBJ_DISABLE = 1u << 9;
constexpr int OBJ_SHAPE_SHIFT = 14;
constexpr int OBJ_HFLIP_BIT = 12;
constexpr int OBJ_SIZE_SHIFT = 14;
constexpr int OBJ_PRIORITY_SHIFT = 10;
constexpr int OBJ_PALETTE_SHIFT = 12;

// Shape 0 is square; size 1 is 16x16 and size 2 is 32x32. Those are the only
// two this game has -- every actor is one or the other, and AF_LARGE is the
// whole of the distinction (oam.s:434-449).
constexpr uint16_t OBJ_SQUARE = 0;
constexpr uint16_t OBJ_SIZE_16 = 1;
constexpr uint16_t OBJ_SIZE_32 = 2;

// Priority 2, and it is not an arbitrary middle. oam.s:625 writes `ora #$20` --
// "priority 2: above BG1, below the BG3 HUD" -- and the DS's layers are set up
// to preserve the relation: device/init.cpp gives the ground priority 3, the
// overlay 1 and the box 0, and OBJ wins a tie against a background. So 2 puts
// every sprite over the ground and under both the overlay and the box, which is
// the SNES's arrangement with the numbers the DS's four layers need.
constexpr uint16_t OBJ_PRIORITY = 2;

// WHERE EACH PAGE LANDS IN OBJECT VRAM, in tile numbers.
//
// The resident set is Sora's sheet then two 8 KiB object pages
// (gen/assets.h's SPRITE_ASSETS and OBJ_RESIDENT_BYTES), and a tile number
// counts in units of the 1D boundary. So the bases are the running byte total
// divided by the boundary -- derived, because typing 480 and 736 would be two
// numbers that stop being right the moment a page is resized and nothing would
// say so. test_oam_pack.cpp checks these against SPRITE_ASSETS by name.
constexpr int SORA_SHEET_BYTES = 15360;     // 30 cels of 32x32 at 4bpp
constexpr int OBJ_PAGE_BYTES = 8192;        // 16 cels

// The second page is SHARED: the town overwrites the islanders rather than
// joining them (gen/assets.h's note, main.s:540). So AF_PAGE1 means "the other
// page", not "the islanders" -- which page is resident is a scene decision and
// not a sprite one, and this file is deliberately unable to tell.
uint16_t soraTile(int cel);
uint16_t pageTile(uint8_t snesTile, bool page1);

// One entry, packed.
//
// `soraCel` is what world.cpp's updateSoraFrame() returned this frame. That
// function computes the cel and the mirror bit and its caller discards the cel
// with a comment saying the rest "is the device tier's half, because
// soraFrameCur is a fact about VRAM and not about the world" (world.cpp:900-905).
// This is that half: the first consumer the return value has ever had.
//
// `frameCount` drives the damage flash, and it is a parameter rather than a
// global because the parity is real behaviour: oam.s:290-296 flashes on
// `frameCount & 2` while actHitT is non-zero, and §M6 records that a swallowed
// NMI flips that parity permanently.
//
// THE COORDINATE WRAP IS THE BEHAVIOUR, NOT A GUARD. Y is eight bits and X is
// nine, so a sprite at y = -16 is stored as 240 and the hardware draws rows
// 240..255 (off a 192-line screen) and then 0..15 (visible at the top) -- which
// is exactly right, and is why the cull can accept anything from -32 without a
// second thought. Masking is what makes a partially-off-screen sprite work; a
// clamp would pin it to the edge instead.
OamEntry packSprite(const SpriteSlot& s, const Actors& a, int player,
                    int soraCel, uint32_t frameCount);

// The whole table, plus the entries that hide the slots nobody used.
//
// EVERY UNUSED SLOT IS HIDDEN EXPLICITLY, which oam.s does first rather than
// last (ClearOamBuffer, oam.s:31-51, called at the top of BuildOam). Leaving
// them is not "leaving them blank": OAM holds whatever the previous frame put
// there, so a frame with fewer sprites than the last would show the tail of the
// last one, frozen, until something overwrote it.
void packAll(const SpriteSlot* slots, int n, const Actors& a, int player,
             int soraCel, uint32_t frameCount, OamEntry* out);

}  // namespace kh::device
