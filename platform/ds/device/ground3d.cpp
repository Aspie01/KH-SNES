// The 3D quad ground: the visible set, the quad list, and the budget.

#include "ground3d.h"

#include "constants.h"

namespace kh::device {
namespace {

int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

}  // namespace

Frustum visibleTiles(const Camera& cam, int mapW, int mapH, int margin) {
    // The screen in tiles, plus one for the partial tile at each edge, plus the
    // margin.  The +1 is the same off-by-one the 2D streamer has and for the
    // same reason: a camera that is not on a tile boundary shows part of one
    // more tile than the division suggests, and leaving it out is a column of
    // nothing at the right edge that only appears at some scroll positions.
    const int firstX = (cam.x >= 0 ? cam.x : cam.x - (TILE_PX - 1)) / TILE_PX;
    const int firstY = (cam.y >= 0 ? cam.y : cam.y - (TILE_PX - 1)) / TILE_PX;
    const int lastX = (cam.x + SCREEN_W - 1) / TILE_PX;
    const int lastY = (cam.y + SCREEN_H - 1) / TILE_PX;

    // Clamped to the map, because a quad outside it has nothing to texture and
    // no height to sit at.  The margin widens the visible set and the clamp
    // then takes it back at the edges, which is correct: there is no world out
    // there to draw and the backdrop is what shows.
    Frustum f;
    f.loX = clampi(firstX - margin, 0, mapW - 1);
    f.loY = clampi(firstY - margin, 0, mapH - 1);
    f.hiX = clampi(lastX + margin, 0, mapW - 1);
    f.hiY = clampi(lastY + margin, 0, mapH - 1);
    return f;
}

Ground3dBuild buildQuads(const SceneGround& g, const CharMap& map,
                         const Camera& cam, int margin, Quad* out, int cap) {
    Ground3dBuild b;
    if (!g.valid() || !map.valid()) return b;

    const Frustum f = visibleTiles(cam, g.width(), g.height(), margin);
    for (int ty = f.loY; ty <= f.hiY; ++ty) {
        for (int tx = f.loX; tx <= f.hiX; ++tx) {
            ++b.considered;

            // A HOLE IS A HOLE.  Emitting a flat quad over a non-walkable tile
            // would be a floor the player falls through in the collision map
            // and stands on in the picture -- and the picture is what a person
            // believes.  The 2D renderer has the same rule for free, because a
            // hole is a transparent character; here it has to be a decision.
            if (!g.walkable(tx, ty)) {
                ++b.culled;
                continue;
            }

            if (b.quads >= cap || b.quads >= QUAD_BUDGET) {
                ++b.dropped;
                continue;
            }

            Quad& q = out[b.quads];
            q.tx = int16_t(tx);
            q.ty = int16_t(ty);
            // The four corner heights come from the four TILES that meet at
            // each corner, not from this one.  A per-tile height with per-tile
            // corners is a staircase of floating slabs: every tile flat at its
            // own level with a vertical gap to its neighbour.  Taking the
            // corner from the tiles around it is what turns a height map into a
            // surface -- and it is why this is a quad with four heights rather
            // than a quad with one.
            //
            // heightAt() returns 0 off the map, which is the right answer at a
            // shoreline: the ground slopes down into the sea rather than ending
            // in a cliff.
            q.h[0] = g.heightAt(tx, ty);                        // NW
            q.h[1] = g.heightAt(tx + 1, ty);                    // NE
            q.h[2] = g.heightAt(tx, ty + 1);                    // SW
            q.h[3] = g.heightAt(tx + 1, ty + 1);                // SE
            // A tile is CHARS_PER_TILE characters square and the map is in
            // characters, so the north-west character of the tile is the one
            // whose entry names the texture.
            q.chr = map.at(tx * 2, ty * 2);
            ++b.quads;
        }
    }
    b.vertices = b.quads * VERTS_PER_QUAD;
    return b;
}

QuadGround::QuadGround(Quad* quads, int cap, int margin)
    : quads_(quads), cap_(cap), margin_(margin) {}

bool QuadGround::setMap(const CharMap& map) {
    error_ = nullptr;
    if (!map.valid()) {
        error_ = "the character map is empty or shorter than its own extents";
        return false;
    }
    // NO 64-CHARACTER CEILING HERE, and that is the whole reason this backend
    // exists alongside the other one.  The 2D streamer is bounded by a text
    // background's 64x64 window and slides a view through it; geometry is
    // bounded by the polygon budget and nothing else, so a map twice the size
    // costs twice the memory and the same number of quads on screen.
    map_ = map;
    return true;
}

void QuadGround::load(const SceneGround& ground) {
    error_ = nullptr;
    if (!map_.valid()) {
        error_ = "load() with no character map; call setMap first";
        return;
    }
    if (ground.valid()
        && (ground.width() * 2 != map_.wChars || ground.height() * 2 != map_.hChars)) {
        error_ = "the collision map and the character map are different sizes; "
                 "what is walked on and what is drawn would not be the same map";
        return;
    }
    ground_ = &ground;
    build_ = Ground3dBuild{};
}

void QuadGround::draw(const Camera& cam) {
    build_ = Ground3dBuild{};
    if (!ground_ || !map_.valid()) return;
    build_ = buildQuads(*ground_, map_, cam, margin_, quads_, cap_);
    // The emission -- glBegin(GL_QUADS), four glVertex3v16 per quad, the
    // texture coordinates -- goes here and needs libnds.  Everything above it
    // is the decision, and the decision is what a test can hold.
}

bool mosaicWouldBeLost(const ScreenFx& fx) {
    // Not "is the 3D renderer active" -- this file cannot know that, and the
    // caller that chose the renderer can.  What it answers is the other half:
    // is there mosaic to lose.  Together those two are the condition divergence
    // 006 describes, and separating them is what lets the 2D path ask the same
    // question and get `false` from a renderer that could have honoured it.
    return fx.mosaic != 0;
}

}  // namespace kh::device
