// The island and night stage machines.
//
// §M5's exit criteria again: every transition driven with synthetic input, every
// beat's frame count asserted, no renderer.
//
// The specification is docs/BEHAVIOUR.md §6, which is WRONG about the island in
// four places and incomplete about the night in one.  BEHAVIOUR-AUDIT.md
// findings 8, 13, 14, 15, 16 carry the corrections; every assertion below cites
// which one it is testing, and where the audit is itself incomplete this file
// says so.

#include "check.h"
#include "stage.h"

using namespace kh;

namespace {

struct Stub {
    Actors actors;
    Dialogue dialogue;
    Pad pad;
    SceneGround ground;
    Rng rng;
    int player = 0;
    uint32_t frame = 0;

    Stub() {
        actors.clear();
        player = actors.spawn(ActType::Sora, tileCentre(11), tileCentre(12));
    }
    SceneView view() {
        return SceneView{actors, dialogue, pad, ground, rng, player, frame};
    }
};

template <class M>
StageStep step(M& m, Stub& w, ScreenFx& fx) {
    SceneView v = w.view();
    ++w.frame;
    return m.update(v, fx);
}

const uint8_t ONE_LINE[] = {'H', 'I', '.', SC_END};

void openBox(Stub& w) {
    w.dialogue.open(Script{ONE_LINE, sizeof ONE_LINE}, TextMode::Message);
}

void dismiss(Stub& w) {
    for (int i = 0; i < 16 && w.dialogue.state() == TextState::Reveal; ++i)
        w.dialogue.update(w.pad);
    w.pad.held = raw(Button::A);
    w.pad.pressed = raw(Button::A);
    w.dialogue.update(w.pad);
    w.pad.held = 0;
    w.pad.pressed = 0;
}

}  // namespace

// ===========================================================================
// Destiny Islands
// ===========================================================================

KH_TEST(stage_island_kairi_drives_idle_active_done) {
    // §6 gives IDLE -> ACTIVE -> DONE but not what causes each step.  Talking to
    // Kairi is the whole of it, and what she does depends on where the quest is.
    IslandMachine m;
    m.begin();
    CHECK(m.state() == QuestState::Idle);
    CHECK_EQ(m.day(), 1);

    m.talkToKairi(false);
    CHECK(m.state() == QuestState::Active);     // the list, first time asking

    m.talkToKairi(false);
    CHECK(m.state() == QuestState::Active);     // still something missing

    m.talkToKairi(true);
    CHECK(m.state() == QuestState::Done);       // that's everything
}

KH_TEST(stage_island_day_one_turns_in_by_itself) {
    // Audit finding 14: "Day one's DONE->DAYOUT fires automatically with no
    // player action once the line is dismissed."  Day TWO's does not.
    Stub w;
    ScreenFx fx;
    IslandMachine m;
    m.begin();
    m.setState(QuestState::Done);
    m.setDay(1);

    openBox(w);
    for (int i = 0; i < 8; ++i) step(m, w, fx);
    CHECK(m.state() == QuestState::Done);       // held by the box

    dismiss(w);
    step(m, w, fx);
    CHECK(m.state() == QuestState::DayOut);
    CHECK_EQ(m.dayTimer(), DAY_FADE);

    // Day two at Done waits, however long you leave it.
    IslandMachine n;
    n.begin();
    n.setState(QuestState::Done);
    n.setDay(2);
    for (int i = 0; i < 300; ++i) step(n, w, fx);
    CHECK(n.state() == QuestState::Done);
}

KH_TEST(stage_island_the_day_change_ignores_the_dialogue_box) {
    // NOT IN EITHER DOCUMENT.  DayOut and DayIn are dispatched BEFORE the
    // TextBusy check (island.s:69-76), so they run through an open box exactly as
    // the night's Tear and EndFade do -- and the audit's list of exceptions names
    // only the night's two.
    Stub w;
    ScreenFx fx;
    IslandMachine m;
    m.begin();
    m.setState(QuestState::DayOut);
    m.setDay(1);
    openBox(w);
    CHECK(w.dialogue.busy());

    const int before = m.dayTimer();
    step(m, w, fx);
    CHECK(m.dayTimer() != before);              // it advanced anyway
}

KH_TEST(stage_island_the_day_change_is_thirty_out_and_thirty_in) {
    // §6: "Day change: 30 frames out, 30 frames in, cast rebuilt at black."
    //
    // The two halves do NOT mirror each other, and the reason is an instruction
    // order: DayOut does `dec dayTimer` then `lsr a`, and `dec` is on memory, so
    // the accumulator still holds the PRE-decrement value.  DayIn reloads the
    // POST-decrement value.  So one ramps 15 down and the other 0 up.
    Stub w;
    ScreenFx fx;
    IslandMachine m;
    m.begin();
    m.setState(QuestState::Done);
    m.setDay(1);
    step(m, w, fx);
    CHECK(m.state() == QuestState::DayOut);

    int out = 0;
    uint8_t first = 99, last = 99;
    int rebuiltAt = -1;
    while (m.state() == QuestState::DayOut && out < 4096) {
        const StageStep s = step(m, w, fx);
        ++out;
        if (out == 1) first = fx.brightness;
        if (s.action == SceneAction::RebuildForDayTwo) rebuiltAt = out;
        else last = fx.brightness;
    }
    CHECK_EQ(out, DAY_FADE + 1);            // 30 of dimming, then the rebuild
    CHECK_EQ(first, 15);                    // dayTimer >> 1, read BEFORE the step
    CHECK_EQ(last, 0);
    CHECK_EQ(rebuiltAt, DAY_FADE + 1);
    CHECK_EQ(m.day(), 2);
    CHECK(m.state() == QuestState::DayIn);
    CHECK_EQ(m.dayTimer(), DAY_FADE);
    CHECK_EQ(fx.brightness, 0);

    int in = 0;
    uint8_t firstIn = 99;
    while (m.state() == QuestState::DayIn && in < 4096) {
        step(m, w, fx);
        ++in;
        if (in == 1) firstIn = fx.brightness;
    }
    CHECK_EQ(in, DAY_FADE + 1);
    CHECK_EQ(firstIn, 0);                   // (DAY_FADE - dayTimer) >> 1, AFTER
    CHECK_EQ(fx.brightness, 15);
    // Audit finding 14: DayIn ends at IDLE, which §6's diagram does not show, so
    // day two re-enters IDLE -> ACTIVE -> DONE.
    CHECK(m.state() == QuestState::Idle);
    CHECK_EQ(m.day(), 2);
}

KH_TEST(stage_island_day_two_needs_kairi_again_for_the_race) {
    // Audit finding 14: "Day two's DONE->RACE_SET requires talking to Kairi
    // again."  And §6: "Race countdown: 192 frames, 64 to a number."
    Stub w;
    ScreenFx fx;
    IslandMachine m;
    m.begin();
    m.setDay(2);
    m.setState(QuestState::Done);
    for (int i = 0; i < 200; ++i) step(m, w, fx);
    CHECK(m.state() == QuestState::Done);

    m.talkToKairi(true);
    CHECK(m.state() == QuestState::RaceSet);
    CHECK_EQ(m.dayTimer(), COUNT_LEN);
    CHECK_EQ(COUNT_LEN / 3, 64);            // 64 frames to a number

    int frames = 0;
    while (m.state() == QuestState::RaceSet && frames < 4096) {
        step(m, w, fx);
        ++frames;
    }
    CHECK_EQ(frames, COUNT_LEN + 1);
    CHECK(m.state() == QuestState::RaceRun);
}

KH_TEST(stage_island_riku_wins_a_same_frame_tie) {
    // Audit finding 8: "RaceRun checks `rikuWp >= RACE_WPS` FIRST, so Riku wins
    // a same-frame tie."  This is the one assertion in the file that a reordered
    // port would silently invert.
    Stub w;
    ScreenFx fx;
    IslandMachine m;
    m.begin();
    m.setDay(2);
    m.setState(QuestState::RaceRun);
    m.tagPaopu();                           // Sora is on his way home
    CHECK_EQ(m.raceLeg(), 1);

    // Both arrive on the same frame: set Riku home AND ask for Sora's finish.
    m.setRikuWaypoint(RACE_WPS);
    const StageStep s = step(m, w, fx);
    CHECK(m.state() == QuestState::RaceOver);
    CHECK_EQ(m.raceWon(), 2);               // Riku
    CHECK(s.script == ScriptId::IslandRikuWins);

    // And Sora's finish is then refused, because the race is over.
    m.reachHome();
    CHECK_EQ(m.raceWon(), 2);
}

KH_TEST(stage_island_sora_must_tag_the_paopu_before_home_counts) {
    // Audit finding 8: "Sora's course is two legs" -- the finish only counts once
    // raceLeg is 1.  Running straight home wins nothing.
    IslandMachine m;
    m.begin();
    m.setState(QuestState::RaceRun);
    CHECK_EQ(m.raceLeg(), 0);
    m.reachHome();
    CHECK_EQ(m.raceWon(), 0);               // refused: he never tagged it
    CHECK(m.state() == QuestState::RaceRun);

    m.tagPaopu();
    CHECK_EQ(m.raceLeg(), 1);
    m.tagPaopu();
    CHECK_EQ(m.raceLeg(), 1);               // and tagging twice is not two legs
    m.reachHome();
    CHECK_EQ(m.raceWon(), 1);
    CHECK(m.state() == QuestState::RaceOver);
}

KH_TEST(stage_island_a_loss_forces_excalibur_and_skips_the_prompt) {
    // Audit finding 14: "RACE_OVER goes to NAMING only if Sora won; if Riku won
    // it jumps straight to NAMED with RAFT_EXCALIBUR forced."  §6's diagram shows
    // only the winning path.
    Stub w;
    ScreenFx fx;
    IslandMachine m;
    m.begin();
    m.setState(QuestState::RaceRun);
    m.tagPaopu();
    m.setRikuWaypoint(RACE_WPS);
    step(m, w, fx);                         // Riku wins, says his line
    CHECK(m.state() == QuestState::RaceOver);

    const StageStep s = step(m, w, fx);     // the line is dismissed
    CHECK(m.state() == QuestState::Named);  // NOT Naming
    CHECK(m.raftName() == RaftName::Excalibur);
    CHECK(s.script == ScriptId::IslandRikuNames);
}

KH_TEST(stage_island_a_win_asks_for_the_name) {
    Stub w;
    ScreenFx fx;
    IslandMachine m;
    m.begin();
    m.setState(QuestState::RaceRun);
    m.tagPaopu();
    m.reachHome();
    CHECK_EQ(m.raceWon(), 1);
    CHECK(m.state() == QuestState::RaceOver);

    const StageStep ask = step(m, w, fx);
    CHECK(m.state() == QuestState::Naming);
    CHECK(ask.action == SceneAction::Ask);      // TM_RAFT, three names
    CHECK(ask.script == ScriptId::IslandWhatName);
    CHECK_EQ(menuCount(TextMode::Raft), 3);

    // Still choosing: result 0 holds the machine where it is.
    for (int i = 0; i < 60; ++i) step(m, w, fx);
    CHECK(m.state() == QuestState::Naming);

    // Answer it.  The name is 1-based because 0 means unanswered.
    w.dialogue.open(Script{ONE_LINE, sizeof ONE_LINE}, TextMode::Raft);
    dismiss(w);                                 // Wait -> Prompt
    w.pad.held = raw(Button::Down);
    w.pad.pressed = raw(Button::Down);
    w.dialogue.update(w.pad);
    w.pad.held = raw(Button::A);
    w.pad.pressed = raw(Button::A);
    w.dialogue.update(w.pad);
    w.pad.held = 0;
    w.pad.pressed = 0;
    CHECK_EQ(w.dialogue.result(), 2);
    const StageStep named = step(m, w, fx);
    CHECK(m.state() == QuestState::Named);
    CHECK(m.raftName() == RaftName::Excalibur); // choice 2
    // namedLines[choice - 1], generated from the assembly's .word run.
    CHECK(named.script == ScriptId::IslandNamedExcalibur);
    CHECK(NAMEDLINES[1] == ScriptId::IslandNamedExcalibur);
    CHECK_EQ(named.arg, 2);
}

KH_TEST(stage_island_dusk_is_armed_by_kairi_and_runs_thirty_frames) {
    // Audit finding 16: "Q_DUSK ... is DAY_FADE=30 frames ... It is armed by
    // talking to Kairi at Q_NAMED, not by anything else."
    //
    // The finding also gives the brightness as "(dayTimer-1)>>1 from 14 to 0".
    // That is WRONG: `dec dayTimer` is a memory decrement, so the `lsr a` that
    // follows halves the accumulator's PRE-decrement value.  The ramp starts at
    // 15, not 14 -- the same instruction-order trap as DayOut, which is where the
    // finding probably got it from.
    Stub w;
    ScreenFx fx;
    IslandMachine m;
    m.begin();
    m.setState(QuestState::Named);
    for (int i = 0; i < 120; ++i) step(m, w, fx);
    CHECK(m.state() == QuestState::Named);      // nothing else arms it

    m.talkToKairi(true);
    CHECK(m.state() == QuestState::Dusk);
    CHECK_EQ(m.dayTimer(), DAY_FADE);

    int frames = 0;
    uint8_t first = 99;
    int nightAt = -1;
    while (frames < 4096) {
        const StageStep s = step(m, w, fx);
        ++frames;
        if (frames == 1) first = fx.brightness;
        if (s.action == SceneAction::BeginNight) { nightAt = frames; break; }
    }
    CHECK_EQ(first, 15);                        // NOT 14; see above
    CHECK_EQ(nightAt, DAY_FADE + 1);
    CHECK_EQ(fx.brightness, 0);
}

// ===========================================================================
// The night the island falls
// ===========================================================================

KH_TEST(stage_night_the_keyblade_starts_absent) {
    // Audit, LoadScene's side effects: "keyGot is set to 1 ... The night is the
    // only scene that clears keyGot, and it does so itself after LoadScene."
    // Before it, the Shadows can neither be hurt nor hurt Sora.
    Rng rng;
    NightMachine m;
    m.begin(rng);
    CHECK(!m.keyGot());
    CHECK(m.stage() == NightStage::Intro);
}

KH_TEST(stage_night_beginning_and_restarting_both_draw_from_the_lfsr) {
    // ArmLightning is one `Rand`, and it is called from THREE places:
    // NightBegin (night.s:83), NightRestart (night.s:108) and the end of a wait
    // (night.s:302).  All three matter, because the flash wait and the Shadow
    // spots come out of the SAME register -- a night that skips a draw runs
    // every later arrival off a different sequence than the oracle's.
    //
    // This file used to leave both of the first two undrawn: begin() set the
    // wait to zero and restart() set it to the floor, under a comment that said
    // it drew.  §M6b's night trace is what found it -- 370 frames that could not
    // agree until the draws were in the right places.
    Rng r;
    ScreenFx fx;
    NightMachine m;
    const uint16_t before = r.state();
    m.begin(r);
    CHECK(r.state() != before);                     // it DREW
    CHECK(m.flashWait() >= FLASH_GAP_MIN);
    CHECK(m.flashWait() <= FLASH_GAP_MIN + FLASH_GAP_VAR);
    // ...and it opens with a flash and a full spawn gap, both of which
    // NightBegin sets before the first frame runs.
    CHECK_EQ(m.flashTimer(), FLASH_LEN);
    CHECK_EQ(m.spawnTimer(), m.shadowGap());

    // $ACE1 draws $EF, so the opening wait is 150 + (0xEF & 127) = 261 -- the
    // number the oracle's WRAM holds on the frame the night starts.
    Rng seeded;
    NightMachine n;
    n.begin(seeded);
    CHECK_EQ(n.flashWait(), FLASH_GAP_MIN + (0xEF & FLASH_GAP_VAR));
    CHECK_EQ(n.flashWait(), 261);

    // A retry draws again, so it does not reproduce the run that killed him.
    const uint16_t mid = seeded.state();
    n.setStage(NightStage::Riku);
    n.restart(seeded, false, fx);
    CHECK(seeded.state() != mid);
    CHECK_EQ(n.flashTimer(), 0);                    // NightRestart clears it
    CHECK_EQ(n.spawnTimer(), n.shadowGap());
}

KH_TEST(stage_night_how_dense_the_night_is_belongs_to_the_map) {
    // One pair of numbers on the SNES because the night and the fragment ran on
    // maps of similar size; three pairs here because they no longer do, and
    // SHADOW_MAX_FRAG existed with nothing able to select it until the oracle
    // fixture needed the same mechanism.  divergence 003.
    Stub w;
    ScreenFx fx;
    NightMachine m;
    m.begin(w.rng);
    CHECK_EQ(m.shadowMax(), SHADOW_MAX_NIGHT);      // the island, by default
    CHECK_EQ(m.shadowGap(), SHADOW_GAP);

    // The SNES's density, which is what an oracle fixture runs at.
    m.setDensity(SNES_SHADOW_MAX, SNES_SHADOW_GAP);
    CHECK_EQ(m.shadowMax(), SNES_SHADOW_MAX);
    CHECK_EQ(m.shadowGap(), SNES_SHADOW_GAP);

    const Tile spots[] = {{20, 15}, {30, 12}, {38, 17}, {16, 19}, {24, 20},
                          {33, 22}, {12, 18}, {28, 25}};
    m.setSpots(spots, 8);
    m.setStage(NightStage::Seek);
    // Run long enough to fill twice over and it holds at SIX, not twenty.
    for (int i = 0; i < (SNES_SHADOW_GAP + 1) * (SNES_SHADOW_MAX + 8); ++i)
        step(m, w, fx);
    CHECK_EQ(w.actors.count(ActType::Shadow), SNES_SHADOW_MAX);
    // ...and the gap it filled at was the SNES's, not the island's.
    CHECK(SNES_SHADOW_GAP != SHADOW_GAP);

    // The fragment is the third pair, and it is the SNES's numbers because the
    // fragment's map is the one the DS did not expand.
    NightMachine f;
    Stub v;
    f.begin(v.rng);
    f.setDensity(SHADOW_MAX_FRAG, SNES_SHADOW_GAP);
    CHECK_EQ(f.shadowMax(), SNES_SHADOW_MAX);
}

KH_TEST(stage_night_lightning_is_two_strikes_in_one_flash) {
    // §6: "Lightning flash 8, as bright / out / brighter / out -- two strikes in
    // one flash."  The exact amounts and their order are not in the doc: the ramp
    // reads the POST-decrement timer, giving 16, 16, 0, 0, 26, 26, 0, 0.
    Stub w;
    ScreenFx fx;
    NightMachine m;
    m.begin(w.rng);
    m.setStage(NightStage::Seek);
    openBox(w);                                 // the storm runs through it

    // THE STORM ARRIVES WITH THE FIRST FLASH.  NightBegin sets flashTimer to
    // FLASH_LEN itself (night.s:89) rather than leaving the first frame to arm
    // one -- "no fade back in", the comment says, because the flash IS the fade
    // in.  This test used to spend a frame arming; that frame was the port's and
    // not the SNES's, and the night trace is what said so.
    CHECK_EQ(m.flashTimer(), FLASH_LEN);
    uint8_t seen[FLASH_LEN] = {};
    for (int i = 0; i < FLASH_LEN; ++i) {
        step(m, w, fx);
        seen[i] = fx.whiteout;
    }
    CHECK_EQ(seen[0], 16);
    CHECK_EQ(seen[1], 16);
    CHECK_EQ(seen[2], 0);
    CHECK_EQ(seen[3], 0);
    CHECK_EQ(seen[4], 26);                      // the one that lights the island
    CHECK_EQ(seen[5], 26);
    CHECK_EQ(seen[6], 0);
    CHECK_EQ(seen[7], 0);
    CHECK_EQ(m.flashTimer(), 0);
}

KH_TEST(stage_night_the_gap_between_flashes_is_150_plus_a_random_127) {
    // §6: "Between flashes 150 + random 0-127."  Drawn from the same LFSR the
    // spawn spots use, so the sequence is part of the trace.
    Stub w;
    ScreenFx fx;
    NightMachine m;
    m.begin(w.rng);
    m.setStage(NightStage::Seek);

    // ArmLightning ran inside begin(), so the wait for the SECOND flash is
    // already drawn before the first frame -- and drawing it is what keeps this
    // machine's LFSR in step with the oracle's.
    const int wait = m.flashWait();
    CHECK(wait >= FLASH_GAP_MIN);
    CHECK(wait <= FLASH_GAP_MIN + FLASH_GAP_VAR);

    // The flash itself, then the wait, then the next strike.
    int frames = 0;
    while (m.flashTimer() != 0 && frames < 64) { step(m, w, fx); ++frames; }
    CHECK_EQ(frames, FLASH_LEN);
    int waited = 0;
    while (m.flashTimer() == 0 && waited < 4096) { step(m, w, fx); ++waited; }
    CHECK_EQ(waited, wait + 1);                 // counted down to zero, then fires
}

KH_TEST(stage_night_riku_and_kairi_become_columns_after_dark_hold) {
    // §6: "Column of darkness holds 96."  The column REPLACES the person rather
    // than being spawned beside them, and the first pass is detected by the timer
    // still holding its entry value -- one counter serving both halves.
    Stub w;
    ScreenFx fx;
    NightMachine m;
    m.begin(w.rng);
    m.setStage(NightStage::Seek);

    m.talkToRiku();
    CHECK(m.stage() == NightStage::Riku);
    CHECK_EQ(m.nightTimer(), DARK_HOLD);

    const StageStep first = step(m, w, fx);
    CHECK(first.action == SceneAction::ColumnForRiku);
    CHECK_EQ(m.nightTimer(), DARK_HOLD - 1);

    int frames = 1;
    while (m.stage() == NightStage::Riku && frames < 4096) {
        step(m, w, fx);
        ++frames;
    }
    CHECK_EQ(frames, DARK_HOLD + 1);            // 96 held, then the transition
    CHECK(m.stage() == NightStage::Key);
}

KH_TEST(stage_night_the_keyblade_arrives_on_a_flash_of_its_own) {
    Stub w;
    ScreenFx fx;
    NightMachine m;
    m.begin(w.rng);
    m.setStage(NightStage::Key);
    CHECK(!m.keyGot());

    const StageStep s = step(m, w, fx);
    CHECK(m.keyGot());
    CHECK(m.stage() == NightStage::Kairi);
    CHECK(s.action == SceneAction::ClearColumns);
    CHECK(s.script == ScriptId::NightKey);
    CHECK_EQ(m.flashTimer(), FLASH_LEN);        // "it arrives on a flash of its own"
}

KH_TEST(stage_night_kairi_opens_the_door_by_retyping_it) {
    // Audit finding 13: "OpenTheDoor converts ACT_DOOR to ACT_DOOROPEN (31),
    // which falls outside FindProp's 28..30 scan" -- so once it has opened it can
    // no longer be examined.  A port that SPAWNS a second actor leaves the old
    // one standing behind it and keeps it examinable.
    Stub w;
    ScreenFx fx;
    NightMachine m;
    m.begin(w.rng);
    m.setStage(NightStage::Kairi);

    const StageStep s = m.talkToKairi();
    CHECK(m.stage() == NightStage::Door);
    CHECK(s.action == SceneAction::OpenTheDoor);
    CHECK(s.script == ScriptId::NightKairi);
    CHECK_EQ(m.nightTimer(), DARK_HOLD);
    // The retyped door is outside the examinable range, which actor.h pins.
    CHECK(isWallProp(ActType::Door));
    CHECK(!isWallProp(ActType::DoorOpen));

    const StageStep first = step(m, w, fx);
    CHECK(first.action == SceneAction::ColumnForKairi);
    CHECK_EQ(m.flashTimer(), FLASH_LEN);        // hers arrives on a flash; his did not

    int frames = 1;
    while (m.stage() == NightStage::Door && frames < 4096) {
        step(m, w, fx);
        ++frames;
    }
    CHECK_EQ(frames, DARK_HOLD + 1);
    CHECK(m.stage() == NightStage::Tear);
    CHECK_EQ(m.nightTimer(), TEAR_LEN);
    CHECK_EQ(m.flashTimer(), 0);                // must not be left switched on
}

KH_TEST(stage_night_the_tear_reaches_full_mosaic_where_the_shatter_did_not) {
    // §6: "Island tearing apart 120."  The consequence the doc does not draw:
    // 120/8 is 15, so the Tear reaches mosaic 15 and brightness 0, while the
    // Dive's 96-frame shatter only reaches 12 and 3.  Audit finding 6 adds that
    // the Tear shakes +/-3 and the shatter +/-2.
    Stub w;
    ScreenFx fx;
    NightMachine m;
    m.begin(w.rng);
    m.setStage(NightStage::Door);
    // Come in through the door beat so the timer is armed as the scene arms it.
    m.talkToKairi();
    while (m.stage() == NightStage::Door) step(m, w, fx);
    CHECK(m.stage() == NightStage::Tear);

    // It runs THROUGH an open box, so the line is on screen while it happens.
    openBox(w);
    uint8_t peakMosaic = 0, lowBright = 15;
    int8_t sawPlus = 0, sawMinus = 0;
    int frames = 0;
    int enterAt = -1;
    while (frames < 4096) {
        const StageStep s = step(m, w, fx);
        ++frames;
        if (s.action == SceneAction::EnterFragment) { enterAt = frames; break; }
        if (fx.mosaic > peakMosaic) peakMosaic = fx.mosaic;
        if (fx.brightness < lowBright) lowBright = fx.brightness;
        if (fx.shakeX == 3) sawPlus = 1;
        if (fx.shakeX == -3) sawMinus = 1;
    }
    CHECK_EQ(enterAt, TEAR_LEN + 1);
    CHECK_EQ(peakMosaic, 15);                   // the Dive's reached 12
    CHECK_EQ(lowBright, 0);                     // the Dive's reached 3
    CHECK_EQ(sawPlus, 1);
    CHECK_EQ(sawMinus, 1);
    CHECK(m.stage() == NightStage::Boss);
    CHECK(m.keyGot());
}

KH_TEST(stage_night_the_boss_sweeps_its_craters_where_the_dive_does_not) {
    // BEHAVIOUR.md §12, latent bug 1: "The Dive does not sweep Darkside's crater
    // Shadows (the night's script does)."  So this transition must emit the sweep
    // and the Dive's must not -- which is the difference the bug consists of.
    Stub w;
    ScreenFx fx;
    NightMachine m;
    m.begin(w.rng);
    m.setStage(NightStage::Boss);
    w.actors.spawn(ActType::Darkside, World::fromInt(0), World::fromInt(0));
    for (int i = 0; i < 120; ++i) step(m, w, fx);
    CHECK(m.stage() == NightStage::Boss);

    for (int i = 0; i < MAX_ACTORS; ++i)
        if (w.actors.type[i] == ActType::Darkside) w.actors.type[i] = ActType::None;
    const StageStep s = step(m, w, fx);
    CHECK(m.stage() == NightStage::End);
    CHECK(s.action == SceneAction::SweepCraterShadows);
    CHECK_EQ(m.nightTimer(), END_FADE);

    // And the Dive's equivalent transition asks for no sweep.
    DiveMachine d;
    d.begin();
    d.setStage(DiveStage::Boss);
    const StageStep ds = step(d, w, fx);
    CHECK(d.stage() == DiveStage::Done);
    CHECK(ds.action == SceneAction::Say);       // not SweepCraterShadows
}

KH_TEST(stage_night_the_flash_is_suppressed_from_end_onward) {
    // §6: "A flash is skipped from END onward -- a strike resetting the unit
    // under the closing fade undid the fade every time one landed."
    //
    // THIS IS THE REASON ScreenFx EXISTS, so it gets the most direct test in the
    // file.  Come in through Boss rather than a setter, with a flash MID-RAMP, so
    // the thing being suppressed is a real strike and not a fresh one.
    Stub w;
    ScreenFx fx;
    NightMachine m;
    m.begin(w.rng);
    m.setStage(NightStage::Boss);
    w.actors.spawn(ActType::Darkside, World::fromInt(0), World::fromInt(0));

    CHECK_EQ(m.flashTimer(), FLASH_LEN);        // begin() armed one
    step(m, w, fx);                             // Boss < End, so it runs
    CHECK_EQ(m.flashTimer(), FLASH_LEN - 1);
    step(m, w, fx);
    CHECK_EQ(m.flashTimer(), FLASH_LEN - 2);    // ...and keeps running

    for (int i = 0; i < MAX_ACTORS; ++i)
        if (w.actors.type[i] == ActType::Darkside) w.actors.type[i] = ActType::None;
    step(m, w, fx);                             // the last Boss frame, then End
    CHECK(m.stage() == NightStage::End);
    const int frozen = m.flashTimer();
    CHECK(frozen > 0);                          // a strike is mid-ramp
    CHECK(frozen < FLASH_LEN);

    int frames = 0;
    uint8_t previous = fx.whiteout;
    bool wentBackwards = false;
    while (frames < 4096) {
        const StageStep s = step(m, w, fx);
        ++frames;
        if (s.action == SceneAction::Say) break;
        if (fx.whiteout < previous) wentBackwards = true;
        previous = fx.whiteout;
    }
    CHECK(!wentBackwards);                      // no strike ever undid the fade
    CHECK_EQ(m.flashTimer(), frozen);           // and the flash never advanced
    CHECK_EQ(frames, END_FADE + 1);
    CHECK_EQ(fx.whiteout, 31);
    CHECK(m.stage() == NightStage::Over);
}

KH_TEST(stage_night_end_fade_runs_through_the_box_and_ramps_to_31) {
    // §6: "Closing fade 62, subtractive to full."  END_FADE is 62, so halving
    // the elapsed count lands exactly on the five bits the register takes.
    Stub w;
    ScreenFx fx;
    NightMachine m;
    m.begin(w.rng);
    m.setStage(NightStage::Boss);
    w.actors.spawn(ActType::Darkside, World::fromInt(0), World::fromInt(0));
    for (int i = 0; i < MAX_ACTORS; ++i)
        if (w.actors.type[i] == ActType::Darkside) w.actors.type[i] = ActType::None;
    step(m, w, fx);
    CHECK(m.stage() == NightStage::End);

    openBox(w);                                 // it ignores this
    uint8_t first = 99;
    int frames = 0;
    while (frames < 4096) {
        const StageStep s = step(m, w, fx);
        ++frames;
        if (frames == 1) first = fx.whiteout;
        if (s.action == SceneAction::Say) break;
    }
    CHECK_EQ(first, 0);                         // (END_FADE - timer) >> 1, AFTER
    CHECK_EQ(frames, END_FADE + 1);
    CHECK_EQ(END_FADE / 2, 31);
    CHECK_EQ(fx.whiteout, 31);
}

KH_TEST(stage_night_the_card_hands_over_to_the_town) {
    Stub w;
    ScreenFx fx;
    NightMachine m;
    m.begin(w.rng);
    m.setStage(NightStage::Over);
    const StageStep s = step(m, w, fx);
    CHECK(s.action == SceneAction::EnterTown);
}

KH_TEST(stage_night_every_stage_is_reachable_end_to_end) {
    // A walk of the whole scene, which is what proves the transitions compose
    // rather than each working in isolation from a setter.
    Stub w;
    ScreenFx fx;
    NightMachine m;
    m.begin(w.rng);
    bool seen[int(NightStage::Over) + 1] = {};
    seen[int(m.stage())] = true;

    step(m, w, fx);                             // Intro -> Seek
    seen[int(m.stage())] = true;
    m.talkToRiku();                             // Seek -> Riku
    seen[int(m.stage())] = true;
    while (m.stage() == NightStage::Riku) step(m, w, fx);
    seen[int(m.stage())] = true;                // Key
    step(m, w, fx);                             // Key -> Kairi
    seen[int(m.stage())] = true;
    m.talkToKairi();                            // Kairi -> Door
    seen[int(m.stage())] = true;
    while (m.stage() == NightStage::Door) step(m, w, fx);
    seen[int(m.stage())] = true;                // Tear
    while (m.stage() == NightStage::Tear) step(m, w, fx);
    seen[int(m.stage())] = true;                // Boss
    CHECK(m.stage() == NightStage::Boss);
    while (m.stage() == NightStage::Boss) step(m, w, fx);
    seen[int(m.stage())] = true;                // End
    while (m.stage() == NightStage::End) step(m, w, fx);
    seen[int(m.stage())] = true;                // Over
    CHECK(m.stage() == NightStage::Over);

    for (int i = 0; i <= int(NightStage::Over); ++i) CHECK(seen[i]);
}

// ===========================================================================
// Death and retry
// ===========================================================================

KH_TEST(stage_night_a_death_before_the_keyblade_takes_it_back) {
    // Audit, NightRestart's rewind rules: "stage < N_KAIRI rewinds to N_SEEK and
    // CLEARS keyGot; stage >= N_KAIRI rewinds to N_KAIRI, keeps the Keyblade and
    // re-runs OpenTheDoor."  LoadScene hands keyGot back as 1, so the night is
    // the only scene that has to take it away again.
    Rng rng;
    ScreenFx fx;
    NightMachine m;
    m.begin(rng);
    m.setStage(NightStage::Riku);            // still looking for him
    const StageStep before = m.restart(rng, false, fx);
    CHECK(m.stage() == NightStage::Seek);
    CHECK(!m.keyGot());
    CHECK(before.action == SceneAction::RespawnNightCast);

    // ...and after it, the Keyblade stays and the door stays open.
    NightMachine n;
    n.begin(rng);
    n.setStage(NightStage::Door);
    const StageStep after = n.restart(rng, false, fx);
    CHECK(n.stage() == NightStage::Kairi);
    CHECK(n.keyGot());
    CHECK(after.action == SceneAction::RespawnNightCast);

    // On the fragment it re-raises the boss instead.
    NightMachine f;
    f.begin(rng);
    f.setStage(NightStage::Boss);
    const StageStep frag = f.restart(rng, true, fx);
    CHECK(f.stage() == NightStage::Boss);
    CHECK(f.keyGot());
    CHECK(frag.action == SceneAction::RespawnFragment);
}

KH_TEST(stage_night_a_death_mid_tear_puts_the_screen_back) {
    // "A death can land in the middle of the island coming apart, so put the
    // screen-wide effects back before anything else."  Mid-Tear the mosaic is
    // coarse, the screen is shaking and the brightness is most of the way to
    // black -- and none of that belongs to the retry.  This is the clearest case
    // for arbitrating effects in one place.
    Stub w;
    ScreenFx fx;
    NightMachine m;
    m.begin(w.rng);
    m.setStage(NightStage::Door);
    m.talkToKairi();
    while (m.stage() == NightStage::Door) step(m, w, fx);
    CHECK(m.stage() == NightStage::Tear);
    for (int i = 0; i < TEAR_LEN / 2; ++i) step(m, w, fx);

    CHECK(fx.mosaic > 0);                    // genuinely mid-effect
    CHECK(fx.brightness < 15);
    CHECK(fx.shakeX != 0);

    m.restart(w.rng, false, fx);
    CHECK_EQ(fx.mosaic, 0);
    CHECK_EQ(fx.shakeX, 0);
    CHECK_EQ(fx.whiteout, 0);
    CHECK_EQ(fx.brightness, 15);
    CHECK(!fx.forcedBlank);
    CHECK(fx.bgVisible);
    CHECK_EQ(m.flashTimer(), 0);
    CHECK_EQ(m.nightTimer(), 0);
    // The Tear was past N_KAIRI, so the Keyblade stays.
    CHECK(m.stage() == NightStage::Kairi);
    CHECK(m.keyGot());
}

KH_TEST(stage_dive_a_death_rewinds_to_the_station_that_is_loaded) {
    // Audit, the checkpoint table: "DIVE3 -> soraOnly + SpawnBoss + DIVE_BOSS;
    // DIVE2 -> SpawnStation2 + DIVE_S2_FIGHT; DIVE1 -> diveSpawns + DIVE_PICK."
    ScreenFx fx;
    DiveMachine m;
    m.begin();
    m.setStage(DiveStage::Fall);
    CHECK(m.restart(SceneId::Dive3, fx).action == SceneAction::SpawnBoss);
    CHECK(m.stage() == DiveStage::Boss);

    m.setStage(DiveStage::Shatter2);
    CHECK(m.restart(SceneId::Dive2, fx).action == SceneAction::EnterStation2);
    CHECK(m.stage() == DiveStage::S2Fight);

    m.setStage(DiveStage::Drop);
    m.choose(ActType::Staff, ActType::Shield);
    m.restart(SceneId::Dive, fx);
    CHECK(m.stage() == DiveStage::Pick);
    // The choice is inert, so a retry deliberately does not un-choose it.
    CHECK(m.taken() == ActType::Staff);

    // And every restart clears the effects, because a death can land mid-shatter.
    CHECK_EQ(fx.mosaic, 0);
    CHECK_EQ(fx.shakeX, 0);
    CHECK_EQ(fx.whiteout, 0);
    CHECK_EQ(fx.brightness, 15);
    CHECK(fx.bgVisible);
    CHECK_EQ(m.shatterTimer(), 0);
    CHECK_EQ(m.fallTimer(), 0);
    CHECK_EQ(m.fadeTimer(), 0);
}

// ===========================================================================
// Three things the cross-check surfaced that neither document states
// ===========================================================================

KH_TEST(stage_island_the_transient_idle_reaches_the_hud) {
    // island.s:215-218 writes Idle, calls HudUpdate, and writes DayIn three
    // instructions later, all in one frame.  The Idle is not dead: the HUD it
    // draws is the one the player sees 31 frames later when the fade-in
    // finishes, and it has to be day two's empty checklist.
    Stub w;
    ScreenFx fx;
    IslandMachine m;
    m.begin();
    m.setState(QuestState::Done);
    m.setDay(1);
    step(m, w, fx);
    while (m.state() == QuestState::DayOut) step(m, w, fx);
    CHECK(m.state() == QuestState::DayIn);
    CHECK(m.hudState() == QuestState::Idle);    // NOT DayIn
}

KH_TEST(stage_island_the_gameplay_gate_is_a_range_not_a_flag) {
    // island.s:114-119: `cmp #Q_RACE_SET / bcc :+ / cmp #Q_NAMED / bne @out`.
    // Pickups, the Keyblade and conversation are live below RaceSet OR at Named
    // exactly -- so Named still allows gathering, and the race is the only thing
    // that takes the island away.
    IslandMachine m;
    m.begin();
    const QuestState live[] = {QuestState::Idle, QuestState::Active,
                               QuestState::Done, QuestState::DayOut,
                               QuestState::DayIn, QuestState::Named};
    for (QuestState s : live) {
        m.setState(s);
        CHECK(m.gameplayLive());
    }
    const QuestState dead[] = {QuestState::RaceSet, QuestState::RaceRun,
                               QuestState::RaceOver, QuestState::Naming,
                               QuestState::Dusk};
    for (QuestState s : dead) {
        m.setState(s);
        CHECK(!m.gameplayLive());
    }
}

KH_TEST(stage_island_tagging_and_winning_cannot_land_on_one_frame) {
    // RaceRun sets raceLeg and immediately returns without falling through to
    // the finish test (island.s:887-890), so a Sora who runs through the paopu
    // tree and straight home cannot win in a single frame.  And he starts the
    // race INSIDE the finish radius -- START is CELL(11,12), FINISH is
    // CELL(12,12), which is 256 apart against a 448 tolerance -- so raceLeg is
    // the only thing standing between him and an instant win.
    IslandMachine m;
    m.begin();
    m.setState(QuestState::RaceRun);
    CHECK_EQ(m.raceLeg(), 0);

    // Standing on the finish from the first frame wins nothing.
    m.reachHome();
    CHECK_EQ(m.raceWon(), 0);
    CHECK(m.state() == QuestState::RaceRun);

    // The two events are separate calls, which is what makes the same-frame case
    // unrepresentable rather than merely unlikely.
    m.tagPaopu();
    CHECK_EQ(m.raceWon(), 0);       // tagging alone still wins nothing
    m.reachHome();
    CHECK_EQ(m.raceWon(), 1);
}

KH_TEST(stage_night_shadows_arrive_through_both_searches_and_never_stop) {
    // night.s:487 and 645: SeekRiku AND SeekKairi both call SpawnShadows.  Unlike
    // the Second District's wave there is no cap and no counter -- they keep
    // arriving until the stage moves on, which is what makes the night a search
    // under pressure rather than a fight with an end.
    Stub w;
    ScreenFx fx;
    NightMachine m;
    m.begin(w.rng);
    const Tile spots[] = {{20, 15}, {30, 12}, {38, 17}, {16, 19}};
    m.setSpots(spots, 4);
    m.setStage(NightStage::Seek);
    // Sora is at (11,12); every spot above is well clear of him.

    // THE FIRST ARRIVAL IS NOT IMMEDIATE.  NightBegin sets spawnTimer to the gap
    // (night.s:81) before the search starts, so the island is empty for the first
    // seventy-one frames of it and the player gets that long to move.  This test
    // used to assert an arrival on frame one, which is what a machine whose
    // begin() left the timer at zero does; the night trace is what said so.
    int first = 0;
    while (first < 4096 && w.actors.count(ActType::Shadow) == 0) {
        step(m, w, fx);
        ++first;
    }
    CHECK_EQ(first, SHADOW_GAP + 1);
    int gap = 0;
    while (gap < 4096 && w.actors.count(ActType::Shadow) == 1) {
        step(m, w, fx);
        ++gap;
    }
    CHECK_EQ(gap, SHADOW_GAP + 1);

    // It fills to SHADOW_MAX_NIGHT and holds there -- no wave limit stops it.
    for (int i = 0; i < (SHADOW_GAP + 1) * (SHADOW_MAX_NIGHT + 6); ++i)
        step(m, w, fx);
    CHECK_EQ(w.actors.count(ActType::Shadow), SHADOW_MAX_NIGHT);

    // Kill them all and they come straight back, however many rounds pass.
    for (int k = 0; k < MAX_ACTORS; ++k)
        if (w.actors.type[k] == ActType::Shadow) w.actors.type[k] = ActType::None;
    for (int i = 0; i < (SHADOW_GAP + 1) * 3; ++i) step(m, w, fx);
    CHECK(w.actors.count(ActType::Shadow) > 0);

    // And the second search spawns too, which is the half a port forgets.
    Stub v;
    NightMachine n;
    n.begin(w.rng);
    n.setSpots(spots, 4);
    n.setStage(NightStage::Kairi);
    for (int i = 0; i <= SHADOW_GAP; ++i) step(n, v, fx);
    CHECK_EQ(v.actors.count(ActType::Shadow), 1);
}

KH_TEST(stage_night_a_spot_on_top_of_the_player_is_refused_here_too) {
    // The same 64 px rule as the Second District, because it is the same routine
    // in the assembly -- SpawnShadows and TownShadows differ only in constants.
    Stub w;
    ScreenFx fx;
    NightMachine m;
    m.begin(w.rng);
    const Tile one[] = {{11, 12}};      // exactly where Sora is standing
    m.setSpots(one, 1);
    m.setStage(NightStage::Seek);

    // The opening gap first -- begin() arms it -- and then the refusal.
    for (int i = 0; i <= SHADOW_GAP; ++i) step(m, w, fx);
    CHECK_EQ(w.actors.count(ActType::Shadow), 0);
    CHECK_EQ(m.spawnTimer(), SPAWN_RETRY);

    for (int i = 0; i < SPAWN_RETRY * 8; ++i) step(m, w, fx);
    CHECK_EQ(w.actors.count(ActType::Shadow), 0);

    // Move him more than 64 px away on one axis and the next attempt lands.
    w.actors.x[w.player] = tileCentre(20);
    for (int i = 0; i < SPAWN_RETRY + 2 && w.actors.count(ActType::Shadow) == 0; ++i)
        step(m, w, fx);
    CHECK_EQ(w.actors.count(ActType::Shadow), 1);
}
