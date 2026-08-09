// The dialogue box's tilemap.  A port of text.s's DrawFrame, ClearTextArea,
// DrawText, DrawAdvanceMark and DrawOptions, collapsed into one pass.
//
// WHY ONE PASS RATHER THAN FIVE ENTRY POINTS.  The SNES redrew the parts that
// changed and set txtDirty so the NMI would upload them, because it had 3.58 MHz
// and a 1 KiB buffer.  The whole map here is 1024 halfwords and the ARM9 writes
// it in a few thousand cycles; rebuilding it every frame removes the entire
// class of bug where a stale cell survives a state change -- which on the SNES
// is precisely what ClearTextArea and the OPT_W padding exist to prevent.

#include "box.h"

#include "gen/assets.h"

namespace kh::device {
namespace {

// text.s:702-707.  Yes/no is menu 0 and the raft is menu 1, which is
// `int(mode) - 1` -- TM_MESSAGE has no menu.
constexpr const char* OPT_YESNO[] = {"YES", "NO"};
constexpr const char* OPT_RAFT[] = {"HIGHWIND", "EXCALIBUR", "RAGNAROK"};

constexpr int OPT_YESNO_COUNT = int(sizeof OPT_YESNO / sizeof *OPT_YESNO);
constexpr int OPT_RAFT_COUNT = int(sizeof OPT_RAFT / sizeof *OPT_RAFT);

// The tables and text.h's menuCount() must agree.  They are two statements of
// one fact -- how many choices a mode offers -- and the interpreter uses the
// function while the renderer uses the table, so a disagreement would show up
// as a menu whose third option cannot be selected or a selection that lands on
// a row with nothing drawn on it.  Neither faults.
static_assert(OPT_YESNO_COUNT == menuCount(TextMode::Prompt),
              "the yes/no table and menuCount() disagree");
static_assert(OPT_RAFT_COUNT == menuCount(TextMode::Raft),
              "the raft table and menuCount() disagree");

// ...and the longest name has to fit the padding, or DrawOptions' erase pass
// leaves the tail of a longer choice on screen.  text.inc set OPT_W to 11 and
// EXCALIBUR is 9, so there are two cells of slack; the assertion is what tells
// whoever adds a fourth name that they have run out.
constexpr int nameLen(const char* s) {
    int n = 0;
    while (s[n]) ++n;
    return n;
}
static_assert(nameLen("EXCALIBUR") <= OPT_W,
              "the longest raft name is wider than OPT_W, so the pad that "
              "erases the previous choice would not reach the end of it");

// A 32x32 background's entry index.  bgEntryIndex() collapses to y * 32 + x at
// this shape, which is worth going through anyway rather than writing the
// multiply out: the one place in this project where a map was indexed by hand
// was the one place that had to be told the hardware uses 32x32 blocks.
int at(int x, int y) { return bgEntryIndex(x, y, BOX_COLS, BOX_MAP_ROWS); }

// DrawFrame, text.s:124.  The nine-patch, by which piece each row and column
// wants.
uint8_t framePiece(int row, int col) {
    const bool top = row == 0;
    const bool bottom = row == BOX_ROWS - 1;
    const bool left = col == 0;
    const bool right = col == BOX_COLS - 1;
    if (top) return left ? CH_WIN_TL : right ? CH_WIN_TR : CH_WIN_T;
    if (bottom) return left ? CH_WIN_BL : right ? CH_WIN_BR : CH_WIN_B;
    return left ? CH_WIN_L : right ? CH_WIN_R : CH_WIN_C;
}

}  // namespace

uint16_t boxCell(uint8_t ch) {
    return uint16_t(uint16_t(ch) | uint16_t(UI_SUBPALETTE << 12));
}

const char* optionText(TextMode mode, int i) {
    if (mode == TextMode::Prompt)
        return (i >= 0 && i < OPT_YESNO_COUNT) ? OPT_YESNO[i] : nullptr;
    if (mode == TextMode::Raft)
        return (i >= 0 && i < OPT_RAFT_COUNT) ? OPT_RAFT[i] : nullptr;
    return nullptr;     // TextMode::Message has no menu
}

int optionRow(TextMode mode, int i) {
    // menuRow, text.s:699.  PROMPT_ROW leaves three lines of question above a
    // yes/no; MENU_ROW leaves two, because three names need one more line than
    // two answers do.
    const int menuRow = mode == TextMode::Raft ? MENU_ROW : PROMPT_ROW;
    return BOX_TOP + TEXT_ROW + menuRow + i;
}

void buildBox(uint16_t* map, const Dialogue& d) {
    // CH_CLEAR EVERYWHERE FIRST.  See the note in box.h: this font's glyphs are
    // opaque, so an unwritten or blank-filled map is a solid rectangle over the
    // world rather than nothing at all.
    const uint16_t clear = boxCell(CH_CLEAR);
    for (int i = 0; i < BOX_ENTRIES; ++i) map[i] = clear;
    if (!d.busy()) return;

    for (int r = 0; r < BOX_ROWS; ++r)
        for (int c = 0; c < BOX_COLS; ++c)
            map[at(c, BOX_TOP + r)] = boxCell(framePiece(r, c));

    // The page.  Dialogue holds ASCII and glyphOf() is the mapping -- which is
    // stated in text.h as the reason the page is ASCII at all: "the glyph
    // mapping is a rendering decision".  This is the renderer.
    for (int r = 0; r < TEXT_H; ++r) {
        const char* line = d.row(r);
        for (int c = 0; c < TEXT_W; ++c)
            map[at(TEXT_COL + c, BOX_TOP + TEXT_ROW + r)] =
                boxCell(glyphOf(line[c]));
    }

    // DrawAdvanceMark, text.s:517.  The bottom-right cell of the text area, and
    // it OVERWRITES whatever character was there -- which is the SNES's
    // behaviour and not a bug: a line that reached the last column of the last
    // row has already told the reader everything except that it is finished.
    if (d.state() == TextState::Wait)
        map[at(TEXT_COL + TEXT_W - 1, BOX_TOP + TEXT_ROW + TEXT_H - 1)] =
            boxCell(CH_ADVANCE);

    if (d.state() != TextState::Prompt) return;

    // DrawOptions, text.s:564.  The cursor on the selected line and a blank on
    // the others -- not "draw the cursor", because the cell it vacates has to
    // become something, and a full rebuild makes that automatic rather than a
    // second pass to remember.
    const int n = menuCount(d.mode());
    for (int i = 0; i < n; ++i) {
        const int row = optionRow(d.mode(), i);
        map[at(OPT_COL, row)] =
            boxCell(i == d.choice() ? CH_CURSOR : CH_BLANK);
        // ...and the text, padded to OPT_W.  `k` stops advancing at the
        // terminator rather than the loop stopping there, which is text.s's
        // @pad arm: the cells past the end of a short choice must be BLANKED,
        // not left holding the tail of a longer one drawn on this row before.
        const char* text = optionText(d.mode(), i);
        int k = 0;
        for (int c = 0; c < OPT_W; ++c) {
            const char ch = (text && text[k]) ? text[k++] : ' ';
            map[at(OPT_COL + 1 + c, row)] = boxCell(glyphOf(ch));
        }
    }
}

}  // namespace kh::device
