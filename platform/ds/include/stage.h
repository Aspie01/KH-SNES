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
#include "gen/scripts.h"
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
//
// THE SCRIPT IS A PASSENGER, AND IT IS NEVER OPTIONAL FOR THE PERFORMER.  Say
// and Ask are the two actions that ARE a line; every other action that sets
// StageStep::script is one where the SNES did something and THEN spoke, in the
// same routine and on the same frame, and "one action per step" is exactly why
// the line has to ride along rather than follow as a second step.  Six actions
// other than Say and Ask carry one in the tree today:
//
//     SpawnBoss      DiveBoss        stage_dive.cpp:196
//     SweepGauntlets TownWon         stage_town.cpp:145
//     HudChanged     TownCid         interact.cpp:386   (Cid's first line)
//     EnterFragment  NightFragment   stage_night.cpp:113
//     OpenTheDoor    NightKairi      stage_night.cpp:153
//     ClearColumns   NightKey        stage_night.cpp:190
//
// So the rule for anybody performing a StageStep is: DO THE ACTION, THEN OPEN
// `script` IF IT IS SET.  Not "if the action is one of the talking ones" -- the
// action says what to do, `script` says what to say, and the two are
// independent.  Dropping a passenger costs the line, which is bad, and the BOX,
// which is worse: no box means Dialogue::busy() stays false, and every machine
// in this file gates its own update on that flag, so the player keeps control
// through the frames the SNES spent reading and every input after that lands a
// conversation early.
//
// perform() in platform/ds/host/trace_main.cpp is the tree's only performer
// (the device tier that would be the other one is §M7).  It implements eight of
// the actions above and sends the rest to a `default:` arm that STOPS THE RUN
// and names the action -- which is the safe way to not handle something, and is
// why the four passengers it has no arm for are not a live hazard.  HudChanged
// was the one arm that was both implemented and silent.  It no longer is;
// auditPassengerSurvivesPerform(), beside perform(), is the standing check that
// it stays that way, and doors_only_cids_hud_step_carries_a_line in
// platform/ds/host/tests/test_doors.cpp pins which HudChanged steps carry one
// so that a new passenger on a silently-implemented arm cannot arrive unnoticed.
//
// The guard is on ScriptId::None and not on emptiness: Dialogue::open sets
// state_ = Reveal for an empty Script too (text.cpp:31-43), so a performer that
// opened unconditionally would put an empty box in front of every bare action.
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
    // ...and the same for one of the island's ROOMS, which is a separate action
    // rather than a reuse of the one above.  EnterDistrict's performer asks the
    // TownMachine where it was going (town_.doorTo(), town_.doorLanding()), and a
    // room transition has no TownMachine and no gate -- the pending door is the
    // CALLER's, because taking one is a scene load and Game is the only thing
    // that can do that.  Sharing the action would mean sharing that holder, which
    // is how the island would end up storing a destination on the town's machine.
    //
    // INSERTED HERE AND NOT APPENDED, because it belongs beside its sibling and
    // because nothing depends on a SceneAction's VALUE: check_constants.py pairs
    // no SceneAction family, the trace carries action NAMES, and test_boot's
    // every-action-has-a-name loop is bounded by whichever member is last --
    // which is still RestartScene.
    EnterRoom,
    DropPair,           // Donald and Goofy, placed well above the square
    LowerPair,          // one frame of their descent
    RaiseArmor,         // the Guard Armor comes down
    SweepGauntlets,     // its hands go with it
    // The objective line or the gauge needs redrawing -- AND, when `script` is
    // set, a line to say once it has been.  The redraw alone is what six of the
    // seven in the tree mean; the seventh is Cid, where the SNES does
    // `jsr HudUpdate` and then `jmp Say` in one branch (town.s:542-546) and the
    // step has to carry both halves.  A performer that handles the redraw and
    // ignores the passenger is the failure the block comment above describes.
    HudChanged,
    // Destiny Islands
    RebuildForDayTwo,   // clear, re-init the world, spawn day two's table
    BeginNight,         // the light goes, and what comes up is not the morning
    // The night
    ColumnForRiku,      // delete Riku, stand a column of dark where he was
    ColumnForKairi,     // ...and the same to her
    ClearColumns,       // the dark thins out
    OpenTheDoor,        // retype the Door on the wall, do NOT spawn a second
    EnterFragment,      // load the fragment, spawn its cast, raise Darkside
    SweepCraterShadows, // whatever the boss left behind goes with it
    EnterTown,          // there is somewhere for him to wash up
    // Death and retry
    RespawnNightCast,   // re-run the night's whole table, on the island
    RespawnFragment,    // ...or the fragment's, and raise Darkside again
    RespawnDistrict,    // ...or whichever of the three districts is loaded
    RestartScene,       // out of HP and the card dismissed: put the scene back
};

struct StageStep {
    SceneAction action = SceneAction::None;
    // The line to open, or None.  For Say and Ask it IS the action; for
    // everything else it is a passenger the performer owes the same treatment
    // regardless of what the action was -- see "THE SCRIPT IS A PASSENGER"
    // above.  Defaulted to None because most steps are bare and a performer
    // must be able to tell "nothing to say" from "something to say" without
    // consulting the action.
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

// Pick a spot at random, refuse it if it is on top of the player, and spawn
// there.  Shared by the night and the Second District because it is one routine
// in the assembly too -- SpawnShadows and TownShadows differ only in their
// constants and in whether anything counts the arrivals.
//
// A Heartless arriving in your face reads as a bug rather than as a Heartless,
// so a spot within 64 px of the player on BOTH axes is refused.  The caller
// shortens its own timer when that happens.
struct SpotOutcome {
    bool spawned = false;
    bool tooClose = false;
};

SpotOutcome spawnAtSpot(SceneView& view, const Tile* spots, int count, int cap);

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
    // pendActor / pendWeapon: which weapon the open prompt is about.  They are
    // machine state rather than the caller's because the prompt outlives the
    // frame that opened it -- the answer arrives whenever the player gives one.
    void setPending(int slot, ActType weapon) {
        pendActor_ = int16_t(slot);
        pendWeapon_ = weapon;
    }
    int pendingActor() const { return pendActor_; }
    ActType pendingWeapon() const { return pendWeapon_; }
    void armShatter() { shatter_ = SHATTER_LEN; }
    // A death rewinds to the checkpoint for whichever station is loaded, which
    // is a per-scene stage and not a single one.  The caller re-spawns the cast.
    StageStep restart(SceneId scene, ScreenFx& fx);

private:
    StageStep shatterStep(SceneView& view, ScreenFx& fx);
    StageStep fallStep(SceneView& view, ScreenFx& fx);
    StageStep fadeStep(ScreenFx& fx);

    DiveStage stage_ = DiveStage::Intro;
    int shatter_ = 0;
    int fall_ = 0;
    int fade_ = 0;
    int16_t pendActor_ = -1;
    ActType pendWeapon_ = ActType::None;
    ActType taken_ = ActType::None;
    ActType given_ = ActType::None;
};

// --- the fall's spread of light, and why it lives out here -------------------
//
// Where each speck of light starts, relative to Sora.  moteOfsX / moteOfsY,
// dive.s:919-924.
//
// This is Tier-1 LIBRARY data and not oracle-fixture data, which is the
// distinction that decides which file it goes in.  ORACLE_NIGHT_SPOTS and
// ORACLE_TOWN_SPOTS live in trace_main.cpp precisely BECAUSE the DS ships
// different ones, so the fixture's copy is the SNES's and the shipped copy is
// the DS's and they are two different facts.  The two halves of that are
// documented in DIFFERENT places and it is worth naming both rather than
// waving at one, because a reader who checks the citation and finds it does not
// cover the case is entitled to distrust the whole argument: the night's ten
// spots against the island's thirty-five are divergence 003, and the town's are
// NOT in any divergence file at all -- assets/ds/town2_cast.txt ships twelve
// against the SNES table's eight, and constants.h:471-472 is where TOWN_SPOTS
// records that the DS reads its count from the file instead.  The motes have no
// DS variant: the offsets are world-space Q12.4 and a screen 32 lines shorter
// cannot see them, so there is exactly one table and it is shared.  Precedent is
// RACE_WP (world.cpp:820-825) -- a table lifted from the assembly, cited there,
// living in the simulation even with one caller.
//
// Two callers WILL exist.  trace_main.cpp's perform() needs it now, and the
// device tier's scene layer needs it at §M7 where there is no oracle at all to
// catch a copy that has drifted.  One table, one home.
//
// It stays a FREE FUNCTION of the frame number rather than a DiveMachine member
// or a payload on StageStep, so that "A machine here is PURE LOGIC" at the top
// of this file stays literally true: SceneAction::SpawnMote still carries no
// position and the machine still touches no Actors.
struct MoteOffset { World dx, dy; };

// dive.s:479-481 tests `fallTimer & $03` -- one mote every fourth frame -- and
// dive.s:512-515 indexes the spread with `(frameCount >> 2) & $07`.  The two are
// a matched pair: eight offsets visited by a mote every fourth frame is what the
// assembly's comment at dive.s:510-511 means by "so successive motes do not
// stack up", and moving either number alone re-stacks them.
//
// They are here and not in constants.h because constants.h:388-394 is the
// game.inc mirror and these two are dive.s CODE facts -- `and #$03` and
// `and #$07` are addressing, not tuning, and game.inc names neither.  What
// actually matters is that they exist ONCE: do not split the cadence and the
// mask across two headers, because the thing that goes wrong is one of them
// being changed alone.
constexpr int MOTE_SPAWN_EVERY = 4;
constexpr int MOTE_SPREAD = 8;

MoteOffset moteOffset(uint32_t frame);

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
    //
    // THE LANDING TRAVELS WITH THE DOOR, and it did not before.  StageStep::arg
    // carries a scene id and nothing else (stage_town.cpp:62), so
    // SceneAction::EnterDistrict promised "load `arg`, stand Sora on the
    // landing" (see the enum above) with no landing anywhere in the message --
    // TownDoor::landing was written by the door table and read by nothing in
    // the whole tree.  The caller would have had to re-find the row it had just
    // matched, from a scene id that names the DESTINATION rather than the door,
    // which cannot be done unambiguously once a district has two ways out.
    //
    // The machine is the right place to hold it because that is where the SNES
    // held it: `doorTo` was the doorTable OFFSET, it survived LoadDistrict, and
    // DoorRow re-read doorTable+4/+5 through it afterwards (town.s:373-381).
    // One row, remembered across the swap.
    //
    // The landing DEFAULTS to nowhere so that the tests which only exercise the
    // fade -- test_stage.cpp's timing walks, and the "clear the fade the last
    // one armed" line in test_interact.cpp -- stay as they are: they are about
    // doorTimer and say nothing about a destination tile, and making them
    // invent one would be noise in a fixture.  Every call that means a real
    // door passes it, and test_doors.cpp's end-to-end ladder asserts the value
    // that comes back out, so a real call site that dropped it would fail
    // rather than silently land Sora on (0,0).
    void openDoor(int toDistrict, Tile landing = Tile{}) {
        door_ = DOOR_FADE * 2;
        doorTo_ = uint8_t(toDistrict);
        doorLanding_ = landing;
    }
    // Where SceneAction::EnterDistrict is to stand him: the tile directly south
    // of the door ON THE FAR SIDE (town.s:1386-1389).  NOT <scene>doors.bin's
    // land_i/land_j, which is the near-side tile beside the door in its own map.
    Tile doorLanding() const { return doorLanding_; }
    void arriveAtThird();

    // A death, and what the district owes the retry.  TownRestart (town.s:94)
    // is the one restart the port did not have -- the Dive and the night both
    // did -- and it does five things, of which the third is the one nobody
    // guesses: THE WAVE COUNTER GOES BACK TO ZERO.  Its comment says why in the
    // assembly: "half a wave of survivors left standing while the counter says
    // the district is nearly clear would be a retry that is easier than the
    // attempt."  It also re-arms the spawn timer, clears the door, puts the
    // screen-wide effects back, and asks for the armour again if the district
    // is on its boss.
    //
    // It does NOT re-seed the LFSR.  Only TownBegin does that, and it is the
    // one place in the game that does it deliberately -- see Rng::TOWN_SEED.
    StageStep restart(ScreenFx& fx);

private:
    StageStep doorStep(ScreenFx& fx);
    StageStep shadows(SceneView& view);
    StageStep meet(SceneView& view, ScreenFx& fx);
    StageStep watchArmor(SceneView& view, ScreenFx& fx);

    TownStage stage_ = TownStage::Arrive;
    SceneId district_ = SceneId::Town1;   // which of the three is loaded
    int door_ = 0;
    uint8_t doorTo_ = 0;
    Tile doorLanding_;
    int timer_ = 0;
    int spawn_ = 0;
    int spawned_ = 0;
    const Tile* spots_ = nullptr;
    int nSpots_ = 0;
};

// ---------------------------------------------------------------------------
// Destiny Islands
//
// IDLE -> ACTIVE -> DONE -> DAYOUT -> DAYIN  (day one into day two)
//      -> RACE_SET -> RACE_RUN -> RACE_OVER -> NAMING -> NAMED -> DUSK
//
// Corrections the diagram needs, all from audit finding 14 and verified against
// island.s:
//   - DAYIN ends at IDLE, which the diagram does not show, so day two re-enters
//     IDLE -> ACTIVE -> DONE;
//   - day one's DONE -> DAYOUT fires automatically with no player action once
//     the line is dismissed;
//   - day two's DONE -> RACE_SET needs talking to Kairi AGAIN;
//   - RACE_OVER goes to NAMING only if Sora won.  If Riku won it jumps straight
//     to NAMED with Excalibur forced, because he was never going to pick
//     anything else.
//
// And one the diagram cannot show: THE DAY CHANGE IGNORES THE DIALOGUE BOX.
// DayOut and DayIn are dispatched BEFORE the TextBusy check (island.s:69-76),
// which makes them exceptions alongside the night's Tear and EndFade -- and the
// audit's list of exceptions names only the night's two.
// ---------------------------------------------------------------------------
class IslandMachine {
public:
    void begin();
    StageStep update(SceneView& view, ScreenFx& fx);

    QuestState state() const { return state_; }
    int day() const { return day_; }
    int dayTimer() const { return dayTimer_; }
    RaftName raftName() const { return raft_; }
    // 0 while it is being run, 1 Sora, 2 Riku.  Riku wins a same-frame tie
    // because RaceRun tests his waypoint count FIRST -- audit finding 8.
    int raceWon() const { return raceWon_; }
    int raceLeg() const { return leg_; }

    // The gameplay gate, which is a range and not a flag: pickups, the Keyblade
    // and conversation are live for state < RaceSet OR state == Named exactly.
    // So Named still allows gathering -- the race takes the scene away and gives
    // it back once the raft has a name.  island.s:114-119.
    bool gameplayLive() const {
        return state_ < QuestState::RaceSet || state_ == QuestState::Named;
    }
    // What the HUD was last told to draw, which is NOT always the current state.
    // The day change writes Idle, refreshes the HUD, and only then writes DayIn
    // -- so the checklist the player sees when the screen comes back is day two's
    // empty one and not a day-change state.  island.s:215-218.
    QuestState hudState() const { return hudState_; }

    void setState(QuestState s) { state_ = s; hudState_ = s; }
    void setDay(int d) { day_ = d; }
    // Talking to Kairi is the island's only real input, and what it does depends
    // entirely on where the quest has got to.  Returns what to say.
    StageStep talkToKairi(bool haveAll);
    // The race, driven from outside because Riku's course and Sora's collision
    // belong to the actor layer.
    void beginRace();
    void setRikuWaypoint(int wp) { rikuWp_ = wp; }
    // Only while the race is RUNNING.  RaceRun is what tests the paopu tree on
    // the SNES, and it only runs at Q_RACE_RUN -- so a player who walks out
    // there during the 193-frame countdown is not credited then.  He IS credited
    // on the very first RaceRun frame, because nothing freezes him during the
    // countdown and RaceRun tests where he is standing rather than where he went.
    void tagPaopu() { if (state_ == QuestState::RaceRun && leg_ == 0) leg_ = 1; }
    void reachHome();

private:
    StageStep dayOut(ScreenFx& fx);
    StageStep dayIn(ScreenFx& fx);
    StageStep dusk(ScreenFx& fx);
    StageStep countdown();
    StageStep raceRun();
    StageStep afterRace();

    QuestState state_ = QuestState::Idle;
    QuestState hudState_ = QuestState::Idle;
    int day_ = 1;
    // ONE byte time-shared by four unrelated machines on the SNES: the DayOut
    // fade, the DayIn fade, the Dusk fade and the race countdown.  Kept as one
    // field here for the same reason -- four fields would let two of them be
    // live at once, which the original could not represent and no beat needs.
    int dayTimer_ = 0;
    int rikuWp_ = 0;
    int leg_ = 0;
    int raceWon_ = 0;
    RaftName raft_ = RaftName::None;
    bool handedOver_ = false;   // Dusk asks for the night exactly once
};

// ---------------------------------------------------------------------------
// The night the island falls
//
// INTRO -> SEEK -> RIKU -> KEY -> KAIRI -> DOOR -> TEAR -> BOSS -> END -> OVER
//
// This is the scene ScreenFx exists for.  The lightning and the closing fade
// both want the colour-math unit, so the flash is SUPPRESSED from END onward --
// a strike resetting the unit back to translucent shadows undid the fade every
// time one landed.  TEAR and END additionally run THROUGH an open dialogue box
// rather than waiting for it, so the line about the island coming apart is on
// screen while it does.
// ---------------------------------------------------------------------------
class NightMachine {
public:
    // NightBegin takes the RNG because it DRAWS FROM IT: `jsr ArmLightning`
    // (night.s:83) is one `Rand`, and so is the one in NightRestart.  That is
    // not a detail -- the flash wait and the Shadow spots come out of the same
    // LFSR, so a night that skips the draw runs every later arrival off a
    // different sequence.  §M6 names this as THE determinism hazard, and the
    // trace found it: the first version of this file set `wait_` to the minimum
    // with no draw, under a comment saying it drew.
    //
    // It also arms the first flash and sets the spawn timer to the gap, both of
    // which NightBegin does and neither of which is cosmetic: with the timer at
    // zero the first Shadow arrives on the first frame of the search instead of
    // seventy-one frames into it.
    void begin(Rng& rng);
    StageStep update(SceneView& view, ScreenFx& fx);

    NightStage stage() const { return stage_; }
    int nightTimer() const { return timer_; }
    int flashTimer() const { return flash_; }
    int flashWait() const { return wait_; }
    bool keyGot() const { return key_; }

    void setStage(NightStage s) { stage_ = s; }
    // Talking to Riku, and later to Kairi, is what moves the night on.
    StageStep talkToRiku();
    StageStep talkToKairi();
    // A death rewinds to whichever of the two searches was in progress -- and
    // clears the Keyblade if it had not been earned yet.  It also has to put the
    // screen-wide effects back, because a death can land in the middle of the
    // island coming apart.  It re-arms the lightning, which is the second draw.
    StageStep restart(Rng& rng, bool onFragment, ScreenFx& fx);
    // Where the Shadows come up.  The night spawns them in BOTH searches and
    // has no wave cap -- unlike the Second District, they keep arriving for as
    // long as the search lasts.
    void setSpots(const Tile* spots, int count) { spots_ = spots; nSpots_ = count; }
    int spawnTimer() const { return spawn_; }

    // HOW DENSE THE NIGHT IS, and it belongs to the MAP rather than to this
    // machine.  The SNES had one pair of numbers because the night and the
    // fragment ran on maps of similar size; the DS island is four times what it
    // was and the fragment is deliberately untouched, so the island wants 20 at
    // a 42-frame gap and the fragment still wants 6 at 70 -- see divergence 003
    // and the derivation in constants.h.  The default is the island's, because
    // that is where the night starts; whoever loads the fragment says so, and so
    // does an oracle fixture, which needs the SNES's 6 and 70.
    void setDensity(int alive, int gap) { shadowMax_ = alive; shadowGap_ = gap; }
    int shadowMax() const { return shadowMax_; }
    int shadowGap() const { return shadowGap_; }

private:
    void armLightning(Rng& rng);
    void lightning(SceneView& view, ScreenFx& fx);
    void spawnShadows(SceneView& view);
    StageStep column(SceneView& view, SceneAction make, NightStage next,
                     ScriptId say);
    StageStep tear(SceneView& view, ScreenFx& fx);
    StageStep endFade(ScreenFx& fx);

    NightStage stage_ = NightStage::Intro;
    int timer_ = 0;
    int flash_ = 0;
    int wait_ = 0;
    int spawn_ = 0;
    bool key_ = false;
    const Tile* spots_ = nullptr;
    int nSpots_ = 0;
    int shadowMax_ = SHADOW_MAX_NIGHT;
    int shadowGap_ = SHADOW_GAP;
};

}  // namespace kh
