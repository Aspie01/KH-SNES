#pragma once
// §M7 step four: the bottom screen, starting with the half that has an oracle.
//
// THE BOTTOM SCREEN IS THREE THINGS AND ONLY ONE OF THEM IS A PORT.
// vram_map.h reserves HUD_MAP, MENU_MAP and MINIMAP_MAP, and device/init.cpp
// points BG0, BG1 and BG2 of engine B at them. Of the three:
//
//   the HUD      IS a port. platform/snes/src/hud.s is 598 lines that decided
//                what it shows and where -- Sora's gauge, a boss's gauge, and
//                Kairi's checklist -- and the DS's job is to agree.
//   the menu     is NEW. The SNES had no command menu; it had a dialogue box
//                with a yes/no prompt and a three-way raft naming. Inventing a
//                Kingdom Hearts command list here would be designing a game
//                rather than porting one, and it needs a screen to judge.
//   the minimap  is NEW, though less arbitrarily: it is a downscale of data
//                that already exists. What it should LOOK like is still a
//                decision nobody can check without seeing it.
//
// So this file is the HUD. The other two reservations stay reservations, which
// is a better state than a guess: an empty region with a name is something the
// next person fills in, and a wrong one is something they have to notice first.
//
// WHAT MOVED, and it is the whole of the difference. On the SNES the HUD was
// BG3 of the only screen, "the 2bpp layer and, with the mode-1 priority bit
// set, it draws above everything else -- which is exactly what a HUD wants"
// (hud.s:4-6). The DS has a second screen, so the HUD goes there and stops
// competing with the world for a layer at all. The CONTENT does not change;
// where it is drawn does, and the layer priority argument evaporates.

#include <cstdint>

#include "actor.h"

namespace kh::device {

// A sub-engine background is 32 characters across; HUD_MAP is 2 KiB, which is
// 32x32 entries. Both are vram_map.h's and neither is a choice here.
constexpr int HUD_COLS = 32;
constexpr int HUD_ROWS = 32;
constexpr int HUD_ENTRIES = HUD_COLS * HUD_ROWS;

// The three rows hud.s writes, and only three: "Two tilemap rows ever change:
// Sora's gauge and, while a boss is alive, its own" (hud.s:5-6), plus a third
// that day two of the raft needs for its five counts (QUEST_ROW2).
constexpr int HUD_ROW_SORA = 0;
constexpr int HUD_ROW_BOSS = 1;         // ...or Kairi's checklist on the island
constexpr int HUD_ROW_QUEST2 = 2;       // day two only

// Sora's gauge. hud.s:23-24: ten cells at two points each, which is why the
// count has to match SORA_MAX_HP rather than merely fit it -- an eleventh cell
// would be a point of HP the player can never lose.
constexpr int HP_BAR_X = 5;
constexpr int HP_BAR_CELLS = 10;

// A boss's. Both bosses have 36 HP and the same eighteen cells; the Guard
// Armor's bar starts two columns further along because its name is longer
// (hud.s:31-36), and hud.s notes both are "runtime values, not the constants
// the cell writes used to be built from".
constexpr int BOSS_LABEL_X = 1;
constexpr int BOSS_BAR_X = 11;
constexpr int ARMOR_BAR_X = 13;
constexpr int BOSS_BAR_CELLS = 18;

// What the gauge is drawn for. Passed in rather than dug out of the actor pool
// because "which actor is the boss" is a scene question and this file is not
// the place to answer it twice.
struct HudState {
    int player = -1;            // slot, or -1 for none: the row stays blank
    int boss = -1;              // slot of the AF_HUGE actor, or -1
    bool armor = false;         // the Guard Armor rather than Darkside
};

// Build the HUD's rows into a 32x32 map. Every entry is written -- see the note
// in hud.cpp about why blanking first is not optional.
void buildHud(uint16_t* map, const Actors& a, const HudState& s);

// One gauge, exposed because it is the piece with the oracle and the piece
// worth testing directly: `cells` cells at two HP each, filled from the left,
// with a half cell for an odd point. hud.s's DrawGauge.
void drawGauge(uint16_t* map, int row, int x, int cells, int hp);

// A character in the HUD's palette, packed as a DS map entry.
//
// The SNES wrote `TXT_ATTR | ch` -- a priority bit and palette 4 in a three-bit
// field. Neither survives: a DS map entry has no priority bit, because priority
// is per LAYER in BGxCNT, and the palette field is four bits at 12-15. So the
// attribute is a different number for different reasons, which is why
// check_constants.py excuses TXT_ATTR rather than carrying it.
uint16_t hudCell(uint8_t ch);

}  // namespace kh::device
