// The HUD's tilemap rows, ported from platform/snes/src/hud.s.

#include "hud.h"

#include "constants.h"
#include "gen/assets.h"
#include "vram_map.h"

namespace kh::device {
namespace {

// The gauge cells have to match the HP maxima, and this is the assertion
// hud.s:22 asks for in prose: "Each gauge cell is worth two points, so these
// counts have to match the maximum HP values in game.inc."  A mismatch is not
// a drawing bug -- it is a gauge that cannot reach either end, which reads as
// the fight being unwinnable or as the player being immortal.
constexpr int HP_PER_CELL = 2;
static_assert(HP_BAR_CELLS * HP_PER_CELL == SORA_MAX_HP,
              "Sora's gauge and his maximum HP disagree");
static_assert(BOSS_BAR_CELLS * HP_PER_CELL == DS_MAX_HP,
              "Darkside's gauge and his maximum HP disagree");
static_assert(BOSS_BAR_CELLS * HP_PER_CELL == GA_MAX_HP,
              "the Guard Armor's gauge and its maximum HP disagree");

// The bars have to fit the row they are drawn on, at both ends.  hud.s:35
// records that the Armor's "caps at 12 and 31: the row exactly" -- so this is
// the constraint that was already at its limit when the SNES shipped, and the
// one a wider name would break first.
static_assert(HP_BAR_X - 1 >= 0 && HP_BAR_X + HP_BAR_CELLS < HUD_COLS,
              "Sora's gauge and its end caps run off the row");
static_assert(BOSS_BAR_X + BOSS_BAR_CELLS <= HUD_COLS,
              "Darkside's gauge runs off the row");
static_assert(ARMOR_BAR_X + BOSS_BAR_CELLS <= HUD_COLS,
              "the Guard Armor's gauge runs off the row -- hud.s:35 says this "
              "one caps at column 31 exactly, so there is no slack at all");

constexpr uint8_t CH_H = uint8_t(CH_A + 'H' - 'A');
constexpr uint8_t CH_P = uint8_t(CH_A + 'P' - 'A');

}  // namespace

uint16_t hudCell(uint8_t ch) {
    // The font's characters are the first 128 of SUB_CHR (vram_map.h's
    // FONT_CHARS), so a glyph index IS a character number with no base to add.
    // UI_SUBPALETTE is gen/assets.h's, and it is 15 rather than 0 for a reason
    // that applies here too: "the ground reloads into sub-palette 0 on every
    // scene; a font there would change colour with the scenery".  The bottom
    // screen has no ground to reload, but it shares the font and its palette
    // with the main engine's dialogue box, and one font with two palette
    // numbers is a font that is the wrong colour on one of the two screens.
    return uint16_t(uint16_t(ch) | uint16_t(UI_SUBPALETTE << 12));
}

void drawGauge(uint16_t* map, int row, int x, int cells, int hp) {
    // hud.s's DrawGauge, cell by cell: subtract what earlier cells already
    // accounted for, and what is left decides this one.  Two points is full,
    // one is half, none or negative is empty.
    //
    // The `bmi @empty / beq @empty` pair is why this is `<= 0` and not `== 0`:
    // an actor at negative HP -- which happens for a frame between the hit that
    // kills and the death being processed -- draws an empty cell rather than
    // wrapping into a full one.
    for (int c = 0; c < cells; ++c) {
        const int left = hp - c * HP_PER_CELL;
        const uint8_t ch = left <= 0 ? CH_BAR_EMPTY
                         : left >= HP_PER_CELL ? CH_BAR_FULL
                         : CH_BAR_HALF;
        map[row * HUD_COLS + x + c] = hudCell(ch);
    }
}

void buildHud(uint16_t* map, const Actors& a, const HudState& s) {
    // BLANK EVERY ROW FIRST, which hud.s does before anything else
    // (HudUpdate's `@blank` loop over 192 bytes) and which is not the
    // housekeeping it looks like.  The rows persist between updates, so a boss
    // that dies without its row being cleared leaves its gauge on screen,
    // frozen at whatever it last held -- and an empty gauge at that, which
    // reads as a boss still alive with no health left.
    //
    // CH_CLEAR and not CH_BLANK.  They are different glyphs and the difference
    // is the point: text.inc calls CH_CLEAR "the one genuinely transparent
    // cell", because "every glyph carries an opaque background so text can sit
    // inside the dialogue window".  Blanking with CH_BLANK would paint the
    // whole bottom screen with the box's background colour.
    for (int i = 0; i < HUD_ENTRIES; ++i) map[i] = hudCell(CH_CLEAR);

    // --- Sora's row --------------------------------------------------------
    // "no player yet: leave the row blank" -- hud.s:158's `beq @boss`.  There
    // is a frame between a scene load and the cast spawning, and a gauge drawn
    // from an empty slot would show full health for somebody who does not exist.
    if (s.player >= 0 && s.player < MAX_ACTORS
        && a.type[s.player] != ActType::None) {
        map[HUD_ROW_SORA * HUD_COLS + 2] = hudCell(CH_H);
        map[HUD_ROW_SORA * HUD_COLS + 3] = hudCell(CH_P);
        map[HUD_ROW_SORA * HUD_COLS + HP_BAR_X - 1] = hudCell(CH_BAR_L);
        map[HUD_ROW_SORA * HUD_COLS + HP_BAR_X + HP_BAR_CELLS] = hudCell(CH_BAR_R);
        drawGauge(map, HUD_ROW_SORA, HP_BAR_X, HP_BAR_CELLS, a.hp[s.player]);
    }

    // --- the boss's --------------------------------------------------------
    // The bar starts two columns later for the Guard Armor, whose name is
    // longer and whose fight is the same length (hud.s:31-36).  The NAME itself
    // is not drawn here: the labels are script strings, PutLabel walks them out
    // of the same table the dialogue does, and putting a second string layout
    // in this file would be the beginning of a second text renderer.
    if (s.boss >= 0 && s.boss < MAX_ACTORS && a.type[s.boss] != ActType::None) {
        const int x = s.armor ? ARMOR_BAR_X : BOSS_BAR_X;
        map[HUD_ROW_BOSS * HUD_COLS + x - 1] = hudCell(CH_BAR_L);
        map[HUD_ROW_BOSS * HUD_COLS + x + BOSS_BAR_CELLS] = hudCell(CH_BAR_R);
        drawGauge(map, HUD_ROW_BOSS, x, BOSS_BAR_CELLS, a.hp[s.boss]);
    }
    (void)BOSS_LABEL_X;     // where the name goes, when there is a name to draw
}

}  // namespace kh::device
