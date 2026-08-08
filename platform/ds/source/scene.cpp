#include "scene.h"

namespace kh {

void SceneGround::set(Blob collision, Blob height, int w, int h) {
    const size_t need = size_t(w) * size_t(h);
    if (w <= 0 || h <= 0 || collision.size < need || height.size < need) {
        coll_ = nullptr;
        hmap_ = nullptr;
        w_ = 0;
        h_ = 0;
        return;
    }
    coll_ = collision.data;
    hmap_ = height.data;
    w_ = w;
    h_ = h;
}

SpawnResult spawnCast(Actors& actors, Blob table, const SceneGround& ground,
                      uint8_t* variantOut) {
    SpawnResult r{};
    if (table.empty()) return r;

    size_t p = 0;
    while (true) {
        if (p >= table.size) {          // no terminator
            r.malformed = true;
            return r;
        }
        if (table.data[p] == CAST_END) return r;
        if (p + CAST_STRIDE > table.size) {
            r.malformed = true;
            return r;
        }
        const ActType type = static_cast<ActType>(table.data[p]);
        const int32_t i = table.data[p + 1];
        const int32_t j = table.data[p + 2];
        const uint8_t variant = table.data[p + 3];
        p += CAST_STRIDE;

        const int slot = actors.spawn(type, tileCentre(i), tileCentre(j));
        if (slot < 0) {
            ++r.refused;                // the caller MUST look at this
            continue;
        }
        // SetActorZ, at spawn time: a wall prop is hung rather than standing, so
        // its height is forced to zero wherever it is placed; everything else
        // takes the height of the tile under it.  M3 owns the version that runs
        // during movement -- this is the one that runs once, because the pool
        // cannot resolve it without a scene and the scene is here.
        if (has(actors.flags[slot], ActFlags::Flat))
            actors.z[slot] = 0;
        else
            actors.z[slot] = ground.heightAt(int(i), int(j));

        if (variantOut) variantOut[slot] = variant;
        ++r.spawned;
    }
}

int readSpots(Blob table, Tile* out, int cap) {
    int n = 0;
    for (size_t p = 0; p + 1 < table.size; p += SPOT_STRIDE) {
        if (table.data[p] == CAST_END) break;
        if (n >= cap) return -1;        // more entries than the buffer holds
        out[n++] = Tile{table.data[p], table.data[p + 1]};
    }
    return n;
}

int readDoors(Blob table, Door* out, int cap) {
    int n = 0;
    for (size_t p = 0; p + DOOR_STRIDE - 1 < table.size; p += DOOR_STRIDE) {
        if (table.data[p] == CAST_END) break;
        if (n >= cap) return -1;
        out[n++] = Door{Tile{table.data[p], table.data[p + 1]},
                        Tile{table.data[p + 2], table.data[p + 3]}};
    }
    return n;
}

}  // namespace kh
