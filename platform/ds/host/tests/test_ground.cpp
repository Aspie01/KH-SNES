// The 2D ground streamer, driven against the real emitted maps.
//
// SAME CAVEAT AS test_device_init.cpp, and it is worth repeating because this
// file is where it is easiest to forget: nothing here has executed an ARM
// instruction and no DS has displayed any of this.  What is checked is the
// STREAMER'S DECISIONS -- which window column holds which map column, what entry
// goes in each slot, how much is rewritten on a given camera move, and what the
// scroll registers are told.  All of that is arithmetic over data in the tree,
// and all of it is wrong-able in ways a compiler cannot see.
//
// What makes these cases worth more than a synthetic fixture is that the map is
// the REAL one: assets/gen/ds/islandmap.bin, 128 characters wide, which is the
// map that forced this class to exist.  A streamer tested against a made-up
// 70-column grid would agree with itself.

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "check.h"
#include "constants.h"
#include "gen/assets.h"
#include "ground.h"
#include "hostblob.h"
#include "mmio.h"
#include "vram_map.h"

using namespace kh;
using namespace kh::device;
using khhost::load;

namespace {

// The window a device would have in VRAM.  An ordinary array here, which is the
// whole seam: every entry the streamer writes is readable afterwards.
uint16_t g_window[WINDOW_ENTRIES];

// Poisoned rather than zeroed before each case.  Zero is a legal entry -- it is
// the transparent character -- so a window left zeroed cannot distinguish "the
// streamer wrote the correct 0 here" from "the streamer never touched this".
// 0xDEAD is neither a legal character index nor a plausible accident.
constexpr uint16_t POISON = 0xDEAD;
void poison() {
    for (int i = 0; i < WINDOW_ENTRIES; ++i) g_window[i] = POISON;
}

// The island's character map, off disk -- the same bytes a device would link
// as a symbol.  128 x 64 entries of two bytes is 16 KiB; the buffer is file
// scope because a 16 KiB automatic would be a stack overflow on the machine
// this code is actually for, and a test that only fits on the host is a test
// that has quietly stopped describing the device.
unsigned char g_mapBuf[128 * 64 * 2];
unsigned char g_collBuf[64 * 32];
unsigned char g_hgtBuf[64 * 32];
unsigned char g_coll2Buf[48 * 32];
unsigned char g_hgt2Buf[48 * 32];

struct Island {
    CharMap map;
    Island() {
        map.entries = load("islandmap.bin", g_mapBuf, sizeof g_mapBuf);
        map.wChars = 64 * 2;        // SCENE_ASSETS: island is 64x32 tiles
        map.hChars = 32 * 2;
    }
};

// What the window SHOULD hold, computed independently of the streamer: window
// column w holds map column m when m == w (mod 64) and m is in the loaded run.
uint16_t expected(const CharMap& m, int mapCol, int y) { return m.at(mapCol, y); }

// Does the refusal message say the right thing -- WITHOUT dereferencing a null
// one.  CHECK does not abort, so `CHECK(e != nullptr)` followed by
// `strstr(e, ...)` runs the strstr anyway on the failure path, and the suite
// segfaults instead of reporting.  Found by breaking the height rule: the case
// that exists to catch it produced exit 139 and not one line of output, which
// is the worst possible behaviour from a test and the exact failure mode this
// project keeps hunting -- a check that goes quiet precisely when it fires.
bool saysWhy(const char* err, const char* want) {
    return err != nullptr && std::strstr(err, want) != nullptr;
}

Camera at(int x, int y = 0) {
    Camera c;
    c.x = x;
    c.y = y;
    c.bgHOfs = x;
    c.bgVOfs = y;
    return c;
}

}  // namespace

KH_TEST(ground_the_window_is_exactly_what_vram_map_reserved) {
    // Not a tautology: WINDOW_CHARS is used as a modulus in three places, and a
    // window that disagreed with GROUND_MAP would wrap at a different pixel from
    // the hardware -- which tears one column rather than failing.
    CHECK_EQ(WINDOW_ENTRIES * 2, int(vram::GROUND_MAP.bytes));
    CHECK_EQ(WINDOW_CHARS, BG_MAX_CHARS);
    CHECK_EQ(WINDOW_PX, 512);
    CHECK_EQ(VISIBLE_COLS, 33);         // 256/8 + the partial column
    CHECK_EQ(VISIBLE_ROWS, 25);
    // The slack that makes an ordinary frame free.
    CHECK(WINDOW_CHARS - VISIBLE_COLS >= 31);
}

KH_TEST(ground_a_load_fills_the_window_from_the_real_island_map) {
    Island isl;
    CHECK(isl.map.valid());
    CHECK_EQ(isl.map.wChars, 128);
    CHECK_EQ(isl.map.hChars, 64);

    poison();
    Mmio io;
    TilemapGround g(g_window, io, int(vram::MAIN_GROUND_LAYER));
    CHECK(g.setMap(isl.map));
    SceneGround ground;                 // no collision loaded: extents unchecked
    g.load(ground);
    CHECK(g.error() == nullptr);
    CHECK_EQ(g.baseColumn(), 0);
    CHECK_EQ(g.lastWritten(), WINDOW_ENTRIES);

    // EVERY entry, against the map file.  Not a sample -- the block layout is
    // the thing most likely to be wrong, and it is wrong in quadrants, so three
    // of four spot checks can pass while a quarter of the screen is garbage.
    int poisoned = 0;
    for (int w = 0; w < WINDOW_CHARS; ++w)
        for (int y = 0; y < WINDOW_CHARS; ++y) {
            const uint16_t got = g_window[bgEntryIndex(w, y, WINDOW_CHARS, WINDOW_CHARS)];
            if (got == POISON) ++poisoned;
            CHECK_EQ(got, expected(isl.map, w, y));
        }
    CHECK_EQ(poisoned, 0);
}

KH_TEST(ground_a_still_camera_writes_nothing_at_all) {
    // The point of the technique.  A frame that does not cross a character
    // boundary must cost two register writes and no map traffic; a streamer that
    // rewrote the visible columns every frame would work, look identical, and
    // spend 33 columns of DMA a frame forever.
    Island isl;
    Mmio io;
    TilemapGround g(g_window, io, int(vram::MAIN_GROUND_LAYER));
    CHECK(g.setMap(isl.map));
    SceneGround ground;
    g.load(ground);

    g.draw(at(0));
    CHECK_EQ(g.lastWritten(), 0);
    for (int x = 0; x < 8 * 20; ++x) {          // twenty characters of travel
        g.draw(at(x));
    }
    // Still inside the loaded run, so still nothing: the window holds columns
    // 0..63 and the screen has only reached column 19 + 33.
    CHECK_EQ(g.lastWritten(), 0);
    CHECK_EQ(g.baseColumn(), 0);
}

KH_TEST(ground_scrolling_right_streams_one_column_at_a_time) {
    Island isl;
    Mmio io;
    TilemapGround g(g_window, io, int(vram::MAIN_GROUND_LAYER));
    CHECK(g.setMap(isl.map));
    SceneGround ground;
    g.load(ground);

    // Walk east one pixel a frame across the whole island and check the window
    // after every single frame.  This is the case that catches a wrap done with
    // the wrong modulus, a column written to the right index with the wrong
    // data, and an off-by-one in which column is "entering".
    const int maxCam = isl.map.wChars * 8 - SCREEN_W;
    int columnsWritten = 0;
    for (int x = 0; x <= maxCam; ++x) {
        g.draw(at(x));
        columnsWritten += g.lastWritten() / WINDOW_CHARS;

        // Every column the screen can see holds the map column it should.
        const int first = x / 8;
        for (int c = first; c < first + VISIBLE_COLS; ++c) {
            const int w = c % WINDOW_CHARS;
            for (int y = 0; y < 4; ++y) {       // four rows is enough per frame
                const int yy = y * 16;
                CHECK_EQ(g_window[bgEntryIndex(w, yy, WINDOW_CHARS, WINDOW_CHARS)],
                         expected(isl.map, c, yy));
            }
        }
    }

    // The island is 128 characters wide and the window starts holding 0..63, so
    // reaching the east edge streams in columns 64.. up to the last visible one.
    // Stated as a number because "it streamed" is not the claim -- "it streamed
    // exactly what it had to" is.
    const int lastVisible = maxCam / 8 + VISIBLE_COLS - 1;
    CHECK_EQ(columnsWritten, lastVisible - (WINDOW_CHARS - 1));
    CHECK_EQ(g.baseColumn(), lastVisible - WINDOW_CHARS + 1);
}

KH_TEST(ground_scrolling_back_west_streams_the_other_way) {
    // The left-hand loop is a separate branch and a streamer that only ever
    // scrolled right would pass every case above.  Walking back is also what a
    // player does constantly.
    Island isl;
    Mmio io;
    TilemapGround g(g_window, io, int(vram::MAIN_GROUND_LAYER));
    CHECK(g.setMap(isl.map));
    SceneGround ground;
    g.load(ground);

    const int maxCam = isl.map.wChars * 8 - SCREEN_W;
    for (int x = 0; x <= maxCam; ++x) g.draw(at(x));
    const int easternBase = g.baseColumn();
    CHECK(easternBase > 0);

    for (int x = maxCam; x >= 0; --x) {
        g.draw(at(x));
        const int first = x / 8;
        for (int c = first; c < first + VISIBLE_COLS; ++c) {
            const int w = c % WINDOW_CHARS;
            CHECK_EQ(g_window[bgEntryIndex(w, 32, WINDOW_CHARS, WINDOW_CHARS)],
                     expected(isl.map, c, 32));
        }
    }
    CHECK_EQ(g.baseColumn(), 0);
}

KH_TEST(ground_a_jump_further_than_the_window_refills_instead_of_walking) {
    // A door, a scene load, or the Shatter can move the camera further in one
    // frame than the window holds.  Sliding one column at a time would still be
    // CORRECT -- and would write hundreds of columns nobody will see.  The
    // refill path bounds the cost at one window.
    Island isl;
    Mmio io;
    TilemapGround g(g_window, io, int(vram::MAIN_GROUND_LAYER));
    CHECK(g.setMap(isl.map));
    SceneGround ground;
    g.load(ground);
    CHECK_EQ(g.baseColumn(), 0);

    g.draw(at(isl.map.wChars * 8 - SCREEN_W));      // the far east, in one frame
    CHECK(g.lastWritten() <= WINDOW_ENTRIES);
    const int first = (isl.map.wChars * 8 - SCREEN_W) / 8;
    for (int c = first; c < first + VISIBLE_COLS; ++c) {
        const int w = c % WINDOW_CHARS;
        CHECK_EQ(g_window[bgEntryIndex(w, 8, WINDOW_CHARS, WINDOW_CHARS)],
                 expected(isl.map, c, 8));
    }
}

KH_TEST(ground_the_scroll_registers_get_the_shaken_offset_wrapped) {
    Island isl;
    Mmio io;
    TilemapGround g(g_window, io, int(vram::MAIN_GROUND_LAYER));
    CHECK(g.setMap(isl.map));
    SceneGround ground;
    g.load(ground);

    const int layer = int(vram::MAIN_GROUND_LAYER);
    mmioLogClear();
    Camera c = at(600, 40);
    c.bgHOfs = 600 + 3;                 // shakeX, as the Shatter and the Tear add
    g.draw(c);
    // Modulo 512, because that is where the window repeats -- the same modulus
    // the column contents use, which is what keeps offset and content agreeing.
    CHECK_EQ(mmioLast(bgHOfs(BGOFS_MAIN, layer)), int64_t(603 % 512));
    CHECK_EQ(mmioLast(bgVOfs(BGOFS_MAIN, layer)), int64_t(40));

    // bgHOfs and NOT cam.x.  They differ by the shake, the SNES wrote the shaken
    // one, and bgHOfs is a trace column -- so a streamer using cam.x would drop
    // the shake and nothing in the oracle diff would see it, because the diff
    // compares the simulation's bgHOfs and never the register.
    CHECK(c.bgHOfs != c.x);
    CHECK_EQ(mmioLast(bgHOfs(BGOFS_MAIN, layer)), int64_t(c.bgHOfs % 512));
}

KH_TEST(ground_a_negative_scroll_wraps_up_and_not_off_the_end) {
    // C++'s % keeps the dividend's sign, so -1 % 64 is -1 and indexes before the
    // window.  A pinned scene sits at a fixed camera and a clamp can put it
    // negative, and this is the single likeliest way to write a streamer that is
    // perfect until something reaches an edge.
    Island isl;
    Mmio io;
    TilemapGround g(g_window, io, int(vram::MAIN_GROUND_LAYER));
    CHECK(g.setMap(isl.map));
    SceneGround ground;
    g.load(ground);

    mmioLogClear();
    Camera c = at(-16, -8);
    c.bgHOfs = -16;
    c.bgVOfs = -8;
    g.draw(c);
    CHECK_EQ(mmioLast(bgHOfs(BGOFS_MAIN, int(vram::MAIN_GROUND_LAYER))),
             int64_t(WINDOW_PX - 16));
    CHECK_EQ(mmioLast(bgVOfs(BGOFS_MAIN, int(vram::MAIN_GROUND_LAYER))),
             int64_t(WINDOW_PX - 8));
    CHECK_EQ(g.baseColumn(), -2);
    // ...and the entries went somewhere inside the window rather than before it.
    for (int y = 0; y < WINDOW_CHARS; ++y)
        CHECK_EQ(g_window[bgEntryIndex(WINDOW_CHARS - 1, y, WINDOW_CHARS, WINDOW_CHARS)],
                 expected(isl.map, -1, y));
}

KH_TEST(ground_refuses_a_map_taller_than_the_window) {
    // The one overflow this class does NOT handle, refused loudly at load rather
    // than discovered as a seam a third of the way up the screen.  There is no
    // row logic here at all: a 65-character map would wrap onto itself.
    Island isl;
    Mmio io;
    TilemapGround g(g_window, io, int(vram::MAIN_GROUND_LAYER));

    // NARROWER as well as taller, so the blob still holds wChars * hChars
    // entries and the SIZE check passes -- otherwise this rejects for being
    // truncated and never reaches the height rule at all.  That is not a
    // contrivance, it is the case being isolated: a genuinely tall map would be
    // authored with enough bytes in it.  (The first draft of this case got the
    // wrong rejection and only the message assertion noticed, which is the
    // argument for asserting on messages rather than on `false`.)
    CharMap tall = isl.map;
    tall.wChars = WINDOW_CHARS;
    tall.hChars = WINDOW_CHARS + 1;
    CHECK(tall.valid());
    CHECK(!g.setMap(tall));
    CHECK(!g.ready());
    CHECK(saysWhy(g.error(), "taller"));

    // ...and a truncated map is a different complaint, so the two rejections
    // cannot be confused for one another.
    CharMap short_ = isl.map;
    short_.entries.size = 16;
    CHECK(!g.setMap(short_));
    CHECK(saysWhy(g.error(), "shorter"));

    // ...and exactly 64 is fine, which is the case every DS map actually is.
    CharMap exact = isl.map;
    exact.hChars = WINDOW_CHARS;
    CHECK(g.setMap(exact));
    CHECK(g.ready());
}

KH_TEST(ground_refuses_a_collision_map_that_is_a_different_size) {
    // What is walked on and what is drawn have to be the same map.  A mismatch
    // is Sora stopping at a wall that is not on screen -- which reads as a
    // collision bug in code that is correct, and is the most expensive kind of
    // wrong to chase.
    Island isl;
    Mmio io;
    TilemapGround g(g_window, io, int(vram::MAIN_GROUND_LAYER));
    CHECK(g.setMap(isl.map));

    SceneGround wrong;
    wrong.set(load("town1coll.bin", g_coll2Buf, sizeof g_coll2Buf),
              load("town1height.bin", g_hgt2Buf, sizeof g_hgt2Buf),
              48, 32);                              // the town, against the island
    g.load(wrong);
    CHECK(g.error() != nullptr);
    CHECK(!g.ready());

    // The right one loads.
    SceneGround right;
    right.set(load("islandcoll.bin", g_collBuf, sizeof g_collBuf),
              load("islandheight.bin", g_hgtBuf, sizeof g_hgtBuf), 64, 32);
    TilemapGround g2(g_window, io, int(vram::MAIN_GROUND_LAYER));
    CHECK(g2.setMap(isl.map));
    g2.load(right);
    CHECK(g2.error() == nullptr);
    CHECK_EQ(g2.lastWritten(), WINDOW_ENTRIES);
}

KH_TEST(ground_every_streaming_scene_in_the_pipeline_actually_fits) {
    // The pipeline says which scenes stream (SceneAsset::streams).  This checks
    // the streamer can carry every one of them -- so a map authored later that
    // this class cannot handle fails here rather than on a screen.
    Mmio io;
    int streaming = 0, fixed = 0;
    for (const SceneAsset& s : SCENE_ASSETS) {
        const int wc = int(s.tilesW) * 2;
        const int hc = int(s.tilesH) * 2;
        // The claim the pipeline makes: streams exactly when it does not fit.
        CHECK_EQ(s.streams, wc > WINDOW_CHARS || hc > WINDOW_CHARS);
        if (s.streams) ++streaming; else ++fixed;

        // ...and the claim this class makes: vertical always fits, so the
        // horizontal slide is the whole job.
        CHECK(hc <= WINDOW_CHARS);

        // A scene that borrows another's ground has no map of its own.
        if (s.chars == 0) { CHECK(s.groundFrom != nullptr); continue; }
        char file[64];
        std::snprintf(file, sizeof file, "%smap.bin", s.name);
        CharMap m{load(file, g_mapBuf, sizeof g_mapBuf), wc, hc};
        CHECK(m.valid());
        TilemapGround g(g_window, io, int(vram::MAIN_GROUND_LAYER));
        CHECK(g.setMap(m));
        CHECK(g.error() == nullptr);
    }
    CHECK_EQ(streaming, 5);         // island, night, town1, town2, town3
    CHECK_EQ(fixed, 5);             // three stations, fragment, Secret Place
}
