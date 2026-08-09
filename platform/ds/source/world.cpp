#include "world.h"

namespace kh {
namespace {

// atkOfsX/atkOfsY, world.s:2241-2242: the centre of a swing, one tile ahead of
// Sora.  A cardinal is 256 and a diagonal 176, which is 256/sqrt(2) rounded --
// the same scaling dirVel uses, so the arc sits the same distance away whichever
// way he faces.
constexpr World ATK_OFS_X[8] = {
    World::fromRaw(   0), World::fromRaw( 176), World::fromRaw( 256), World::fromRaw( 176),
    World::fromRaw(   0), World::fromRaw(-176), World::fromRaw(-256), World::fromRaw(-176),
};
constexpr World ATK_OFS_Y[8] = {
    World::fromRaw( 256), World::fromRaw( 176), World::fromRaw(   0), World::fromRaw(-176),
    World::fromRaw(-256), World::fromRaw(-176), World::fromRaw(   0), World::fromRaw( 176),
};
static_assert(ATK_OFS_X[2].raw() == ATK_OFS_CARDINAL.raw(), "east is a cardinal");
static_assert(ATK_OFS_X[1].raw() == ATK_OFS_DIAGONAL.raw(), "north-east is not");

// dirTable, world.s:2256-2258, indexed vertical*3 + horizontal with 0/1/2 for
// each axis.  The centre is "no direction", which is why this returns a bool.
constexpr int8_t DIR_TABLE[9] = {
    int8_t(Dir::NW), int8_t(Dir::N), int8_t(Dir::NE),
    int8_t(Dir::W),  -1,             int8_t(Dir::E),
    int8_t(Dir::SW), int8_t(Dir::S), int8_t(Dir::SE),
};

World absW(World v) { return World::fromRaw(v.raw() < 0 ? -v.raw() : v.raw()); }

int idx(Dir d) { return static_cast<int>(d); }

}  // namespace

// ---------------------------------------------------------------------------
// The shared vocabulary
// ---------------------------------------------------------------------------
bool readMoveDir(const Pad& pad, Dir& out) {
    // Opposite buttons do NOT cancel.  ReadMoveDir tests left and then right,
    // and the second test overwrites, so left+right is RIGHT and up+down is
    // DOWN.  Reproduced rather than tidied: the trace would diverge on any
    // frame a player rolls the d-pad through the centre.
    int h = 1;
    if (pad.isHeld(Button::Left)) h = 0;
    if (pad.isHeld(Button::Right)) h = 2;
    int v = 1;
    if (pad.isHeld(Button::Up)) v = 0;
    if (pad.isHeld(Button::Down)) v = 2;
    const int8_t d = DIR_TABLE[v * 3 + h];
    if (d < 0) return false;
    out = static_cast<Dir>(d);
    return true;
}

void setVelFull(Actors& a, int slot) {
    a.vx[slot] = DIR_VEL_X[idx(a.dir[slot])];
    a.vy[slot] = DIR_VEL_Y[idx(a.dir[slot])];
}

void setVelHalf(Actors& a, int slot) {
    // asr1, not a divide.  A Shadow's westward step is 9 and its eastward 8,
    // because -17 >> 1 floors to -9 -- see grid.h, where that asymmetry is
    // asserted rather than described.
    a.vx[slot] = asr1(DIR_VEL_X[idx(a.dir[slot])]);
    a.vy[slot] = asr1(DIR_VEL_Y[idx(a.dir[slot])]);
}

void clearVelocity(Actors& a, int slot) {
    a.vx[slot] = World::fromRaw(0);
    a.vy[slot] = World::fromRaw(0);
}

void animateWalk(Actors& a, int slot) {
    if (a.animT[slot] != 0) {
        --a.animT[slot];
        return;
    }
    a.animT[slot] = 6;                      // ...so a cel lasts 7 frames
    a.anim[slot] = uint8_t((a.anim[slot] + 1) & 3);
}

namespace {

// AttackPoint: the centre of the arc, from the PLAYER's position and facing --
// not from the attacking actor's, because only Sora ever swings.
void attackPoint(const Actors& a, int player, World& cx, World& cy) {
    const int d = idx(a.dir[player]);
    cx = a.x[player] + ATK_OFS_X[d];
    cy = a.y[player] + ATK_OFS_Y[d];
}

bool heartlessInRange(const Actors& a, int slot, World cx, World cy) {
    return absW(a.x[slot] - cx).raw() < ATK_REACH_X.raw()
        && absW(a.y[slot] - cy).raw() < ATK_REACH_Y.raw();
}

void hurtHeartless(WorldState& w, Actors& a, int slot, int player) {
    if (a.hp[slot] != 0) --a.hp[slot];
    if (a.hp[slot] == 0) {
        a.type[slot] = ActType::None;
        w.hitStop = 4;                      // a kill is held one frame longer
        return;
    }
    a.hitT[slot] = HEART_RECOIL_FRAMES;
    // Knocked away along SORA's facing at twice the walk speed.  KNOCKBACK = 3
    // in game.inc is dead; both knockback sites double instead.
    const int d = idx(a.dir[player]);
    a.vx[slot] = World::fromRaw(int16_t(DIR_VEL_X[d].raw() * 2));
    a.vy[slot] = World::fromRaw(int16_t(DIR_VEL_Y[d].raw() * 2));
    w.hitStop = 3;
}

void doAttackHit(WorldState& w, SceneView& view, int player) {
    Actors& a = view.actors;
    World cx{}, cy{};
    attackPoint(a, player, cx, cy);
    for (int i = 0; i < MAX_ACTORS; ++i) {
        if (a.type[i] != ActType::Shadow) continue;
        // A wooden sword goes straight through them.  The assembly loads this
        // eight-bit deliberately: saidNoUse is the byte after keyGot, and a
        // sixteen-bit load would read "already told" as "has the Keyblade".
        if (!w.keyGot) continue;
        if (!heartlessInRange(a, i, cx, cy)) continue;
        hurtHeartless(w, a, i, player);
    }
    // The two bosses answer to the same test with their own extents.  Neither
    // is simulated yet -- see the note at the end of this file.
}

void spawnSlash(SceneView& view, int player) {
    World cx{}, cy{};
    attackPoint(view.actors, player, cx, cy);
    const int s = view.actors.spawn(ActType::Slash, cx, cy);
    if (s >= 0) view.actors.timer[s] = 6;
}

// HeartlessAimDir: which way a Shadow leans, with a deadzone.  Returns false
// for "stand still" -- and a Shadow that stands still skips its move AND its
// touch test, which is why it can never damage Sora point-blank.
bool heartlessAimDir(const Actors& a, int slot, int player, Dir& out) {
    const World dx = a.x[player] - a.x[slot];
    const World dy = a.y[player] - a.y[slot];
    int h = 1;
    if (dx.raw() >= 0) {
        if (dx.raw() >= HEART_DEAD_X.raw()) h = 2;
    } else if (-dx.raw() >= HEART_DEAD_X.raw()) {
        h = 0;
    }
    int v = 1;
    if (dy.raw() >= 0) {
        if (dy.raw() >= HEART_DEAD_Y.raw()) v = 2;
    } else if (-dy.raw() >= HEART_DEAD_Y.raw()) {
        v = 0;
    }
    const int8_t d = DIR_TABLE[v * 3 + h];
    if (d < 0) return false;
    out = static_cast<Dir>(d);
    return true;
}

// HeartlessTouchTest.  The X half-extent is HALVED before the compare -- a bare
// `lsr a` in the assembly, uncommented -- so the effective box is 320 x 160 and
// not the 160 x 160 the constant suggests.  See BEHAVIOUR.md section 2.
bool heartlessTouchTest(const Actors& a, int slot, World px, World py) {
    return asr1(absW(a.x[slot] - px)).raw() < TOUCH_X.raw()
        && absW(a.y[slot] - py).raw() < TOUCH_Y.raw();
}

void touchPlayer(WorldState& w, SceneView& view, int slot) {
    Actors& a = view.actors;
    const int p = view.player;
    if (a.state[p] == ActState::Hurt) return;       // already reeling
    if (a.type[p] == ActType::None) return;
    if (!heartlessTouchTest(a, slot, a.x[p], a.y[p])) return;
    damageSora(w, view, slot);
}

}  // namespace

void damageSora(WorldState& w, SceneView& view, int from) {
    Actors& a = view.actors;
    const int p = view.player;
    const Dir push = a.dir[from];       // along the ATTACKER's heading
    if (a.state[p] == ActState::Dead) return;
    if (a.hp[p] != 0) --a.hp[p];
    if (a.hp[p] == 0) {
        a.state[p] = ActState::Dead;
        a.timer[p] = DEATH_FRAMES;
    } else {
        a.state[p] = ActState::Hurt;
        a.timer[p] = HURT_FRAMES;
    }
    a.vx[p] = World::fromRaw(int16_t(DIR_VEL_X[idx(push)].raw() * 2));
    a.vy[p] = World::fromRaw(int16_t(DIR_VEL_Y[idx(push)].raw() * 2));
    w.hitStop = 3;
}

void updateSlash(Actors& a, int slot) {
    if (a.timer[slot] == 0) {
        a.type[slot] = ActType::None;
        return;
    }
    --a.timer[slot];
    a.tile[slot] = uint8_t(a.timer[slot] >= 3 ? sprite::Slash0 : sprite::Slash1);
}

// ---------------------------------------------------------------------------
// Sora
// ---------------------------------------------------------------------------
void updateSora(WorldState& w, SceneView& view, ScreenFx& fx, int slot) {
    Actors& a = view.actors;

    // A dialogue box takes control away entirely -- and clears the velocity, so
    // he does not coast into a wall while somebody talks.
    if (view.dialogue.busy()) {
        clearVelocity(a, slot);
        return;
    }

    switch (a.state[slot]) {
        case ActState::Dead: {
            if (a.timer[slot] == 0) {
                if (w.deadFlag == 0) w.deadFlag = 1;
                return;
            }
            --a.timer[slot];
            // 50 frames to brightness 12 down to 0, floored at 3 so GAME OVER
            // stays readable.  The shift reads the POST-decrement value.
            uint8_t b = uint8_t(a.timer[slot] >> 2);
            if (b < 3) b = 3;
            fx.brightness = b;
            return;
        }

        case ActState::Fall: {
            // The scene is carrying him: no control, and a slow tumble.  The
            // facing advances every 9 frames, which is what makes it read as
            // turning over rather than spinning.
            clearVelocity(a, slot);
            if (a.animT[slot] != 0) {
                --a.animT[slot];
                return;
            }
            a.animT[slot] = 8;
            a.dir[slot] = static_cast<Dir>((idx(a.dir[slot]) + 1) & 7);
            return;
        }

        case ActState::Attack: {
            // DECREMENTS BEFORE TESTING, unlike the scene machines' timers.
            // The frame that sets the timer is the spent one, and ATK_ACTIVE is
            // tested against the post-decrement value -- see audit finding 52.
            --a.timer[slot];
            if (a.timer[slot] == ATK_ACTIVE) doAttackHit(w, view, view.player);
            if (a.timer[slot] == 0) a.state[slot] = ActState::Idle;
            return;
        }

        case ActState::Hurt: {
            --a.timer[slot];
            if (a.timer[slot] == 0) a.state[slot] = ActState::Idle;
            tryMoveActor(a, slot, view.ground);
            // Bleed the knockback off.  asr1, so a negative velocity decays to
            // -1 AND STAYS THERE -- -1 is a fixed point of an arithmetic shift,
            // which is why a westward knockback never quite stops.
            a.vx[slot] = asr1(a.vx[slot]);
            a.vy[slot] = asr1(a.vy[slot]);
            return;
        }

        default:
            break;
    }

    // --- free movement ---
    if (view.pad.wasPressed(Button::B)) {
        a.state[slot] = ActState::Attack;
        a.timer[slot] = ATTACK_FRAMES;
        clearVelocity(a, slot);
        spawnSlash(view, view.player);
        return;
    }

    Dir d{};
    if (!readMoveDir(view.pad, d)) {
        a.state[slot] = ActState::Idle;
        a.anim[slot] = 0;
        clearVelocity(a, slot);
        return;
    }
    a.dir[slot] = d;
    a.state[slot] = ActState::Walk;
    setVelFull(a, slot);
    tryMoveActor(a, slot, view.ground);
    animateWalk(a, slot);
}

// ---------------------------------------------------------------------------
// The Shadow Heartless
// ---------------------------------------------------------------------------
void updateHeartless(WorldState& w, SceneView& view, int slot) {
    Actors& a = view.actors;

    if (a.hitT[slot] != 0) {
        // Recoiling: coast on the knockback and bleed it off.  It is NOT
        // invulnerable while it does -- unlike a boss, which is.
        --a.hitT[slot];
        tryMoveActor(a, slot, view.ground);
        a.vx[slot] = asr1(a.vx[slot]);
        a.vy[slot] = asr1(a.vy[slot]);
    } else {
        Dir d{};
        if (heartlessAimDir(a, slot, view.player, d)) {
            a.dir[slot] = d;
            setVelHalf(a, slot);
            tryMoveActor(a, slot, view.ground);
            // Before the Keyblade they cannot reach him.  This gate is why
            // NightRestart's pre-N_KAIRI branch is unreachable: no keyGot means
            // no damage means no death to restart from.  Audit finding 56.
            if (w.keyGot) touchPlayer(w, view, slot);
        }
        // A Shadow whose aim comes back "stand still" skips the move AND the
        // touch, so it can never connect point-blank.  That is the behaviour,
        // not an oversight here.
    }

    if (a.animT[slot] != 0) {
        --a.animT[slot];
    } else {
        a.animT[slot] = HEART_ANIM_FRAMES;
        a.anim[slot] = uint8_t((a.anim[slot] + 1) & 3);
    }
    // Cels sit two tiles apart, from whichever base this scene's copy starts at.
    a.tile[slot] = uint8_t(a.anim[slot] * 2 + w.heartTile);
}

// ---------------------------------------------------------------------------
// The dispatcher
// ---------------------------------------------------------------------------
void updateWorld(WorldState& w, SceneView& view, ScreenFx& fx) {
    // Impact freeze.  It stops EVERY actor and nothing else: the scene script,
    // the camera, the HUD and OAM all keep running, which is what makes a
    // connect read as weight rather than as a stall.  Decremented on the frozen
    // frame, so N costs N frames.
    if (w.hitStop != 0) {
        --w.hitStop;
        return;
    }

    Actors& a = view.actors;
    for (int i = 0; i < MAX_ACTORS; ++i) {
        switch (a.type[i]) {
            case ActType::None:                                   break;
            case ActType::Sora:    updateSora(w, view, fx, i);     break;
            case ActType::Shadow:  updateHeartless(w, view, i);    break;
            case ActType::Slash:   updateSlash(a, i);              break;
            // Darkside, Armor, Orb, Mote, Fish and Riku have behaviour in
            // world.s and island.s and are NOT here yet -- see the note below.
            // Everything else is inert by having no case, which is the same
            // thing the assembly's comparison chain does.
            default:                                               break;
        }
    }
}

// WHAT IS NOT HERE, AND WHY IT IS SAFE TO SAY SO.
//
// UpdateDarkside, DarksideSlam, DarksideSweep, FireOrbs, SpawnOrb, UpdateOrb,
// OrbHitPlayer, HurtBoss, BossInRange, PlayerUnderBoss, UpdateArmor, UpdateFish,
// UpdateMote and UpdateRiku are all still on the SNES side only.  That is 700
// lines of world.s and town.s against the 300 here.
//
// The dispatcher above is written so their absence is INERT rather than wrong:
// an actor of a type with no case is simply not updated, which is exactly what
// the assembly's comparison chain does for a prop.  So a scene containing only
// Sora, Shadows and slashes -- the Station of Awakening, the night's search, the
// Second District's wave -- simulates completely, and a scene containing a boss
// simulates everything except the boss.  The trace oracle will say which, at the
// frame it first matters, rather than leaving it to be discovered.

}  // namespace kh
