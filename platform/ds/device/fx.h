#pragma once
// ScreenFx, applied.  The six fields the stage machines write, turned into the
// five registers the DS reads.
//
// WHY THIS IS A FILE AND NOT FIVE LINES IN main().  stage.h states the rule
// this implements: "effects that contend for one global register must be
// arbitrated in one place per frame, not written where they are decided."
// ScreenFx is that one place on the deciding side; this is the one place on the
// applying side.  Putting it in the frame loop would mean the arbitration lived
// in a file no host test can compile, which is exactly the half of a rule that
// stops being followed first.
//
// THE ONE REAL PROBLEM THIS FILE HAD TO SOLVE, and it is worth stating because
// the obvious answer is wrong.  The SNES had TWO independent units:
//
//     INIDISP  screen brightness, 0..15        -> ScreenFx::brightness
//     CGADSUB  the fixed-colour fade to white  -> ScreenFx::whiteout
//
// and the game uses both AT THE SAME TIME.  The night's lightning writes
// whiteout while the Tear is dragging brightness down (stage_night.cpp:42 and
// :119); the Dive's fade writes whiteout 31 with brightness still at 15
// (stage_dive.cpp:154).  The DS's MASTER_BRIGHT is ONE unit with a mode bit --
// it fades up OR down, never both -- so mapping both fields onto it would make
// one of the two silently win, and which one would depend on the order of two
// lines in this file.
//
// So brightness takes MASTER_BRIGHT and whiteout takes the BLEND unit:
// BLDCNT/BLDY in brightness-increase mode over every layer plus the backdrop.
// Two units for two fields, which is the arrangement the SNES had, and neither
// beat can eat the other.  That is the only non-mechanical decision here and it
// is why this header is longer than the code.
//
// NOTHING HERE IS AN OPINION ABOUT HOW IT SHOULD LOOK.  Every value is a
// transcription of a GBATEK field plus the scaling that maps a 0..15 or 0..31
// range onto a 0..16 one, endpoints exact.  What it will actually look like on
// a screen is the one thing this cannot tell you.

#include <cstdint>

#include "mmio.h"
#include "stage.h"

namespace kh::device {

// GBATEK's I/O map, engine A.  Engine B has its own MASTER_BRIGHT at +0x1000
// and its own blend unit; neither is written here, because every effect in
// ScreenFx is about the WORLD and the world is engine A.  A HUD that dimmed
// with the scene would be a status readout you cannot read during the fade.
constexpr uint32_t MOSAIC_MAIN = 0x0400004C;        // 16-bit
constexpr uint32_t BLDCNT_MAIN = 0x04000050;        // 16-bit
constexpr uint32_t BLDY_MAIN = 0x04000054;          // 16-bit
constexpr uint32_t MASTER_BRIGHT_MAIN = 0x0400006C; // 16-bit

// MASTER_BRIGHT, GBATEK "Master Brightness Up/Down":
//   0-4    Factor (0..16, values above 16 behave as 16)
//   14-15  Mode (0 = disable, 1 = up toward white, 2 = down toward black)
constexpr int BRIGHT_MODE_SHIFT = 14;
constexpr uint16_t BRIGHT_MODE_NONE = 0;
constexpr uint16_t BRIGHT_MODE_UP = 1;
constexpr uint16_t BRIGHT_MODE_DOWN = 2;
constexpr int BRIGHT_FACTOR_MAX = 16;

// BLDCNT, GBATEK "Color Special Effects Selection":
//   0-5    1st target: BG0, BG1, BG2, BG3, OBJ, backdrop
//   6-7    effect (0 none, 1 alpha, 2 brightness increase, 3 decrease)
constexpr uint16_t BLD_TARGET_ALL = 0x003F;         // all four BGs, OBJ, backdrop
constexpr int BLD_EFFECT_SHIFT = 6;
constexpr uint16_t BLD_EFFECT_NONE = 0;
constexpr uint16_t BLD_EFFECT_BRIGHTEN = 2;

// ScreenFx::brightness is 15 for full and 0 for black, which is INIDISP's sense
// inverted -- see constants.h.  MASTER_BRIGHT counts the other way, so this is a
// reflection and a rescale in one, with both endpoints exact: 15 gives a
// disabled unit and 0 gives factor 16, which is full black.
uint16_t masterBrightValue(const ScreenFx& fx);

// ...and whiteout, 0..31, onto the blend unit.  Zero disables the effect rather
// than selecting it at strength zero: a blend unit armed over every layer costs
// nothing to look at but is a live global that the next person to want alpha
// would find already claimed.
uint16_t bldcntValue(const ScreenFx& fx);
uint16_t bldyValue(const ScreenFx& fx);

// MOSAIC, GBATEK: bits 0-3 BG H size, 4-7 BG V size, 8-11 OBJ H, 12-15 OBJ V,
// each "0 = 1 dot .. 15 = 16 dots" -- which is the SNES's $2106 encoding
// exactly, so mosaicAmt goes across unchanged.
//
// THE OBJECT FIELDS STAY ZERO.  The SNES's MOSAIC register had per-BG enables
// and no object bits at all: the Shatter and the Tear coarsen the GROUND, and
// Sora stays sharp while the floor he is standing on comes apart.  Mosaicking
// the sprites too would be a different effect that happens to share a name.
uint16_t mosaicValue(const ScreenFx& fx);

// DISPCNT for this frame: the initialisation's value, plus forced blank, minus
// the ground layer when the fall turns it off.
//
// `base` is device/init.cpp's dispcntMainValue() and is passed in rather than
// called, because this must not become a second place that decides which layers
// exist.  fx can subtract a layer; it can never add one.
uint32_t dispcntWith(uint32_t base, const ScreenFx& fx);

// All five, in one place, once a frame.
void applyFx(Mmio& io, uint32_t dispcntBase, const ScreenFx& fx);

}  // namespace kh::device
