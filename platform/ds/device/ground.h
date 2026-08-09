#pragma once
// §M7 step two: the 2D tilemap ground, and the streaming that makes it work.
//
// THE PROBLEM, in one sentence.  A DS text background addresses at most 64x64
// characters -- 512x512 pixels -- and the island is 128x64 characters wide.
// The map does not fit in the window, so the window is a sliding view of it, and
// the sliding is this file.
//
// WHY THE WINDOW IS A TORUS AND NOT A BUFFER.  The hardware wraps a 512x512
// background at 512 pixels in both axes, with no help and no cost: scroll past
// the right edge and column 0 reappears.  So the window is never "moved" -- what
// moves is which map column each window column HOLDS, and a scroll of one
// character means rewriting exactly one column, sixty-four entries, rather than
// blitting a screen.  That is the whole technique, it is what the SNES did with
// its own 64x32 window, and it is the reason the DS can scroll a 1024-pixel map
// through 8 KiB of map RAM.
//
// WHAT IS DECIDED HERE AND WHAT IS NOT.  Every decision -- which columns are
// stale, which map column each window column should hold, what entry goes in
// each slot, what the scroll registers get -- is arithmetic over data already in
// the tree, and the host suite checks all of it against the map files
// themselves.  What is NOT here is any claim that a DS displays the result.
// The window is written through a pointer that is VRAM on a device and an
// ordinary array in a test, which is the same seam device/mmio.h uses for
// registers and for the same reason: a stub of somebody else's header would be
// an unverifiable claim, and a pointer to memory is not a claim at all.

#include <cstddef>
#include <cstdint>

#include "grid.h"
#include "mmio.h"
#include "scene.h"

namespace kh::device {

// The background scroll registers.  GBATEK: 4000010h BG0HOFS, 4000012h BG0VOFS,
// and +4 per layer after that.  Write-only, 16-bit, and only the low nine bits
// are used -- which is exactly the 512 the window wraps at, so the masking below
// is the hardware's own arithmetic restated rather than a defensive trim.
constexpr uint32_t BGOFS_MAIN = 0x04000010;
constexpr uint32_t bgHOfs(uint32_t base, int layer) {
    return base + uint32_t(layer) * 4;
}
constexpr uint32_t bgVOfs(uint32_t base, int layer) {
    return base + uint32_t(layer) * 4 + 2;
}

// The window, in characters.  Not a free parameter: it is GROUND_MAP's 8 KiB
// divided by two bytes an entry, and vram_map.h froze that.  The assertion
// tying the two lives in ground.cpp where both are in scope.
constexpr int WINDOW_CHARS = 64;
constexpr int WINDOW_ENTRIES = WINDOW_CHARS * WINDOW_CHARS;
constexpr int WINDOW_PX = WINDOW_CHARS * 8;             // 512

// How many character columns and rows the screen can show at once.  The +1 is
// the partial column at each edge when the scroll is not a multiple of eight,
// and leaving it out is the classic one-column-of-garbage-at-the-right-edge bug.
constexpr int VISIBLE_COLS = SCREEN_W / 8 + 1;          // 33
constexpr int VISIBLE_ROWS = SCREEN_H / 8 + 1;          // 25

// A scene's character map: the 16-bit entries tools/build_assets.py emits into
// <scene>map.bin, ROW-MAJOR and in map order rather than in the hardware's block
// order (gen/assets.h says so, and bgEntryIndex() is the translation).
//
// Kept separate from SceneGround DELIBERATELY, and this is worth stating because
// the obvious move is to widen that class.  SceneGround carries collision and
// height, which is what the simulation needs and what the 3D backend needs; a
// character map is what the 2D backend needs and what the 3D backend would never
// look at.  Widening the shared type would hand every consumer a field one of
// them cannot use, and GroundRenderer::load() would start meaning different
// things to its two implementations.  So the common part stays in the virtual
// and the backend-specific part arrives here.
struct CharMap {
    Blob entries;           // 16-bit, row-major, wChars * hChars of them
    int wChars = 0;
    int hChars = 0;

    bool valid() const;
    // The entry at a character coordinate, or 0 off the map.  Off-map is a real
    // case and not a guard: a 96-character-wide town in a 64-wide window means
    // the columns past the map's edge are addressable, and they have to be
    // something.  Zero is the transparent character every scene's tile 0 is.
    uint16_t at(int cx, int cy) const;
};

// The 2D backend.  One of the two GroundRenderers vram_map.h keeps room for;
// the other is the 3D quad ground, and they share no VRAM, which is what makes
// switching a call rather than a bank remap.
class TilemapGround final : public GroundRenderer {
public:
    // `window` is GROUND_MAP: WINDOW_ENTRIES 16-bit entries.  On a device that
    // is vram::address(vram::GROUND_MAP, vram::Use::MainBg); in a test it is an
    // array, which is what makes every decision below checkable.
    TilemapGround(uint16_t* window, Mmio& io, int layer);

    // The map this scene draws with.  Called before load(); a CharMap that does
    // not match the SceneGround's tile extents is refused rather than drawn
    // half-right, because a map one row short does not look like a bug, it looks
    // like the artist forgot something.
    bool setMap(const CharMap& map);

    void load(const SceneGround& ground) override;
    void draw(const Camera& cam) override;

    // --- what a test asks, and what a bring-up would print ------------------
    bool ready() const { return ready_; }
    // Entries written by the most recent call.  A load is the whole window; a
    // draw is zero on a still frame and 64 per column that scrolled in.
    int lastWritten() const { return lastWritten_; }
    // The half-open range of map columns the window currently holds.
    int baseColumn() const { return base_; }
    // Why setMap() or load() refused, or nullptr.  A renderer that fails
    // silently draws the previous scene, which is the one failure mode that
    // looks deliberate.
    const char* error() const { return error_; }

private:
    void writeColumn(int mapCol);
    void fill();

    uint16_t* window_;
    Mmio* io_;
    int layer_;
    CharMap map_{};
    int base_ = 0;              // window holds map columns [base_, base_+64)
    int lastWritten_ = 0;
    bool ready_ = false;
    const char* error_ = nullptr;
};

}  // namespace kh::device
