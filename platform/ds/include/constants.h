// constants.h -- every tuning number, ported from platform/snes/src/game.inc.
//
// The SNES name is preserved in a comment beside each one so a reader can
// cross-reference the assembly and docs/BEHAVIOUR.md without guessing.
//
// Ranges, speeds and positions are `World` (Q12.4).  Times are frames at 60 Hz
// and are plain ints, because both platforms run at 60 Hz and a frame count is
// not a fixed-point quantity.
//
// Nothing SNES-hardware-specific is here: no VRAM addresses, no PPU register
// values, no palette CGRAM layout.  Those belong to the SNES build alone.  The
// sprite and palette identifiers at the bottom are the one exception, and they
// are carried as opaque ids -- see the note there.
//
// DEAD CONSTANTS ARE DELIBERATELY ABSENT.  game.inc defines ORB_SPEED,
// LINE_X, LINE_Y, KNOCKBACK and AF_SOLID, each with exactly one reference in the
// whole codebase: its own definition.  A reimplementer who ports them will wire
// them up and then wonder why nothing changes.  See docs/BEHAVIOUR.md section 11
// for what happens instead of each.

#pragma once

#include "fixed.h"

namespace kh {

// ---------------------------------------------------------------------------
// Geometry.  TILE_PX, TILE_SHIFT, tileOf() and tileCentre() live in fixed.h,
// because the tile lookup is the reason that header exists.
//
// MAP SIZE IS PER-SCENE, not global.  The SNES was locked to one size by its
// tilemap; the DS worlds are larger and differ from each other, so a loaded
// scene carries its own extents and M3 makes the camera clamp read them.  The
// constants below are the two known sizes and a ceiling for pool allocation.
// See docs/WORLD_SIZES.md.
// ---------------------------------------------------------------------------
constexpr int ORACLE_MAP_W = 32;        // MAP_W  the five frozen SNES maps, and
constexpr int ORACLE_MAP_H = 16;        // MAP_H  therefore the trace fixtures

constexpr int DS_ISLAND_W = 64;         // assets/ds/island.txt
constexpr int DS_ISLAND_H = 32;

// The three districts are all one size, because Traverse Town is one place cut
// into three by doors and a district that read as larger than its neighbours
// would read as a different town.  assets/ds/town{1,2,3}.txt.
constexpr int DS_DISTRICT_W = 48;
constexpr int DS_DISTRICT_H = 32;

// Nothing may be larger than this; the collision and height buffers are sized
// from it and a scene loader must reject anything that does not fit.
constexpr int MAP_MAX_W = 64;
constexpr int MAP_MAX_H = 32;
constexpr int MAP_MAX_CELLS = MAP_MAX_W * MAP_MAX_H;

static_assert(DS_ISLAND_W <= MAP_MAX_W && DS_ISLAND_H <= MAP_MAX_H,
              "raise MAP_MAX_* before authoring a map bigger than the buffers");
static_assert(DS_DISTRICT_W <= MAP_MAX_W && DS_DISTRICT_H <= MAP_MAX_H,
              "raise MAP_MAX_* before authoring a map bigger than the buffers");

// A single DS 2D background holds 64x64 characters, which is 32x32 of our
// tiles.  Anything wider must stream; the 3D ground has no such limit.
constexpr int DS_BG_MAX_TILES = 32;

// Kept for the oracle fixtures, whose camera bounds the trace diff compares.
constexpr int WORLD_W = ORACLE_MAP_W * TILE_PX;     // 512 px
constexpr int WORLD_H = ORACLE_MAP_H * TILE_PX;     // 256 px

// The SNES screen was 256x224.  The DS is 256x192, which is a deliberate
// divergence with consequences for the camera -- see
// docs/behaviour/divergences/001-ds-screen-height.md.
constexpr int SCREEN_W = 256;
constexpr int SCREEN_H = 192;               // SNES: 224
constexpr int SNES_SCREEN_H = 224;          // kept, so the oracle diff can explain itself

constexpr int CAM_MAX_X = WORLD_W - SCREEN_W;   // 256, unchanged
constexpr int CAM_MAX_Y = WORLD_H - SCREEN_H;   // 64 on the DS; was 32

// Scenes smaller than the screen pin the camera so the void around them never
// scrolls into view.  Equal low and high bounds mean a fixed camera.
constexpr int DIVE_CAM_X = (WORLD_W - SCREEN_W) / 2;    // DIVE_CAM_X 128
constexpr int DIVE_CAM_Y = (WORLD_H - SCREEN_H) / 2;    // DIVE_CAM_Y; 32 on the DS, was 16
constexpr int FRAG_CAM_X = 128;             // FRAG_CAM_X
constexpr int FRAG_CAM_Y = 24;              // FRAG_CAM_Y -- see the divergence note

// A Station of Awakening is a disc in a void, and "deliberately smaller than the
// screen" stopped being true on the DS: at radius 110 it spans y 18..238, and a
// camera pinned at 32 showing 192 lines sees 32..224 -- clipping the outer golden
// lip and the spoked ring, which are the two things that make it read as glass.
// So the DS draws a smaller one.  This is the first consequence of divergence 001
// that broke something rather than merely widening a clamp.
// See docs/behaviour/divergences/004-ds-station-radius.md.
constexpr int DIVE_CX = 256;                // world pixels
constexpr int DIVE_CY = 128;
constexpr int SNES_DIVE_R = 110;            // DIVE_RX/DIVE_RY, for the oracle
constexpr int DIVE_R = 92;                  // 184 px across, 4 px of void each end
constexpr int DIVE_INSET = 14;              // how far inside the rim a tile centre
                                            // must sit to be standable
static_assert(DIVE_CY - DIVE_R >= DIVE_CAM_Y
              && DIVE_CY + DIVE_R <= DIVE_CAM_Y + SCREEN_H,
              "the station does not fit the screen it is pinned to");
static_assert(DIVE_CX - DIVE_R >= DIVE_CAM_X
              && DIVE_CX + DIVE_R <= DIVE_CAM_X + SCREEN_W,
              "the station does not fit the screen it is pinned to");

constexpr int MAX_STEP = 1;             // MAX_STEP  height steps a move may cross

// ---------------------------------------------------------------------------
// The dialogue box
//
// Geometry in characters, from text.inc.  The box's PLACEMENT on screen is a
// rendering decision and belongs to the device tier -- and on the DS it is a
// different decision anyway, because the box shares the bottom screen with the
// HUD instead of sitting over the world.  What the interpreter needs is only how
// many characters fit on a line and how many lines fit before a page break.
// ---------------------------------------------------------------------------
constexpr int TEXT_W = 28;              // TEXT_W  characters per line
constexpr int TEXT_H = 5;               // TEXT_H  lines before the box must page
constexpr int REVEAL_DELAY = 1;         // REVEAL_DELAY  one character every 2 frames
constexpr int REVEAL_BACKSTOP = 512;    // iterations RevealAll gives up after

// Glyph numbers in the 2 bpp font page, in the order build_assets.py emits them.
constexpr uint8_t CH_BLANK = 0;
constexpr uint8_t CH_A = 1;             // A..Z run from here
constexpr uint8_t CH_0 = 27;            // 0..9 run from here
constexpr uint8_t CH_DOT = 37;
constexpr uint8_t CH_COMMA = 38;
constexpr uint8_t CH_BANG = 39;
constexpr uint8_t CH_QUERY = 40;
constexpr uint8_t CH_APOS = 41;
constexpr uint8_t CH_DASH = 42;
constexpr uint8_t CH_COLON = 43;
constexpr uint8_t CH_SLASH = 44;
constexpr uint8_t CH_CURSOR = 45;       // menu selection arrow
constexpr uint8_t CH_ADVANCE = 46;      // "press a button" indicator

// ---------------------------------------------------------------------------
// Actor pool
//
// The SNES held 32.  That was never an OAM limit -- an ordinary actor is a
// single 32x32 sprite and only an AF_HUGE boss takes four, so a full SNES pool
// used about 35 of its 128 entries.  It was a WRAM and cycle limit: sixteen
// parallel arrays in the low bank, updated by a 3.58 MHz CPU every frame.
//
// Neither limit is the DS's.  The pool is ~3 KB of a 4 MB machine and the ARM9
// runs at 33 MHz, so what actually bounds it is the OBJ budget below -- and that
// bounds what is ON SCREEN, not what exists.  This is a divergence and not a
// free upgrade: see docs/behaviour/divergences/002-ds-actor-pool.md.
//
// It is also not optional.  The expanded island asks for 69 prop actors before
// a single person or item is placed on it, so at 32 the DS worlds cannot be
// populated at all.
// ---------------------------------------------------------------------------
constexpr int MAX_ACTORS = 128;         // SNES: MAX_ACTORS = 32
constexpr int SNES_MAX_ACTORS = 32;     // kept, so the oracle diff can explain itself
constexpr int TRANSIENT_ACTORS = 6;     // TRANSIENT_ACTORS  slack for spawned effects

// One OAM entry per actor, and the main engine has 128 of them -- the sub screen
// has its own 128 for the HUD, so the two do not compete.  A cast table may not
// put more than OBJ_BUDGET_SCENERY objects inside one camera window; the rest is
// held back for the transients, for Sora, and for a boss's four quadrants.
// tools/check_map.py slides a window over every map and enforces it.
constexpr int MAX_OBJECTS = 128;        // per 2D engine, so 256 across both screens
constexpr int OBJ_BUDGET_SCENERY = 96;
static_assert(OBJ_BUDGET_SCENERY + TRANSIENT_ACTORS + 4 + 1 <= MAX_OBJECTS,
              "leave room for the transients, a four-quadrant boss and Sora");

// ---------------------------------------------------------------------------
// Interaction ranges -- half-extents of a box around a point.  A tile is 256 in
// these units, so 384 reaches a tile and a half.
//
// Which range applies is decided by the actor's type alone, and the SELECTION
// RULE differs per interaction: pickups, swings and conversation take the FIRST
// match in slot order, while looking at a wall prop takes the NEAREST by
// Manhattan distance.  See docs/BEHAVIOUR-AUDIT.md finding 11.
// ---------------------------------------------------------------------------
constexpr World TALK_X = World::fromRaw(384);   // TALK_X   press A near a person
constexpr World TALK_Y = World::fromRaw(384);
constexpr World PICK_X = World::fromRaw(224);   // PICK_X   walked into
constexpr World PICK_Y = World::fromRaw(224);
constexpr World SWING_X = World::fromRaw(544);  // SWING_X  answered with the Keyblade
constexpr World SWING_Y = World::fromRaw(544);
constexpr World PROP_X = World::fromRaw(416);   // PROP_X   examined on a wall
constexpr World PROP_Y = World::fromRaw(416);
constexpr World REACH_X = World::fromRaw(448);  // REACH_X (dive.s) the dream weapons
constexpr World REACH_Y = World::fromRaw(448);

// The swing arc is centred just off Sora's body rather than at arm's length, so
// an enemy pressed against him is still inside it.  Do not move it outward.
constexpr World ATK_REACH_X = World::fromRaw(272);  // ATK_REACH_X (world.s)
constexpr World ATK_REACH_Y = World::fromRaw(272);
constexpr World ATK_OFS_CARDINAL = World::fromRaw(256);  // atkOfsX/Y, cardinal
constexpr World ATK_OFS_DIAGONAL = World::fromRaw(176);  // ...and diagonal

// TOUCH is NOT a symmetric box.  HeartlessTouchTest halves abs(dx) with a bare
// `lsr` before comparing, so a Heartless reaches 320 horizontally and 160
// vertically.  Darkside's fist uses the raw 160 x 160 with no halving.
// docs/BEHAVIOUR.md section 2 and docs/BEHAVIOUR-AUDIT.md findings 1-3.
constexpr World TOUCH_X = World::fromRaw(160);      // TOUCH_X (world.s)
constexpr World TOUCH_Y = World::fromRaw(160);
constexpr World HEART_TOUCH_X = World::fromRaw(320);    // the effective reach: TOUCH_X * 2
constexpr World HEART_TOUCH_Y = World::fromRaw(160);

// The Heartless AI deadzone.  Inside it an axis contributes nothing -- and if
// BOTH axes are inside, the Shadow neither moves nor tests for contact, which is
// why it can never damage point-blank.
constexpr World HEART_DEAD_X = World::fromRaw(208);    // HEART_DEAD_X (world.s)
constexpr World HEART_DEAD_Y = World::fromRaw(208);

// ---------------------------------------------------------------------------
// Sora
// ---------------------------------------------------------------------------
constexpr int SORA_MAX_HP = 20;         // SORA_MAX_HP
constexpr World WALK_SPEED = World::fromRaw(24);    // WALK_SPEED  1.5 px/frame
constexpr int ATTACK_FRAMES = 18;       // ATTACK_FRAMES  but control is withheld
                                        // for frames 0..18 inclusive = 19
constexpr int ATK_ACTIVE = 12;          // ATK_ACTIVE  the one frame that connects
constexpr int HURT_FRAMES = 20;         // HURT_FRAMES
constexpr int DEATH_FRAMES = 50;        // DEATH_FRAMES  brightness 12 down to 3
constexpr int KNOCKBACK_SHIFT = 1;      // both knockback sites use `asl a`, i.e. x2

// Eight facings.  Only S, SE, E, NE, N are drawn; W, SW, NW reuse the eastern
// art mirrored.  The order is load-bearing: the velocity tables index by it.
enum class Dir : uint8_t {
    S = 0, SE = 1, E = 2, NE = 3, N = 4, NW = 5, W = 6, SW = 7, Count = 8
};

// dirVelX / dirVelY, verbatim from world.s.  A diagonal is the cardinal scaled
// by roughly 1/sqrt(2), rounded to whole Q12.4 units.
constexpr World DIR_VEL_X[8] = {
    World::fromRaw(  0), World::fromRaw( 17), World::fromRaw( 24), World::fromRaw( 17),
    World::fromRaw(  0), World::fromRaw(-17), World::fromRaw(-24), World::fromRaw(-17),
};
constexpr World DIR_VEL_Y[8] = {
    World::fromRaw( 24), World::fromRaw( 17), World::fromRaw(  0), World::fromRaw(-17),
    World::fromRaw(-24), World::fromRaw(-17), World::fromRaw(  0), World::fromRaw( 17),
};

// ---------------------------------------------------------------------------
// Shadow Heartless
// ---------------------------------------------------------------------------
constexpr int HEART_MAX_HP = 3;         // HEART_MAX_HP
constexpr int HEART_ANIM_FRAMES = 10;   // the timer is set to 10 and counted to
                                        // zero, so a cel lasts 11 frames
constexpr int HEART_RECOIL_FRAMES = 14; // and is NOT invulnerable, unlike a boss
constexpr int HEART_SPEED_SHIFT = 1;    // dirVel >> 1

// ---------------------------------------------------------------------------
// Darkside -- the Dive boss.  It never walks.
// ---------------------------------------------------------------------------
constexpr int DS_MAX_HP = 36;           // DS_MAX_HP
constexpr int DS_SLAM_WIND = 44;        // DS_SLAM_WIND  the mark is taken at the
                                        // START of this, not the end
constexpr int DS_SLAM_HOLD = 22;        // DS_SLAM_HOLD  fist down, vulnerable
constexpr int DS_REST = 56;             // DS_REST  also its opening timer
constexpr int DS_ORB_WIND = 40;         // DS_ORB_WIND
constexpr int DS_ORB_REST = 30;         // DS_ORB_REST
constexpr int DS_SWEEP_WIND = 26;       // DS_SWEEP_WIND  shorter than the fist's
constexpr int DS_SWEEP_HOLD = 16;       // DS_SWEEP_HOLD
constexpr int ORB_LIFE = 110;           // ORB_LIFE
constexpr int BOSS_FLINCH_FRAMES = 10;  // HurtBoss; invulnerable throughout

constexpr World DS_HURT_X = World::fromRaw(576);    // DS_HURT_X  centred on its feet
constexpr World DS_HURT_Y = World::fromRaw(640);    // DS_HURT_Y  deeper than the sweep
constexpr World SWEEP_X = World::fromRaw(704);      // SWEEP_X  wide and shallow
constexpr World SWEEP_Y = World::fromRaw(448);      // SWEEP_Y
constexpr World SWEEP_ARC_OFS[3] = {                // sweepOfs, the three arcs
    World::fromRaw(-384), World::fromRaw(0), World::fromRaw(384),
};
constexpr World ORB_MUZZLE_UP = World::fromRaw(640);    // 40 px above its feet

enum class BossState : uint8_t {
    Rest = 0, SlamUp = 1, SlamHit = 2, OrbUp = 3, OrbFire = 4,
    SweepUp = 5, SweepHit = 6,
};

// ---------------------------------------------------------------------------
// The Guard Armor -- the Traverse Town boss.  Tracks sideways ONLY; see
// docs/BEHAVIOUR.md section 5 for why that is a fix and not a limitation.
// ---------------------------------------------------------------------------
constexpr int GA_MAX_HP = 36;           // GA_MAX_HP  same gauge as Darkside
constexpr World GA_WALK = World::fromRaw(20);       // GA_WALK  Sora walks 24
constexpr World GA_STOP = World::fromRaw(512);      // GA_STOP  two tiles
constexpr World GA_HURT_X = World::fromRaw(640);    // GA_HURT_X
constexpr World GA_HURT_Y = World::fromRaw(704);    // GA_HURT_Y
constexpr World GA_HAND_R = World::fromRaw(640);    // GA_HAND_R  hand station, out
constexpr World GA_HAND_UP = World::fromRaw(448);   // GA_HAND_UP  ...and up
constexpr World GA_HAND_X = World::fromRaw(448);    // GA_HAND_X  what a fist hurts within
constexpr World GA_HAND_Y = World::fromRaw(384);    // GA_HAND_Y
constexpr int GA_HAND_HIGH = 10;        // GA_HAND_HIGH  height steps, wound up
constexpr int GA_SLAM_WIND = 40;        // GA_SLAM_WIND  mark taken at its START
constexpr int GA_SLAM_HOLD = 20;        // GA_SLAM_HOLD
constexpr int GA_REST = 50;             // GA_REST
constexpr int GA_WALK_LEN = 96;         // GA_WALK_LEN
constexpr int GA_DROP = 40;             // GA_DROP
constexpr int GA_DROP_Z = GA_DROP / 4;  // GA_DROP_Z  the descent is timer >> 2

enum class ArmorState : uint8_t {
    Drop = 0, Walk = 1, Wind = 2, Slam = 3, Rest = 4,
};

// ---------------------------------------------------------------------------
// Scenes and their stage machines
// ---------------------------------------------------------------------------
enum class SceneId : uint8_t {
    Dive = 0, Dive2 = 1, Dive3 = 2,
    Island = 3, Night = 4, Fragment = 5,
    Town1 = 6, Town2 = 7, Town3 = 8,
    Count = 9,
};

enum class DiveStage : uint8_t {
    Intro = 0, Pick = 1, Drop = 2, Shatter = 3,
    S2Intro = 4, S2Fight = 5, Shatter2 = 6,
    S3Intro = 7, Boss = 8, Done = 9,
    Fall = 10, Fade = 11, Arrived = 12,
};

// The dream weapon choice has NO mechanical consequence: weaponTaken and
// weaponGiven are each written once and read nowhere.  Kept because the script
// branches on them for dialogue.  Do not invent stat effects.
enum class Weapon : uint8_t { None = 0, Sword = 1, Shield = 2, Staff = 3 };

enum class QuestState : uint8_t {
    Idle = 0, Active = 1, Done = 2, DayOut = 3, DayIn = 4,
    RaceSet = 5, RaceRun = 6, RaceOver = 7,
    Naming = 8, Named = 9, Dusk = 10,
};

enum class NightStage : uint8_t {
    Intro = 0, Seek = 1, Riku = 2, Key = 3, Kairi = 4,
    Door = 5, Tear = 6, Boss = 7, End = 8, Over = 9,
};

enum class TownStage : uint8_t {
    Arrive = 0, Look = 1, Second = 2, Third = 3,
    Meet = 4, Boss = 5, Won = 6, Over = 7,
};

// ...and the raft's name, which also only selects a confirmation line.
enum class RaftName : uint8_t { None = 0, Highwind = 1, Excalibur = 2, Ragnarok = 3 };

// ---------------------------------------------------------------------------
// Scene timings
// ---------------------------------------------------------------------------
constexpr int FADE_LEN = 64;            // FADE_LEN  scene swaps at the halfway point
constexpr int FALL_LEN = 170;           // FALL_LEN
constexpr int MOTE_LIFE = 40;           // MOTE_LIFE
constexpr World MOTE_RISE = World::fromRaw(96);     // MOTE_RISE  6 px a frame
constexpr int SHATTER_LEN = 96;         // SHATTER_LEN  reaches mosaic 12, brightness 3
constexpr int TEAR_LEN = 120;           // TEAR_LEN     reaches mosaic 15, brightness 0
constexpr int END_FADE = 62;            // END_FADE

constexpr int DAY_FADE = 30;            // DAY_FADE  each half of the day change
constexpr int COUNT_LEN = 192;          // COUNT_LEN  64 frames to a number
constexpr World FISH_SWIM = World::fromRaw(16);     // FISH_SWIM  they drift
constexpr World RIKU_VX = World::fromRaw(17);       // RIKU_VX
constexpr World RIKU_VY = World::fromRaw(17);       // RIKU_VY
constexpr World RIKU_NEAR = World::fromRaw(40);     // RIKU_NEAR  waypoint tolerance
constexpr World TAG_X = World::fromRaw(448);        // TAG_X  used for BOTH race legs
constexpr World TAG_Y = World::fromRaw(448);
constexpr int RACE_WPS = 20;            // RACE_WPS

// The night
//
// SHADOW_MAX was one number on the SNES because the night and the fragment ran
// on maps of similar size.  They no longer do: the island is four times what it
// was and the fragment is deliberately untouched, so one constant would either
// leave the island deserted or bury the fragment.
//
// The island's figure preserves PER-SCREEN density rather than the count.  Six
// Shadows over 196 walkable tiles is one per 33; 670 walkable tiles at the same
// density is 20.  That matters because a DS screen shows 17x13 tiles -- about
// 15% of the expanded island -- so six spread over the whole of it means
// typically ONE on screen, and the night's tension is being hunted.
//
// SHADOW_GAP scales with the same argument, not with the count: the fill should
// still take about as long as crossing the map. The island is twice as wide and
// twice as tall, so ~2x the SNES's 420 frames, which at 20 alive is a gap of 42.
//
// BOTH of these are balance and neither has been played on hardware.  Treat them
// as the derivation's answer, not as measured -- see
// docs/behaviour/divergences/003-ds-night-density.md.
constexpr int SNES_SHADOW_MAX = 6;      // SHADOW_MAX, for the oracle fixtures
constexpr int SHADOW_MAX_NIGHT = 20;    // the expanded island
constexpr int SHADOW_MAX_FRAG = 6;      // unchanged, because the map is unchanged
constexpr int SHADOW_GAP = 42;          // SHADOW_GAP was 70
constexpr int SNES_SHADOW_GAP = 70;

// The spot table is DATA now -- assets/gen/ds/nightspots.bin, terminated -- so
// the count comes from the file and this is only the buffer it is read into.
// The island carries 35 of them against the SNES's 10, because spots are where
// the Shadows come up and four times the ground needs four times the places or
// they all arrive in the same corner.
constexpr int SNES_NIGHT_SPOTS = 10;    // NIGHT_SPOTS, for the oracle fixtures
constexpr int MAX_SPOTS = 64;
static_assert(SHADOW_MAX_NIGHT <= MAX_SPOTS,
              "more Shadows alive than there are places for them to come up");
constexpr int DARK_HOLD = 96;           // DARK_HOLD
constexpr int FLASH_LEN = 8;            // FLASH_LEN
constexpr int FLASH_GAP_MIN = 150;      // FLASH_GAP_MIN
constexpr int FLASH_GAP_VAR = 127;      // FLASH_GAP_VAR  plus a random 0..127

// Traverse Town
constexpr int TOWN_SHADOWS = 5;         // TOWN_SHADOWS  alive at once
constexpr int TOWN_WAVE = 8;            // TOWN_WAVE  arrivals before the way opens
constexpr int TOWN_GAP = 80;            // TOWN_GAP
constexpr int TOWN_SPOTS = 8;           // TOWN_SPOTS
constexpr int DOOR_ROW = 4;             // DOOR_ROW  every district door is in row 4
constexpr int DOOR_FADE = 30;           // DOOR_FADE  each half of a transition
constexpr int FALL_WAIT = 70;           // FALL_WAIT
constexpr int FALL_DROP = 24;           // FALL_DROP
constexpr int FALL_Z = 12;              // FALL_Z  height steps

// Screen shake: a signed pixel offset alternating on (frameCount & 2), so a
// 4-frame period.  The amplitudes differ per effect.
constexpr int SHAKE_PERIOD_MASK = 2;
constexpr int SHAKE_SHATTER = 2;        // the Dive's platform
constexpr int SHAKE_TEAR = 3;           // the island coming apart
constexpr int SHAKE_ARMOR_LAND = 3;
constexpr int SHAKE_PAIR_LAND = 2;

// ---------------------------------------------------------------------------
// Kairi's lists.  itemCount is indexed by (actor type - Log), so a pickup
// tallies itself with no lookup table.
// ---------------------------------------------------------------------------
enum class Item : uint8_t {
    Log = 0, Cloth = 1, Rope = 2, Mush = 3,
    Nut = 4, Egg = 5, Water = 6, Fish = 7, Count = 8,
};

constexpr int NEED[static_cast<int>(Item::Count)] = {
    2,  // NEED_LOGS
    1,  // NEED_CLOTH
    1,  // NEED_ROPE
    3,  // NEED_MUSH
    2,  // NEED_NUT
    1,  // NEED_EGG
    1,  // NEED_WATER
    3,  // NEED_FISH
};

// ---------------------------------------------------------------------------
// The RNG.  A 16-bit Galois LFSR: shift left, and xor 0x002D on carry out.  The
// seeds matter -- reproduce them exactly or the Heartless arrive in different
// places and the oracle diff in M6 is worthless.
// ---------------------------------------------------------------------------
constexpr uint16_t LFSR_TAP = 0x002D;
constexpr uint16_t RNG_SEED_WORLD = 0xACE1;     // InitWorld
constexpr uint16_t RNG_SEED_TOWN = 0x1D57;      // TownBegin

// ---------------------------------------------------------------------------
// Sprite and palette identifiers.
//
// These numbers are SNES sprite-page offsets and OBJ palette slots.  They are
// meaningless as addresses on the DS, and are carried here as OPAQUE IDENTIFIERS
// only, because the type tables in actor.h index by them and because keeping the
// values lets any divergence be traced straight back to the assembly.  The DS
// renderer maps (id, page) pairs to its own atlas; it must not treat them as
// offsets into anything.
//
// AF_PAGE1 selects the second of two 256-tile pages, so an identifier is only
// unique together with that flag.
// ---------------------------------------------------------------------------
namespace sprite {
// page 0
constexpr uint8_t Sora = 0x00, Palm = 0x04, RockBig = 0x08, ShadowBig = 0x0C;
constexpr uint8_t Pedestal = 0x40, Sword = 0x44, Shield = 0x48, Staff = 0x4C;
constexpr uint8_t Darkside = 0x80, Orb = 0x88, Streak = 0x8C;
constexpr uint8_t Heart0 = 0xC8, HeartNight = 0xA8;
constexpr uint8_t Shadow = 0xE8, Slash0 = 0xEA, Rock = 0xEE;
// page 1, islanders
constexpr uint8_t Kairi = 0x00, Riku = 0x04, Tidus = 0x08, Selphie = 0x0C;
constexpr uint8_t Wakka = 0x40, Door = 0x44;
constexpr uint8_t Log = 0x48, Cloth = 0x4A, Rope = 0x4C, Mush = 0x4E;
constexpr uint8_t Fish = 0x68, Coconut = 0x6C, Egg = 0x6E;
constexpr uint8_t Bottle = 0x80, Faces = 0x84, Scribble = 0x88, DoorOpen = 0x8C;
constexpr uint8_t Dark = 0xC0;
// page 1, the town's cast in place of the islanders
constexpr uint8_t Cid = 0x00, TownMan = 0x04, TownWoman = 0x08, Lamp = 0x0C;
constexpr uint8_t Donald = 0x40, Goofy = 0x44, Gauntlet = 0x48, Armor = 0x80;
}  // namespace sprite

namespace pal {
constexpr uint8_t Sora = 0, Heart = 1, Scene = 2, Fx = 3, Shadow = 4, Dive = 5;
// The islanders must live below 4 or SNES OBJ colour math would half-add them
// into the sea, so they take slot 1 -- nothing on the island is a Heartless.
// The town's cast does the same, with the Heartless colours in three spare
// slots of the same palette.
constexpr uint8_t Isle = 1;
}  // namespace pal

}  // namespace kh
