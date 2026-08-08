// The actor table and the type tables.
//
// Most of what matters here is already proven at compile time by the
// static_asserts in actor.h -- if this file compiles, the type ranges and the
// table alignment are correct.  What is left to test at runtime is the pool
// behaviour and the things a static_assert cannot reach.

#include "check.h"
#include "actor.h"

using namespace kh;

KH_TEST(actor_type_ranges) {
    // The engine asks these as range comparisons rather than lookups, so the
    // boundaries are the design.  Check both ends and just outside.
    CHECK(!isIslander(ActType::Mote));          // 13, one below Kairi
    CHECK(isIslander(ActType::Kairi));
    CHECK(isIslander(ActType::Wakka));
    CHECK(!isIslander(ActType::Log));           // 19, one above Wakka

    CHECK(!isPickup(ActType::Wakka));
    CHECK(isPickup(ActType::Log));
    CHECK(isPickup(ActType::Bottle));
    CHECK(!isPickup(ActType::Fish));            // answers to the Keyblade instead
    CHECK(!isPickup(ActType::PalmC));

    CHECK(!isWallProp(ActType::PalmC));
    CHECK(isWallProp(ActType::Door));
    CHECK(isWallProp(ActType::Scribble));
    // Once the door opens it becomes type 31 and leaves the examinable range,
    // so it can no longer be looked at.  Shipped behaviour, not a port bug.
    CHECK(!isWallProp(ActType::DoorOpen));

    CHECK(!isResident(ActType::Dark));
    CHECK(isResident(ActType::Cid));
    CHECK(isResident(ActType::TownWoman));
    CHECK(!isResident(ActType::Lamp));          // scenery, not somebody

    CHECK(isDreamWeapon(ActType::Sword));
    CHECK(isDreamWeapon(ActType::Staff));
    CHECK(!isDreamWeapon(ActType::Pedestal));
}

KH_TEST(actor_pickups_index_the_tally) {
    // itemCount is indexed by (type - Log), which is why a pickup needs no
    // lookup table to tally itself.  Every pickup must land on its own slot.
    CHECK(itemOf(ActType::Log) == Item::Log);
    CHECK(itemOf(ActType::Cloth) == Item::Cloth);
    CHECK(itemOf(ActType::Rope) == Item::Rope);
    CHECK(itemOf(ActType::Mush) == Item::Mush);
    CHECK(itemOf(ActType::Coconut) == Item::Nut);
    CHECK(itemOf(ActType::Egg) == Item::Egg);
    CHECK(itemOf(ActType::Bottle) == Item::Water);

    // Day one wants two logs, one cloth, one rope; day two three fish, three
    // mushrooms, two coconuts, an egg and a bottle of water.
    CHECK_EQ(NEED[static_cast<int>(Item::Log)], 2);
    CHECK_EQ(NEED[static_cast<int>(Item::Cloth)], 1);
    CHECK_EQ(NEED[static_cast<int>(Item::Rope)], 1);
    CHECK_EQ(NEED[static_cast<int>(Item::Mush)], 3);
    CHECK_EQ(NEED[static_cast<int>(Item::Nut)], 2);
    CHECK_EQ(NEED[static_cast<int>(Item::Egg)], 1);
    CHECK_EQ(NEED[static_cast<int>(Item::Water)], 1);
    CHECK_EQ(NEED[static_cast<int>(Item::Fish)], 3);
}

KH_TEST(actor_spawn_fills_from_the_type_tables) {
    Actors a;
    a.clear();

    const int s = a.spawn(ActType::Sora, tileCentre(11), tileCentre(12));
    CHECK_EQ(s, 0);
    CHECK(a.type[s] == ActType::Sora);
    CHECK_EQ(a.hp[s], SORA_MAX_HP);
    CHECK_EQ(a.tile[s], sprite::Sora);
    CHECK_EQ(a.pal[s], pal::Sora);
    CHECK(has(a.flags[s], ActFlags::Large));
    CHECK(has(a.flags[s], ActFlags::Shadow));
    CHECK(!has(a.flags[s], ActFlags::Page1));
    // Sora's spawn on the island: tile (11,12), and TileToWorld returns centres.
    CHECK_EQ(a.x[s].toInt(), 184);
    CHECK_EQ(a.y[s].toInt(), 200);
    CHECK_EQ(a.vx[s].raw(), 0);
    CHECK(a.state[s] == ActState::Idle);

    const int h = a.spawn(ActType::Shadow, tileCentre(7), tileCentre(9));
    CHECK_EQ(h, 1);
    CHECK_EQ(a.hp[h], HEART_MAX_HP);
    CHECK(!has(a.flags[h], ActFlags::Large));   // 16x16, unlike everyone else
    CHECK(has(a.flags[h], ActFlags::Shadow));
}

KH_TEST(actor_pool_is_bounded_and_says_so) {
    Actors a;
    a.clear();

    // Fill it exactly.
    for (int i = 0; i < MAX_ACTORS; ++i)
        CHECK_EQ(a.spawn(ActType::Rock, World(), World()), i);

    // And then refuse, visibly.  The SNES version signalled this with a clear
    // carry that SpawnTable ignored, which silently dropped the last entry of
    // whichever table overflowed -- that is how the bottle under the waterfall
    // went missing for the whole of day two.
    CHECK_EQ(a.spawn(ActType::Rock, World(), World()), -1);
    CHECK_EQ(a.count(ActType::Rock), MAX_ACTORS);
}

KH_TEST(actor_slots_are_reused_lowest_first) {
    Actors a;
    a.clear();
    for (int i = 0; i < 4; ++i) a.spawn(ActType::Rock, World(), World());

    a.type[1] = ActType::None;                  // something died
    CHECK_EQ(a.spawn(ActType::Shadow, World(), World()), 1);

    // Lowest free slot first is what makes the depth sort's tie-break stable:
    // equal-Y actors keep slot order, lower index frontmost.
    CHECK(a.type[1] == ActType::Shadow);
    CHECK_EQ(a.count(ActType::Rock), 3);
    CHECK_EQ(a.count(ActType::Shadow), 1);
}

KH_TEST(actor_clear_empties_everything) {
    Actors a;
    a.clear();
    a.spawn(ActType::Darkside, tileCentre(15), tileCentre(7));
    CHECK_EQ(a.count(ActType::Darkside), 1);

    a.clear();
    CHECK_EQ(a.count(ActType::Darkside), 0);
    for (int i = 0; i < MAX_ACTORS; ++i) {
        CHECK(a.type[i] == ActType::None);
        CHECK_EQ(a.hp[i], 0);
    }
}

KH_TEST(actor_flags_bit_values_match_the_assembly) {
    // These are the shipped bit positions.  0x04 is absent on purpose: it was
    // AF_SOLID, which nothing ever tested.
    CHECK_EQ(static_cast<int>(ActFlags::Large),  0x01);
    CHECK_EQ(static_cast<int>(ActFlags::Shadow), 0x02);
    CHECK_EQ(static_cast<int>(ActFlags::HFlip),  0x08);
    CHECK_EQ(static_cast<int>(ActFlags::Talk),   0x10);
    CHECK_EQ(static_cast<int>(ActFlags::Huge),   0x20);
    CHECK_EQ(static_cast<int>(ActFlags::Page1),  0x40);
    CHECK_EQ(static_cast<int>(ActFlags::Flat),   0x80);

    const ActFlags f = ActFlags::Large | ActFlags::Talk | ActFlags::Page1;
    CHECK(has(f, ActFlags::Talk));
    CHECK(!has(f, ActFlags::Shadow));
    CHECK(!has(without(f, ActFlags::Talk), ActFlags::Talk));
    CHECK(has(without(f, ActFlags::Talk), ActFlags::Large));
}

KH_TEST(actor_every_type_has_a_row_in_every_table) {
    // The assembler asserts this over in world.s.  Here the tables are sized by
    // ACT_TYPE_COUNT so a missing row will not compile -- but a row of the wrong
    // WIDTH would, so check that nothing past the end reads as populated and
    // that the boss rows landed where they should.
    CHECK_EQ(static_cast<int>(ActType::Count), 41);
    CHECK_EQ(hpFor(ActType::None), 0);
    CHECK_EQ(tileFor(ActType::None), 0);

    // Exactly four types carry HP: Sora and the three things that fight him.
    int withHp = 0;
    for (int i = 0; i < ACT_TYPE_COUNT; ++i)
        if (hpFor(static_cast<ActType>(i)) != 0) ++withHp;
    CHECK_EQ(withHp, 4);            // Sora, Shadow, Darkside, Guard Armor

    // Exactly two are 64x64.
    int huge = 0;
    for (int i = 0; i < ACT_TYPE_COUNT; ++i)
        if (has(flagsFor(static_cast<ActType>(i)), ActFlags::Huge)) ++huge;
    CHECK_EQ(huge, 2);
}

KH_TEST(dir_velocity_tables) {
    // Verbatim from world.s, and the facing order is what indexes them.
    CHECK_EQ(DIR_VEL_X[static_cast<int>(Dir::S)].raw(), 0);
    CHECK_EQ(DIR_VEL_Y[static_cast<int>(Dir::S)].raw(), 24);
    CHECK_EQ(DIR_VEL_X[static_cast<int>(Dir::E)].raw(), 24);
    CHECK_EQ(DIR_VEL_Y[static_cast<int>(Dir::E)].raw(), 0);
    CHECK_EQ(DIR_VEL_X[static_cast<int>(Dir::N)].raw(), 0);
    CHECK_EQ(DIR_VEL_Y[static_cast<int>(Dir::N)].raw(), -24);
    CHECK_EQ(DIR_VEL_X[static_cast<int>(Dir::W)].raw(), -24);

    // A diagonal is the cardinal scaled by about 1/sqrt(2): 24 * 0.707 = 16.97,
    // rounded to 17.  Both axes carry it, because the ground is square.
    CHECK_EQ(DIR_VEL_X[static_cast<int>(Dir::NE)].raw(), 17);
    CHECK_EQ(DIR_VEL_Y[static_cast<int>(Dir::NE)].raw(), -17);
    CHECK_EQ(DIR_VEL_X[static_cast<int>(Dir::SW)].raw(), -17);
    CHECK_EQ(DIR_VEL_Y[static_cast<int>(Dir::SW)].raw(), 17);

    // Opposite facings must be exact negations, or walking east then west would
    // not return to where you started.
    for (int d = 0; d < 4; ++d) {
        CHECK_EQ(DIR_VEL_X[d].raw(), -DIR_VEL_X[d + 4].raw());
        CHECK_EQ(DIR_VEL_Y[d].raw(), -DIR_VEL_Y[d + 4].raw());
    }

    // A Heartless moves at half speed, truncating: 17 >> 1 is 8, not 9.
    CHECK_EQ((DIR_VEL_X[static_cast<int>(Dir::NE)] >> HEART_SPEED_SHIFT).raw(), 8);
    CHECK_EQ((DIR_VEL_X[static_cast<int>(Dir::E)] >> HEART_SPEED_SHIFT).raw(), 12);
}

KH_TEST(ds_screen_divergence_is_explicit) {
    // The DS is 192 lines, not 224.  This is the one deliberate divergence
    // introduced in M1; see docs/behaviour/divergences/001-ds-screen-height.md.
    CHECK_EQ(SCREEN_H, 192);
    CHECK_EQ(SNES_SCREEN_H, 224);
    CHECK_EQ(CAM_MAX_X, 256);           // unchanged: the world is still 512 wide
    CHECK_EQ(CAM_MAX_Y, 64);            // was 32 -- 32 more rows are reachable
    CHECK_EQ(DIVE_CAM_X, 128);          // unchanged
    CHECK_EQ(DIVE_CAM_Y, 32);           // was 16
}

KH_TEST(map_size_is_per_scene) {
    // The SNES was locked to one map size by its tilemap.  These are the two
    // known sizes; M3 makes the camera clamp read them from the loaded scene.
    CHECK_EQ(ORACLE_MAP_W, 32);
    CHECK_EQ(ORACLE_MAP_H, 16);
    CHECK_EQ(DS_ISLAND_W, 64);
    CHECK_EQ(DS_ISLAND_H, 32);
    CHECK(DS_ISLAND_W * DS_ISLAND_H <= MAP_MAX_CELLS);

    // Four times the area, and wider than one DS background -- so it streams.
    CHECK_EQ(DS_ISLAND_W * DS_ISLAND_H, 4 * ORACLE_MAP_W * ORACLE_MAP_H);
    CHECK(DS_ISLAND_W > DS_BG_MAX_TILES);
}
