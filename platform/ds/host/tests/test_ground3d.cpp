// The 3D quad ground, and the measurement §M7's brief asked for by name.
//
// "Build the visible-set walk, measure, and only merge if the measurement asks
// for it."  The measurement is arithmetic over map files -- the polygon budget
// is a number and the visible set is a rectangle -- so it is the part of this
// backend that most deserved doing before anybody could see it. "2x over" is
// the kind of thing you want settled before building a renderer around it.
//
// The usual caveat is stronger here than anywhere else in the milestone: no DS
// has drawn a triangle. What is checked is which tiles the walk selects, how
// many quads and vertices that costs, and whether that fits. Whether the ground
// LOOKS like ground is exactly the question a screen answers and this does not.

#include <cstdio>

#include "check.h"
#include "constants.h"
#include "gen/assets.h"
#include "ground.h"
#include "ground3d.h"
#include "hostblob.h"
#include "scene.h"
#include "stage.h"

using namespace kh;
using namespace kh::device;
using khhost::load;

namespace {

// Sized for the whole budget, so the cap never masks a measurement: a test that
// ran out of array would report a comfortable quad count for the wrong reason.
Quad g_quads[QUAD_BUDGET];

unsigned char g_map[128 * 64 * 2];
unsigned char g_coll[64 * 32];
unsigned char g_hgt[64 * 32];

struct Scene {
    SceneGround ground;
    CharMap chars;
    bool ok = false;

    Scene(const char* name, int tw, int th) {
        char f[64];
        std::snprintf(f, sizeof f, "%scoll.bin", name);
        Blob c = load(f, g_coll, sizeof g_coll);
        std::snprintf(f, sizeof f, "%sheight.bin", name);
        Blob h = load(f, g_hgt, sizeof g_hgt);
        std::snprintf(f, sizeof f, "%smap.bin", name);
        chars.entries = load(f, g_map, sizeof g_map);
        chars.wChars = tw * 2;
        chars.hChars = th * 2;
        ground.set(c, h, tw, th);
        ok = ground.valid() && chars.valid();
    }
};

Camera at(int x, int y) {
    Camera c;
    c.x = x;
    c.y = y;
    c.bgHOfs = x;
    c.bgVOfs = y;
    return c;
}

}  // namespace

KH_TEST(g3d_the_budget_is_bound_by_vertices_and_not_by_polygons) {
    // 2048 polygons and 6144 vertices, and a quad costs one polygon and four
    // vertices.  So vertices bind at 1536 quads and the polygon limit is
    // unreachable by quads alone -- which means the number to reason about is
    // 1536 and not 2048, and reasoning about the wrong one gives a renderer a
    // third more headroom than it has.
    CHECK_EQ(POLY_MAX, 2048);
    CHECK_EQ(VERT_MAX, 6144);
    CHECK_EQ(VERTS_PER_QUAD, 4);
    CHECK_EQ(QUAD_BUDGET, 1536);
    CHECK(QUAD_BUDGET < POLY_MAX);
    CHECK_EQ(QUAD_BUDGET * VERTS_PER_QUAD, VERT_MAX);
}

KH_TEST(g3d_the_briefs_premise_is_true_about_tiles_and_not_about_quads) {
    // The brief reasons: "a whole 64x64 ground is 4096 quads -- 2x over ...
    // frustum culling is therefore the load-bearing piece".  The first half is
    // arithmetic and it is right; the "therefore" is what the measurement
    // corrected, and the two are separated here so that neither is quietly
    // carrying the other.
    //
    // TRUE: the island has more TILES than the vertex budget allows quads.
    CHECK_EQ(DS_ISLAND_W * DS_ISLAND_H, 2048);
    CHECK(DS_ISLAND_W * DS_ISLAND_H > QUAD_BUDGET);

    // NOT TRUE, and this is the correction: a quad is a tile WITH GROUND ON IT,
    // and most tiles have none.  See
    // g3d_culling_is_a_saving_and_not_what_keeps_it_inside_the_budget for the
    // measured figures -- the island is 670 walkable of 2048, and the worst map
    // in the game fits the budget whole with no culling at all.
    Scene isl("island", 64, 32);
    CHECK(isl.ok);
    int walkable = 0;
    for (int y = 0; y < DS_ISLAND_H; ++y)
        for (int x = 0; x < DS_ISLAND_W; ++x)
            if (isl.ground.walkable(x, y)) ++walkable;
    CHECK(walkable < QUAD_BUDGET);
    CHECK(walkable * 3 < DS_ISLAND_W * DS_ISLAND_H);    // under a third is sea
}

KH_TEST(g3d_the_visible_set_covers_the_screen_and_no_more_than_it_must) {
    // A camera at the origin sees 256x192 pixels, which is 16x12 tiles, plus
    // the partial tile at each far edge.
    Frustum f = visibleTiles(at(0, 0), 64, 32, 0);
    CHECK_EQ(f.loX, 0);
    CHECK_EQ(f.loY, 0);
    CHECK_EQ(f.hiX, SCREEN_W / TILE_PX - 1);        // 15
    CHECK_EQ(f.hiY, SCREEN_H / TILE_PX - 1);        // 11

    // Off a tile boundary it sees one more of each, which is the partial tile.
    // Leaving this out is a strip of nothing at the right and bottom edges that
    // only appears at some scroll positions -- the same off-by-one the 2D
    // streamer has, and the same reason.
    f = visibleTiles(at(1, 1), 64, 32, 0);
    CHECK_EQ(f.loX, 0);
    CHECK_EQ(f.hiX, SCREEN_W / TILE_PX);            // 16
    CHECK_EQ(f.hiY, SCREEN_H / TILE_PX);            // 12

    // The margin widens it, and the clamp takes it back at the map's edge --
    // correctly, because there is no world out there and the backdrop shows.
    f = visibleTiles(at(0, 0), 64, 32, 4);
    CHECK_EQ(f.loX, 0);                             // clamped, not -4
    CHECK_EQ(f.hiX, SCREEN_W / TILE_PX - 1 + 4);
    f = visibleTiles(at(64 * 16 - SCREEN_W, 0), 64, 32, 4);
    CHECK_EQ(f.hiX, 63);                            // clamped to the last tile
}

KH_TEST(g3d_no_camera_in_the_game_can_go_negative_so_the_floor_is_defensive) {
    // AN HONEST CASE ABOUT AN UNOBSERVABLE BRANCH.
    //
    // visibleTiles() floors a negative camera rather than letting integer
    // division truncate towards zero.  The first draft of this case asserted
    // f.loX == 0 and called that a test of the floor -- it is not: the clamp
    // to the map produces 0 either way, so the branch cannot be observed
    // through the function's output at all.  A deliberate breakage that
    // replaced the floor with plain division changed nothing and passed, which
    // is how the vacuousness was found.
    //
    // So the truthful statement is this: the floor is correct, it is defensive,
    // and no camera the game can produce reaches it.  That is worth asserting
    // in the direction that IS checkable -- the bounds.
    CHECK_EQ(scrollingBounds(64, 32).loX, 0);
    CHECK_EQ(scrollingBounds(64, 32).loY, 0);
    CHECK(pinnedBounds(DIVE_CAM_X, DIVE_CAM_Y).loX >= 0);
    CHECK(pinnedBounds(DIVE_CAM_X, DIVE_CAM_Y).loY >= 0);
    CHECK(pinnedBounds(FRAG_CAM_X, FRAG_CAM_Y).loX >= 0);
    CHECK(pinnedBounds(FRAG_CAM_X, FRAG_CAM_Y).loY >= 0);

    // ...and the two arithmetics really do agree after the clamp, which is the
    // fact that makes the branch unobservable rather than merely untested.
    for (int x = -64; x < 0; ++x) {
        Camera c;
        c.x = x;
        CHECK_EQ(visibleTiles(c, 64, 32, 0).loX, 0);
    }
}

KH_TEST(g3d_every_shipped_map_fits_the_budget_at_every_camera_it_can_reach) {
    // THE MEASUREMENT.  The brief asks for it by name, and this is it: every
    // scene, every camera position it can scroll to, the peak quad count.
    //
    // Stepped a whole tile at a time rather than a pixel: the visible set is a
    // function of the tile the camera is in, so a pixel step measures the same
    // rectangle sixteen times.  A half-tile offset is included because the
    // partial-tile ring appears at any non-zero remainder.
    struct Case { const char* name; int w, h; };
    const Case scenes[] = {
        {"island", 64, 32}, {"town1", 48, 32}, {"town2", 48, 32},
        {"town3", 48, 32}, {"station1", 32, 16}, {"fragment", 32, 16},
    };
    const int MARGIN = 4;       // the tilt and zoom allowance

    int worst = 0;
    const char* worstName = "";
    for (const Case& s : scenes) {
        Scene sc(s.name, s.w, s.h);
        CHECK(sc.ok);
        if (!sc.ok) continue;

        const int maxX = s.w * TILE_PX - SCREEN_W;
        const int maxY = s.h * TILE_PX - SCREEN_H;
        for (int y = 0; y <= (maxY > 0 ? maxY : 0); y += TILE_PX / 2) {
            for (int x = 0; x <= (maxX > 0 ? maxX : 0); x += TILE_PX / 2) {
                const Ground3dBuild b =
                    buildQuads(sc.ground, sc.chars, at(x, y), MARGIN,
                               g_quads, QUAD_BUDGET);
                CHECK(!b.overBudget());
                CHECK(b.vertices <= VERT_MAX);
                CHECK(b.quads <= POLY_MAX);
                if (b.quads > worst) { worst = b.quads; worstName = s.name; }
            }
        }
    }

    // The brief's estimate was "~192 tiles, ~400 with tilt and zoom margin,
    // which is comfortably inside".  Pinned as a range rather than a number,
    // because the exact peak depends on how much of a map is walkable and that
    // is content -- but the ORDER is the claim, and a peak in the thousands
    // would mean the culling is not working at all.
    CHECK(worst > 0);
    CHECK(worst < QUAD_BUDGET / 2);         // comfortably inside, measured
    CHECK(worst <= (SCREEN_W / TILE_PX + 1 + 2 * MARGIN)
                 * (SCREEN_H / TILE_PX + 1 + 2 * MARGIN));
    CHECK(worstName[0] != '\0');
}

KH_TEST(g3d_culling_is_a_saving_and_not_what_keeps_it_inside_the_budget) {
    // THE MEASUREMENT CORRECTED THE BRIEF, and this case is the correction.
    //
    // §M7 says "frustum culling is therefore the load-bearing piece".  That
    // followed from "a whole 64x64 ground is 4096 quads -- 2x over", which
    // assumes every tile is a quad.  Most tiles are not ground: the island is
    // 2048 tiles and 670 of them are walkable, because the rest is sea.
    //
    // Measured, over every shipped map:
    //
    //     scene      tiles  walkable  verts whole   peak culled
    //     island      2048       670         2680          405
    //     town1       1536       891         3564          510
    //     town2       1536       822         3288          450
    //     town3       1536       877         3508          507
    //     station1     512        76          304           76
    //     fragment     512        96          384           96
    //     budget                             6144         6144
    //
    // EVERY MAP FITS WHOLE.  The worst, town1, is 891 quads and 3564 vertices
    // -- 58% of the quad budget with no frustum culling at all.  So the walk is
    // a saving of about 1.7x on the geometry engine's time, which is worth
    // having, but it is NOT what keeps this backend inside the hardware limit.
    // What keeps it inside is that the maps are mostly not ground.
    //
    // That matters for what comes next.  The brief's instruction was "build the
    // visible-set walk, measure, and only merge if the measurement asks for
    // it", and the measurement's answer is that coplanar merging is not needed
    // and will not be until a map is 1.7x more walkable than the Second
    // District.  A renderer built around the 4096 figure would have started
    // with a merge pass it never needed.
    Scene isl("island", 64, 32);
    Scene t1("town1", 48, 32);
    CHECK(isl.ok && t1.ok);

    // A margin large enough to reach every tile is the no-culling case,
    // expressed through the same code path rather than a second one.
    const Ground3dBuild whole =
        buildQuads(t1.ground, t1.chars, at(0, 0), 64, g_quads, QUAD_BUDGET);
    CHECK(!whole.overBudget());
    CHECK(whole.quads <= QUAD_BUDGET);
    CHECK(whole.vertices <= VERT_MAX);
    CHECK_EQ(whole.considered, 48 * 32);        // it really did look at all of it

    // ...and culling is still a real saving, which is why the walk stays.
    const Ground3dBuild culled =
        buildQuads(t1.ground, t1.chars, at(0, 0), 4, g_quads, QUAD_BUDGET);
    CHECK(culled.quads < whole.quads);
    CHECK(culled.considered < whole.considered);
    CHECK(!culled.overBudget());

    // The headroom, stated as the number that would change the answer: a map
    // with more than QUAD_BUDGET walkable tiles needs the walk to be correct
    // rather than merely faster.  town1 is the closest and it is not close.
    CHECK(whole.quads * 2 < QUAD_BUDGET * 2);   // i.e. under budget
    CHECK(whole.quads < QUAD_BUDGET);
    CHECK(QUAD_BUDGET * 100 / whole.quads >= 150);   // >= 1.5x of headroom
}

KH_TEST(g3d_the_budget_still_bites_when_something_asks_for_too_much) {
    // The maps that exist fit, and a cap that has never been reached is a cap
    // nobody has tested.  A tiny array is the same condition a 1.7x-denser map
    // would produce, and it must drop rather than overrun.
    Scene isl("island", 64, 32);
    CHECK(isl.ok);
    Quad small[8];
    const Ground3dBuild b =
        buildQuads(isl.ground, isl.chars, at(0, 0), 4, small, 8);
    CHECK_EQ(b.quads, 8);
    CHECK(b.overBudget());
    CHECK(b.dropped > 0);
    CHECK_EQ(b.considered, b.quads + b.culled + b.dropped);
    CHECK_EQ(b.vertices, 8 * VERTS_PER_QUAD);
}

KH_TEST(g3d_stops_at_the_vertex_budget_and_not_at_the_polygon_one) {
    // The two caps are 1536 and 2048, and no shipped map produces enough quads
    // to tell them apart -- which a deliberate breakage proved by swapping one
    // for the other and changing nothing.  So this is the denser map the
    // measurement names as the threshold: every tile walkable, which is 2048
    // candidates on an island-sized ground.
    //
    // It matters because the wrong cap is not a crash, it is a frame that asks
    // the geometry engine for 8192 vertices and gets the last 2048 of them
    // dropped by the hardware -- a wedge of missing ground whose shape depends
    // on the traversal order, which reads as a map data fault.
    static uint8_t coll[MAP_MAX_CELLS];
    static uint8_t hgt[MAP_MAX_CELLS];
    static uint8_t chr[MAP_MAX_CELLS * 4 * 2];
    for (int i = 0; i < DS_ISLAND_W * DS_ISLAND_H; ++i) { coll[i] = 1; hgt[i] = 0; }

    SceneGround g;
    g.set(Blob{coll, size_t(DS_ISLAND_W * DS_ISLAND_H)},
          Blob{hgt, size_t(DS_ISLAND_W * DS_ISLAND_H)}, DS_ISLAND_W, DS_ISLAND_H);
    CharMap m{Blob{chr, sizeof chr}, DS_ISLAND_W * 2, DS_ISLAND_H * 2};
    CHECK(g.valid() && m.valid());

    // THE ARRAY MUST BE BIGGER THAN THE BUDGET or the cap stops the walk first
    // and the internal limit is never reached -- which is exactly how the first
    // version of this case failed to distinguish the two.  Passing
    // cap = QUAD_BUDGET made `b.quads >= cap` the binding clause, so swapping
    // the other one for POLY_MAX changed nothing and the probe stayed silent.
    static Quad roomy[POLY_MAX];
    Camera c;
    const Ground3dBuild b = buildQuads(g, m, c, 64, roomy, POLY_MAX);
    CHECK_EQ(b.considered, DS_ISLAND_W * DS_ISLAND_H);      // 2048, all walkable
    CHECK_EQ(b.culled, 0);
    CHECK_EQ(b.quads, QUAD_BUDGET);                         // 1536, not 2048
    CHECK_EQ(b.vertices, VERT_MAX);
    CHECK(b.overBudget());
    CHECK_EQ(b.dropped, DS_ISLAND_W * DS_ISLAND_H - QUAD_BUDGET);
    // The polygon limit was never the binding one, which is the whole point.
    CHECK(b.quads < POLY_MAX);
}

KH_TEST(g3d_a_hole_in_the_map_is_a_hole_and_not_a_flat_quad) {
    // A quad over a non-walkable tile is a floor the player falls through in
    // the collision map and stands on in the picture -- and the picture is what
    // a person believes.  The 2D renderer gets this for free, because a hole is
    // a transparent character; here it has to be a decision.
    Scene isl("island", 64, 32);
    CHECK(isl.ok);
    const Ground3dBuild b =
        buildQuads(isl.ground, isl.chars, at(0, 0), 0, g_quads, QUAD_BUDGET);
    CHECK(b.culled > 0);                    // the island has sea in view
    CHECK_EQ(b.considered, b.quads + b.culled + b.dropped);
    for (int i = 0; i < b.quads; ++i)
        CHECK(isl.ground.walkable(g_quads[i].tx, g_quads[i].ty));
}

KH_TEST(g3d_a_corner_height_comes_from_the_tiles_that_meet_there) {
    // A per-tile height with per-tile corners is a staircase of floating slabs:
    // every tile flat at its own level with a vertical gap to its neighbour.
    // Taking each corner from the tiles AROUND it is what turns a height map
    // into a surface, and it is why a Quad carries four heights and not one.
    Scene isl("island", 64, 32);
    CHECK(isl.ok);
    const Ground3dBuild b =
        buildQuads(isl.ground, isl.chars, at(0, 0), 0, g_quads, QUAD_BUDGET);
    CHECK(b.quads > 0);

    for (int i = 0; i < b.quads; ++i) {
        const Quad& q = g_quads[i];
        CHECK_EQ(int(q.h[0]), int(isl.ground.heightAt(q.tx, q.ty)));
        CHECK_EQ(int(q.h[1]), int(isl.ground.heightAt(q.tx + 1, q.ty)));
        CHECK_EQ(int(q.h[2]), int(isl.ground.heightAt(q.tx, q.ty + 1)));
        CHECK_EQ(int(q.h[3]), int(isl.ground.heightAt(q.tx + 1, q.ty + 1)));
    }

    // ...and at least one quad really is a ramp, or the assertion above is
    // being satisfied by a map that happens to be flat everywhere in view.
    int ramps = 0;
    for (int i = 0; i < b.quads; ++i) {
        const Quad& q = g_quads[i];
        if (q.h[0] != q.h[1] || q.h[0] != q.h[2] || q.h[0] != q.h[3]) ++ramps;
    }
    CHECK(ramps > 0);
}

KH_TEST(g3d_has_no_64_character_ceiling_which_is_why_both_backends_exist) {
    // The 2D streamer refuses a map taller than its 64-character window and
    // slides a view through anything wider.  Geometry has no window: it is
    // bounded by the polygon budget and nothing else, so the map that forced
    // the streamer to exist costs this backend nothing extra.
    Scene isl("island", 64, 32);
    CHECK(isl.ok);
    QuadGround g(g_quads, QUAD_BUDGET, 4);
    CHECK(g.setMap(isl.chars));
    CHECK(g.error() == nullptr);
    CHECK_EQ(isl.chars.wChars, 128);            // twice the streamer's window
    g.load(isl.ground);
    CHECK(g.error() == nullptr);
    g.draw(at(0, 0));
    CHECK(g.lastBuild().quads > 0);
    CHECK(!g.lastBuild().overBudget());
}

KH_TEST(g3d_refuses_a_collision_map_that_is_a_different_size) {
    Scene isl("island", 64, 32);
    Scene town("town1", 48, 32);
    CHECK(isl.ok);
    QuadGround g(g_quads, QUAD_BUDGET, 4);
    CHECK(g.setMap(isl.chars));
    g.load(town.ground);                        // the town, against the island
    CHECK(g.error() != nullptr);
    g.draw(at(0, 0));
    CHECK_EQ(g.lastBuild().quads, 0);
}

KH_TEST(g3d_says_when_mosaic_would_be_silently_lost) {
    // divergence 006, made answerable.  GBATEK: "All other bits in BG0CNT have
    // no effect on 3D, namely, mosaic cannot be used on the 3D layer" -- and the
    // 3D image IS BG0 of the main engine, so there is nowhere else to put it.
    //
    // The Dive's Shatter and the night's Tear both coarsen the ground with
    // mosaic.  Under this backend those two beats do not merely look different:
    // they do not happen, nothing faults, and the effect's timer runs to
    // completion over a ground that never changed.  Invisible, silent, and only
    // findable by somebody who knows what the beat is supposed to look like --
    // which is the worst shape a divergence can take.
    ScreenFx fx;
    CHECK_EQ(int(fx.mosaic), 0);
    CHECK(!mosaicWouldBeLost(fx));

    fx.mosaic = 1;
    CHECK(mosaicWouldBeLost(fx));
    fx.mosaic = 15;
    CHECK(mosaicWouldBeLost(fx));

    // The other effects are NOT lost: brightness, whiteout and the shake are
    // register writes the 3D layer is subject to like any other, and only the
    // mosaic bit is excluded.  Asserting that keeps the refusal narrow -- a
    // renderer that bailed out on every effect would be a much bigger
    // divergence wearing this one's justification.
    ScreenFx other;
    other.brightness = 0;
    other.whiteout = 31;
    other.shakeX = 4;
    other.bgVisible = false;
    CHECK(!mosaicWouldBeLost(other));
}
