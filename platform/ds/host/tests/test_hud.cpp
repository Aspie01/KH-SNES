// The HUD's rows, against platform/snes/src/hud.s.
//
// The bottom screen is three reservations and only one of them is a port. This
// is that one: the SNES had a HUD and decided what it shows and where, so the
// gauge is checkable against something rather than judged by eye. The command
// menu and the minimap are not here, and their absence is the honest state --
// see the note at the top of device/hud.h.

#include "actor.h"
#include "check.h"
#include "constants.h"
#include "gen/assets.h"
#include "hud.h"
#include "vram_map.h"

using namespace kh;
using namespace kh::device;

namespace {

uint16_t g_map[HUD_ENTRIES];

uint8_t glyph(int row, int col) {
    return uint8_t(g_map[row * HUD_COLS + col] & 0x3FF);
}

int spawnAt(Actors& a, ActType t, int hp) {
    const int i = a.spawn(t, tileCentre(4), tileCentre(4));
    a.hp[i] = uint8_t(hp);
    return i;
}

}  // namespace

KH_TEST(hud_the_map_is_exactly_what_vram_map_reserved_for_it) {
    // 2 KiB of map RAM at two bytes an entry is 1024 entries, which is 32x32 --
    // and a sub-engine background is 32 characters across.  Neither number is a
    // choice this file gets to make.
    CHECK_EQ(HUD_ENTRIES * 2, int(vram::HUD_MAP.bytes));
    CHECK_EQ(HUD_COLS, 32);
    CHECK_EQ(HUD_ROWS, 32);
    // ...and the font it draws with fits the character reservation behind it.
    CHECK(vram::FONT_CHARS <= vram::SUB_CHR_MAX);
}

KH_TEST(hud_a_gauge_cell_is_two_points_and_the_counts_match_the_maxima) {
    // hud.s:22 asks for this in prose: "Each gauge cell is worth two points, so
    // these counts have to match the maximum HP values in game.inc."  A
    // mismatch is not a drawing bug -- it is a gauge that cannot reach one end,
    // which reads as the fight being unwinnable or the player immortal.
    CHECK_EQ(HP_BAR_CELLS * 2, SORA_MAX_HP);
    CHECK_EQ(BOSS_BAR_CELLS * 2, DS_MAX_HP);
    CHECK_EQ(BOSS_BAR_CELLS * 2, GA_MAX_HP);
    // Both bosses have the same gauge and the same fight; only the bar's start
    // differs, because the Guard Armor's name is longer.
    CHECK_EQ(DS_MAX_HP, GA_MAX_HP);
    CHECK(ARMOR_BAR_X > BOSS_BAR_X);
    // "caps at 12 and 31: the row exactly" -- hud.s:35.  No slack at all.
    CHECK_EQ(ARMOR_BAR_X + BOSS_BAR_CELLS, HUD_COLS - 1);
}

KH_TEST(hud_the_gauge_fills_from_the_left_with_a_half_cell_for_an_odd_point) {
    // DrawGauge, cell by cell.  Two points full, one half, none empty.
    for (int hp = 0; hp <= SORA_MAX_HP; ++hp) {
        for (int i = 0; i < HUD_ENTRIES; ++i) g_map[i] = 0;
        drawGauge(g_map, 0, HP_BAR_X, HP_BAR_CELLS, hp);
        for (int c = 0; c < HP_BAR_CELLS; ++c) {
            const int left = hp - c * 2;
            const uint8_t want = left <= 0 ? CH_BAR_EMPTY
                               : left >= 2 ? CH_BAR_FULL : CH_BAR_HALF;
            CHECK_EQ(glyph(0, HP_BAR_X + c), want);
        }
    }

    // The two ends, spelled out, because they are the two a reader checks.
    for (int i = 0; i < HUD_ENTRIES; ++i) g_map[i] = 0;
    drawGauge(g_map, 0, HP_BAR_X, HP_BAR_CELLS, SORA_MAX_HP);
    for (int c = 0; c < HP_BAR_CELLS; ++c)
        CHECK_EQ(glyph(0, HP_BAR_X + c), CH_BAR_FULL);
    drawGauge(g_map, 0, HP_BAR_X, HP_BAR_CELLS, 0);
    for (int c = 0; c < HP_BAR_CELLS; ++c)
        CHECK_EQ(glyph(0, HP_BAR_X + c), CH_BAR_EMPTY);
    // One point is exactly one half cell and nine empties.
    drawGauge(g_map, 0, HP_BAR_X, HP_BAR_CELLS, 1);
    CHECK_EQ(glyph(0, HP_BAR_X), CH_BAR_HALF);
    CHECK_EQ(glyph(0, HP_BAR_X + 1), CH_BAR_EMPTY);
}

KH_TEST(hud_negative_hp_draws_empty_rather_than_wrapping_to_full) {
    // hud.s's `bmi @empty / beq @empty` -- two branches, not one.  There is a
    // frame between the hit that kills and the death being processed, and an
    // actor sits at negative HP through it; a `== 0` test would let that wrap
    // into a full cell and the gauge would refill at the moment of death.
    for (int i = 0; i < HUD_ENTRIES; ++i) g_map[i] = 0;
    drawGauge(g_map, 0, HP_BAR_X, HP_BAR_CELLS, -6);
    for (int c = 0; c < HP_BAR_CELLS; ++c)
        CHECK_EQ(glyph(0, HP_BAR_X + c), CH_BAR_EMPTY);
}

KH_TEST(hud_blanks_with_the_transparent_cell_and_not_the_opaque_one) {
    // CH_CLEAR and CH_BLANK are different glyphs and the difference is the
    // point: text.inc calls CH_CLEAR "the one genuinely transparent cell",
    // because "every glyph carries an opaque background so text can sit inside
    // the dialogue window".  Blanking with CH_BLANK would paint the whole
    // bottom screen with the dialogue box's background colour.
    CHECK(CH_CLEAR != CH_BLANK);
    Actors a;
    a.clear();
    HudState s;
    buildHud(g_map, a, s);              // no player, no boss: all blank
    for (int i = 0; i < HUD_ENTRIES; ++i) CHECK_EQ(g_map[i] & 0x3FF, CH_CLEAR);
}

KH_TEST(hud_every_cell_is_rewritten_so_a_dead_boss_leaves_no_gauge) {
    // The rows persist between updates, so a boss that dies without its row
    // being cleared leaves an empty gauge on screen -- which reads as a boss
    // still alive with no health left.  hud.s blanks before it draws for
    // exactly this reason.
    Actors a;
    a.clear();
    const int sora = spawnAt(a, ActType::Sora, SORA_MAX_HP);
    const int boss = spawnAt(a, ActType::Darkside, DS_MAX_HP);
    HudState s;
    s.player = sora;
    s.boss = boss;
    buildHud(g_map, a, s);
    CHECK_EQ(glyph(HUD_ROW_BOSS, BOSS_BAR_X), CH_BAR_FULL);

    // The boss dies; the row goes.
    a.type[boss] = ActType::None;
    s.boss = -1;
    buildHud(g_map, a, s);
    for (int c = 0; c < HUD_COLS; ++c)
        CHECK_EQ(glyph(HUD_ROW_BOSS, c), CH_CLEAR);
    // ...and Sora's row survives, because blanking is not forgetting.
    CHECK_EQ(glyph(HUD_ROW_SORA, HP_BAR_X), CH_BAR_FULL);
}

KH_TEST(hud_draws_nothing_for_a_player_slot_that_holds_nobody) {
    // hud.s:158's `beq @boss`: "no player yet: leave the row blank".  There is
    // a frame between a scene load and the cast spawning, and a gauge built
    // from an empty slot would show full health for somebody who does not exist.
    Actors a;
    a.clear();
    HudState s;
    s.player = 0;                       // a slot, but nothing is in it
    buildHud(g_map, a, s);
    for (int c = 0; c < HUD_COLS; ++c) CHECK_EQ(glyph(HUD_ROW_SORA, c), CH_CLEAR);

    // ...and an out-of-range slot is refused rather than read.
    s.player = MAX_ACTORS + 10;
    buildHud(g_map, a, s);
    for (int c = 0; c < HUD_COLS; ++c) CHECK_EQ(glyph(HUD_ROW_SORA, c), CH_CLEAR);
}

KH_TEST(hud_sora_gets_his_label_and_both_end_caps) {
    Actors a;
    a.clear();
    const int sora = spawnAt(a, ActType::Sora, 13);
    HudState s;
    s.player = sora;
    buildHud(g_map, a, s);

    CHECK_EQ(glyph(HUD_ROW_SORA, 2), uint8_t(CH_A + 'H' - 'A'));
    CHECK_EQ(glyph(HUD_ROW_SORA, 3), uint8_t(CH_A + 'P' - 'A'));
    CHECK_EQ(glyph(HUD_ROW_SORA, HP_BAR_X - 1), CH_BAR_L);
    CHECK_EQ(glyph(HUD_ROW_SORA, HP_BAR_X + HP_BAR_CELLS), CH_BAR_R);
    // 13 HP is six full cells and a half.
    for (int c = 0; c < 6; ++c) CHECK_EQ(glyph(HUD_ROW_SORA, HP_BAR_X + c), CH_BAR_FULL);
    CHECK_EQ(glyph(HUD_ROW_SORA, HP_BAR_X + 6), CH_BAR_HALF);
    CHECK_EQ(glyph(HUD_ROW_SORA, HP_BAR_X + 7), CH_BAR_EMPTY);
}

KH_TEST(hud_the_armors_bar_starts_two_columns_further_along) {
    // The two bosses have the same gauge and the same fight; only the start
    // moves, because "the Guard Armor has a longer name" (hud.s:31-33).
    Actors a;
    a.clear();
    const int boss = spawnAt(a, ActType::Armor, GA_MAX_HP / 2);
    HudState s;
    s.boss = boss;

    s.armor = false;
    buildHud(g_map, a, s);
    CHECK_EQ(glyph(HUD_ROW_BOSS, BOSS_BAR_X - 1), CH_BAR_L);
    CHECK_EQ(glyph(HUD_ROW_BOSS, ARMOR_BAR_X - 1), CH_BAR_FULL);   // mid-bar

    s.armor = true;
    buildHud(g_map, a, s);
    CHECK_EQ(glyph(HUD_ROW_BOSS, ARMOR_BAR_X - 1), CH_BAR_L);
    CHECK_EQ(glyph(HUD_ROW_BOSS, BOSS_BAR_X - 1), CH_CLEAR);       // nothing there now
    CHECK_EQ(glyph(HUD_ROW_BOSS, ARMOR_BAR_X + BOSS_BAR_CELLS), CH_BAR_R);
    // The right cap is the last column of the row, exactly.
    CHECK_EQ(ARMOR_BAR_X + BOSS_BAR_CELLS, HUD_COLS - 1);
}

KH_TEST(hud_a_cell_carries_the_font_palette_and_no_priority_bit) {
    // The SNES wrote `TXT_ATTR | ch` -- a priority bit and palette 4 in a
    // three-bit field.  Neither survives: a DS map entry has no priority bit,
    // because priority is per LAYER in BGxCNT, and the palette is four bits at
    // 12-15.  So the attribute is a different number for different reasons,
    // which is why check_constants.py excuses TXT_ATTR rather than carrying it.
    CHECK_EQ(int(hudCell(CH_BAR_FULL) & 0x3FF), int(CH_BAR_FULL));
    CHECK_EQ(int(hudCell(CH_BAR_FULL) >> 12), UI_SUBPALETTE);
    // Bits 10-11 are the flip bits on a DS map entry, and a HUD never flips.
    CHECK_EQ(int(hudCell(CH_BAR_FULL) >> 10) & 3, 0);

    Actors a;
    a.clear();
    HudState s;
    s.player = spawnAt(a, ActType::Sora, SORA_MAX_HP);
    buildHud(g_map, a, s);
    for (int i = 0; i < HUD_ENTRIES; ++i) {
        CHECK_EQ((g_map[i] >> 12) & 0xF, UI_SUBPALETTE);
        CHECK_EQ((g_map[i] >> 10) & 3, 0);
    }
    // Every glyph it can draw is inside the font's 128 characters.
    for (int i = 0; i < HUD_ENTRIES; ++i)
        CHECK(int(g_map[i] & 0x3FF) < vram::FONT_CHARS);
}

KH_TEST(hud_the_gauge_glyphs_are_the_ones_text_inc_numbers) {
    // These arrived with this milestone: text.inc had them and the DS did not,
    // because check_constants.py read game.inc only and its DS-side reader
    // looked in three headers by name.  Twenty-two constants were invisible
    // from both directions at once.  Pinned here so the numbering cannot drift
    // from the order build_assets.py emits the font in.
    CHECK_EQ(int(CH_BAR_L), 48);
    CHECK_EQ(int(CH_BAR_FULL), 49);
    CHECK_EQ(int(CH_BAR_HALF), 50);
    CHECK_EQ(int(CH_BAR_EMPTY), 51);
    CHECK_EQ(int(CH_BAR_R), 52);
    CHECK_EQ(int(CH_CLEAR), 127);
    // The nine-patch too, which the dialogue box on the MAIN engine will want.
    CHECK_EQ(int(CH_WIN_TL), 56);
    CHECK_EQ(int(CH_WIN_BR), 64);
    CHECK_EQ(int(CH_WIN_BR) - int(CH_WIN_TL), 8);   // nine cells, contiguous
}
