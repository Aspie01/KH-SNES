// The two-screen initialisation.  Every value derived, none typed.
//
// GBATEK is quoted rather than cited throughout, for the reason vram_map.h
// gives about its own bank table: a register field programmed with a value the
// silicon reads differently does not fault, it draws a plausible wrong picture,
// and the transcription is what a reader checks against without leaving the
// file.

#include "init.h"

#include "constants.h"
// BEFORE vram_map.h on purpose: gen/assets.h defines KH_ASSETS_H_INCLUDED,
// which switches on vram_map.h's cross-file assertions -- the ones tying the
// sprite reservation to the pipeline's 1D boundary.  This translation unit
// depends on that boundary (the DISPCNT field below is derived from it), so
// it is exactly the place those checks should be live.
#include "gen/assets.h"
#include "vram_map.h"

namespace kh::device {
namespace {

using namespace kh::vram;

// ---------------------------------------------------------------------------
// POWCNT1 -- 4000304h, 16-bit.  GBATEK, "Power Control":
//
//   0     Enable Flag for both LCDs (0=Disable) (Prohibited, see notes)
//   1     2D Graphics Engine A       (0=Disable, 1=Enable)
//   2     3D Rendering Engine        (0=Disable, 1=Enable)
//   3     3D Geometry Engine         (0=Disable, 1=Enable)
//   9     2D Graphics Engine B       (0=Disable, 1=Enable)
//   15    Display Swap (0=Send Display A to Lower Screen, 1=Send A to Upper)
//
// THE 3D ENGINES STAY OFF.  Bits 2 and 3 belong to the 3D GroundRenderer, which
// is one of two alternatives and may never be the one selected; vram_map.h is
// explicit that the two renderers are disjoint and switching between them is a
// call rather than a remap.  Powering the geometry engine here would spend
// current and a command FIFO on a backend that has not been chosen, and -- worse
// for a reader -- would make it look as though this function had an opinion
// about which renderer runs.  It does not.
//
// BIT 15 IS THE ONE THAT IS NOT ARITHMETIC.  Engine A is the one with 3D, the
// bigger BG window and the sprites, so it is the world; engine B is the HUD,
// the command menu and the minimap.  The world goes on the top screen, which is
// what bit 15 says.  Everything else in this file follows from vram_map.h; this
// follows from the DS having two screens and the game having a front.
// ---------------------------------------------------------------------------
constexpr uint16_t POW_LCD = 1 << 0;
constexpr uint16_t POW_2D_A = 1 << 1;
constexpr uint16_t POW_2D_B = 1 << 9;
constexpr uint16_t POW_SWAP_A_TO_TOP = 1 << 15;

constexpr uint16_t POWCNT1_VALUE =
    uint16_t(POW_LCD | POW_2D_A | POW_2D_B | POW_SWAP_A_TO_TOP);
static_assert(POWCNT1_VALUE == 0x8203);

// ---------------------------------------------------------------------------
// DISPCNT -- 4000000h engine A, 4001000h engine B, 32-bit.  GBATEK:
//
//   0-2    BG Mode (0-6, 0-5 on engine B)
//   3      BG0 2D/3D Selection (engine A only)
//   4      Tile OBJ Mapping (0=2D, 1=1D)
//   7      Forced Blank
//   8-11   BG0..BG3 Enable
//   12     OBJ Enable
//   16-17  Display Mode (1 = graphics display)
//   20-21  Tile OBJ 1D-Boundary (0=32, 1=64, 2=128, 3=256 bytes)
//   24-26  Character Base   (engine A only)
//   27-29  Screen Base      (engine A only)
//
// MODE 0, because it is the only mode in which all four backgrounds are text
// layers, and every region vram_map.h reserves is a text layer's.  The rotation
// and affine modes buy nothing here: the SNES had no per-layer rotation to
// reproduce and the 3D ground -- the one thing that wants a transform -- gets it
// from the geometry engine and not from a BG mode.
//
// BITS 24-29 ARE ZERO, AND THAT IS THE POINT OF THE 62 KiB RULE.  They are
// engine A's DISPCNT-wide base terms, in 64 KiB units, added to every layer's
// BGxCNT base at once -- and engine B has no equivalent.  vram_map.h keeps
// every region below BASE_REACH so both stay zero, which is what lets the two
// engines share one piece of arithmetic.  Writing anything else here would
// silently invalidate every base computed below, for all four layers together.
// The assertion is in vram_map.h; this is the consumer that depends on it.
// ---------------------------------------------------------------------------
constexpr uint32_t DISP_MODE0 = 0;
constexpr uint32_t DISP_OBJ_1D = 1u << 4;
constexpr uint32_t DISP_FORCED_BLANK = 1u << 7;
constexpr uint32_t DISP_BG_ENABLE = 1u << 8;        // <<layer
constexpr uint32_t DISP_OBJ_ENABLE = 1u << 12;
constexpr uint32_t DISP_GRAPHICS = 1u << 16;        // display mode 1
constexpr int DISP_OBJ_BOUNDARY_SHIFT = 20;

// The 1D boundary field, derived from the byte count the pipeline encodes
// against rather than named twice.  gen/assets.h's OBJ_BOUNDARY is 32 today and
// the recovery path in vram_map.h raises it to 64; both are expressible, and
// the ladder is 32/64/128/256 for field values 0/1/2/3.
constexpr uint32_t objBoundaryField(int bytes) {
    return bytes == 32 ? 0u : bytes == 64 ? 1u
         : bytes == 128 ? 2u : bytes == 256 ? 3u : 0xFFFFFFFFu;
}

constexpr uint32_t bgBit(Layer l) { return DISP_BG_ENABLE << unsigned(l); }

// Engine A: the ground, the overlay, the box, and the sprites.  BG2 is NOT
// enabled -- vram_map.h holds it back as the whole margin, because with 3D on
// there are only three tilemap layers and one in reserve is the difference
// between a later task having somewhere to go and a later task reopening a
// frozen file.
constexpr uint32_t DISPCNT_MAIN_VALUE =
    DISP_MODE0 | DISP_OBJ_1D | DISP_GRAPHICS | DISP_OBJ_ENABLE
    | bgBit(MAIN_GROUND_LAYER) | bgBit(MAIN_OVERLAY_LAYER) | bgBit(MAIN_BOX_LAYER);

// Engine B: the HUD, the command menu, the minimap.  BG3 is the margin here,
// for the same reason and with more of it -- engine B has no 3D, so all four of
// its layers are tilemaps.
//
// NO BOUNDARY FIELD IS SET FOR ENGINE B, and that is not an omission being
// deferred: the sub OBJ window is bank I, 16 KiB, and vram_map.h's SUB_OBJ_TILES
// records that the BANK binds there rather than the reach.  Whatever the bottom
// screen's sprites turn out to be, they fit inside tile numbers 0..511 at
// boundary 32, which is field 0, which is what a zeroed field already says.
constexpr uint32_t DISPCNT_SUB_VALUE =
    DISP_MODE0 | DISP_OBJ_1D | DISP_GRAPHICS | DISP_OBJ_ENABLE
    | bgBit(SUB_HUD_LAYER) | bgBit(SUB_MENU_LAYER) | bgBit(SUB_MINIMAP_LAYER);

// ---------------------------------------------------------------------------
// BGxCNT -- 4000008h + layer*2 (engine A), 4001008h + layer*2 (engine B).
// 16-bit, and laid out IDENTICALLY on both engines.  GBATEK:
//
//   0-1    BG Priority (0 = highest, drawn in front)
//   2-5    Character Base Block (0-15, 16 KiB steps)
//   6      Mosaic
//   7      Colours (0 = 16 colours/16 palettes, 1 = 256/1)
//   8-12   Screen Base Block (0-31, 2 KiB steps)
//   13     BG0/BG1 ext palette slot; BG2/BG3 display area overflow
//   14-15  Screen Size (text: 0=256x256, 1=512x256, 2=256x512, 3=512x512)
//
// BIT 7 IS ZERO ON EVERY LAYER.  The pipeline emits 4bpp characters and 16-entry
// sub-palettes (docs/DS_FORMATS.md), and a map entry's top four bits are the
// sub-palette number -- which only exist in 16-colour mode.  Setting bit 7 would
// not merely change the depth, it would make every map entry's palette nibble
// part of nothing and draw the whole scene out of one 256-colour table.
// ---------------------------------------------------------------------------
constexpr int BG_PRIORITY_MASK = 3;
constexpr int BG_CHAR_BASE_SHIFT = 2;
constexpr int BG_MAP_BASE_SHIFT = 8;
constexpr int BG_SIZE_SHIFT = 14;

// Text-layer screen sizes, by the map's own extent in characters.
constexpr uint16_t BG_SIZE_256 = 0;
constexpr uint16_t BG_SIZE_512x512 = 3;

constexpr uint16_t bgcntValue(const Region& chars, const Region& map,
                              int priority, uint16_t size) {
    return uint16_t(uint16_t(priority & BG_PRIORITY_MASK)
                    | uint16_t(chars.charBase() << BG_CHAR_BASE_SHIFT)
                    | uint16_t(map.mapBase() << BG_MAP_BASE_SHIFT)
                    | uint16_t(size << BG_SIZE_SHIFT));
}

// PRIORITIES, and the one DS rule that has no SNES counterpart.
//
// On the SNES priority was per TILE; here it is per LAYER, and when two layers
// share a priority the LOWER-NUMBERED BG wins.  That second clause is why the
// box can sit on BG3 -- the highest-numbered layer, which loses every tie -- and
// still be in front of everything: it is given priority 0 and nothing else is.
//
// The ground takes 3, the lowest, because everything in this game is drawn over
// the ground and nothing is drawn under it.
constexpr int PRI_BOX = 0;          // over the world and over the sprites
constexpr int PRI_OVERLAY = 1;
constexpr int PRI_GROUND = 3;
// The bottom screen's three panels do not overlap today.  The order is stated
// anyway, because "they do not overlap" is a fact about art that has not been
// drawn, and the status readout is the one that must never be occluded.
constexpr int PRI_HUD = 0;
constexpr int PRI_MENU = 1;
constexpr int PRI_MINIMAP = 2;

// GROUND_MAP is the 64x64-character streaming window, which is 512x512 pixels
// and the only layer here that is not a single 32x32 block.  Everything else is
// one 2 KiB block: a dialogue box, an overlay and three bottom-screen panels
// are all screen-sized or smaller and none of them scrolls.
constexpr uint16_t BGCNT_GROUND =
    bgcntValue(GROUND_CHR, GROUND_MAP, PRI_GROUND, BG_SIZE_512x512);
constexpr uint16_t BGCNT_OVERLAY =
    bgcntValue(UI_CHR, OVERLAY_MAP, PRI_OVERLAY, BG_SIZE_256);
constexpr uint16_t BGCNT_BOX =
    bgcntValue(UI_CHR, BOX_MAP, PRI_BOX, BG_SIZE_256);
constexpr uint16_t BGCNT_HUD =
    bgcntValue(SUB_CHR, HUD_MAP, PRI_HUD, BG_SIZE_256);
constexpr uint16_t BGCNT_MENU =
    bgcntValue(SUB_CHR, MENU_MAP, PRI_MENU, BG_SIZE_256);
constexpr uint16_t BGCNT_MINIMAP =
    bgcntValue(SUB_CHR, MINIMAP_MAP, PRI_MINIMAP, BG_SIZE_256);

// The derivation, pinned to the numbers a human can check by hand against the
// tables above.  These are the only literals in the file that are not quoted
// from GBATEK, and they exist so that a change to a Region in vram_map.h -- a
// file that is frozen, but additively edited three times now -- cannot silently
// re-point a layer.
static_assert(BGCNT_GROUND == 0xD803);      // pri 3, chr 0, map 24, 512x512
static_assert(BGCNT_OVERLAY == 0x1D09);     // pri 1, chr 2, map 29, 256x256
static_assert(BGCNT_BOX == 0x1C08);         // pri 0, chr 2, map 28, 256x256
static_assert(BGCNT_HUD == 0x0800);         // pri 0, chr 0, map 8
static_assert(BGCNT_MENU == 0x0901);        // pri 1, chr 0, map 9
static_assert(BGCNT_MINIMAP == 0x0A02);     // pri 2, chr 0, map 10

// The two engines must not be given the same base twice by accident: engine A's
// box and overlay share UI_CHR deliberately (one font, one region) but must not
// share a MAP.
static_assert(BOX_MAP.mapBase() != OVERLAY_MAP.mapBase());
static_assert(HUD_MAP.mapBase() != MENU_MAP.mapBase()
              && MENU_MAP.mapBase() != MINIMAP_MAP.mapBase());

}  // namespace

uint16_t powcnt1Value() { return POWCNT1_VALUE; }
uint32_t dispcntMainValue() {
    return DISPCNT_MAIN_VALUE
           | (objBoundaryField(OBJ_BOUNDARY) << DISP_OBJ_BOUNDARY_SHIFT);
}
uint32_t dispcntSubValue() { return DISPCNT_SUB_VALUE; }

uint16_t bgcntMainValue(int layer) {
    if (layer == int(MAIN_GROUND_LAYER)) return BGCNT_GROUND;
    if (layer == int(MAIN_OVERLAY_LAYER)) return BGCNT_OVERLAY;
    if (layer == int(MAIN_BOX_LAYER)) return BGCNT_BOX;
    return 0;
}

uint16_t bgcntSubValue(int layer) {
    if (layer == int(SUB_HUD_LAYER)) return BGCNT_HUD;
    if (layer == int(SUB_MENU_LAYER)) return BGCNT_MENU;
    if (layer == int(SUB_MINIMAP_LAYER)) return BGCNT_MINIMAP;
    return 0;
}

void initScreens(Mmio& io) {
    // 1. FORCED BLANK FIRST, on both engines, before anything is pointed
    //    anywhere.  Between here and the end of this function every layer is
    //    aimed at VRAM that has not been written yet, and without this the
    //    display controller spends those scanlines drawing whatever the banks
    //    powered on holding.  It is a few frames of noise, it happens once, and
    //    it is the kind of thing that gets diagnosed as a bad bank mapping.
    io.write32(DISPCNT_MAIN, DISP_FORCED_BLANK);
    io.write32(DISPCNT_SUB, DISP_FORCED_BLANK);

    // 2. Power.  After forced blank so the LCDs come up already blanked, and
    //    before the banks because a bank cannot be mapped into an engine that
    //    is not running.
    io.write16(POWCNT1, POWCNT1_VALUE);

    // 3. THE NINE BANKS, ONE BYTE AT A TIME, EACH TO ITS OWN ADDRESS.
    //
    //    Both halves of that sentence are load-bearing and both are the same
    //    hazard from different sides.  VRAMCNT is an EIGHT-BIT register, so a
    //    16-bit store to 0x04000240 configures bank A and bank B together --
    //    and the natural way to write this loop, over a uint16_t or with a
    //    memcpy, would set four banks correctly and clobber four others with
    //    the high halves of their neighbours' bytes.
    //
    //    And the addresses are NOT CONSECUTIVE: 0x04000247 is WRAMCNT, between
    //    VRAMCNT_G and VRAMCNT_H, and writing it repartitions the 32 KiB the
    //    ARM9 and ARM7 share.  Bank H's byte is 0x81, which as a WRAMCNT value
    //    hands the ARM9's first 16 KiB to the ARM7 while the ARM9 is using it.
    //
    //    Both are why this iterates over vram_map.h's VRAMCNT_ADDR rather than
    //    over an index -- the gap is in the table, asserted there by
    //    vramcntAddressesSkipWramcnt(), and a loop that adds one cannot see it.
    for (unsigned b = 0; b < unsigned(Bank::Count); ++b) {
        const Bank bank = Bank(b);
        io.write8(vramcntAddr(bank), vramcnt(bank));
    }

    // 4. The layers.  Bases and sizes only; nothing here decides what is IN
    //    them.  Written before the engines leave forced blank so that no frame
    //    is ever composited from a half-configured set of layers.
    io.write16(bgcnt(BGCNT_MAIN, int(MAIN_GROUND_LAYER)), BGCNT_GROUND);
    io.write16(bgcnt(BGCNT_MAIN, int(MAIN_OVERLAY_LAYER)), BGCNT_OVERLAY);
    io.write16(bgcnt(BGCNT_MAIN, int(MAIN_BOX_LAYER)), BGCNT_BOX);
    io.write16(bgcnt(BGCNT_SUB, int(SUB_HUD_LAYER)), BGCNT_HUD);
    io.write16(bgcnt(BGCNT_SUB, int(SUB_MENU_LAYER)), BGCNT_MENU);
    io.write16(bgcnt(BGCNT_SUB, int(SUB_MINIMAP_LAYER)), BGCNT_MINIMAP);

    // 5. ...and only now let the display controllers out.  The forced-blank bit
    //    is absent from both of these values, which is what clears it.
    io.write32(DISPCNT_MAIN, dispcntMainValue());
    io.write32(DISPCNT_SUB, dispcntSubValue());
}

}  // namespace kh::device
