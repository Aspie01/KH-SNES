// The frame loop, the scene table, and the second performer.

#include "boot.h"

#include "gen/doors.h"

namespace kh::device {
namespace {

// A cast table that did not fully load is not a warning.  scene.h says why:
// "SpawnTable ignored the SNES's clear-carry and silently dropped whichever
// entry overflowed, which is how the bottle under the waterfall went missing
// for a whole day."  So every spawn site here checks, and an incomplete load
// leaves an error somebody can read on the bottom screen.
bool spawnAll(Actors& a, Blob table, const SceneGround& g) {
    if (table.empty()) return true;         // a table a scene does not have
    return spawnCast(a, table, g).complete();
}

}  // namespace

const char* actionName(SceneAction a) {
    switch (a) {
        case SceneAction::None: return "None";
        case SceneAction::Say: return "Say";
        case SceneAction::Ask: return "Ask";
        case SceneAction::EnterStation2: return "EnterStation2";
        case SceneAction::EnterStation3: return "EnterStation3";
        case SceneAction::SpawnBoss: return "SpawnBoss";
        case SceneAction::BeginFall: return "BeginFall";
        case SceneAction::SpawnMote: return "SpawnMote";
        case SceneAction::EnterIsland: return "EnterIsland";
        case SceneAction::EnterDistrict: return "EnterDistrict";
        case SceneAction::DropPair: return "DropPair";
        case SceneAction::LowerPair: return "LowerPair";
        case SceneAction::RaiseArmor: return "RaiseArmor";
        case SceneAction::SweepGauntlets: return "SweepGauntlets";
        case SceneAction::HudChanged: return "HudChanged";
        case SceneAction::RebuildForDayTwo: return "RebuildForDayTwo";
        case SceneAction::BeginNight: return "BeginNight";
        case SceneAction::ColumnForRiku: return "ColumnForRiku";
        case SceneAction::ColumnForKairi: return "ColumnForKairi";
        case SceneAction::ClearColumns: return "ClearColumns";
        case SceneAction::OpenTheDoor: return "OpenTheDoor";
        case SceneAction::EnterFragment: return "EnterFragment";
        case SceneAction::SweepCraterShadows: return "SweepCraterShadows";
        case SceneAction::EnterTown: return "EnterTown";
        case SceneAction::RespawnNightCast: return "RespawnNightCast";
        case SceneAction::RespawnFragment: return "RespawnFragment";
        case SceneAction::RespawnDistrict: return "RespawnDistrict";
        case SceneAction::RestartScene: return "RestartScene";
    }
    return "?";
}

CameraBounds boundsFor(SceneId s, int tilesW, int tilesH) {
    // THE THREE STATIONS ARE PINNED AND NOTHING ELSE IS.
    //
    // A Station of Awakening is a disc in a void: the SNES pinned the camera at
    // (DIVE_CAM_X, DIVE_CAM_Y) and the whole disc is inside one screen, so
    // scrolling would be scrolling over nothing.  The oracle confirms it --
    // every dive scenario in tools/trace_check.py runs against pinnedBounds and
    // the camera columns agree frame for frame.
    //
    // THE FRAGMENT IS NOT PINNED HERE, AND THAT IS A JUDGEMENT RATHER THAN A
    // CITATION.  It is the same 32x16 shape and it looks like the same kind of
    // scene, but night.s writes no camLo/camHi pair for it, no trace scenario
    // reaches it, and a map 512 px wide against a 256 px screen genuinely has
    // somewhere to scroll to.  So it gets the ordinary rule.  If it turns out to
    // want pinning, this is the one line to change and the reason it was not
    // pinned is here rather than lost.
    if (s == SceneId::Dive || s == SceneId::Dive2 || s == SceneId::Dive3)
        return pinnedBounds(DIVE_CAM_X, DIVE_CAM_Y);
    return scrollingBounds(tilesW, tilesH);
}

Game::Game(SceneSource source) : source_(source) {}

SceneView Game::view() {
    return SceneView{actors_, dialogue_, pad_, ground_, rng_, player_, frame_};
}

void Game::findPlayer() {
    player_ = -1;
    for (int i = 0; i < MAX_ACTORS; ++i)
        if (actors_.type[i] == ActType::Sora) { player_ = i; break; }
}

void Game::applyDensity() {
    // HOW DENSE THE NIGHT IS BELONGS TO THE MAP, not to the machine -- stage.h's
    // setDensity() says so and divergence 003 derives the numbers.  The island
    // is four times the SNES's ground and wants twenty Shadows at a 42-frame
    // gap; the fragment is deliberately untouched and still wants the SNES's six
    // at seventy.  Whoever loads the scene says which, and this is that.
    if (scene_ == SceneId::Fragment)
        night_.setDensity(SNES_SHADOW_MAX, SNES_SHADOW_GAP);
    else
        night_.setDensity(SHADOW_MAX_NIGHT, SHADOW_GAP);
}

bool Game::enter(SceneId s) {
    error_ = nullptr;
    const SceneBlobs* b = source_ ? source_(s) : nullptr;
    if (!b) {
        error_ = "no bytes for that scene; the source has no entry for it";
        return false;
    }

    ground_.set(b->collision, b->height, b->tilesW, b->tilesH);
    if (!ground_.valid()) {
        error_ = "the collision or height table is shorter than the extents the "
                 "scene claims; the pipeline and the scene table disagree";
        return false;
    }

    // A tile is CHARS_PER_TILE characters square.  The renderer refuses a
    // mismatch too (device/ground.cpp), which is the check that matters, but
    // deriving it here from the SAME extents is what makes the two agree by
    // construction rather than by both being typed correctly.
    chars_ = CharMap{b->chars, b->tilesW * (TILE_PX / 8), b->tilesH * (TILE_PX / 8)};

    actors_.clear();
    if (!spawnAll(actors_, b->cast, ground_)) {
        error_ = "the cast did not fully load: the pool refused a row or the "
                 "table ran off its end with no terminator";
        return false;
    }
    findPlayer();

    nSpots_ = readSpots(b->spots, spots_, MAX_SPOTS);
    if (nSpots_ < 0) {
        error_ = "more Heartless spots than MAX_SPOTS; that is malformed data "
                 "and not a buffer to enlarge";
        return false;
    }
    nDoors_ = readDoors(b->doors, doors_, MAX_DOORS);
    if (nDoors_ < 0) {
        error_ = "more doors than MAX_DOORS";
        return false;
    }

    night_.setSpots(spots_, nSpots_);
    town_.setSpots(spots_, nSpots_);
    bounds_ = boundsFor(s, b->tilesW, b->tilesH);
    scene_ = s;
    applyDensity();
    // LoadDistrict sets lastTileI/J to $FF, which is "nowhere" -- so whichever
    // tile Sora lands on counts as a step onto it.  Without this a landing that
    // happens to be on a door would not re-fire, which is right, but a landing
    // NEXT to one would fire on the frame after, which is not.
    interact_.reset();
    sceneChanged_ = true;
    // The reset path runs UpdateCamera once before MainLoop (main.s:120), so the
    // camera is on the player by the first frame rather than at the origin.
    updateCamera(cam_, actors_, player_, bounds_, fx_.shakeX);
    return true;
}

bool Game::begin(SceneId first) {
    // InitWorld, world.s:52.  The seed is not decoration: a Galois register at
    // zero STAYS at zero, so a night reached without this has every flash wait
    // at the minimum and every Shadow on one tile, for ever.  §M6 names it as
    // THE determinism hazard and town.s:68 carries the same warning.
    rng_.seed(Rng::WORLD_SEED);
    world_.reset();
    inv_.reset();
    fx_.reset();
    frame_ = 0;
    soraCel_ = -1;
    if (!enter(first)) return false;

    switch (first) {
        case SceneId::Dive:
        case SceneId::Dive2:
        case SceneId::Dive3:
            dive_.begin();
            // ...AND THE STAGE THAT SCENE BELONGS TO, which begin() alone does
            // not give.  DiveMachine::begin() rewinds to DIVE_INTRO, which is
            // the first station's opening; starting the build on the second or
            // third station with that stage runs station one's beats on station
            // three's glass, and the visible symptom is a boss that never
            // rises, because SpawnBoss is emitted from DIVE_S3_INTRO
            // (stage_dive.cpp:193-196) and nothing ever gets there.
            //
            // Found by test_boot.cpp, which drove the third station for four
            // thousand frames and never saw the action.  It is a bring-up entry
            // point rather than a route the game takes -- the game arrives at
            // each station through EnterStation2/3, which set the stage
            // themselves -- so it is exactly the kind of thing that would have
            // been discovered by a person holding a DS and wondering why.
            if (first == SceneId::Dive2) dive_.setStage(DiveStage::S2Intro);
            if (first == SceneId::Dive3) dive_.setStage(DiveStage::S3Intro);
            break;
        case SceneId::Island:
            island_.begin();
            // ...and the gate.  SceneUpdate runs IslandUpdate only when
            // diveStage is DIVE_ARRIVED and otherwise runs DiveUpdate even on
            // the island (main.s:622-628), so a build that starts ON the island
            // has to say the Dive is over or the Dive machine drives it.
            dive_.begin();
            dive_.setStage(DiveStage::Arrived);
            break;
        case SceneId::Night:
        case SceneId::Fragment:
            night_.begin(rng_);
            dive_.begin();
            dive_.setStage(DiveStage::Arrived);
            world_.keyGot = night_.keyGot();
            break;
        case SceneId::Town1:
        case SceneId::Town2:
        case SceneId::Town3:
            town_.begin(rng_);          // TownBegin RE-SEEDS to $1D57
            town_.setDistrict(first);
            dive_.begin();
            dive_.setStage(DiveStage::Arrived);
            break;
        case SceneId::Count:
            error_ = "SceneId::Count is the end-of-enum sentinel, not a scene";
            return false;
    }
    return true;
}

bool Game::takeSceneChanged() {
    const bool v = sceneChanged_;
    sceneChanged_ = false;
    return v;
}

HudState Game::hudState() const {
    HudState h;
    h.player = player_;
    // WHICH ACTOR IS THE BOSS is a scene question, and device/hud.h refuses to
    // answer it twice -- so it is answered here, once, the way the SNES did:
    // the AF_HUGE actor, of which there is never more than one.
    for (int i = 0; i < MAX_ACTORS; ++i) {
        if (actors_.type[i] == ActType::None) continue;
        if (!has(actors_.flags[i], ActFlags::Huge)) continue;
        h.boss = i;
        h.armor = actors_.type[i] == ActType::Armor;
        break;
    }
    return h;
}

StageStep Game::machineStep() {
    SceneView v = view();
    // THE DIVE IS THE GATE FOR EVERY SCENE, not just for its own three.
    // main.s:622-628 dispatches on diveStage before it dispatches on sceneId:
    // until the Dive says DIVE_ARRIVED, DiveUpdate runs even standing on the
    // island.  A port that dispatched on the scene alone would run the island's
    // machine through the fall, and §M6 caught exactly that as 670 frames of a
    // single differing column.
    if (dive_.stage() != DiveStage::Arrived) return dive_.update(v, fx_);

    switch (scene_) {
        case SceneId::Dive:
        case SceneId::Dive2:
        case SceneId::Dive3:
            return dive_.update(v, fx_);
        case SceneId::Island: {
            // The race is the one place two owners hold one number.  The machine
            // owns rikuWp and WorldState carries the copy the actor layer
            // advances, so it is handed back once a frame BEFORE the machine
            // looks -- or Riku crosses the line and nothing notices until the
            // frame after.
            island_.setRikuWaypoint(world_.rikuWp);
            const StageStep s = island_.update(v, fx_);
            world_.raceRunning = island_.state() == QuestState::RaceRun;
            return s;
        }
        case SceneId::Night:
        case SceneId::Fragment: {
            const StageStep s = night_.update(v, fx_);
            // keyGot is one global the SNES's NightUpdate writes and UpdateWorld
            // reads, in that order within a frame -- so the copy is handed over
            // HERE, between the two, and not at the top of the next frame.  It
            // gates both directions: a wooden sword cannot hurt a Shadow and a
            // Shadow cannot hurt Sora.
            world_.keyGot = night_.keyGot();
            return s;
        }
        case SceneId::Town1:
        case SceneId::Town2:
        case SceneId::Town3:
            return town_.update(v, fx_);
        case SceneId::Count:
            break;
    }
    return StageStep{};
}

StageStep Game::interactStep() {
    SceneView v = view();
    switch (scene_) {
        case SceneId::Dive:
        case SceneId::Dive2:
        case SceneId::Dive3: {
            // dive.s:82-86: a prompt that just closed leaves its answer in
            // txtResult, HandleAnswer reads it, and then `stz txtResult`.  Both
            // halves, or a silent no-arm re-answers itself every frame.
            const int answer = dialogue_.result();
            const StageStep s = diveInteract(dive_, v, answer);
            if (answer != 0) dialogue_.clearResult();
            return s;
        }
        case SceneId::Island:
            return islandInteract(island_, v, inv_);
        case SceneId::Night:
        case SceneId::Fragment:
            return nightInteract(night_, v);
        case SceneId::Town1:
        case SceneId::Town2:
        case SceneId::Town3: {
            const TownDoors d = townDoorsFor(scene_);
            return townInteract(town_, interact_, v, d.rows, d.count);
        }
        case SceneId::Count:
            break;
    }
    return StageStep{};
}

bool Game::perform(const StageStep& step) {
    // THE SCRIPT IS A PASSENGER AND IT IS NEVER OPTIONAL, which is stage.h's
    // rule and the one thing a second performer is most likely to get wrong:
    // "DO THE ACTION, THEN OPEN `script` IF IT IS SET.  Not 'if the action is
    // one of the talking ones'."  So the passenger is opened HERE, once, after
    // the switch, for every arm that reaches the bottom -- which makes dropping
    // it impossible rather than merely discouraged.  trace_main.cpp does it per
    // arm because it implements eight actions; this implements most of them and
    // could not keep that up.
    //
    // Guarded on ScriptId::None because most steps are bare: Dialogue::open sets
    // state_ = Reveal even for an empty Script (text.cpp:31-43), so an
    // unguarded open would park an empty box in front of every objective-line
    // redraw and busy() would hold the machine that asked behind it.
    bool ok = true;
    // Say and Ask ARE the line, so they must not also fall through to the
    // passenger below or the box would be opened twice.
    bool spoke = false;

    switch (step.action) {
        case SceneAction::None:
            break;

        case SceneAction::Say:
            dialogue_.open(scriptFor(step.script), TextMode::Message);
            spoke = true;
            break;

        case SceneAction::Ask:
            // WHICH MENU, and the action does not say.  StageStep carries an
            // action, a script and a byte, and no mode -- so the mode has to be
            // inferred, and there is exactly one three-way menu in the game:
            // the raft naming, which is the only Ask that opens
            // ScriptId::IslandWhatName (stage_island.cpp:121).  Everything else
            // is yes/no.  Inferring from the script rather than from the scene
            // is deliberate: the island asks BOTH kinds.
            //
            // Getting it wrong is not visible as a crash.  With TextMode::Prompt
            // the raft menu shows two of three names and menuCount() caps the
            // answer at 2, so Ragnarok becomes unreachable and the raft is named
            // by a menu that is silently one item short.
            dialogue_.open(scriptFor(step.script),
                           step.script == ScriptId::IslandWhatName
                               ? TextMode::Raft
                               : TextMode::Prompt);
            spoke = true;
            break;

        case SceneAction::HudChanged:
            // The redraw is the caller's -- buildHud() runs every frame anyway,
            // so there is nothing to schedule.  The PASSENGER is the point, and
            // it is handled at the bottom with everything else's.
            break;

        // --- scene changes ---------------------------------------------------
        case SceneAction::EnterStation2:
            ok = enter(SceneId::Dive2);
            fx_.brightness = 15;        // ...restore brightness
            break;
        case SceneAction::EnterStation3:
            ok = enter(SceneId::Dive3);
            fx_.brightness = 15;
            break;
        case SceneAction::EnterIsland:
            ok = enter(SceneId::Island);
            if (ok) {
                // InitWorld again: the island is the scene that re-runs it, and
                // that is the seeding the night's LFSR depends on having had.
                rng_.seed(Rng::WORLD_SEED);
                inv_.reset();
                island_.begin();
                const SceneBlobs* b = source_(SceneId::Island);
                ok = b && spawnAll(actors_, b->day1, ground_);
                if (!ok) error_ = "the island's day-one table did not load";
            }
            break;
        case SceneAction::RebuildForDayTwo: {
            // island.s:215-218: clear, re-init, spawn day two.  The DAY-ONE
            // table is not respawned and the two never coexist -- the cast file
            // says so in as many words.
            const SceneBlobs* b = source_(SceneId::Island);
            if (!b) { ok = false; error_ = "the island has no bytes"; break; }
            actors_.clear();
            ok = spawnAll(actors_, b->cast, ground_)
                 && spawnAll(actors_, b->day2, ground_);
            findPlayer();
            if (!ok) error_ = "the island's day-two table did not load";
            break;
        }
        case SceneAction::BeginNight:
            ok = enter(SceneId::Night);
            if (ok) {
                night_.begin(rng_);     // ArmLightning is the first draw
                world_.keyGot = night_.keyGot();
            }
            break;
        case SceneAction::EnterFragment:
            ok = enter(SceneId::Fragment);
            // ...and raise Darkside, through the same arm that raises it on
            // the third station rather than through a second copy of the
            // spawn-then-arm sequence.
            if (ok) ok = perform(StageStep{SceneAction::SpawnBoss});
            break;
        case SceneAction::EnterTown:
            ok = enter(SceneId::Town1);
            if (ok) {
                town_.begin(rng_);      // TownBegin re-seeds to $1D57
                town_.setDistrict(SceneId::Town1);
            }
            break;
        case SceneAction::EnterDistrict: {
            const SceneId to = SceneId(step.arg);
            if (to >= SceneId::Count) {
                ok = false;
                error_ = "a door names a scene id past the end of the enum";
                break;
            }
            // THE LANDING TRAVELS WITH THE DOOR.  stage.h spells out why the
            // machine holds it rather than the caller re-finding the row: a
            // scene id names the DESTINATION, and once a district has two ways
            // out that cannot be matched back to a door unambiguously.
            const Tile land = town_.doorLanding();
            ok = enter(to);
            if (ok) {
                town_.setDistrict(to);
                if (player_ >= 0) {
                    actors_.x[player_] = tileCentre(land.i);
                    actors_.y[player_] = tileCentre(land.j);
                    actors_.vx[player_] = World::fromRaw(0);
                    actors_.vy[player_] = World::fromRaw(0);
                    actors_.dir[player_] = Dir::S;
                    setActorZ(actors_, player_, ground_);
                    updateCamera(cam_, actors_, player_, bounds_, fx_.shakeX);
                }
            }
            break;
        }

        // --- the four routines, shared with the trace emitter -----------------
        case SceneAction::RaiseArmor: {
            SceneView v = view();
            ok = kh::raiseArmor(v, world_);
            break;
        }
        case SceneAction::SweepGauntlets: {
            SceneView v = view();
            kh::sweepGauntlets(v, world_, fx_);
            break;
        }
        case SceneAction::BeginFall: {
            SceneView v = view();
            ok = kh::beginFall(v);
            break;
        }
        case SceneAction::SpawnMote: {
            SceneView v = view();
            ok = kh::spawnMote(v);
            break;
        }

        case SceneAction::SpawnBoss: {
            const SceneBlobs* b = source_(scene_);
            if (!b || b->boss.empty()) {
                ok = false;
                error_ = "this scene has no [boss] table to raise";
                break;
            }
            if (!spawnAll(actors_, b->boss, ground_)) {
                ok = false;
                error_ = "the boss table did not load";
                break;
            }
            // Darkside arrives RESTING rather than mid-attack, which is what
            // gives the player the beat between the rise and the first fist.
            for (int i = 0; i < MAX_ACTORS; ++i) {
                if (actors_.type[i] != ActType::Darkside) continue;
                actors_.hp[i] = uint8_t(DS_MAX_HP);
                actors_.state[i] = ActState::Idle;      // DSS_REST shares the byte
                actors_.timer[i] = uint8_t(DS_REST);
                world_.bossHP = uint8_t(DS_MAX_HP);
                break;
            }
            break;
        }

        case SceneAction::SweepCraterShadows:
            // Whatever the boss left behind goes with it.  The Shadows only --
            // the fragment has no bystanders, but naming the type rather than
            // clearing the pool is what keeps that true if it ever does.
            for (int i = 0; i < MAX_ACTORS; ++i)
                if (actors_.type[i] == ActType::Shadow)
                    actors_.type[i] = ActType::None;
            break;

        // --- everything that needs a routine nobody has ported yet ------------
        //
        // REFUSED BY NAME, NOT IGNORED.  Each of these is a beat with a body in
        // the assembly that this port has not written: Donald and Goofy's
        // descent, and the four retry paths.  The night's three column beats
        // used to be on this list and are the arms directly above -- which is
        // what this list is FOR, and why they were found rather than forgotten.
        // The four retries are the remaining cluster and want doing together:
        // each one reloads a scene's cast, which is the performer's other half
        // and the seam perform.h deliberately does not cross.  An arm
        // that returned true would make the machine's timer run on over a beat
        // that never happened -- invisible, silent, and only findable by someone
        // who knows what the scene is supposed to look like, which is the worst
        // shape a defect can take and the one this project keeps finding.
        //
        // So they set lastActionPerformed() false and the caller puts the name
        // on the bottom screen.  A person testing the build sees which beat is
        // missing, in the scene it is missing from, on the frame it was wanted.
        // --- the night's three actor beats ------------------------------------
        // Shared with the trace performer through perform.h, for the reason that
        // file gives: the oracle diff only ever runs ONE performer's copy, so a
        // second copy here could drift arbitrarily far and every scenario would
        // stay green.
        case SceneAction::ColumnForRiku: {
            SceneView v = view();
            ok = kh::standColumn(v, ActType::Riku);
            if (!ok) error_ = "the pool refused the column of dark, so Riku is "
                              "gone and nothing is standing where he was";
            break;
        }
        case SceneAction::ColumnForKairi: {
            SceneView v = view();
            ok = kh::standColumn(v, ActType::Kairi);
            if (!ok) error_ = "the pool refused the column of dark, so Kairi is "
                              "gone and nothing is standing where she was";
            break;
        }
        case SceneAction::ClearColumns: {
            SceneView v = view();
            kh::clearColumns(v);
            break;
        }
        case SceneAction::OpenTheDoor: {
            SceneView v = view();
            ok = kh::openTheDoor(v);
            if (!ok) error_ = "there is no Door on the wall to open; the night's "
                              "last beat has nothing to act on";
            break;
        }

        case SceneAction::DropPair:
        case SceneAction::LowerPair:
        case SceneAction::RespawnNightCast:
        case SceneAction::RespawnFragment:
        case SceneAction::RespawnDistrict:
        case SceneAction::RestartScene:
            ok = false;
            error_ = "this beat is not ported yet -- see the action name";
            break;
    }

    if (!spoke && step.script != ScriptId::None)
        dialogue_.open(scriptFor(step.script), TextMode::Message);
    return ok;
}

void Game::frame(const Pad& pad) {
    ++frame_;
    pad_ = pad;

    // TextUpdate, before the scene.  A box that closed this frame has already
    // consumed the button that closed it by the time anything else looks --
    // which is text.s's TextClose masking A and B out of padPressed, and is
    // load-bearing: without it, closing Kairi's message immediately reopens it.
    dialogue_.update(pad_);

    // SceneUpdate.  GameOverUpdate runs INSTEAD of it when the player is out of
    // HP -- "being out of HP takes priority over whatever the scene was doing"
    // (main.s:602) -- and the world still updates underneath, which is why the
    // dispatch is here and not around the whole frame.
    StageStep step;
    if (world_.deadFlag != 0) {
        SceneView v = view();
        step = gameOverStep(world_, v);
    } else {
        step = machineStep();
        // THE @play TAIL.  Each scene's update ends in the part that runs when
        // no beat is in progress and the world is the player's; the machines
        // return an action when a beat IS in progress, so "the machine wanted
        // nothing and no box is up" is the condition for handing control back.
        //
        // THIS ORDERING IS NOT ORACLE-CHECKED.  No trace scenario presses A next
        // to anything, so trace_check.py has never exercised an interaction and
        // cannot adjudicate this line.  It is the reading of the assembly that
        // interact.h states -- "the `@play` tail of its scene's update" -- and
        // it is flagged here rather than left looking as settled as the code
        // around it.
        if (step.action == SceneAction::None && !dialogue_.busy())
            step = interactStep();
    }
    lastAction_ = step.action;
    lastOk_ = perform(step);

    SceneView v = view();
    updateWorld(world_, v, fx_);

    // RaceRun tests where Sora IS STANDING rather than where he went, so it runs
    // AFTER UpdateWorld -- a player who crossed the line during the frame is
    // credited on it (interact.h).
    if (scene_ == SceneId::Island && dive_.stage() == DiveStage::Arrived) {
        SceneView rv = view();
        islandRaceCheck(island_, rv);
    }

    // THE RETURN VALUE updateWorld() THREW AWAY.  world.cpp:905 calls this and
    // casts the result to void under a comment saying the rest "is the device
    // tier's half, because soraFrameCur is a fact about VRAM and not about the
    // world".  This is that half, and this is the first caller the cel has ever
    // had.  Calling it a second time is free of consequence: it derives the cel
    // from the facing and the walk counter and writes the same mirror bit it
    // just wrote, so it is idempotent within a frame -- which is why this is a
    // plain call rather than a value threaded out of updateWorld().
    soraCel_ = updateSoraFrame(actors_, player_);

    updateCamera(cam_, actors_, player_, bounds_, fx_.shakeX);
}

}  // namespace kh::device
