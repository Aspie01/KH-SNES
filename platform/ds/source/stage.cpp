#include "stage.h"

namespace kh {

SpotOutcome spawnAtSpot(SceneView& view, const Tile* spots, int count, int cap) {
    SpotOutcome out;
    if (spots == nullptr || count <= 0) return out;
    if (view.actors.count(ActType::Shadow) >= cap) return out;

    const uint8_t spot = view.rng.pick(count);
    const World sx = tileCentre(spots[spot].i);
    const World sy = tileCentre(spots[spot].j);
    const int32_t dx = sx.raw() - view.actors.x[view.player].raw();
    const int32_t dy = sy.raw() - view.actors.y[view.player].raw();
    // BOTH axes have to be inside 64 px to count as too close -- the SNES fell
    // through to the spawn as soon as either was outside.
    if ((dx < 0 ? -dx : dx) < SPAWN_CLEAR && (dy < 0 ? -dy : dy) < SPAWN_CLEAR) {
        out.tooClose = true;
        return out;
    }
    out.spawned = view.actors.spawn(ActType::Shadow, sx, sy) >= 0;
    return out;
}

}  // namespace kh
