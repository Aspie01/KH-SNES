#pragma once
// The dialogue interpreter.
//
// A script is a byte stream: anything >= 32 is a character, anything below is a
// control code.  That format is unchanged from text.s and the scripts port
// verbatim from its .byte runs -- see docs/BEHAVIOUR.md §13.
//
// What is NOT unchanged is SC_PAGE, which did not work.  See PAGING below and
// docs/behaviour/divergences/005-sc-page.md.
//
// The interpreter owns a PAGE BUFFER of characters rather than writing tilemap
// cells.  On the SNES the two were the same thing: PutChar wrote a glyph index
// with an attribute straight into txtBuf, the BG3 staging buffer.  Separating
// them is what lets the whole of this file be tested with no renderer at all,
// which is what §M5's exit criteria asks for -- a test can read back the page and
// compare it against the sentence it expects to see.

#include <cstddef>
#include <cstdint>

#include "constants.h"
#include "pad.h"

namespace kh {

// --- the script format -----------------------------------------------------
// Anything below 32 is a code, not a character.
constexpr uint8_t SC_END = 0x00;         // end of message
constexpr uint8_t SC_NL = 0x01;          // newline
constexpr uint8_t SC_PAGE = 0x02;        // wait, clear the text area, keep going

enum class TextState : uint8_t {
    Closed = 0,     // TS_CLOSED
    Reveal = 1,     // TS_REVEAL  characters appearing one at a time
    Wait = 2,       // TS_WAIT    a page or the message has finished
    Prompt = 3,     // TS_PROMPT  the selector is up
};

// Anything above Message is a prompt, and the mode doubles as the menu id --
// which is why Raft is a mode and not a flag.
enum class TextMode : uint8_t {
    Message = 0,    // TM_MESSAGE
    Prompt = 1,     // TM_PROMPT  yes/no
    Raft = 2,       // TM_RAFT    the three names
};

// A script, and its length.  A span rather than a bare pointer because the
// backstop in revealAll() needs to know where the bytes end: a script with no
// terminator ran 512 iterations and then gave up on the SNES, which is a
// backstop against a data bug rather than a design.
struct Script {
    const uint8_t* data = nullptr;
    size_t size = 0;

    constexpr bool empty() const { return data == nullptr || size == 0; }
};

// How many options each mode's menu shows.  MenuCount, as a function of the
// mode, because that is what it was.
constexpr int menuCount(TextMode m) {
    return m == TextMode::Raft ? 3 : 2;
}

class Dialogue {
public:
    void open(Script script, TextMode mode);
    void update(Pad& pad);
    void close(Pad& pad);

    // Carry set while a message is on screen.  Every scene gates its own update
    // on this, so it is the single most-called thing in the file.
    bool busy() const { return state_ != TextState::Closed; }

    TextState state() const { return state_; }
    TextMode mode() const { return mode_; }
    int choice() const { return choice_; }
    // 1-based; 0 means "no answer yet", so a scene can poll it.
    int result() const { return result_; }
    // ...and put it back to "no answer yet" once the scene has acted on it.
    //
    // THIS WAS THE MISSING HALF OF A DOCUMENTED CONTRACT.  interact.h's
    // diveInteract() says of its `answer` parameter: "the caller clears it
    // after, exactly as DiveUpdate does with txtResult" -- and there was no way
    // to.  open() zeroes result_ and nothing else did, so an answer survived
    // until the next box opened; a prompt whose yes-arm speaks is therefore
    // fine and one whose no-arm is silent re-answers itself on the following
    // frame, for ever.  dive.s:86 (`stz txtResult`, right after HandleAnswer)
    // is the line this is.
    void clearResult() { result_ = 0; }
    bool moreToCome() const { return more_; }

    // --- the page, for tests and for the renderer --------------------------
    // Row-major, TEXT_H rows of TEXT_W characters, space-filled.  ASCII, not
    // glyph indices: the glyph mapping is a rendering decision and glyphOf()
    // below is where it lives.
    const char* row(int r) const { return page_[r]; }
    int cursorCol() const { return col_; }
    int cursorRow() const { return row_; }
    // The page with trailing blanks trimmed and rows joined by '\n'.  Test
    // convenience; writes at most TEXT_H * (TEXT_W + 1) bytes plus a terminator.
    size_t text(char* out, size_t cap) const;

private:
    void emitOne();
    void revealAll();
    void put(char c);
    void newLine();
    void clearPage();

    Script script_{};
    size_t ptr_ = 0;                    // txtPtr, as an offset
    TextState state_ = TextState::Closed;
    TextMode mode_ = TextMode::Message;
    uint8_t delay_ = 0;                 // txtDelay
    uint8_t hold_ = 0;                  // txtHold
    int8_t choice_ = 0;                 // txtChoice
    int8_t result_ = 0;                 // txtResult
    bool dirty_ = false;                // txtDirty
    bool more_ = false;                 // stopped at SC_PAGE, not at SC_END
    int col_ = 0;                       // txtCol
    int row_ = 0;                       // txtRow
    char page_[TEXT_H][TEXT_W + 1]{};
};

// The glyph a character maps to in the 2 bpp font page, mirroring asciiToTile.
// Lower case folds onto upper -- the font has no lower case -- and anything
// unmapped becomes CH_BLANK, so an unexpected byte is a hole and not garbage.
uint8_t glyphOf(char c);

}  // namespace kh
