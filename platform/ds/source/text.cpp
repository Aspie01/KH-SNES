#include "text.h"

namespace kh {

uint8_t glyphOf(char c) {
    if (c >= 'a' && c <= 'z') c = char(c - 'a' + 'A');
    if (c >= 'A' && c <= 'Z') return uint8_t(CH_A + (c - 'A'));
    if (c >= '0' && c <= '9') return uint8_t(CH_0 + (c - '0'));
    switch (c) {
        case '.': return CH_DOT;
        case ',': return CH_COMMA;
        case '!': return CH_BANG;
        case '?': return CH_QUERY;
        case '\'': return CH_APOS;
        case '-': return CH_DASH;
        case ':': return CH_COLON;
        case '/': return CH_SLASH;
        default: return CH_BLANK;       // including space, which is an opaque box
    }
}

void Dialogue::clearPage() {
    for (int r = 0; r < TEXT_H; ++r) {
        for (int c = 0; c < TEXT_W; ++c) page_[r][c] = ' ';
        page_[r][TEXT_W] = '\0';
    }
    col_ = 0;
    row_ = 0;
}

void Dialogue::open(Script script, TextMode mode) {
    script_ = script;
    ptr_ = 0;
    mode_ = mode;
    state_ = TextState::Reveal;
    choice_ = 0;
    result_ = 0;
    delay_ = 0;
    dirty_ = false;
    more_ = false;
    hold_ = 1;              // ignore the button that opened the box
    clearPage();            // DrawFrame + ClearTextArea
}

void Dialogue::close(Pad& pad) {
    state_ = TextState::Closed;
    more_ = false;
    // Eat the button that dismissed the box.  Scene scripts test padPressed
    // after this runs, and without it the press that closed a message would
    // immediately start the next conversation.
    pad.consume(ADVANCE_BUTTONS);
}

void Dialogue::put(char c) {
    if (row_ < TEXT_H && col_ < TEXT_W) page_[row_][col_] = c;
    ++col_;
    // No word wrap: the SNES wrapped mid-word at column TEXT_W and so does this.
    if (col_ >= TEXT_W) newLine();
    dirty_ = true;
}

void Dialogue::newLine() {
    col_ = 0;
    ++row_;
    // Clamped, not scrolled: a page break is what should follow, and if the
    // script does not provide one the last line is overwritten.  That is the
    // shipped behaviour and it is a script bug when it shows.
    if (row_ >= TEXT_H) row_ = TEXT_H - 1;
}

void Dialogue::emitOne() {
    if (ptr_ >= script_.size) {         // a script with no terminator
        state_ = TextState::Wait;
        more_ = false;
        return;
    }
    const uint8_t b = script_.data[ptr_];
    if (b >= 32) {
        put(char(b));
        ++ptr_;
        return;
    }
    if (b == SC_END) {
        // Deliberately does NOT step ptr_: text.s left txtPtr on the terminator
        // and nothing reads it again, but a re-open would otherwise skip a byte.
        state_ = TextState::Wait;
        more_ = false;
        return;
    }
    if (b == SC_NL) {
        ++ptr_;
        newLine();
        return;
    }
    // SC_PAGE, and any other unknown code, which text.s also fell through to
    // here -- so an unrecognised byte pages rather than being drawn.
    ++ptr_;
    state_ = TextState::Wait;
    more_ = true;
}

void Dialogue::revealAll() {
    // Fast-forward: dump the rest of the page in one frame.  Bounded, because a
    // script with no terminator would otherwise spin here.
    for (int i = 0; i < REVEAL_BACKSTOP && state_ == TextState::Reveal; ++i)
        emitOne();
}

void Dialogue::update(Pad& pad) {
    if (state_ == TextState::Closed) return;

    // A press is only accepted once the button that opened the box has been
    // released.  Without this the same press would open and dismiss a message
    // in the same frame.
    bool pressed = false;
    if (hold_) {
        if (!(pad.held & raw(ADVANCE_BUTTONS))) hold_ = 0;
    } else {
        pressed = (pad.pressed & raw(ADVANCE_BUTTONS)) != 0;
    }

    switch (state_) {
        case TextState::Reveal:
            if (pressed) {
                revealAll();
            } else if (delay_) {
                --delay_;
            } else {
                emitOne();
                delay_ = uint8_t(REVEAL_DELAY);
            }
            return;

        case TextState::Wait:
            if (!pressed) return;
            // PAGING.  On the SNES every press here closed the box, including
            // one at an SC_PAGE, so 63% of the game's dialogue was unreachable.
            // See docs/behaviour/divergences/005-sc-page.md.
            if (more_) {
                clearPage();
                more_ = false;
                delay_ = 0;
                state_ = TextState::Reveal;
                return;
            }
            if (mode_ == TextMode::Message) {
                close(pad);
            } else {
                state_ = TextState::Prompt;      // DrawOptions
            }
            return;

        case TextState::Prompt: {
            const int n = menuCount(mode_);
            // Up is tested first and returns, then down, then the confirm.  So a
            // frame carrying both Up and A moves the cursor and does not confirm.
            if (pad.wasPressed(Button::Up)) {
                choice_ = int8_t((choice_ == 0 ? n : choice_) - 1);
                return;
            }
            if (pad.wasPressed(Button::Down)) {
                choice_ = int8_t(choice_ + 1 >= n ? 0 : choice_ + 1);
                return;
            }
            if (pressed) {
                result_ = int8_t(choice_ + 1);    // 1-based; 0 means unanswered
                close(pad);
            }
            return;
        }

        case TextState::Closed:
            return;
    }
}

size_t Dialogue::text(char* out, size_t cap) const {
    size_t n = 0;
    int last = -1;
    for (int r = 0; r < TEXT_H; ++r)
        for (int c = 0; c < TEXT_W; ++c)
            if (page_[r][c] != ' ') last = r;
    for (int r = 0; r <= last; ++r) {
        int end = TEXT_W;
        while (end > 0 && page_[r][end - 1] == ' ') --end;
        for (int c = 0; c < end; ++c)
            if (n + 1 < cap) out[n++] = page_[r][c];
        if (r != last && n + 1 < cap) out[n++] = '\n';
    }
    if (cap) out[n < cap ? n : cap - 1] = '\0';
    return n;
}

}  // namespace kh
