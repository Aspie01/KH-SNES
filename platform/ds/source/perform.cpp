// The four SceneActions that are routines.  Lifted verbatim out of
// platform/ds/host/trace_main.cpp, which was their only home while there was
// only one performer.

#include "perform.h"

namespace kh {

bool raiseArmor(SceneView& view, WorldState& world) {
    const int p = view.player;
    if (p < 0 || p >= MAX_ACTORS) return false;
    Actors& a = view.actors;
    a.x[p] = tileCentre(16);
    a.y[p] = tileCentre(10);
    a.vx[p] = World::fromRaw(0);
    a.vy[p] = World::fromRaw(0);
    a.dir[p] = Dir::S;
    a.state[p] = ActState::Idle;
    a.timer[p] = 0;
    a.z[p] = view.ground.heightAt(16, 10);

    const int armor = a.spawn(ActType::Armor, tileCentre(16), tileCentre(7));
    if (armor < 0) return false;
    a.z[armor] = uint8_t(GA_DROP_Z);
    a.timer[armor] = uint8_t(GA_DROP);      // Drop, by the state being 0
    world.bossHP = uint8_t(GA_MAX_HP);
    // SpawnHands: both start ON the torso, numbered 0 then 1.  The Drop branch
    // is the one branch that does not call PlaceHands, so they arrive with the
    // body rather than reaching out ahead of it.
    for (int hand = 0; hand < 2; ++hand) {
        const int h = a.spawn(ActType::Gauntlet, tileCentre(16), tileCentre(7));
        if (h < 0) return false;
        a.anim[h] = uint8_t(hand);
    }
    return true;
}

void sweepGauntlets(SceneView& view, WorldState& world, ScreenFx& fx) {
    for (int i = 0; i < MAX_ACTORS; ++i)
        if (view.actors.type[i] == ActType::Gauntlet)
            view.actors.type[i] = ActType::None;
    world.bossHP = 0;
    fx.shakeX = 0;
}

bool beginFall(SceneView& view) {
    const int p = view.player;
    if (p < 0 || p >= MAX_ACTORS) return false;
    view.actors.state[p] = ActState::Fall;
    view.actors.anim[p] = 0;
    view.actors.animT[p] = 0;
    return true;
}

bool spawnMote(SceneView& view) {
    const int p = view.player;
    if (p < 0 || p >= MAX_ACTORS) return false;
    Actors& a = view.actors;
    const MoteOffset o = moteOffset(view.frame);
    const int m = a.spawn(ActType::Mote, a.x[p] + o.dx, a.y[p] + o.dy);
    if (m < 0) return true;             // dive.s:534 -- a full table loses it
    // AFTER the spawn, which zeroed both.  dive.s:535-536, then dive.s:544-545.
    a.timer[m] = uint8_t(MOTE_LIFE);
    a.vy[m] = -MOTE_RISE;               // `lda #.loword(-MOTE_RISE)`
    // z is deliberately left at zero, and that is a match rather than an
    // omission.  SpawnActor ends in `jsr SetActorZ` (world.s:234), but a mote
    // spawns 120..160 px below Sora and therefore off the bottom edge of a
    // 32x16 map: TileIndex rejects the row, TileHeight returns zero for a
    // rejected index, and Actors::spawn already leaves zero.
    return true;
}

bool standColumn(SceneView& view, ActType who) {
    Actors& a = view.actors;
    int found = -1;
    for (int i = 0; i < MAX_ACTORS; ++i) {
        if (a.type[i] == who) { found = i; break; }
    }
    if (found < 0) return true;         // night.s:553-560 falls out to `rts`

    // Read, clear, spawn -- in that order, because the slot matters.  See the
    // header: the column takes the slot the person vacated only because the
    // clear happens first.
    const World px = a.x[found];
    const World py = a.y[found];
    a.type[found] = ActType::None;
    return a.spawn(ActType::Dark, px, py) >= 0;
}

void clearColumns(SceneView& view) {
    for (int i = 0; i < MAX_ACTORS; ++i)
        if (view.actors.type[i] == ActType::Dark)
            view.actors.type[i] = ActType::None;
}

bool openTheDoor(SceneView& view) {
    Actors& a = view.actors;
    bool opened = false;
    // Every match, with no early exit -- night.s:684's `@next` continues the
    // scan.  One door is what the cast places; the loop is what the ROM does.
    for (int i = 0; i < MAX_ACTORS; ++i) {
        if (a.type[i] != ActType::Door) continue;
        a.type[i] = ActType::DoorOpen;
        a.tile[i] = tileFor(ActType::DoorOpen);
        opened = true;
    }
    return opened;
}

}  // namespace kh
