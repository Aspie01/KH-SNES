#pragma once
// §M7 step five: the touchscreen, and the constraint that decides its design.
//
// THIS IS THE FIRST INPUT THE SNES NEVER HAD, so unlike everything else in the
// device tier there is no oracle to be right against. That absence is not a
// licence -- it is the thing that has to be designed around, and the argument
// runs like this:
//
//   tools/trace_check.py is worth more than any feature in this milestone. It
//   drives both machines with the same recorded PAD input and requires the same
//   state, frame for frame, over eight scenarios. That is what has caught every
//   real defect in the simulation, and it is the only reason anybody can
//   believe the port behaves like the game.
//
//   Its guarantee is conditional: same input, same state. A touchscreen is a
//   SECOND INPUT CHANNEL, and the recorded scripts have no touch column. So the
//   moment the simulation can be moved by a touch, "the DS matches the oracle
//   given this input" stops being a statement about the DS and becomes a
//   statement about the DS with the pen up.
//
// So: A TOUCH SYNTHESISES BUTTONS AND DOES NOTHING ELSE. It is a shortcut for
// input the player could already have given, never a new capability. Nothing
// downstream of applyTouch() can tell a tap from a press, the simulation is
// unchanged, and every scenario in trace_check.py goes on proving exactly what
// it proved before.
//
// The cost is real and worth stating: an interaction that CANNOT be expressed
// as a button is one this port cannot have. Dragging the camera, pinch-zooming
// the 3D ground, drawing a spell -- all of those are off the table until
// somebody decides the oracle has finished its job. That is a trade, it is
// being made deliberately, and this comment is where it is recorded.
//
// WHAT IS ACTUALLY WIRED TODAY is one region: tap anywhere to advance a message.
// It needs no new design -- the box exists, the state machine exists, and the
// action is `Button::A`, which text.s already accepts. The command menu and the
// minimap would be the other two, and device/hud.h says why they are not here.

#include <cstdint>

#include "pad.h"

namespace kh::device {

// The bottom screen, in pixels. Same dimensions as the top; a touch is reported
// in its own coordinates and the two screens never share a coordinate space.
constexpr int TOUCH_W = 256;
constexpr int TOUCH_H = 192;

struct TouchPoint {
    int16_t x = 0;
    int16_t y = 0;
};

// One frame of the pen, with the same shape Pad has: what is true now, and what
// became true this frame. That parallel is deliberate -- pad.h's model came
// from pad.s and is the one everything here already understands.
class TouchState {
public:
    // `down` is libnds's KEY_TOUCH; `x` and `y` are the calibrated position,
    // which is MEANINGFUL ONLY WHILE DOWN.
    //
    // The touchscreen returns nothing useful with the pen up -- there is no
    // contact to measure -- so the value read on the release frame is noise.
    // Sampling it there is the classic DS input bug: the tap registers at a
    // position the player never touched, usually a corner, and it looks like a
    // mis-calibration rather than a timing mistake. So the position is LATCHED
    // while down and the latch is what at() returns, which is what makes a
    // release-triggered action land where the finger actually was.
    void update(bool down, int16_t x, int16_t y);

    bool down() const { return down_; }
    bool pressed() const { return down_ && !wasDown_; }
    bool released() const { return !down_ && wasDown_; }

    // The last position the pen was measured at. Valid on the release frame,
    // which is the whole reason it is latched.
    TouchPoint at() const { return at_; }

    // Which 8x8 character cell the touch is in -- the unit the bottom screen's
    // maps are addressed in, so a region can be written in the same coordinates
    // the HUD is drawn in rather than converted at every comparison.
    int cellX() const { return at_.x / 8; }
    int cellY() const { return at_.y / 8; }

    void clear();

private:
    TouchPoint at_{};
    bool down_ = false;
    bool wasDown_ = false;
    bool everDown_ = false;
};

// A rectangle of the bottom screen that stands for a button.
//
// `why` is not decoration. A region is an undocumented control -- there is no
// label on a touchscreen unless something draws one -- so the reason it exists
// has to travel with it or the next person cannot tell a deliberate hit area
// from a leftover.
struct TouchRegion {
    int16_t x, y, w, h;
    Button button;
    const char* why;
};

// True if the touch is inside the region. Half-open on the far edges, so two
// regions sharing a boundary do not both claim it.
bool inRegion(const TouchRegion& r, TouchPoint p);

// The buttons a touch synthesises this frame.
//
// FIRST MATCH WINS, and the order of the table is therefore the priority. That
// is stated rather than sorted because a smaller region inside a larger one is
// a real layout -- a button on a panel -- and the only sane rule is that the
// author writes the specific one first.
Button touchButtons(const TouchState& t, const TouchRegion* regions, int n);

// Fold a touch into the pad the simulation will see.
//
// ON THE PRESS EDGE, not while held. A held finger is not a held button: there
// is no autorepeat on a touchscreen and a player resting a thumb on the glass
// would otherwise hold A down forever, which in a dialogue box means every
// message advances the instant it opens.
//
// `pressed` gets the synthesised bits and `held` gets them for exactly the one
// frame, so consume() -- which text.cpp calls to stop the press that dismissed
// a box from starting the next conversation -- works on a tap identically.
void applyTouch(Pad& pad, const TouchState& t, const TouchRegion* regions, int n);

// The one region wired today: tap anywhere to advance a message.
//
// The whole screen, because a dialogue box has no target worth aiming at and
// making the player hit one would be worse than the button they already have.
// It resolves to A rather than to ADVANCE_BUTTONS because synthesising two
// buttons for one tap would make consume(Button::A) leave B pressed, and the
// next thing to test B would see a press nobody made.
extern const TouchRegion ADVANCE_REGION[1];
constexpr int ADVANCE_REGION_COUNT = 1;

}  // namespace kh::device
