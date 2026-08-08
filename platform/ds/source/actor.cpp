#include "actor.h"

namespace kh {

void Actors::clear() {
    // ClearActors only zeroed type, flags, hitT, state and timer -- the position
    // fields were left as they were, because a free slot's coordinates are never
    // read.  Clearing everything costs nothing here and removes a class of
    // "worked because nobody looked" from the port.
    for (int i = 0; i < MAX_ACTORS; ++i) {
        type[i] = ActType::None;
        z[i] = 0;
        x[i] = World();
        y[i] = World();
        vx[i] = World();
        vy[i] = World();
        dir[i] = Dir::S;
        anim[i] = 0;
        animT[i] = 0;
        state[i] = ActState::Idle;
        timer[i] = 0;
        hp[i] = 0;
        flags[i] = ActFlags::None;
        tile[i] = 0;
        pal[i] = 0;
        hitT[i] = 0;
    }
}

int Actors::spawn(ActType t, World px, World py) {
    int slot = -1;
    for (int i = 0; i < MAX_ACTORS; ++i) {
        if (type[i] == ActType::None) {
            slot = i;
            break;
        }
    }
    if (slot < 0) return -1;        // caller MUST check; see the header

    type[slot] = t;
    tile[slot] = tileFor(t);
    pal[slot] = palFor(t);
    flags[slot] = flagsFor(t);
    hp[slot] = hpFor(t);

    dir[slot] = Dir::S;
    anim[slot] = 0;
    animT[slot] = 0;
    state[slot] = ActState::Idle;
    timer[slot] = 0;
    hitT[slot] = 0;

    x[slot] = px;
    y[slot] = py;
    vx[slot] = World();
    vy[slot] = World();

    // Height is deliberately NOT resolved here.  SpawnActor called SetActorZ,
    // which needs the collision and height maps for the current scene -- that
    // arrives with the grid in M3.  Until then a spawned actor sits at height
    // zero, which is correct for every flat scene and wrong only on the island.
    z[slot] = 0;

    return slot;
}

int Actors::count(ActType t) const {
    int n = 0;
    for (int i = 0; i < MAX_ACTORS; ++i)
        if (type[i] == t) ++n;
    return n;
}

}  // namespace kh
