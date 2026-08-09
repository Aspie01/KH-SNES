// §M7's two-screen initialisation, checked against §M4's frozen allocation.
//
// READ THIS BEFORE BELIEVING A GREEN RUN.  Nothing in this container has
// executed an ARM instruction.  devkitPro is absent (tools/check_device.py),
// there is no emulator, and initScreens() has never run on a DS.  What these
// cases prove is that it WRITES WHAT vram_map.h SAYS IT SHOULD -- the right
// values, to the right addresses, at the right widths, in an order that never
// leaves a frame half-configured, and touching nothing it must not touch.
//
// That is worth having, and it is worth being precise about why: every one of
// those four properties is a thing a careful person gets wrong silently. A bank
// programmed with the wrong MST does not fault, it draws the last thing at that
// address. A 16-bit store to an 8-bit register configures its neighbour too. A
// loop that adds one to a register address walks over WRAMCNT and repartitions
// the memory the two processors share. None of those is caught by a compiler,
// and until this file none of them was caught by anything.
//
// What it emphatically does not prove is that GBATEK is right, that the values
// are the ones the hardware wants, or that a picture appears. The first frame
// on real hardware is still the first frame on real hardware.

#include "check.h"
#include "constants.h"
#include "gen/assets.h"
#include "init.h"
#include "mmio.h"
#include "vram_map.h"

using namespace kh;
using namespace kh::device;
using namespace kh::vram;

namespace {

// Every case starts from an empty log.  The recorder is file-scope state in
// mmio_host.cpp, and the suite runs its cases in both orders (Makefile.host's
// `run` target), so a case that inherited a previous one's writes would pass or
// fail depending on the direction -- which is exactly the failure mode running
// backwards exists to find.
struct Recorded {
    Mmio io;
    Recorded() { mmioLogClear(); initScreens(io); }
};

}  // namespace

KH_TEST(devinit_programs_the_nine_banks_exactly_as_vram_map_froze_them) {
    Recorded r;
    CHECK(!mmioLogOverflowed());

    // The values are not restated here -- vram_map.h's vramcnt() is the
    // authority and it is asserted there byte by byte.  What this pins is that
    // the initialisation actually WRITES them, which is a different claim and
    // the one nothing checked.
    for (unsigned b = 0; b < unsigned(Bank::Count); ++b) {
        const Bank bank = Bank(b);
        CHECK_EQ(mmioLast(vramcntAddr(bank)), int64_t(vramcnt(bank)));
    }

    // Every bank exactly once.  Twice would mean two places believe they own
    // the allocation, which is the condition vram_map.h was frozen to prevent.
    for (unsigned b = 0; b < unsigned(Bank::Count); ++b)
        CHECK_EQ(mmioTouches(vramcntAddr(Bank(b))), 1);
}

KH_TEST(devinit_never_writes_wramcnt) {
    // THE ONE THAT COULD ONLY BE ASSERTED ABOUT A TABLE BEFORE.
    //
    // 0x04000247 is WRAMCNT, between VRAMCNT_G and VRAMCNT_H.  vram_map.h can
    // assert that its ADDRESS TABLE skips it; only a log of actual writes can
    // assert that the CODE does.  A loop over `0x04000240 + i` would satisfy
    // every static_assert in the tree and fail here.
    Recorded r;
    CHECK_EQ(mmioTouches(WRAMCNT_ADDR), 0);
    CHECK_EQ(mmioLast(WRAMCNT_ADDR), int64_t(-1));      // never written at all

    // ...and the consequence, stated so the number is on the record: bank H's
    // byte read as a WRAMCNT value is allocation mode 1, which gives the ARM9
    // only the second 16 KiB and hands the first to the ARM7 mid-boot.
    CHECK_EQ(vramcnt(Bank::H) & 0x03, 1);
}

KH_TEST(devinit_writes_vramcnt_one_byte_at_a_time) {
    // VRAMCNT is an eight-bit register.  A 16-bit store to 0x04000240 sets bank
    // A *and* bank B, so the tidy-looking loop over uint16_t would configure
    // four banks and clobber four -- and a test that only compared the four it
    // meant to write would pass.  mmioTouches() answers by OVERLAP for this
    // reason: it sees a write that lands on an address without being addressed
    // to it.
    Recorded r;
    const MmioWrite* log = mmioLog();
    for (int i = 0; i < mmioLogCount(); ++i) {
        bool isVramcnt = false;
        for (unsigned b = 0; b < unsigned(Bank::Count); ++b)
            if (log[i].addr == vramcntAddr(Bank(b))) isVramcnt = true;
        if (isVramcnt) CHECK_EQ(int(log[i].width), 1);
    }
    // Each bank's byte is touched by exactly one write -- so no wide store
    // reached it sideways from a neighbour.
    for (unsigned b = 0; b < unsigned(Bank::Count); ++b)
        CHECK_EQ(mmioTouches(vramcntAddr(Bank(b)), 1), 1);
}

KH_TEST(devinit_points_every_layer_at_the_region_reserved_for_it) {
    Recorded r;

    // The bases are DERIVED from vram_map.h's Regions, so this decomposes the
    // written register back into a char base and a map base and checks those
    // against the Regions rather than against a remembered number.
    struct Layout { uint32_t reg; Region chars; Region map; };
    const Layout main_[] = {
        {bgcnt(BGCNT_MAIN, int(MAIN_GROUND_LAYER)), GROUND_CHR, GROUND_MAP},
        {bgcnt(BGCNT_MAIN, int(MAIN_OVERLAY_LAYER)), UI_CHR, OVERLAY_MAP},
        {bgcnt(BGCNT_MAIN, int(MAIN_BOX_LAYER)), UI_CHR, BOX_MAP},
    };
    for (const Layout& l : main_) {
        const int64_t v = mmioLast(l.reg);
        CHECK(v >= 0);
        CHECK_EQ(int((v >> 2) & 0x0F), l.chars.charBase());
        CHECK_EQ(int((v >> 8) & 0x1F), l.map.mapBase());
        CHECK_EQ(int((v >> 7) & 1), 0);     // 16-colour: the palette nibble lives
    }

    const Layout sub[] = {
        {bgcnt(BGCNT_SUB, int(SUB_HUD_LAYER)), SUB_CHR, HUD_MAP},
        {bgcnt(BGCNT_SUB, int(SUB_MENU_LAYER)), SUB_CHR, MENU_MAP},
        {bgcnt(BGCNT_SUB, int(SUB_MINIMAP_LAYER)), SUB_CHR, MINIMAP_MAP},
    };
    for (const Layout& l : sub) {
        const int64_t v = mmioLast(l.reg);
        CHECK(v >= 0);
        CHECK_EQ(int((v >> 2) & 0x0F), l.chars.charBase());
        CHECK_EQ(int((v >> 8) & 0x1F), l.map.mapBase());
        CHECK_EQ(int((v >> 7) & 1), 0);
    }

    // The box is in front of everything, which on this machine takes TWO facts:
    // priority 0, and the tie-break that a lower-numbered BG wins.  The box is
    // BG3, the highest number, so it loses every tie -- and priority 0 is what
    // means it is never in one.
    CHECK_EQ(mmioLast(bgcnt(BGCNT_MAIN, int(MAIN_BOX_LAYER))) & 3, 0);
    CHECK(int(MAIN_BOX_LAYER) > int(MAIN_GROUND_LAYER));
    CHECK((mmioLast(bgcnt(BGCNT_MAIN, int(MAIN_GROUND_LAYER))) & 3) > 0);

    // BG2 of engine A is the margin vram_map.h holds back, and an unconfigured
    // layer must also be an unENABLED one -- a BG pointed nowhere and switched
    // on draws whatever base 0 happens to contain, which is the ground's
    // characters read as a map.
    CHECK_EQ(mmioTouches(bgcnt(BGCNT_MAIN, 2), 2), 0);
    CHECK_EQ(dispcntMainValue() & (1u << (8 + 2)), 0u);
    CHECK_EQ(mmioTouches(bgcnt(BGCNT_SUB, 3), 2), 0);
    CHECK_EQ(dispcntSubValue() & (1u << (8 + 3)), 0u);
}

KH_TEST(devinit_keeps_both_dispcnt_base_fields_zero) {
    // THE 62 KiB RULE'S CONSUMER.  Bits 24-26 and 27-29 of engine A's DISPCNT
    // are engine-wide base terms in 64 KiB units, added to every layer's BGxCNT
    // base at once -- and engine B has no equivalent.  vram_map.h keeps every
    // region under BASE_REACH so both stay zero, which is what lets one piece
    // of arithmetic serve both engines.  This is the code that depends on it.
    Recorded r;
    const int64_t main_ = mmioLast(DISPCNT_MAIN);
    CHECK(main_ >= 0);
    CHECK_EQ(int((main_ >> 24) & 0x07), 0);     // character base
    CHECK_EQ(int((main_ >> 27) & 0x07), 0);     // screen base

    // ...and the invariant it rests on, restated where the dependency is, so
    // that raising a region past 62 KiB fails here as well as in vram_map.h.
    CHECK(MAIN_BG_SPARE.end() <= BASE_REACH);
    CHECK(SUB_BG_SPARE.end() <= BASE_REACH);

    // 1D sprite mapping, and the boundary field matching what the pipeline
    // encoded against.  gen/assets.h owns OBJ_BOUNDARY; a DISPCNT that
    // disagreed would halve or double every tile number the generator emitted.
    CHECK_EQ(int((main_ >> 4) & 1), 1);
    CHECK_EQ(OBJ_BOUNDARY, 32);
    CHECK_EQ(int((main_ >> 20) & 3), 0);        // field 0 == 32 bytes
}

KH_TEST(devinit_blanks_both_screens_before_it_points_anything_anywhere) {
    // Between the first write and the last, every layer is aimed at VRAM that
    // has not been written yet.  Without forced blank the display controller
    // spends those scanlines drawing whatever the banks powered on holding --
    // a few frames of noise, once, that gets diagnosed as a bad bank mapping.
    Recorded r;
    const MmioWrite* log = mmioLog();
    const int n = mmioLogCount();
    CHECK(n > 0);
    CHECK(!mmioLogOverflowed());

    // First two writes: forced blank on both engines, before anything else.
    CHECK_EQ(log[0].addr, DISPCNT_MAIN);
    CHECK((log[0].value & (1u << 7)) != 0u);
    CHECK_EQ(log[1].addr, DISPCNT_SUB);
    CHECK((log[1].value & (1u << 7)) != 0u);

    // Last two: the real values, with forced blank absent -- which is what
    // clears it.  A separate "clear the bit" write would be a third state the
    // hardware passes through and nothing describes.
    CHECK_EQ(log[n - 2].addr, DISPCNT_MAIN);
    CHECK_EQ(log[n - 2].value, dispcntMainValue());
    CHECK_EQ(log[n - 1].addr, DISPCNT_SUB);
    CHECK_EQ(log[n - 1].value, dispcntSubValue());
    CHECK_EQ(dispcntMainValue() & (1u << 7), 0u);
    CHECK_EQ(dispcntSubValue() & (1u << 7), 0u);

    // Every bank is mapped BEFORE the engines are let out of forced blank, and
    // AFTER power -- a bank cannot be mapped into an engine that is not running.
    int powerAt = -1, lastBankAt = -1;
    for (int i = 0; i < n; ++i) {
        if (log[i].addr == POWCNT1) powerAt = i;
        for (unsigned b = 0; b < unsigned(Bank::Count); ++b)
            if (log[i].addr == vramcntAddr(Bank(b)) && i > lastBankAt)
                lastBankAt = i;
    }
    CHECK(powerAt > 1);
    CHECK(lastBankAt > powerAt);
    CHECK(lastBankAt < n - 2);
}

KH_TEST(devinit_powers_both_engines_and_leaves_the_3d_ones_alone) {
    Recorded r;
    const int64_t p = mmioLast(POWCNT1);
    CHECK_EQ(p, int64_t(powcnt1Value()));
    CHECK_EQ(int(p & 1), 1);                    // both LCDs; disabling is prohibited
    CHECK_EQ(int((p >> 1) & 1), 1);             // 2D engine A
    CHECK_EQ(int((p >> 9) & 1), 1);             // 2D engine B
    CHECK_EQ(int((p >> 15) & 1), 1);            // engine A to the TOP screen

    // The 3D rendering and geometry engines stay off.  They belong to the 3D
    // GroundRenderer, which is one of two alternatives and may never be chosen;
    // vram_map.h is explicit that switching between the two is a call and not a
    // remap, so this function must not have an opinion about which one runs.
    CHECK_EQ(int((p >> 2) & 1), 0);
    CHECK_EQ(int((p >> 3) & 1), 0);
    // ...and the matching half: BG0 is not yet selected as the 3D layer.
    CHECK_EQ(dispcntMainValue() & (1u << 3), 0u);
}

KH_TEST(devinit_touches_nothing_outside_the_registers_it_documents) {
    // The blast radius, asserted.  A write to an address this file does not
    // name is either a mistake or an undocumented decision, and both should
    // have to be argued for rather than merely committed.
    Recorded r;
    const MmioWrite* log = mmioLog();
    for (int i = 0; i < mmioLogCount(); ++i) {
        const uint32_t a = log[i].addr;
        bool known = a == POWCNT1 || a == DISPCNT_MAIN || a == DISPCNT_SUB;
        for (int l = 0; l < 4 && !known; ++l)
            known = a == bgcnt(BGCNT_MAIN, l) || a == bgcnt(BGCNT_SUB, l);
        for (unsigned b = 0; b < unsigned(Bank::Count) && !known; ++b)
            known = a == vramcntAddr(Bank(b));
        CHECK(known);
    }
    // Nothing in VRAM, palette RAM or OAM: the initialisation points layers at
    // regions and uploads nothing into them.  That is step one of six, and a
    // first frame of whatever VRAM powered on holding is the honest consequence.
    for (const AddressSpan& s : ADDRESS_MAP)
        CHECK_EQ(mmioTouches(s.base, int(s.bytes)), 0);
}
