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
// Nothing in this file may be edited.  If a later task needs something this
// allocation does not give it, the answer is in WHAT THIS GIVES UP at the bottom
// -- every reservation has a named recovery path, and taking one is a decision
// with consequences that are written down rather than a number that gets nudged.
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
//
// Two constraints in that table do most of the work here, and neither is
// guessable:
//
//   C AND D CANNOT BE MAIN OBJ.  The main-OBJ rows list A, B, E, F, G and no
//   others.  Sprites must come out of one of those five, which is what stops
//   the two big flexible banks being spent on them.
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

constexpr uint32_t OAM_MAIN = 0x07000000;
constexpr uint32_t OAM_SUB = 0x07000400;

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
    Count
};

// Bit per Use, one row per bank, read straight off the quoted table.
constexpr uint16_t bit(Use u) { return uint16_t(1u << unsigned(u)); }

constexpr uint16_t BANK_CAN[] = {
    // A: LCDC, main BG, main OBJ, texture.  "Bit2 not used by VRAM-A,B,H,I".
    uint16_t(bit(Use::Lcdc) | bit(Use::MainBg) | bit(Use::MainObj) | bit(Use::Texture)),
    // B: identical to A.
    uint16_t(bit(Use::Lcdc) | bit(Use::MainBg) | bit(Use::MainObj) | bit(Use::Texture)),
    // C: main BG, texture, sub BG.  NOT main OBJ.
    uint16_t(bit(Use::Lcdc) | bit(Use::MainBg) | bit(Use::Texture) | bit(Use::SubBg)),
    // D: main BG, texture, sub OBJ.  NOT main OBJ.
    uint16_t(bit(Use::Lcdc) | bit(Use::MainBg) | bit(Use::Texture) | bit(Use::SubObj)),
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
        default: return -1;
    }
}

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
//
// MORE THAN 128 KiB OF TEXTURE: one slot.  RECOVERY: banks C and D are both
//   texture-capable at MST 3, so slots 1 and 2 are available without disturbing
//   anything that draws.  Taking C costs the sub engine's expansion path and
//   taking D costs sub OBJ's; take D first, since I has four times the sprite
//   room the bottom screen needs.
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
static_assert(OBJ_RESIDENT.bytes == 1024u * 32u,
              "OBJ_RESIDENT is by definition the reach at boundary 32: 1024 tile "
              "numbers of 32 bytes.  Widening it does not create address space, "
              "it just overlaps the reserve that boundary 64 would need");

}  // namespace kh::vram
#endif
