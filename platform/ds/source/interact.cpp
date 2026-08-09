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
        if (absW(a.x[p] - tileCentre(27)).raw() < TAG_X.raw()
            && absW(a.y[p] - tileCentre(7)).raw() < TAG_Y.raw())
            m.tagPaopu();
        return;
    }
    if (absW(a.x[p] - tileCentre(12)).raw() < TAG_X.raw()
        && absW(a.y[p] - tileCentre(12)).raw() < TAG_Y.raw())
        m.reachHome();
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
        if (m.stage() >= doors[d].needs) {
            m.openDoor(int(doors[d].to));
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
