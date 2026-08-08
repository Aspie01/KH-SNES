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
    // The SNES was locked to one map size by its tilemap.  These are the sizes
    // that exist; M3 makes the camera clamp read them from the loaded scene.
    CHECK_EQ(ORACLE_MAP_W, 32);
    CHECK_EQ(ORACLE_MAP_H, 16);
    CHECK_EQ(DS_ISLAND_W, 64);
    CHECK_EQ(DS_ISLAND_H, 32);
    CHECK_EQ(DS_DISTRICT_W, 48);
    CHECK_EQ(DS_DISTRICT_H, 32);
    CHECK(DS_ISLAND_W * DS_ISLAND_H <= MAP_MAX_CELLS);
    CHECK(DS_DISTRICT_W * DS_DISTRICT_H <= MAP_MAX_CELLS);

    // Four times the area for the island, three for a district -- and both are
    // wider than one DS background, so both stream.
    CHECK_EQ(DS_ISLAND_W * DS_ISLAND_H, 4 * ORACLE_MAP_W * ORACLE_MAP_H);
    CHECK_EQ(DS_DISTRICT_W * DS_DISTRICT_H, 3 * ORACLE_MAP_W * ORACLE_MAP_H);
    CHECK(DS_ISLAND_W > DS_BG_MAX_TILES);
    CHECK(DS_DISTRICT_W > DS_BG_MAX_TILES);

    // The island is the biggest thing authored, so it is what the ceiling is
    // for.  If a district ever exceeds it, MAP_MAX_* is what has to move.
    CHECK(DS_DISTRICT_W <= DS_ISLAND_W);
}

KH_TEST(actor_pool_is_larger_than_the_snes_and_for_a_reason) {
    // The SNES held 32 because of WRAM and cycles, not OAM -- a full pool used
    // about 35 of its 128 entries.  Neither limit is the DS's, and the expanded
    // island asks for 69 prop actors off the map before anybody is placed on it,
    // so 32 here is not a sparse world, it is an unloadable one.
    // See docs/behaviour/divergences/002-ds-actor-pool.md.
    CHECK_EQ(SNES_MAX_ACTORS, 32);
    CHECK_EQ(MAX_ACTORS, 128);
    CHECK(MAX_ACTORS > 69 + TRANSIENT_ACTORS);

    // The pool holds the whole map; OAM holds what is on screen.  Those are
    // different numbers, and it is the second that bounds a dense map.
    CHECK_EQ(MAX_OBJECTS, 128);             // per engine; the HUD has its own
    CHECK(OBJ_BUDGET_SCENERY < MAX_OBJECTS);
    // Enough left for the transients, a four-quadrant boss and Sora.  Checked
    // against the numbers as well as the expression constants.h asserts, so
    // moving either one has to be deliberate.
    CHECK_EQ(MAX_OBJECTS - OBJ_BUDGET_SCENERY, 32);
    CHECK(OBJ_BUDGET_SCENERY + TRANSIENT_ACTORS + 4 + 1 <= MAX_OBJECTS);
}

KH_TEST(the_station_fits_the_screen_it_is_pinned_to) {
    // The static_asserts in constants.h already prove containment, so what is
    // left to check here is the thing they cannot: that the SNES radius would
    // NOT have fitted, which is the entire reason the DS draws a smaller disc.
    // See docs/behaviour/divergences/004-ds-station-radius.md.
    CHECK_EQ(SNES_DIVE_R, 110);
    CHECK_EQ(DIVE_R, 92);

    CHECK_EQ(DIVE_CAM_Y, 32);
    CHECK(DIVE_CY - SNES_DIVE_R < DIVE_CAM_Y);              // 18 < 32: clipped
    CHECK(DIVE_CY + SNES_DIVE_R > DIVE_CAM_Y + SCREEN_H);   // 238 > 224: clipped
    CHECK_EQ(DIVE_CAM_Y - (DIVE_CY - SNES_DIVE_R), 14);     // by 14 px each end
    CHECK_EQ((DIVE_CY + SNES_DIVE_R) - (DIVE_CAM_Y + SCREEN_H), 14);

    // ...and that it WOULD have fitted the SNES, by two pixels at each end --
    // which is why this never showed up until the screen got shorter.
    constexpr int SNES_DIVE_CAM_Y = (WORLD_H - SNES_SCREEN_H) / 2;   // 16
    CHECK_EQ(SNES_DIVE_CAM_Y, 16);
    CHECK(DIVE_CY - SNES_DIVE_R >= SNES_DIVE_CAM_Y);
    CHECK(DIVE_CY + SNES_DIVE_R <= SNES_DIVE_CAM_Y + SNES_SCREEN_H);
    CHECK_EQ((DIVE_CY - SNES_DIVE_R) - SNES_DIVE_CAM_Y, 2);

    // The DS disc leaves 4 px of void at each end, and the standable radius is
    // what the cast was re-placed against.
    CHECK_EQ((DIVE_CY - DIVE_R) - DIVE_CAM_Y, 4);
    CHECK_EQ(DIVE_R - DIVE_INSET, 78);
}

KH_TEST(night_density_is_per_screen_not_per_map) {
    // SHADOW_MAX was one number because the night and the fragment ran on maps
    // of similar size.  They no longer do, so it splits -- and the island's
    // figure preserves tiles-per-Shadow, not the count, because a DS screen sees
    // about 15% of the expanded island.
    // See docs/behaviour/divergences/003-ds-night-density.md.
    CHECK_EQ(SNES_SHADOW_MAX, 6);
    CHECK_EQ(SHADOW_MAX_NIGHT, 20);
    CHECK_EQ(SHADOW_MAX_FRAG, SNES_SHADOW_MAX);     // the map is unchanged

    // 196 walkable tiles / 6 == 33 per Shadow.  670 / 20 == 33 as well, give or
    // take the integer division -- that equality IS the derivation, so if either
    // side moves the divergence note has to be rewritten and not just the number.
    CHECK_EQ(196 / SNES_SHADOW_MAX, 32);            // 32.67
    CHECK_EQ(670 / SHADOW_MAX_NIGHT, 33);           // 33.5

    // Twenty of them need somewhere to come up, and the table is data now, so
    // this constant is only the buffer it is read into.
    CHECK_EQ(SNES_NIGHT_SPOTS, 10);
    CHECK(SHADOW_MAX_NIGHT <= MAX_SPOTS);
    CHECK(35 <= MAX_SPOTS);                         // what the island carries

    // Both budgets hold, which is why the argument above had to be about the
    // scene rather than about the hardware.
    CHECK(69 + 6 + SHADOW_MAX_NIGHT + TRANSIENT_ACTORS <= MAX_ACTORS);
    CHECK(29 + SHADOW_MAX_NIGHT <= OBJ_BUDGET_SCENERY);

    // The gap scales with how long the map takes to cross, not with the count.
    CHECK_EQ(SNES_SHADOW_GAP, 70);
    CHECK_EQ(SHADOW_GAP, 42);
    CHECK(SHADOW_MAX_NIGHT * SHADOW_GAP <= 2 * SNES_SHADOW_MAX * SNES_SHADOW_GAP);
}

KH_TEST(actor_pool_still_refuses_past_the_new_ceiling) {
    // Raising the ceiling must not have turned the refusal into a wrap: a pool
    // that quietly recycled slot 0 would overwrite whatever stood there.
    Actors a;
    a.clear();
    for (int i = 0; i < MAX_ACTORS; ++i)
        CHECK_EQ(a.spawn(ActType::Palm, World(), World()), i);
    CHECK_EQ(a.spawn(ActType::Palm, World(), World()), -1);
    CHECK_EQ(a.spawn(ActType::Shadow, World(), World()), -1);
    CHECK_EQ(a.count(ActType::Palm), MAX_ACTORS);
    CHECK_EQ(a.count(ActType::Shadow), 0);
    CHECK(a.type[0] == ActType::Palm);
}
