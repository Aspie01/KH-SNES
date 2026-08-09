// The OAM attribute packing, against GBATEK's layout and the SNES's decisions.
//
// The depth sort had an oracle it could copy. This half does not, quite: the
// two machines lay an entry out differently enough that the port is a
// translation, so what these cases pin is that each SNES DECISION survived the
// translation -- the priority relation, the flash parity, the mirror rule, the
// shadow's own palette -- rather than that the bytes match, which they cannot.
//
// The usual caveat stands and is not weaker here for the layout being quoted:
// nothing in this container has drawn a sprite.

#include <cstring>

#include "actor.h"
#include "check.h"
#include "constants.h"
#include "gen/assets.h"
#include "grid.h"
#include "oam.h"
#include "oam_pack.h"
#include "vram_map.h"

using namespace kh;
using namespace kh::device;

namespace {

SpriteSlot g_slots[OAM_SLOTS];
OamEntry g_oam[OAM_SLOTS];

int at(Actors& a, ActType t, int px, int py) {
    return a.spawn(t, World::fromRaw(px << 4), World::fromRaw(py << 4));
}

Camera still() { return Camera{}; }

// Field accessors, written out rather than shared with the implementation, so
// a mistake in the shifts is not agreed with by the thing checking it.
int y_of(const OamEntry& e) { return e.attr0 & 0xFF; }
int shape_of(const OamEntry& e) { return (e.attr0 >> 14) & 3; }
bool disabled(const OamEntry& e) { return (e.attr0 & (1u << 9)) != 0; }
int x_of(const OamEntry& e) { return e.attr1 & 0x1FF; }
bool hflip_of(const OamEntry& e) { return (e.attr1 & (1u << 12)) != 0; }
int size_of(const OamEntry& e) { return (e.attr1 >> 14) & 3; }
int tile_of(const OamEntry& e) { return e.attr2 & 0x3FF; }
int prio_of(const OamEntry& e) { return (e.attr2 >> 10) & 3; }
int pal_of(const OamEntry& e) { return (e.attr2 >> 12) & 0xF; }

OamBuild build(const Actors& a) { return buildOam(a, still(), g_slots); }

}  // namespace

KH_TEST(oampack_the_page_bases_are_what_the_pipeline_actually_emits) {
    // Derived, not typed -- and this is the check that the derivation matches
    // the bytes tools/build_assets.py writes.  A page resized without these
    // moving would put every cel on it at the wrong address, and every sprite
    // from that page would be some other sprite.
    // By name, with strcmp.  The first draft matched on characters at fixed
    // offsets and got "objtownchr" wrong -- index 3 is 't', not 'o' -- which
    // is a silly way to fail a check whose whole point is exactness.
    uint32_t sora = 0, obj = 0, obj2 = 0, town = 0;
    for (const SpriteAsset& s : SPRITE_ASSETS) {
        if (std::strcmp(s.name, "sorachr") == 0) sora = s.bytes;
        if (std::strcmp(s.name, "objchr") == 0) obj = s.bytes;
        if (std::strcmp(s.name, "obj2chr") == 0) obj2 = s.bytes;
        if (std::strcmp(s.name, "objtownchr") == 0) town = s.bytes;
    }
    CHECK(sora && obj && obj2 && town);     // all four found by name
    CHECK_EQ(int(sora), SORA_SHEET_BYTES);
    CHECK_EQ(int(obj), OBJ_PAGE_BYTES);
    CHECK_EQ(int(obj2), OBJ_PAGE_BYTES);
    CHECK_EQ(int(town), OBJ_PAGE_BYTES);
    // The second page is SHARED: the town's sheet is the same size because it
    // substitutes for the islanders rather than joining them.
    CHECK_EQ(int(obj2), int(town));
    CHECK_EQ(int(sora + obj + obj2), int(OBJ_RESIDENT_BYTES));

    // Sora's sheet is thirty cels, which is the six frames by five drawn
    // facings constants.h counts.
    CHECK_EQ(int(sora) / OBJ_CEL_BYTES, SORA_CELS);
    CHECK_EQ(SORA_CELS, SORA_FACINGS * SORA_CELS_PER_FACING);

    // ...and the bases those imply, checked at the boundary in force.
    CHECK_EQ(int(soraTile(0)), 0);
    CHECK_EQ(int(soraTile(1)), OBJ_CEL_TILES);
    CHECK_EQ(int(pageTile(0, false)), SORA_SHEET_BYTES / OBJ_BOUNDARY);
    CHECK_EQ(int(pageTile(0, true)),
             (SORA_SHEET_BYTES + OBJ_PAGE_BYTES) / OBJ_BOUNDARY);
    // Everything resident fits the ten-bit number, with the margin stated.
    CHECK(int(pageTile(0, true)) + OBJ_PAGE_BYTES / OBJ_BOUNDARY
          <= int(MAP_TILE_MASK) + 1);
}

KH_TEST(oampack_a_sprite_is_square_and_the_size_is_the_large_flag) {
    // 16x16 and 32x32 are the only two shapes this game has, and AF_LARGE is
    // the whole of the distinction (oam.s:434-449).  Shape 0 is square; size 1
    // is 16x16 and size 2 is 32x32 in GBATEK's table.
    Actors a;
    a.clear();
    const int big = at(a, ActType::Sora, 100, 100);
    const int small = at(a, ActType::Shadow, 140, 100);
    CHECK(has(flagsFor(ActType::Sora), ActFlags::Large));
    CHECK(!has(flagsFor(ActType::Shadow), ActFlags::Large));

    const OamBuild b = build(a);
    packAll(g_slots, b.used, a, big, 0, 0, g_oam);
    for (int k = 0; k < b.used; ++k) {
        if (g_slots[k].kind != SpriteKind::Actor) continue;
        CHECK_EQ(shape_of(g_oam[k]), 0);
        CHECK_EQ(size_of(g_oam[k]), g_slots[k].actor == big ? 2 : 1);
        CHECK(!disabled(g_oam[k]));
    }
    CHECK(small >= 0);
}

KH_TEST(oampack_every_sprite_takes_priority_two) {
    // oam.s:625's `ora #$20` -- "priority 2: above BG1, below the BG3 HUD".
    // The DS keeps the RELATION rather than the number: device/init.cpp gives
    // the ground priority 3, the overlay 1 and the box 0, and OBJ wins a tie
    // against a background, so 2 is over the ground and under both.
    Actors a;
    a.clear();
    at(a, ActType::Sora, 100, 100);
    at(a, ActType::Darkside, 60, 140);
    const OamBuild b = build(a);
    packAll(g_slots, b.used, a, 0, 0, 0, g_oam);
    CHECK(b.used >= 3);
    for (int k = 0; k < b.used; ++k) CHECK_EQ(prio_of(g_oam[k]), 2);

    // The relation itself, restated where it is depended on: sprites in front
    // of the ground, behind the overlay and the box.
    CHECK(int(OBJ_PRIORITY) < 3);           // ground is 3
    CHECK(int(OBJ_PRIORITY) > 1);           // overlay is 1, box is 0
}

KH_TEST(oampack_a_negative_coordinate_wraps_rather_than_clamping) {
    // THE WRAP IS THE BEHAVIOUR.  Y is eight bits and X is nine, so a sprite at
    // y = -16 is stored as 240 and the hardware draws rows 240..255 -- off a
    // 192-line screen -- and then 0..15 at the top.  That is what makes a
    // partially off-screen sprite work, and it is why oam.cpp's cull can accept
    // anything from -32 without a second thought.  A clamp would pin the sprite
    // to the edge and it would slide along it instead of leaving.
    Actors a;
    a.clear();
    at(a, ActType::Shadow, 8 - 24, 100);        // small anchor -8: x = -24
    OamBuild b = build(a);
    CHECK(b.used >= 1);
    packAll(g_slots, b.used, a, -1, -1, 0, g_oam);
    CHECK_EQ(int(g_slots[0].x), -24);
    CHECK_EQ(x_of(g_oam[0]), 512 - 24);

    a.clear();
    at(a, ActType::Shadow, 100, 16 - 16);       // small anchor -16: y = -16
    b = build(a);
    packAll(g_slots, b.used, a, -1, -1, 0, g_oam);
    CHECK_EQ(int(g_slots[0].y), -16);
    CHECK_EQ(y_of(g_oam[0]), 256 - 16);
    // ...and the wrap really does bring it back onto the screen: 240 + 16 rows
    // of a 16-tall sprite is 256, so it lands on rows 0..0 -- the bottom edge
    // of the sprite just touching the top of the display.
    CHECK_EQ((240 + 16) % 256, 0);
}

KH_TEST(oampack_the_flash_is_the_frame_parity_and_not_a_toggle) {
    // oam.s:288-297: while actHitT is non-zero, `frameCount & 2` alternates the
    // palette with PAL_OBJ_FX.  `& 2` and not `& 1` makes it a FOUR-frame
    // cycle -- two on, two off -- which reads as a flash rather than shimmer.
    //
    // §M6 records that a swallowed NMI flips this parity permanently, which is
    // why frameCount is a parameter here and not read from anywhere.
    Actors a;
    a.clear();
    const int i = at(a, ActType::Shadow, 100, 100);
    const uint8_t own = a.pal[i];
    a.hitT[i] = 5;

    int flashed = 0;
    for (uint32_t f = 0; f < 8; ++f) {
        const OamBuild b = build(a);
        packAll(g_slots, b.used, a, -1, -1, f, g_oam);
        const bool isFx = pal_of(g_oam[0]) == int(pal::Fx);
        CHECK_EQ(isFx, (f & 2u) != 0u);
        if (isFx) ++flashed;
    }
    CHECK_EQ(flashed, 4);           // four of eight: a two-on two-off cycle

    // ...and with no hit timer it never flashes, whatever the frame.
    a.hitT[i] = 0;
    for (uint32_t f = 0; f < 8; ++f) {
        const OamBuild b = build(a);
        packAll(g_slots, b.used, a, -1, -1, f, g_oam);
        CHECK_EQ(pal_of(g_oam[0]), int(own));
    }
}

KH_TEST(oampack_page1_selects_a_base_rather_than_setting_a_bit) {
    // The SNES reached its second page through bit 0 of the attribute byte
    // (game.inc:101, oam.s:640-647).  A DS tile number is ten flat bits, so the
    // flag has to become a BASE -- and a port that kept looking for a bit to
    // set would put every islander and every raft material on top of the
    // Heartless.
    Actors a;
    a.clear();
    const int islander = at(a, ActType::Kairi, 100, 100);
    const int heartless = at(a, ActType::Shadow, 140, 100);
    CHECK(has(flagsFor(ActType::Kairi), ActFlags::Page1));
    CHECK(!has(flagsFor(ActType::Shadow), ActFlags::Page1));

    const OamBuild b = build(a);
    packAll(g_slots, b.used, a, -1, -1, 0, g_oam);
    int kairiTile = -1, shadowTile = -1;
    for (int k = 0; k < b.used; ++k) {
        if (g_slots[k].kind != SpriteKind::Actor) continue;
        if (g_slots[k].actor == islander) kairiTile = tile_of(g_oam[k]);
        if (g_slots[k].actor == heartless) shadowTile = tile_of(g_oam[k]);
    }
    CHECK(kairiTile >= 0 && shadowTile >= 0);
    // A whole page apart, and both inside the resident set.
    CHECK_EQ(kairiTile - int(pageTile(a.tile[islander], false)),
             OBJ_PAGE_BYTES / OBJ_BOUNDARY);
    CHECK(kairiTile < int(MAP_TILE_MASK) + 1);
    CHECK(shadowTile < int(pageTile(0, true)));
}

KH_TEST(oampack_soras_cel_is_the_one_updateSoraFrame_returns) {
    // world.cpp's updateSoraFrame() picks the cel and writes the mirror bit,
    // and its caller DISCARDS the cel with a comment saying the rest "is the
    // device tier's half" (world.cpp:900-905).  This is that half: until now
    // the return value had no consumer at all.
    Actors a;
    a.clear();
    const int sora = at(a, ActType::Sora, 100, 100);

    for (int cel = 0; cel < SORA_CELS; ++cel) {
        const OamBuild b = build(a);
        packAll(g_slots, b.used, a, sora, cel, 0, g_oam);
        CHECK_EQ(tile_of(g_oam[0]), cel * OBJ_CEL_TILES);
    }
    // The last cel still fits under the first object page, which is what makes
    // the sheet and the pages disjoint.
    CHECK_EQ(int(soraTile(SORA_CELS - 1)) + OBJ_CEL_TILES,
             int(pageTile(0, false)));

    // Somebody who is not the player is not Sora's sheet even if their type is
    // -- the sheet is one actor's, and a second Sora would be a page lookup.
    const int other = at(a, ActType::Sora, 140, 100);
    const OamBuild b = build(a);
    packAll(g_slots, b.used, a, sora, 3, 0, g_oam);
    for (int k = 0; k < b.used; ++k) {
        if (g_slots[k].kind != SpriteKind::Actor) continue;
        if (g_slots[k].actor == other)
            CHECK_EQ(tile_of(g_oam[k]), int(pageTile(a.tile[other], false)));
    }
}

KH_TEST(oampack_the_mirror_bit_comes_from_the_flag_and_never_from_a_shadow) {
    // world.cpp's DRAW_FLIP collapses eight compass directions to five drawn
    // facings and a mirror -- "west is east, mirrored".  The flag is simulation
    // state written in exactly one place; this reads it.
    //
    // A SHADOW NEVER MIRRORS.  An ellipse has no facing, and inheriting the bit
    // would be harmless today and wrong the moment a blob stops being
    // symmetrical.
    Actors a;
    a.clear();
    const int i = at(a, ActType::Sora, 100, 100);
    a.flags[i] = a.flags[i] | ActFlags::HFlip;

    const OamBuild b = build(a);
    packAll(g_slots, b.used, a, i, 0, 0, g_oam);
    bool sawActor = false, sawShadow = false;
    for (int k = 0; k < b.used; ++k) {
        if (g_slots[k].kind == SpriteKind::Actor) {
            CHECK(hflip_of(g_oam[k]));
            sawActor = true;
        }
        if (g_slots[k].kind == SpriteKind::Shadow) {
            CHECK(!hflip_of(g_oam[k]));
            sawShadow = true;
        }
    }
    CHECK(sawActor && sawShadow);
}

KH_TEST(oampack_a_shadow_is_its_own_blob_with_its_own_palette) {
    // It is not a darkened copy of the actor: it is one of two ellipses,
    // game.inc:82 and :96, and it draws from pal::Shadow.
    Actors a;
    a.clear();
    const int big = at(a, ActType::Sora, 100, 100);          // Large + Shadow
    const OamBuild b = build(a);
    packAll(g_slots, b.used, a, big, 0, 0, g_oam);
    for (int k = 0; k < b.used; ++k) {
        if (g_slots[k].kind != SpriteKind::Shadow) continue;
        CHECK_EQ(pal_of(g_oam[k]), int(pal::Shadow));
        CHECK_EQ(tile_of(g_oam[k]), int(pageTile(sprite::ShadowBig, false)));
        CHECK_EQ(size_of(g_oam[k]), 2);
        // ...and it does NOT take the actor's palette, which is the whole point.
        CHECK(pal_of(g_oam[k]) != int(a.pal[big]) || int(pal::Shadow) == int(a.pal[big]));
    }
}

KH_TEST(oampack_a_boss_quadrant_takes_its_own_corner_of_the_sheet) {
    // oam.s:363's quadT: $00, $04, $40, $44.  A SNES page is 16 characters wide
    // and a 32x32 object is a 4x4 block, so the quarters are four cels apart
    // across and sixty-four down.
    Actors a;
    a.clear();
    const int boss = at(a, ActType::Darkside, 128, 150);
    const OamBuild b = build(a);
    packAll(g_slots, b.used, a, -1, -1, 0, g_oam);

    const uint8_t base = a.tile[boss];
    const uint8_t want[4] = {0x00, 0x04, 0x40, 0x44};
    int seen = 0;
    for (int k = 0; k < b.used; ++k) {
        if (g_slots[k].kind != SpriteKind::BossQuadrant) continue;
        const int q = g_slots[k].quadrant;
        CHECK_EQ(tile_of(g_oam[k]),
                 int(pageTile(uint8_t(base + want[q]),
                              has(a.flags[boss], ActFlags::Page1))));
        CHECK_EQ(size_of(g_oam[k]), 2);         // every quadrant is 32x32
        ++seen;
    }
    CHECK_EQ(seen, 4);
    // Four distinct cels, which is what stops a boss being one quarter drawn
    // four times.
    int tiles[4] = {-1, -1, -1, -1};
    int n = 0;
    for (int k = 0; k < b.used; ++k)
        if (g_slots[k].kind == SpriteKind::BossQuadrant) tiles[n++] = tile_of(g_oam[k]);
    for (int p = 0; p < 4; ++p)
        for (int q = p + 1; q < 4; ++q) CHECK(tiles[p] != tiles[q]);
}

KH_TEST(oampack_every_unused_slot_is_hidden_and_not_merely_left) {
    // ClearOamBuffer (oam.s:31-51) runs at the TOP of BuildOam, and the reason
    // is not obvious from the name: OAM holds what the previous frame left in
    // it, so a frame with fewer sprites than the last would go on drawing the
    // tail of the last one, frozen, until something overwrote it.
    Actors a;
    a.clear();
    at(a, ActType::Shadow, 100, 100);
    const OamBuild b = build(a);
    CHECK(b.used < OAM_SLOTS);

    // Poison first, so "hidden" is something the packer did rather than
    // something the array already was.
    for (int k = 0; k < OAM_SLOTS; ++k) g_oam[k] = OamEntry{0, 0, 0};
    packAll(g_slots, b.used, a, -1, -1, 0, g_oam);

    for (int k = b.used; k < OAM_SLOTS; ++k) {
        CHECK(disabled(g_oam[k]));              // the DS's explicit bit...
        CHECK_EQ(y_of(g_oam[k]), 224);          // ...and parked, as belt to it
    }
    // 224 and not 240: a 32-tall sprite at 240 would span 240..255 and then
    // 0..15, and reappear across the top of the screen.
    CHECK(224 + 32 <= 256);
    CHECK(224 >= SCREEN_H);
    // Every used slot is enabled, so the two states cannot be confused.
    for (int k = 0; k < b.used; ++k) CHECK(!disabled(g_oam[k]));
}

KH_TEST(oampack_a_full_pool_packs_every_slot_and_overruns_none) {
    Actors a;
    a.clear();
    for (int k = 0; k < MAX_ACTORS; ++k)
        at(a, ActType::Shadow, (k % 16) * 12, (k / 16) * 12 + 40);
    const OamBuild b = build(a);
    CHECK_EQ(b.used, OAM_SLOTS);

    packAll(g_slots, b.used, a, -1, -1, 0, g_oam);
    for (int k = 0; k < OAM_SLOTS; ++k) {
        CHECK(!disabled(g_oam[k]));
        CHECK(tile_of(g_oam[k]) <= int(MAP_TILE_MASK));
        CHECK_EQ(prio_of(g_oam[k]), 2);
    }
}
