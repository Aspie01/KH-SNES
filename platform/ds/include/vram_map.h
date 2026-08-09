#pragma once
// The allocation of the DS's nine VRAM banks.  FROZEN.
//
// WHY THIS FILE IS FROZEN.  Five later tasks all want VRAM -- the two-screen
// init, the 2D tilemap ground, the sprite path, the bottom screen, and the 3D
// quad ground -- and every one of them would happily allocate its own.  Two of
// them would then collide, and because a VRAM collision is not a crash but a
// wrong picture, the collision would be found by looking at the screen and
// blamed on whichever task was written last.  So the allocation is decided once,
// before any of them exist, and they consume it read-only.
//
// WHAT THE FREEZE ACTUALLY FORBIDS, stated precisely, because the loose version
// ("nothing in this file may be edited") is a rule this file's own history has
// broken twice and both times correctly.  No ADDRESS may move, no region may be
// resized, no bank may change its disposition, and no consumer may edit any of
// it for any reason.  Adding an assertion, a derived constant or a paragraph is
// not an edit to the allocation; it is the allocation finally saying what it
// meant, and both later passes over this file did exactly that and nothing else.
//
// If a later task needs something this allocation does not give it, the answer
// is in WHAT THIS GIVES UP at the bottom -- every reservation has a named
// recovery path, and taking one is a decision with consequences that are written
// down rather than a number that gets nudged.
//
// EVERY ADDRESS HERE IS QUOTED, NOT REMEMBERED.  The bank table is GBATEK's
// "DS Memory Control - VRAM", reproduced below verbatim enough to check against,
// and the base arithmetic is its "BGxCNT" note.  A bank programmed with an MST
// its silicon does not implement does not fault -- it simply does not appear
// where the code expects it, and the layer that wanted it draws garbage or
// nothing.  That is why the legality of every assignment is asserted here rather
// than trusted.
//
// UNITS.  Registers take these in their own units and this header carries both,
// because converting at the call site is where the factor-of-eight errors live:
//   BG character base   BGxCNT bits 2-5, units of 16 KiB   (DS widened this
//                       field from the GBA's bits 2-3 -- 0..15, not 0..3)
//   BG screen/map base  BGxCNT bits 8-12, units of 2 KiB
//   OBJ tile number     units of the 1D boundary, see gen/assets.h
//   engine A adds DISPCNT-wide offsets to both bases; ENGINE B DOES NOT.
//
// Nothing here is a devkitPro type and nothing includes libnds, so the host
// build compiles it and host/tests/test_vram.cpp checks the arithmetic with no
// hardware present.  The device tier turns these numbers into VRAMCNT and
// BGxCNT writes and adds nothing of its own.

#include <cstdint>

namespace kh::vram {

// ---------------------------------------------------------------------------
// The hardware, as quoted.  GBATEK "DS Memory Control - VRAM".
// ---------------------------------------------------------------------------
//
//   VRAM    SIZE  MST  OFS   ARM9, Plain ARM9-CPU Access (so-called LCDC mode)
//   A       128K  0    -     6800000h-681FFFFh
//   B       128K  0    -     6820000h-683FFFFh
//   C       128K  0    -     6840000h-685FFFFh
//   D       128K  0    -     6860000h-687FFFFh
//   E       64K   0    -     6880000h-688FFFFh
//   F       16K   0    -     6890000h-6893FFFh
//   G       16K   0    -     6894000h-6897FFFh
//   H       32K   0    -     6898000h-689FFFFh
//   I       16K   0    -     68A0000h-68A3FFFh
//   VRAM    SIZE  MST  OFS   2D Graphics Engine A, BG-VRAM (max 512K)
//   A,B,C,D 128K  1    0..3  6000000h+(20000h*OFS)
//   E       64K   1    -     6000000h
//   F,G     16K   1    0..3  6000000h+(4000h*OFS.0)+(10000h*OFS.1)
//   VRAM    SIZE  MST  OFS   2D Graphics Engine A, OBJ-VRAM (max 256K)
//   A,B     128K  2    0..1  6400000h+(20000h*OFS.0)  ;(OFS.1 must be zero)
//   E       64K   2    -     6400000h
//   F,G     16K   2    0..3  6400000h+(4000h*OFS.0)+(10000h*OFS.1)
//   VRAM    SIZE  MST  OFS   Texture/Rear-plane Image
//   A,B,C,D 128K  3    0..3  Slot OFS(0-3)
//   VRAM    SIZE  MST  OFS   Texture Palette
//   E       64K   3    -     Slots 0-3
//   F,G     16K   3    0..3  Slot (OFS.0*1)+(OFS.1*4)  ;ie. Slot 0, 1, 4, or 5
//   VRAM    SIZE  MST  OFS   2D Graphics Engine B, BG-VRAM (max 128K)
//   C       128K  4    -     6200000h
//   H       32K   1    -     6200000h
//   I       16K   1    -     6208000h
//   VRAM    SIZE  MST  OFS   2D Graphics Engine B, OBJ-VRAM (max 128K)
//   D       128K  4    -     6600000h
//   I       16K   2    -     6600000h
//   VRAM    SIZE  MST  OFS   2D Graphics Engine A, BG Extended Palette
//   E       64K   4    -     Slot 0-3  ;only lower 32K used
//   F,G     16K   4    0..1  Slot 0-1 (OFS=0), Slot 2-3 (OFS=1)
//   VRAM    SIZE  MST  OFS   2D Graphics Engine A, OBJ Extended Palette
//   F,G     16K   5    -     Slot 0  ;16K each (only lower 8K used)
//   VRAM    SIZE  MST  OFS   2D Graphics Engine B, BG Extended Palette
//   H       32K   2    -     Slot 0-3
//   VRAM    SIZE  MST  OFS   2D Graphics Engine B, OBJ Extended Palette
//   I       16K   3    -     Slot 0  ;(only lower 8K used)
//   VRAM    SIZE  MST  OFS   <ARM7>, Plain <ARM7>-CPU Access
//   C,D     128K  2    0..1  6000000h+(20000h*OFS.0)  ;OFS.1 must be zero
//
// Two constraints in that table do most of the work here, and neither is
// guessable:
//
//   C AND D CANNOT BE MAIN OBJ -- AND THEIR MST 2 IS NOT UNUSED.  The main-OBJ
//   rows list A, B, E, F, G and no others, so sprites must come from one of
//   those five, which is what stops the two big flexible banks taking them.  But
//   look at the last row: on C and D, MST 2 hands the bank to the ARM7 as work
//   RAM.  So writing the A/B/E "MST 2 means sprites" pattern to bank C does not
//   produce a bank that is merely absent from the OBJ window -- it produces a
//   bank the other CPU now owns.  That is why Use::Arm7 exists below: the matrix
//   has to say what MST 2 on C means, not just that it is not sprites.
//
//   H CAN BE ALMOST NOTHING ELSE.  Its only modes are LCDC, sub BG, and sub BG
//   extended palette.  A 32 KiB bank that can serve exactly one screen is not a
//   bank you hold in reserve; it goes to that screen.
//
// And one that bites when loading rather than when drawing: "In Extended Palette
// and Texture Image/Palette modes, VRAM is not mapped to CPU address space, and
// can be accessed only by the display controller (so, to initialize or change
// the memory, it should be temporarily switched to Plain-CPU mode)."

constexpr uint32_t KiB = 1024;

enum class Bank : uint8_t { A, B, C, D, E, F, G, H, I, Count };

constexpr uint32_t BANK_SIZE[] = {
    128 * KiB, 128 * KiB, 128 * KiB, 128 * KiB,   // A B C D
    64 * KiB,                                     // E
    16 * KiB, 16 * KiB,                           // F G
    32 * KiB,                                     // H
    16 * KiB,                                     // I
};
static_assert(sizeof BANK_SIZE / sizeof *BANK_SIZE == unsigned(Bank::Count));

constexpr uint32_t bankSize(Bank b) { return BANK_SIZE[unsigned(b)]; }

// LCDC, the address a bank answers at when it is mapped to nothing else.  This
// is where extended palettes and texture data are LOADED, because in their own
// modes they are invisible to the CPU.
constexpr uint32_t LCDC_ADDR[] = {
    0x06800000, 0x06820000, 0x06840000, 0x06860000,   // A B C D
    0x06880000,                                       // E
    0x06890000, 0x06894000,                           // F G
    0x06898000,                                       // H
    0x068A0000,                                       // I
};
constexpr uint32_t lcdcAddr(Bank b) { return LCDC_ADDR[unsigned(b)]; }

// The four windows a bank can be mapped into, and what each holds at most.
constexpr uint32_t MAIN_BG_BASE = 0x06000000;
constexpr uint32_t MAIN_BG_MAX = 512 * KiB;
constexpr uint32_t MAIN_OBJ_BASE = 0x06400000;
constexpr uint32_t MAIN_OBJ_MAX = 256 * KiB;
constexpr uint32_t SUB_BG_BASE = 0x06200000;
constexpr uint32_t SUB_BG_MAX = 128 * KiB;
constexpr uint32_t SUB_OBJ_BASE = 0x06600000;
constexpr uint32_t SUB_OBJ_MAX = 128 * KiB;

// Standard palette RAM is not a bank and cannot be reallocated.  Sprite palettes
// are a SEPARATE region at +0x200, not the upper half of a shared table as on
// the SNES -- see docs/DS_FORMATS.md.
constexpr uint32_t PAL_MAIN_BG = 0x05000000;
constexpr uint32_t PAL_MAIN_OBJ = 0x05000200;
constexpr uint32_t PAL_SUB_BG = 0x05000400;
constexpr uint32_t PAL_SUB_OBJ = 0x05000600;
constexpr uint32_t PAL_REGION_BYTES = 512;      // 256 entries, 16 sub-palettes

// ---------------------------------------------------------------------------
// OAM -- the OTHER half of the sprite budget, and the half that binds
//
// Object VRAM above is 32 KiB of resident cel data, uploaded once and then
// unchanging.  OAM is 128 ENTRIES per engine, re-competed for every single frame
// by whatever is on screen, and it is the one that runs out: the island's cast
// is 69 props before a person or an item is placed on it.
//
// This section exists because the file named two addresses and stopped there.
// Everything else here is allocated to the byte and asserted; OAM had a base
// address, no size, no entry count, no partition and no reader anywhere in the
// tree -- while constants.h, whose own docstring says "nothing SNES-hardware-
// specific is here: no VRAM addresses, no PPU register values", carried
// `MAX_OBJECTS = 128`.  That is a DS hardware number, it is this file's to own,
// and it was asserted against nothing at all.  See the guarded block at the
// bottom, where the two are tied.
//
// GBATEK, "DS Memory Map": 07000000h-070003FFh is engine A's OAM and
// 07000400h-070007FFh is engine B's, 1 KiB each, 128 entries of 8 bytes.
// ---------------------------------------------------------------------------
constexpr uint32_t OAM_MAIN = 0x07000000;
constexpr uint32_t OAM_SUB = 0x07000400;
constexpr uint32_t OAM_BYTES = 1024;
constexpr int OAM_ENTRY_BYTES = 8;
constexpr int OAM_ENTRIES = int(OAM_BYTES) / OAM_ENTRY_BYTES;   // 128 per engine
static_assert(OAM_SUB == OAM_MAIN + OAM_BYTES,
              "the two OAMs are ADJACENT, so a 129th entry written to the main "
              "engine is the sub engine's entry 0 -- a sprite that appears on "
              "the other screen, which faults nothing and reads as a renderer "
              "bug on a screen the renderer never touched");

// AN ENTRY IS 8 BYTES AND ONLY 6 OF THEM ARE YOURS.  attr0, attr1 and attr2 are
// the sprite; the fourth halfword of every entry belongs to the affine matrix
// table, which is INTERLEAVED through OAM rather than stored after it.  Matrix n
// is the spare halfword of entries 4n, 4n+1, 4n+2 and 4n+3 -- one of PA, PB, PC,
// PD each.
//
// Two consequences, both silent:
//   * clearing OAM clears the matrices, and a matrix of zeroes scales a sprite
//     to nothing rather than leaving it alone;
//   * writing 128 PACKED 6-byte records fills 768 bytes, and every sprite after
//     the first is at the wrong address.
// The SNES had neither problem: its entry was 4 bytes plus 2 bits in a separate
// high table, and there were no matrices at all.  So this is a trap with no
// oracle behind it -- nothing in the frozen build can be diffed against to find
// it, which is exactly why it is written down here.
constexpr int OAM_ATTR_BYTES = 6;           // attr0, attr1, attr2
constexpr int OAM_AFFINE_SLOTS = 32;
constexpr int OAM_AFFINE_BYTES = 8;         // PA PB PC PD, one halfword each
static_assert(OAM_AFFINE_SLOTS * OAM_AFFINE_BYTES
                  == OAM_ENTRIES * (OAM_ENTRY_BYTES - OAM_ATTR_BYTES),
              "the affine matrices ARE the spare halfword of every entry; if "
              "these two do not agree then the interleave above is mis-stated "
              "and every matrix written past the first lands inside a sprite");

constexpr uint32_t oamEntry(uint32_t oam, int n) {
    return oam + uint32_t(n) * OAM_ENTRY_BYTES;
}
// Halfword `part` (0=PA, 1=PB, 2=PC, 3=PD) of affine matrix `slot`.  Derived
// from the interleave rather than from a second base address, because a second
// base address is the thing that would go stale.
constexpr uint32_t oamAffine(uint32_t oam, int slot, int part) {
    return oamEntry(oam, slot * 4 + part) + OAM_ATTR_BYTES;
}
static_assert(oamEntry(OAM_MAIN, OAM_ENTRIES - 1) + OAM_ENTRY_BYTES
                  == OAM_MAIN + OAM_BYTES,
              "the last entry ends exactly at the end of OAM");
static_assert(oamAffine(OAM_MAIN, 0, 0) == OAM_MAIN + OAM_ATTR_BYTES);
static_assert(oamAffine(OAM_MAIN, 1, 0) == OAM_MAIN + 4 * OAM_ENTRY_BYTES + 6);
static_assert(oamAffine(OAM_MAIN, OAM_AFFINE_SLOTS - 1, 3) == OAM_MAIN + OAM_BYTES - 2,
              "the last matrix halfword is the last halfword of OAM");

// ---------------------------------------------------------------------------
// The base arithmetic.  GBATEK's "BGxCNT" note: "character base extended from
// bit2-3 to bit2-5", and
//   engine A screen base: BGxCNT.bits*2K + DISPCNT.bits*64K
//   engine B screen base: BGxCNT.bits*2K + 0
//   engine A char base:   BGxCNT.bits*16K + DISPCNT.bits*64K
//   engine B char base:   BGxCNT.bits*16K + 0
//
// The asymmetry is the trap: a base that works on the main engine, moved to the
// sub engine unchanged, lands 64 KiB times the DISPCNT term too low.  This
// header therefore keeps every region of both engines inside the first 64 KiB of
// its window -- the last addressable map base is 31, which is 62 KiB, so a region
// ending at 64 KiB is the most BGxCNT alone can name -- and asserts it.  Both
// DISPCNT base terms are then zero and the two engines share one arithmetic.
// ---------------------------------------------------------------------------
constexpr uint32_t CHAR_BLOCK = 16 * KiB;
constexpr uint32_t MAP_BLOCK = 2 * KiB;
constexpr int CHAR_BASE_MAX = 15;       // bits 2-5
constexpr int MAP_BASE_MAX = 31;        // bits 8-12

// A region of a window: where it starts and how big it is, with the register
// values that name it derived rather than stored, so the two cannot disagree.
// Sizes are what the region is ALLOWED, not what it uses today -- a later task
// growing into its own reservation must not need this frozen file edited.
struct Region {
    uint32_t offset;        // from the base of its window
    uint32_t bytes;
    constexpr uint32_t end() const { return offset + bytes; }
    constexpr bool overlaps(const Region& o) const {
        return offset < o.end() && o.offset < end();
    }
    constexpr int charBase() const { return int(offset / CHAR_BLOCK); }
    constexpr int mapBase() const { return int(offset / MAP_BLOCK); }
};

// ---------------------------------------------------------------------------
// The capability matrix, so an illegal mapping is a COMPILE error.
//
// This is the whole reason the table above is transcribed rather than cited.  A
// bank programmed with an MST its silicon does not implement does not fault: it
// simply is not there, and the layer that wanted it draws the last thing that
// happened to be at that address.  Encoding the matrix means `Bank::C` as main
// OBJ -- the single most tempting illegal mapping, because C is big and idle --
// cannot be written at all.
// ---------------------------------------------------------------------------
enum class Use : uint8_t {
    Lcdc, MainBg, MainObj, SubBg, SubObj,
    Texture, TexPalette, MainBgExtPal, MainObjExtPal, SubBgExtPal, SubObjExtPal,
    Arm7,       // C and D only, and never wanted here -- see the note above
    Count
};

// Bit per Use, one row per bank, read straight off the quoted table.
constexpr uint16_t bit(Use u) { return uint16_t(1u << unsigned(u)); }

constexpr uint16_t BANK_CAN[] = {
    // A: LCDC, main BG, main OBJ, texture.  "Bit2 not used by VRAM-A,B,H,I".
    uint16_t(bit(Use::Lcdc) | bit(Use::MainBg) | bit(Use::MainObj) | bit(Use::Texture)),
    // B: identical to A.
    uint16_t(bit(Use::Lcdc) | bit(Use::MainBg) | bit(Use::MainObj) | bit(Use::Texture)),
    // C: main BG, texture, sub BG, ARM7 work RAM.  NOT main OBJ -- and its
    // MST 2, the one that means main OBJ everywhere else, means ARM7 here.
    uint16_t(bit(Use::Lcdc) | bit(Use::MainBg) | bit(Use::Texture) | bit(Use::SubBg)
             | bit(Use::Arm7)),
    // D: main BG, texture, sub OBJ, ARM7 work RAM.  Same trap as C.
    uint16_t(bit(Use::Lcdc) | bit(Use::MainBg) | bit(Use::Texture) | bit(Use::SubObj)
             | bit(Use::Arm7)),
    // E: main BG, main OBJ, texture palette, main BG ext palette.  No OFS.
    uint16_t(bit(Use::Lcdc) | bit(Use::MainBg) | bit(Use::MainObj)
             | bit(Use::TexPalette) | bit(Use::MainBgExtPal)),
    // F: everything E can do, plus main OBJ ext palette.  16 KiB.
    uint16_t(bit(Use::Lcdc) | bit(Use::MainBg) | bit(Use::MainObj)
             | bit(Use::TexPalette) | bit(Use::MainBgExtPal) | bit(Use::MainObjExtPal)),
    // G: identical to F.
    uint16_t(bit(Use::Lcdc) | bit(Use::MainBg) | bit(Use::MainObj)
             | bit(Use::TexPalette) | bit(Use::MainBgExtPal) | bit(Use::MainObjExtPal)),
    // H: sub BG and sub BG ext palette, and nothing else at all.
    uint16_t(bit(Use::Lcdc) | bit(Use::SubBg) | bit(Use::SubBgExtPal)),
    // I: sub BG, sub OBJ, sub OBJ ext palette.
    uint16_t(bit(Use::Lcdc) | bit(Use::SubBg) | bit(Use::SubObj) | bit(Use::SubObjExtPal)),
};
static_assert(sizeof BANK_CAN / sizeof *BANK_CAN == unsigned(Bank::Count));

constexpr bool canDo(Bank b, Use u) { return (BANK_CAN[unsigned(b)] & bit(u)) != 0; }

// The MST value that selects a use on a given bank.  Returns -1 for an illegal
// pair, which the assignment assertions turn into a compile error.  Note E, F
// and G do not agree with each other on ext-palette MSTs, and H and I have
// their own numbering entirely -- there is no formula, only the table.
constexpr int mstFor(Bank b, Use u) {
    if (!canDo(b, u)) return -1;
    switch (u) {
        case Use::Lcdc: return 0;
        case Use::MainBg: return 1;
        case Use::MainObj: return 2;
        case Use::Texture: return 3;
        case Use::TexPalette: return 3;
        case Use::MainBgExtPal: return 4;
        case Use::MainObjExtPal: return 5;
        case Use::SubBg: return b == Bank::C ? 4 : 1;    // C is MST 4, H and I are 1
        case Use::SubObj: return b == Bank::D ? 4 : 2;   // D is MST 4, I is 2
        case Use::SubBgExtPal: return 2;                 // H only
        case Use::SubObjExtPal: return 3;                // I only
        case Use::Arm7: return 2;                        // C and D only
        default: return -1;
    }
}

// ---------------------------------------------------------------------------
// THE OFS FIELD, which was written nine times and read none
//
// `Assignment::ofs` below is the OFS field of a VRAMCNT register, and `offset`
// is where the bank consequently lands -- or, for texture and texture palette,
// the SLOT it occupies.  Nine rows carry eighteen of those numbers and nothing
// in the tree read one: not a test, not an assertion, not code.  All eighteen
// are zero, so nothing is wrong today; but "all zero and unchecked" is what a
// table looks like immediately before somebody sets one, and the recovery paths
// at the bottom of this file invite exactly that.
//
// There are three separate traps in that one field, and the quoted table above
// has all three:
//
//   * E, H AND I HAVE NO OFS FIELD.  GBATEK: "Offset not used by VRAM-E,H,I."
//     A non-zero ofs on one of those is not a bank placed 16 KiB up; it is a bit
//     the silicon ignores while the code that wrote it believes otherwise.
//   * THE LEGAL RANGE DEPENDS ON THE USE, not just on the bank.  A and B take
//     OFS 0..3 as main BG and only 0..1 as main OBJ -- "(OFS.1 must be zero)".
//   * F AND G ARE NOT LINEAR.  Their offset is 4000h*OFS.0 + 10000h*OFS.1, so
//     OFS 2 is 64 KiB up and not 32, and their texture-palette slot is
//     OFS.0 + OFS.1*4 -- which is the whole reason F and G reach slots 0, 1, 4
//     and 5 and can never reach 2 or 3.
// ---------------------------------------------------------------------------

// The largest OFS a (bank, use) pair accepts: 0 where the bank has no OFS field
// at all, and -1 for a pair the silicon does not implement.
constexpr int ofsMax(Bank b, Use u) {
    if (!canDo(b, u)) return -1;
    const bool abcd = unsigned(b) <= unsigned(Bank::D);
    const bool fg = b == Bank::F || b == Bank::G;
    switch (u) {
        case Use::Lcdc: return 0;                   // LCDC mode has no offset
        case Use::MainBg: return (abcd || fg) ? 3 : 0;          // E: none
        case Use::MainObj: return fg ? 3 : (abcd ? 1 : 0);      // A,B: OFS.1 = 0
        case Use::Texture: return 3;                            // A-D, four slots
        case Use::TexPalette: return fg ? 3 : 0;                // E: none
        case Use::MainBgExtPal: return fg ? 1 : 0;              // E: none
        case Use::MainObjExtPal: return 0;                      // F,G: slot 0
        case Use::SubBg: return 0;                              // C,H,I: none
        case Use::SubObj: return 0;                             // D,I: none
        case Use::SubBgExtPal: return 0;                        // H
        case Use::SubObjExtPal: return 0;                       // I
        case Use::Arm7: return 1;                               // C,D: OFS.1 = 0
        default: return -1;
    }
}

// Where a bank LANDS in its window for a given OFS.  A, B, C and D step a whole
// 128 KiB bank; F and G do not step linearly at all; E, H and I cannot step.
constexpr uint32_t bankWindowOffset(Bank b, uint8_t ofs) {
    if (b == Bank::F || b == Bank::G)
        return uint32_t(ofs & 1u) * 16u * KiB + uint32_t((ofs >> 1) & 1u) * 64u * KiB;
    if (unsigned(b) <= unsigned(Bank::D)) return uint32_t(ofs) * 128u * KiB;
    return 0;
}

// ...and which SLOT it occupies, for the two uses addressed by slot rather than
// by an offset in a window.  E as texture palette has no OFS and covers slots
// 0-3 together, so its slot is the first of the four.
constexpr int bankSlot(Bank b, Use u, uint8_t ofs) {
    if (u == Use::TexPalette && (b == Bank::F || b == Bank::G))
        return int(ofs & 1u) + int((ofs >> 1) & 1u) * 4;
    return int(ofs);
}
static_assert(bankWindowOffset(Bank::F, 2) == 64 * KiB,
              "F and G are not linear: OFS 2 is 10000h, not two 4000h steps");
static_assert(bankSlot(Bank::G, Use::TexPalette, 2) == 4,
              "F and G reach texture palette slots 0, 1, 4 and 5 and no others");
static_assert(ofsMax(Bank::E, Use::MainObj) == 0,
              "bank E has no OFS field, so it can only sit at its window's base");
static_assert(ofsMax(Bank::A, Use::MainObj) == 1 && ofsMax(Bank::A, Use::MainBg) == 3,
              "the legal OFS range depends on the use, not only on the bank");

// One bank's disposition.  `offset` is where the bank lands inside its window;
// for the banks used here it is always zero, because a non-zero OFS buys nothing
// when a window holds one bank and costs the F/G offset quirk
// (6000000h + 4000h*OFS.0 + 10000h*OFS.1, which is NOT a linear 16 KiB step).
struct Assignment {
    Bank bank;
    Use use;
    uint8_t ofs;
    uint32_t offset;        // within its window, or the slot number for texture
    const char* why;
};

// ---------------------------------------------------------------------------
// THE ALLOCATION
//
// Reasoning, in the order the constraints bind:
//
//   H -> sub BG.  H's only modes are LCDC, sub BG and sub BG extended palette.
//        A 32 KiB bank that can serve exactly one screen goes to that screen;
//        holding it in reserve reserves nothing.
//   I -> sub OBJ.  The alternative is D, and D is a 128 KiB bank that is one of
//        only two remaining texture-capable banks.  16 KiB is 512 sprite tile
//        numbers at boundary 32, which is four times what a cursor, a command
//        list and a few minimap markers need.
//   A -> 3D texture.  Texture can only be A-D and the granule is a whole bank,
//        so the ground atlas costs 128 KiB whether it is 8 KiB or 80.
//   F -> texture palette.  Sixteen 16-colour palettes is 512 bytes; F is the
//        smallest bank that can hold them, and spending E here would cost the
//        one 64 KiB bank that can be main OBJ.
//   E -> main OBJ.  Sprites can only come from A, B, E, F or G.  A is texture
//        and F is the texture palette; G is 16 KiB, half of what the resident
//        pages need; B is 128 KiB for a 32 KiB job.  E is the exact fit, and its
//        64 KiB is precisely the reach at boundary 64 if a fourth page ever
//        forces the boundary up.
//   B -> main BG.  Of the banks left (B, C, D, G), G is too small and C and D
//        are the ONLY 128 KiB banks that can serve the sub engine.  B can be
//        main BG, main OBJ or texture and nothing else, so it is the least
//        flexible large bank and therefore the right one to spend.
//   C, D, G -> LCDC.  Not idle: they are the three recovery paths named at the
//        bottom of this file, and meanwhile they are 272 KiB of CPU-visible
//        scratch at their LCDC addresses -- which is where the streaming map is
//        staged and where extended palettes would be built before being mapped.
// ---------------------------------------------------------------------------
constexpr Assignment ASSIGNMENTS[] = {
    {Bank::A, Use::Texture, 0, 0,
     "3D texture can only be A-D and the granule is a whole bank"},
    {Bank::B, Use::MainBg, 0, 0,
     "the least flexible large bank: B is main BG, main OBJ or texture, nothing else"},
    {Bank::C, Use::Lcdc, 0, 0,
     "held: the only 128 KiB bank that can be sub BG, and CPU-visible scratch meanwhile"},
    {Bank::D, Use::Lcdc, 0, 0,
     "held: the only 128 KiB bank that can be sub OBJ"},
    {Bank::E, Use::MainObj, 0, 0,
     "sprites are A/B/E/F/G only; 64 KiB is exactly the reach at boundary 64"},
    {Bank::F, Use::TexPalette, 0, 0,
     "512 bytes of texture palette; spending E here would cost the main OBJ bank"},
    {Bank::G, Use::Lcdc, 0, 0,
     "held: the only remaining main OBJ extended palette, and a second texture palette"},
    {Bank::H, Use::SubBg, 0, 0,
     "H can be LCDC, sub BG or sub BG ext palette and nothing else"},
    {Bank::I, Use::SubObj, 0, 0,
     "16 KiB is 512 sprite tile numbers; the alternative is D, worth more held"},
};
constexpr int ASSIGNMENT_COUNT = sizeof ASSIGNMENTS / sizeof *ASSIGNMENTS;
static_assert(ASSIGNMENT_COUNT == int(Bank::Count),
              "every bank must have an explicit disposition, LCDC included");

// ...and the two fields of that table nothing used to read.  Both are checked
// against the hardware rules above rather than against each other, so a row
// whose ofs and offset are consistently wrong still fails.
constexpr bool everyOffsetIsLegal() {
    for (const Assignment& a : ASSIGNMENTS) {
        const int hi = ofsMax(a.bank, a.use);
        if (hi < 0 || int(a.ofs) > hi) return false;
        const bool bySlot = a.use == Use::Texture || a.use == Use::TexPalette;
        const uint32_t want = bySlot ? uint32_t(bankSlot(a.bank, a.use, a.ofs))
                                     : bankWindowOffset(a.bank, a.ofs);
        if (a.offset != want) return false;
    }
    return true;
}
static_assert(everyOffsetIsLegal(),
              "an assignment sets an OFS its bank does not have, or a larger one "
              "than that use allows, or records an offset that is not where the "
              "OFS actually puts the bank -- and on F or G those are different "
              "numbers, because their offset is not a linear step");

// The slot a use ends up in, read off the table rather than restated.  There
// were two statements of where the texture lives -- ASSIGNMENTS' row for bank A
// and TEXTURE_SLOT below -- and nothing tied them, so moving the texture to
// bank C for the extra slot would have left TEXTURE_SLOT saying 0.
constexpr int slotAssignedTo(Use u) {
    for (const Assignment& a : ASSIGNMENTS)
        if (a.use == u) return bankSlot(a.bank, a.use, a.ofs);
    return -1;
}

// ---------------------------------------------------------------------------
// THE BYTES THE HARDWARE ACTUALLY TAKES
//
// The top of this file says "the device tier turns these numbers into VRAMCNT
// and BGxCNT writes and adds nothing of its own."  That was a promise the file
// did not keep: it gave the INGREDIENTS -- a bank, a use, an MST, an OFS -- and
// left the composition to the tier that has no way to check it.  The very first
// thing §M7 does is write nine bytes, every one of them fully determined by the
// table above, and not one of them was written down.
//
// GBATEK, "DS Video Stuff - VRAM Control", VRAMCNT_A..I:
//
//   Bit   Expl.
//   0-2   VRAM MST              ;Bit2 not used by VRAM-A,B,H,I
//   3-4   VRAM Offset (0-3)     ;Offset not used by VRAM-E,H,I
//   5-6   Not used
//   7     VRAM Enable (0=Disable, 1=Enable)
// ---------------------------------------------------------------------------
constexpr int VRAMCNT_MST_SHIFT = 0;
constexpr int VRAMCNT_OFS_SHIFT = 3;
constexpr uint8_t VRAMCNT_ENABLE = 0x80;

// "Bit2 not used by VRAM-A,B,H,I" -- so those four have a TWO-bit MST field and
// the rest have three.  It matters: MST 4 is sub BG on C and sub OBJ on D, and
// on a two-bit bank the 4 simply does not fit.  Nothing here needs a 4 on one of
// those banks, and the assertion below is what keeps that true.
constexpr int mstBits(Bank b) {
    return (b == Bank::A || b == Bank::B || b == Bank::H || b == Bank::I) ? 2 : 3;
}

// The byte, for one bank, composed from its row of ASSIGNMENTS.
constexpr uint8_t vramcnt(Bank b) {
    for (const Assignment& a : ASSIGNMENTS)
        if (a.bank == b)
            return uint8_t(uint8_t(mstFor(a.bank, a.use) << VRAMCNT_MST_SHIFT)
                           | uint8_t(a.ofs << VRAMCNT_OFS_SHIFT)
                           | VRAMCNT_ENABLE);
    return 0;       // unreachable: everyBankAssignedOnce()
}

// ...AND THE REGISTER ADDRESSES, WHICH ARE NOT NINE CONSECUTIVE BYTES.
//
//   4000240h VRAMCNT_A   4000243h VRAMCNT_D   4000246h VRAMCNT_G
//   4000241h VRAMCNT_B   4000244h VRAMCNT_E   4000247h **WRAMCNT**
//   4000242h VRAMCNT_C   4000245h VRAMCNT_F   4000248h VRAMCNT_H
//                                             4000249h VRAMCNT_I
//
// 4000247h IS WRAMCNT, the register that splits the 32 KiB of shared work RAM
// between the ARM9 and the ARM7.  A loop that writes nine bytes from 4000240h
// therefore does not merely misplace bank H -- it hands H's control byte to
// WRAMCNT and repartitions the memory the two processors share.
//
//   WRAMCNT bits 0-1, Shared WRAM Bank Allocation (GBATEK):
//     0  ARM9 = 32K,     ARM7 = 0K
//     1  ARM9 = 2nd 16K, ARM7 = 1st 16K
//     2  ARM9 = 1st 16K, ARM7 = 2nd 16K
//     3  ARM9 = 0K,      ARM7 = 32K
//
// Our H byte is 0x81, so bits 0-1 are 1: the ARM9 keeps only the second 16 KiB
// and the first is reassigned to the ARM7, mid-initialisation, while the ARM9
// is using it.  Not the worst of the four values -- a byte ending in 3 would
// take all of it -- and that is precisely what makes it bad, because half a
// region disappearing corrupts rather than halts.
//
// It is the most destructive one-line mistake available in the DS's
// initialisation, it is written as the natural loop, and this is the file that
// exists to stop it.
constexpr uint32_t VRAMCNT_ADDR[] = {
    0x04000240, 0x04000241, 0x04000242, 0x04000243,   // A B C D
    0x04000244,                                       // E
    0x04000245, 0x04000246,                           // F G
    0x04000248,                                       // H -- 247h is WRAMCNT
    0x04000249,                                       // I
};
static_assert(sizeof VRAMCNT_ADDR / sizeof *VRAMCNT_ADDR == unsigned(Bank::Count));
constexpr uint32_t WRAMCNT_ADDR = 0x04000247;
constexpr uint32_t vramcntAddr(Bank b) { return VRAMCNT_ADDR[unsigned(b)]; }

constexpr bool vramcntAddressesSkipWramcnt() {
    for (uint32_t a : VRAMCNT_ADDR)
        if (a == WRAMCNT_ADDR) return false;
    // ...and they are otherwise ascending and distinct, so the gap is the only
    // discontinuity rather than one of several.
    for (unsigned i = 1; i < unsigned(Bank::Count); ++i)
        if (VRAMCNT_ADDR[i] <= VRAMCNT_ADDR[i - 1]) return false;
    return true;
}
static_assert(vramcntAddressesSkipWramcnt(),
              "a VRAMCNT address collides with WRAMCNT, which does not misplace "
              "a bank -- it repartitions the work RAM the two CPUs share");
static_assert(VRAMCNT_ADDR[unsigned(Bank::G)] + 1 == WRAMCNT_ADDR
                  && WRAMCNT_ADDR + 1 == VRAMCNT_ADDR[unsigned(Bank::H)],
              "WRAMCNT sits between G and H; that is the whole hazard and it "
              "must stay stated rather than implied by the numbers");

constexpr bool everyMstFitsItsField() {
    for (const Assignment& a : ASSIGNMENTS)
        if (mstFor(a.bank, a.use) >= (1 << mstBits(a.bank))) return false;
    return true;
}
static_assert(everyMstFitsItsField(),
              "an MST is wider than its bank's field: A, B, H and I have two "
              "bits, so a 4 on one of them writes a zero into bit 2 and selects "
              "a different mode entirely");

// The nine bytes, spelled out.  Not because a reader could not compute them, but
// because these are what §M7 writes and a value that only exists as an
// expression is a value nobody has ever looked at.
static_assert(vramcnt(Bank::A) == 0x83, "A: texture, MST 3, slot 0");
static_assert(vramcnt(Bank::B) == 0x81, "B: main BG, MST 1, offset 0");
static_assert(vramcnt(Bank::C) == 0x80, "C: LCDC, MST 0");
static_assert(vramcnt(Bank::D) == 0x80, "D: LCDC, MST 0");
static_assert(vramcnt(Bank::E) == 0x82, "E: main OBJ, MST 2");
static_assert(vramcnt(Bank::F) == 0x83, "F: texture palette, MST 3, slot 0");
static_assert(vramcnt(Bank::G) == 0x80, "G: LCDC, MST 0");
static_assert(vramcnt(Bank::H) == 0x81, "H: sub BG, MST 1");
static_assert(vramcnt(Bank::I) == 0x82, "I: sub OBJ, MST 2");

// ---------------------------------------------------------------------------
// MAIN BG WINDOW -- bank B, 128 KiB at 0x06000000.
//
// EVERYTHING HERE STAYS UNDER 62 KiB, and that is a decision rather than an
// accident.  The map base field is five bits of 2 KiB units, so 62 KiB is as far
// as BGxCNT alone reaches; past it a region needs DISPCNT's 64 KiB term, which
// is ENGINE-WIDE and shifts all four layers at once.  Keeping under 62 KiB lets
// both DISPCNT base terms stay zero, which means a base computed for this engine
// is also correct arithmetic for the sub engine, which has no such term at all.
//
// The two ground renderers share this window without a remap: the 3D one uses
// BG0 and touches neither GROUND_CHR nor GROUND_MAP, which simply go unread.
// ---------------------------------------------------------------------------
// 1024 characters is the architectural maximum a text layer can address with its
// ten-bit index, so this reservation cannot be outgrown by any scene ever
// authored.  The biggest today is the island at 247.
constexpr Region GROUND_CHR{0, 32 * KiB};               // char base 0
// The dialogue box lives over the world, not on the bottom screen: the bottom
// screen is the command menu and the status, and a line of dialogue belongs with
// the thing that is speaking.  Shares its 16 KiB with a future overlay layer.
constexpr Region UI_CHR{32 * KiB, 16 * KiB};            // char base 2
// One 64x64 window, 8 KiB.  NOT double buffered: a streamer rewrites the columns
// and rows that scroll in, which is what the SNES did and what the hardware
// wrap in bgEntryIndex() exists for.
constexpr Region GROUND_MAP{48 * KiB, 8 * KiB};         // map base 24
constexpr Region BOX_MAP{56 * KiB, 2 * KiB};            // map base 28
constexpr Region OVERLAY_MAP{58 * KiB, 2 * KiB};        // map base 29
constexpr Region MAIN_BG_SPARE{60 * KiB, 2 * KiB};      // map base 30

// ---------------------------------------------------------------------------
// MAIN OBJ WINDOW -- bank E, 64 KiB at 0x06400000.
// ---------------------------------------------------------------------------
// Exactly the ten-bit reach at boundary 32.  gen/assets.h owns the boundary and
// the resident byte count; the assertions below tie the two files together so
// that raising one without the other does not compile.
constexpr Region OBJ_RESIDENT{0, 32 * KiB};
// Addressable only at boundary 64, where the reach doubles and every cel's tile
// number halves.  Reserved for that day and unusable before it.
constexpr Region OBJ_BOUNDARY64{32 * KiB, 32 * KiB};

// ---------------------------------------------------------------------------
// SUB BG WINDOW -- bank H, 32 KiB at 0x06200000.
//
// Engine B adds NO DISPCNT term to either base, so every offset here is the
// register value times its unit and nothing else.
// ---------------------------------------------------------------------------
constexpr Region SUB_CHR{0, 16 * KiB};                  // char base 0
constexpr Region HUD_MAP{16 * KiB, 2 * KiB};            // map base 8
constexpr Region MENU_MAP{18 * KiB, 2 * KiB};           // map base 9
constexpr Region MINIMAP_MAP{20 * KiB, 2 * KiB};        // map base 10
constexpr Region SUB_BG_SPARE{22 * KiB, 10 * KiB};      // map bases 11-15

// ---------------------------------------------------------------------------
// SUB OBJ WINDOW -- bank I, 16 KiB at 0x06600000.
// ---------------------------------------------------------------------------
constexpr Region SUB_OBJ_CHR{0, 16 * KiB};

// 3D texture and its palette are addressed by SLOT, not by an offset in a
// window, and are not CPU-visible while mapped -- load them through LCDC.
constexpr int TEXTURE_SLOT = 0;         // bank A, 128 KiB
constexpr int TEXTURE_PALETTE_SLOT = 0; // bank F, 16 KiB
static_assert(slotAssignedTo(Use::Texture) == TEXTURE_SLOT,
              "the texture slot named here is not the slot ASSIGNMENTS puts the "
              "texture bank in");
static_assert(slotAssignedTo(Use::TexPalette) == TEXTURE_PALETTE_SLOT,
              "the texture palette slot named here is not the slot ASSIGNMENTS "
              "puts the palette bank in");

// ---------------------------------------------------------------------------
// WHICH LAYER, not just which bytes
//
// The first version of this file reserved memory and said nothing about layers,
// which was a real gap: on the main engine with 3D enabled there are only three
// tilemap layers left, and layers are the scarcer resource.  Two later tasks
// picking their own would collide exactly the way two tasks picking their own
// addresses would -- which is the thing this file exists to prevent.
//
// THE GROUND IS ALWAYS BG0, on both engines' terms:
//
//   * With the 2D renderer it is a text background reading GROUND_CHR and
//     GROUND_MAP.
//   * With the 3D renderer BG0 *is* the 3D image -- GBATEK: the 3D layer
//     occupies BG0 of engine A, and engine B has none ("BG0 is always Text").
//
// So "BG0 is the ground" holds under both renderers and the swap changes only
// how BG0 is fed.  That is what makes the two GroundRenderers alternatives
// rather than rivals, and it is why the priority bits are the only part of
// BG0CNT the 3D path respects.
//
// The dialogue box takes the HIGHEST-priority layer, because it has to sit over
// the world and over the sprites, and priority is per-layer on this machine
// where the SNES had it per-tile.
// ---------------------------------------------------------------------------
enum class Layer : uint8_t { Bg0 = 0, Bg1 = 1, Bg2 = 2, Bg3 = 3 };

// Engine A.  BG2 is deliberately empty: with 3D on, BG0 is spoken for and three
// tilemap layers is all there is, so one held back is the whole margin.
constexpr Layer MAIN_GROUND_LAYER = Layer::Bg0;      // 2D tilemap, or the 3D image
constexpr Layer MAIN_OVERLAY_LAYER = Layer::Bg1;     // OVERLAY_MAP
constexpr Layer MAIN_BOX_LAYER = Layer::Bg3;         // BOX_MAP, in front of all
// Engine B.  No 3D here, so all four are tilemap layers and BG3 is the margin.
constexpr Layer SUB_HUD_LAYER = Layer::Bg0;
constexpr Layer SUB_MENU_LAYER = Layer::Bg1;
constexpr Layer SUB_MINIMAP_LAYER = Layer::Bg2;

// ---------------------------------------------------------------------------
// The character CEILINGS, stated in characters
//
// A region's size in bytes is not the limit a caller runs into -- the limit is
// how many characters it may index, and a text layer's index is ten bits whatever
// its reservation is.  GROUND_CHR was given the full 1024 precisely so it could
// not be outgrown; UI_CHR and SUB_CHR were not, and the same reasoning applies
// to them.  So the ceiling is a number here rather than an inference: a layer
// indexing past its own is reading the NEXT region's bytes as character data,
// which draws recognisable-but-wrong glyphs and nothing faults.
// ---------------------------------------------------------------------------
constexpr int CHAR_BYTES = 32;                      // 8x8 at 4bpp
constexpr int GROUND_CHR_MAX = int(GROUND_CHR.bytes / CHAR_BYTES);   // 1024
constexpr int UI_CHR_MAX = int(UI_CHR.bytes / CHAR_BYTES);           // 512
constexpr int SUB_CHR_MAX = int(SUB_CHR.bytes / CHAR_BYTES);         // 512
// 1024 spelled out rather than borrowed from gen/assets.h's MAP_TILE_MASK: this
// header must compile standalone, and the cross-file check that the two agree is
// the guarded block at the bottom, where both are in scope.
constexpr int TEXT_LAYER_CHARS = 1024;      // a ten-bit character index
static_assert(GROUND_CHR_MAX == TEXT_LAYER_CHARS,
              "the ground may address every character a text layer can name");

// The font is 128 characters and there are TWO COPIES of it -- one in UI_CHR for
// the dialogue box over the world, one in SUB_CHR for the bottom screen.
// Character data is per-engine and cannot be shared across the two, so the 4 KiB
// is spent twice on purpose rather than by oversight.
constexpr int FONT_CHARS = 128;
static_assert(FONT_CHARS <= UI_CHR_MAX && FONT_CHARS <= SUB_CHR_MAX,
              "the font must fit both engines' character reservations");

// ---------------------------------------------------------------------------
// ...and the same reasoning for SPRITES, which the pass that stated the
// character ceilings did not apply to them
//
// An OBJ tile number is ten bits as well, but it counts in units of the 1D
// BOUNDARY rather than in characters -- so the ceiling is whichever of two
// things binds first: the reach the boundary buys, or the bank behind the
// window.  gen/assets.h states the first ("at boundary 32 it reaches only the
// first 32 KiB of object VRAM however much of it the machine has") and cannot
// state the second, because it does not know which bank is mapped where.
//
// ON THE MAIN ENGINE THE REACH BINDS.  OBJ_RESIDENT is exactly the 32 KiB a
// ten-bit number reaches at boundary 32 and bank E holds 64 KiB, which is what
// makes OBJ_BOUNDARY64 a reserve rather than a hole.
//
// ON THE SUB ENGINE THE BANK BINDS, and by a factor of two.  Bank I is 16 KiB,
// so tile numbers 512 to 1023 name addresses past the end of it.  A sub-engine
// sprite given a tile number above 511 is not clipped and does not fault: engine
// B reads unmapped VRAM and draws whatever comes back.  That is the same silent
// over-index the character ceilings were stated to prevent, one window across.
// ---------------------------------------------------------------------------
constexpr int OBJ_TILE_NUMBERS = 1024;      // ten bits, on both engines
// At boundary 32, where a tile number's unit and a 4bpp character are both 32
// bytes.  gen/assets.h owns the boundary; the guarded block at the bottom checks
// that this arithmetic still holds if it is ever raised.
constexpr int SUB_OBJ_TILES = int(SUB_OBJ_CHR.bytes) / CHAR_BYTES;   // 512
static_assert(SUB_OBJ_TILES < OBJ_TILE_NUMBERS,
              "bank I is smaller than a ten-bit tile number reaches, so the "
              "ceiling is the bank and not the index -- if these ever become "
              "equal the comment above is wrong and the sub engine gained a bank");
static_assert(SUB_OBJ_TILES * CHAR_BYTES == int(SUB_OBJ_CHR.bytes),
              "the sub-engine sprite ceiling is not a whole number of tiles");

// ---------------------------------------------------------------------------
// Palettes: RELOADED PER SCENE, not partitioned
//
// The pipeline emits sixteen 16-colour sub-palettes, and they split nine OBJ to
// seven BG -- both inside the sixteen a standard region holds, so nothing
// overflows.  But dedupe_tilemap_ds() hard-codes sub-palette 0 for every scene's
// map, so all seven BG palettes want to BE sub-palette 0, at different times.
//
// That is a decision and not an accident: it is what the SNES did, DMAing a new
// palette into CGRAM per scene, and it is why index 0 keeps working as the
// backdrop (docs/DS_FORMATS.md -- the backdrop is entry 0 of sub-palette 0 and
// of no other).  A later task that wants two scenes' grounds resident at once
// must give one of them a non-zero sub-palette, and then it must ALSO emit that
// scene's backdrop explicitly, because it will no longer inherit one.
// ---------------------------------------------------------------------------
constexpr int PAL_SUBPALETTES = 16;
constexpr int PAL_SUBPALETTE_COLOURS = 16;
static_assert(PAL_SUBPALETTES * PAL_SUBPALETTE_COLOURS * 2 == PAL_REGION_BYTES,
              "a standard palette region is sixteen sub-palettes of sixteen");
constexpr int SCENE_BG_SUBPALETTE = 0;      // and reloaded, see above

// ---------------------------------------------------------------------------
// THE ASSERTIONS
//
// These are the point of the file.  Everything above is a decision; this is what
// makes the decision hold, because the failure mode being defended against does
// not fault, crash or warn -- it draws a plausible wrong picture, which is found
// by eye weeks later and blamed on the last thing written.
//
// They come in four kinds, and the last two matter as much as the first:
//   1. no two regions in a window overlap;
//   2. nothing runs past its bank, or past its window's capacity;
//   3. every base is expressible in the register that has to carry it;
//   4. nothing here contradicts a constant in another file.
// ---------------------------------------------------------------------------

// --- kind 0: the transcribed hardware table is internally consistent ---------
constexpr uint32_t totalVram() {
    uint32_t n = 0;
    for (uint32_t s : BANK_SIZE) n += s;
    return n;
}
static_assert(totalVram() == 656 * KiB,
              "the DS has 656 KiB of VRAM in nine banks; BANK_SIZE was edited");

// Every LCDC address is its predecessor plus that bank's size, which is what
// makes the banks one contiguous 656 KiB region in LCDC mode.  A mistyped
// address in the table above breaks this and nothing else would notice.
constexpr bool lcdcContiguous() {
    for (unsigned i = 1; i < unsigned(Bank::Count); ++i)
        if (LCDC_ADDR[i] != LCDC_ADDR[i - 1] + BANK_SIZE[i - 1]) return false;
    return true;
}
static_assert(lcdcContiguous(), "an LCDC address does not follow its predecessor");

// --- kind 1 and 2: every assignment is legal, and fits -----------------------
constexpr bool everyAssignmentIsLegal() {
    for (const Assignment& a : ASSIGNMENTS)
        if (mstFor(a.bank, a.use) < 0) return false;
    return true;
}
static_assert(everyAssignmentIsLegal(),
              "a bank is assigned a use its silicon does not implement; on "
              "hardware it would simply not appear and the layer would draw "
              "whatever was already there");

constexpr bool everyBankAssignedOnce() {
    for (unsigned b = 0; b < unsigned(Bank::Count); ++b) {
        int seen = 0;
        for (const Assignment& a : ASSIGNMENTS) if (unsigned(a.bank) == b) ++seen;
        if (seen != 1) return false;
    }
    return true;
}
static_assert(everyBankAssignedOnce(),
              "a bank has two dispositions or none; VRAMCNT holds one MST");

// The window a use lands in, and its capacity -- so a region can be checked
// against the right ceiling without repeating the number.
constexpr uint32_t windowMax(Use u) {
    return u == Use::MainBg ? MAIN_BG_MAX
         : u == Use::MainObj ? MAIN_OBJ_MAX
         : u == Use::SubBg ? SUB_BG_MAX
         : u == Use::SubObj ? SUB_OBJ_MAX : 0;
}

constexpr bool fits(const Region& r, Bank b, Use u) {
    return r.end() <= bankSize(b) && r.end() <= windowMax(u);
}

// ...and the base that goes with the ceiling, so that a Region -- which is an
// OFFSET and nothing else -- can be turned into the address the device tier
// writes.
//
// THIS COMPOSITION IS WHY THE FOUR BASES ARE HERE, and until this line nothing
// performed it: MAIN_BG_BASE, MAIN_OBJ_BASE, SUB_BG_BASE and SUB_OBJ_BASE were
// read by no code, no test and no assertion in the whole tree.  Four transcribed
// addresses with no consumer is four chances for a wrong digit to survive to
// §M7 and show up as a layer drawing the wrong thing -- which is the exact
// failure this file exists to prevent, and the one kind of it the file was not
// defending against.
constexpr uint32_t windowBase(Use u) {
    return u == Use::MainBg ? MAIN_BG_BASE
         : u == Use::MainObj ? MAIN_OBJ_BASE
         : u == Use::SubBg ? SUB_BG_BASE
         : u == Use::SubObj ? SUB_OBJ_BASE : 0;
}

constexpr uint32_t address(const Region& r, Use u) {
    return windowBase(u) + r.offset;
}

static_assert(address(GROUND_CHR, Use::MainBg) == 0x06000000,
              "a window base or a region offset moved");
static_assert(address(UI_CHR, Use::MainBg) == 0x06008000,
              "a window base or a region offset moved");
static_assert(address(GROUND_MAP, Use::MainBg) == 0x0600C000,
              "a window base or a region offset moved");
static_assert(address(BOX_MAP, Use::MainBg) == 0x0600E000,
              "a window base or a region offset moved");
static_assert(address(OVERLAY_MAP, Use::MainBg) == 0x0600E800,
              "a window base or a region offset moved");
static_assert(address(MAIN_BG_SPARE, Use::MainBg) == 0x0600F000,
              "a window base or a region offset moved");
static_assert(address(OBJ_RESIDENT, Use::MainObj) == 0x06400000,
              "a window base or a region offset moved");
static_assert(address(OBJ_BOUNDARY64, Use::MainObj) == 0x06408000,
              "a window base or a region offset moved");
static_assert(address(SUB_CHR, Use::SubBg) == 0x06200000,
              "a window base or a region offset moved");
static_assert(address(HUD_MAP, Use::SubBg) == 0x06204000,
              "a window base or a region offset moved");
static_assert(address(MENU_MAP, Use::SubBg) == 0x06204800,
              "a window base or a region offset moved");
static_assert(address(MINIMAP_MAP, Use::SubBg) == 0x06205000,
              "a window base or a region offset moved");
static_assert(address(SUB_BG_SPARE, Use::SubBg) == 0x06205800,
              "a window base or a region offset moved");
static_assert(address(SUB_OBJ_CHR, Use::SubObj) == 0x06600000,
              "a window base or a region offset moved");

// The whole graphics address space, so that the transcribed bases are checked
// against each other rather than only against themselves.  These seven spans are
// everything the display controllers can be pointed at, and they are mutually
// exclusive: a base with a wrong digit lands in one of the others and this
// stops compiling.
struct AddressSpan {
    uint32_t base;
    uint32_t bytes;
    const char* what;
};

constexpr AddressSpan ADDRESS_MAP[] = {
    {PAL_MAIN_BG, 4 * PAL_REGION_BYTES, "palette RAM, four regions"},
    {MAIN_BG_BASE, MAIN_BG_MAX, "engine A BG window"},
    {SUB_BG_BASE, SUB_BG_MAX, "engine B BG window"},
    {MAIN_OBJ_BASE, MAIN_OBJ_MAX, "engine A OBJ window"},
    {SUB_OBJ_BASE, SUB_OBJ_MAX, "engine B OBJ window"},
    {LCDC_ADDR[0], 656 * KiB, "LCDC, all nine banks"},
    {OAM_MAIN, 2 * OAM_BYTES, "OAM, both engines"},
};

constexpr bool addressMapDisjoint() {
    for (const AddressSpan& a : ADDRESS_MAP)
        for (const AddressSpan& b : ADDRESS_MAP)
            if (&a != &b && a.base < b.base + b.bytes && b.base < a.base + a.bytes)
                return false;
    return true;
}
static_assert(addressMapDisjoint(),
              "two of the transcribed base addresses name overlapping regions, "
              "so at least one of them is wrong -- and every Region in this file "
              "is an offset from one of them");
static_assert(LCDC_ADDR[0] + totalVram() == 0x068A4000,
              "the LCDC region ends where GBATEK's memory map says it does");

static_assert(fits(GROUND_CHR, Bank::B, Use::MainBg), "ground characters");
static_assert(fits(UI_CHR, Bank::B, Use::MainBg), "dialogue and overlay characters");
static_assert(fits(GROUND_MAP, Bank::B, Use::MainBg), "the streaming window");
static_assert(fits(BOX_MAP, Bank::B, Use::MainBg), "the dialogue box map");
static_assert(fits(OVERLAY_MAP, Bank::B, Use::MainBg), "the overlay map");
static_assert(fits(MAIN_BG_SPARE, Bank::B, Use::MainBg), "main BG spare");
static_assert(fits(OBJ_RESIDENT, Bank::E, Use::MainObj), "the resident object pages");
static_assert(fits(OBJ_BOUNDARY64, Bank::E, Use::MainObj), "the boundary-64 reserve");
static_assert(fits(SUB_CHR, Bank::H, Use::SubBg), "bottom screen characters");
static_assert(fits(HUD_MAP, Bank::H, Use::SubBg), "the HUD map");
static_assert(fits(MENU_MAP, Bank::H, Use::SubBg), "the command menu map");
static_assert(fits(MINIMAP_MAP, Bank::H, Use::SubBg), "the minimap map");
static_assert(fits(SUB_BG_SPARE, Bank::H, Use::SubBg), "sub BG spare");
static_assert(fits(SUB_OBJ_CHR, Bank::I, Use::SubObj), "bottom screen sprites");

// Disjointness, stated pairwise and by hand rather than in a loop, because the
// pairs are the specification: these are the collisions that would actually
// happen, and naming them is what a reader needs.
static_assert(!GROUND_CHR.overlaps(UI_CHR), "the two main character blocks collide");
static_assert(!GROUND_CHR.overlaps(GROUND_MAP), "ground characters over its own map");
static_assert(!GROUND_CHR.overlaps(BOX_MAP), "ground characters over the box map");
static_assert(!GROUND_CHR.overlaps(OVERLAY_MAP), "ground characters over the overlay map");
static_assert(!GROUND_CHR.overlaps(MAIN_BG_SPARE), "ground characters over the spare");
static_assert(!UI_CHR.overlaps(GROUND_MAP), "UI characters over the streaming window");
static_assert(!UI_CHR.overlaps(BOX_MAP), "UI characters over the box map");
static_assert(!UI_CHR.overlaps(OVERLAY_MAP), "UI characters over the overlay map");
static_assert(!UI_CHR.overlaps(MAIN_BG_SPARE), "UI characters over the spare");
static_assert(!GROUND_MAP.overlaps(BOX_MAP), "the streaming window over the box map");
static_assert(!GROUND_MAP.overlaps(OVERLAY_MAP), "the streaming window over the overlay map");
static_assert(!GROUND_MAP.overlaps(MAIN_BG_SPARE), "the streaming window over the spare");
static_assert(!BOX_MAP.overlaps(OVERLAY_MAP), "the box map over the overlay map");
static_assert(!BOX_MAP.overlaps(MAIN_BG_SPARE), "the box map over the spare");
static_assert(!OVERLAY_MAP.overlaps(MAIN_BG_SPARE), "the overlay map over the spare");
static_assert(!OBJ_RESIDENT.overlaps(OBJ_BOUNDARY64), "the object reserves collide");
static_assert(!SUB_CHR.overlaps(HUD_MAP), "sub characters over the HUD map");
static_assert(!SUB_CHR.overlaps(MENU_MAP), "sub characters over the menu map");
static_assert(!SUB_CHR.overlaps(MINIMAP_MAP), "sub characters over the minimap map");
static_assert(!SUB_CHR.overlaps(SUB_BG_SPARE), "sub characters over the spare");
static_assert(!HUD_MAP.overlaps(MENU_MAP), "the HUD map over the menu map");
static_assert(!HUD_MAP.overlaps(MINIMAP_MAP), "the HUD map over the minimap map");
static_assert(!HUD_MAP.overlaps(SUB_BG_SPARE), "the HUD map over the spare");
static_assert(!MENU_MAP.overlaps(MINIMAP_MAP), "the menu map over the minimap map");
static_assert(!MENU_MAP.overlaps(SUB_BG_SPARE), "the menu map over the spare");
static_assert(!MINIMAP_MAP.overlaps(SUB_BG_SPARE), "the minimap map over the spare");

// --- kind 3: every base is expressible in its register -----------------------
constexpr bool isCharBase(const Region& r) {
    return r.offset % CHAR_BLOCK == 0 && r.charBase() <= CHAR_BASE_MAX;
}
constexpr bool isMapBase(const Region& r) {
    return r.offset % MAP_BLOCK == 0 && r.mapBase() <= MAP_BASE_MAX;
}
static_assert(isCharBase(GROUND_CHR) && GROUND_CHR.charBase() == 0, "BG char base");
static_assert(isCharBase(UI_CHR) && UI_CHR.charBase() == 2, "BG char base");
static_assert(isMapBase(GROUND_MAP) && GROUND_MAP.mapBase() == 24, "BG map base");
static_assert(isMapBase(BOX_MAP) && BOX_MAP.mapBase() == 28, "BG map base");
static_assert(isMapBase(OVERLAY_MAP) && OVERLAY_MAP.mapBase() == 29, "BG map base");
static_assert(isCharBase(SUB_CHR) && SUB_CHR.charBase() == 0, "sub BG char base");
static_assert(isMapBase(HUD_MAP) && HUD_MAP.mapBase() == 8, "sub BG map base");
static_assert(isMapBase(MENU_MAP) && MENU_MAP.mapBase() == 9, "sub BG map base");
static_assert(isMapBase(MINIMAP_MAP) && MINIMAP_MAP.mapBase() == 10, "sub BG map base");

// The reason everything is under 62 KiB.  Past that a base needs DISPCNT's
// engine-wide 64 KiB term, which moves all four layers together -- and engine B
// has no such term at all, so an offset that needs it on the main engine has no
// equivalent on the sub one.  Both DISPCNT base fields stay zero.
constexpr uint32_t BASE_REACH = uint32_t(MAP_BASE_MAX + 1) * MAP_BLOCK;   // 64 KiB
static_assert(BASE_REACH == 64 * KiB);
static_assert(MAIN_BG_SPARE.end() <= BASE_REACH,
              "a main BG region needs DISPCNT's engine-wide base term, which "
              "would shift every layer including the ones that do not want it");
static_assert(SUB_BG_SPARE.end() <= BASE_REACH,
              "a sub BG region is past what BGxCNT reaches, and engine B has no "
              "DISPCNT term to make up the difference");

// --- kind 4: agreement with the rest of the tree -----------------------------
// GROUND_CHR must hold every character a text layer can address, so that no
// scene ever authored can outgrow it.  1024 is the ten-bit index field.
static_assert(GROUND_CHR.bytes >= 1024u * 32u,
              "a text layer can address 1024 characters; reserve all of them or "
              "a future scene outgrows this file, which is frozen");
static_assert(SUB_CHR.bytes >= 512u * 32u,
              "the bottom screen needs the font plus its own furniture");

// ---------------------------------------------------------------------------
// WHAT THIS GIVES UP, AND HOW TO GET IT BACK
//
// This file is frozen, which is only honest if the things it refuses have a
// written way out.  Each of these is a decision with a cost, not an oversight.
//
// EXTENDED PALETTES: none, on either engine.  The art is authored to 15-colour
//   sub-palettes and the DS gives sixteen of them where the SNES gave eight, so
//   there is nothing to buy.  RECOVERY: bank G is held for exactly this.  Main
//   BG extended palette is G at MST 4 (slots 0-1 at OFS 0, 2-3 at OFS 1); main
//   OBJ extended palette is G at MST 5, slot 0, lower 8 KiB only.  The sub
//   engine's are H at MST 2 and I at MST 3, which would COST the bank currently
//   serving that window -- so the sub engine's extended palettes are the
//   expensive ones and the main engine's are free.  Remember that an extended
//   palette is not CPU-visible while mapped: switch the bank to LCDC, write it
//   at its LCDC address, switch back, then set DISPCNT bit 30 or 31.
//
// A FOURTH RESIDENT OBJECT PAGE: does not fit.  Three pages are 31744 bytes of
//   the 32768 a ten-bit tile number reaches at boundary 32, and the town's page
//   only fits because it SUBSTITUTES for the second rather than joining it.
//   RECOVERY: OBJ_BOUNDARY64 above is the reserve, and taking it means raising
//   the boundary in tools/ds_encode.py, which HALVES every cel's tile number --
//   dsTileFor() is generated against the boundary for that reason.  No bank
//   moves; bank E already covers 64 KiB.
//   AND IT COSTS THE BOTTOM SCREEN HALF ITS SPRITE TILES, which was not written
//   down until the pass that added SUB_OBJ_TILES: the boundary is one field of
//   one register per engine, but the ceiling it produces is bounded by the BANK
//   on the sub engine and by the REACH on the main one, so raising it buys 32 KiB
//   up here and loses 256 tile numbers down there.  Bank I is 16 KiB, which is
//   512 units at boundary 32 and 256 at 64.  Every assertion in this file passed
//   at boundary 64 before that one existed, so the trade was invisible.
//
// MORE THAN 128 KiB OF TEXTURE: one slot of the four.  Texture space is 512 KiB
//   addressed as four 128 KiB slots, slot n at texture offset 0x20000*n, and the
//   slot is the OFS field -- so any of A-D can be any slot.  In practice the
//   pairing is FIXED BY CONVENTION at A=0, B=1, C=2, D=3, because libnds's GL
//   allocator treats banks A-D as one contiguous heap in that order and its
//   aliases hard-code it.  Here A is slot 0 and B is spent on main BG, so the
//   slots actually available are 2 (bank C) and 3 (bank D).  RECOVERY: take D
//   and slot 3 first -- it costs the sub-OBJ expansion path, and I already has
//   four times the sprite room the bottom screen needs -- then C and slot 2,
//   which costs the sub-BG one.  But note the entry below: if a REAR-PLANE
//   bitmap is ever wanted it needs both of those slots together, so taking one
//   for texture forecloses it.
//   Two arithmetic rules go with it, both from PLTT_BASE and TEXIMAGE_PARAM:
//   texture image data is addressed div-8 so it must be 8-byte aligned, and a
//   palette base is div-16 for every format except the 4-colour one (which is
//   div-8), so a 16-colour palette must be 16-byte aligned.  Texture palette
//   space is 0x18000 bytes across up to six 16 KiB slots -- and F and G can only
//   reach slots 0, 1, 4 and 5, because their OFS maps to (OFS.0)+(OFS.1*4).
//   Slots 2 and 3 come only from E.
//
// A SECOND SUB-ENGINE BACKGROUND BANK: H is 32 KiB.  RECOVERY: bank I can be sub
//   BG at MST 1, where it lands at 0x06208000 -- immediately after H, so the two
//   are contiguous and the window becomes 48 KiB.  That costs sub OBJ, which
//   would then have to come from D.
//
// MOSAIC ON THE 3D GROUND: impossible, and this is the one that is not a VRAM
//   decision at all but has to be recorded where the 3D path is chosen.  GBATEK,
//   on the 3D layer: "All other bits in BG0CNT have no effect on 3D, namely,
//   mosaic cannot be used on the 3D layer."  The Dive's Shatter and the night's
//   Tear both coarsen the GROUND with MOSAIC (dive.s:174, night.s:780, both
//   `ora #$01` = BG1 only), so the 3D renderer cannot reproduce either.  The 2D
//   renderer can, which is one more reason both survive in this allocation
//   rather than one replacing the other.  See docs/behaviour/divergences/.
//
// A 3D REAR-PLANE BITMAP: not available, and it is the one recovery path that is
//   NOT a matter of finding a spare bank.  GBATEK, "Rear Color/Depth Bitmaps":
//   the rear plane can be fed by bitmap instead of by CLEAR_COLOR, and it is
//   "two bitmaps (one with color data, one with depth data), each containing
//   256x256 16bit entries, and so, each occupying a whole 128K slot -- Rear Color
//   Bitmap (located in Texture Slot 2) ... Rear Depth Bitmap (located in Texture
//   Slot 3) ... This method requires VRAM to be allocated to Texture Slot 2 AND
//   3 ... in that case the VRAM is used as Rear-plane, and cannot be used for
//   Textures."
//
//   So the slots are not negotiable and they are not independent: a rear-plane
//   costs slot 2 AND slot 3, which under the pairing above is bank C AND bank D
//   -- BOTH, or neither.  That spends the entire remaining texture budget and
//   both of the sub engine's expansion banks at once, and it is worth knowing
//   before wanting one, because a fog gradient behind the 3D ground is exactly
//   the sort of thing the night and the Dive would ask for.  The register method
//   (CLEAR_COLOR plus CLEAR_DEPTH) costs no VRAM at all and is the answer unless
//   a per-pixel backdrop is genuinely the point.
//
// A SECOND BANK IN THE MAIN OBJ WINDOW: bank E has NO OFS field ("Offset not
//   used by VRAM-E,H,I"), so it can only ever sit at the base of whatever window
//   it is in -- 0x06400000 here.  Anything else added to that window must
//   therefore be placed ABOVE it, and A or B at OFS 0 would land exactly on top
//   of it.  There is no assertion that can catch this, because the second bank
//   would be mapped by a later task's VRAMCNT write and not by this file: it is
//   written down instead, which is what this section is for.
//
// ANY REGION PAST 62 KiB IN A BG WINDOW: needs DISPCNT's 64 KiB base term, which
//   is engine-wide and moves all four layers together, and which engine B does
//   not have at all.  RECOVERY: there is 66 KiB of bank B and 66 KiB of bank H
//   past the reservations here, but reaching it means every other base on that
//   engine moves too.  Prefer the spare regions above.
// ---------------------------------------------------------------------------

}  // namespace kh::vram

// The cross-file checks live behind a guard so vram_map.h stays standalone --
// its whole job is to be includable by the device tier before anything else
// exists.  Anything that has already included gen/assets.h gets the checks free.
#ifdef KH_ASSETS_H_INCLUDED
namespace kh::vram {

// The whole of bank E is reserved for sprites, in two halves: the part a tile
// number reaches at the boundary in force today, and the part that becomes
// reachable if the boundary is ever raised.  The invariant is not that the two
// match some particular number -- it is that WHATEVER the boundary is, the reach
// it buys fits inside what this file reserved.  Raising the boundary to 64 is
// therefore fine and passes; raising it to 128 asks for 128 KiB of window from a
// 64 KiB bank, and fails here rather than on hardware.
static_assert(OBJ_RESIDENT.bytes + OBJ_BOUNDARY64.bytes == 64 * KiB,
              "the sprite reservation is bank E in full");
static_assert(uint32_t(OBJ_REACH) <= OBJ_RESIDENT.bytes + OBJ_BOUNDARY64.bytes,
              "gen/assets.h's 1D boundary now reaches further than this file "
              "reserved for sprites; the extra would land in whatever follows "
              "bank E in the OBJ window, which is nothing, so the sprites past "
              "the reservation would simply not be there");
static_assert(OBJ_RESIDENT_BYTES <= uint32_t(OBJ_REACH),
              "the emitted object pages are past what a ten-bit tile number "
              "reaches at the current boundary");
// Not a boundary check -- the one above is.  This pins OBJ_RESIDENT itself, so
// that widening it to "make room" silently eats the boundary-64 reserve.
static_assert(int(MAP_TILE_MASK) + 1 == TEXT_LAYER_CHARS,
              "gen/assets.h's ten-bit character reach and this file's disagree");
static_assert(OBJ_RESIDENT.bytes == 1024u * 32u,
              "OBJ_RESIDENT is by definition the reach at boundary 32: 1024 tile "
              "numbers of 32 bytes.  Widening it does not create address space, "
              "it just overlaps the reserve that boundary 64 would need");
// The sub engine's ceiling is the BANK, not the index, and SUB_OBJ_TILES was
// derived above from CHAR_BYTES because this header must compile without
// gen/assets.h.  Here both are in scope, so the assumption is checked: raising
// the boundary halves the tile count and this catches a stale 512.
static_assert(int(SUB_OBJ_CHR.bytes) / OBJ_BOUNDARY == SUB_OBJ_TILES,
              "the sub-engine sprite ceiling was computed at boundary 32 and the "
              "boundary has moved; every sub-engine tile number is now wrong by "
              "the same factor");
static_assert(OBJ_TILE_NUMBERS == int(MAP_TILE_MASK) + 1,
              "a tile number and a character number are both ten bits");

}  // namespace kh::vram
#endif

// The second cross-file block, and the same idea: constants.h is the gameplay
// tier's numbers and this is the hardware's, and there is exactly one number
// that belongs to both.  Guarded, so vram_map.h still compiles standalone --
// the device tier includes it before constants.h exists in the build at all.
//
// The dependency runs THIS WAY ROUND on purpose.  constants.h could include
// vram_map.h and get the tie unconditionally, but then the platform-neutral
// simulation -- the whole of Tier 1, everything the host suite tests with no
// hardware present -- would depend on the DS's VRAM layout to compile.  That is
// backwards, and the guard is the price of keeping it the right way round.
#ifdef KH_CONSTANTS_H_INCLUDED
namespace kh::vram {

// MAX_OBJECTS is a DS HARDWARE NUMBER in the gameplay header, which that
// header's own docstring forbids: "Nothing SNES-hardware-specific is here: no
// VRAM addresses, no PPU register values, no palette CGRAM layout."  The rule is
// right and the exception was not deliberate -- 128 is the OAM entry count, it
// belongs to this file, and it was asserted against nothing whatsoever.
static_assert(MAX_OBJECTS == OAM_ENTRIES,
              "constants.h's per-engine object budget is the number of OAM "
              "entries the hardware has; they cannot be two different numbers");

// The scenery budget with the rest of a worst-case frame on top of it.  This is
// constants.h's own assertion restated against the hardware number rather than
// against the copy of it -- so it now means what it says, instead of comparing
// 128 with a 128 typed two lines above.
static_assert(OBJ_BUDGET_SCENERY + TRANSIENT_ACTORS + 4 + 1 <= OAM_ENTRIES,
              "a camera window's scenery, the transients, a four-quadrant boss "
              "and Sora do not fit in one engine's OAM");

// THE POOL IS NOT THE BUDGET, and the two being equal today is a coincidence of
// two unrelated arguments -- MAX_ACTORS is 128 because the island needs 69 props
// to exist, OAM_ENTRIES is 128 because the hardware says so.  A full pool
// therefore CANNOT be drawn, and that is fine: the pool holds the whole map and
// OAM holds what is on screen.  tools/check_map.py is what keeps the two apart,
// by sliding a camera window over every map and holding it to
// OBJ_BUDGET_SCENERY.  Asserted as an inequality in the direction that is
// actually load-bearing: the budget must fit OAM, the pool need not.
static_assert(OBJ_BUDGET_SCENERY < MAX_ACTORS,
              "the scenery budget is a limit on a window of the pool, so a "
              "budget at or above the pool size limits nothing");

}  // namespace kh::vram
#endif
