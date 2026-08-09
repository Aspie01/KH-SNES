#pragma once
// §M7 step six: the 3D quad ground, and the measurement the brief asked for.
//
// THE BRIEF IS UNUSUALLY SPECIFIC ABOUT WHAT THIS TASK IS, and it is worth
// quoting because it is also the answer to "how much of this can be done with
// no hardware":
//
//   "the limits are 2048 polygons and 6144 vertices per frame. A quad is one
//   polygon, so a whole 64x64 ground is 4096 quads -- 2x over. But the visible
//   window at 256x192 is ~192 tiles, ~400 with tilt and zoom margin, which is
//   comfortably inside. FRUSTUM CULLING IS THEREFORE THE LOAD-BEARING PIECE AND
//   COPLANAR TILE MERGING IS AN OPTIMISATION. Build the visible-set walk,
//   measure, and only merge if the measurement asks for it."
//
// So the deliverable is a visible-set walk and a MEASUREMENT, and a measurement
// is arithmetic over map files. That is the whole of what a screen would tell
// you about the budget -- the numbers are the numbers -- and it is the part of
// this milestone that most deserved doing before anybody could see it, because
// "2x over" is the kind of thing you want to know before building a renderer
// around it.
//
// WHAT IS HERE. The visible set, the quad list it produces, the vertex and
// polygon accounting against both hardware limits, and the measurement over
// every shipped map at every camera position it can reach.
//
// WHAT IS NOT. The GL calls. glBegin/glVertex3v16/glTexCoord2t16 are libnds and
// the geometry engine, and they are a thin emission over the list this
// produces -- the same shape as the OAM packing, where deciding what to draw is
// the part with content and writing it out is not. `Quad` is the boundary.
//
// AND ONE THING THAT IS A REFUSAL RATHER THAN A GAP. See mosaic below.

#include <cstdint>

#include "grid.h"
#include "ground.h"
#include "scene.h"
#include "stage.h"

namespace kh::device {

// GBATEK, "DS 3D Video": the geometry engine takes at most 2048 polygons and
// 6144 vertices in a frame. Both, not either: a quad costs one polygon and four
// vertices, so vertices bind first at 1536 quads and the polygon limit is
// unreachable by quads alone.
constexpr int POLY_MAX = 2048;
constexpr int VERT_MAX = 6144;
constexpr int VERTS_PER_QUAD = 4;
constexpr int QUAD_BUDGET = VERT_MAX / VERTS_PER_QUAD;      // 1536
static_assert(QUAD_BUDGET < POLY_MAX,
              "vertices bind before polygons for a quad ground, so the budget "
              "to reason about is the vertex one -- and a change that made the "
              "polygon limit bind first would invalidate every count below");

// One tile of ground, as the geometry engine will see it.
//
// The corners are implied: a tile is axis-aligned and TILE_PX across, so a
// quad is fully described by its north-west corner and the four heights. Four
// and not one, because a tile whose neighbours are at different heights has to
// be a ramp or the world is a staircase of floating slabs -- and the height map
// is per tile, so the corner heights come from the four tiles that meet there.
struct Quad {
    int16_t tx, ty;         // tile coordinates
    uint8_t h[4];           // corner heights, NW NE SW SE, in height steps
    uint16_t chr;           // the map entry, for the texture lookup
};

struct Ground3dBuild {
    int quads = 0;          // emitted
    int vertices = 0;       // quads * 4
    int considered = 0;     // tiles the walk looked at
    int culled = 0;         // outside the frustum
    int dropped = 0;        // wanted a quad, the budget was spent
    bool overBudget() const { return dropped > 0; }
};

// The visible set, as a rectangle of tiles.
//
// A rectangle and not a true frustum, and that is a decision worth defending:
// the camera here is the SNES's -- an axis-aligned window that scrolls and
// never rotates (grid.h's Camera has an x and a y and nothing else). A
// perspective frustum would be the right answer for a camera that tilts, and
// the moment one exists this is what has to change; until then a rotation test
// would be culling against a rotation nobody can express.
//
// `margin` is the tilt and zoom allowance the brief calls for: extra rings of
// tiles beyond the screen, so that turning the camera does not reveal an edge
// where the world stops. It is a parameter because its right value is the one
// thing here a screen really would tell you.
struct Frustum {
    int loX, loY, hiX, hiY;     // inclusive tile bounds
};

Frustum visibleTiles(const Camera& cam, int mapW, int mapH, int margin);

// Walk the visible set and build the quad list.
//
// Tiles with no ground are skipped rather than emitted flat: a hole in the map
// is a hole, and a quad at height zero over it would be a floor the player
// falls through in the collision map and stands on in the picture.
Ground3dBuild buildQuads(const SceneGround& g, const CharMap& map,
                         const Camera& cam, int margin, Quad* out, int cap);

// The 3D backend. The other GroundRenderer -- vram_map.h keeps room for both and
// they share no VRAM, which is what makes switching a call and not a remap.
class QuadGround final : public GroundRenderer {
public:
    QuadGround(Quad* quads, int cap, int margin);

    bool setMap(const CharMap& map);
    void load(const SceneGround& ground) override;
    void draw(const Camera& cam) override;

    const Ground3dBuild& lastBuild() const { return build_; }
    const Quad* quads() const { return quads_; }
    const char* error() const { return error_; }

private:
    Quad* quads_;
    int cap_;
    int margin_;
    const SceneGround* ground_ = nullptr;
    CharMap map_{};
    Ground3dBuild build_{};
    const char* error_ = nullptr;
};

// MOSAIC IS A REFUSAL AND NOT A GAP.
//
// docs/behaviour/divergences/006 records the constraint and GBATEK states it
// flatly: "All other bits in BG0CNT have no effect on 3D, namely, mosaic cannot
// be used on the 3D layer." The 3D image IS BG0 of the main engine, so there is
// no other layer to put it on.
//
// The Dive's Shatter and the night's Tear both coarsen the ground with mosaic
// (dive.s:174, night.s:780). With the 3D renderer active those two beats do not
// merely look different -- they do not happen, and nothing faults, and the
// effect's timer runs to completion over a ground that never changed. That is
// the worst shape a divergence can take: invisible, silent, and only findable
// by someone who knows what the beat is supposed to look like.
//
// So it is a question this file can answer rather than a note in a document.
bool mosaicWouldBeLost(const ScreenFx& fx);

}  // namespace kh::device
