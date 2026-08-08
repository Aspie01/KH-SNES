#pragma once
// Ground geometry, movement and the camera.  The heart of the port.
//
// The view is three-quarters overhead, so screen space IS world space: an actor
// stores world pixels in Q12.4 and drawing needs no projection.  The map comes
// back only for "what am I standing on", and on a square lattice that inverts
// world = tile * 16 into a shift.  See docs/BEHAVIOUR.md §1.
//
// Ported from platform/snes/src/grid.s (TileIndex, TileWalkable, TileHeight,
// StepOk, StoreZ, TryMoveActor, UpdateCamera) and world.s (SetActorZ, PlayerPos,
// NearPlayer).  Two divergences are mandated and marked DIVERGENCE below; see
// docs/behaviour/divergences/001-ds-screen-height.md.

#include <cstdint>

#include "actor.h"
#include "constants.h"
#include "fixed.h"
#include "scene.h"

namespace kh {

// A sign-preserving halving, which is what the ASR1 macro was: `cmp #$8000`
// then `ror a`, so the sign bit rotates back in.  Two consequences a port must
// match if it diffs traces, both from BEHAVIOUR-AUDIT.md:
//   - it FLOORS, so a Shadow's westward diagonal is 9 while its eastward is 8;
//   - -1 is a fixed point, so a negative knockback decays to -1 and stays there
//     rather than reaching zero.
constexpr World asr1(World v) { return World::fromRaw(v.raw() >> 1); }

static_assert(asr1(World::fromRaw(-17)).raw() == -9, "ASR1 floors");
static_assert(asr1(World::fromRaw(17)).raw() == 8, "and truncates upward-of-zero");
static_assert(asr1(World::fromRaw(-1)).raw() == -1, "-1 is a fixed point");

// --- collision -------------------------------------------------------------

// TileHeight: the ground height under a world pixel, zero off the map.
uint8_t tileHeight(const SceneGround& g, World x, World y);

// TileWalkable: is this world pixel standing on walkable ground, and if so how
// high is it?  Off the map is not walkable -- a negative coordinate shifted to a
// large unsigned value on the SNES and failed the same range check, which is
// what lets the rim work with no border of blocking tiles.
bool tileWalkable(const SceneGround& g, World x, World y, uint8_t& outHeight);

// StepOk: walkable, AND within MAX_STEP of the height being left.
bool stepOk(const SceneGround& g, World x, World y, uint8_t fromZ,
            uint8_t& outHeight);

// --- movement --------------------------------------------------------------

// Which axes a move managed.  The SNES returned nothing; a port that wants to
// test the resolution ORDER has to be able to see it, and §1 calls that order
// load-bearing.
// CAREFUL: Refused is rarer than it looks, and YOnly does not mean "moved".
// Step 3 tests (current X, candidate Y), so for a purely HORIZONTAL move the
// candidate Y is the current Y -- it tests the tile the actor is already
// standing on, which passes, and writes Y back unchanged.  A walk straight into
// a wall therefore reports YOnly having moved nothing.  Refused needs every
// branch to fail, which needs both velocities non-zero.  This is faithful; the
// SNES did the same and it is why `@slideY` cannot be reached with a zero vy.
enum class MoveResult : uint8_t {
    Both = 0,       // the full move committed
    XOnly = 1,      // slid along a wall horizontally
    YOnly = 2,      // ...or vertically, OR a blocked cardinal move (see above)
    Refused = 3,    // wedged: both axes and both slides failed
};

// TryMoveActor: apply vx/vy with wall sliding.
//
// THE RESOLUTION ORDER IS LOAD-BEARING.  Both axes, then X only, then Y only,
// then refuse.  That is what produces wall sliding, and X-before-Y means a
// diagonal into a corner resolves horizontally.  Reversing the last two changes
// how the island's walkways feel.  BEHAVIOUR.md §1.
//
// The single-axis attempts use the CURRENT value of the other axis, not the
// candidate -- so a failed X does not leak into the Y attempt.
MoveResult tryMoveActor(Actors& a, int slot, const SceneGround& g);

// SetActorZ: re-derive the height an actor is standing on.  A Flat actor is hung
// on a wall rather than standing on anything, so its height is forced to zero.
//
// Any code that repositions an actor without going through tryMoveActor must
// call this, or the one-step rule measures from a stale deck.  BEHAVIOUR-AUDIT.md
// records that PutActor and PlaceSora do and UpdateRiku deliberately does not.
void setActorZ(Actors& a, int slot, const SceneGround& g);

// NearPlayer: is this actor within (rangeX, rangeY) of a point?  Half-extents,
// and the comparison is STRICTLY LESS THAN -- a difference exactly equal to the
// range is a miss.
bool nearPoint(const Actors& a, int slot, World px, World py,
               World rangeX, World rangeY);

// --- camera ----------------------------------------------------------------

// camLoX/camHiX/camLoY/camHiY.  A scene smaller than the screen pins the camera
// by setting low equal to high, which is why these are bounds and not a flag.
struct CameraBounds {
    int loX = 0;
    int hiX = 0;
    int loY = 0;
    int hiY = 0;
};

struct Camera {
    int x = 0;              // camX, whole pixels
    int y = 0;              // camY
    int bgHOfs = 0;         // what the background scroll register is given
    int bgVOfs = 0;
};

// A map bigger than the screen scrolls to its edges.
//
// DIVERGENCE: the vertical range is 32 pixels wider than the SNES's, because the
// screen is 32 lines shorter.  Thirty-two more rows of every map become
// reachable by the camera than were ever composed to be seen.
constexpr CameraBounds scrollingBounds(int mapW, int mapH) {
    return CameraBounds{0, mapW * TILE_PX - SCREEN_W,
                        0, mapH * TILE_PX - SCREEN_H};
}

// ...and one smaller than the screen does not scroll at all.
constexpr CameraBounds pinnedBounds(int x, int y) {
    return CameraBounds{x, x, y, y};
}

// UpdateCamera: centre on the player and clamp.
//
// DIVERGENCE: centres on playerY - 96, not - 112, because SCREEN_H is 192 and
// not 224.  Horizontally it is unchanged: SCREEN_W / 2 is 128 on both.
//
// DIVERGENCE: bgVOfs is camY.  The SNES wrote (camY - 1) & 0x3FF because its
// BGnVOFS register displays background line value + 1.  The DS does not share
// that quirk, so biasing by one here would scroll the world up a pixel.
//
// shakeX is a signed byte, normally zero; the platform shatter drives it.  It
// moves the BACKGROUND only and never camX, so sprite positions -- which are
// derived from camX -- do not shake with the ground.  That is the shipped
// behaviour and it is what makes the shake read as the world coming apart rather
// than as the camera being jostled.
void updateCamera(Camera& cam, const Actors& a, int player,
                  const CameraBounds& bounds, int shakeX = 0);

// --- the ground renderer ---------------------------------------------------

// The one sanctioned virtual in the port.  It exists NOW, with the seam free, so
// that the 3D quad backend in §M7 is a swap and not a refactor -- and so the 2D
// streaming backend has somewhere to go that is not the middle of the camera.
//
// Nothing else in the simulation may be virtual: no RTTI, no exceptions, and a
// vtable in a per-frame path is a cost with no payer.
class GroundRenderer {
public:
    virtual void load(const SceneGround& ground) = 0;
    virtual void draw(const Camera& cam) = 0;
    virtual ~GroundRenderer() = default;
};

// Draws nothing, and records that it was asked to.  This is what the simulation
// runs against on the host: the geometry is testable with no renderer at all,
// and a scene that forgets to load its ground still fails a test rather than
// silently drawing the previous one.
class NullGroundRenderer final : public GroundRenderer {
public:
    void load(const SceneGround& ground) override;
    void draw(const Camera& cam) override;

    int loads() const { return loads_; }
    int draws() const { return draws_; }
    const SceneGround* loaded() const { return loaded_; }
    Camera lastCamera() const { return last_; }

private:
    int loads_ = 0;
    int draws_ = 0;
    const SceneGround* loaded_ = nullptr;
    Camera last_{};
};

}  // namespace kh
