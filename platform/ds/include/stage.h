#pragma once
// The scene stage machines.
//
// Each scene's script is one byte advanced by one routine, and the stages are
// the specification of the opening's pacing.  docs/BEHAVIOUR.md §6 gives the
// diagrams and the frame counts; BEHAVIOUR-AUDIT.md findings 7, 8, 13-16, 34-36
// correct and complete them, and where the two disagree the audit wins.
//
// A machine here is PURE LOGIC.  It reads the world, advances its stage and its
// timers, writes the frame's screen effects, and returns an ACTION for the
// caller to perform.  It never loads a scene, opens a dialogue box or touches a
// register itself.  Three reasons, in order of how much they matter:
//
//   1. It is the only way §M5's exit criteria is meetable -- "drive its stage
//      machine through every transition with synthetic input and assert the
//      frame count of each beat", with no renderer.
//   2. §6's own rule about the colour-math unit: effects that contend for one
//      global register must be arbitrated in one place per frame, not written
//      where they are decided.  ScreenFx IS that one place.
//   3. It keeps the second virtual out of the codebase.  An action enum costs
//      nothing and the brief sanctions exactly one virtual, GroundRenderer.

#include <cstdint>

#include "actor.h"
#include "constants.h"
#include "grid.h"
#include "scene.h"
#include "text.h"

namespace kh {

// ---------------------------------------------------------------------------
// The random number generator
//
// Load-bearing, and must be reproduced bit-exactly for the trace oracle: a
// 16-bit Galois LFSR returning the LOW BYTE of the new state.  Seeded 0xACE1 by
// InitWorld and RE-SEEDED 0x1D57 by TownBegin, which is why seeding is explicit
// here rather than happening in a constructor.  BEHAVIOUR-AUDIT.md.
// ---------------------------------------------------------------------------
class Rng {
public:
    static constexpr uint16_t WORLD_SEED = 0xACE1;
    static constexpr uint16_t TOWN_SEED = 0x1D57;

    void seed(uint16_t s) { state_ = s; }
    uint16_t state() const { return state_; }

    uint8_t next() {
        const bool carry = (state_ & 0x8000) != 0;
        state_ = uint16_t(state_ << 1);
        if (carry) state_ = uint16_t(state_ ^ 0x002D);
        return uint8_t(state_ & 0x00FF);
    }

    // Pick one of `count` spots.  NOT a modulo: the SNES reduced by repeated
    // subtraction, which for a count that does not divide 256 gives a different
    // distribution AND a different sequence.  Reproduce the loop, not the intent.
    uint8_t pick(int count) {
        int v = next();
        while (v >= count) v -= count;
        return uint8_t(v);
    }

private:
    uint16_t state_ = WORLD_SEED;
};

// ---------------------------------------------------------------------------
// The frame's screen effects, arbitrated in one place
//
// §6: "effects that contend for one global register must be arbitrated in one
// place per frame, not written where they are decided."  On the SNES that meant
// shadowing INIDISP, MOSAIC and the colour-math registers in RAM and uploading
// once in the NMI.  Here it is a struct the machine fills and the device tier
// applies -- and on the host it is simply a value a test can assert.
// ---------------------------------------------------------------------------
struct ScreenFx {
    uint8_t brightness = 15;    // screenBright, 0..15.  15 is full.
    bool forcedBlank = false;   // INIDISP bit 7, while VRAM is rewritten
    uint8_t mosaic = 0;         // mosaicAmt, 0..15 block size
    int8_t shakeX = 0;          // added to the background scroll, never to camX
    uint8_t whiteout = 0;       // coldataAmt, 0..31 -- the fade to white
    bool bgVisible = true;      // BG1 off during the fall leaves the backdrop

    void reset() { *this = ScreenFx{}; }
};

// ---------------------------------------------------------------------------
// What a machine asks the caller to do
//
// One per frame at most.  A transition that does several things at once -- the
// platform landing loads a scene, clears the actors, spawns a table, restores
// the brightness and opens a message -- is ONE action, because those five are
// not independently orderable and a caller that could interleave them would be
// able to get it wrong.
// ---------------------------------------------------------------------------
enum class SceneAction : uint8_t {
    None = 0,
    Say,                // open script `script` as a message
    Ask,                // ...as a prompt; the answer arrives in Dialogue::result
    EnterStation2,      // load DIVE2, clear, spawn station 2, restore brightness
    EnterStation3,      // ...and the third
    SpawnBoss,          // Darkside rises out of the floor
    BeginFall,          // hand Sora to the scene: no input, a slow tumble
    SpawnMote,          // one speck of light, off to one side and below
    EnterIsland,        // the whiteout's midpoint: load the island, init the quest
    EnterDistrict,      // a door's midpoint: load `arg`, stand Sora on the landing
    DropPair,           // Donald and Goofy, placed well above the square
    LowerPair,          // one frame of their descent
    RaiseArmor,         // the Guard Armor comes down
    SweepGauntlets,     // its hands go with it
    HudChanged,         // the objective line or the gauge needs redrawing
};

// The scripts a stage machine opens.  Named rather than pointed at, because the
// bytes are the device tier's business and the transition is not.
enum class ScriptId : uint8_t {
    None = 0,
    // The Dive
    DiveIntro, DiveStation2, DiveStation3, DiveFloorGoes, DiveBoss,
    DiveVictory, DiveWake,
    // Traverse Town
    TownWake, TownClear, TownMeet, TownWon, TownCard,
};

struct StageStep {
    SceneAction action = SceneAction::None;
    ScriptId script = ScriptId::None;
    uint8_t arg = 0;            // a scene id, or whatever the action needs
};

// What a machine needs to see.  References, because none of it is optional and
// a null here would be a crash in a frame loop rather than a recoverable state.
struct SceneView {
    Actors& actors;
    Dialogue& dialogue;
    Pad& pad;
    const SceneGround& ground;
    Rng& rng;
    int player = 0;             // playerIdx
    uint32_t frame = 0;         // frameCount; several effects key off its low bits
};

// ---------------------------------------------------------------------------
// The Dive
//
// INTRO -> PICK -> DROP -> SHATTER -> S2_INTRO -> S2_FIGHT -> SHATTER2
//       -> S3_INTRO -> BOSS -> DONE -> FALL -> FADE -> ARRIVED
//
// The weapon choice is INERT.  weaponTaken and weaponGiven are each written once
// and read nowhere; audit finding 7 exists because a porting agent will
// otherwise invent stat effects for it.  The machine records the choice and
// nothing depends on it.
// ---------------------------------------------------------------------------
// DiveStage itself lives in constants.h, with the rest of the stage enums --
// §M1 landed them and a second copy here is a second thing to keep in step.

class DiveMachine {
public:
    void begin();
    StageStep update(SceneView& view, ScreenFx& fx);

    DiveStage stage() const { return stage_; }
    int shatterTimer() const { return shatter_; }
    int fallTimer() const { return fall_; }
    int fadeTimer() const { return fade_; }
    // The dream weapon taken and the one given up.  Inert -- see above.
    ActType taken() const { return taken_; }
    ActType given() const { return given_; }

    void setStage(DiveStage s) { stage_ = s; }
    void choose(ActType taken, ActType given) { taken_ = taken; given_ = given; }
    void armShatter() { shatter_ = SHATTER_LEN; }

private:
    StageStep shatterStep(SceneView& view, ScreenFx& fx);
    StageStep fallStep(SceneView& view, ScreenFx& fx);
    StageStep fadeStep(ScreenFx& fx);

    DiveStage stage_ = DiveStage::Intro;
    int shatter_ = 0;
    int fall_ = 0;
    int fade_ = 0;
    ActType taken_ = ActType::None;
    ActType given_ = ActType::None;
};

// ---------------------------------------------------------------------------
// Traverse Town
//
// ARRIVE -> LOOK -> SECOND -> THIRD -> MEET -> BOSS -> WON -> OVER
//
// townStage is PROGRESS, NOT LOCATION.  The player may walk back through any
// door already opened and the town remembers where it had got to; doors carry
// the stage the town must have reached, so the world is gated with no lock flags
// anywhere.  §6.
// ---------------------------------------------------------------------------
class TownMachine {
public:
    void begin(Rng& rng);       // TownBegin RE-SEEDS the RNG; see Rng::TOWN_SEED
    StageStep update(SceneView& view, ScreenFx& fx);

    TownStage stage() const { return stage_; }
    int doorTimer() const { return door_; }
    int townTimer() const { return timer_; }
    // Counts SPAWNS, not kills, despite the name it is given in the assembly.
    // Audit finding 36: `inc townKills` runs after a successful spawn.
    int waveSpawned() const { return spawned_; }
    int spawnTimer() const { return spawn_; }

    void setStage(TownStage s) { stage_ = s; }
    void setDistrict(SceneId s) { district_ = s; }
    SceneId district() const { return district_; }
    // Where the Heartless come up.  Scene data, read from <scene>spots.bin, and
    // the machine spawns them ITSELF rather than asking: the count must only
    // advance on a spawn that succeeded, and the "not on top of the player" test
    // needs both the spot's world position and Sora's.
    void setSpots(const Tile* spots, int count) { spots_ = spots; nSpots_ = count; }
    // A door has been stepped onto; run the fade, swap at the midpoint.
    void openDoor(int toDistrict) { door_ = DOOR_FADE * 2; doorTo_ = uint8_t(toDistrict); }
    void arriveAtThird();

private:
    StageStep doorStep(ScreenFx& fx);
    StageStep shadows(SceneView& view);
    StageStep meet(SceneView& view, ScreenFx& fx);
    StageStep watchArmor(SceneView& view, ScreenFx& fx);

    TownStage stage_ = TownStage::Arrive;
    SceneId district_ = SceneId::Town1;   // which of the three is loaded
    int door_ = 0;
    uint8_t doorTo_ = 0;
    int timer_ = 0;
    int spawn_ = 0;
    int spawned_ = 0;
    const Tile* spots_ = nullptr;
    int nSpots_ = 0;
};

}  // namespace kh
