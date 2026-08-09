// The touchscreen, folded into the pad.

#include "touch.h"

#include "constants.h"

namespace kh::device {

static_assert(TOUCH_W == SCREEN_W && TOUCH_H == SCREEN_H,
              "the two screens are the same size; if that ever stops being "
              "true, a region written in one's coordinates is wrong in the "
              "other's and nothing about the mistake is visible");

const TouchRegion ADVANCE_REGION[1] = {
    {0, 0, TOUCH_W, TOUCH_H, Button::A,
     "tap anywhere to advance a message -- a dialogue box has no target worth "
     "aiming at, and making the player hit one would be worse than the button "
     "they already have"},
};

void TouchState::clear() {
    at_ = TouchPoint{};
    down_ = false;
    wasDown_ = false;
    everDown_ = false;
}

void TouchState::update(bool down, int16_t x, int16_t y) {
    wasDown_ = down_;
    down_ = down;
    // LATCH ONLY WHILE DOWN.  With the pen up there is no contact to measure
    // and the reading is noise; taking it on the release frame is the classic
    // DS input bug, where the tap registers somewhere the player never touched
    // and it reads as a calibration fault rather than a timing one.
    if (down) at_ = TouchPoint{x, y};
    if (down) everDown_ = true;
    // ...and before the first contact there is no last position either.  Zero
    // is a legal coordinate -- the top-left pixel -- so a region covering it
    // would fire on the first frame of a scene without anybody touching
    // anything.  everDown_ is what keeps at() from meaning "the corner".
    if (!everDown_) at_ = TouchPoint{-1, -1};
}

bool inRegion(const TouchRegion& r, TouchPoint p) {
    // Half-open on the far edges: two regions sharing a boundary must not both
    // claim it, or which one wins depends on their order in a table whose order
    // is supposed to mean priority and not tie-breaking.
    return p.x >= r.x && p.x < r.x + r.w
        && p.y >= r.y && p.y < r.y + r.h;
}

Button touchButtons(const TouchState& t, const TouchRegion* regions, int n) {
    if (!t.down() && !t.released()) return Button::None;
    const TouchPoint p = t.at();
    for (int i = 0; i < n; ++i)
        if (inRegion(regions[i], p)) return regions[i].button;
    return Button::None;
}

void applyTouch(Pad& pad, const TouchState& t, const TouchRegion* regions,
                int n) {
    // THE PRESS EDGE ONLY.  A held finger is not a held button: a touchscreen
    // has no autorepeat, and a player resting a thumb on the glass would
    // otherwise hold A down forever -- which inside a dialogue box means every
    // message advances on the frame it opens and the whole conversation goes
    // past in six frames.
    if (!t.pressed()) return;
    const Button b = touchButtons(t, regions, n);
    if (b == Button::None) return;

    // Both words, and `held` matters as much as `pressed`.  Anything that tests
    // isHeld() on the frame of a press -- and the SNES's own code does, because
    // padPressed is a subset of padHeld by construction -- would otherwise see a
    // press with nothing held, which is a state the hardware can never produce.
    pad.pressed = uint16_t(pad.pressed | raw(b));
    pad.held = uint16_t(pad.held | raw(b));
}

}  // namespace kh::device
