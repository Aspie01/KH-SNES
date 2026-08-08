#include "grid.h"

namespace kh {

namespace {

// TileIndex, as a pair of tile coordinates.  tileOf() floors, so a negative
// world pixel gives a negative tile and inBounds rejects it -- the same outcome
// the SNES got by shifting a negative into a large unsigned value and failing an
// unsigned compare against MAP_W.  One difference, in the DS's favour: a
// coordinate past 65535 wrapped there and does not here.
struct TileAt {
    int i;
    int j;
};

TileAt tileAt(World x, World y) {
    return TileAt{int(tileOf(x)), int(tileOf(y))};
}

}  // namespace

uint8_t tileHeight(const SceneGround& g, World x, World y) {
    const TileAt t = tileAt(x, y);
    return g.heightAt(t.i, t.j);        // zero off the map
}

bool tileWalkable(const SceneGround& g, World x, World y, uint8_t& outHeight) {
    const TileAt t = tileAt(x, y);
    if (!g.walkable(t.i, t.j)) {
        outHeight = 0;                  // TileWalkable's `stz tmp2` on the way out
        return false;
    }
    outHeight = g.heightAt(t.i, t.j);
    return true;
}

bool stepOk(const SceneGround& g, World x, World y, uint8_t fromZ,
            uint8_t& outHeight) {
    if (!tileWalkable(g, x, y, outHeight)) return false;
    const int d = int(outHeight) - int(fromZ);
    return (d < 0 ? -d : d) <= MAX_STEP;
}

MoveResult tryMoveActor(Actors& a, int slot, const SceneGround& g) {
    const uint8_t fromZ = a.z[slot];
    const World candX = a.x[slot] + a.vx[slot];
    const World candY = a.y[slot] + a.vy[slot];
    uint8_t z = 0;

    // 1. Both axes.
    if (stepOk(g, candX, candY, fromZ, z)) {
        a.x[slot] = candX;
        a.y[slot] = candY;
        a.z[slot] = z;                  // StoreZ
        return MoveResult::Both;
    }
    // 2. X only, against the CURRENT Y.
    if (stepOk(g, candX, a.y[slot], fromZ, z)) {
        a.x[slot] = candX;
        a.z[slot] = z;
        return MoveResult::XOnly;
    }
    // 3. Y only, against the CURRENT X -- which step 2 did not commit.
    if (stepOk(g, a.x[slot], candY, fromZ, z)) {
        a.y[slot] = candY;
        a.z[slot] = z;
        return MoveResult::YOnly;
    }
    // 4. Wedged.  Note that z is NOT touched: the actor did not move, so the
    //    height it is standing on has not changed.
    return MoveResult::Refused;
}

void setActorZ(Actors& a, int slot, const SceneGround& g) {
    if (has(a.flags[slot], ActFlags::Flat)) {
        a.z[slot] = 0;                  // hung on a wall, not standing on anything
        return;
    }
    a.z[slot] = tileHeight(g, a.x[slot], a.y[slot]);
}

bool nearPoint(const Actors& a, int slot, World px, World py,
               World rangeX, World rangeY) {
    const int32_t dx = a.x[slot].raw() - px.raw();
    const int32_t dy = a.y[slot].raw() - py.raw();
    // Strictly less than: `cmp range / bcs miss` misses on equality.
    if ((dx < 0 ? -dx : dx) >= rangeX.raw()) return false;
    if ((dy < 0 ? -dy : dy) >= rangeY.raw()) return false;
    return true;
}

namespace {

// The clamp, spelled out in the SNES's own order: a centre below the low bound
// takes the low bound, one at or above the high bound takes the high bound.
int clampCam(int centre, int lo, int hi) {
    if (centre < lo) return lo;
    if (centre >= hi) return hi;
    return centre;
}

}  // namespace

void updateCamera(Camera& cam, const Actors& a, int player,
                  const CameraBounds& bounds, int shakeX) {
    // Q12.4 to whole pixels.  A shift, not a divide: the ARM9 has no hardware
    // divide either, and this runs once a frame in the same place it always did.
    const int px = a.x[player].toInt();
    const int py = a.y[player].toInt();

    cam.x = clampCam(px - SCREEN_W / 2, bounds.loX, bounds.hiX);
    cam.y = clampCam(py - SCREEN_H / 2, bounds.loY, bounds.hiY);

    // The shake moves the background and not camX, so sprites do not shake with
    // the ground.
    cam.bgHOfs = cam.x + shakeX;
    // DIVERGENCE: camY, not (camY - 1) & 0x3FF.  See grid.h.
    cam.bgVOfs = cam.y;
}

void NullGroundRenderer::load(const SceneGround& ground) {
    ++loads_;
    loaded_ = &ground;
}

void NullGroundRenderer::draw(const Camera& cam) {
    ++draws_;
    last_ = cam;
}

}  // namespace kh
