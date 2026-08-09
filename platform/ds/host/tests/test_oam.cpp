// OAM -- the sprite budget's other half, and the place two headers meet.
//
// WHY THIS IS NOT IN test_vram.cpp.  That file's first line of work is
// `#include "vram_map.h"`, deliberately, because §M4's exit criterion is that
// the header compiles standalone and the only way to mean that is a translation
// unit that supplies it nothing.  The guarded cross-file blocks at the bottom of
// vram_map.h are therefore INACTIVE there -- which is the point, and which means
// they need somewhere else to fire.
//
// So this file includes constants.h FIRST and vram_map.h second, which is the
// order a real consumer uses, and in which `KH_CONSTANTS_H_INCLUDED` is defined
// by the time vram_map.h is parsed.  Its static_asserts are half the value of
// this file; the cases below are the other half, because a static_assert says
// which invariant broke and a test says what the numbers actually were.
#include "constants.h"

#include "vram_map.h"

#include "check.h"

using namespace kh;
using namespace kh::vram;

KH_TEST(oam_is_allocated_and_not_merely_addressed) {
    // The file named OAM_MAIN and OAM_SUB and stopped: no size, no entry count,
    // no partition, and nothing in the tree read either address.  Everything
    // else in that header is allocated to the byte.
    CHECK_EQ(OAM_BYTES, 1024u);
    CHECK_EQ(OAM_ENTRIES, 128);
    CHECK_EQ(OAM_ENTRY_BYTES, 8);
    CHECK_EQ(OAM_ENTRIES * OAM_ENTRY_BYTES, int(OAM_BYTES));

    // The two are ADJACENT, which is the whole reason the entry count matters:
    // there is no guard region to run into.  A 129th main-engine entry is the
    // sub engine's entry 0, so the symptom is a sprite appearing on the OTHER
    // SCREEN -- nothing faults and nothing warns.
    CHECK_EQ(OAM_SUB, OAM_MAIN + OAM_BYTES);
    CHECK_EQ(oamEntry(OAM_MAIN, OAM_ENTRIES), OAM_SUB);
    CHECK_EQ(oamEntry(OAM_MAIN, 0), 0x07000000u);
    CHECK_EQ(oamEntry(OAM_SUB, 0), 0x07000400u);
    CHECK_EQ(oamEntry(OAM_SUB, OAM_ENTRIES - 1) + OAM_ENTRY_BYTES, 0x07000800u);
}

KH_TEST(oam_an_entry_is_eight_bytes_and_only_six_of_them_are_yours) {
    // The affine matrices are INTERLEAVED through OAM, not stored after it:
    // matrix n is the fourth halfword of entries 4n..4n+3.  The SNES had no
    // equivalent -- 4 bytes plus 2 bits in a separate high table, and no
    // matrices at all -- so there is no oracle that can find this, which is
    // exactly why it is pinned.
    CHECK_EQ(OAM_ATTR_BYTES, 6);
    CHECK_EQ(OAM_AFFINE_SLOTS, 32);
    CHECK_EQ(OAM_AFFINE_SLOTS * OAM_AFFINE_BYTES,
             OAM_ENTRIES * (OAM_ENTRY_BYTES - OAM_ATTR_BYTES));

    // PA of matrix 0 is the last halfword of entry 0; PB is the last halfword
    // of entry 1, not the next halfword along.
    CHECK_EQ(oamAffine(OAM_MAIN, 0, 0), OAM_MAIN + 6);
    CHECK_EQ(oamAffine(OAM_MAIN, 0, 1), OAM_MAIN + 14);
    CHECK_EQ(oamAffine(OAM_MAIN, 0, 3), OAM_MAIN + 30);
    CHECK_EQ(oamAffine(OAM_MAIN, 1, 0), OAM_MAIN + 38);
    CHECK_EQ(oamAffine(OAM_MAIN, OAM_AFFINE_SLOTS - 1, 3), OAM_MAIN + OAM_BYTES - 2);

    // Every matrix halfword is inside OAM, and no two of them are the same
    // address -- which is the property a packed-array implementation breaks.
    for (int s = 0; s < OAM_AFFINE_SLOTS; ++s)
        for (int p = 0; p < 4; ++p) {
            const uint32_t at = oamAffine(OAM_MAIN, s, p);
            CHECK(at >= OAM_MAIN && at + 2 <= OAM_MAIN + OAM_BYTES);
            CHECK_EQ((at - OAM_MAIN) % OAM_ENTRY_BYTES, OAM_ATTR_BYTES);
        }

    // ...and the arithmetic a naive implementation would use instead.  128
    // packed 6-byte records is 768 bytes, so it fits, does not fault, and puts
    // every sprite after the first at the wrong address.
    CHECK_EQ(OAM_ENTRIES * OAM_ATTR_BYTES, 768);
    CHECK(OAM_ENTRIES * OAM_ATTR_BYTES < int(OAM_BYTES));
}

KH_TEST(oam_is_where_the_gameplay_object_budget_comes_from) {
    // constants.h's docstring says it holds no hardware numbers.  MAX_OBJECTS is
    // one: it is the OAM entry count, and it was asserted against nothing --
    // constants.h's own static_assert compared the budget with a 128 typed two
    // lines above it, which is a tautology dressed as a check.
    CHECK_EQ(MAX_OBJECTS, OAM_ENTRIES);
    CHECK_EQ(OBJ_BUDGET_SCENERY, 96);
    CHECK(OBJ_BUDGET_SCENERY + TRANSIENT_ACTORS + 4 + 1 <= OAM_ENTRIES);

    // The pool is not the budget, and the two being equal is a coincidence of
    // two unrelated arguments: MAX_ACTORS is 128 because the island needs 69
    // props to EXIST, OAM_ENTRIES is 128 because the hardware says so.  So a
    // full pool cannot be drawn, and does not need to be -- the pool holds the
    // whole map, OAM holds what is on screen, and check_map.py slides a camera
    // window over every map to keep the second inside the budget.
    CHECK_EQ(MAX_ACTORS, 128);
    CHECK(OBJ_BUDGET_SCENERY < MAX_ACTORS);

    // The SNES's pool fitted its OAM four times over, which is why this was
    // never a constraint there and is one here.
    CHECK(SNES_MAX_ACTORS * 4 <= OAM_ENTRIES);
}

KH_TEST(oam_the_sub_engine_has_its_own_and_a_smaller_tile_ceiling) {
    // The two engines' OAMs do not compete -- that is the reason the HUD can be
    // sprites at all.  But the sub engine's sprite CHARACTERS come from bank I,
    // which is 16 KiB, and a tile number is ten bits: numbers 512 to 1023 name
    // addresses past the end of the bank.  Engine B reads unmapped VRAM and
    // draws whatever comes back.
    //
    // This is the same over-index the character ceilings were stated to prevent
    // (UI_CHR_MAX, SUB_CHR_MAX), one window across, and the pass that stated
    // those did not apply the reasoning to sprites.
    CHECK_EQ(SUB_OBJ_TILES, 512);
    CHECK_EQ(OBJ_TILE_NUMBERS, 1024);
    CHECK(SUB_OBJ_TILES < OBJ_TILE_NUMBERS);
    CHECK_EQ(uint32_t(SUB_OBJ_TILES) * uint32_t(CHAR_BYTES), SUB_OBJ_CHR.bytes);
    CHECK_EQ(SUB_OBJ_CHR.bytes, bankSize(Bank::I));

    // The main engine is the other way round: there the REACH binds and the
    // bank has room to spare, which is what makes OBJ_BOUNDARY64 a reserve.
    CHECK(OBJ_RESIDENT.bytes < bankSize(Bank::E));
    CHECK_EQ(OBJ_RESIDENT.bytes + OBJ_BOUNDARY64.bytes, bankSize(Bank::E));
}
