// Packing a SpriteSlot into three OAM halfwords.

#include "oam_pack.h"

#include "constants.h"
#include "gen/assets.h"
#include "vram_map.h"

namespace kh::device {
namespace {

// The page bases, in tile numbers.  A tile number counts in units of the 1D
// boundary, so the base is the running byte total divided by it -- and if the
// boundary is ever raised to 64 (vram_map.h's recovery path for a fourth page)
// every one of these halves along with every cel index, which is exactly why
// they are divisions and not literals.
constexpr int SORA_TILE = 0;
constexpr int PAGE0_TILE = SORA_SHEET_BYTES / OBJ_BOUNDARY;
constexpr int PAGE1_TILE = PAGE0_TILE + OBJ_PAGE_BYTES / OBJ_BOUNDARY;
constexpr int RESIDENT_TILES = PAGE1_TILE + OBJ_PAGE_BYTES / OBJ_BOUNDARY;

static_assert(SORA_SHEET_BYTES + 2 * OBJ_PAGE_BYTES == int(OBJ_RESIDENT_BYTES),
              "the three resident regions do not add up to what the pipeline "
              "emits; gen/assets.h's OBJ_RESIDENT_BYTES is the authority");
static_assert(RESIDENT_TILES <= MAP_TILE_MASK + 1,
              "the resident pages run past what a ten-bit tile number reaches");
static_assert(SORA_SHEET_BYTES % OBJ_BOUNDARY == 0
                  && OBJ_PAGE_BYTES % OBJ_BOUNDARY == 0,
              "a page must start on a tile-number boundary or every cel in it "
              "is addressed half a cel low");

// The two ground-shadow blobs.  game.inc:82 and :96 -- TILE_SHADOWBIG is the
// 32x32 and TILE_SHADOW the 16x16, and both "put the ellipse centre on the
// sprite's bottom edge" (oam.s, EmitShadowSprite) so the offsets drop it onto
// the actor's feet.  Both live on page 0.
constexpr uint8_t SHADOW_SMALL = sprite::Shadow;        // $E8
constexpr uint8_t SHADOW_LARGE = sprite::ShadowBig;     // $0C

// Quadrant tile offsets for a 64x64 boss, oam.s:363: $00, $04, $40, $44.  A
// SNES page is a 16-character-wide grid and a 32x32 object is a 4x4 block, so
// the four quarters are four cels apart across and sixty-four down.
constexpr uint8_t QUAD_TILE[4] = {0x00, 0x04, 0x40, 0x44};

uint16_t clip9(int v) { return uint16_t(uint32_t(v) & 0x1FFu); }
uint16_t clip8(int v) { return uint16_t(uint32_t(v) & 0xFFu); }

// Which palette this sprite draws with, and the flash.
//
// oam.s:288-297: while actHitT is non-zero, `frameCount & 2` alternates between
// the actor's own palette and PAL_OBJ_FX.  Two frames on, two off -- the `& 2`
// and not `& 1` is a four-frame cycle, which is slow enough to read as a flash
// rather than as shimmer.  §M6 notes this parity is one of the things a
// swallowed NMI would invert permanently, which is why frameCount is passed in
// rather than read from anywhere.
uint8_t paletteFor(const Actors& a, int i, uint32_t frameCount) {
    if (a.hitT[i] != 0 && (frameCount & 2u) != 0) return pal::Fx;
    return a.pal[i];
}

}  // namespace

uint16_t soraTile(int cel) {
    return uint16_t(SORA_TILE + cel * OBJ_CEL_TILES);
}

uint16_t pageTile(uint8_t snesTile, bool page1) {
    // dsTileFor() is the re-serialisation: "a SNES page is a 16-character-wide
    // grid in which a 32x32 object occupies a 4x4 block" (gen/assets.h:84), and
    // the DS pages are cel-contiguous instead.  The page base is added AFTER,
    // because dsTileFor is page-relative -- adding it first would scale the
    // base through the cel arithmetic.
    return uint16_t((page1 ? PAGE1_TILE : PAGE0_TILE) + dsTileFor(snesTile));
}

OamEntry packSprite(const SpriteSlot& s, const Actors& a, int player,
                    int soraCel, uint32_t frameCount) {
    const int i = s.actor;
    const bool page1 = has(a.flags[i], ActFlags::Page1);

    uint16_t tile = 0;
    uint8_t palette = 0;
    switch (s.kind) {
        case SpriteKind::Shadow:
            // The blob is its own sprite with its own palette -- it is not a
            // darkened copy of the actor, it is one of two ellipses.
            tile = pageTile(s.large ? SHADOW_LARGE : SHADOW_SMALL, false);
            palette = pal::Shadow;
            break;
        case SpriteKind::BossQuadrant:
            tile = pageTile(uint8_t(a.tile[i] + QUAD_TILE[s.quadrant]), page1);
            palette = paletteFor(a, i, frameCount);
            break;
        case SpriteKind::Actor:
            // Sora is the one actor with a sheet rather than a page entry: six
            // frames by five drawn facings, indexed by the cel updateSoraFrame
            // picked.  Everybody else is a fixed cel on one of the two pages.
            if (i == player && a.type[i] == ActType::Sora && soraCel >= 0)
                tile = soraTile(soraCel);
            else
                tile = pageTile(a.tile[i], page1);
            palette = paletteFor(a, i, frameCount);
            break;
    }

    // A shadow never mirrors.  The actor's HFlip is about which way it faces,
    // and an ellipse has no facing -- inheriting the bit would be harmless
    // today and wrong the moment a blob stops being symmetrical.
    const bool hflip = s.kind != SpriteKind::Shadow
                       && has(a.flags[i], ActFlags::HFlip);

    OamEntry e{};
    e.attr0 = uint16_t(clip8(s.y) | uint16_t(OBJ_SQUARE << OBJ_SHAPE_SHIFT));
    e.attr1 = uint16_t(clip9(s.x)
                       | uint16_t(hflip ? (1u << OBJ_HFLIP_BIT) : 0u)
                       | uint16_t((s.large ? OBJ_SIZE_32 : OBJ_SIZE_16)
                                  << OBJ_SIZE_SHIFT));
    e.attr2 = uint16_t(uint16_t(tile & MAP_TILE_MASK)
                       | uint16_t(OBJ_PRIORITY << OBJ_PRIORITY_SHIFT)
                       | uint16_t(uint16_t(palette & 0x0F) << OBJ_PALETTE_SHIFT));
    return e;
}

void packAll(const SpriteSlot* slots, int n, const Actors& a, int player,
             int soraCel, uint32_t frameCount, OamEntry* out) {
    for (int k = 0; k < n && k < OAM_SLOTS; ++k)
        out[k] = packSprite(slots[k], a, player, soraCel, frameCount);

    // ...and hide the rest.  ClearOamBuffer (oam.s:31-51) parks all 128 at
    // OAM_HIDE_Y before anything is written, for a reason that is not obvious
    // from the name: OAM holds what the PREVIOUS frame left in it, so a frame
    // with fewer sprites than the last would keep drawing the tail of the last
    // one, frozen, until something happened to overwrite it.
    //
    // The DS has a bit the SNES did not -- attr0's OBJ Disable -- and it is
    // used here in ADDITION to parking the sprite off screen, not instead.  The
    // bit is unambiguous; the position is the belt to its braces, and costs a
    // halfword that is being written anyway.
    //
    // 224 IS THE RIGHT PARK FOR BOTH MACHINES FOR DIFFERENT REASONS.  On the
    // SNES it is the screen height, and oam.s:23 explains: "a 32-tall sprite
    // here ends at 255 and never wraps back onto the visible 224 lines".  Here
    // the screen is 192, so 224 is well past the bottom -- but the reason it is
    // not 240 is the same wrap: a 32-tall sprite at 240 would span 240..255 and
    // then 0..15, and reappear across the top of the screen.
    constexpr uint16_t HIDE_Y = 224;
    static_assert(HIDE_Y + 32 <= 256,
                  "a parked 32-tall sprite must not wrap back onto the screen");
    static_assert(HIDE_Y >= SCREEN_H, "...and must be off the bottom of it");
    for (int k = n; k < OAM_SLOTS; ++k)
        out[k] = OamEntry{uint16_t(HIDE_Y | OBJ_DISABLE), 0, 0};
}

}  // namespace kh::device
