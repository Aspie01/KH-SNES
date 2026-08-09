// The touchscreen, and the constraint that shapes it.
//
// There is no oracle here -- the SNES had no touchscreen -- so most of these
// cases pin a DECISION rather than a behaviour. The decision is the one in
// device/touch.h: a touch synthesises buttons and does nothing else, because
// tools/trace_check.py's guarantee is "same input, same state" and the recorded
// scripts have no touch column. Let the simulation be moved by a pen and that
// guarantee silently becomes "...with the pen up".
//
// Which makes the most important case in this file the one that asserts a
// negative: that a touch cannot produce anything a button could not.

#include "actor.h"
#include "check.h"
#include "constants.h"
#include "pad.h"
#include "text.h"
#include "touch.h"

using namespace kh;
using namespace kh::device;

namespace {

// A frame of the pen. Down at a position, then up, is two calls -- and the
// position on the second is deliberately garbage, because that is what the
// hardware gives you with nothing touching it.
void press(TouchState& t, int x, int y) {
    t.update(true, int16_t(x), int16_t(y));
}
void release(TouchState& t) {
    t.update(false, -9999, -9999);      // noise, as the real thing returns
}

}  // namespace

KH_TEST(touch_synthesises_only_buttons_and_never_anything_else) {
    // THE CASE THAT MATTERS.  Everything a touch can do arrives as a Button, so
    // there is no path by which a pen can reach the simulation except one a
    // recorded pad script could have taken too.  That is what keeps every
    // scenario in trace_check.py proving what it claims.
    //
    // Asserted over the whole screen rather than at a few points: a region
    // table is data, and a single entry that resolved to something else would
    // be a hole in the argument rather than a bug in a corner.
    TouchState t;
    for (int y = 0; y < TOUCH_H; y += 8)
        for (int x = 0; x < TOUCH_W; x += 8) {
            t.clear();
            press(t, x, y);
            const Button b = touchButtons(t, ADVANCE_REGION, ADVANCE_REGION_COUNT);
            // Every result is a single valid Button from pad.h's enum -- one
            // bit, and one that pad.s could have set.
            const uint16_t bits = raw(b);
            CHECK((bits & (bits - 1)) == 0);        // at most one bit
            CHECK((bits & 0x000F) == 0);            // bits 0-3 are not buttons
        }
}

KH_TEST(touch_fires_on_the_press_edge_and_not_while_held) {
    // A held finger is not a held button.  There is no autorepeat on a
    // touchscreen, and a player resting a thumb on the glass would otherwise
    // hold A down forever -- which in a dialogue box means every message
    // advances on the frame it opens and the conversation goes past in six.
    TouchState t;
    Pad pad;

    press(t, 128, 96);
    CHECK(t.pressed());
    applyTouch(pad, t, ADVANCE_REGION, ADVANCE_REGION_COUNT);
    CHECK(pad.wasPressed(Button::A));

    // ...and every frame after, while the finger stays down, nothing.
    for (int f = 0; f < 30; ++f) {
        pad = Pad{};
        press(t, 128, 96);
        CHECK(t.down());
        CHECK(!t.pressed());
        applyTouch(pad, t, ADVANCE_REGION, ADVANCE_REGION_COUNT);
        CHECK(!pad.wasPressed(Button::A));
        CHECK(!pad.isHeld(Button::A));
    }

    // Lift and tap again: a second press, because that is a second input.
    release(t);
    pad = Pad{};
    applyTouch(pad, t, ADVANCE_REGION, ADVANCE_REGION_COUNT);
    CHECK(!pad.wasPressed(Button::A));
    press(t, 128, 96);
    applyTouch(pad, t, ADVANCE_REGION, ADVANCE_REGION_COUNT);
    CHECK(pad.wasPressed(Button::A));
}

KH_TEST(touch_sets_held_as_well_as_pressed) {
    // padPressed is a SUBSET of padHeld by construction on the hardware -- a
    // button cannot become held without being held -- and the SNES's own code
    // tests isHeld() on the frame of a press.  A synthesised press with nothing
    // held would be a state the hardware can never produce, and the first thing
    // to notice would be whatever tests the two together.
    TouchState t;
    Pad pad;
    press(t, 10, 10);
    applyTouch(pad, t, ADVANCE_REGION, ADVANCE_REGION_COUNT);
    CHECK(pad.wasPressed(Button::A));
    CHECK(pad.isHeld(Button::A));
    CHECK((pad.pressed & ~pad.held) == 0);      // the invariant, stated
}

KH_TEST(touch_latches_the_position_because_a_release_reads_noise) {
    // The touchscreen returns nothing useful with the pen up -- there is no
    // contact to measure.  Sampling on the release frame is the classic DS
    // input bug: the tap registers where the player never touched, usually a
    // corner, and it reads as a calibration fault rather than a timing one.
    TouchState t;
    press(t, 200, 150);
    CHECK_EQ(int(t.at().x), 200);
    CHECK_EQ(int(t.at().y), 150);

    release(t);                         // the driver hands over -9999, -9999
    CHECK(t.released());
    CHECK_EQ(int(t.at().x), 200);       // ...and the latch is what survives
    CHECK_EQ(int(t.at().y), 150);
}

KH_TEST(touch_before_the_first_contact_is_nowhere_and_not_the_corner) {
    // Zero is a legal coordinate -- the top-left pixel -- so a freshly
    // constructed TouchState reporting (0, 0) would sit inside any region
    // covering the corner.  The advance region covers the whole screen, so that
    // is every region there is: a scene would open and dismiss its own first
    // message before a frame had been drawn.
    TouchState t;
    CHECK(!t.down());
    CHECK(!t.pressed());
    CHECK(!t.released());
    t.update(false, 0, 0);
    CHECK_EQ(int(t.at().x), -1);
    CHECK_EQ(int(t.at().y), -1);
    CHECK(!inRegion(ADVANCE_REGION[0], t.at()));

    Pad pad;
    applyTouch(pad, t, ADVANCE_REGION, ADVANCE_REGION_COUNT);
    CHECK_EQ(int(pad.pressed), 0);
}

KH_TEST(touch_regions_are_half_open_so_a_shared_edge_belongs_to_one) {
    // Two regions sharing a boundary must not both claim it, or which one wins
    // depends on their order in a table whose order is supposed to mean
    // priority rather than tie-breaking.
    const TouchRegion left{0, 0, 128, 192, Button::Left, "the left half"};
    const TouchRegion right{128, 0, 128, 192, Button::Right, "the right half"};
    CHECK(inRegion(left, TouchPoint{127, 0}));
    CHECK(!inRegion(left, TouchPoint{128, 0}));
    CHECK(inRegion(right, TouchPoint{128, 0}));
    CHECK(!inRegion(right, TouchPoint{256, 0}));
    // ...and the near edges are inclusive, so the first pixel is inside.
    CHECK(inRegion(left, TouchPoint{0, 0}));
    CHECK(!inRegion(left, TouchPoint{-1, 0}));
}

KH_TEST(touch_takes_the_first_matching_region_so_order_is_priority) {
    // A small region inside a larger one is a real layout -- a button on a
    // panel -- and the only sane rule is that the author writes the specific
    // one first.  Stated rather than sorted, because sorting would need an
    // ordering on rectangles and there is not an obvious one.
    const TouchRegion table[] = {
        {100, 100, 20, 20, Button::B, "the small one, written first"},
        {0, 0, 256, 192, Button::A, "everything else"},
    };
    TouchState t;
    press(t, 110, 110);
    CHECK(touchButtons(t, table, 2) == Button::B);
    t.clear();
    press(t, 10, 10);
    CHECK(touchButtons(t, table, 2) == Button::A);
}

KH_TEST(touch_advances_a_real_dialogue_box_exactly_as_a_button_does) {
    // The one interaction wired today, end to end against the real interpreter
    // -- and driven in LOCKSTEP rather than compared at the end, because what
    // is being demonstrated is that nothing in text.cpp can tell the two apart
    // on any frame, not merely that both eventually close the box.
    //
    // Two presses, not one: the first reveals the rest of the message and the
    // second dismisses it, which is txtHold behaving as BEHAVIOUR.md section 13
    // describes.  A tap has to do both, in the same order, on the same frames.
    const uint8_t script[] = {CH_A, CH_A, CH_A, CH_A, CH_A, SC_END};

    Dialogue byButton, byTouch;
    byButton.open(Script{script, sizeof script}, TextMode::Message);
    byTouch.open(Script{script, sizeof script}, TextMode::Message);
    TouchState t;

    // Press on frames 20 and 40; the frames between are the reveal running.
    // The touch side lifts the frame after each press, because a held finger
    // must not autorepeat -- which is the property this schedule also exercises.
    int agreed = 0;
    for (int f = 0; f < 60; ++f) {
        const bool pressNow = (f == 20 || f == 40);

        Pad padB;
        if (pressNow) { padB.pressed = raw(Button::A); padB.held = raw(Button::A); }

        Pad padT;
        if (pressNow) t.update(true, 128, 96); else t.update(false, -9999, -9999);
        applyTouch(padT, t, ADVANCE_REGION, ADVANCE_REGION_COUNT);

        // The pads are indistinguishable BEFORE the box sees them, which is the
        // whole design: there is nothing for the simulation to tell apart.
        CHECK_EQ(int(padB.pressed), int(padT.pressed));
        CHECK_EQ(int(padB.held), int(padT.held));

        byButton.update(padB);
        byTouch.update(padT);
        CHECK_EQ(byButton.busy(), byTouch.busy());
        if (byButton.busy() == byTouch.busy()) ++agreed;
    }
    CHECK_EQ(agreed, 60);
    // ...and the two presses really did dismiss it, so this was not sixty
    // frames of agreeing that nothing happened.
    CHECK(!byButton.busy());
    CHECK(!byTouch.busy());
}

KH_TEST(touch_advance_is_one_button_and_not_both) {
    // ADVANCE_BUTTONS is A|B and text.s tested them together, but a tap
    // synthesises only A.  Two buttons for one tap would make consume(A) leave
    // B pressed, and the next thing to test B would see a press nobody made --
    // which is the exact failure consume() exists to prevent, reintroduced by
    // the thing meant to be a convenience.
    CHECK(ADVANCE_REGION[0].button == Button::A);
    CHECK(raw(ADVANCE_BUTTONS) != raw(Button::A));

    Pad pad;
    TouchState t;
    press(t, 5, 5);
    applyTouch(pad, t, ADVANCE_REGION, ADVANCE_REGION_COUNT);
    pad.consume(Button::A);
    CHECK_EQ(int(pad.pressed), 0);      // nothing left over
}

KH_TEST(touch_every_region_says_why_it_exists) {
    // A region is an undocumented control: there is no label on a touchscreen
    // unless something draws one, so the reason has to travel with the
    // rectangle or the next person cannot tell a deliberate hit area from a
    // leftover.
    for (int i = 0; i < ADVANCE_REGION_COUNT; ++i) {
        CHECK(ADVANCE_REGION[i].why != nullptr);
        CHECK(ADVANCE_REGION[i].why[0] != '\0');
        CHECK(ADVANCE_REGION[i].w > 0 && ADVANCE_REGION[i].h > 0);
        // ...and it is on the screen it claims to be on.
        CHECK(ADVANCE_REGION[i].x >= 0 && ADVANCE_REGION[i].y >= 0);
        CHECK(ADVANCE_REGION[i].x + ADVANCE_REGION[i].w <= TOUCH_W);
        CHECK(ADVANCE_REGION[i].y + ADVANCE_REGION[i].h <= TOUCH_H);
    }
}

KH_TEST(touch_maps_to_the_cell_grid_the_bottom_screen_is_drawn_in) {
    // Regions get written in pixels, but the bottom screen's furniture is a
    // 32x32 grid of 8x8 characters, so a hit area meant to line up with drawn
    // text has to be expressible in the same units it was drawn in.
    TouchState t;
    press(t, 0, 0);
    CHECK_EQ(t.cellX(), 0);
    CHECK_EQ(t.cellY(), 0);
    press(t, 7, 7);
    CHECK_EQ(t.cellX(), 0);
    CHECK_EQ(t.cellY(), 0);
    press(t, 8, 8);
    CHECK_EQ(t.cellX(), 1);
    CHECK_EQ(t.cellY(), 1);
    press(t, TOUCH_W - 1, TOUCH_H - 1);
    CHECK_EQ(t.cellX(), 31);            // 32 columns across
    CHECK_EQ(t.cellY(), 23);            // ...and 24 rows on a 192-line screen
}
