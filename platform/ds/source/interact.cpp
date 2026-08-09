#include "interact.h"

#include "grid.h"

namespace kh {
namespace {

World absW(World v) { return World::fromRaw(v.raw() < 0 ? -v.raw() : v.raw()); }

bool inRange(const Actors& a, int slot, World px, World py, World rx, World ry) {
    return absW(a.x[slot] - px).raw() < rx.raw()
        && absW(a.y[slot] - py).raw() < ry.raw();
}

// A tile coordinate out of a Q12.4 position.  Four shifts to pixels and four
// more to tiles, so it is the top byte of the fixed-point one -- which is what
// the assembly does with `xba`.
int tileOfPos(World v) { return int(v.raw() >> WORLD_TO_TILE_SHIFT); }

}  // namespace

bool Inventory::haveAll(int day) const {
    if (day == 2) {
        return of(Item::Fish) >= NEED[int(Item::Fish)]
            && of(Item::Mush) >= NEED[int(Item::Mush)]
            && of(Item::Nut) >= NEED[int(Item::Nut)]
            && of(Item::Egg) >= NEED[int(Item::Egg)]
            && of(Item::Water) >= NEED[int(Item::Water)];
    }
    return of(Item::Log) >= NEED[int(Item::Log)]
        && of(Item::Cloth) >= NEED[int(Item::Cloth)]
        && of(Item::Rope) >= NEED[int(Item::Rope)];
}

int nearestOfRange(const Actors& a, int player, ActType lo, ActType hi,
                   World rx, World ry) {
    if (player < 0 || player >= MAX_ACTORS) return -1;
    const World px = a.x[player], py = a.y[player];
    // FIRST HIT IN SLOT ORDER, not nearest.  The scan runs 0..MAX_ACTORS and
    // returns on the first match, so which of two overlapping islanders answers
    // is decided by who was spawned first.  Reproduced rather than improved:
    // the cast tables put nobody within talking distance of anybody else, so
    // "improving" it would change nothing except the one case where it did.
    for (int i = 0; i < MAX_ACTORS; ++i) {
        if (a.type[i] < lo || a.type[i] > hi) continue;
        if (inRange(a, i, px, py, rx, ry)) return i;
    }
    return -1;
}

int nearestProp(const Actors& a, int player) {
    if (player < 0 || player >= MAX_ACTORS) return -1;
    const World px = a.x[player], py = a.y[player];
    int best = -1;
    int32_t bestD = 0x7FFF;         // the assembly's starting value, exactly
    for (int i = 0; i < MAX_ACTORS; ++i) {
        if (!isWallProp(a.type[i])) continue;
        if (!inRange(a, i, px, py, PROP_X, PROP_Y)) continue;
        const int32_t d = absW(a.x[i] - px).raw() + absW(a.y[i] - py).raw();
        // `bcs @keep`, so an equal distance KEEPS the earlier slot.
        if (d < bestD) { bestD = d; best = i; }
    }
    return best;
}

// ---------------------------------------------------------------------------
// The Dive: the dream weapons
// ---------------------------------------------------------------------------
StageStep diveInteract(DiveMachine& m, SceneView& view, int answer) {
    Actors& a = view.actors;

    // A prompt that just closed leaves its answer behind, and HandleAnswer runs
    // BEFORE the stage dispatch -- dive.s:83-88.  So the frame an answer
    // arrives does nothing else.
    if (answer != 0) {
        if (answer != 1) return StageStep{};        // 2 is no; nothing happens
        const int slot = m.pendingActor();
        const ActType weapon = m.pendingWeapon();
        if (slot < 0 || slot >= MAX_ACTORS) return StageStep{};
        if (m.stage() == DiveStage::Drop) {
            // Giving one up: the second choice, and the floor goes with it.
            m.choose(m.taken(), weapon);
            a.type[slot] = ActType::None;
            m.setStage(DiveStage::Shatter);
            m.armShatter();
            return StageStep{SceneAction::Say, ScriptId::DiveChosen};
        }
        // Taking a power: the weapon leaves its pedestal and he is asked what
        // he will give up.
        m.choose(weapon, m.given());
        a.type[slot] = ActType::None;
        m.setStage(DiveStage::Drop);
        return StageStep{SceneAction::Say, ScriptId::DiveGiveUp};
    }

    if (m.stage() != DiveStage::Pick && m.stage() != DiveStage::Drop)
        return StageStep{};
    if (!view.pad.wasPressed(Button::A)) return StageStep{};

    const int slot = nearestOfRange(a, view.player, ActType::Sword,
                                    ActType::Staff, REACH_X, REACH_Y);
    if (slot < 0) return StageStep{};
    m.setPending(slot, a.type[slot]);
    // AskAbout: which description depends on which half of the choice this is,
    // and it is a PROMPT and not a message -- the answer is what moves the
    // scene, so the box has to come back with one.
    const int w = int(a.type[slot]) - int(ActType::Sword);
    const ScriptId line = m.stage() == DiveStage::Drop ? DESCDROP[w]
                                                       : DESCTAKE[w];
    return StageStep{SceneAction::Ask, line};
}

// ---------------------------------------------------------------------------
// Destiny Islands
// ---------------------------------------------------------------------------
namespace {

// Collect: tally one item and say so.  The line is indexed by the tally slot,
// which is why the pickup types are consecutive and aligned with Item.
StageStep collect(Inventory& inv, Item it) {
    inv.add(it);
    return StageStep{SceneAction::Say, GOTLINES[int(it)], uint8_t(it)};
}

// CheckPickups: walk into a collectable and it is yours.
StageStep checkPickups(SceneView& view, Inventory& inv) {
    Actors& a = view.actors;
    const int p = view.player;
    // PlayerZ.  Reaching up onto the treehouse from the grass below it should
    // not count, so the deck has to match as well as the distance -- which is
    // the one thing about this scan that is not just a box test.
    const uint8_t deck = a.z[p];
    for (int i = 0; i < MAX_ACTORS; ++i) {
        if (!isPickup(a.type[i])) continue;
        if (a.z[i] != deck) continue;
        if (!inRange(a, i, a.x[p], a.y[p], PICK_X, PICK_Y)) continue;
        const Item it = itemOf(a.type[i]);
        // Take it off the beach FIRST, so nothing below can pick it up twice.
        a.type[i] = ActType::None;
        return collect(inv, it);
    }
    return StageStep{};
}

// SwingAt: what the Keyblade is good for when nothing is attacking.
StageStep swingAt(SceneView& view, Inventory& inv) {
    Actors& a = view.actors;
    const int p = view.player;
    for (int i = 0; i < MAX_ACTORS; ++i) {
        if (a.type[i] != ActType::Fish && a.type[i] != ActType::PalmC) continue;
        if (!inRange(a, i, a.x[p], a.y[p], SWING_X, SWING_Y)) continue;
        if (a.type[i] == ActType::Fish) {
            a.type[i] = ActType::None;              // caught
            return collect(inv, Item::Fish);
        }
        // The palm keeps standing; it just has nothing left to give.
        a.type[i] = ActType::Palm;
        return collect(inv, Item::Nut);
    }
    return StageStep{};
}

// LookAt: what Sora makes of the thing on the wall.  The door is the one that
// changes -- before the raft has a name it is an oddity, and after it he has
// started to wonder about it.
StageStep lookAt(const IslandMachine& m, ActType prop) {
    int line = 3;                                   // the scribble
    if (prop == ActType::Door)
        line = m.state() == QuestState::Named ? 1 : 0;
    else if (prop == ActType::Faces)
        line = 2;
    return StageStep{SceneAction::Say, PROPLINES[line]};
}

// TalkTo: whatever the islander has to say, which is a different line on the
// second day.  Kairi is not in this table -- she is the quest.
StageStep talkTo(IslandMachine& m, ActType who, const Inventory& inv) {
    if (who == ActType::Kairi) {
        // The state BEFORE the machine changes it is what picks the line, so
        // it has to be read first: talkToKairi is what moves Idle to Active.
        const QuestState was = m.state();
        const bool all = inv.haveAll(m.day());
        StageStep s = m.talkToKairi(all);
        if (s.action != SceneAction::Say) return s;   // a race offer, or the HUD
        int line = 0;                                 // the rest-up line
        if (was == QuestState::Idle) line = 1;        // the list
        else if (was == QuestState::Active) line = all ? 2 : 3;
        s.script = m.day() == 2 ? KAIRI2LINES[line] : KAIRILINES[line];
        return s;
    }
    const int who1 = int(who) - int(ActType::Riku);
    return StageStep{SceneAction::Say,
                     m.day() == 2 ? ISLANDER2LINES[who1] : ISLANDERLINES[who1]};
}

}  // namespace

StageStep islandInteract(IslandMachine& m, SceneView& view, Inventory& inv) {
    if (view.dialogue.busy()) return StageStep{};
    // "Once the raft has a name there is nothing left to gather, and during the
    // race Sora has better things to do than talk."  The gate is a RANGE and
    // not a flag, and Named is inside it: the race takes the scene away and
    // gives it back once the raft is named.
    if (!m.gameplayLive()) return StageStep{};

    const StageStep got = checkPickups(view, inv);
    if (got.action != SceneAction::None) return got;

    if (view.pad.wasPressed(Button::B)) {
        const StageStep hit = swingAt(view, inv);
        if (hit.action != SceneAction::None) return hit;
        return StageStep{};     // the swing itself is UpdateSora's business
    }
    if (!view.pad.wasPressed(Button::A)) return StageStep{};

    // A person first, then the wall.  Standing between Kairi and a drawing
    // talks to Kairi, which is the order the assembly picks and the one that
    // matters: the drawings are in the Secret Place and she is outside it.
    const int who = nearestOfRange(view.actors, view.player, ActType::Kairi,
                                   ActType::Wakka, TALK_X, TALK_Y);
    if (who >= 0) return talkTo(m, view.actors.type[who], inv);

    const int prop = nearestProp(view.actors, view.player);
    if (prop < 0) return StageStep{};
    return lookAt(m, view.actors.type[prop]);
}

void islandRaceCheck(IslandMachine& m, SceneView& view) {
    if (m.state() != QuestState::RaceRun) return;
    const Actors& a = view.actors;
    const int p = view.player;
    // The paopu tree first, then home, and only in that order: tagPaopu is a
    // no-op once the leg has turned, and reachHome refuses until it has.
    if (m.raceLeg() == 0) {
        if (absW(a.x[p] - PAOPU_X).raw() < TAG_X.raw()
            && absW(a.y[p] - PAOPU_Y).raw() < TAG_Y.raw())
            m.tagPaopu();
        return;
    }
    if (absW(a.x[p] - FINISH_X).raw() < TAG_X.raw()
        && absW(a.y[p] - FINISH_Y).raw() < TAG_Y.raw())
        m.reachHome();
}

// PlaceRacers, island.s:767.  Both of them onto the start line by Kairi, and
// it is the OTHER routine the race needed that nothing called: OfferRace runs
// it between arming the countdown and saying the challenge line, so a port that
// only ported beginRace() starts the race with Riku wherever he happened to be
// sitting.  §M3b's race fixture noticed exactly that and worked around it --
// "he is running from where he SITS rather than from the start line".
//
// PutActor is inlined here because it is four assignments and a SetActorZ, and
// the only thing interesting about it is which of them people forget: it clears
// the VELOCITY too, so a racer caught mid-stride does not slide off the line.
void placeRacers(SceneView& view) {
    Actors& a = view.actors;
    const int p = view.player;
    if (p >= 0 && p < MAX_ACTORS) {
        a.x[p] = START_SORA_X;
        a.y[p] = START_SORA_Y;
        a.vx[p] = World::fromRaw(0);
        a.vy[p] = World::fromRaw(0);
        setActorZ(a, p, view.ground);
    }
    // FIRST Riku in slot order, and there is only ever one -- the scan is the
    // assembly's and it stops at the first, so a second would never be moved.
    for (int i = 0; i < MAX_ACTORS; ++i) {
        if (a.type[i] != ActType::Riku) continue;
        a.x[i] = START_RIKU_X;
        a.y[i] = START_RIKU_Y;
        a.vx[i] = World::fromRaw(0);
        a.vy[i] = World::fromRaw(0);
        setActorZ(a, i, view.ground);
        return;
    }
}

// ---------------------------------------------------------------------------
// The night
// ---------------------------------------------------------------------------
StageStep nightInteract(NightMachine& m, SceneView& view) {
    if (view.dialogue.busy()) return StageStep{};
    if (!view.pad.wasPressed(Button::A)) return StageStep{};

    // TalkTarget takes the TYPE the current beat is waiting for, which is why
    // there is one search and not two: nothing else on the island answers.
    if (m.stage() == NightStage::Seek) {
        if (nearestOfRange(view.actors, view.player, ActType::Riku,
                           ActType::Riku, TALK_X, TALK_Y) < 0)
            return StageStep{};
        StageStep s = m.talkToRiku();
        s.script = ScriptId::NightRiku;
        return s;
    }
    if (m.stage() == NightStage::Kairi) {
        if (nearestOfRange(view.actors, view.player, ActType::Kairi,
                           ActType::Kairi, TALK_X, TALK_Y) < 0)
            return StageStep{};
        return m.talkToKairi();     // it carries its own line
    }
    return StageStep{};
}

// ---------------------------------------------------------------------------
// Traverse Town
// ---------------------------------------------------------------------------
StageStep townInteract(TownMachine& m, Interact& st, SceneView& view,
                       const TownDoor* doors, int nDoors) {
    if (view.dialogue.busy()) return StageStep{};
    Actors& a = view.actors;

    // TalkTown first, because a conversation that just opened holds the door
    // shut for the frame: TownUpdate re-tests TextBusy between the two, and a
    // door taken mid-sentence would change district under an open box.
    if (view.pad.wasPressed(Button::A)) {
        const int who = nearestOfRange(a, view.player, ActType::Cid,
                                       ActType::TownWoman, TALK_X, TALK_Y);
        if (who >= 0) {
            // CID IS THE REASON THE DOOR OPENS, and this branch is what makes
            // the whole door table live.  town.s:533-551: talking to him at
            // T_LOOK writes T_SECOND, calls HudUpdate and says scriptCid;
            // afterwards it says scriptCid2 and changes nothing.
            //
            // Without it the wiring is dead on arrival.  Nothing else in the
            // non-test tree sets TownStage::Second -- Woke (town.s:251) reaches
            // Look and stops -- so the First District's exit is gated on a
            // stage that never arrives, and the Second and Third Districts are
            // unreachable in a town whose own rule is that every district stays
            // reachable.  The bolted-door line would be the last thing the game
            // ever said.
            //
            // THE HUD ACTION WITH THE LINE RIDING ALONG.  The SNES does both
            // and in this order -- `jsr HudUpdate` at town.s:542 then `jmp Say`
            // at town.s:546 -- and a StageStep carries one action, so the
            // redraw is the action and the script is the passenger.  The
            // precedent for a passenger is NightMachine::talkToKairi
            // (stage_night.cpp:146-153), which returns OpenTheDoor carrying
            // ScriptId::NightKairi; the consumer's half of the bargain is
            // trace_main.cpp:455-456, the HudChanged arm of perform(), which
            // opens `step.script` when it is set exactly as the SweepGauntlets
            // arm at trace_main.cpp:478-479 does.
            //
            // IT IS NOT THE ISLAND'S SHAPE, WHICH IS THE OPPOSITE ONE.
            // IslandMachine::talkToKairi's Idle arm (stage_island.cpp:154-158)
            // returns a BARE HudChanged, and talkTo bails out above the line
            // lookup for exactly that case -- `if (s.action !=
            // SceneAction::Say) return s;`, interact.cpp:184 -- so on the
            // island "advance the state and redraw the objective" says nothing
            // at all.  Cid cannot work that way: his line is the one that tells
            // the player the door is open.
            //
            // THE PASSENGER IS NOW HONOURED, AND CHECKED AT BOTH ENDS.  It was
            // not, and this comment used to record that as a standing hazard:
            // HudChanged was the one action perform() deliberately dropped, and
            // it dropped the script with it, so the day a scene layer performed
            // this step Cid's line would be lost AND the box would never open.
            // The box is the worse half -- an unopened box leaves
            // dialogue.busy() false, and every machine here gates its update on
            // that flag, so the player would act on the frames the SNES spent
            // reading and every input after that would land a conversation
            // early.  It was latent only because nothing outside the tests
            // calls townInteract yet, which is the kind of luck that expires.
            //
            // Both halves of it are now enforced:
            //
            //   * THE CONSUMER.  perform()'s HudChanged arm opens `step.script`
            //     when it is set, exactly as the SweepGauntlets arm does, and
            //     auditPassengerSurvivesPerform() beside it runs on every
            //     dstrace invocation -- so all eight of tools/trace_check.py's
            //     scenarios refuse to emit a trace from a build that drops it.
            //     Reverting the arm makes every one of them fail by name.
            //
            //   * THE PRODUCER.  test_doors.cpp's
            //     doors_only_cids_hud_step_carries_a_line drives all seven
            //     HudChanged steps the tree can emit and pins six of them bare
            //     and this one carrying TownCid, so a passenger appearing on an
            //     action whose arm is implemented and silent gets caught while
            //     it is still being written.
            //
            // What remains for the scene layer is only what it always was:
            // townInteract has no non-test caller, so call it.  The step it
            // returns is now safe to perform.
            if (a.type[who] == ActType::Cid) {
                if (m.stage() == TownStage::Look) {
                    m.setStage(TownStage::Second);
                    return StageStep{SceneAction::HudChanged, ScriptId::TownCid};
                }
                // "THE DOOR AT THE END OF THE ROW. IT'S OPEN NOW."
                // (town.s:1479-1481) -- TownCid2's first and only reference in
                // the tree, and @cidAgain's line.
                //
                // THE EQUALITY IS DELIBERATE AND IT IS ALSO SAFE, which are two
                // different facts and an earlier version of this comment ran
                // them together.  `cmp #T_LOOK` is an equality and not a
                // "later than", so read on its own this arm looks reachable at
                // every other stage, Arrive included.  It is not: TalkTown has
                // exactly one caller (town.s:240) and that caller gates it,
                // sending T_ARRIVE to Woke, T_MEET to Meet, T_BOSS to
                // WatchArmor, T_WON to AfterArmor and T_OVER to an immediate
                // rts (town.s:217-232), and falling through only on the three
                // stages its own comment calls walkable -- "T_LOOK, T_SECOND,
                // T_THIRD: the town is walkable and the doors are live"
                // (town.s:234).  So this arm is reached at Second and Third
                // and nowhere else.
                //
                // WHICH IS WHY THE LINE IS NEVER A LIE.  The door it promises
                // is doorTable row 0, gated on T_SECOND (town.s:1391), and both
                // stages that can reach this arm have passed that gate.  Saying
                // it were Arrive reachable would have Cid announce an open door
                // before Sora has been told to look for him -- see
                // docs/BEHAVIOUR.md section 6, which specifies this and says
                // why the equality is safe rather than sloppy.
                return StageStep{SceneAction::Say, ScriptId::TownCid2};
            }
            const int i = int(a.type[who]) - int(ActType::Cid);
            const ScriptId LINES[3] = {ScriptId::TownCid, ScriptId::TownMan,
                                       ScriptId::TownWoman};
            return StageStep{SceneAction::Say, LINES[i]};
        }
    }

    // CheckDoors.  ONLY THE FRAME HE ARRIVES ON COUNTS -- a bolted door has a
    // line, and it would be said on every frame he stood in front of it.
    const int ti = tileOfPos(a.x[view.player]);
    const int tj = tileOfPos(a.y[view.player]);
    if (ti == st.lastI && tj == st.lastJ) return StageStep{};
    st.lastI = int16_t(ti);
    st.lastJ = int16_t(tj);

    for (int d = 0; d < nDoors; ++d) {
        if (doors[d].at.i != ti || doors[d].at.j != tj) continue;
        // A DOOR WITH NO FAR SIDE.  The DS's First and Second Districts each
        // carry a painted shop front -- the Accessory Shop and the Hotel -- that
        // the SNES never had and that leads nowhere: there is no fourth SceneId
        // (Count = 9 is the sentinel, constants.h:353), no interior map, no cast
        // and no .bin behind either of them.  gen/doors.h says so with
        // `to == SceneId::Count`.
        //
        // TESTED BEFORE THE GATE AND NOT AS A GATE.  The tempting shortcut is to
        // give a shop front an unreachable `needs` and let the existing
        // comparison keep it shut, but that is a rule pretending to be data: the
        // town's stages are progress and every one of them is reached in a
        // normal playthrough, so any value that worked would work by being out
        // of range rather than by meaning anything, and it would silently become
        // an opening door the day an eighth stage was added.  It also lands in
        // the @shut arm below and makes the shop front SAY one of the two bolted
        // lines -- both of which are written for a specific gate and are false
        // on a shop (scriptShut1, town.s:1494, ends "FIND THEM FIRST", stale
        // once Cid is found; scriptShut2, town.s:1501, ends "WHILE THE SQUARE
        // BEHIND YOU IS MOVING", which is not true in the First District at all).
        //
        // So it returns nothing at all, at every stage, forever.  An
        // unresponsive painted door is a smaller failure than a scripted lie,
        // and test_doors.cpp walks all eight TownStages over both of them.
        if (doors[d].to == SceneId::Count) return StageStep{};
        if (m.stage() >= doors[d].needs) {
            // The landing goes with it.  It is the FAR-side tile out of
            // gen/doors.h -- directly south of the door in the DESTINATION's map
            // (town.s:1386-1389) -- and not <scene>doors.bin's near-side one.
            m.openDoor(int(doors[d].to), doors[d].landing);
            return StageStep{};
        }
        // Shut, and which line depends on WHICH door: the one to the Second
        // District says something different from the one deeper in.
        return StageStep{SceneAction::Say,
                         doors[d].needs == TownStage::Second
                             ? ScriptId::TownShut1 : ScriptId::TownShut2};
    }
    return StageStep{};
}

// ---------------------------------------------------------------------------
// Being out of HP, which takes priority over whatever the scene was doing
// ---------------------------------------------------------------------------
StageStep gameOverStep(WorldState& w, SceneView& view) {
    if (view.dialogue.busy()) return StageStep{};       // the card is still up
    if (w.deadFlag == 1) {
        w.deadFlag = 2;                                 // the next pass retries
        return StageStep{SceneAction::Say, ScriptId::DiveGameOver};
    }
    return StageStep{SceneAction::RestartScene};
}

}  // namespace kh
