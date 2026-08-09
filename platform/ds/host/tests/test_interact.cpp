// The interaction layer: who is standing next to what, and what a press does.
//
// §M5 delivered the stage machines and stated the rule that made them testable
// -- a machine returns an ACTION for the caller to perform.  It did not deliver
// the caller, so every one of `talkToKairi`, `talkToRiku`, `openDoor`,
// `arriveAtThird`, `tagPaopu`, `reachHome` and `choose` was public with nothing
// calling it.  These are the tests for the thing that calls them.
//
// Read against the assembly rather than against the oracle: none of it is
// reachable from a trace, because a trace drives a pad and not a plan.

#include "check.h"
#include "interact.h"

using namespace kh;

namespace {

// A SceneView holds REFERENCES, so one made at the top of a test stays current
// as the world underneath it changes -- which is why these tests build it once
// and then keep pressing buttons.
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
    void press(Button b) {
        pad.held = raw(b);
        pad.pressed = raw(b);
    }
    void release() {
        pad.held = 0;
        pad.pressed = 0;
    }
    void putPlayer(int i, int j) {
        actors.x[player] = tileCentre(i);
        actors.y[player] = tileCentre(j);
    }
};

}  // namespace

// ===========================================================================
// The dream weapons
// ===========================================================================

KH_TEST(interact_a_dream_weapon_is_asked_about_and_the_answer_moves_the_dive) {
    // dive.s:137 and 833.  PICK and DROP are the only two stages that read the
    // pad, the prompt is a PROMPT and not a message because its answer is what
    // advances the scene, and the description depends on which half of the
    // choice it is: descTake before, descDrop after.
    Stub w;
    DiveMachine m;
    m.begin();
    m.setStage(DiveStage::Pick);
    const int shield = w.actors.spawn(ActType::Shield, tileCentre(12),
                                      tileCentre(12));
    w.putPlayer(11, 12);                    // one tile away, inside REACH

    // Nothing happens without a press.
    SceneView v = w.view();
    CHECK(diveInteract(m, v, 0).action == SceneAction::None);

    w.press(Button::A);
    StageStep s = diveInteract(m, v, 0);
    CHECK(s.action == SceneAction::Ask);
    CHECK(s.script == ScriptId::DiveShieldTake);
    CHECK_EQ(m.pendingActor(), shield);
    CHECK(m.pendingWeapon() == ActType::Shield);
    CHECK(m.stage() == DiveStage::Pick);    // the ANSWER moves it, not the ask

    // "No" leaves everything where it was.
    w.release();
    CHECK(diveInteract(m, v, 2).action == SceneAction::None);
    CHECK(m.stage() == DiveStage::Pick);
    CHECK(w.actors.type[shield] == ActType::Shield);

    // "Yes" takes it: off the pedestal, on to DROP, and he is asked what he
    // will give up.
    s = diveInteract(m, v, 1);
    CHECK(s.action == SceneAction::Say);
    CHECK(s.script == ScriptId::DiveGiveUp);
    CHECK(m.stage() == DiveStage::Drop);
    CHECK(m.taken() == ActType::Shield);
    CHECK(w.actors.type[shield] == ActType::None);

    // The second half asks with the OTHER table, and its yes breaks the floor.
    const int staff = w.actors.spawn(ActType::Staff, tileCentre(12),
                                     tileCentre(12));
    w.press(Button::A);
    s = diveInteract(m, v, 0);
    CHECK(s.action == SceneAction::Ask);
    CHECK(s.script == ScriptId::DiveStaffDrop);
    w.release();
    s = diveInteract(m, v, 1);
    CHECK(s.action == SceneAction::Say);
    CHECK(s.script == ScriptId::DiveChosen);
    CHECK(m.stage() == DiveStage::Shatter);
    CHECK_EQ(m.shatterTimer(), SHATTER_LEN);
    CHECK(m.given() == ActType::Staff);
    CHECK(w.actors.type[staff] == ActType::None);
    CHECK(m.taken() == ActType::Shield);    // and the first choice is kept
}

KH_TEST(interact_a_weapon_out_of_reach_is_not_asked_about) {
    Stub w;
    DiveMachine m;
    m.begin();
    m.setStage(DiveStage::Pick);
    w.actors.spawn(ActType::Sword, tileCentre(16), tileCentre(4));
    w.putPlayer(11, 12);
    w.press(Button::A);
    SceneView v = w.view();
    CHECK(diveInteract(m, v, 0).action == SceneAction::None);
    // ...and neither is one you are standing on before the pedestals are live.
    m.setStage(DiveStage::Intro);
    w.actors.spawn(ActType::Sword, tileCentre(11), tileCentre(12));
    CHECK(diveInteract(m, v, 0).action == SceneAction::None);
}

// ===========================================================================
// Destiny Islands
// ===========================================================================

KH_TEST(interact_a_pickup_is_taken_by_walking_into_it_and_tallies_itself) {
    // island.s:280.  The type IS the tally slot, which is why the seven
    // walk-into pickups are consecutive and aligned with Item -- and it is
    // taken off the beach BEFORE the line opens, so nothing can take it twice.
    Stub w;
    IslandMachine m;
    m.begin();
    Inventory inv;
    const int log = w.actors.spawn(ActType::Log, tileCentre(11), tileCentre(12));
    SceneView v = w.view();

    const StageStep s = islandInteract(m, v, inv);
    CHECK(s.action == SceneAction::Say);
    CHECK(s.script == ScriptId::IslandGotLog);
    CHECK_EQ(inv.of(Item::Log), 1);
    CHECK(w.actors.type[log] == ActType::None);
    // The same frame again picks up nothing, because it is gone.
    CHECK(islandInteract(m, v, inv).action == SceneAction::None);
    CHECK_EQ(inv.of(Item::Log), 1);
}

KH_TEST(interact_reaching_up_onto_the_treehouse_does_not_count) {
    // PlayerZ: the deck has to match as well as the distance.  Without it a
    // player standing on the grass collects what is on the platform above him,
    // which is the one thing about this scan that is not a box test.
    Stub w;
    IslandMachine m;
    m.begin();
    Inventory inv;
    const int rope = w.actors.spawn(ActType::Rope, tileCentre(11),
                                    tileCentre(12));
    w.actors.z[rope] = 2;                   // up on the platform
    w.actors.z[w.player] = 0;               // ...and he is on the grass
    SceneView v = w.view();
    CHECK(islandInteract(m, v, inv).action == SceneAction::None);
    CHECK_EQ(inv.of(Item::Rope), 0);

    w.actors.z[w.player] = 2;               // climb up
    CHECK(islandInteract(m, v, inv).action == SceneAction::Say);
    CHECK_EQ(inv.of(Item::Rope), 1);
}

KH_TEST(interact_the_keyblade_catches_a_fish_and_shakes_a_palm_once) {
    // SwingAt, island.s:358.  Two things answer to it and they answer
    // differently: a fish is taken and a coconut palm becomes an ordinary one,
    // still standing, with nothing left to give.
    Stub w;
    IslandMachine m;
    m.begin();
    Inventory inv;
    const int fish = w.actors.spawn(ActType::Fish, tileCentre(12), tileCentre(12));
    SceneView v = w.view();

    w.press(Button::B);
    StageStep s = islandInteract(m, v, inv);
    CHECK(s.script == ScriptId::IslandGotFish);
    CHECK_EQ(inv.of(Item::Fish), 1);
    CHECK(w.actors.type[fish] == ActType::None);

    const int palm = w.actors.spawn(ActType::PalmC, tileCentre(12), tileCentre(12));
    w.release();
    w.press(Button::B);
    s = islandInteract(m, v, inv);
    CHECK(s.script == ScriptId::IslandGotNut);
    CHECK_EQ(inv.of(Item::Nut), 1);
    CHECK(w.actors.type[palm] == ActType::Palm);        // still standing
    // ...and it gives nothing the second time, because it is a plain palm now.
    w.release();
    w.press(Button::B);
    CHECK(islandInteract(m, v, inv).action == SceneAction::None);
    CHECK_EQ(inv.of(Item::Nut), 1);
}

KH_TEST(interact_the_two_days_ask_for_different_lists) {
    // HaveAll, island.s:695.  Day one is timber and day two is provisions, and
    // the two lists do not overlap -- a port that checked all eight on both
    // days would make day one unfinishable.
    Inventory inv;
    CHECK(!inv.haveAll(1));
    for (int i = 0; i < NEED[int(Item::Log)]; ++i) inv.add(Item::Log);
    inv.add(Item::Cloth);
    CHECK(!inv.haveAll(1));
    inv.add(Item::Rope);
    CHECK(inv.haveAll(1));
    CHECK(!inv.haveAll(2));                 // ...and none of it counts tomorrow

    Inventory two;
    for (int i = 0; i < NEED[int(Item::Fish)]; ++i) two.add(Item::Fish);
    for (int i = 0; i < NEED[int(Item::Mush)]; ++i) two.add(Item::Mush);
    for (int i = 0; i < NEED[int(Item::Nut)]; ++i) two.add(Item::Nut);
    two.add(Item::Egg);
    CHECK(!two.haveAll(2));
    two.add(Item::Water);
    CHECK(two.haveAll(2));
    CHECK(!two.haveAll(1));
}

KH_TEST(interact_talking_to_kairi_walks_the_quest_and_picks_its_own_line) {
    // TalkKairi, island.s:619.  The line is chosen from the state BEFORE the
    // machine changes it, which is the trap: talkToKairi is what turns Idle
    // into Active, so a caller that read the state afterwards would hand over
    // the list and then say "still something missing" about it.
    Stub w;
    IslandMachine m;
    m.begin();
    Inventory inv;
    w.actors.spawn(ActType::Kairi, tileCentre(12), tileCentre(12));

    w.press(Button::A);
    SceneView v = w.view();
    StageStep s = islandInteract(m, v, inv);
    CHECK(m.state() == QuestState::Active);             // the list is handed over
    CHECK(s.action == SceneAction::HudChanged);

    // Asked again with nothing in hand: the reminder.
    w.release(); w.press(Button::A);
    s = islandInteract(m, v, inv);
    CHECK(s.action == SceneAction::Say);
    CHECK(s.script == ScriptId::IslandKairiRemind);
    CHECK(m.state() == QuestState::Active);

    // ...and with the whole list, the other line, and the quest moves on.
    for (int i = 0; i < NEED[int(Item::Log)]; ++i) inv.add(Item::Log);
    inv.add(Item::Cloth);
    inv.add(Item::Rope);
    w.release(); w.press(Button::A);
    s = islandInteract(m, v, inv);
    CHECK(s.action == SceneAction::Say);
    CHECK(s.script == ScriptId::IslandKairiFinish);
    CHECK(m.state() == QuestState::Done);
}

KH_TEST(interact_the_islanders_have_a_different_line_on_the_second_day) {
    Stub w;
    IslandMachine m;
    m.begin();
    Inventory inv;
    w.actors.spawn(ActType::Wakka, tileCentre(12), tileCentre(12));
    w.press(Button::A);
    SceneView v = w.view();
    CHECK(islandInteract(m, v, inv).script == ScriptId::IslandWakka);

    m.setDay(2);
    w.release(); w.press(Button::A);
    CHECK(islandInteract(m, v, inv).script == ScriptId::IslandWakka2);
}

KH_TEST(interact_the_wall_takes_the_nearest_of_three_and_the_door_changes) {
    // FindProp is the one search that does NOT take the first hit: three
    // drawings hang within arm's reach of each other, so it takes whichever is
    // closest by |dx| + |dy|.  And the door is the one that has two lines --
    // before the raft has a name it is an oddity, and after it he wonders.
    Stub w;
    IslandMachine m;
    m.begin();
    Inventory inv;
    w.actors.spawn(ActType::Faces, tileCentre(1), tileCentre(6));
    w.actors.spawn(ActType::Door, tileCentre(2), tileCentre(6));
    w.actors.spawn(ActType::Scribble, tileCentre(3), tileCentre(6));

    w.putPlayer(1, 6);                      // right on top of the faces
    w.press(Button::A);
    SceneView v = w.view();
    CHECK(islandInteract(m, v, inv).script == ScriptId::IslandFaces);

    w.putPlayer(3, 6);
    w.release(); w.press(Button::A);
    CHECK(islandInteract(m, v, inv).script == ScriptId::IslandScribble);

    w.putPlayer(2, 6);
    w.release(); w.press(Button::A);
    CHECK(islandInteract(m, v, inv).script == ScriptId::IslandDoor);

    // Once the raft has a name he has started to wonder about it.
    m.setState(QuestState::Named);
    w.release(); w.press(Button::A);
    CHECK(islandInteract(m, v, inv).script == ScriptId::IslandDoor2);
}

KH_TEST(interact_the_island_is_taken_away_by_the_race_and_given_back) {
    // "The gameplay gate is a RANGE and not a flag: pickups, the Keyblade and
    // conversation are live for state < RaceSet OR state == Named exactly."
    Stub w;
    IslandMachine m;
    m.begin();
    Inventory inv;
    w.actors.spawn(ActType::Log, tileCentre(11), tileCentre(12));

    m.setState(QuestState::RaceSet);
    SceneView v = w.view();
    CHECK(islandInteract(m, v, inv).action == SceneAction::None);
    CHECK_EQ(inv.of(Item::Log), 0);         // the countdown owns the scene

    m.setState(QuestState::Named);
    CHECK(islandInteract(m, v, inv).action == SceneAction::Say);
    CHECK_EQ(inv.of(Item::Log), 1);         // ...and it is his again
}

KH_TEST(interact_the_race_is_two_legs_in_order_and_riku_wins_a_tie) {
    // RaceRun tests where Sora IS STANDING rather than where he went, and the
    // paopu tree has to be tagged before home counts for anything.  Audit
    // finding 8: Riku's waypoint count is tested first, so he takes a tie.
    Stub w;
    IslandMachine m;
    m.begin();
    m.setState(QuestState::RaceRun);

    w.putPlayer(12, 12);                    // standing on the finish already
    SceneView v = w.view();
    islandRaceCheck(m, v);
    CHECK_EQ(m.raceWon(), 0);               // going home first wins nothing
    CHECK_EQ(m.raceLeg(), 0);

    w.putPlayer(27, 7);                     // the paopu tree
    islandRaceCheck(m, v);
    CHECK_EQ(m.raceLeg(), 1);
    CHECK_EQ(m.raceWon(), 0);

    w.putPlayer(20, 10);                    // ...somewhere in between
    islandRaceCheck(m, v);
    CHECK_EQ(m.raceWon(), 0);

    w.putPlayer(12, 12);
    islandRaceCheck(m, v);
    CHECK_EQ(m.raceWon(), 1);
    CHECK(m.state() == QuestState::RaceOver);
}

// ===========================================================================
// The night
// ===========================================================================

KH_TEST(interact_the_night_only_answers_to_whoever_it_is_waiting_for) {
    // TalkTarget takes the TYPE the current beat wants, so there is one search
    // and not two: nothing else out there answers, and Kairi standing next to
    // you during the search for Riku is not a way past him.
    Stub w;
    NightMachine m;
    m.begin(w.rng);
    m.setStage(NightStage::Seek);
    w.actors.spawn(ActType::Kairi, tileCentre(12), tileCentre(12));

    w.press(Button::A);
    SceneView v = w.view();
    CHECK(nightInteract(m, v).action == SceneAction::None);
    CHECK(m.stage() == NightStage::Seek);

    const int riku = w.actors.spawn(ActType::Riku, tileCentre(12), tileCentre(12));
    w.release(); w.press(Button::A);
    StageStep s = nightInteract(m, v);
    CHECK(s.action == SceneAction::Say);
    CHECK(s.script == ScriptId::NightRiku);
    CHECK(m.stage() == NightStage::Riku);
    CHECK_EQ(m.nightTimer(), DARK_HOLD);

    // And the second search answers to her and not to him.
    m.setStage(NightStage::Kairi);
    w.actors.type[riku] = ActType::None;
    w.release(); w.press(Button::A);
    s = nightInteract(m, v);
    CHECK(s.action == SceneAction::OpenTheDoor);
    CHECK(s.script == ScriptId::NightKairi);
    CHECK(m.stage() == NightStage::Door);
}

// ===========================================================================
// Traverse Town
// ===========================================================================

namespace {

const TownDoor TOWN1_DOORS[] = {
    {{25, DOOR_ROW}, SceneId::Town2, {26, 5}, TownStage::Second},
};

}  // namespace

KH_TEST(interact_a_door_fires_on_arrival_and_is_gated_on_progress) {
    // townStage is PROGRESS, NOT LOCATION: a door carries the stage the town
    // must have reached, so the world is gated with no lock flags anywhere.
    // And only the frame he ARRIVES on counts -- a bolted door has a line, and
    // it would be said on every frame he stood in front of it otherwise.
    Stub w;
    Interact st;
    TownMachine m;
    m.begin(w.rng);
    m.setStage(TownStage::Look);            // ...before the way is open
    m.setDistrict(SceneId::Town1);
    w.putPlayer(25, DOOR_ROW);

    SceneView v = w.view();
    StageStep s = townInteract(m, st, v, TOWN1_DOORS, 1);
    CHECK(s.action == SceneAction::Say);
    CHECK(s.script == ScriptId::TownShut1);
    CHECK_EQ(m.doorTimer(), 0);

    // Standing there says it once and then nothing, however many frames pass.
    for (int i = 0; i < 30; ++i) {
        CHECK(townInteract(m, st, v, TOWN1_DOORS, 1).action == SceneAction::None);
    }

    // Step off and back on, with the town further along, and it opens.
    w.putPlayer(25, DOOR_ROW + 1);
    townInteract(m, st, v, TOWN1_DOORS, 1);
    m.setStage(TownStage::Second);
    w.putPlayer(25, DOOR_ROW);
    s = townInteract(m, st, v, TOWN1_DOORS, 1);
    CHECK(s.action == SceneAction::None);
    CHECK_EQ(m.doorTimer(), DOOR_FADE * 2);
    // ...and a stage PAST the one it wants still opens it: progress, not a key.
    m.setStage(TownStage::Boss);
    w.putPlayer(25, DOOR_ROW + 1);
    townInteract(m, st, v, TOWN1_DOORS, 1);
    m.openDoor(0);                          // clear the fade the last one armed
    w.putPlayer(25, DOOR_ROW);
    CHECK(townInteract(m, st, v, TOWN1_DOORS, 1).action == SceneAction::None);
    CHECK_EQ(m.doorTimer(), DOOR_FADE * 2);
}

KH_TEST(interact_a_resident_is_talked_to_and_a_conversation_holds_the_door) {
    Stub w;
    Interact st;
    TownMachine m;
    m.begin(w.rng);
    m.setStage(TownStage::Second);
    w.putPlayer(25, DOOR_ROW);
    w.actors.spawn(ActType::Cid, tileCentre(25), tileCentre(DOOR_ROW));

    w.press(Button::A);
    SceneView v = w.view();
    const StageStep s = townInteract(m, st, v, TOWN1_DOORS, 1);
    // Talking wins the frame, and the door under his feet stays shut: a
    // district change under an open box is the thing this ordering prevents.
    CHECK(s.action == SceneAction::Say);
    CHECK(s.script == ScriptId::TownCid);
    CHECK_EQ(m.doorTimer(), 0);
}

// ===========================================================================
// Being out of HP
// ===========================================================================

KH_TEST(interact_the_game_over_card_goes_up_once_and_then_the_scene_restarts) {
    // GameOverUpdate, dive.s:324, which runs INSTEAD of the scene's own update
    // for every scene -- "being out of HP takes priority over whatever the
    // scene was doing".  Two passes and not one: the card first, the retry
    // when it has been dismissed.
    Stub w;
    WorldState state;
    state.deadFlag = 1;                     // UpdateSora hands it over as a 1

    SceneView v = w.view();
    StageStep s = gameOverStep(state, v);
    CHECK(s.action == SceneAction::Say);
    CHECK(s.script == ScriptId::DiveGameOver);
    CHECK_EQ(int(state.deadFlag), 2);

    // While the card is up, nothing.
    const uint8_t ONE_LINE[] = {'H', 'I', '.', SC_END};
    w.dialogue.open(Script{ONE_LINE, sizeof ONE_LINE}, TextMode::Message);
    CHECK(gameOverStep(state, v).action == SceneAction::None);
    CHECK_EQ(int(state.deadFlag), 2);

    // Dismissed: the retry.
    for (int i = 0; i < 16 && w.dialogue.state() == TextState::Reveal; ++i)
        w.dialogue.update(w.pad);
    w.press(Button::A);
    w.dialogue.update(w.pad);
    w.release();
    CHECK(!w.dialogue.busy());
    CHECK(gameOverStep(state, v).action == SceneAction::RestartScene);
}

KH_TEST(interact_the_race_starts_on_the_start_line_and_not_where_they_were_sat) {
    // PlaceRacers, island.s:767 -- the other routine the race needed that
    // nothing called.  OfferRace runs it between arming the countdown and
    // saying the challenge line, so a port that only had beginRace() started
    // the race with Riku wherever he happened to be sitting.  §M3b's race
    // fixture noticed exactly that and worked around it: "he is running from
    // where he SITS -- tile (27,8) on the small island -- rather than from the
    // start line".
    Stub w;
    const int riku = w.actors.spawn(ActType::Riku, tileCentre(27), tileCentre(8));
    w.putPlayer(4, 4);
    w.actors.vx[w.player] = World::fromRaw(24);      // caught mid-stride
    w.actors.vy[riku] = World::fromRaw(-17);
    SceneView v = w.view();

    placeRacers(v);
    CHECK_EQ(w.actors.x[w.player].raw(), START_SORA_X.raw());
    CHECK_EQ(w.actors.y[w.player].raw(), START_SORA_Y.raw());
    CHECK_EQ(w.actors.x[riku].raw(), START_RIKU_X.raw());
    CHECK_EQ(w.actors.y[riku].raw(), START_RIKU_Y.raw());
    // PutActor clears the velocity too, which is the half people forget: a
    // racer still carrying a step slides off the line on the first frame.
    CHECK_EQ(w.actors.vx[w.player].raw(), 0);
    CHECK_EQ(w.actors.vy[w.player].raw(), 0);
    CHECK_EQ(w.actors.vx[riku].raw(), 0);
    CHECK_EQ(w.actors.vy[riku].raw(), 0);

    // The start line is one tile west of the finish, which is Kairi's spot --
    // that is what makes Sora's second leg read as "back where you started".
    CHECK_EQ(FINISH_X.raw() - START_SORA_X.raw(), 256);
    CHECK_EQ(FINISH_Y.raw(), START_SORA_Y.raw());
    // ...and the two of them start one tile apart, not on top of each other.
    CHECK_EQ(START_RIKU_X.raw() - START_SORA_X.raw(), 512);

    // A scene with no Riku in it must not fall over, and must still place Sora.
    Stub alone;
    alone.putPlayer(4, 4);
    SceneView v2 = alone.view();
    placeRacers(v2);
    CHECK_EQ(alone.actors.x[alone.player].raw(), START_SORA_X.raw());
}
