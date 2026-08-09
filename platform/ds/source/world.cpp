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

// Forward: the boss hurt test lives with the other attack code.
World absW(World v) { return World::fromRaw(v.raw() < 0 ? -v.raw() : v.raw()); }

int idx(Dir d) { return static_cast<int>(d); }

// actState is ONE BYTE on the SNES and two different enumerations use it: an
// ordinary actor stores ST_IDLE..ST_FALL and a boss stores DSS_REST..DSS_SWEEP_HIT
// in the same field.  The DS types the field as ActState, so the sharing has to
// be spelled somewhere; spelling it here, once, is better than a cast at each of
// the fourteen sites that would otherwise need one.
BossState bossState(const Actors& a, int slot) {
    return static_cast<BossState>(static_cast<uint8_t>(a.state[slot]));
}
void setBossState(Actors& a, int slot, BossState s) {
    a.state[slot] = static_cast<ActState>(static_cast<uint8_t>(s));
}


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

// HurtBoss.  Gated on the flinch, so a boss cannot be hit twice in ten frames --
// which is the difference between it and a Shadow, and the reason a boss fight
// is a rhythm rather than a mash.
void hurtBoss(WorldState& w, Actors& a, int slot) {
    if (a.hitT[slot] != 0) return;
    if (a.hp[slot] == 0) return;
    --a.hp[slot];
    w.bossHP = a.hp[slot];
    a.hitT[slot] = 10;
    w.hitStop = 3;
    if (a.hp[slot] == 0) a.type[slot] = ActType::None;
}

void doAttackHit(WorldState& w, SceneView& view, int player) {
    Actors& a = view.actors;
    World cx{}, cy{};
    attackPoint(a, player, cx, cy);
    for (int i = 0; i < MAX_ACTORS; ++i) {
        if (a.type[i] == ActType::Darkside || a.type[i] == ActType::Armor) {
            // Both bosses answer to the same test with their own extents; the
            // Guard Armor's torso is the narrower of the two.
            const World hx = a.type[i] == ActType::Darkside ? DS_HURT_X : GA_HURT_X;
            const World hy = a.type[i] == ActType::Darkside ? DS_HURT_Y : GA_HURT_Y;
            if (absW(a.x[i] - cx).raw() < hx.raw()
                && absW(a.y[i] - cy).raw() < hy.raw()) {
                hurtBoss(w, a, i);
            }
            continue;
        }
        if (a.type[i] != ActType::Shadow) continue;
        // A wooden sword goes straight through them.  The assembly loads this
        // eight-bit deliberately: saidNoUse is the byte after keyGot, and a
        // sixteen-bit load would read "already told" as "has the Keyblade".
        if (!w.keyGot) continue;
        if (!heartlessInRange(a, i, cx, cy)) continue;
        hurtHeartless(w, a, i, player);
    }
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
// Darkside
//
// THE BOSS HAS NO VELOCITY, AND USES actVX/actVY FOR SOMETHING ELSE.  It never
// walks, so AimAtPlayer parks the player's POSITION in those two fields and the
// slam reads them back 44 frames later.  That is why the mark is stale for the
// whole telegraph -- audit finding 4, the single most consequential error in the
// original specification, which said the mark was taken at the END of the
// wind-up and so inverted the dodge window.
// ---------------------------------------------------------------------------
namespace {

// The three arcs a sweep leaves across the front of the boss, Q12.4.
constexpr World SWEEP_OFS[3] = {
    World::fromRaw(-384), World::fromRaw(0), World::fromRaw(384),
};

// AimAtPlayer: remember where the player is standing, in the velocity fields.
void aimAtPlayer(Actors& a, int boss, int player) {
    a.vx[boss] = a.x[player];
    a.vy[boss] = a.y[player];
}

// The same nine-bucket table the pad and the Shadows use, but on a delta rather
// than on buttons -- so an orb is fired along one of the eight facings a player
// could walk, which is what makes it dodgeable by walking.
bool bucketDir(World dx, World dy, World deadX, World deadY, Dir& out) {
    int h = 1;
    if (dx.raw() >= 0) {
        if (dx.raw() >= deadX.raw()) h = 2;
    } else if (-dx.raw() >= deadX.raw()) {
        h = 0;
    }
    int v = 1;
    if (dy.raw() >= 0) {
        if (dy.raw() >= deadY.raw()) v = 2;
    } else if (-dy.raw() >= deadY.raw()) {
        v = 0;
    }
    const int8_t d = DIR_TABLE[v * 3 + h];
    if (d < 0) return false;
    out = static_cast<Dir>(d);
    return true;
}

void spawnOrb(SceneView& view, World ox, World oy, Dir d) {
    const int s = view.actors.spawn(ActType::Orb, ox, oy);
    if (s < 0) return;                  // the table was full; the orb is lost
    view.actors.dir[s] = d;
    view.actors.timer[s] = ORB_LIFE;
    // Twice a walk: an orb outruns you.  ORB_SPEED = 40 in game.inc is DEAD --
    // it has one reference, its own definition, and nothing reads it.
    view.actors.vx[s] = World::fromRaw(int16_t(DIR_VEL_X[idx(d)].raw() * 2));
    view.actors.vy[s] = World::fromRaw(int16_t(DIR_VEL_Y[idx(d)].raw() * 2));
}

void fireOrbs(SceneView& view, int boss) {
    Actors& a = view.actors;
    // The hole in the chest, 40 px above its feet.
    const World ox = a.x[boss];
    const World oy = a.y[boss] - ORB_MUZZLE_UP;
    Dir d{};
    if (!bucketDir(a.x[view.player] - ox, a.y[view.player] - oy,
                   HEART_DEAD_X, HEART_DEAD_Y, d)) {
        d = Dir::S;             // directly underneath: fire downward
    }
    // Centre, then one either side -- a three-way spread you walk out of
    // rather than dodge through.
    spawnOrb(view, ox, oy, static_cast<Dir>((idx(d) + 7) & 7));
    spawnOrb(view, ox, oy, d);
    spawnOrb(view, ox, oy, static_cast<Dir>((idx(d) + 1) & 7));
}

void darksideSlam(WorldState& w, SceneView& view, int boss) {
    Actors& a = view.actors;
    const World mx = a.vx[boss], my = a.vy[boss];    // the mark, not a velocity
    // A Shadow crawls out of the impact.  The Dive never sweeps these up, which
    // is BEHAVIOUR.md section 12's first latent bug -- a survivor persists into
    // the 170-frame fall.
    a.spawn(ActType::Shadow, mx, my);
    // ...and here is where the fist misses, every single time.
    //
    // The impact point is held in tmp0/tmp1, and the Shadow's SpawnActor call
    // above ends with `jsr SetActorZ` -- whose own comment says it "clobbers
    // tmp0-tmp4, ALL OF WHICH ARE SPENT" (world.s:SpawnActor).  In every other
    // caller they are.  Here they are not: SetActorZ leaves the new actor's
    // position SHIFTED DOWN BY FOUR in them (world.s:SetActorZ), so by the time
    // this test runs, the "impact point" is one sixteenth of the mark.
    //
    // With Sora at 4224 and the clobbered value 264, the difference is 3960
    // against a TOUCH_X of 160.  It is never close.  DARKSIDE'S SLAM CANNOT
    // DAMAGE SORA -- only the sweep and the orbs can, because neither reads
    // tmp after a spawn.  Confirmed on the oracle: a fist landing exactly on
    // him leaves him at 20 HP with hitStopTimer still zero.
    //
    // Reproduced rather than fixed, because the oracle is the specification and
    // a silent fix would make every trace diff meaningless.  Audit finding 59
    // has the one-line change if it is ever wanted.
    const World hx = World::fromRaw(int16_t(mx.raw() >> 4));
    const World hy = World::fromRaw(int16_t(my.raw() >> 4));
    if (absW(a.x[view.player] - hx).raw() < TOUCH_X.raw()
        && absW(a.y[view.player] - hy).raw() < TOUCH_Y.raw()) {
        damageSora(w, view, boss);
    }
}

void darksideSweep(WorldState& w, SceneView& view, int boss) {
    Actors& a = view.actors;
    const World cx = a.x[boss];
    const World cy = a.y[boss] + World::fromRaw(160);    // 10 px in front
    for (const World& off : SWEEP_OFS) {
        const int s = a.spawn(ActType::Slash, cx + off, cy);
        if (s >= 0) a.timer[s] = 8;
    }
    if (playerUnderBoss(a, boss, view.player)) damageSora(w, view, boss);
}

}  // namespace

bool playerUnderBoss(const Actors& a, int boss, int player) {
    return absW(a.x[player] - a.x[boss]).raw() < SWEEP_X.raw()
        && absW(a.y[player] - a.y[boss]).raw() < SWEEP_Y.raw();
}

void updateDarkside(WorldState& w, SceneView& view, int slot) {
    Actors& a = view.actors;
    // The flinch counts down every frame whatever else is happening, and it is
    // what makes the boss invulnerable between hits -- unlike a Shadow, which
    // can be hit again while it recoils.
    if (a.hitT[slot] != 0) --a.hitT[slot];

    switch (bossState(a, slot)) {
        case BossState::SlamUp:
            if (a.timer[slot] != 0) { --a.timer[slot]; return; }
            darksideSlam(w, view, slot);
            setBossState(a, slot, BossState::SlamHit);
            a.timer[slot] = DS_SLAM_HOLD;
            return;

        case BossState::OrbUp:
            if (a.timer[slot] != 0) { --a.timer[slot]; return; }
            fireOrbs(view, slot);
            setBossState(a, slot, BossState::OrbFire);
            a.timer[slot] = DS_ORB_REST;
            return;

        case BossState::SweepUp:
            if (a.timer[slot] != 0) { --a.timer[slot]; return; }
            darksideSweep(w, view, slot);
            setBossState(a, slot, BossState::SweepHit);
            a.timer[slot] = DS_SWEEP_HOLD;
            return;

        case BossState::SlamHit:
        case BossState::OrbFire:
        case BossState::SweepHit:
            if (a.timer[slot] != 0) { --a.timer[slot]; return; }
            setBossState(a, slot, BossState::Rest);
            a.timer[slot] = DS_REST;
            return;

        case BossState::Rest:
        default:
            break;
    }

    // Resting.
    if (a.timer[slot] != 0) { --a.timer[slot]; return; }

    // Standing underneath is answered IMMEDIATELY, ahead of the alternation --
    // the sweep is the punish for hugging its feet, and it has a shorter
    // telegraph than the fist for exactly that reason.
    if (playerUnderBoss(a, slot, view.player)) {
        setBossState(a, slot, BossState::SweepUp);
        a.timer[slot] = DS_SWEEP_WIND;
        return;
    }

    // Otherwise it strictly alternates, using actAnim as the toggle: the flip
    // happens FIRST, so a boss that has just spawned with anim 0 slams before
    // it ever fires.
    a.anim[slot] = uint8_t(a.anim[slot] ^ 1);
    if (a.anim[slot] == 0) {
        setBossState(a, slot, BossState::OrbUp);
        a.timer[slot] = DS_ORB_WIND;
    } else {
        aimAtPlayer(a, slot, view.player);      // the mark, taken NOW
        setBossState(a, slot, BossState::SlamUp);
        a.timer[slot] = DS_SLAM_WIND;
    }
}

void updateOrb(WorldState& w, SceneView& view, int slot) {
    Actors& a = view.actors;
    if (a.timer[slot] == 0) {
        a.type[slot] = ActType::None;           // burnt out
        return;
    }
    --a.timer[slot];

    if (a.animT[slot] == 0) {
        a.animT[slot] = 5;
        a.anim[slot] = uint8_t(a.anim[slot] ^ 1);
        a.tile[slot] = uint8_t(a.anim[slot] * 2 + sprite::Orb);
    } else {
        --a.animT[slot];
    }

    // Straight line, no collision, no ground: an orb goes through walls and
    // over the void, which is why the Station of Awakening's rim does not
    // shelter you.
    a.x[slot] = a.x[slot] + a.vx[slot];
    a.y[slot] = a.y[slot] + a.vy[slot];

    const int p = view.player;
    if (a.type[p] == ActType::None) return;
    if (a.state[p] == ActState::Hurt) return;
    // The same halved-X overlap the Shadows use; nothing here is Shadow-specific.
    if (!heartlessTouchTest(a, slot, a.x[p], a.y[p])) return;
    damageSora(w, view, slot);      // knocked back along the ORB's heading
    a.type[slot] = ActType::None;   // ...and it bursts
}


// ---------------------------------------------------------------------------
// The Guard Armor
//
// It differs from Darkside in three ways that all matter to a port: it WALKS, it
// carries its two hands as separate actors, and it arrives by falling.  The
// hands are why placeHands() is called from every single branch below, including
// the ones that do nothing else -- a branch that forgot it would leave a fist
// hanging in the air for a frame, which reads as a glitch rather than as a boss.
//
// Like Darkside it reuses actVX/actVY as the slam MARK rather than as a
// velocity, and like Darkside it takes that mark on ENTRY to the wind-up, 40
// frames early.  Unlike Darkside its fist actually connects: ArmorSlam spawns
// nothing, so the scratch it reads back is still the mark -- which is exactly
// what makes audit finding 59 specific to the Shadow that crawls out of the
// other boss's fist.
// ---------------------------------------------------------------------------
namespace {

// StepArmor: close on the player HORIZONTALLY ONLY, and stop two tiles short so
// it does not jitter on the spot.  It never moves vertically at all, which is
// why the Second District's fight is a left-right dance.
void stepArmor(Actors& a, int slot, int player, const SceneGround& g) {
    const World dx = a.x[player] - a.x[slot];
    World vx = dx.raw() < 0 ? World::fromRaw(int16_t(-GA_WALK.raw())) : GA_WALK;
    if (absW(dx).raw() < GA_STOP.raw()) vx = World::fromRaw(0);
    a.vx[slot] = vx;
    a.vy[slot] = World::fromRaw(0);
    tryMoveActor(a, slot, g);
}

// ArmorSlam.  No spawn, so the mark survives to be tested -- see the note above.
void armorSlam(WorldState& w, SceneView& view, int slot) {
    Actors& a = view.actors;
    w.hitStop = 6;              // heavier than a connect: the whole arm lands
    const World mx = a.vx[slot], my = a.vy[slot];
    if (absW(a.x[view.player] - mx).raw() < GA_HAND_X.raw()
        && absW(a.y[view.player] - my).raw() < GA_HAND_Y.raw()) {
        damageSora(w, view, slot);
    }
}

}  // namespace

void placeHands(Actors& a, int armor) {
    const World bx = a.x[armor], by = a.y[armor];
    const World mx = a.vx[armor], my = a.vy[armor];     // the mark, if striking
    const ArmorState st = static_cast<ArmorState>(static_cast<uint8_t>(a.state[armor]));
    const uint8_t bz = a.z[armor];
    const bool striking = st == ArmorState::Wind || st == ArmorState::Slam;

    for (int i = 0; i < MAX_ACTORS; ++i) {
        if (a.type[i] != ActType::Gauntlet) continue;
        // actAnim picks which hand this is: 0 left, 1 right.  Only the RIGHT one
        // ever strikes; the left keeps station whatever the torso is doing,
        // which is what makes the wind-up readable.
        const bool right = a.anim[i] != 0;
        if (striking && right) {
            a.x[i] = mx;
            a.y[i] = my;
            // Wound up it rides well clear of the ground; landed it is on it.
            a.z[i] = st == ArmorState::Slam ? uint8_t(0) : uint8_t(GA_HAND_HIGH);
            a.tile[i] = uint8_t(sprite::Gauntlet + 4);      // the closed fist
        } else {
            a.x[i] = right ? bx + GA_HAND_R : bx - GA_HAND_R;
            a.y[i] = by - GA_HAND_UP;
            a.z[i] = bz;
            a.tile[i] = sprite::Gauntlet;                   // the open hand
        }
    }
}

void updateArmor(WorldState& w, SceneView& view, ScreenFx& fx, int slot) {
    Actors& a = view.actors;
    if (a.hitT[slot] != 0) --a.hitT[slot];

    switch (static_cast<ArmorState>(static_cast<uint8_t>(a.state[slot]))) {
        case ArmorState::Walk:
            if (a.timer[slot] != 0) {
                --a.timer[slot];
                stepArmor(a, slot, view.player, view.ground);
                placeHands(a, slot);
                return;
            }
            // The mark is taken HERE, 40 frames before the fist lands.
            a.vx[slot] = a.x[view.player];
            a.vy[slot] = a.y[view.player];
            a.state[slot] = static_cast<ActState>(uint8_t(ArmorState::Wind));
            a.timer[slot] = uint8_t(GA_SLAM_WIND);
            placeHands(a, slot);
            return;

        case ArmorState::Wind:
            if (a.timer[slot] != 0) { --a.timer[slot]; placeHands(a, slot); return; }
            a.state[slot] = static_cast<ActState>(uint8_t(ArmorState::Slam));
            a.timer[slot] = uint8_t(GA_SLAM_HOLD);
            placeHands(a, slot);        // BEFORE the slam, so the fist is down
            armorSlam(w, view, slot);
            return;

        case ArmorState::Slam:
            if (a.timer[slot] != 0) { --a.timer[slot]; placeHands(a, slot); return; }
            a.state[slot] = static_cast<ActState>(uint8_t(ArmorState::Rest));
            a.timer[slot] = uint8_t(GA_REST);
            placeHands(a, slot);
            return;

        case ArmorState::Rest:
            if (a.timer[slot] != 0) { --a.timer[slot]; placeHands(a, slot); return; }
            a.state[slot] = static_cast<ActState>(uint8_t(ArmorState::Walk));
            a.timer[slot] = uint8_t(GA_WALK_LEN);
            // The mark is spent, and these two are a velocity again from here.
            clearVelocity(a, slot);
            placeHands(a, slot);
            return;

        case ArmorState::Drop:
        default:
            break;
    }

    // Still coming down.  The descent IS the timer shifted twice -- there is no
    // separate height, which is why GA_DROP_Z is defined as GA_DROP / 4.
    if (a.timer[slot] != 0) {
        --a.timer[slot];
        a.z[slot] = uint8_t(a.timer[slot] >> 2);
        // Four-frame shake period, from frameCount's bit 1 -- the same shape all
        // four of this game's shakes use, and the reason the trace carries the
        // frame number at all.
        fx.shakeX = (view.frame & 0x02) ? int8_t(-3) : int8_t(3);
        return;
    }
    fx.shakeX = 0;
    a.z[slot] = 0;
    w.hitStop = 8;              // the heaviest freeze in the game: it has landed
    a.state[slot] = static_cast<ActState>(uint8_t(ArmorState::Walk));
    a.timer[slot] = uint8_t(GA_WALK_LEN);
    placeHands(a, slot);
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
            case ActType::Darkside: updateDarkside(w, view, i);     break;
            case ActType::Orb:     updateOrb(w, view, i);           break;
            case ActType::Armor:   updateArmor(w, view, fx, i);     break;
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
// UpdateFish, UpdateMote and UpdateRiku are still on the SNES side only.  All
// three are small and none of them fights: a fish drifts in the shallows, a mote
// rises past Sora during a fall, and Riku only moves during the race, along a
// waypoint list, ignoring terrain entirely (audit finding 9).
//
// The dispatcher above is written so their absence is INERT rather than wrong:
// an actor of a type with no case is simply not updated, which is exactly what
// the assembly's comparison chain does for a prop.  So a scene containing only
// Sora, Shadows and slashes -- the Station of Awakening, the night's search, the
// Second District's wave -- simulates completely, and a scene containing a boss
// simulates everything except the boss.  The trace oracle will say which, at the
// frame it first matters, rather than leaving it to be discovered.

}  // namespace kh
