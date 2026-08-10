// The 2D tilemap ground and its streamer.

#include "ground.h"

#include "constants.h"
#include "gen/assets.h"
#include "vram_map.h"

namespace kh::device {
namespace {

using namespace kh::vram;

// WINDOW_CHARS is not a choice this file gets to make.  GROUND_MAP is 8 KiB of
// map RAM and an entry is two bytes, so the window is 4096 entries, and a
// square background of 4096 entries is 64x64.  Asserted rather than commented,
// because the alternative is a streamer that wraps at a different modulus from
// the hardware -- which does not fail, it tears one column.
static_assert(WINDOW_ENTRIES * 2 == int(GROUND_MAP.bytes),
              "the window and vram_map.h's GROUND_MAP reservation disagree "
              "about how many entries there are");
static_assert(WINDOW_CHARS == BG_MAX_CHARS,
              "gen/assets.h's BG_MAX_CHARS is what bgEntryIndex() lays out "
              "against; a different window here would index a different shape");
static_assert(VISIBLE_COLS < WINDOW_CHARS,
              "the screen must be narrower than the window, or a scroll would "
              "need a column to be in two places at once");
static_assert(VISIBLE_ROWS <= WINDOW_CHARS, "same, vertically");

// A tile is TILE_PX across and a character is eight pixels, so a map's extent in
// characters is its extent in tiles times this.  Derived, because the day
// TILE_PX changes is the day every one of these turns into an off-by-double.
constexpr int CHARS_PER_TILE = TILE_PX / 8;
static_assert(CHARS_PER_TILE * 8 == TILE_PX, "a tile is a whole number of characters");

// Positive modulus.  C++'s % keeps the sign of the dividend, and a camera can
// legitimately sit at a negative scroll on a pinned scene, so `-1 % 64` being
// -1 rather than 63 would index outside the window.  This is the single most
// likely way to write a streamer that works until the camera reaches an edge.
constexpr int wrap(int v, int m) { return ((v % m) + m) % m; }

}  // namespace

bool CharMap::valid() const {
    return !entries.empty() && wChars > 0 && hChars > 0
           && entries.size >= size_t(wChars) * size_t(hChars) * 2;
}

uint16_t CharMap::at(int cx, int cy) const {
    if (cx < 0 || cy < 0 || cx >= wChars || cy >= hChars) return 0;
    const size_t i = (size_t(cy) * size_t(wChars) + size_t(cx)) * 2;
    // Little-endian by hand rather than a uint16_t* cast: the blob comes off
    // disk on the host and out of a linked .bin on the device, and neither is
    // guaranteed two-byte aligned.  An unaligned halfword load on ARMv5TE does
    // not fault -- it silently returns a ROTATED word, which is the kind of
    // wrong that survives a code review.
    return uint16_t(entries.data[i] | (uint16_t(entries.data[i + 1]) << 8));
}

TilemapGround::TilemapGround(uint16_t* window, Mmio& io, int layer)
    : window_(window), io_(&io), layer_(layer) {}

bool TilemapGround::setMap(const CharMap& map) {
    ready_ = false;
    error_ = nullptr;
    if (!map.valid()) {
        error_ = "the character map is empty or shorter than its own extents";
        return false;
    }
    // THE ONE THING THAT CANNOT BE STREAMED AROUND.  Horizontal overflow is
    // what this class exists to handle; vertical overflow is not, because the
    // window is as tall as the tallest map anyone has authored and the streamer
    // has no row logic at all.  A 65-character-tall map would wrap onto itself
    // and draw its own top rows underneath its bottom ones -- a seam a third of
    // the way up the screen that looks like corrupt map data.
    //
    // So it is refused HERE, at load time, with the reason, rather than being
    // discovered by eye.  Adding row streaming is a real piece of work and this
    // is the message that tells whoever needs it that they have to do it.
    if (map.hChars > WINDOW_CHARS) {
        error_ = "the map is taller than the 64-character window and this "
                 "streamer only slides horizontally; it would wrap onto itself";
        return false;
    }
    map_ = map;
    ready_ = true;
    return true;
}

void TilemapGround::writeColumn(int mapCol) {
    // VOLATILE, BECAUSE THE DESTINATION IS VRAM AND NOTHING EVER READS IT BACK.
    //
    // This is the one place in the port that writes video memory through a
    // plain pointer: every other path is either a dmaCopy or a volatile store
    // through device/mmio.h.  A compiler is entitled to notice that four
    // thousand stores into a buffer nobody loads from are dead, and the display
    // controller is not a reader it knows about.  Whether it actually does is
    // beside the point -- VRAM is a device, and a device is written with
    // volatile.
    //
    // On the host this changes nothing: the tests point `window` at an ordinary
    // array and read it back, which a volatile store serves exactly as well.
    volatile uint16_t* const vram = window_;
    const int w = wrap(mapCol, WINDOW_CHARS);
    for (int y = 0; y < WINDOW_CHARS; ++y)
        vram[bgEntryIndex(w, y, WINDOW_CHARS, WINDOW_CHARS)] = map_.at(mapCol, y);
    lastWritten_ += WINDOW_CHARS;
}

void TilemapGround::fill() {
    lastWritten_ = 0;
    for (int c = base_; c < base_ + WINDOW_CHARS; ++c) writeColumn(c);
}

void TilemapGround::load(const SceneGround& ground) {
    if (!ready_) {
        error_ = error_ ? error_ : "load() with no character map; call setMap first";
        return;
    }
    // The two halves of a scene have to agree.  SceneGround's extents are in
    // TILES and drive collision; the CharMap's are in CHARACTERS and drive what
    // is drawn.  A mismatch means the thing being walked on and the thing being
    // looked at are different maps -- Sora stopping at a wall that is not there,
    // which reads as a collision bug in code that is correct.
    if (ground.valid()
        && (ground.width() * CHARS_PER_TILE != map_.wChars
            || ground.height() * CHARS_PER_TILE != map_.hChars)) {
        error_ = "the collision map and the character map are different sizes; "
                 "what is walked on and what is drawn would not be the same map";
        ready_ = false;
        return;
    }
    base_ = 0;
    fill();
}

void TilemapGround::draw(const Camera& cam) {
    lastWritten_ = 0;
    if (!ready_) return;

    // WHICH COLUMNS THE SCREEN NEEDS.  cam.x is whole pixels and a character is
    // eight of them; an arithmetic shift would be wrong for a negative camera
    // and a division truncates towards zero, so the floor is taken explicitly.
    const int first = (cam.x >= 0 ? cam.x : cam.x - 7) / 8;
    const int last = first + VISIBLE_COLS - 1;

    // ...and slide the window until it contains them.  The window holds a
    // 64-column run and the screen needs 33, so there is 31 columns of slack --
    // which is why an ordinary frame writes NOTHING and a fast scroll writes one
    // column per eight pixels rather than a screenful.
    //
    // The loops are `while` and not `if` on purpose.  A scene load, a door, or
    // the Dive's Shatter can move the camera further in one frame than the slack
    // absorbs, and an `if` would slide by one column and leave the rest of the
    // window holding another part of the map -- visibly, and only on the frames
    // that jump.
    while (first < base_) {
        --base_;
        writeColumn(base_);                 // the column entering on the left
    }
    while (last > base_ + WINDOW_CHARS - 1) {
        writeColumn(base_ + WINDOW_CHARS);  // ...and on the right
        ++base_;
    }

    // A jump so large that nothing in the window survives is a refill, not a
    // slide.  The loops above already produce the right ANSWER in that case --
    // they would just walk the whole distance one column at a time, writing
    // columns nothing will ever see.  Catching it keeps the cost bounded by the
    // window rather than by how far the camera moved.
    if (lastWritten_ > WINDOW_ENTRIES) {
        base_ = first;
        fill();
    }

    // THE SCROLL REGISTERS, and the wrap that makes the torus work.  The window
    // repeats every 512 pixels, so the register wants the camera's position
    // modulo 512 -- the same modulus writeColumn() uses, which is what keeps the
    // pixel offset and the column contents describing the same place.
    //
    // cam.bgHOfs, not cam.x: the two differ by shakeX, which is the Dive's
    // Shatter and the night's Tear jostling the frame.  The SNES wrote the
    // shaken value to its scroll register and the unshaken one drove nothing,
    // and BEHAVIOUR.md's camera section keeps that split.  Writing cam.x here
    // would produce a world that does not shake, which is a divergence nothing
    // in the trace would catch -- bgHOfs IS the trace column.
    io_->write16(bgHOfs(BGOFS_MAIN, layer_), uint16_t(wrap(cam.bgHOfs, WINDOW_PX)));
    io_->write16(bgVOfs(BGOFS_MAIN, layer_), uint16_t(wrap(cam.bgVOfs, WINDOW_PX)));
}

}  // namespace kh::device
