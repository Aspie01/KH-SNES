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
    // ...and the trap inside the trap.  MST 2 means main OBJ on A, B and E, so
    // the obvious way to "just put the sprites in C" is to write MST 2 to C --
    // which is a legal value there, and means hand the bank to the ARM7.  Not an
    // absent bank: a bank the other CPU owns.
    CHECK(canDo(Bank::C, Use::Arm7));
    CHECK(canDo(Bank::D, Use::Arm7));
    CHECK_EQ(mstFor(Bank::C, Use::Arm7), 2);
    CHECK_EQ(mstFor(Bank::C, Use::MainObj), mstFor(Bank::D, Use::MainObj));  // both -1
    for (Bank b : {Bank::A, Bank::B, Bank::E, Bank::F, Bank::G, Bank::H, Bank::I})
        CHECK(!canDo(b, Use::Arm7));
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

KH_TEST(vram_the_layers_are_assigned_and_the_ground_is_bg0_either_way) {
    // The first version of this header reserved bytes and said nothing about
    // layers, which was the real gap the adversarial pass found: with 3D on the
    // main engine has only three tilemap layers left, so layers are scarcer than
    // bytes and two later tasks picking their own would collide exactly the way
    // two picking their own addresses would.
    //
    // BG0 is the ground under BOTH renderers -- a text background with the 2D
    // one, the 3D image itself with the other -- which is what makes the two
    // GroundRenderers alternatives rather than rivals.
    CHECK_EQ(int(MAIN_GROUND_LAYER), int(Layer::Bg0));
    // The box is on the highest-priority layer, because priority is per-LAYER
    // here where the SNES had it per-tile.
    CHECK_EQ(int(MAIN_BOX_LAYER), int(Layer::Bg3));
    // Every main-engine layer is used at most once, and one is held back.
    const Layer mainUsed[] = {MAIN_GROUND_LAYER, MAIN_OVERLAY_LAYER, MAIN_BOX_LAYER};
    for (const Layer& a : mainUsed)
        for (const Layer& b : mainUsed)
            if (&a != &b) CHECK(int(a) != int(b));
    const Layer subUsed[] = {SUB_HUD_LAYER, SUB_MENU_LAYER, SUB_MINIMAP_LAYER};
    for (const Layer& a : subUsed)
        for (const Layer& b : subUsed)
            if (&a != &b) CHECK(int(a) != int(b));
    // The sub engine has no 3D, so all four of its layers are tilemaps and the
    // one left over is the margin there.
    CHECK_EQ(int(SUB_HUD_LAYER), int(Layer::Bg0));
}

KH_TEST(vram_a_character_ceiling_is_a_number_not_an_inference) {
    // A region's size in bytes is not the limit a caller hits -- the limit is how
    // many characters it may index, and a text layer's index is ten bits whatever
    // the reservation is.  GROUND_CHR was given all 1024 so it could not be
    // outgrown; the pass found the same reasoning had not been applied to the
    // other two, so their ceilings are stated rather than left to be worked out.
    CHECK_EQ(GROUND_CHR_MAX, TEXT_LAYER_CHARS);
    CHECK_EQ(GROUND_CHR_MAX, 1024);
    CHECK_EQ(UI_CHR_MAX, 512);
    CHECK_EQ(SUB_CHR_MAX, 512);
    CHECK_EQ(GROUND_CHR_MAX * CHAR_BYTES, int(GROUND_CHR.bytes));
    CHECK_EQ(UI_CHR_MAX * CHAR_BYTES, int(UI_CHR.bytes));

    // A layer at UI_CHR's base indexing past its ceiling reads the NEXT region's
    // bytes as characters.  This is the arithmetic that says which region, so a
    // future overlay author can see what they would be reading.
    const uint32_t past = UI_CHR.offset + uint32_t(UI_CHR_MAX) * CHAR_BYTES;
    CHECK_EQ(past, GROUND_MAP.offset);      // ...the ground's streaming window
    // The font is 128 characters, which is a quarter of the ceiling -- and there
    // are two copies, because character data is per-engine.
    CHECK_EQ(FONT_CHARS, 128);
    CHECK(FONT_CHARS * CHAR_BYTES * 2 < int(UI_CHR.bytes + SUB_CHR.bytes));
}

KH_TEST(vram_a_region_is_an_offset_and_these_are_the_addresses_it_becomes) {
    // Every Region in the header is an offset from a window base, and until this
    // pass NOTHING PERFORMED THAT ADDITION -- MAIN_BG_BASE, MAIN_OBJ_BASE,
    // SUB_BG_BASE and SUB_OBJ_BASE were read by no code, no test and no
    // assertion anywhere in the tree.  Four transcribed addresses with no
    // consumer is four chances for a wrong digit to survive to the device tier
    // and show up as a layer drawing the wrong thing.
    CHECK_EQ(windowBase(Use::MainBg), 0x06000000u);
    CHECK_EQ(windowBase(Use::SubBg), 0x06200000u);
    CHECK_EQ(windowBase(Use::MainObj), 0x06400000u);
    CHECK_EQ(windowBase(Use::SubObj), 0x06600000u);
    // A use that is not a window has no base, and gets 0 rather than a
    // plausible-looking one.
    CHECK_EQ(windowBase(Use::Lcdc), 0u);
    CHECK_EQ(windowBase(Use::Texture), 0u);

    CHECK_EQ(address(GROUND_CHR, Use::MainBg), 0x06000000u);
    CHECK_EQ(address(UI_CHR, Use::MainBg), 0x06008000u);
    CHECK_EQ(address(GROUND_MAP, Use::MainBg), 0x0600C000u);
    CHECK_EQ(address(BOX_MAP, Use::MainBg), 0x0600E000u);
    CHECK_EQ(address(OVERLAY_MAP, Use::MainBg), 0x0600E800u);
    CHECK_EQ(address(OBJ_RESIDENT, Use::MainObj), 0x06400000u);
    CHECK_EQ(address(SUB_CHR, Use::SubBg), 0x06200000u);
    CHECK_EQ(address(HUD_MAP, Use::SubBg), 0x06204000u);
    CHECK_EQ(address(MENU_MAP, Use::SubBg), 0x06204800u);
    CHECK_EQ(address(MINIMAP_MAP, Use::SubBg), 0x06205000u);
    CHECK_EQ(address(SUB_OBJ_CHR, Use::SubObj), 0x06600000u);

    // ...and the composition agrees with the register arithmetic, which is the
    // property that makes both usable: a base written to BGxCNT and the address
    // a DMA writes to must name the same bytes.
    CHECK_EQ(address(GROUND_CHR, Use::MainBg),
             windowBase(Use::MainBg) + uint32_t(GROUND_CHR.charBase()) * CHAR_BLOCK);
    CHECK_EQ(address(HUD_MAP, Use::SubBg),
             windowBase(Use::SubBg) + uint32_t(HUD_MAP.mapBase()) * MAP_BLOCK);
}

KH_TEST(vram_the_seven_spans_of_the_graphics_address_space_are_disjoint) {
    // Seven bases, transcribed from GBATEK's memory map, previously checked only
    // against themselves.  These are everything the two display controllers can
    // be pointed at, and they are mutually exclusive -- so a wrong digit in any
    // one of them lands inside another and this fails.
    CHECK(addressMapDisjoint());
    for (const AddressSpan& a : ADDRESS_MAP) {
        CHECK(a.bytes > 0);
        CHECK(a.what != nullptr && a.what[0] != '\0');
    }
    // LCDC is the nine banks end to end, so its span is the whole of VRAM and it
    // has to reach exactly as far as the last bank does.
    CHECK_EQ(LCDC_ADDR[0], 0x06800000u);
    CHECK_EQ(LCDC_ADDR[0] + totalVram(), 0x068A4000u);
    CHECK_EQ(lcdcAddr(Bank::I) + bankSize(Bank::I), LCDC_ADDR[0] + totalVram());
    // Palette RAM is four regions of 512 bytes and not part of VRAM at all.
    CHECK_EQ(PAL_MAIN_BG + 4 * PAL_REGION_BYTES, 0x05000800u);
}

KH_TEST(vram_the_ofs_field_is_checked_against_the_hardware_not_assumed_zero) {
    // Nine assignments carry an `ofs` and an `offset`, eighteen numbers, and
    // nothing in the tree read one of them before this pass.  All eighteen are
    // zero, so nothing was wrong -- but the recovery paths at the bottom of the
    // header invite a later task to set one, and there are three separate traps
    // waiting when it does.
    CHECK(everyOffsetIsLegal());
    for (const Assignment& a : ASSIGNMENTS) {
        CHECK(ofsMax(a.bank, a.use) >= 0);
        CHECK(int(a.ofs) <= ofsMax(a.bank, a.use));
    }

    // Trap one: E, H and I have no OFS field at all.  GBATEK, "Offset not used
    // by VRAM-E,H,I".  A non-zero ofs there is a bit the silicon ignores.
    CHECK_EQ(ofsMax(Bank::E, Use::MainObj), 0);
    CHECK_EQ(ofsMax(Bank::E, Use::MainBg), 0);
    CHECK_EQ(ofsMax(Bank::H, Use::SubBg), 0);
    CHECK_EQ(ofsMax(Bank::I, Use::SubObj), 0);
    CHECK_EQ(bankWindowOffset(Bank::E, 3), 0u);     // no field: it cannot move

    // Trap two: the range depends on the USE.  A and B take 0..3 as main BG and
    // only 0..1 as main OBJ -- "(OFS.1 must be zero)".
    CHECK_EQ(ofsMax(Bank::A, Use::MainBg), 3);
    CHECK_EQ(ofsMax(Bank::A, Use::MainObj), 1);
    CHECK_EQ(ofsMax(Bank::B, Use::MainObj), 1);
    CHECK_EQ(ofsMax(Bank::C, Use::Arm7), 1);
    // ...and an illegal pair is -1, so it can never be mistaken for "no field".
    CHECK_EQ(ofsMax(Bank::C, Use::MainObj), -1);
    CHECK_EQ(ofsMax(Bank::H, Use::MainBg), -1);

    // Trap three: F and G do not step linearly.  4000h*OFS.0 + 10000h*OFS.1,
    // so OFS 2 is 64 KiB and not 32, and the slot is OFS.0 + OFS.1*4.
    CHECK_EQ(bankWindowOffset(Bank::F, 0), 0u);
    CHECK_EQ(bankWindowOffset(Bank::F, 1), 16u * KiB);
    CHECK_EQ(bankWindowOffset(Bank::F, 2), 64u * KiB);          // not 32
    CHECK_EQ(bankWindowOffset(Bank::F, 3), 80u * KiB);
    CHECK_EQ(bankSlot(Bank::F, Use::TexPalette, 0), 0);
    CHECK_EQ(bankSlot(Bank::F, Use::TexPalette, 1), 1);
    CHECK_EQ(bankSlot(Bank::G, Use::TexPalette, 2), 4);         // not 2
    CHECK_EQ(bankSlot(Bank::G, Use::TexPalette, 3), 5);
    // Which is why the header says slots 2 and 3 come only from E.
    for (uint8_t o = 0; o < 4; ++o) {
        const int s = bankSlot(Bank::F, Use::TexPalette, o);
        CHECK(s != 2 && s != 3);
    }
    // A-D are the linear ones, a whole bank a step, and their texture slot is
    // just the OFS.
    CHECK_EQ(bankWindowOffset(Bank::A, 2), 256u * KiB);
    CHECK_EQ(bankSlot(Bank::C, Use::Texture, 2), 2);

    // And the two places that named a slot now agree by construction rather
    // than by both happening to say zero.
    CHECK_EQ(slotAssignedTo(Use::Texture), TEXTURE_SLOT);
    CHECK_EQ(slotAssignedTo(Use::TexPalette), TEXTURE_PALETTE_SLOT);
    CHECK_EQ(slotAssignedTo(Use::SubObjExtPal), -1);    // nothing is assigned it
}

KH_TEST(vram_the_scene_palette_is_reloaded_rather_than_partitioned) {
    // Sixteen sub-palettes, nine OBJ and seven BG, all inside the sixteen a
    // standard region holds -- so nothing overflows.  But the pipeline hard-codes
    // sub-palette 0 for every scene's map, so the seven BG palettes want to BE
    // sub-palette 0 at different times.  That is what the SNES did, and it is why
    // index 0 keeps working as the backdrop.
    CHECK_EQ(SCENE_BG_SUBPALETTE, 0);
    CHECK_EQ(PAL_SUBPALETTES * PAL_SUBPALETTE_COLOURS * 2, int(PAL_REGION_BYTES));
    // Each engine gets its own BG and OBJ region, 512 bytes apiece, and the
    // sprite palettes are a SEPARATE region rather than the top half of a shared
    // table as on the SNES.
    CHECK_EQ(PAL_MAIN_OBJ - PAL_MAIN_BG, PAL_REGION_BYTES);
    CHECK_EQ(PAL_SUB_BG - PAL_MAIN_OBJ, PAL_REGION_BYTES);
    CHECK_EQ(PAL_SUB_OBJ - PAL_SUB_BG, PAL_REGION_BYTES);
}
