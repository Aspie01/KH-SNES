// Q12.4 parity with the SNES build.
//
// Every tuning number in docs/BEHAVIOUR.md is a raw World value, so if this
// file is wrong every number in the specification is silently rescaled and the
// oracle diff in M6 is worthless.  These are the cheapest tests in the project
// and they guard the most.

#include "check.h"
#include "fixed.h"

KH_TEST(fixed_snes_parity) {
    // 16 pixels of world per unit of 16.  This single line is the contract with
    // the 65816 build.
    CHECK_EQ(World::fromInt(16).raw(), 256);
    CHECK_EQ(World::fromInt(1).raw(), 16);
    CHECK_EQ(World::fromRaw(256).toInt(), 16);

    // Sora's walk speed, straight from game.inc: 24 raw is 1.5 px/frame.
    CHECK_EQ(World::fromRaw(24).toInt(), 1);
    CHECK_EQ(World::fromParts(1, 8).raw(), 24);

    // The eight-facing velocity tables are raw values and must survive as such.
    CHECK_EQ(World::fromRaw(17).raw(), 17);
}

KH_TEST(fixed_floors_it_does_not_truncate) {
    // A tile lookup on a negative coordinate is the reason toInt() and tileOf()
    // shift rather than divide.  Truncation toward zero would put -1 px in cell
    // 0 and let an actor stand one pixel west of the map.
    CHECK_EQ(World::fromInt(-1).toInt(), -1);
    CHECK_EQ(World::fromRaw(-1).toInt(), -1);      // -1/16 px still floors to -1
    CHECK_EQ(tileOf(World::fromInt(-1)), -1);
    CHECK_EQ(tileOf(World::fromRaw(-1)), -1);
    CHECK_EQ(tileOf(World::fromInt(0)), 0);
}

KH_TEST(fixed_tile_boundaries) {
    // The map is 32 tiles wide, so 511 px is the last pixel inside it and 512
    // is off the east edge.  grid.s rejects both edges with one unsigned
    // compare against MAP_W; the port must agree about where the edge is.
    CHECK_EQ(tileOf(World::fromInt(0)), 0);
    CHECK_EQ(tileOf(World::fromInt(15)), 0);
    CHECK_EQ(tileOf(World::fromInt(16)), 1);
    CHECK_EQ(tileOf(World::fromInt(511)), 31);
    CHECK_EQ(tileOf(World::fromInt(512)), 32);     // one past the east edge

    // TileToWorld returns cell centres, and every spawn point in the game is
    // one: tile 11 -> 11*16 + 8 = 184 px.
    CHECK_EQ(tileCentre(11).toInt(), 184);
    CHECK_EQ(tileCentre(0).toInt(), 8);
    CHECK_EQ(tileCentre(31).toInt(), 504);
}

KH_TEST(fixed_arithmetic_is_exact_at_engine_magnitudes) {
    // Nothing in the engine multiplies two positions, but hit-box maths
    // subtracts them and boss aiming stores them, so the extremes have to be
    // representable and the shift must not lose the fraction.
    const World far = World::fromInt(512);         // the east edge of the world
    const World v   = World::fromRaw(24);          // the fastest thing that walks

    CHECK_EQ((far - World::fromInt(512)).raw(), 0);
    CHECK_EQ((far + v).raw(), 8192 + 24);
    CHECK_EQ((-v).raw(), -24);
    CHECK_EQ(v.abs().raw(), 24);
    CHECK_EQ((-v).abs().raw(), 24);

    // A fixed-point multiply of two full-scale coordinates: 8192 * 8192 is 2^26,
    // which overflows nothing at 32 bits, but the >>4 must happen after the
    // widening or the intermediate is wrong.  Check the identity that catches it.
    CHECK_EQ((far * World::fromInt(1)).raw(), far.raw());
    CHECK_EQ((far * World::fromParts(0, 8)).raw(), 8192 / 2);   // times one half

    // Scaling by a plain count is not a fixed-point multiply and must not
    // shift: knockback is dirVel * 2 and orbs are dirVel * 2.
    CHECK_EQ((v * 2).raw(), 48);
    CHECK_EQ((World::fromRaw(17) * 2).raw(), 34);   // the diagonal orb speed

    // Halving is a shift, because ARMv5TE has no divide.  A Heartless moves at
    // dirVel >> 1 and its touch test halves abs(dx).
    CHECK_EQ((v >> 1).raw(), 12);
    CHECK_EQ((World::fromRaw(17) >> 1).raw(), 8);   // 8, not 8.5: the engine truncates
}

KH_TEST(fixed_render_conversion) {
    // The DS's 1.19.12 vertex and matrix format.  One world pixel is 4096.
    CHECK_EQ(toRender(World::fromInt(1)).raw(), 4096);
    CHECK_EQ(toRender(World::fromInt(16)).raw(), 256 * 256);
    CHECK_EQ(toRender(World::fromRaw(1)).raw(), 256);   // 1/16 px, exactly

    // Widening shift, so it is lossless in the only direction it is allowed to
    // go.  There is deliberately no conversion back: rendering never feeds the
    // simulation.
    CHECK_EQ(toRender(World::fromInt(-1)).raw(), -4096);
}

KH_TEST(fixed_comparisons) {
    const World a = World::fromInt(3);
    const World b = World::fromInt(4);
    CHECK(a < b);
    CHECK(a <= b);
    CHECK(b > a);
    CHECK(b >= a);
    CHECK(a != b);
    CHECK(a == World::fromInt(3));

    // Signed comparison, not unsigned: grid.s relies on the unsigned trick only
    // inside its bounds check, where it converts deliberately.
    CHECK(World::fromInt(-1) < World::fromInt(0));
}

// ---------------------------------------------------------------------------
// Compile-fail checks.  Documented rather than automated: the harness has no
// way to assert that something does not compile, and adding a build system that
// could is not worth it for two lines.
//
// Uncommenting either MUST fail to compile.  If one of them builds, fixed.h has
// grown an implicit conversion and the type has stopped doing its only job --
// report that, do not work around it.
//
//   World w = 3;                        // no converting constructor from int
//   auto  x = World::fromInt(1) + toRender(World::fromInt(1));   // mixed scales
//
// Verified by hand at the time of writing: both are errors under
// g++ 13.3.0 -std=c++17.
// ---------------------------------------------------------------------------
