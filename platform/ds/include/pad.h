#pragma once
// The controller, as the simulation sees it.
//
// Two words: what is held this frame, and what became held this frame.  That is
// the whole of pad.s -- it read the SNES's auto-joypad registers and derived
// padPressed as (now & ~last) -- and it is exactly what libnds gives you as
// keysHeld() and keysDown(), so the seam is a struct rather than a driver.
//
// The buttons keep their SNES names.  A DS mapping belongs in the device tier,
// beside the code that calls scanKeys(), and not in the simulation.

#include <cstdint>

namespace kh {

enum class Button : uint16_t {
    None  = 0,
    // Bit order is the SNES's own so that a recorded input trace replays on
    // either platform without a translation table.  See BEHAVIOUR.md §10.
    R      = 1 << 4,
    L      = 1 << 5,
    X      = 1 << 6,
    A      = 1 << 7,
    Right  = 1 << 8,
    Left   = 1 << 9,
    Down   = 1 << 10,
    Up     = 1 << 11,
    Start  = 1 << 12,
    Select = 1 << 13,
    Y      = 1 << 14,
    B      = 1 << 15,
};

constexpr uint16_t raw(Button b) { return static_cast<uint16_t>(b); }

constexpr Button operator|(Button a, Button b) {
    return static_cast<Button>(raw(a) | raw(b));
}

// A or B: the two buttons that advance a message.  text.s tested them together
// everywhere, so they are one constant here rather than an expression repeated.
constexpr Button ADVANCE_BUTTONS = Button::A | Button::B;

struct Pad {
    uint16_t held = 0;          // padHeld
    uint16_t pressed = 0;       // padPressed -- became held THIS frame

    constexpr bool isHeld(Button b) const { return (held & raw(b)) != 0; }
    constexpr bool wasPressed(Button b) const { return (pressed & raw(b)) != 0; }

    // TextClose masks A and B out of padPressed so that the press which
    // dismissed a box cannot also start the next conversation.  That is a WRITE
    // to shared input state, and it is load-bearing: without it, closing Kairi's
    // message immediately reopened it.  See BEHAVIOUR.md §13.
    void consume(Button b) { pressed = uint16_t(pressed & ~raw(b)); }
};

}  // namespace kh
