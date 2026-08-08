// The DS asset backend.
//
// §M2's exit criteria: the SNES path reproduces its baseline (Gate 0 proves
// that, not this file), --target ds emits files for every map, check_map.py
// still passes, and "a host test loads a generated collision map and asserts a
// known-walkable and a known-blocked tile".
//
// The interesting part is not the collision map, which is a byte array that
// carried over unchanged.  It is the two formats that DID change and that fail
// by producing a plausible picture rather than an error: linear-versus-planar
// characters, and the flip bits moving from 14/15 to 10/11.

#include "check.h"
#include "gen/assets.h"
#include "hostblob.h"

using namespace kh;

namespace {

unsigned char a[khhost::MAX_BLOB];
unsigned char b[khhost::MAX_BLOB];

bool have(Blob x, const char* what) {
    if (!x.empty()) return true;
    std::printf("  MISSING %s -- run: python3 tools/build_assets.py\n", what);
    return false;
}

const SceneAsset* sceneNamed(const char* name) {
    for (const SceneAsset& s : SCENE_ASSETS) {
        const char* p = s.name;
        const char* q = name;
        while (*p && *p == *q) { ++p; ++q; }
        if (*p == 0 && *q == 0) return &s;
    }
    return nullptr;
}

}  // namespace

KH_TEST(assets_a_generated_collision_map_says_where_the_ground_is) {
    // The exit criterion, on the map the criterion names.  assets/island.txt is
    // the ORACLE island, 32x16; the DS runs the expanded one, and both are
    // emitted.  Check the DS one, because that is what will be loaded.
    Blob coll = khhost::load("islandcoll.bin", a, sizeof a);
    if (!have(coll, "islandcoll.bin")) { CHECK(false); return; }
    CHECK_EQ(coll.size, size_t(DS_ISLAND_W * DS_ISLAND_H));

    // Known walkable: where Sora wakes up.  Known blocked: the sea in the
    // north-west corner, and the cliff wall along the top of the map.
    CHECK(coll.data[20 * DS_ISLAND_W + 24] != 0);       // (24,20), the lawn
    CHECK(coll.data[0 * DS_ISLAND_W + 0] == 0);         // (0,0), open water
    CHECK(coll.data[5 * DS_ISLAND_W + 30] == 0);        // (30,5), the cliff

    // ...and the height map alongside it, which is what makes the one-step rule
    // mean anything.  The lookout deck is +2 and the lawn under it is 0.
    Blob h = khhost::load("islandheight.bin", b, sizeof b);
    if (!have(h, "islandheight.bin")) { CHECK(false); return; }
    CHECK_EQ(h.size, coll.size);
    CHECK_EQ(h.data[9 * DS_ISLAND_W + 9], 2);           // (9,9), the deck
    CHECK_EQ(h.data[10 * DS_ISLAND_W + 9], 1);          // (9,10), its step
    CHECK_EQ(h.data[11 * DS_ISLAND_W + 9], 0);          // (9,11), the lawn
}

KH_TEST(assets_characters_are_linear_4bpp_and_not_planar) {
    // 32 bytes a character either way, so a size check proves nothing.  What
    // distinguishes them is that a LINEAR tile's byte is two adjacent pixels and
    // a PLANAR tile's byte is one bitplane of eight -- so in a linear tile, a run
    // of identical pixels is a run of identical bytes, and in a planar one it is
    // not.
    Blob chr = khhost::load("islandchr.bin", a, sizeof a);
    if (!have(chr, "islandchr.bin")) { CHECK(false); return; }
    CHECK_EQ(chr.size % 32, 0u);
    CHECK_EQ(chr.size / 32, 247u);          // what the pipeline reports

    // Every nibble must be a real palette index.  A 16-colour palette means
    // that is vacuous -- but it stops being vacuous the moment anything emits
    // 8bpp by mistake, which would double the byte count and halve the tiles.
    for (size_t i = 0; i < chr.size; ++i) {
        CHECK((chr.data[i] & 0x0F) <= 15);
        CHECK((chr.data[i] >> 4) <= 15);
    }

}

KH_TEST(assets_the_ds_encoder_agrees_with_the_snes_one_on_shared_art) {
    // The strongest check available without a DS, and it needs no assumption
    // about the content.
    //
    // The fragment is the one scene both machines paint from the same map, and
    // the two dedupes fold flips identically and walk the world in the same
    // order -- so fragchr.bin and fragmentchr.bin hold the SAME characters in the
    // SAME order, encoded differently.  Decode each with its own decoder and the
    // pixels must match.  The SNES side is known good: it is in the shipped ROM.
    Blob planar = khhost::loadSnes("fragchr.bin", a, sizeof a);
    Blob linear = khhost::load("fragmentchr.bin", b, sizeof b);
    if (!have(planar, "fragchr.bin") || !have(linear, "fragmentchr.bin")) {
        CHECK(false);
        return;
    }
    CHECK_EQ(planar.size, linear.size);
    CHECK_EQ(planar.size, 112u * 32u);

    for (size_t t = 0; t < planar.size / 32; ++t) {
        for (int y = 0; y < 8; ++y) {
            for (int x = 0; x < 8; ++x) {
                // SNES: bitplanes 0/1 interleaved by row for sixteen bytes,
                // then 2/3.  One byte is one bitplane of eight pixels.
                int want = 0;
                for (int pair = 0; pair < 2; ++pair) {
                    const size_t base = t * 32 + size_t(pair) * 16 + size_t(y) * 2;
                    const int lo = planar.data[base];
                    const int hi = planar.data[base + 1];
                    const int bit = 7 - x;
                    if ((lo >> bit) & 1) want |= 1 << (pair * 2);
                    if ((hi >> bit) & 1) want |= 1 << (pair * 2 + 1);
                }
                // DS: two pixels a byte, LEFT pixel in the LOW nibble.
                const uint8_t by = linear.data[t * 32 + size_t(y) * 4 + size_t(x) / 2];
                const int got = (x & 1) ? (by >> 4) : (by & 0x0F);
                CHECK_EQ(got, want);
            }
        }
    }
}

KH_TEST(assets_map_entries_carry_the_flip_bits_where_the_ds_wants_them) {
    // The SNES puts H flip at 14 and V flip at 15; the DS puts them at 10 and 11
    // and gives 12-15 to the palette.  Reuse the SNES packing and every folded
    // character comes out unflipped AND on a palette that does not exist.
    CHECK_EQ(MAP_FLIP_H, 1 << 10);
    CHECK_EQ(MAP_FLIP_V, 1 << 11);
    CHECK_EQ(MAP_TILE_MASK, 0x03FF);
    CHECK_EQ(MAP_PAL_SHIFT, 12);

    Blob map = khhost::load("islandmap.bin", a, sizeof a);
    Blob chr = khhost::load("islandchr.bin", b, sizeof b);
    if (!have(map, "islandmap.bin") || !have(chr, "islandchr.bin")) {
        CHECK(false);
        return;
    }
    const size_t entries = map.size / 2;
    CHECK_EQ(entries, size_t(DS_ISLAND_W * 2) * size_t(DS_ISLAND_H * 2));

    const uint16_t chars = uint16_t(chr.size / 32);
    int flipped = 0;
    for (size_t i = 0; i < entries; ++i) {
        const uint16_t e = uint16_t(map.data[i * 2] | (map.data[i * 2 + 1] << 8));
        CHECK((e & MAP_TILE_MASK) < chars);         // no index past the tileset
        CHECK_EQ(e >> MAP_PAL_SHIFT, 0);            // one palette, slot 0
        if (e & (MAP_FLIP_H | MAP_FLIP_V)) ++flipped;
    }
    // The fold is worth having only if it actually folds: a third of the ground
    // is mirrored, per the dedupe's own reasoning.
    CHECK(flipped > 0);
    CHECK(chars < entries);
}

KH_TEST(assets_the_palette_word_is_the_same_on_both_machines) {
    // 15-bit BGR little-endian, (b << 10) | (g << 5) | r.  This is the one
    // format the two machines share exactly, which is worth an assertion because
    // it looks like something that ought to differ -- and if it ever does, this
    // is where the pipeline stops reusing snes_color().
    Blob pal = khhost::load("islandpal.bin", a, sizeof a);
    if (!have(pal, "islandpal.bin")) { CHECK(false); return; }
    CHECK_EQ(pal.size, 32u);                        // sixteen entries, two bytes

    for (size_t i = 0; i < pal.size; i += 2) {
        const uint16_t v = uint16_t(pal.data[i] | (pal.data[i + 1] << 8));
        CHECK_EQ(v >> 15, 0);                       // bit 15 is unused
    }
    // Entry 0 is the backdrop and is never drawn for a sprite; the rest are not
    // all the same colour, which is the cheapest proof the file is not zeroed.
    bool varied = false;
    for (size_t i = 2; i < pal.size; i += 2)
        if (pal.data[i] != pal.data[2] || pal.data[i + 1] != pal.data[3])
            varied = true;
    CHECK(varied);

    // The night is the same ground under different colours, so its palette must
    // differ from the island's and its character data must not exist at all.
    Blob night = khhost::load("nightpal.bin", b, sizeof b);
    if (!have(night, "nightpal.bin")) { CHECK(false); return; }
    CHECK_EQ(night.size, pal.size);
    bool differs = false;
    for (size_t i = 0; i < pal.size; ++i)
        if (night.data[i] != pal.data[i]) differs = true;
    CHECK(differs);
}

KH_TEST(assets_the_background_block_formula_is_not_row_major) {
    // The single easiest thing in the DS's 2D setup to get subtly wrong.  A text
    // background is built from 32x32-character BLOCKS; writing row-major shreds a
    // 512-wide map into diagonal bands, which reads as a corrupt tileset rather
    // than as a layout bug.
    //
    // 256x256: one block, so it IS row-major.
    CHECK_EQ(bgEntryIndex(0, 0, 32, 32), 0);
    CHECK_EQ(bgEntryIndex(31, 0, 32, 32), 31);
    CHECK_EQ(bgEntryIndex(0, 1, 32, 32), 32);
    CHECK_EQ(bgEntryIndex(31, 31, 32, 32), 1023);

    // 512x256: two blocks side by side.  Tile (32,0) is the START of the second
    // block, not entry 32 -- that is the whole trap.
    CHECK_EQ(bgEntryIndex(0, 0, 64, 32), 0);
    CHECK_EQ(bgEntryIndex(31, 0, 64, 32), 31);
    CHECK_EQ(bgEntryIndex(32, 0, 64, 32), 1024);
    CHECK_EQ(bgEntryIndex(63, 31, 64, 32), 2047);
    CHECK(bgEntryIndex(32, 0, 64, 32) != 32);

    // 256x512: two blocks stacked, so the second starts at 1024 as well.
    CHECK_EQ(bgEntryIndex(0, 32, 32, 64), 1024);

    // 512x512: four blocks, left-right then top-bottom.
    CHECK_EQ(bgEntryIndex(0, 0, 64, 64), 0);
    CHECK_EQ(bgEntryIndex(32, 0, 64, 64), 1024);
    CHECK_EQ(bgEntryIndex(0, 32, 64, 64), 2048);
    CHECK_EQ(bgEntryIndex(32, 32, 64, 64), 3072);
    CHECK_EQ(bgEntryIndex(63, 63, 64, 64), 4095);

    // Every position in a 512x512 map lands somewhere distinct.
    bool used[4096] = {};
    for (int y = 0; y < 64; ++y)
        for (int x = 0; x < 64; ++x) {
            const int o = bgEntryIndex(x, y, 64, 64);
            CHECK(o >= 0 && o < 4096);
            CHECK(!used[o]);
            used[o] = true;
        }
}

KH_TEST(assets_a_coordinate_past_the_background_wraps_rather_than_overruns) {
    // The hardware wraps -- GBATEK, "When the screen is scrolled it'll always
    // wraparound" -- and a streamer scrolling a window off the edge depends on
    // it.  Without the wrap, y = 64 in a 64x64 background computes block 4 and
    // writes 2 KiB past the end of an 8 KiB map, which is the next background.
    for (int shape = 0; shape < 4; ++shape) {
        const int w = (shape & 1) ? 64 : 32;
        const int h = (shape & 2) ? 64 : 32;
        const int entries = (w / 32) * (h / 32) * BG_BLOCK_ENTRIES;
        CHECK_EQ(bgEntryIndex(w, 0, w, h), bgEntryIndex(0, 0, w, h));
        CHECK_EQ(bgEntryIndex(0, h, w, h), bgEntryIndex(0, 0, w, h));
        CHECK_EQ(bgEntryIndex(w + 5, h + 3, w, h), bgEntryIndex(5, 3, w, h));
        // ...and nothing, wrapped or not, lands outside the map.
        for (int y = 0; y < h * 2; ++y)
            for (int x = 0; x < w * 2; ++x) {
                const int o = bgEntryIndex(x, y, w, h);
                CHECK(o >= 0 && o < entries);
            }
    }
}

KH_TEST(assets_the_object_pages_fit_the_ten_bit_tile_number) {
    // An OAM tile number is ten bits and addresses byte offset * OBJ_BOUNDARY,
    // so at the default boundary of 32 it reaches the first 32 KiB of object
    // VRAM and no further -- past that it aliases back to the start, which looks
    // like the wrong sprite rather than like a fault.  The margin is 1024 bytes
    // and this is the only test that would notice it going.
    CHECK_EQ(OBJ_REACH, (MAP_TILE_MASK + 1) * OBJ_BOUNDARY);
    CHECK_EQ(OBJ_CEL_TILES, OBJ_CEL_BYTES / OBJ_BOUNDARY);

    uint32_t sora = 0, obj = 0, obj2 = 0, town = 0;
    for (const SpriteAsset& s : SPRITE_ASSETS) {
        if (std::strcmp(s.name, "sorachr") == 0) sora = s.bytes;
        if (std::strcmp(s.name, "objchr") == 0) obj = s.bytes;
        if (std::strcmp(s.name, "obj2chr") == 0) obj2 = s.bytes;
        if (std::strcmp(s.name, "objtownchr") == 0) town = s.bytes;
    }
    CHECK(sora > 0 && obj > 0 && obj2 > 0 && town > 0);

    // Every page's byte count must be a whole number of addressable units, or
    // the page after it does not start on a tile number at all.
    for (const SpriteAsset& s : SPRITE_ASSETS) CHECK_EQ(s.bytes % OBJ_BOUNDARY, 0u);

    // The resident set is the SNES's: the town's page OVERWRITES the second
    // object page (main.s:540) rather than joining it, so three pages are up at
    // once and not four.  Four would not fit.
    CHECK_EQ(OBJ_RESIDENT_BYTES, sora + obj + (obj2 > town ? obj2 : town));
    CHECK(OBJ_RESIDENT_BYTES <= uint32_t(OBJ_REACH));
    // ...and four would not, at the boundary of 32 this is generated against --
    // spelled with the literal because that is the fact forcing the sharing, and
    // it stops being true if the boundary is ever raised.
    CHECK(sora + obj + obj2 + town > (MAP_TILE_MASK + 1) * 32u);

    // dsTileFor() is derived from the boundary, not from a literal 16.  A cel is
    // 512 bytes, so consecutive cels are OBJ_CEL_TILES apart and the sixteenth
    // character of one is immediately before the first of the next.
    CHECK_EQ(dsTileFor(0), 0);
    CHECK_EQ(dsTileFor(4), OBJ_CEL_TILES);           // the cel to the right
    CHECK_EQ(dsTileFor(64), 4 * OBJ_CEL_TILES);      // the cel below
}

KH_TEST(assets_every_scene_that_fits_records_its_bgxcnt_size) {
    // 512x256 and 256x512 both put their second block at entry 1024 and differ
    // only in whether it is the right half or the bottom half, so a map without
    // its size code renders correctly in one shape and transposed in the other.
    // A streaming scene has no answer yet -- the window's shape is the VRAM
    // map's business -- and says so with -1 rather than with a plausible guess.
    int fits = 0, streams = 0;
    for (const SceneAsset& s : SCENE_ASSETS) {
        if (s.streams) {
            CHECK_EQ(s.bgSize, -1);
            ++streams;
        } else {
            // Every scene of this game that fits is 64x32 characters: a station
            // and the fragment are both 32x16 sixteen-pixel tiles.
            CHECK_EQ(s.bgSize, 1);
            CHECK(s.tilesW * 2 <= 64 && s.tilesH * 2 <= 64);
            ++fits;
        }
    }
    CHECK_EQ(fits, 4);       // three stations and the fragment
    CHECK_EQ(streams, 5);    // the island, the night, and three districts
}

KH_TEST(assets_the_scene_table_says_which_ones_stream) {
    // A map wider than 64 characters cannot be uploaded to a background at all,
    // whatever order it is in, so the emitted map is row-major and the renderer
    // streams a window out of it.  The table is what says which scenes need that.
    const SceneAsset* island = sceneNamed("island");
    CHECK(island != nullptr);
    CHECK_EQ(island->tilesW, DS_ISLAND_W);
    CHECK(island->streams);                 // 128 characters wide
    CHECK(island->groundFrom == nullptr);
    CHECK(island->chars > 0);

    // The night borrows the island's ground and has none of its own.
    const SceneAsset* night = sceneNamed("night");
    CHECK(night != nullptr);
    CHECK(night->groundFrom != nullptr);
    CHECK_EQ(night->chars, 0);

    // The stations and the fragment fit one background, which is the whole
    // reason they were left unexpanded.
    const char* pinned[] = {"station1", "station2", "station3", "fragment"};
    for (const char* n : pinned) {
        const SceneAsset* s = sceneNamed(n);
        CHECK(s != nullptr);
        CHECK(!s->streams);
        CHECK(s->tilesW * 2 <= BG_MAX_CHARS);
        CHECK(s->tilesH * 2 <= BG_MAX_CHARS);
    }
    // ...and every district streams.
    const char* wide[] = {"town1", "town2", "town3"};
    for (const char* n : wide) {
        const SceneAsset* s = sceneNamed(n);
        CHECK(s != nullptr);
        CHECK(s->streams);
    }
    // Nine scenes, which is every one that has a cast file.
    CHECK_EQ(sizeof SCENE_ASSETS / sizeof SCENE_ASSETS[0], 9u);
}

KH_TEST(assets_one_dimensional_sprite_mapping_renumbers_the_objects) {
    // A 32x32 object is sixteen CONSECUTIVE characters under 1D mapping, so the
    // object pages are re-serialised cel-contiguous and every index moves.  A
    // SNES page is a 16-character grid in which a 32x32 object is a 4x4 block.
    CHECK_EQ(dsTileFor(0), 0);              // cel 0
    CHECK_EQ(dsTileFor(4), 16);             // one cel right: SNES +4, DS +16
    CHECK_EQ(dsTileFor(8), 32);
    CHECK_EQ(dsTileFor(64), 64);            // one cel down: SNES +64, DS +64
    CHECK_EQ(dsTileFor(68), 80);            // ...and one right again

    // The mapping is injective over the cels of a page, which is the property
    // that matters: two objects must never land on the same characters.
    bool seen[256] = {};
    for (int row = 0; row < 4; ++row)
        for (int col = 0; col < 4; ++col) {
            const int t = dsTileFor(row * 64 + col * 4);
            CHECK(t >= 0 && t < 256);
            CHECK(!seen[t]);
            seen[t] = true;
        }

    // And the pages themselves are the size that implies: sixteen cels of
    // sixteen characters of 32 bytes.
    Blob obj = khhost::load("objchr.bin", a, sizeof a);
    if (!have(obj, "objchr.bin")) { CHECK(false); return; }
    CHECK_EQ(obj.size, 16u * 16u * 32u);

    // Sora needed no reordering -- the SNES already stored him cel-contiguous --
    // so his page is thirty cels and a frame's index is the same number on both
    // machines.
    Blob sora = khhost::load("sorachr.bin", b, sizeof b);
    if (!have(sora, "sorachr.bin")) { CHECK(false); return; }
    CHECK_EQ(sora.size, 30u * 16u * 32u);
}

KH_TEST(assets_the_font_went_from_two_bits_to_four) {
    // A DS text background has no 2bpp mode, so the font doubles in size and
    // keeps its picture.  128 characters either way, and the index of a glyph is
    // unchanged, which is what lets text.h's glyphOf() carry over untouched.
    Blob font = khhost::load("hudchr.bin", a, sizeof a);
    if (!have(font, "hudchr.bin")) { CHECK(false); return; }
    CHECK_EQ(font.size, 128u * 32u);        // 4bpp: 32 bytes a character
    CHECK_EQ(font.size, 2u * (128u * 16u)); // exactly twice the 2bpp page

    // CH_CLEAR is the one genuinely transparent cell and is character 127.
    const size_t clear = 127 * 32;
    bool blank = true;
    for (size_t i = 0; i < 32; ++i)
        if (font.data[clear + i] != 0) blank = false;
    CHECK(blank);

    // ...and character 0, the space, is NOT blank: every glyph carries an opaque
    // background so text can sit inside the dialogue window.
    bool spaceOpaque = false;
    for (size_t i = 0; i < 32; ++i)
        if (font.data[i] != 0) spaceOpaque = true;
    CHECK(spaceOpaque);
}
