// The depth sort, against the rules platform/snes/src/oam.s establishes.
//
// This is the first piece of the device tier with a real ORACLE. The screen
// initialisation had GBATEK and the ground streamer had arithmetic, but neither
// had a frozen implementation to be right or wrong against. Depth does: oam.s
// is 689 lines of 65816 that decided every one of these questions, and the DS's
// only job is to answer them the same way.
//
// So these cases are cited, not invented. Where the DS must differ -- and it
// must, in exactly two places -- the divergence is named and the reason is the
// screen being 32 lines shorter or the pool being four times larger, never a
// preference.

#include "actor.h"
#include "check.h"
#include "constants.h"
#include "grid.h"
#include "oam.h"

using namespace kh;
using namespace kh::device;

namespace {

SpriteSlot g_out[OAM_SLOTS];
uint8_t g_order[MAX_ACTORS];

Camera still() {
    Camera c;
    return c;                   // origin, so screen coordinates are world ones
}

// Spawn at whole pixels rather than tiles, because depth is about Y to the
// pixel and tileCentre() would quantise the very thing under test.
int at(Actors& a, ActType t, int px, int py) {
    return a.spawn(t, World::fromRaw(px << 4), World::fromRaw(py << 4));
}

// Both actors in the height case are Large, so one anchor undoes both.
constexpr int LARGE_ANCHOR = 32;

int countKind(const OamBuild& b, SpriteKind k) {
    int n = 0;
    for (int i = 0; i < b.used; ++i) if (g_out[i].kind == k) ++n;
    return n;
}

}  // namespace

KH_TEST(oamsort_orders_by_world_y_descending_frontmost_first) {
    // oam.s:6-9: "being further down the screen means being nearer, so sorting
    // actors by world Y descending and writing them out in that order makes a
    // character walk behind a palm when north of it and in front of it when
    // south, with no per-object layer authoring at all."
    Actors a;
    a.clear();
    const int north = at(a, ActType::Palm, 100, 40);
    const int south = at(a, ActType::Palm, 100, 200);
    const int mid = at(a, ActType::Sora, 100, 120);

    const int n = buildSortList(a, g_order);
    CHECK_EQ(n, 3);
    CHECK_EQ(int(g_order[0]), south);       // largest Y, frontmost, slot 0
    CHECK_EQ(int(g_order[1]), mid);
    CHECK_EQ(int(g_order[2]), north);

    // ...and the sentence the whole scheme exists for, as an assertion: Sora is
    // in front of the palm he is south of and behind the one he is north of.
    int soraAt = -1, northAt = -1, southAt = -1;
    for (int i = 0; i < n; ++i) {
        if (g_order[i] == mid) soraAt = i;
        if (g_order[i] == north) northAt = i;
        if (g_order[i] == south) southAt = i;
    }
    CHECK(soraAt < northAt);        // in front of the one to the north
    CHECK(soraAt > southAt);        // behind the one to the south
}

KH_TEST(oamsort_is_stable_and_the_tie_break_is_the_actor_slot) {
    // THE ONE CHARACTER THAT MATTERS.  oam.s:132's `cmp tmp0 / bcs @place`
    // stops shifting as soon as the occupying actor is at a Y at least as
    // large, so equals keep the order BuildSortList scanned them in -- and it
    // scans slots ascending (oam.s:82-95).
    //
    // Two actors on one row is not a corner case: it is the island's palm rows
    // and the Second District's wave arriving on one line.  An unstable sort
    // would be free to swap them differently on different frames, and two
    // sprites standing still would flicker past each other.
    Actors a;
    a.clear();
    const int first = at(a, ActType::Palm, 10, 100);
    const int second = at(a, ActType::RockBig, 60, 100);
    const int third = at(a, ActType::Rock, 110, 100);
    CHECK(first < second && second < third);

    const int n = buildSortList(a, g_order);
    CHECK_EQ(n, 3);
    CHECK_EQ(int(g_order[0]), first);
    CHECK_EQ(int(g_order[1]), second);
    CHECK_EQ(int(g_order[2]), third);

    // ...and it stays stable with the equals interleaved among distinct rows,
    // which is where a "sort the equal run separately" bug would show.
    a.clear();
    const int front = at(a, ActType::Sora, 0, 200);
    const int tieA = at(a, ActType::Palm, 0, 100);
    const int back = at(a, ActType::Rock, 0, 20);
    const int tieB = at(a, ActType::RockBig, 0, 100);
    const int m = buildSortList(a, g_order);
    CHECK_EQ(m, 4);
    CHECK_EQ(int(g_order[0]), front);
    CHECK_EQ(int(g_order[1]), tieA);        // lower slot first, though it was
    CHECK_EQ(int(g_order[2]), tieB);        // inserted before tieB existed
    CHECK_EQ(int(g_order[3]), back);
}

KH_TEST(oamsort_skips_the_holes_a_kill_leaves) {
    // The pool is scanned by slot and a dead actor is a hole, not a gap to be
    // closed -- world.s updates in slot order and allocation is a first-free
    // scan, so a slot IS state.
    Actors a;
    a.clear();
    at(a, ActType::Shadow, 0, 10);
    const int dead = at(a, ActType::Shadow, 0, 20);
    at(a, ActType::Shadow, 0, 30);
    a.type[dead] = ActType::None;

    const int n = buildSortList(a, g_order);
    CHECK_EQ(n, 2);
    for (int i = 0; i < n; ++i) CHECK(g_order[i] != uint8_t(dead));
}

KH_TEST(oamsort_emits_actors_then_the_boss_then_the_shadows) {
    // The three passes ARE the depth rule, and each is cited in oam.h.  The
    // boss is behind every actor (oam.s:213) because "it towers over Sora and
    // he fights at its feet"; the shadows are behind everything (oam.s:12)
    // "while still sitting above BG1".
    Actors a;
    a.clear();
    at(a, ActType::Sora, 100, 100);             // casts a shadow
    at(a, ActType::Darkside, 120, 150);         // Huge
    at(a, ActType::Shadow, 140, 80);            // casts a shadow

    const OamBuild b = buildOam(a, still(), g_out);
    CHECK(b.used > 0);
    CHECK_EQ(b.actorsSorted, 3);

    // Every Actor slot precedes every BossQuadrant, which precedes every Shadow.
    int lastActor = -1, firstQuad = OAM_SLOTS, lastQuad = -1, firstShadow = OAM_SLOTS;
    for (int i = 0; i < b.used; ++i) {
        if (g_out[i].kind == SpriteKind::Actor) lastActor = i;
        if (g_out[i].kind == SpriteKind::BossQuadrant) {
            if (i < firstQuad) firstQuad = i;
            lastQuad = i;
        }
        if (g_out[i].kind == SpriteKind::Shadow && i < firstShadow) firstShadow = i;
    }
    CHECK(lastActor < firstQuad);
    CHECK(lastQuad < firstShadow);
    CHECK_EQ(countKind(b, SpriteKind::BossQuadrant), 4);
    // The boss took no Actor slot of its own: oam.s:398-402 returns without
    // advancing the cursor for a Huge actor, which is what lets it be in the
    // sort at all.
    CHECK_EQ(countKind(b, SpriteKind::Actor), 2);
}

KH_TEST(oamsort_a_boss_goes_out_as_four_quadrants_around_its_feet) {
    // oam.s:360-362.  A 64x64 body standing on its own position: the quadrants
    // run -32..0 across and -64..-32 up.
    Actors a;
    a.clear();
    const int boss = at(a, ActType::Darkside, 128, 120);
    const OamBuild b = buildOam(a, still(), g_out);
    CHECK_EQ(countKind(b, SpriteKind::BossQuadrant), 4);

    const int wantX[4] = {128 - 32, 128, 128 - 32, 128};
    const int wantY[4] = {120 - 64, 120 - 64, 120 - 32, 120 - 32};
    int q = 0;
    for (int i = 0; i < b.used; ++i) {
        if (g_out[i].kind != SpriteKind::BossQuadrant) continue;
        CHECK_EQ(int(g_out[i].quadrant), q);
        CHECK_EQ(int(g_out[i].x), wantX[q]);
        CHECK_EQ(int(g_out[i].y), wantY[q]);
        CHECK_EQ(int(g_out[i].actor), boss);
        CHECK(g_out[i].large);
        ++q;
    }
    CHECK_EQ(q, 4);
}

KH_TEST(oamsort_a_sprite_stands_on_its_position_rather_than_hanging_from_it) {
    // oam.s:430-449: the anchor is the bottom centre -- half the width across
    // and the full height up.  Large is 32x32, small 16x16.
    Actors a;
    a.clear();
    at(a, ActType::Sora, 100, 100);         // Large
    a.clear();
    const int small = at(a, ActType::Shadow, 100, 100);
    CHECK(!has(flagsFor(ActType::Shadow), ActFlags::Large));
    OamBuild b = buildOam(a, still(), g_out);
    CHECK(b.used >= 1);
    CHECK_EQ(int(g_out[0].x), 100 - 8);
    CHECK_EQ(int(g_out[0].y), 100 - 16);
    CHECK(!g_out[0].large);
    CHECK_EQ(int(g_out[0].actor), small);

    a.clear();
    at(a, ActType::Sora, 100, 100);
    CHECK(has(flagsFor(ActType::Sora), ActFlags::Large));
    b = buildOam(a, still(), g_out);
    CHECK_EQ(int(g_out[0].x), 100 - 16);
    CHECK_EQ(int(g_out[0].y), 100 - 32);
    CHECK(g_out[0].large);
}

KH_TEST(oamsort_height_lifts_the_sprite_and_not_its_shadow) {
    // "Standing on a raised deck lifts the sprite without moving the actor: the
    // world stays one flat plane as far as the geometry is concerned"
    // (oam.s:394).  Eight pixels a step, oam.s:404-407.
    //
    // The shadow stays on the ground, which is what makes a jump read as a
    // jump: the actor rises off its own shadow.
    Actors a;
    a.clear();
    const int i = at(a, ActType::Sora, 100, 150);
    a.z[i] = 3;
    const OamBuild b = buildOam(a, still(), g_out);

    int actorY = -9999, shadowY = -9999;
    for (int k = 0; k < b.used; ++k) {
        if (g_out[k].kind == SpriteKind::Actor) actorY = g_out[k].y;
        if (g_out[k].kind == SpriteKind::Shadow) shadowY = g_out[k].y;
    }
    CHECK_EQ(actorY, 150 - 3 * 8 - 32);
    CHECK_EQ(shadowY, 150 - 32);            // no lift
    CHECK_EQ(shadowY - actorY, 3 * 8);
}

KH_TEST(oamsort_culls_off_screen_before_taking_a_slot) {
    // oam.s:452-464: the range check wraps the `jsr WriteOamEntry`, and the
    // slot cursor advances only on a write.  So an off-screen actor is FREE,
    // and the 128 ceiling limits what is drawn rather than what exists.
    //
    // Backwards, this is the bug where a big map runs out of sprites while
    // showing almost none.
    Actors a;
    a.clear();
    at(a, ActType::Sora, 100, 100);                 // on screen
    for (int k = 0; k < 40; ++k) at(a, ActType::Shadow, 4000 + k * 20, 100);

    const OamBuild b = buildOam(a, still(), g_out);
    CHECK_EQ(b.actorsSorted, 41);
    CHECK(b.culled >= 40);
    CHECK_EQ(b.droppedToCeiling, 0);                // nothing hit the ceiling
    for (int i = 0; i < b.used; ++i) {
        CHECK(g_out[i].x < SCREEN_W);
        CHECK(g_out[i].y < SCREEN_H);
        CHECK(g_out[i].x + 32 >= 0);
        CHECK(g_out[i].y + 32 >= 0);
    }
}

KH_TEST(oamsort_the_cull_rectangle_is_32_lines_shorter_than_the_snes) {
    // DIVERGENCE 001, reaching the sprite path.  SCREEN_H is 192 here and 224
    // there, so an actor between those two rows is on screen on the SNES and
    // culled on the DS.  It is not a bug and it is not adjustable: the screen
    // really is shorter.  Pinned so that it is a stated consequence rather than
    // something noticed later as "sprites vanish early at the bottom".
    CHECK_EQ(SCREEN_H, 192);
    CHECK_EQ(SNES_SCREEN_H, 224);

    Actors a;
    a.clear();
    at(a, ActType::Shadow, 100, 200 + 16);      // feet at 216: inside 224, not 192
    const OamBuild b = buildOam(a, still(), g_out);
    CHECK_EQ(b.used, 0);
    CHECK(b.culled >= 1);

    // ...and one row above the DS's edge is still drawn, so the boundary is
    // where it should be and not a whole sprite early.
    a.clear();
    at(a, ActType::Shadow, 100, SCREEN_H - 1 + 16);
    const OamBuild b2 = buildOam(a, still(), g_out);
    CHECK_EQ(countKind(b2, SpriteKind::Actor), 1);
}

KH_TEST(oamsort_stops_at_the_hardware_ceiling_and_says_how_many_it_dropped) {
    // oam.s:161, :190, :308 all check `cmp #128 / bcs @done` before emitting.
    // The tail of the sort is dropped ON PURPOSE -- and being the tail, it is
    // the furthest back, which is the right thing to lose.
    //
    // DIVERGENCE 002 makes this reachable in a way it never was: the pool is
    // 128 here against the oracle's 32, so a busy frame has four times the
    // candidates for the same 128 entries.  The SNES could not fill OAM from a
    // 32-actor pool even with shadows; the DS can.
    Actors a;
    a.clear();
    for (int k = 0; k < MAX_ACTORS; ++k) {
        const int i = at(a, ActType::Shadow, (k % 16) * 8, (k / 16) * 8 + 32);
        CHECK(i >= 0);
    }
    CHECK_EQ(MAX_ACTORS, 128);

    const OamBuild b = buildOam(a, still(), g_out);
    CHECK_EQ(b.actorsSorted, 128);
    CHECK_EQ(b.used, OAM_SLOTS);
    // 128 actors plus 128 shadows wants 256 slots and there are 128.
    CHECK(b.droppedToCeiling > 0);
    CHECK_EQ(b.used + b.droppedToCeiling + b.culled, 128 + 128);

    // What survived is the FRONT of the sort, which is the point: every Actor
    // slot is filled before any Shadow, so losing the tail loses shadows and
    // the actors furthest away rather than something in the middle.
    CHECK_EQ(countKind(b, SpriteKind::Actor), 128);
    CHECK_EQ(countKind(b, SpriteKind::Shadow), 0);
}

KH_TEST(oamsort_shadows_come_out_in_sort_order_too) {
    // They are all behind the actors, so why does their order matter?  Because
    // shadows overlap EACH OTHER: two blobs drawn in a different order from
    // their casters would put the nearer one's shadow under the further one's.
    // oam.s:186-206 walks sortIdx for exactly this reason.
    Actors a;
    a.clear();
    const int back = at(a, ActType::Shadow, 100, 60);
    const int front = at(a, ActType::Shadow, 104, 64);
    CHECK(has(flagsFor(ActType::Shadow), ActFlags::Shadow));

    const OamBuild b = buildOam(a, still(), g_out);
    int firstShadowActor = -1, secondShadowActor = -1;
    for (int i = 0; i < b.used; ++i) {
        if (g_out[i].kind != SpriteKind::Shadow) continue;
        if (firstShadowActor < 0) firstShadowActor = g_out[i].actor;
        else if (secondShadowActor < 0) secondShadowActor = g_out[i].actor;
    }
    CHECK_EQ(firstShadowActor, front);      // frontmost caster, frontmost shadow
    CHECK_EQ(secondShadowActor, back);
}

KH_TEST(oamsort_a_boss_half_off_the_edge_costs_only_the_quadrants_on_screen) {
    // Per quadrant, not per boss.  A 64x64 body straddling an edge has some
    // quarters visible and some not, and each is culled on its own -- so it
    // costs two slots and not four.
    // Feet at -16, not at 0.  At 0 the left quadrants land at exactly -32,
    // which the SNES's cull DRAWS: `adc #32 / cmp #(SCREEN_W+32)` accepts any
    // x >= -32 whatever the sprite's width, so a 32-wide sprite entirely off
    // the left edge still costs a slot.  That slop is faithful -- oam.s took a
    // uniform margin rather than branching on size -- and the first draft of
    // this case sat exactly on it and asserted the opposite.
    Actors a;
    a.clear();
    at(a, ActType::Darkside, -16, 120);
    const OamBuild b = buildOam(a, still(), g_out);
    const int quads = countKind(b, SpriteKind::BossQuadrant);
    CHECK_EQ(quads, 2);                     // the right-hand pair only
    for (int i = 0; i < b.used; ++i)
        if (g_out[i].kind == SpriteKind::BossQuadrant) {
            CHECK(g_out[i].x + 32 >= 0);
            CHECK_EQ(int(g_out[i].x), -16);
        }

    // ...and the boundary itself, pinned, because it is the SNES's and not a
    // tidier one somebody might "fix" later: -32 is drawn, -33 is not.
    a.clear();
    at(a, ActType::Shadow, -32 + 8, 100);   // small sprite anchors -8: x = -32
    CHECK_EQ(countKind(buildOam(a, still(), g_out), SpriteKind::Actor), 1);
    CHECK_EQ(int(g_out[0].x), -32);
    a.clear();
    at(a, ActType::Shadow, -33 + 8, 100);
    CHECK_EQ(countKind(buildOam(a, still(), g_out), SpriteKind::Actor), 0);
}

KH_TEST(oamsort_an_empty_frame_is_empty_rather_than_undefined) {
    Actors a;
    a.clear();
    const OamBuild b = buildOam(a, still(), g_out);
    CHECK_EQ(b.used, 0);
    CHECK_EQ(b.actorsSorted, 0);
    CHECK_EQ(b.culled, 0);
    CHECK_EQ(b.droppedToCeiling, 0);
    CHECK_EQ(buildSortList(a, g_order), 0);
}

KH_TEST(oamsort_depth_is_the_ground_position_and_ignores_the_height_lift) {
    // THE CASE THAT DISTINGUISHES WORLD Y FROM SCREEN Y, and the file did not
    // have one until a deliberate breakage failed to fire and exposed that.
    //
    // A camera subtracts the same offset from every actor, so "sorted by world
    // Y" and "sorted by screen Y" agree on every scrolling test that can be
    // written.  They part company on HEIGHT: feet() lifts a sprite by z*8 for
    // drawing (oam.s:404-407), and the sort key is `lda actY,x` -- the ground
    // position, before any lift (oam.s:112-113).
    //
    // So somebody standing on a raised deck keeps the depth of the ground he is
    // standing on.  A port that sorted on the lifted position would draw him
    // behind things he is south of, and it would look like a depth bug that
    // only happens on the decks.
    Actors a;
    a.clear();
    const int high = at(a, ActType::Sora, 100, 100);    // south, but lifted
    const int low = at(a, ActType::Palm, 140, 80);      // north, on the ground
    a.z[high] = 6;                                      // 48 pixels up

    const int n = buildSortList(a, g_order);
    CHECK_EQ(n, 2);
    CHECK_EQ(int(g_order[0]), high);        // world Y 100 wins: frontmost
    CHECK_EQ(int(g_order[1]), low);

    // ...and the two orderings really do disagree here, which is what makes the
    // assertion above worth making.  Screen Y: the lifted one is at 100-48 = 52
    // and the other at 80, so sorting on screen would reverse them.
    const OamBuild b = buildOam(a, still(), g_out);
    int highY = 0, lowY = 0;
    for (int i = 0; i < b.used; ++i) {
        if (g_out[i].kind != SpriteKind::Actor) continue;
        if (g_out[i].actor == high) highY = g_out[i].y + LARGE_ANCHOR;
        if (g_out[i].actor == low) lowY = g_out[i].y + LARGE_ANCHOR;
    }
    CHECK(highY < lowY);                    // drawn higher up the screen...
    CHECK_EQ(int(g_out[0].actor), high);    // ...and still in front
}

KH_TEST(oamsort_the_camera_moves_the_sprites_and_not_the_sort) {
    // Depth is a WORLD property.  Scrolling changes where things are drawn and
    // must not change who is in front of whom -- a port that sorted on screen Y
    // would agree everywhere except across a clamp, where the camera stops and
    // the world does not.
    Actors a;
    a.clear();
    at(a, ActType::Palm, 300, 100);
    at(a, ActType::Sora, 300, 140);

    buildSortList(a, g_order);
    const uint8_t a0 = g_order[0], a1 = g_order[1];

    Camera c;
    c.x = 200;
    c.y = 50;
    const OamBuild b = buildOam(a, c, g_out);
    CHECK(b.used >= 2);
    CHECK_EQ(int(g_out[0].actor), int(a0));
    CHECK_EQ(int(g_out[1].actor), int(a1));
    // ...and the positions moved by exactly the camera.
    CHECK_EQ(int(g_out[0].x), 300 - 200 - 16);
    CHECK_EQ(int(g_out[0].y), 140 - 50 - 32);
}
