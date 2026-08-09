#pragma once
// The dialogue box, drawn.  The other half of §M5.
//
// §M5 delivered the dialogue INTERPRETER and said so explicitly: "The
// interpreter owns a PAGE BUFFER of characters rather than writing tilemap
// cells... Separating them is what lets the whole of this file be tested with no
// renderer at all."  That was right, and it left a renderer to write.  This is
// it, and it is the last consumer in the tree of `Dialogue::row()`, which until
// now nothing but a test had ever read.
//
// IT IS A PORT AND NOT A DESIGN.  platform/snes/src/text.s already decided
// every number here -- DrawFrame's nine-patch, ClearTextArea's extent,
// DrawAdvanceMark's one cell, DrawOptions' cursor column and per-mode row --
// and platform/ds/include/constants.h already mirrors the geometry it used.
// The only thing this file gets to choose is what happens to the ONE number the
// DS cannot keep, and that is the box's top row: text.inc's BOX_ROW is 20 on a
// 28-row screen and the DS has 24 rows.  See BOX_TOP below.
//
// WHERE IT DRAWS.  vram_map.h's BOX_MAP, on BG3 of the MAIN engine -- over the
// world, not on the bottom screen, because "a line of dialogue belongs with the
// thing that is speaking".  Engine A's BG3 is the highest-numbered layer and so
// loses every priority tie, which is exactly why device/init.cpp gives it
// priority 0: it is in front of the sprites and in front of the ground.

#include <cstdint>

#include "constants.h"
#include "text.h"

namespace kh::device {

// BOX_MAP is 2 KiB, which is one 32x32-character block.  Both numbers are
// vram_map.h's and neither is a choice made here.
constexpr int BOX_COLS = 32;
constexpr int BOX_MAP_ROWS = 32;
constexpr int BOX_ENTRIES = BOX_COLS * BOX_MAP_ROWS;

// WHERE THE BOX SITS, and the one divergence in this file.
//
// text.inc: BOX_ROW = 20, on a 224-line screen -- 28 rows, so the seven-row box
// occupies rows 20..26 and leaves one row of margin below it.  The DS screen is
// 192 lines and 24 rows.  Carrying 20 across would leave the bottom three rows
// of the box off the screen, taking the advance mark and the whole of a yes/no
// prompt with them -- which is not a cosmetic difference, it is a prompt the
// player cannot see the answer to.
//
// So it is DERIVED from the screen rather than transcribed: the box sits on the
// bottom edge.  That gives 17 against the SNES's 20, and it loses the SNES's
// one row of margin, which is the honest consequence of a screen 32 lines
// shorter and is the same trade divergence 001 records for the camera.
constexpr int BOX_TOP = SCREEN_H / 8 - BOX_ROWS;
static_assert(BOX_TOP == 17, "the screen height or the box height moved");
static_assert(BOX_TOP + BOX_ROWS <= BOX_MAP_ROWS, "the box runs off its own map");
static_assert(TEXT_COL + TEXT_W <= BOX_COLS, "the text area runs off the row");

// DrawOptions, text.s:541.  The cursor goes here and the option's text starts
// one cell to its right, padded to OPT_W so that a longer choice above is
// erased rather than left showing its tail.
constexpr int OPT_COL = TEXT_COL + 16;
static_assert(OPT_COL + 1 + OPT_W <= BOX_COLS, "an option runs off the row");

// A character in the box's palette, packed as a DS map entry.
//
// The same translation device/hud.h's hudCell() makes and for the same reasons:
// the SNES's TXT_ATTR was a priority bit and a three-bit palette number, and a
// DS map entry has neither -- priority is per layer in BGxCNT and the palette
// field is four bits at 12..15.  Two functions rather than one because they are
// two different regions' character sets on two different engines, and the day
// one of them moves is the day sharing an implementation would be wrong.
uint16_t boxCell(uint8_t ch);

// The whole 32x32 map for this frame.
//
// EVERY ENTRY IS WRITTEN, including the ones outside the box, and the fill is
// CH_CLEAR -- "the one genuinely transparent cell" (text.inc), because every
// other glyph in this font carries an opaque background so that text can sit
// inside the window.  Filling with CH_BLANK instead would paint an opaque
// 256x256 rectangle over the top-left of the world and it would look exactly
// like a bank mapping fault.
//
// A closed box is therefore a map of transparent cells, not a stale one.  The
// SNES could leave BG3 alone between messages because it toggled the layer;
// here the layer stays on and the map is what changes, so "closed" has to be
// something drawn.
void buildBox(uint16_t* map, const Dialogue& d);

// The choices a prompt shows.  text.s:702-707's optYesNo and optRaft, as ASCII
// -- glyphOf() does the mapping, so this table is readable and the font
// numbering stays in one place.
//
// Exposed because it is the part with an oracle: `menuCount(mode)` already
// exists in text.h and says how many there are, and a table that disagreed with
// it would draw two of three raft names with no complaint from anything.
const char* optionText(TextMode mode, int i);

// Which map row an option is drawn on.  text.s:601: TEXT_ROW + menuRow + i, box
// relative, plus BOX_TOP to reach the map.  menuRow is PROMPT_ROW for a yes/no
// and MENU_ROW for the raft, which is what gives the three-way menu the extra
// line it needs above.
int optionRow(TextMode mode, int i);

}  // namespace kh::device
