// Ground geometry, movement and the camera.
//
// Every assertion cites the section it tests.  The authority is
// platform/snes/src/grid.s and world.s; docs/BEHAVIOUR.md §1 states the
// resolution order and §7 the camera.
//
// The fixtures are the REAL DS island, assets/ds/island.txt, loaded through the
// same collision and height maps the DS will run.  The 32x16 oracle island is
// the trace fixture and belongs to §M6; these routines are size-agnostic and the
// same code serves both.
//
// Positions are given as exact world PIXELS, because one frame of walking is
// about 1.5 px and a test placed at a tile centre would never reach the tile
// boundary it is trying to cross.

#include "check.h"
#include "grid.h"
#include "hostblob.h"

using namespace kh;

namespace {

unsigned char collBuf[khhost::MAX_BLOB];
unsigned char hmapBuf[khhost::MAX_BLOB];

// The island, or a false that every test checks before going on.
bool island(SceneGround& g) {
    Blob coll = khhost::load("islandcoll.bin", collBuf, sizeof collBuf);
    Blob hmap = khhost::load("islandheight.bin", hmapBuf, sizeof hmapBuf);
    if (coll.empty() || hmap.empty()) {
        std::printf("  MISSING island data -- run: python3 tools/build_assets.py\n");
        return false;
    }
    g.set(coll, hmap, DS_ISLAND_W, DS_ISLAND_H);
    return g.valid();
}

// Put one actor at an exact pixel with an exact facing's velocity.
int place(Actors& a, const SceneGround& g, int px, int py, Dir d,
          ActType t = ActType::Sora) {
    const int slot = a.spawn(t, World::fromInt(px), World::fromInt(py));
    a.dir[slot] = d;
    a.vx[slot] = DIR_VEL_X[int(d)];
    a.vy[slot] = DIR_VEL_Y[int(d)];
    setActorZ(a, slot, g);
    return slot;
}

}  // namespace

KH_TEST(grid_asr1_is_a_sign_preserving_halving) {
    // The ASR1 macro, and the two consequences BEHAVIOUR-AUDIT.md calls out.
    // The static_asserts in grid.h already prove these at compile time; they are
    // here as well because they are the reason a Shadow's diagonals differ.
    CHECK_EQ(asr1(World::fromRaw(24)).raw(), 12);
    CHECK_EQ(asr1(World::fromRaw(17)).raw(), 8);        // eastward diagonal
    CHECK_EQ(asr1(World::fromRaw(-17)).raw(), -9);      // westward is 9, not 8
    CHECK_EQ(asr1(World::fromRaw(-1)).raw(), -1);       // and never reaches zero
    CHECK_EQ(asr1(World::fromRaw(0)).raw(), 0);
}

KH_TEST(grid_off_the_map_is_not_walkable) {
    // §1: "Off-map rejection is one unsigned compare per axis: a negative
    // coordinate shifts to a large unsigned value, so >= MAP_W catches both edges
    // at once."  Here tileOf() floors instead, giving a negative tile that fails
    // the bounds check -- same outcome, and no 16-bit wraparound to worry about.
    SceneGround g;
    if (!island(g)) { CHECK(false); return; }

    uint8_t h = 0xEE;
    CHECK(!tileWalkable(g, World::fromInt(-1), World::fromInt(160), h));
    CHECK_EQ(h, 0);                                     // TileWalkable's stz tmp2
    CHECK(!tileWalkable(g, World::fromInt(160), World::fromInt(-1), h));
    CHECK(!tileWalkable(g, World::fromRaw(-17), World::fromInt(160), h));
    CHECK(!tileWalkable(g, World::fromInt(DS_ISLAND_W * TILE_PX),
                        World::fromInt(160), h));
    CHECK(!tileWalkable(g, World::fromInt(160),
                        World::fromInt(DS_ISLAND_H * TILE_PX), h));
    CHECK_EQ(tileHeight(g, World::fromInt(-1), World::fromInt(-1)), 0);

    // An actor at x = 0 walking west is refused rather than wrapping.
    Actors a;
    a.clear();
    const int s = place(a, g, 0, 160, Dir::W);
    const int32_t was = a.x[s].raw();
    tryMoveActor(a, s, g);
    CHECK_EQ(a.x[s].raw(), was);
}

KH_TEST(grid_walking_into_the_west_rim_slides_along_it) {
    // §1: the resolution order is what produces wall sliding.
    //
    // Tile (4,9) is grass.  West of it (3,9) is water and north (4,8) is water;
    // south (4,10) is grass.  So a south-west move has both diagonal and X
    // blocked and slides vertically.
    SceneGround g;
    if (!island(g)) { CHECK(false); return; }
    uint8_t h = 0;
    CHECK(tileWalkable(g, World::fromInt(4 * 16 + 8), World::fromInt(9 * 16 + 8), h));
    CHECK(!tileWalkable(g, World::fromInt(3 * 16 + 8), World::fromInt(9 * 16 + 8), h));
    CHECK(!tileWalkable(g, World::fromInt(4 * 16 + 8), World::fromInt(8 * 16 + 8), h));

    Actors a;
    a.clear();
    // West edge, bottom row of pixels, so one frame crosses both boundaries.
    const int s = place(a, g, 4 * 16, 9 * 16 + 15, Dir::SW);
    const int32_t x0 = a.x[s].raw();
    const int32_t y0 = a.y[s].raw();

    CHECK(tryMoveActor(a, s, g) == MoveResult::YOnly);
    CHECK_EQ(a.x[s].raw(), x0);                         // held against the water
    CHECK(a.y[s].raw() > y0);                           // and slid south past it
    CHECK_EQ(int(tileOf(a.y[s])), 10);
}

KH_TEST(grid_a_diagonal_into_a_corner_resolves_horizontally) {
    // §1: "steps 2-and-3 in that order mean a diagonal into a corner resolves
    // horizontally.  Reversing them changes how the island's walkways feel."
    //
    // This is the ONLY geometry where that order is observable: the diagonal
    // target must be blocked while BOTH orthogonal neighbours are free.  Tile
    // (16,8) is grass, east (17,8) is grass, north (16,7) is grass, and the
    // north-east target (17,7) is a palm trunk.
    SceneGround g;
    if (!island(g)) { CHECK(false); return; }
    uint8_t h = 0;
    CHECK(tileWalkable(g, World::fromInt(17 * 16 + 8), World::fromInt(8 * 16 + 8), h));
    CHECK(tileWalkable(g, World::fromInt(16 * 16 + 8), World::fromInt(7 * 16 + 8), h));
    CHECK(!tileWalkable(g, World::fromInt(17 * 16 + 8), World::fromInt(7 * 16 + 8), h));

    Actors a;
    a.clear();
    // The north-east pixel corner of (16,8), so one NE frame crosses both.
    const int s = place(a, g, 16 * 16 + 15, 8 * 16, Dir::NE);
    const int32_t y0 = a.y[s].raw();
    CHECK(tryMoveActor(a, s, g) == MoveResult::XOnly);
    CHECK_EQ(int(tileOf(a.x[s])), 17);                  // went east
    CHECK_EQ(a.y[s].raw(), y0);                         // and NOT north

    // The north route was genuinely available -- which is what makes the
    // horizontal resolution a choice rather than the only option.
    Actors b;
    b.clear();
    const int n = place(b, g, 16 * 16 + 15, 8 * 16, Dir::N);
    CHECK(tryMoveActor(b, n, g) == MoveResult::Both);
    CHECK_EQ(int(tileOf(b.y[n])), 7);
}

KH_TEST(grid_a_raised_deck_is_only_reachable_across_its_step) {
    // §1: MAX_STEP is 1, so a +2 deck cannot be entered from the ground -- only
    // over the +1 course beneath it.  The lookout: (9..12,9) is P at height 2,
    // (9..12,10) is L at height 1, and the lawn around them is height 0.
    SceneGround g;
    if (!island(g)) { CHECK(false); return; }
    uint8_t h = 0;
    CHECK(tileWalkable(g, World::fromInt(9 * 16 + 8), World::fromInt(9 * 16 + 8), h));
    CHECK_EQ(h, 2);                                     // the deck
    CHECK(tileWalkable(g, World::fromInt(9 * 16 + 8), World::fromInt(10 * 16 + 8), h));
    CHECK_EQ(h, 1);                                     // the step
    CHECK(tileWalkable(g, World::fromInt(8 * 16 + 8), World::fromInt(9 * 16 + 8), h));
    CHECK_EQ(h, 0);                                     // the lawn beside it

    // From the lawn, walking east into the deck: refused, and the height the
    // actor is standing on is not disturbed.
    Actors a;
    a.clear();
    const int lawn = place(a, g, 8 * 16 + 15, 9 * 16 + 8, Dir::E);
    CHECK_EQ(a.z[lawn], 0);
    const int32_t x0 = a.x[lawn].raw();
    tryMoveActor(a, lawn, g);
    CHECK_EQ(a.x[lawn].raw(), x0);
    CHECK_EQ(a.z[lawn], 0);

    // From the step, walking north onto the deck: allowed, and StoreZ records it.
    const int step = place(a, g, 9 * 16 + 8, 10 * 16, Dir::N);
    CHECK_EQ(a.z[step], 1);
    CHECK(tryMoveActor(a, step, g) == MoveResult::Both);
    CHECK_EQ(int(tileOf(a.y[step])), 9);
    CHECK_EQ(a.z[step], 2);

    // ...and reaching the step from the lawn is the move that makes the deck
    // reachable at all, so it has to work.
    const int below = place(a, g, 9 * 16 + 8, 11 * 16, Dir::N);
    CHECK_EQ(a.z[below], 0);
    CHECK(tryMoveActor(a, below, g) == MoveResult::Both);
    CHECK_EQ(a.z[below], 1);
}

KH_TEST(grid_a_blocked_cardinal_move_reports_the_other_axis) {
    // Faithful, and surprising enough to pin down.  Step 3 tests (current X,
    // candidate Y), and for a purely horizontal move the candidate Y IS the
    // current Y -- so it tests the tile the actor is already standing on, which
    // passes, and writes Y back unchanged.  MoveResult::Refused is therefore
    // NOT what a walk into a wall returns; it needs both axes to fail, which
    // needs both velocities to be non-zero.
    SceneGround g;
    if (!island(g)) { CHECK(false); return; }
    Actors a;
    a.clear();

    // Due west into the water at (3,9): nothing moves, and it reports YOnly.
    const int s = place(a, g, 4 * 16, 9 * 16 + 8, Dir::W);
    const int32_t x0 = a.x[s].raw();
    const int32_t y0 = a.y[s].raw();
    CHECK(tryMoveActor(a, s, g) == MoveResult::YOnly);
    CHECK_EQ(a.x[s].raw(), x0);
    CHECK_EQ(a.y[s].raw(), y0);                         // the "slide" moved nothing

    // Genuinely wedged: north-west out of (4,9), where west, north and the
    // diagonal are all water.  Now every branch fails.
    const int w = place(a, g, 4 * 16, 9 * 16, Dir::NW);
    CHECK(tryMoveActor(a, w, g) == MoveResult::Refused);
    CHECK_EQ(a.x[w].raw(), World::fromInt(4 * 16).raw());
    CHECK_EQ(a.y[w].raw(), World::fromInt(9 * 16).raw());
}

KH_TEST(grid_setActorZ_hangs_a_wall_prop_at_zero) {
    // world.s:244-251.  A Flat actor is hung on a wall rather than standing on
    // anything, so its height is forced to zero wherever it is placed -- and the
    // chalk in the Secret Place is placed on cave floor at height 0 anyway, so
    // the test has to use somewhere raised to mean anything.
    SceneGround g;
    if (!island(g)) { CHECK(false); return; }
    Actors a;
    a.clear();

    // The treehouse deck, height 2.
    const int deck = a.spawn(ActType::Sora, tileCentre(25), tileCentre(9));
    setActorZ(a, deck, g);
    CHECK_EQ(a.z[deck], 2);

    const int prop = a.spawn(ActType::Faces, tileCentre(25), tileCentre(9));
    CHECK(has(a.flags[prop], ActFlags::Flat));
    a.z[prop] = 3;                                      // stale, on purpose
    setActorZ(a, prop, g);
    CHECK_EQ(a.z[prop], 0);
}

KH_TEST(grid_nearPoint_is_strictly_inside) {
    // world.s:2185-2226.  `cmp range / bcs miss` misses on equality, so the
    // ranges in §2's interaction table are exclusive.
    SceneGround g;
    if (!island(g)) { CHECK(false); return; }
    Actors a;
    a.clear();
    const World px = tileCentre(20);
    const World py = tileCentre(12);
    const int s = a.spawn(ActType::Kairi, px, py);

    CHECK(nearPoint(a, s, px, py, World::fromRaw(1), World::fromRaw(1)));

    a.x[s] = World::fromRaw(px.raw() + TALK_X.raw());
    CHECK(!nearPoint(a, s, px, py, TALK_X, TALK_Y));
    a.x[s] = World::fromRaw(px.raw() + TALK_X.raw() - 1);
    CHECK(nearPoint(a, s, px, py, TALK_X, TALK_Y));

    // And it is symmetric: the absolute value is taken on both axes.
    a.x[s] = World::fromRaw(px.raw() - (TALK_X.raw() - 1));
    CHECK(nearPoint(a, s, px, py, TALK_X, TALK_Y));
    a.x[s] = World::fromRaw(px.raw() - TALK_X.raw());
    CHECK(!nearPoint(a, s, px, py, TALK_X, TALK_Y));

    a.x[s] = px;
    a.y[s] = World::fromRaw(py.raw() - TALK_Y.raw());
    CHECK(!nearPoint(a, s, px, py, TALK_X, TALK_Y));
}

KH_TEST(grid_camera_centres_on_96_not_112) {
    // §7, and DIVERGENCE: the screen is 192 lines, so the vertical centre is 96.
    // The horizontal one is unchanged -- SCREEN_W / 2 is 128 on both machines.
    // docs/behaviour/divergences/001-ds-screen-height.md.
    SceneGround g;
    if (!island(g)) { CHECK(false); return; }
    Actors a;
    a.clear();
    const CameraBounds b = scrollingBounds(DS_ISLAND_W, DS_ISLAND_H);
    CHECK_EQ(b.hiX, DS_ISLAND_W * TILE_PX - SCREEN_W);      // 1024 - 256
    CHECK_EQ(b.hiY, DS_ISLAND_H * TILE_PX - SCREEN_H);      // 512 - 192

    // Somewhere in the middle of the island, where neither clamp bites.
    const int s = place(a, g, 500, 300, Dir::S);
    Camera cam;
    updateCamera(cam, a, s, b);
    CHECK_EQ(cam.x, 500 - 128);
    CHECK_EQ(cam.y, 300 - 96);                              // not 300 - 112

    // DIVERGENCE: bgVOfs is camY.  The SNES wrote (camY - 1) & 0x3FF because its
    // BGnVOFS register displays background line value + 1.
    CHECK_EQ(cam.bgVOfs, cam.y);
    CHECK_EQ(cam.bgVOfs, 204);
    CHECK_EQ(cam.bgHOfs, cam.x);
}

KH_TEST(grid_camera_clamps_to_the_world_and_pins_when_told) {
    SceneGround g;
    if (!island(g)) { CHECK(false); return; }
    Actors a;
    a.clear();
    const CameraBounds b = scrollingBounds(DS_ISLAND_W, DS_ISLAND_H);
    Camera cam;

    // The north-west corner: a negative centre takes the low bound.
    const int nw = place(a, g, 8, 8, Dir::S);
    updateCamera(cam, a, nw, b);
    CHECK_EQ(cam.x, 0);
    CHECK_EQ(cam.y, 0);
    CHECK_EQ(cam.bgVOfs, 0);            // and NOT 0x3FF, which the SNES gave here

    // The south-east corner: clamped to the far edge.
    const int se = place(a, g, DS_ISLAND_W * TILE_PX - 8,
                         DS_ISLAND_H * TILE_PX - 8, Dir::S);
    updateCamera(cam, a, se, b);
    CHECK_EQ(cam.x, b.hiX);
    CHECK_EQ(cam.y, b.hiY);

    // A station is smaller than the screen, so its camera does not move at all.
    const CameraBounds pin = pinnedBounds(DIVE_CAM_X, DIVE_CAM_Y);
    const int mid = place(a, g, 500, 300, Dir::S);
    updateCamera(cam, a, mid, pin);
    CHECK_EQ(cam.x, DIVE_CAM_X);
    CHECK_EQ(cam.y, DIVE_CAM_Y);
    const int edge = place(a, g, 16, 16, Dir::S);
    updateCamera(cam, a, edge, pin);
    CHECK_EQ(cam.x, DIVE_CAM_X);
    CHECK_EQ(cam.y, DIVE_CAM_Y);
}

KH_TEST(grid_the_shake_moves_the_background_and_not_the_camera) {
    // shakeX is applied to bgHOfs alone.  Sprite positions come off camX, so the
    // ground shakes and the cast does not -- which is what makes the platform
    // shatter read as the world coming apart rather than the camera being
    // jostled.  §8.
    SceneGround g;
    if (!island(g)) { CHECK(false); return; }
    Actors a;
    a.clear();
    const int s = place(a, g, 500, 300, Dir::S);
    Camera cam;
    const CameraBounds b = scrollingBounds(DS_ISLAND_W, DS_ISLAND_H);

    updateCamera(cam, a, s, b, 5);
    CHECK_EQ(cam.x, 372);
    CHECK_EQ(cam.bgHOfs, 377);
    updateCamera(cam, a, s, b, -5);
    CHECK_EQ(cam.x, 372);               // unmoved
    CHECK_EQ(cam.bgHOfs, 367);          // and the shake is signed
    CHECK_EQ(cam.bgVOfs, cam.y);        // vertical is never shaken
}

KH_TEST(grid_the_ground_renderer_seam_is_free) {
    // §M3 mandates the interface now, with one implementation that does nothing,
    // so §M7's 3D quad backend is a swap and not a refactor.  This test exists to
    // prove the seam is actually usable through the base class -- if the only
    // caller were the concrete type, the virtual would be decoration.
    SceneGround g;
    if (!island(g)) { CHECK(false); return; }
    NullGroundRenderer null;
    GroundRenderer& r = null;

    CHECK_EQ(null.loads(), 0);
    r.load(g);
    CHECK_EQ(null.loads(), 1);
    CHECK(null.loaded() == &g);

    Camera cam;
    cam.x = 12;
    cam.y = 34;
    r.draw(cam);
    CHECK_EQ(null.draws(), 1);
    CHECK_EQ(null.lastCamera().x, 12);
    CHECK_EQ(null.lastCamera().y, 34);
}

KH_TEST(grid_walking_the_island_end_to_end_stays_on_it) {
    // A property, not a case: drive an actor from Sora's spawn in each of the
    // eight facings for a few hundred frames and assert it is standing on
    // walkable ground the whole way.  This is what catches an off-by-one in the
    // tile lookup that a hand-placed fixture walks straight past.
    SceneGround g;
    if (!island(g)) { CHECK(false); return; }
    Actors a;
    a.clear();
    const int s = place(a, g, 24 * 16 + 8, 20 * 16 + 8, Dir::S);
    uint8_t h = 0;

    for (int d = 0; d < int(Dir::Count); ++d) {
        a.dir[s] = Dir(d);
        a.vx[s] = DIR_VEL_X[d];
        a.vy[s] = DIR_VEL_Y[d];
        for (int f = 0; f < 400; ++f) {
            tryMoveActor(a, s, g);
            CHECK(tileWalkable(g, a.x[s], a.y[s], h));
            CHECK_EQ(a.z[s], h);        // the cached height never goes stale
        }
    }
}
