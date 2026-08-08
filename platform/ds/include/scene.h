#pragma once
// Scenes: the ground a scene stands on, and the cast that stands on it.
//
// The cast tables are DATA, emitted by tools/build_assets.py from
// assets/ds/<scene>_cast.txt into assets/gen/ds/.  Nothing here transcribes a
// spawn table from the assembly, and nothing here decides where a palm goes --
// prop actors are derived from the map by the pipeline and arrive already in the
// table.  See docs/WORLD_SIZES.md.
//
// This is deliberately NOT the asset backend (§M2).  A Blob is a pointer and a
// length; where those come from is the loader's business, and the two answers
// are "read the file" on the host and "link the .bin in" on the device.  Keeping
// the seam this thin is what lets §M5 be written and tested before §M2 exists.

#include <cstddef>
#include <cstdint>

#include "actor.h"
#include "constants.h"

namespace kh {

struct Blob {
    const uint8_t* data = nullptr;
    size_t size = 0;

    constexpr bool empty() const { return data == nullptr || size == 0; }
};

// A cast row is (type, i, j, variant); the table ends with a single 0xFF.
constexpr uint8_t CAST_END = 0xFF;
constexpr size_t CAST_STRIDE = 4;

// A Heartless spot is (i, j), same terminator.
constexpr size_t SPOT_STRIDE = 2;

// A door is (i, j, land_i, land_j).  Which district it leads to is scene logic
// and the table deliberately does not say -- see the porting brief, §M5.
constexpr size_t DOOR_STRIDE = 4;

struct Tile {
    uint8_t i = 0;
    uint8_t j = 0;
};

struct Door {
    Tile at;
    Tile landing;
};

// The ground.  Collision and height are w*h byte arrays, straight out of the
// pipeline; the extents come with them because map size is per-scene now and the
// camera's clamp reads it (see constants.h).
class SceneGround {
public:
    void set(Blob collision, Blob height, int w, int h);

    int width() const { return w_; }
    int height() const { return h_; }
    bool valid() const { return coll_ != nullptr && hmap_ != nullptr; }

    bool inBounds(int i, int j) const {
        return i >= 0 && j >= 0 && i < w_ && j < h_;
    }
    // Off the map is not walkable, which is what makes the rim work without a
    // border of blocking tiles.  BEHAVIOUR.md §1.
    bool walkable(int i, int j) const {
        return inBounds(i, j) && coll_[j * w_ + i] != 0;
    }
    uint8_t heightAt(int i, int j) const {
        return inBounds(i, j) ? hmap_[j * w_ + i] : 0;
    }

private:
    const uint8_t* coll_ = nullptr;
    const uint8_t* hmap_ = nullptr;
    int w_ = 0;
    int h_ = 0;
};

// How a table load ended.  A partial load is not a warning to be printed and
// forgotten: SpawnTable ignored the SNES's clear-carry and silently dropped
// whichever entry overflowed, which is how the bottle under the waterfall went
// missing for a whole day.
struct SpawnResult {
    int spawned = 0;
    int refused = 0;            // rows the pool had no room for
    bool malformed = false;     // ran off the end with no terminator

    constexpr bool complete() const { return refused == 0 && !malformed; }
};

// Walk a cast table and spawn every row.  Positions are tile coordinates and
// become world centres, exactly as TileToWorld then a shift by 4 did.
SpawnResult spawnCast(Actors& actors, Blob table, const SceneGround& ground,
                      uint8_t* variantOut = nullptr);

// Read a spot or door table.  Both return how many entries were stored, capped
// at `cap`; a table longer than the buffer is malformed data, not a resize.
int readSpots(Blob table, Tile* out, int cap);
int readDoors(Blob table, Door* out, int cap);

}  // namespace kh
