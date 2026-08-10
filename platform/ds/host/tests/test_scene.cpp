// Loading a scene: the ground, and the cast that stands on it.
//
// These tests read the REAL tables out of assets/gen/ds/, not fixtures.  That is
// the point of them: nine cast files have been authored and validated by
// tools/check_map.py, and until something loaded one, the only thing proven was
// that the data was self-consistent.  If they fail with "run build_assets.py",
// that is what to do.

#include "check.h"
#include "gen/assets.h"
#include "hostblob.h"

using namespace kh;

namespace {

// Big enough for the largest thing the pipeline emits: the island's 16 KB
// tilemap is not loaded here, but its 2 KB collision and height maps are.
unsigned char buf1[khhost::MAX_BLOB];
unsigned char buf2[khhost::MAX_BLOB];
unsigned char buf3[khhost::MAX_BLOB];

bool have(Blob b, const char* what) {
    if (!b.empty()) return true;
    std::printf("  MISSING %s -- run: python3 tools/build_assets.py\n", what);
    return false;
}

}  // namespace

KH_TEST(scene_ground_reads_the_islands_collision_map) {
    // The DS island is 64x32 with 670 walkable tiles -- the number
    // tools/check_map.py reports.  Reading it back here is what proves the
    // pipeline's bytes and the engine's indexing agree about which way round
    // i and j go, which is the kind of thing that is invisible until sprites
    // stand in the sea.
    Blob coll = khhost::load("islandcoll.bin", buf1, sizeof buf1);
    Blob hmap = khhost::load("islandheight.bin", buf2, sizeof buf2);
    if (!have(coll, "islandcoll.bin") || !have(hmap, "islandheight.bin")) {
        CHECK(false);
        return;
    }
    CHECK_EQ(coll.size, size_t(DS_ISLAND_W * DS_ISLAND_H));
    CHECK_EQ(hmap.size, size_t(DS_ISLAND_W * DS_ISLAND_H));

    SceneGround g;
    g.set(coll, hmap, DS_ISLAND_W, DS_ISLAND_H);
    CHECK(g.valid());
    CHECK_EQ(g.width(), DS_ISLAND_W);
    CHECK_EQ(g.height(), DS_ISLAND_H);

    int walkable = 0;
    for (int j = 0; j < g.height(); ++j)
        for (int i = 0; i < g.width(); ++i)
            if (g.walkable(i, j)) ++walkable;
    CHECK_EQ(walkable, 670);

    // Where Sora wakes up, and the sea north of the cliff.
    CHECK(g.walkable(24, 20));
    CHECK(!g.walkable(0, 0));

    // Off the map is not walkable, which is what makes the rim work without a
    // border of blocking tiles.  BEHAVIOUR.md §1.
    CHECK(!g.walkable(-1, 20));
    CHECK(!g.walkable(24, -1));
    CHECK(!g.walkable(DS_ISLAND_W, 20));
    CHECK(!g.walkable(24, DS_ISLAND_H));
    CHECK_EQ(g.heightAt(-1, -1), 0);

    // A refusal, not a crash, on a map that does not fit its buffers.
    SceneGround bad;
    bad.set(coll, hmap, 4096, 4096);
    CHECK(!bad.valid());
}

KH_TEST(scene_spawns_the_whole_island_cast) {
    // 69 props derived from the map plus 9 placed = 78 rows, which is what
    // build_assets.py reports for ds/island.
    Blob coll = khhost::load("islandcoll.bin", buf1, sizeof buf1);
    Blob hmap = khhost::load("islandheight.bin", buf2, sizeof buf2);
    Blob cast = khhost::load("islandcast.bin", buf3, sizeof buf3);
    if (!have(cast, "islandcast.bin") || !have(coll, "islandcoll.bin")) {
        CHECK(false);
        return;
    }
    // 75, down from 78: Faces, the Door and the Scribble moved into
    // assets/ds/cave_cast.txt when the Secret Place became a room.  69 props
    // derived from the map plus 6 placed people.
    CHECK_EQ(cast.size, size_t(75 * CAST_STRIDE + 1));      // + the terminator

    SceneGround g;
    g.set(coll, hmap, DS_ISLAND_W, DS_ISLAND_H);

    Actors a;
    a.clear();
    uint8_t variant[MAX_ACTORS] = {};
    SpawnResult r = spawnCast(a, cast, g, variant);
    CHECK(r.complete());                                    // nothing dropped
    CHECK_EQ(r.spawned, 75);
    CHECK_EQ(r.refused, 0);

    // The pipeline emits props in map order and then the placed cast, so slot 0
    // is a prop and Sora is not at the front.  That ordering is deliberate --
    // it keeps prop slots stable under an edit elsewhere on the map.
    CHECK(a.type[0] == ActType::Palm);
    CHECK_EQ(a.count(ActType::Sora), 1);
    CHECK_EQ(a.count(ActType::Palm), 39);       // one per T tile
    CHECK_EQ(a.count(ActType::PalmC), 7);       // and per Y tile, fruit still on
    CHECK_EQ(a.count(ActType::RockBig), 7);
    CHECK_EQ(a.count(ActType::Rock), 16);
    CHECK_EQ(a.count(ActType::Kairi), 1);
    CHECK_EQ(a.count(ActType::Riku), 1);
    // THE DRAWINGS ARE NOT ON THE ISLAND ANY MORE.  They are in the chamber, and
    // scene_the_secret_places_cast_is_the_back_wall below is where they are
    // asserted -- so this is not a check deleted, it is a check that moved with
    // the content it was about.
    CHECK_EQ(a.count(ActType::Faces), 0);
    CHECK_EQ(a.count(ActType::Door), 0);
    CHECK_EQ(a.count(ActType::Scribble), 0);

    // Positions are tile centres, exactly as TileToWorld then a shift by 4.
    int sora = -1;
    for (int i = 0; i < MAX_ACTORS; ++i)
        if (a.type[i] == ActType::Sora) sora = i;
    CHECK(sora >= 0);
    CHECK_EQ(tileOf(a.x[sora]), 24);
    CHECK_EQ(tileOf(a.y[sora]), 20);
    CHECK_EQ(a.x[sora].raw(), tileCentre(24).raw());

    // SetActorZ ran: a wall prop is hung and forced to height 0, everything else
    // takes the height of the tile under it.  world.s:244-277.
    for (int i = 0; i < MAX_ACTORS; ++i) {
        if (a.type[i] == ActType::None) continue;
        if (has(a.flags[i], ActFlags::Flat))
            CHECK_EQ(a.z[i], 0);
        else
            CHECK_EQ(a.z[i], g.heightAt(int(tileOf(a.x[i])), int(tileOf(a.y[i]))));
    }
    // ...and at least one actor is genuinely off the ground, or that loop proves
    // nothing.  The treehouse cloth and the lookout rope stand on decks.
    bool raised = false;
    for (int i = 0; i < MAX_ACTORS; ++i)
        if (a.type[i] != ActType::None && a.z[i] > 0) raised = true;
    CHECK(raised);
}

KH_TEST(scene_the_night_has_no_coconuts_left) {
    // The night runs on the island's map with one prop override: every Y tile
    // gives a plain Palm, because day two picked the fruit.  This is the test
    // that the override survives the trip through the binary rather than being a
    // comment in a text file.  See docs/WORLD_SIZES.md.
    Blob coll = khhost::load("islandcoll.bin", buf1, sizeof buf1);
    Blob hmap = khhost::load("islandheight.bin", buf2, sizeof buf2);
    Blob cast = khhost::load("nightcast.bin", buf3, sizeof buf3);
    if (!have(cast, "nightcast.bin")) {
        CHECK(false);
        return;
    }
    SceneGround g;
    g.set(coll, hmap, DS_ISLAND_W, DS_ISLAND_H);
    Actors a;
    a.clear();
    SpawnResult r = spawnCast(a, cast, g);
    CHECK(r.complete());
    CHECK_EQ(r.spawned, 75);                    // 69 props + 6 placed
    CHECK_EQ(a.count(ActType::PalmC), 0);       // picked clean
    CHECK_EQ(a.count(ActType::Palm), 46);       // 39, plus the 7 that were PalmC

    // The islanders went home.  Only these two did not.
    CHECK_EQ(a.count(ActType::Riku), 1);
    CHECK_EQ(a.count(ActType::Kairi), 1);
    CHECK_EQ(a.count(ActType::Tidus), 0);
    CHECK_EQ(a.count(ActType::Selphie), 0);
    CHECK_EQ(a.count(ActType::Wakka), 0);
    // And nothing is carrying the day's errand.
    CHECK_EQ(a.count(ActType::Log), 0);
    CHECK_EQ(a.count(ActType::Bottle), 0);
}

KH_TEST(scene_a_dream_weapon_sits_on_its_dais) {
    // station1: Sora, three pedestals and three weapons, with each weapon on the
    // same tile as its dais because it hovers over the stone.  The stations are
    // the only scenes with no derived props at all -- a disc of glass has no prop
    // tiles.  docs/behaviour/divergences/004-ds-station-radius.md.
    Blob coll = khhost::load("station1coll.bin", buf1, sizeof buf1);
    Blob hmap = khhost::load("station1height.bin", buf2, sizeof buf2);
    Blob cast = khhost::load("station1cast.bin", buf3, sizeof buf3);
    if (!have(cast, "station1cast.bin") || !have(coll, "station1coll.bin")) {
        CHECK(false);
        return;
    }
    SceneGround g;
    g.set(coll, hmap, ORACLE_MAP_W, ORACLE_MAP_H);

    // 76 standable tiles at radius 92, against the SNES's 96 at 110.
    int walkable = 0;
    for (int j = 0; j < g.height(); ++j)
        for (int i = 0; i < g.width(); ++i)
            if (g.walkable(i, j)) ++walkable;
    CHECK_EQ(walkable, 76);
    CHECK(g.walkable(16, 11));                  // where he arrives
    CHECK(!g.walkable(19, 11));                 // the SNES pedestal, now off it

    Actors a;
    a.clear();
    SpawnResult r = spawnCast(a, cast, g);
    CHECK(r.complete());
    CHECK_EQ(r.spawned, 7);
    CHECK_EQ(a.count(ActType::Pedestal), 3);
    CHECK_EQ(a.count(ActType::Sword), 1);
    CHECK_EQ(a.count(ActType::Shield), 1);
    CHECK_EQ(a.count(ActType::Staff), 1);

    // Each weapon shares its dais's tile.  Every station tile is flat glass, so
    // both sit at height 0 and the weapon's hover is a rendering offset.
    for (int w = 0; w < MAX_ACTORS; ++w) {
        if (!isDreamWeapon(a.type[w])) continue;
        bool paired = false;
        for (int p = 0; p < MAX_ACTORS; ++p)
            if (a.type[p] == ActType::Pedestal
                && a.x[p].raw() == a.x[w].raw() && a.y[p].raw() == a.y[w].raw())
                paired = true;
        CHECK(paired);
        CHECK_EQ(a.z[w], 0);
    }
}

KH_TEST(scene_reads_the_spot_and_door_tables) {
    // The night carries 35 Heartless spots against the SNES's 10, and the count
    // comes from the FILE -- SNES_NIGHT_SPOTS is kept only so the oracle diff can
    // name the fixture's value.  docs/behaviour/divergences/003-ds-night-density.md.
    Blob spots = khhost::load("nightspots.bin", buf1, sizeof buf1);
    if (!have(spots, "nightspots.bin")) {
        CHECK(false);
        return;
    }
    Tile out[MAX_SPOTS];
    const int n = readSpots(spots, out, MAX_SPOTS);
    CHECK_EQ(n, 35);
    CHECK(n > SNES_NIGHT_SPOTS);
    CHECK(n <= MAX_SPOTS);
    CHECK(SHADOW_MAX_NIGHT <= n);               // somewhere for each of them

    // Every spot has to be standable, which check_map.py already enforces --
    // asserting it here as well is cheap and catches a mismatch between the two
    // tools rather than trusting one of them.
    Blob coll = khhost::load("islandcoll.bin", buf2, sizeof buf2);
    Blob hmap = khhost::load("islandheight.bin", buf3, sizeof buf3);
    SceneGround g;
    g.set(coll, hmap, DS_ISLAND_W, DS_ISLAND_H);
    for (int i = 0; i < n; ++i) CHECK(g.walkable(out[i].i, out[i].j));

    // A buffer smaller than the table is a refusal, not a truncation.
    Tile small[4];
    CHECK_EQ(readSpots(spots, small, 4), -1);

    // The Second District's three doors, each with the tile Sora stands on.
    Blob doors = khhost::load("town2doors.bin", buf1, sizeof buf1);
    if (!have(doors, "town2doors.bin")) {
        CHECK(false);
        return;
    }
    Door d[8];
    const int nd = readDoors(doors, d, 8);
    CHECK_EQ(nd, 3);
    for (int i = 0; i < nd; ++i) {
        CHECK_EQ(d[i].at.j, DOOR_ROW);          // every district door is in row 4
        CHECK_EQ(d[i].landing.j, DOOR_ROW + 1); // and you land immediately south
        CHECK_EQ(d[i].landing.i, d[i].at.i);
    }
}

KH_TEST(scene_a_full_pool_refuses_visibly) {
    // The failure the SNES had.  SpawnTable ignored the clear carry, so an
    // overflowing table silently lost whichever entry came last -- which is how
    // the bottle under the waterfall went missing for a whole day.  A partial
    // load has to be reportable, so spawnCast counts refusals.
    Blob cast = khhost::load("islandcast.bin", buf3, sizeof buf3);
    if (!have(cast, "islandcast.bin")) {
        CHECK(false);
        return;
    }
    SceneGround g;                              // deliberately invalid: no ground
    CHECK(!g.valid());

    Actors a;
    a.clear();
    // Fill all but ten slots, then load a 75-row table into what is left.
    for (int i = 0; i < MAX_ACTORS - 10; ++i)
        a.spawn(ActType::Rock, World(), World());
    SpawnResult r = spawnCast(a, cast, g);
    CHECK(!r.complete());
    CHECK_EQ(r.spawned, 10);
    CHECK_EQ(r.refused, 65);
    CHECK(!r.malformed);

    // A table with no terminator is malformed, and says so.
    const uint8_t truncated[] = {3, 15, 4, 0, 3, 16, 4, 0};
    Actors b;
    b.clear();
    SpawnResult t = spawnCast(b, Blob{truncated, sizeof truncated}, g);
    CHECK(t.malformed);
    CHECK_EQ(t.spawned, 2);                     // what it managed before the end
}

// ===========================================================================
// The Secret Place -- the first room split out of a map
// ===========================================================================

KH_TEST(scene_the_secret_place_is_a_room_of_its_own) {
    // The chamber behind the waterfall used to be a six-tile pocket in the
    // island's cliff.  It is a scene now, which is what the PS2 does with it.
    //
    // EXTENTS COME OUT OF SCENE_ASSETS rather than being typed here.  The island
    // has DS_ISLAND_W in constants.h because three other things need it; a second
    // hand-written pair for every new room is how two numbers for one fact start.
    const SceneAsset& s = SCENE_ASSETS[static_cast<int>(SceneId::Cave)];
    CHECK_EQ(int(s.tilesW), 32);
    CHECK_EQ(int(s.tilesH), 16);
    CHECK(!s.streams);                  // 64x32 characters fits one background
    CHECK_EQ(s.bgSize, 1);
    CHECK(s.groundFrom == nullptr);      // its own ground, not the island's

    Blob coll = khhost::load("cavecoll.bin", buf1, sizeof buf1);
    Blob hmap = khhost::load("caveheight.bin", buf2, sizeof buf2);
    if (!have(coll, "cavecoll.bin") || !have(hmap, "caveheight.bin")) {
        CHECK(false);
        return;
    }
    const int W = int(s.tilesW);
    CHECK_EQ(coll.size, size_t(s.tilesW) * size_t(s.tilesH));
    CHECK_EQ(hmap.size, coll.size);

    // Known walkable: the middle of the chamber, and the corridor that reaches
    // it.  Known blocked: the void outside, and the rock back wall.
    CHECK(coll.data[7 * W + 8] != 0);           // (8,7), the chamber floor
    CHECK(coll.data[9 * W + 20] != 0);          // (20,9), the corridor
    CHECK(coll.data[0 * W + 0] == 0);           // (0,0), the dark
    CHECK(coll.data[3 * W + 8] == 0);           // (8,3), the rock wall

    // THE FLOOR IS FLAT AND THE WALL IS +3, which is what makes the wall a wall:
    // the painter draws a raised tile 8*h pixels above its own cell with a face
    // filling the gap, and that face is what the drawings are hung on.
    CHECK_EQ(hmap.data[7 * W + 8], 0);
    CHECK_EQ(hmap.data[9 * W + 20], 0);
    CHECK_EQ(hmap.data[3 * W + 8], 3);

    // ...and NOTHING raised sits south of walkable floor anywhere in the room.  A
    // raised tile covers what is NORTH of it, so rock below the floor would paint
    // over the floor -- the failure the first draft of this map had, and the
    // reason its walls are the dark rather than more rock.
    for (int j = 0; j + 1 < int(s.tilesH); ++j) {
        for (int i = 0; i < W; ++i) {
            if (hmap.data[(j + 1) * W + i] == 0) continue;   // not raised
            CHECK(coll.data[j * W + i] == 0);                // so nothing walks there
        }
    }
}

KH_TEST(scene_the_secret_places_cast_is_the_back_wall) {
    // Where scene_ground_reads_the_islands_cast's Faces check went.  The three
    // drawings left the island when the chamber became a room, and this is the
    // other half of that move -- so the assertion did not disappear, it followed
    // the content.
    const SceneAsset& s = SCENE_ASSETS[static_cast<int>(SceneId::Cave)];
    Blob coll = khhost::load("cavecoll.bin", buf1, sizeof buf1);
    Blob hmap = khhost::load("caveheight.bin", buf2, sizeof buf2);
    Blob cast = khhost::load("cavecast.bin", buf3, sizeof buf3);
    if (!have(cast, "cavecast.bin") || !have(coll, "cavecoll.bin")) {
        CHECK(false);
        return;
    }

    SceneGround g;
    g.set(coll, hmap, int(s.tilesW), int(s.tilesH));
    Actors a;
    a.clear();
    SpawnResult r = spawnCast(a, cast, g);
    CHECK(r.complete());
    CHECK_EQ(r.refused, 0);
    // Four: Sora and the three drawings.  No props -- a cave has no palms.
    CHECK_EQ(r.spawned, 4);
    CHECK_EQ(a.count(ActType::Sora), 1);
    CHECK_EQ(a.count(ActType::Faces), 1);
    CHECK_EQ(a.count(ActType::Door), 1);
    CHECK_EQ(a.count(ActType::Scribble), 1);

    // ON THE TOPMOST WALKABLE ROW, WITH ROCK DIRECTLY NORTH.  That is not
    // decoration: all three are AF_FLAT, which keeps them at height zero so they
    // land on the FACE of the tile behind rather than on top of it.  A flat
    // sprite with the void behind it would be a drawing hanging in mid-air.
    const int W = int(s.tilesW);
    for (int k = 0; k < MAX_ACTORS; ++k) {
        const ActType t = a.type[k];
        if (t != ActType::Faces && t != ActType::Door && t != ActType::Scribble)
            continue;
        CHECK(has(flagsFor(t), ActFlags::Flat));
        CHECK_EQ(a.z[k], 0);
        const int i = tileOf(a.x[k]);
        const int j = tileOf(a.y[k]);
        CHECK(coll.data[j * W + i] != 0);                   // standing on floor
        CHECK(j > 0);
        CHECK(coll.data[(j - 1) * W + i] == 0);             // rock behind it
        CHECK_EQ(hmap.data[(j - 1) * W + i], 3);            // ...and it is a cliff
    }

    // The Door is the middle one, which is what "the Door is among them" means:
    // between the two drawings rather than off to one side.
    int faces = -1, door = -1, scrib = -1;
    for (int k = 0; k < MAX_ACTORS; ++k) {
        if (a.type[k] == ActType::Faces) faces = k;
        if (a.type[k] == ActType::Door) door = k;
        if (a.type[k] == ActType::Scribble) scrib = k;
    }
    CHECK(faces >= 0 && door >= 0 && scrib >= 0);
    CHECK(tileOf(a.x[faces]) < tileOf(a.x[door]));
    CHECK(tileOf(a.x[door]) < tileOf(a.x[scrib]));
    // Two tiles apart, not adjacent: FindProp is a nearest-wins scan and the
    // chamber is wide enough to afford unambiguous spacing.
    CHECK_EQ(tileOf(a.x[door]) - tileOf(a.x[faces]), 2);
    CHECK_EQ(tileOf(a.x[scrib]) - tileOf(a.x[door]), 2);
}

KH_TEST(scene_the_third_mushroom_is_in_the_room_it_was_always_described_as_in) {
    // docs/DESTINY_ISLANDS.md, day two: "one inside the Secret Place".  It used
    // to sit at (7,6) on the island map, which was the floor of the pocket; it is
    // in the chamber now, and it is the whole of the cave's day-two table.
    Blob day2 = khhost::load("caveday2.bin", buf1, sizeof buf1);
    if (!have(day2, "caveday2.bin")) { CHECK(false); return; }
    CHECK_EQ(day2.size, size_t(1 * CAST_STRIDE + 1));
    CHECK_EQ(day2.data[0], uint8_t(ActType::Mush));
}
