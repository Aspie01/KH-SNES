// The dialogue interpreter.
//
// Every assertion here cites what it is checking against.  The authority is
// platform/snes/src/text.s; docs/BEHAVIOUR.md §13 describes it and
// docs/BEHAVIOUR-AUDIT.md finding 42 corrects the description of SC_PAGE.

#include <cstring>

#include "check.h"
#include "text.h"

using namespace kh;

namespace {

// dive.s scriptIntro, verbatim.  Three pages, which is why it is the fixture:
// on the SNES only the first of them was ever visible.
const uint8_t SCRIPT_INTRO[] = {
    'S','O',' ','M','U','C','H',' ','T','O',' ','D','O',',', SC_NL,
    'S','O',' ','L','I','T','T','L','E',' ','T','I','M','E','.', SC_NL,
    SC_NL,
    'T','A','K','E',' ','Y','O','U','R',' ','T','I','M','E','.', SC_PAGE,
    'D','O','N','\'','T',' ','B','E',' ','A','F','R','A','I','D','.', SC_NL,
    SC_NL,
    'T','H','E',' ','D','O','O','R',' ','I','S',' ','S','T','I','L','L',' ',
    'S','H','U','T','.', SC_PAGE,
    'P','O','W','E','R',' ','S','L','E','E','P','S','.', SC_END,
};

const uint8_t SCRIPT_ONE_PAGE[] = {'H','I','.', SC_END};

// A script with no terminator at all, which is what REVEAL_BACKSTOP defends
// against.  text.s gave up after 512 iterations.
const uint8_t SCRIPT_RUNAWAY[] = {'A','B','C'};

Script scriptOf(const uint8_t* d, size_t n) { return Script{d, n}; }

// Run frames until the box stops revealing, with nothing held.  Returns how
// many frames it took, so a test can assert the reveal rate.
int settle(Dialogue& d, Pad& pad, int limit = 4096) {
    int f = 0;
    while (d.state() == TextState::Reveal && f < limit) {
        d.update(pad);
        ++f;
    }
    return f;
}

// One frame with A newly pressed, then release it -- which is what a real press
// looks like and what txtHold is there to distinguish.
void tapA(Dialogue& d, Pad& pad) {
    pad.held = raw(Button::A);
    pad.pressed = raw(Button::A);
    d.update(pad);
    pad.held = 0;
    pad.pressed = 0;
}

const char* pageOf(Dialogue& d, char (&buf)[256]) {
    d.text(buf, sizeof buf);
    return buf;
}

}  // namespace

KH_TEST(text_glyphs_match_asciiToTile) {
    // text.s's asciiToTile: A..Z from 1, 0..9 from 27, the punctuation the
    // scripts use, lower case folded onto upper, everything else CH_BLANK.
    CHECK_EQ(glyphOf('A'), CH_A);
    CHECK_EQ(glyphOf('Z'), CH_A + 25);
    CHECK_EQ(glyphOf('a'), CH_A);           // folded: the font has no lower case
    CHECK_EQ(glyphOf('z'), CH_A + 25);
    CHECK_EQ(glyphOf('0'), CH_0);
    CHECK_EQ(glyphOf('9'), CH_0 + 9);
    CHECK_EQ(glyphOf('.'), CH_DOT);
    CHECK_EQ(glyphOf(','), CH_COMMA);
    CHECK_EQ(glyphOf('!'), CH_BANG);
    CHECK_EQ(glyphOf('?'), CH_QUERY);
    CHECK_EQ(glyphOf('\''), CH_APOS);
    CHECK_EQ(glyphOf('-'), CH_DASH);
    CHECK_EQ(glyphOf(':'), CH_COLON);
    CHECK_EQ(glyphOf('/'), CH_SLASH);
    CHECK_EQ(glyphOf(' '), CH_BLANK);       // an opaque box background
    CHECK_EQ(glyphOf('#'), CH_BLANK);       // unmapped: a hole, not garbage
    CHECK_EQ(glyphOf('('), CH_BLANK);
}

KH_TEST(text_opens_holding_the_button_that_opened_it) {
    // txtHold: a press is not accepted until the button is released, or the same
    // press that opened a box would dismiss it.  text.s:92-93, 308-317.
    Dialogue d;
    Pad pad;
    d.open(scriptOf(SCRIPT_ONE_PAGE, sizeof SCRIPT_ONE_PAGE), TextMode::Message);
    CHECK(d.busy());
    CHECK(d.state() == TextState::Reveal);

    // Hold A from the frame the box opened.  It must not fast-forward.
    pad.held = raw(Button::A);
    pad.pressed = raw(Button::A);
    d.update(pad);
    d.update(pad);
    CHECK(d.state() == TextState::Reveal);
    char buf[256];
    CHECK(std::strcmp(pageOf(d, buf), "H") == 0);   // one character, not the page

    // Release, and now a press is accepted.
    pad.held = 0;
    pad.pressed = 0;
    d.update(pad);
    tapA(d, pad);
    CHECK(d.state() == TextState::Wait);
    CHECK(std::strcmp(pageOf(d, buf), "HI.") == 0);
}

KH_TEST(text_reveals_one_character_every_two_frames) {
    // REVEAL_DELAY = 1 means emit, then wait one frame: one character every two.
    // text.s:21, 356-360.
    Dialogue d;
    Pad pad;
    d.open(scriptOf(SCRIPT_ONE_PAGE, sizeof SCRIPT_ONE_PAGE), TextMode::Message);
    char buf[256];

    d.update(pad);                                  // 'H'
    CHECK(std::strcmp(pageOf(d, buf), "H") == 0);
    d.update(pad);                                  // the delay frame
    CHECK(std::strcmp(pageOf(d, buf), "H") == 0);
    d.update(pad);                                  // 'I'
    CHECK(std::strcmp(pageOf(d, buf), "HI") == 0);

    // "HI." plus SC_END is 4 bytes, so 7 frames from open: 4 emits, 3 delays.
    d.open(scriptOf(SCRIPT_ONE_PAGE, sizeof SCRIPT_ONE_PAGE), TextMode::Message);
    CHECK_EQ(settle(d, pad), 7);
    CHECK(d.state() == TextState::Wait);
}

KH_TEST(text_a_press_dumps_the_rest_of_the_page) {
    // RevealAll: A or B fast-forwards the whole page in one frame.
    // text.s:482-496.
    Dialogue d;
    Pad pad;
    d.open(scriptOf(SCRIPT_INTRO, sizeof SCRIPT_INTRO), TextMode::Message);
    d.update(pad);                                  // let hold_ clear, emit 'S'
    tapA(d, pad);
    CHECK(d.state() == TextState::Wait);
    char buf[256];
    CHECK(std::strcmp(pageOf(d, buf),
                      "SO MUCH TO DO,\n"
                      "SO LITTLE TIME.\n"
                      "\n"
                      "TAKE YOUR TIME.") == 0);
}

KH_TEST(text_sc_page_pages_instead_of_closing) {
    // THE DIVERGENCE.  On the SNES, TS_WAIT + a press + TM_MESSAGE closed the
    // box unconditionally, and SC_PAGE also set TS_WAIT -- so a page break ended
    // the message and 63% of the game's dialogue was unreachable.  Verified on
    // hardware-accurate emulation before changing it.
    // See docs/behaviour/divergences/005-sc-page.md.
    Dialogue d;
    Pad pad;
    char buf[256];
    d.open(scriptOf(SCRIPT_INTRO, sizeof SCRIPT_INTRO), TextMode::Message);
    settle(d, pad);
    CHECK(d.state() == TextState::Wait);
    CHECK(d.moreToCome());                          // stopped at SC_PAGE, not SC_END

    tapA(d, pad);
    CHECK(d.busy());                                // it did NOT close
    CHECK(d.state() == TextState::Reveal);
    CHECK(std::strcmp(pageOf(d, buf), "") == 0);    // and the area was cleared

    settle(d, pad);
    CHECK(d.state() == TextState::Wait);
    CHECK(d.moreToCome());
    CHECK(std::strcmp(pageOf(d, buf),
                      "DON'T BE AFRAID.\n"
                      "\n"
                      "THE DOOR IS STILL SHUT.") == 0);

    tapA(d, pad);
    settle(d, pad);
    CHECK(d.state() == TextState::Wait);
    CHECK(!d.moreToCome());                         // this one really is the end
    CHECK(std::strcmp(pageOf(d, buf), "POWER SLEEPS.") == 0);

    tapA(d, pad);
    CHECK(!d.busy());                               // and NOW it closes
}

KH_TEST(text_close_eats_the_dismissing_press) {
    // TextClose masks A and B out of padPressed, so the press that closed a
    // message cannot start the next conversation.  text.s:104-120.  This is a
    // write to shared input state and scenes depend on it.
    Dialogue d;
    Pad pad;
    d.open(scriptOf(SCRIPT_ONE_PAGE, sizeof SCRIPT_ONE_PAGE), TextMode::Message);
    settle(d, pad);

    pad.held = raw(Button::A) | raw(Button::Up);
    pad.pressed = raw(Button::A) | raw(Button::Up);
    d.update(pad);
    CHECK(!d.busy());
    CHECK(!pad.wasPressed(Button::A));              // eaten
    CHECK(pad.wasPressed(Button::Up));              // and only A and B are
    CHECK(pad.isHeld(Button::A));                   // held is untouched
}

KH_TEST(text_wraps_at_the_column_and_clamps_at_the_last_line) {
    // PutChar wraps mid-word at TEXT_W; NewLine clamps at TEXT_H - 1 rather than
    // scrolling, so a script with too many lines overwrites its last one.
    // text.s:241-295.
    uint8_t script[TEXT_W * 2 + 2];
    for (int i = 0; i < TEXT_W * 2; ++i) script[i] = uint8_t('A' + (i % 26));
    script[TEXT_W * 2] = SC_END;
    Dialogue d;
    Pad pad;
    d.open(scriptOf(script, sizeof script), TextMode::Message);
    settle(d, pad);
    CHECK_EQ(std::strlen(d.row(0)), size_t(TEXT_W));
    CHECK_EQ(std::strlen(d.row(1)), size_t(TEXT_W));
    CHECK_EQ(d.cursorRow(), 2);                     // wrapped exactly twice

    // Now overflow the line count: TEXT_H newlines past the end.
    uint8_t many[TEXT_H + 4];
    for (int i = 0; i < TEXT_H + 3; ++i) many[i] = SC_NL;
    many[TEXT_H + 3] = SC_END;
    d.open(scriptOf(many, sizeof many), TextMode::Message);
    settle(d, pad);
    CHECK_EQ(d.cursorRow(), TEXT_H - 1);            // clamped, not out of range
}

KH_TEST(text_a_script_with_no_terminator_gives_up) {
    // The 512-iteration backstop, and the bounds check that makes it reachable.
    Dialogue d;
    Pad pad;
    d.open(scriptOf(SCRIPT_RUNAWAY, sizeof SCRIPT_RUNAWAY), TextMode::Message);
    CHECK_EQ(settle(d, pad, 64), 7);                // 3 chars, then it stops
    CHECK(d.state() == TextState::Wait);
    CHECK(!d.moreToCome());
    char buf[256];
    CHECK(std::strcmp(pageOf(d, buf), "ABC") == 0);
}

KH_TEST(text_prompt_wraps_and_reports_one_based) {
    // TS_PROMPT: up and down wrap, and txtResult is choice + 1 so that 0 can
    // mean "no answer yet".  text.s:378-420.
    Dialogue d;
    Pad pad;
    d.open(scriptOf(SCRIPT_ONE_PAGE, sizeof SCRIPT_ONE_PAGE), TextMode::Prompt);
    settle(d, pad);
    CHECK(d.state() == TextState::Wait);
    tapA(d, pad);
    CHECK(d.state() == TextState::Prompt);          // a prompt does not close
    CHECK_EQ(d.choice(), 0);
    CHECK_EQ(d.result(), 0);                        // unanswered

    auto tap = [&](Button b) {
        pad.held = raw(b);
        pad.pressed = raw(b);
        d.update(pad);
        pad.held = 0;
        pad.pressed = 0;
    };
    tap(Button::Down);
    CHECK_EQ(d.choice(), 1);
    tap(Button::Down);
    CHECK_EQ(d.choice(), 0);                        // wrapped past the bottom
    tap(Button::Up);
    CHECK_EQ(d.choice(), 1);                        // wrapped past the top

    tap(Button::A);
    CHECK(!d.busy());
    CHECK_EQ(d.result(), 2);                        // 1-based

    // The raft menu has three options, so it wraps at three.
    d.open(scriptOf(SCRIPT_ONE_PAGE, sizeof SCRIPT_ONE_PAGE), TextMode::Raft);
    CHECK_EQ(menuCount(TextMode::Raft), 3);
    CHECK_EQ(menuCount(TextMode::Prompt), 2);
    settle(d, pad);
    tapA(d, pad);
    tap(Button::Up);
    CHECK_EQ(d.choice(), 2);                        // straight to the last one
}

KH_TEST(text_up_beats_confirm_on_the_same_frame) {
    // The dispatch order in TS_PROMPT is up, then down, then confirm, and each
    // returns -- so a frame carrying both Up and A moves the cursor and does not
    // answer.  This is the kind of thing a reordered port silently changes.
    Dialogue d;
    Pad pad;
    d.open(scriptOf(SCRIPT_ONE_PAGE, sizeof SCRIPT_ONE_PAGE), TextMode::Prompt);
    settle(d, pad);
    tapA(d, pad);
    CHECK(d.state() == TextState::Prompt);

    pad.held = raw(Button::Up) | raw(Button::A);
    pad.pressed = raw(Button::Up) | raw(Button::A);
    d.update(pad);
    CHECK(d.busy());                                // not answered
    CHECK_EQ(d.result(), 0);
    CHECK_EQ(d.choice(), 1);                        // moved instead
}
