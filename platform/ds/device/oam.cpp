// The depth sort and the sprite table, ported from platform/snes/src/oam.s.

#include "oam.h"

#include "constants.h"

namespace kh::device {
namespace {

// A sprite's anchor is its BOTTOM CENTRE, so it stands on its position rather
// than hanging from it.  oam.s:430-449: large subtracts 16 and 32, small
// subtracts 8 and 16 -- half the width and the full height in each case.
constexpr int LARGE_W = 32, LARGE_H = 32;
constexpr int SMALL_W = 16, SMALL_H = 16;

// The cull, and the bias that makes it one comparison.
//
// oam.s:452-460 adds 32 and compares unsigned against SCREEN_H + 32, which
// "rejects both the negative and the past-the-edge cases at once".  Reproduced
// as the signed range it means, because C++ has signed comparison and writing
// the trick out again would be transcribing a workaround for an instruction set
// this machine does not have -- and would hide that the two are the same rule.
//
// THE RECTANGLE IS 32 LINES SHORTER HERE.  SCREEN_H is 192 and not 224, so the
// DS culls actors the SNES drew: an actor between y 192 and 224 is on screen
// there and off screen here.  That is divergence 001 reaching the sprite path,
// and it is the reason this is a port of the RULE and not of the numbers.
constexpr int CULL_MARGIN = 32;

bool onScreen(int x, int y) {
    return y + CULL_MARGIN >= 0 && y < SCREEN_H
        && x + CULL_MARGIN >= 0 && x < SCREEN_W;
}

// Where an actor's feet are on screen.  oam.s:376-391: world position shifted
// down four (Q12.4 to whole pixels) minus the camera, then lifted by the
// actor's height.
//
// "Standing on a raised deck lifts the sprite without moving the actor: the
// world stays one flat plane as far as the geometry is concerned" (oam.s:394).
// Eight pixels a height step, oam.s:404-407.
void feet(const Actors& a, int i, const Camera& cam, int& x, int& y) {
    x = (a.x[i].raw() >> 4) - cam.x;
    y = (a.y[i].raw() >> 4) - cam.y - int(a.z[i]) * 8;
}

constexpr uint8_t quadCount = 4;
// oam.s:360-362.  Offsets from the boss's feet: a 64x64 body standing on its
// own position, so the quadrants run from -32 to 0 across and -64 to -32 up.
constexpr int QUAD_X[quadCount] = {-32, 0, -32, 0};
constexpr int QUAD_Y[quadCount] = {-64, -64, -32, -32};

// THE SORT KEY, named once and used on both sides of the comparison.
//
// It is the actor's world Y and nothing else -- `lda actY,x` at oam.s:112 for
// the inserted actor and again at oam.s:129 for the one already in the slot.
// The GROUND position, before the height lift feet() applies: somebody standing
// on a raised deck keeps the depth of the deck he is standing on, and a sort
// that used the lifted position would draw him behind things he is south of.
//
// A FUNCTION AND NOT TWO EXPRESSIONS, which the first version of this file got
// wrong in a way nothing would have caught.  The insertion loop compared
// `a.y[out[j-1]].raw()` against a key computed as `a.y[i].raw()` -- the same
// comparison written twice, agreeing only because both happened to be the raw
// Y.  Change the key to add a bias or a tie-break and only one side follows,
// which is a sort that is subtly not a sort: not an ordering error you can see,
// but a comparator that is no longer transitive.
//
// It was found by a deliberate breakage that FAILED TO FIRE.  Mutating the key
// to subtract the height lift -- the mutation that stands for "sorted on screen
// Y" -- changed one side of the comparison, left the other alone, and produced
// the correct order by accident.  A probe that does not fire is worth as much
// as one that does.
int32_t sortKey(const Actors& a, int i) { return a.y[i].raw(); }

bool push(SpriteSlot* out, OamBuild& b, const SpriteSlot& s) {
    if (b.used >= OAM_SLOTS) {
        ++b.droppedToCeiling;
        return false;
    }
    out[b.used++] = s;
    return true;
}

}  // namespace

int buildSortList(const Actors& a, uint8_t* out) {
    int n = 0;
    // Slots in order, so the tie-break is the slot number.  oam.s:82-95.
    for (int i = 0; i < MAX_ACTORS; ++i) {
        if (a.type[i] == ActType::None) continue;

        // Insertion sort, and the SNES's reason still holds: "with at most 24
        // actors and near-sorted input frame to frame, insertion sort costs far
        // less than the setup for anything cleverer" (oam.s:101-102).  The DS
        // pool is four times larger, but the input is just as near-sorted --
        // actors move a few pixels a frame -- so the cost is still linear in
        // practice rather than quadratic.
        //
        // AND IT IS THE ONLY EASY WAY TO BE STABLE.  std::sort is not stable,
        // std::stable_sort allocates, and both are barred here anyway.
        const int32_t key = sortKey(a, i);
        int j = n;
        // `>=` and not `>`: stop at the first actor already at least as far
        // forward, which leaves equals in the order they were scanned.  This
        // one character is the whole tie-break.  oam.s:132's `bcs @place`.
        while (j > 0 && sortKey(a, out[j - 1]) < key) {
            out[j] = out[j - 1];
            --j;
        }
        out[j] = uint8_t(i);
        ++n;
    }
    return n;
}

OamBuild buildOam(const Actors& a, const Camera& cam, SpriteSlot* out) {
    OamBuild b;
    uint8_t order[MAX_ACTORS];
    b.actorsSorted = buildSortList(a, order);

    // --- pass one: the actors, front to back -------------------------------
    for (int k = 0; k < b.actorsSorted; ++k) {
        const int i = order[k];
        const ActFlags f = a.flags[i];

        // The boss is drawn by pass two, as four quadrants.  oam.s:398-402
        // returns WITHOUT advancing the slot cursor, so a huge actor costs
        // nothing here -- which is why the boss can be in the sort at all.
        if (has(f, ActFlags::Huge)) continue;

        const bool large = has(f, ActFlags::Large);
        int fx = 0, fy = 0;
        feet(a, i, cam, fx, fy);
        const int x = fx - (large ? LARGE_W / 2 : SMALL_W / 2);
        const int y = fy - (large ? LARGE_H : SMALL_H);

        // Culled BEFORE a slot is taken, exactly as oam.s:452-464 does -- the
        // `jsr WriteOamEntry` is inside the range check and tmp7 advances only
        // on a write.  So an off-screen actor is free, and the 128 ceiling is a
        // limit on what is DRAWN and not on what exists.  Getting this backwards
        // would make a large map run out of sprites while showing very few.
        if (!onScreen(x, y)) {
            ++b.culled;
            continue;
        }
        push(out, b, SpriteSlot{int16_t(x), int16_t(y), uint8_t(i),
                                SpriteKind::Actor, 0, large});
    }

    // --- pass two: the boss, behind every actor ----------------------------
    // Scanned in slot order rather than sort order, as oam.s:225-240 does; with
    // at most one huge actor alive the two are the same, and the assertion that
    // they are is the test's job rather than a runtime cost.
    for (int i = 0; i < MAX_ACTORS; ++i) {
        if (a.type[i] == ActType::None) continue;
        if (!has(a.flags[i], ActFlags::Huge)) continue;
        int fx = 0, fy = 0;
        feet(a, i, cam, fx, fy);
        for (uint8_t q = 0; q < quadCount; ++q) {
            const int x = fx + QUAD_X[q];
            const int y = fy + QUAD_Y[q];
            // Per quadrant, not per boss.  A 64x64 body straddling the edge has
            // some quadrants on screen and some off, and the SNES culls each
            // one on its own (WriteOamRaw's own range check) -- so a boss
            // half off the left edge costs two slots and not four.
            if (!onScreen(x, y)) {
                ++b.culled;
                continue;
            }
            push(out, b, SpriteSlot{int16_t(x), int16_t(y), uint8_t(i),
                                    SpriteKind::BossQuadrant, q, true});
        }
    }

    // --- pass three: the shadows, behind everything ------------------------
    // In SORT order (oam.s:186-206 walks sortIdx), which matters even though
    // they are all behind the actors: shadows overlap each other, and two
    // overlapping blobs drawn in a different order from their casters would
    // make the nearer one's shadow sit under the further one's.
    for (int k = 0; k < b.actorsSorted; ++k) {
        const int i = order[k];
        if (!has(a.flags[i], ActFlags::Shadow)) continue;
        int fx = 0, fy = 0;
        feet(a, i, cam, fx, fy);
        // A shadow sits on the GROUND, so it does not take the height lift --
        // the actor rises off its own shadow when it climbs.  feet() applied
        // the lift, so it is added back rather than recomputed, which keeps one
        // statement of the world-to-screen arithmetic.
        const int sy = fy + int(a.z[i]) * 8;
        const bool large = has(a.flags[i], ActFlags::Large);
        const int x = fx - (large ? LARGE_W / 2 : SMALL_W / 2);
        const int y = sy - (large ? LARGE_H : SMALL_H);
        if (!onScreen(x, y)) {
            ++b.culled;
            continue;
        }
        push(out, b, SpriteSlot{int16_t(x), int16_t(y), uint8_t(i),
                                SpriteKind::Shadow, 0, large});
    }

    return b;
}

}  // namespace kh::device
