// actor.h -- the actor table, ported from platform/snes/src/ram.s and world.s.
//
// A STRUCTURE OF ARRAYS, not an array of structs, and that is not inherited
// superstition.  On the 65816 it turned every field access into abs,X with a
// constant base; on the ARM9 it keeps a sweep over one field inside one cache
// line instead of striding a 24-byte record.  Every loop in the engine walks one
// or two fields across all actors, which is exactly the access pattern this
// layout is for.
//
// The numeric values of ActType are LOAD-BEARING.  The engine asks "is this
// something I can walk into" and "is this somebody who lives here" as range
// comparisons on the type id, so the ordering is part of the design and the
// static_asserts below pin it.

#pragma once

#include "constants.h"

namespace kh {

// ---------------------------------------------------------------------------
// Actor types.  Consecutive runs are deliberate -- see the range predicates.
// ---------------------------------------------------------------------------
enum class ActType : uint8_t {
    None = 0,
    Sora = 1,
    Shadow = 2,             // Shadow Heartless
    Palm = 3,
    RockBig = 4,
    Rock = 5,
    Slash = 6,              // transient keyblade arc
    Pedestal = 7,           // Station of Awakening dais
    Sword = 8,              // the three dream weapons hovering above them
    Shield = 9,
    Staff = 10,
    Darkside = 11,
    Orb = 12,               // dark orb from its chest
    Mote = 13,              // rising light during the fall

    // The five islanders are consecutive, so one range check covers
    // "is this somebody I can talk to".
    Kairi = 14,
    Riku = 15,
    Tidus = 16,
    Selphie = 17,
    Wakka = 18,

    // Everything taken by walking into it, in one range.  Their order also
    // indexes Item, so a pickup tallies itself with no lookup table.
    Log = 19,
    Cloth = 20,
    Rope = 21,
    Mush = 22,
    Coconut = 23,
    Egg = 24,
    Bottle = 25,

    // ...and the two that answer to the Keyblade instead.
    Fish = 26,
    PalmC = 27,             // a palm still carrying its coconuts

    // What is on the wall of the Secret Place, in one range so a single scan
    // covers "is this something Sora can look at".
    Door = 28,
    Faces = 29,
    Scribble = 30,

    // The night.  NOTE: DoorOpen is 31, which falls OUTSIDE the Door..Scribble
    // range above -- so once the door has opened it can no longer be examined.
    // That is the shipped behaviour; see docs/BEHAVIOUR-AUDIT.md finding 13.
    DoorOpen = 31,
    Dark = 32,              // a column of darkness standing on the ground

    // Traverse Town.  The three residents are consecutive.
    Cid = 33,
    TownMan = 34,
    TownWoman = 35,
    Lamp = 36,              // a street lamp, so somebody can walk behind it
    Donald = 37,
    Goofy = 38,
    Armor = 39,             // the Guard Armor, 64x64
    Gauntlet = 40,          // ...and one of its hands

    Count = 41,
};

constexpr int ACT_TYPE_COUNT = static_cast<int>(ActType::Count);

// The range predicates the engine actually performs.  Written as functions so
// the boundaries live in one place.
constexpr bool isIslander(ActType t) {
    return t >= ActType::Kairi && t <= ActType::Wakka;
}
constexpr bool isPickup(ActType t) {
    return t >= ActType::Log && t <= ActType::Bottle;
}
constexpr bool isWallProp(ActType t) {
    return t >= ActType::Door && t <= ActType::Scribble;
}
constexpr bool isResident(ActType t) {
    return t >= ActType::Cid && t <= ActType::TownWoman;
}
constexpr bool isDreamWeapon(ActType t) {
    return t >= ActType::Sword && t <= ActType::Staff;
}
// A pickup tallies itself: the slot is the type's offset from Log.
constexpr Item itemOf(ActType t) {
    return static_cast<Item>(static_cast<int>(t) - static_cast<int>(ActType::Log));
}

static_assert(static_cast<int>(ActType::Wakka) - static_cast<int>(ActType::Kairi) == 4,
              "five islanders, consecutive: one range check covers talking");
static_assert(static_cast<int>(ActType::Bottle) - static_cast<int>(ActType::Log) == 6,
              "seven walk-into pickups, consecutive and aligned with Item");
static_assert(itemOf(ActType::Bottle) == Item::Water, "pickup order indexes Item");
static_assert(itemOf(ActType::Log) == Item::Log, "pickup order indexes Item");
static_assert(static_cast<int>(ActType::Scribble) - static_cast<int>(ActType::Door) == 2,
              "three wall props, consecutive");
static_assert(!isWallProp(ActType::DoorOpen),
              "the opened door leaves the examinable range -- shipped behaviour");
static_assert(static_cast<int>(ActType::TownWoman) - static_cast<int>(ActType::Cid) == 2,
              "three town residents, consecutive");

// ---------------------------------------------------------------------------
// Sora's state machine
// ---------------------------------------------------------------------------
enum class ActState : uint8_t {
    Idle = 0,
    Walk = 1,
    Attack = 2,
    Hurt = 3,
    Dead = 4,               // out of HP; no control until the retry
    Fall = 5,               // carried by the scene, not the player
};

// ---------------------------------------------------------------------------
// actFlags bits
// ---------------------------------------------------------------------------
enum class ActFlags : uint8_t {
    None = 0x00,
    Large = 0x01,           // 32x32 sprite, otherwise 16x16
    Shadow = 0x02,          // drop a ground shadow
    // 0x04 was AF_SOLID.  Dead: nothing tests it and no actor blocks movement.
    HFlip = 0x08,
    Talk = 0x10,
    Huge = 0x20,            // 64x64, emitted as four sprites
    Page1 = 0x40,           // the sprite id refers to the second page
    Flat = 0x80,            // hung on a wall: height forced to 0 wherever placed
};

constexpr ActFlags operator|(ActFlags a, ActFlags b) {
    return static_cast<ActFlags>(static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
}
constexpr bool has(ActFlags f, ActFlags bit) {
    return (static_cast<uint8_t>(f) & static_cast<uint8_t>(bit)) != 0;
}
constexpr ActFlags without(ActFlags f, ActFlags bit) {
    return static_cast<ActFlags>(static_cast<uint8_t>(f) & ~static_cast<uint8_t>(bit));
}

// ---------------------------------------------------------------------------
// The type tables, verbatim from world.s.  A type added without a row in every
// one of them would spawn with whatever byte follows, which is what the
// assembler asserts over there and what the static_asserts do here.
//
// Tile and palette values are opaque identifiers -- see constants.h.
// ---------------------------------------------------------------------------
namespace detail {

using F = ActFlags;

constexpr uint8_t typeTile[ACT_TYPE_COUNT] = {
    0x00,
    sprite::Sora, sprite::Heart0, sprite::Palm, sprite::RockBig, sprite::Rock,
    sprite::Slash0,
    sprite::Pedestal, sprite::Sword, sprite::Shield, sprite::Staff,
    sprite::Darkside,
    sprite::Orb,                    // 12
    sprite::Streak,                 // 13, the mote
    sprite::Kairi, sprite::Riku, sprite::Tidus, sprite::Selphie, sprite::Wakka,
    sprite::Log, sprite::Cloth, sprite::Rope, sprite::Mush, sprite::Coconut,
    sprite::Egg, sprite::Bottle, sprite::Fish,
    sprite::Palm,                   // 27, a coconut palm reuses the palm's art
    sprite::Door, sprite::Faces, sprite::Scribble,
    sprite::DoorOpen, sprite::Dark,
    sprite::Cid, sprite::TownMan, sprite::TownWoman, sprite::Lamp,
    sprite::Donald, sprite::Goofy, sprite::Armor, sprite::Gauntlet,
};

constexpr uint8_t typePal[ACT_TYPE_COUNT] = {
    0x00,
    pal::Sora, pal::Heart, pal::Scene, pal::Scene, pal::Scene, pal::Fx,
    pal::Dive, pal::Dive, pal::Dive, pal::Dive, pal::Heart,
    pal::Heart,                     // 12
    pal::Fx,                        // 13
    pal::Isle, pal::Isle, pal::Isle, pal::Isle, pal::Isle,
    pal::Isle, pal::Isle, pal::Isle, pal::Isle, pal::Isle,
    pal::Isle, pal::Isle, pal::Isle,
    pal::Scene,                     // 27, drawn as an ordinary palm
    pal::Isle, pal::Isle, pal::Isle,
    pal::Isle, pal::Isle,
    pal::Isle, pal::Isle, pal::Isle, pal::Isle,
    pal::Isle, pal::Isle,
    pal::Scene, pal::Scene,         // the Guard Armor and its hands
};

constexpr ActFlags typeFlags[ACT_TYPE_COUNT] = {
    F::None,
    F::Large | F::Shadow,                       // Sora
    F::Shadow,                                  // Shadow
    F::Large | F::Shadow,                       // Palm
    F::Large | F::Shadow,                       // RockBig
    F::Shadow,                                  // Rock
    F::None,                                    // Slash
    F::Large | F::Shadow,                       // Pedestal
    F::Large | F::Talk,                         // Sword -- weapons hover, no shadow
    F::Large | F::Talk,                         // Shield
    F::Large | F::Talk,                         // Staff
    F::Huge | F::Shadow,                        // Darkside
    F::None,                                    // Orb -- floats
    F::None,                                    // Mote -- pure light
    F::Large | F::Shadow | F::Talk | F::Page1,  // Kairi
    F::Large | F::Shadow | F::Talk | F::Page1,  // Riku
    F::Large | F::Shadow | F::Talk | F::Page1,  // Tidus
    F::Large | F::Shadow | F::Talk | F::Page1,  // Selphie
    F::Large | F::Shadow | F::Talk | F::Page1,  // Wakka
    F::Shadow | F::Page1,                       // Log
    F::Shadow | F::Page1,                       // Cloth
    F::Shadow | F::Page1,                       // Rope
    F::Shadow | F::Page1,                       // Mush
    F::Shadow | F::Page1,                       // Coconut
    F::Shadow | F::Page1,                       // Egg
    F::Shadow | F::Page1,                       // Bottle
    F::Page1,                                   // Fish -- floats in the shallows
    F::Large | F::Shadow,                       // PalmC
    F::Large | F::Talk | F::Page1 | F::Flat,    // Door -- hung on the cave wall
    F::Large | F::Talk | F::Page1 | F::Flat,    // Faces
    F::Large | F::Talk | F::Page1 | F::Flat,    // Scribble
    F::Large | F::Talk | F::Page1 | F::Flat,    // DoorOpen
    F::Large | F::Page1,                        // Dark -- casts no shadow
    F::Large | F::Shadow | F::Talk | F::Page1,  // Cid
    F::Large | F::Shadow | F::Talk | F::Page1,  // TownMan
    F::Large | F::Shadow | F::Talk | F::Page1,  // TownWoman
    F::Large | F::Shadow | F::Page1,            // Lamp
    F::Large | F::Shadow | F::Talk | F::Page1,  // Donald
    F::Large | F::Shadow | F::Talk | F::Page1,  // Goofy
    F::Huge | F::Shadow | F::Page1,             // Armor
    F::Large | F::Page1,                        // Gauntlet
};

constexpr uint8_t typeHP[ACT_TYPE_COUNT] = {
    0,
    SORA_MAX_HP, HEART_MAX_HP, 0, 0, 0, 0,
    0, 0, 0, 0, DS_MAX_HP,
    0,
    0,
    0, 0, 0, 0, 0,
    0, 0, 0, 0, 0,
    0, 0, 0, 0,
    0, 0, 0,
    0, 0,
    0, 0, 0, 0, 0, 0, GA_MAX_HP, 0,
};

}  // namespace detail

constexpr uint8_t tileFor(ActType t) { return detail::typeTile[static_cast<int>(t)]; }
constexpr uint8_t palFor(ActType t) { return detail::typePal[static_cast<int>(t)]; }
constexpr ActFlags flagsFor(ActType t) { return detail::typeFlags[static_cast<int>(t)]; }
constexpr uint8_t hpFor(ActType t) { return detail::typeHP[static_cast<int>(t)]; }

// Spot-checks that would catch a row inserted in the wrong place -- the failure
// mode the assembler's own assert exists to prevent.
static_assert(hpFor(ActType::Sora) == SORA_MAX_HP, "type tables are misaligned");
static_assert(hpFor(ActType::Darkside) == DS_MAX_HP, "type tables are misaligned");
static_assert(hpFor(ActType::Armor) == GA_MAX_HP, "type tables are misaligned");
static_assert(hpFor(ActType::Gauntlet) == 0, "a hand has no HP of its own");
static_assert(has(flagsFor(ActType::Darkside), ActFlags::Huge), "emitted as four sprites");
static_assert(has(flagsFor(ActType::Armor), ActFlags::Huge), "emitted as four sprites");
static_assert(has(flagsFor(ActType::Faces), ActFlags::Flat), "hung on a wall");
static_assert(!has(flagsFor(ActType::Fish), ActFlags::Shadow), "floats in the shallows");
static_assert(tileFor(ActType::PalmC) == sprite::Palm, "drawn as an ordinary palm");
static_assert(palFor(ActType::PalmC) == pal::Scene, "...with the scenery palette");

// ---------------------------------------------------------------------------
// The table itself
// ---------------------------------------------------------------------------
struct Actors {
    // Field for field with ram.s.  `type == None` means the slot is free.
    ActType  type[MAX_ACTORS];
    uint8_t  z[MAX_ACTORS];         // ground height in eight-pixel steps
    World    x[MAX_ACTORS];
    World    y[MAX_ACTORS];
    World    vx[MAX_ACTORS];
    World    vy[MAX_ACTORS];
    Dir      dir[MAX_ACTORS];
    uint8_t  anim[MAX_ACTORS];
    uint8_t  animT[MAX_ACTORS];
    ActState state[MAX_ACTORS];
    uint8_t  timer[MAX_ACTORS];
    uint8_t  hp[MAX_ACTORS];
    ActFlags flags[MAX_ACTORS];
    uint8_t  tile[MAX_ACTORS];
    uint8_t  pal[MAX_ACTORS];
    uint8_t  hitT[MAX_ACTORS];

    void clear();

    // Claim a free slot.  Returns the index, or -1 when the table is full --
    // which the SNES build signalled with a clear carry that one caller ignored,
    // silently dropping the last spawn in its table.  Callers here must check.
    int spawn(ActType t, World px, World py);

    int count(ActType t) const;
};

}  // namespace kh
