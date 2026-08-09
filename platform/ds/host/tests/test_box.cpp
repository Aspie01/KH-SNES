// The dialogue box's tilemap.  §M5's other half, six milestones late.
//
// What makes this checkable with no screen is that the SNES already decided
// every number: text.s's DrawFrame, ClearTextArea, DrawText, DrawAdvanceMark
// and DrawOptions, and text.inc's geometry, which constants.h already mirrors.
// So these cases compare the map against the SPECIFICATION rather than against
// a picture -- which is the same thing test_device_init.cpp does for the
// registers and comes with the same caveat: nothing here has ever been drawn.

#include <cstring>

#include "box.h"
#include "check.h"
#include "constants.h"
#include "gen/assets.h"
#include "gen/scripts.h"
#include "text.h"

using namespace kh;
using namespace kh::device;

namespace {

uint16_t g_map[BOX_ENTRIES];

uint8_t glyphAt(int x, int y) { return uint8_t(g_map[y * BOX_COLS + x] & 0x03FF); }
int palAt(int x, int y) { return g_map[y * BOX_COLS + x] >> 12; }

// A Dialogue with `s` fully revealed, which is the state most of these want:
// the reveal is one character every few frames and a case that waited for it
// would be a case about the interpreter, which test_text.cpp already owns.
void openAndReveal(Dialogue& d, ScriptId s, TextMode mode) {
    Pad pad;
    d.open(scriptFor(s), mode);
    for (int i = 0; i < 4000 && d.state() == TextState::Reveal; ++i) {
        pad.held = 0;
        pad.pressed = 0;
        d.update(pad);
    }
}

}  // namespace

KH_TEST(box_a_closed_box_is_transparent_everywhere) {
    // NOT BLANK.  text.inc: CH_CLEAR is "the one genuinely transparent cell" and
    // every other glyph carries an opaque background so text can sit inside the
    // window.  A map filled with CH_BLANK is a solid 256x256 rectangle over the
    // top-left of the world, and it looks exactly like a bank mapping fault --
    // which is a day of debugging the wrong file.
    Dialogue d;
    std::memset(g_map, 0xAB, sizeof g_map);         // poison, to prove it writes
    buildBox(g_map, d);
    CHECK(!d.busy());
    for (int i = 0; i < BOX_ENTRIES; ++i) CHECK_EQ(g_map[i] & 0x03FF, CH_CLEAR);
}

KH_TEST(box_sits_on_the_bottom_edge_of_a_192_line_screen) {
    // The one divergence in box.cpp.  text.inc's BOX_ROW is 20 on a 28-row
    // screen; carrying it across to a 24-row one would push the advance mark
    // and the whole of a yes/no prompt off the bottom.
    CHECK_EQ(BOX_TOP, 17);
    CHECK_EQ(BOX_TOP + BOX_ROWS, SCREEN_H / 8);
    CHECK(BOX_TOP < BOX_ROW);                       // it moved UP, by 3 rows
    CHECK_EQ(BOX_ROW - BOX_TOP, 3);
    // ...which is exactly the 32 lines divergence 001 is about, plus the row of
    // margin the SNES had below the box and the DS does not.
    CHECK_EQ((224 - SCREEN_H) / 8, 4);
}

KH_TEST(box_draws_the_nine_patch_at_its_four_corners) {
    Dialogue d;
    openAndReveal(d, ScriptId::DiveIntro, TextMode::Message);
    buildBox(g_map, d);
    CHECK_EQ(glyphAt(0, BOX_TOP), CH_WIN_TL);
    CHECK_EQ(glyphAt(BOX_COLS - 1, BOX_TOP), CH_WIN_TR);
    CHECK_EQ(glyphAt(0, BOX_TOP + BOX_ROWS - 1), CH_WIN_BL);
    CHECK_EQ(glyphAt(BOX_COLS - 1, BOX_TOP + BOX_ROWS - 1), CH_WIN_BR);
    CHECK_EQ(glyphAt(5, BOX_TOP), CH_WIN_T);
    CHECK_EQ(glyphAt(5, BOX_TOP + BOX_ROWS - 1), CH_WIN_B);
    CHECK_EQ(glyphAt(0, BOX_TOP + 2), CH_WIN_L);
    CHECK_EQ(glyphAt(BOX_COLS - 1, BOX_TOP + 2), CH_WIN_R);
    // ...and nothing above it.  The box is seven rows and the rest of the map
    // has to stay out of the way of the world.
    CHECK_EQ(glyphAt(0, BOX_TOP - 1), CH_CLEAR);
    CHECK_EQ(glyphAt(16, 0), CH_CLEAR);
}

KH_TEST(box_puts_the_page_where_the_text_area_is) {
    Dialogue d;
    openAndReveal(d, ScriptId::DiveIntro, TextMode::Message);
    buildBox(g_map, d);

    // Every character of every row of the page, against Dialogue's own buffer.
    // This is the assertion that the renderer draws THE MESSAGE and not a
    // message: a transposed index or an off-by-one column would still produce
    // plausible-looking glyphs.
    // ...EXCEPT the last cell of the last row, which the advance mark takes.
    // That overwrite is text.s:523's behaviour and not an accident, and the
    // first version of this case did not exempt it and failed -- correctly, and
    // on the test rather than on the code.  It costs nothing today because no
    // shipped script fills the final column of the final line; the day one does,
    // the reader loses one character and the SNES lost it too.
    for (int r = 0; r < TEXT_H; ++r) {
        const char* line = d.row(r);
        for (int c = 0; c < TEXT_W; ++c) {
            if (r == TEXT_H - 1 && c == TEXT_W - 1) continue;
            CHECK_EQ(glyphAt(TEXT_COL + c, BOX_TOP + TEXT_ROW + r),
                     glyphOf(line[c]));
        }
    }
    // ...and the cell the mark took was a blank, so nothing was actually lost.
    CHECK_EQ(glyphOf(d.row(TEXT_H - 1)[TEXT_W - 1]), CH_BLANK);
    // ...and it said something, or the loop above compared blanks to blanks.
    bool any = false;
    for (int c = 0; c < TEXT_W; ++c)
        if (glyphAt(TEXT_COL + c, BOX_TOP + TEXT_ROW) != CH_BLANK) any = true;
    CHECK(any);
}

KH_TEST(box_marks_a_finished_message_and_an_unfinished_one_differently) {
    Dialogue d;
    Pad pad;
    d.open(scriptFor(ScriptId::DiveIntro), TextMode::Message);
    CHECK_EQ(int(d.state()), int(TextState::Reveal));
    buildBox(g_map, d);
    const int markCell = TEXT_COL + TEXT_W - 1;
    const int markRow = BOX_TOP + TEXT_ROW + TEXT_H - 1;
    CHECK(glyphAt(markCell, markRow) != CH_ADVANCE);

    openAndReveal(d, ScriptId::DiveIntro, TextMode::Message);
    CHECK_EQ(int(d.state()), int(TextState::Wait));
    buildBox(g_map, d);
    CHECK_EQ(glyphAt(markCell, markRow), CH_ADVANCE);
    (void)pad;
}

KH_TEST(box_option_rows_match_the_snes_per_mode_offsets) {
    // text.s:699's menuRow: PROMPT_ROW for a yes/no, MENU_ROW for the raft.  The
    // three-way menu needs the line above, which is the whole reason there are
    // two numbers and not one.
    CHECK_EQ(optionRow(TextMode::Prompt, 0), BOX_TOP + TEXT_ROW + PROMPT_ROW);
    CHECK_EQ(optionRow(TextMode::Raft, 0), BOX_TOP + TEXT_ROW + MENU_ROW);
    // ...and every option lands inside the box rather than on its frame.
    for (int i = 0; i < menuCount(TextMode::Prompt); ++i) {
        CHECK(optionRow(TextMode::Prompt, i) >= BOX_TOP + 1);
        CHECK(optionRow(TextMode::Prompt, i) <= BOX_TOP + BOX_ROWS - 2);
    }
    for (int i = 0; i < menuCount(TextMode::Raft); ++i) {
        CHECK(optionRow(TextMode::Raft, i) >= BOX_TOP + 1);
        CHECK(optionRow(TextMode::Raft, i) <= BOX_TOP + BOX_ROWS - 2);
    }
}

KH_TEST(box_every_mode_has_as_many_labels_as_menucount_says) {
    // The interpreter counts with menuCount() and the renderer draws from a
    // table.  A disagreement is a selection that lands on a row with nothing
    // drawn on it, or a name the cursor can never reach -- and neither faults.
    for (int i = 0; i < menuCount(TextMode::Prompt); ++i)
        CHECK(optionText(TextMode::Prompt, i) != nullptr);
    CHECK(optionText(TextMode::Prompt, menuCount(TextMode::Prompt)) == nullptr);
    for (int i = 0; i < menuCount(TextMode::Raft); ++i)
        CHECK(optionText(TextMode::Raft, i) != nullptr);
    CHECK(optionText(TextMode::Raft, menuCount(TextMode::Raft)) == nullptr);
    CHECK(optionText(TextMode::Message, 0) == nullptr);
}

KH_TEST(box_the_cursor_is_on_the_choice_and_nowhere_else) {
    Dialogue d;
    openAndReveal(d, ScriptId::IslandWhatName, TextMode::Raft);
    Pad pad;
    pad.held = pad.pressed = raw(Button::A);
    d.update(pad);                                  // Wait -> Prompt
    CHECK_EQ(int(d.state()), int(TextState::Prompt));
    CHECK_EQ(d.choice(), 0);

    buildBox(g_map, d);
    CHECK_EQ(glyphAt(OPT_COL, optionRow(TextMode::Raft, 0)), CH_CURSOR);
    CHECK_EQ(glyphAt(OPT_COL, optionRow(TextMode::Raft, 1)), CH_BLANK);
    CHECK_EQ(glyphAt(OPT_COL, optionRow(TextMode::Raft, 2)), CH_BLANK);

    pad.held = pad.pressed = raw(Button::Down);
    d.update(pad);
    CHECK_EQ(d.choice(), 1);
    buildBox(g_map, d);
    // THE VACATED CELL HAS TO BECOME SOMETHING.  Drawing the cursor without
    // erasing the old one leaves two arrows and the player cannot tell which
    // answer is selected -- which is why box.cpp rebuilds rather than patches.
    CHECK_EQ(glyphAt(OPT_COL, optionRow(TextMode::Raft, 0)), CH_BLANK);
    CHECK_EQ(glyphAt(OPT_COL, optionRow(TextMode::Raft, 1)), CH_CURSOR);
}

KH_TEST(box_a_short_option_erases_the_tail_of_a_longer_one) {
    // text.s's @pad arm, and the reason OPT_W exists at all.  RAGNAROK is eight
    // characters and EXCALIBUR is nine, so if the pad stopped at the terminator
    // the third row would read "RAGNAROKR" -- one character of the row above,
    // left over, which reads as a typo in the script rather than a drawing bug.
    Dialogue d;
    openAndReveal(d, ScriptId::IslandWhatName, TextMode::Raft);
    Pad pad;
    pad.held = pad.pressed = raw(Button::A);
    d.update(pad);
    CHECK_EQ(int(d.state()), int(TextState::Prompt));

    std::memset(g_map, 0xAB, sizeof g_map);
    buildBox(g_map, d);
    const int row = optionRow(TextMode::Raft, 2);   // RAGNAROK, the short one
    for (int c = 0; c < OPT_W; ++c) {
        const uint8_t g = glyphAt(OPT_COL + 1 + c, row);
        CHECK(c < 8 ? g != CH_BLANK : g == CH_BLANK);
    }
}

KH_TEST(box_uses_the_font_sub_palette_everywhere_it_writes) {
    // Both screens share the font and its palette, and UI_SUBPALETTE is 15
    // rather than 0 because "the ground reloads into sub-palette 0 on every
    // scene; a font there would change colour with the scenery".  One cell in
    // the wrong sub-palette is one character in the wrong colour.
    Dialogue d;
    openAndReveal(d, ScriptId::DiveIntro, TextMode::Message);
    buildBox(g_map, d);
    for (int y = 0; y < BOX_MAP_ROWS; ++y)
        for (int x = 0; x < BOX_COLS; ++x)
            CHECK_EQ(palAt(x, y), UI_SUBPALETTE);
}
