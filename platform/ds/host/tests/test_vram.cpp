// vram_map.h -- the frozen bank allocation.
//
// THIS INCLUDE IS FIRST ON PURPOSE.  §M4's exit criterion is that the header
// compiles standalone, and the only way to mean that is a translation unit whose
// first line of work is including it: no constants.h, no fixed.h, nothing that
// might be supplying a type it forgot to ask for.  The device tier will include
// it before anything else exists, so it has to hold up that way.
#include "vram_map.h"

// ...and only then the things the test itself needs.  gen/assets.h is included
// second deliberately: it defines KH_ASSETS_H_INCLUDED, so the cross-file
// assertions in vram_map.h are NOT active for this translation unit -- they fire
// only where both headers meet, which is what test_assets.cpp is for.  Including
// it here would prove nothing about the standalone case.
#include <cstring>
#include <initializer_list>

#include "check.h"
#include "gen/assets.h"

using namespace kh;
using namespace kh::vram;

KH_TEST(vram_the_bank_table_is_the_hardware_table) {
    // Transcribed from GBATEK "DS Memory Control - VRAM".  If any of these is
    // wrong then every address derived from it is wrong in the same direction,
    // which is the kind of error that looks like a driver bug for a week.
    CHECK_EQ(bankSize(Bank::A), 128u * KiB);
    CHECK_EQ(bankSize(Bank::B), 128u * KiB);
    CHECK_EQ(bankSize(Bank::C), 128u * KiB);
    CHECK_EQ(bankSize(Bank::D), 128u * KiB);
    CHECK_EQ(bankSize(Bank::E), 64u * KiB);
    CHECK_EQ(bankSize(Bank::F), 16u * KiB);
    CHECK_EQ(bankSize(Bank::G), 16u * KiB);
    CHECK_EQ(bankSize(Bank::H), 32u * KiB);
    CHECK_EQ(bankSize(Bank::I), 16u * KiB);

    uint32_t total = 0;
    for (unsigned b = 0; b < unsigned(Bank::Count); ++b) total += bankSize(Bank(b));
    CHECK_EQ(total, 656u * KiB);

    CHECK_EQ(lcdcAddr(Bank::A), 0x06800000u);
    CHECK_EQ(lcdcAddr(Bank::E), 0x06880000u);
    CHECK_EQ(lcdcAddr(Bank::I), 0x068A0000u);
    // Contiguous in LCDC, which is what makes the nine banks one region there.
    for (unsigned b = 1; b < unsigned(Bank::Count); ++b)
        CHECK_EQ(lcdcAddr(Bank(b)), lcdcAddr(Bank(b - 1)) + bankSize(Bank(b - 1)));
}

KH_TEST(vram_the_capability_matrix_refuses_the_tempting_illegal_mappings) {
    // C and D are big and, under this allocation, idle -- so "just put the
    // sprites in C" is the mistake someone will actually make.  The main-OBJ
    // rows of GBATEK's table list A, B, E, F and G, and no others.
    CHECK(!canDo(Bank::C, Use::MainObj));
    CHECK(!canDo(Bank::D, Use::MainObj));
    CHECK(canDo(Bank::A, Use::MainObj));
    CHECK(canDo(Bank::B, Use::MainObj));
    CHECK(canDo(Bank::E, Use::MainObj));
    CHECK(canDo(Bank::F, Use::MainObj));
    CHECK(canDo(Bank::G, Use::MainObj));

    // Texture is A-D only.  E has 64 KiB and cannot hold a texture; it can hold
    // the palette for one.
    for (Bank b : {Bank::A, Bank::B, Bank::C, Bank::D}) CHECK(canDo(b, Use::Texture));
    for (Bank b : {Bank::E, Bank::F, Bank::G, Bank::H, Bank::I})
        CHECK(!canDo(b, Use::Texture));
    for (Bank b : {Bank::E, Bank::F, Bank::G}) CHECK(canDo(b, Use::TexPalette));

    // The sub engine's two windows have three and two candidates respectively.
    CHECK(canDo(Bank::C, Use::SubBg));
    CHECK(canDo(Bank::H, Use::SubBg));
    CHECK(canDo(Bank::I, Use::SubBg));
    CHECK(!canDo(Bank::D, Use::SubBg));
    CHECK(canDo(Bank::D, Use::SubObj));
    CHECK(canDo(Bank::I, Use::SubObj));
    CHECK(!canDo(Bank::C, Use::SubObj));

    // H is the constrained one, and that is why it is spent rather than held.
    CHECK(!canDo(Bank::H, Use::MainBg));
    CHECK(!canDo(Bank::H, Use::MainObj));
    CHECK(!canDo(Bank::H, Use::Texture));
    CHECK(!canDo(Bank::H, Use::SubObj));
    CHECK(canDo(Bank::H, Use::SubBg));
    CHECK(canDo(Bank::H, Use::SubBgExtPal));

    // Extended palettes: BG from E/F/G on the main engine and H on the sub;
    // OBJ from F/G on the main engine and I on the sub.  E cannot do OBJ.
    CHECK(canDo(Bank::E, Use::MainBgExtPal));
    CHECK(!canDo(Bank::E, Use::MainObjExtPal));
    CHECK(canDo(Bank::F, Use::MainObjExtPal));
    CHECK(canDo(Bank::G, Use::MainObjExtPal));
    CHECK(canDo(Bank::I, Use::SubObjExtPal));
}

KH_TEST(vram_the_mst_values_are_the_ones_the_registers_take) {
    // There is no formula here, only the table -- which is exactly why it is
    // worth a test.  Sub BG is MST 4 on C and MST 1 on H and I; sub OBJ is MST 4
    // on D and MST 2 on I.  A single "sub = 4" rule would half work, and half
    // working is the bad case.
    CHECK_EQ(mstFor(Bank::B, Use::MainBg), 1);
    CHECK_EQ(mstFor(Bank::E, Use::MainObj), 2);
    CHECK_EQ(mstFor(Bank::A, Use::Texture), 3);
    CHECK_EQ(mstFor(Bank::F, Use::TexPalette), 3);
    CHECK_EQ(mstFor(Bank::C, Use::SubBg), 4);
    CHECK_EQ(mstFor(Bank::H, Use::SubBg), 1);
    CHECK_EQ(mstFor(Bank::I, Use::SubBg), 1);
    CHECK_EQ(mstFor(Bank::D, Use::SubObj), 4);
    CHECK_EQ(mstFor(Bank::I, Use::SubObj), 2);
    CHECK_EQ(mstFor(Bank::H, Use::SubBgExtPal), 2);
    CHECK_EQ(mstFor(Bank::I, Use::SubObjExtPal), 3);
    CHECK_EQ(mstFor(Bank::G, Use::MainObjExtPal), 5);
    CHECK_EQ(mstFor(Bank::A, Use::Lcdc), 0);

    // An illegal pair is -1, not a plausible number.
    CHECK_EQ(mstFor(Bank::C, Use::MainObj), -1);
    CHECK_EQ(mstFor(Bank::H, Use::MainBg), -1);
    CHECK_EQ(mstFor(Bank::E, Use::Texture), -1);
}

KH_TEST(vram_every_bank_has_exactly_one_disposition) {
    // VRAMCNT holds one MST, so a bank with two roles is not a tight allocation,
    // it is a bug -- and a bank with none is 128 KiB nobody remembered.
    CHECK_EQ(ASSIGNMENT_COUNT, int(Bank::Count));
    int seen[int(Bank::Count)] = {};
    for (const Assignment& a : ASSIGNMENTS) {
        ++seen[unsigned(a.bank)];
        CHECK(mstFor(a.bank, a.use) >= 0);      // legal
        CHECK(a.why != nullptr && a.why[0] != '\0');
    }
    for (int n : seen) CHECK_EQ(n, 1);
}

KH_TEST(vram_no_two_regions_of_a_window_overlap) {
    // The static_asserts already prove this at compile time; doing it again at
    // run time is not redundant, because the compile-time version is written
    // pairwise by hand and a region added without its pair would slip through.
    // This loop cannot.
    struct Named { const char* name; Region r; };

    const Named mainBg[] = {
        {"ground chr", GROUND_CHR}, {"ui chr", UI_CHR}, {"ground map", GROUND_MAP},
        {"box map", BOX_MAP}, {"overlay map", OVERLAY_MAP}, {"spare", MAIN_BG_SPARE},
    };
    for (const Named& a : mainBg)
        for (const Named& b : mainBg)
            if (&a != &b) CHECK(!a.r.overlaps(b.r));

    const Named mainObj[] = {{"resident", OBJ_RESIDENT}, {"b64", OBJ_BOUNDARY64}};
    for (const Named& a : mainObj)
        for (const Named& b : mainObj)
            if (&a != &b) CHECK(!a.r.overlaps(b.r));

    const Named subBg[] = {
        {"sub chr", SUB_CHR}, {"hud", HUD_MAP}, {"menu", MENU_MAP},
        {"minimap", MINIMAP_MAP}, {"spare", SUB_BG_SPARE},
    };
    for (const Named& a : subBg)
        for (const Named& b : subBg)
            if (&a != &b) CHECK(!a.r.overlaps(b.r));

    // ...and every window's regions are inside the bank mapped there.
    for (const Named& n : mainBg) CHECK(n.r.end() <= bankSize(Bank::B));
    for (const Named& n : mainObj) CHECK(n.r.end() <= bankSize(Bank::E));
    for (const Named& n : subBg) CHECK(n.r.end() <= bankSize(Bank::H));
    CHECK(SUB_OBJ_CHR.end() <= bankSize(Bank::I));

    // ...and inside what the window itself can hold, which is a smaller number
    // than the bank in no case here but would be if two banks were stacked.
    for (const Named& n : mainBg) CHECK(n.r.end() <= MAIN_BG_MAX);
    for (const Named& n : mainObj) CHECK(n.r.end() <= MAIN_OBJ_MAX);
    for (const Named& n : subBg) CHECK(n.r.end() <= SUB_BG_MAX);
    CHECK(SUB_OBJ_CHR.end() <= SUB_OBJ_MAX);
}

KH_TEST(vram_every_base_survives_the_round_trip_through_its_register) {
    // A base is stored as a small integer and multiplied back out by hardware.
    // What matters is that the multiply returns the address we meant -- an
    // offset that is not a whole number of blocks silently truncates DOWN, into
    // whatever region precedes it.
    struct Chr { Region r; int base; };
    const Chr chrs[] = {{GROUND_CHR, 0}, {UI_CHR, 2}, {SUB_CHR, 0}};
    for (const Chr& c : chrs) {
        CHECK_EQ(c.r.charBase(), c.base);
        CHECK_EQ(uint32_t(c.r.charBase()) * CHAR_BLOCK, c.r.offset);
        CHECK(c.r.charBase() <= CHAR_BASE_MAX);
    }

    struct Map { Region r; int base; };
    const Map maps[] = {
        {GROUND_MAP, 24}, {BOX_MAP, 28}, {OVERLAY_MAP, 29},
        {HUD_MAP, 8}, {MENU_MAP, 9}, {MINIMAP_MAP, 10},
    };
    for (const Map& m : maps) {
        CHECK_EQ(m.r.mapBase(), m.base);
        CHECK_EQ(uint32_t(m.r.mapBase()) * MAP_BLOCK, m.r.offset);
        CHECK(m.r.mapBase() <= MAP_BASE_MAX);
    }

    // Nothing needs DISPCNT's engine-wide term, which is the property that lets
    // the same arithmetic serve both engines -- engine B has no such term.
    CHECK(MAIN_BG_SPARE.end() <= BASE_REACH);
    CHECK(SUB_BG_SPARE.end() <= BASE_REACH);
}

KH_TEST(vram_the_reservations_match_what_the_pipeline_emits) {
    // The one place this file and gen/assets.h have to agree.  They are
    // generated and written by different things, so drift is the default.
    CHECK_EQ(OBJ_RESIDENT.bytes, uint32_t(OBJ_REACH));
    CHECK(OBJ_RESIDENT_BYTES <= OBJ_RESIDENT.bytes);
    CHECK_EQ(OBJ_BOUNDARY64.end(), uint32_t(OBJ_REACH) * 2);

    // Every scene's characters fit the ground reservation, with the reservation
    // being the architectural maximum rather than the current worst case -- so
    // this cannot start failing when someone draws a busier map.
    CHECK_EQ(GROUND_CHR.bytes, uint32_t(MAP_TILE_MASK + 1) * 32u);
    for (const SceneAsset& s : SCENE_ASSETS)
        CHECK(uint32_t(s.chars) * 32u <= GROUND_CHR.bytes);

    // The biggest map any scene will stream a window out of is 64x64 entries,
    // which is what GROUND_MAP holds.
    CHECK_EQ(GROUND_MAP.bytes, uint32_t(BG_MAX_CHARS) * uint32_t(BG_MAX_CHARS) * 2u);

    // The font is 128 characters and shares UI_CHR with a future overlay.
    uint32_t font = 0;
    for (const SpriteAsset& s : SPRITE_ASSETS)
        if (std::strcmp(s.name, "hudchr") == 0) font = s.bytes;
    CHECK_EQ(font, 4096u);
    CHECK(font <= UI_CHR.bytes);
    CHECK(font <= SUB_CHR.bytes);
}

KH_TEST(vram_both_ground_renderers_fit_without_remapping_a_bank) {
    // The 3D quad ground is a second GroundRenderer, not a different build, so
    // switching to it must not need a VRAMCNT write -- a bank remapped while the
    // display controller is reading it is a torn frame at best.
    //
    // It works because the two renderers use DISJOINT resources: the 2D one uses
    // GROUND_CHR and GROUND_MAP in the main BG window, the 3D one uses the
    // texture slot and BG0, and neither touches the other's.  Everything the two
    // share -- the dialogue box, the bottom screen, the sprites -- is allocated
    // once and read by both.
    CHECK_EQ(TEXTURE_SLOT, 0);
    CHECK_EQ(TEXTURE_PALETTE_SLOT, 0);

    bool textureBankMapped = false, texPalBankMapped = false;
    for (const Assignment& a : ASSIGNMENTS) {
        if (a.use == Use::Texture) {
            textureBankMapped = true;
            CHECK(canDo(a.bank, Use::Texture));
            // A texture slot is a whole 128 KiB bank; the atlas is far smaller,
            // but the granule is not divisible.
            CHECK_EQ(bankSize(a.bank), 128u * KiB);
        }
        if (a.use == Use::TexPalette) {
            texPalBankMapped = true;
            CHECK(canDo(a.bank, Use::TexPalette));
            // Sixteen 16-colour palettes is 512 bytes and any of E/F/G holds it.
            CHECK(bankSize(a.bank) >= 16u * 16u * 2u);
        }
    }
    CHECK(textureBankMapped);
    CHECK(texPalBankMapped);

    // The 2D ground's regions exist whether or not the 3D one is active, and
    // vice versa: no bank serves both, so no bank has to change MST.
    CHECK(GROUND_CHR.bytes > 0 && GROUND_MAP.bytes > 0);
}
