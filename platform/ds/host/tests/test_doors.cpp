// Traverse Town's door wiring: the half of doorTable that <scene>doors.bin
// deliberately does not carry.
//
// WHY THIS IS A NEW FILE AND NOT MORE OF test_interact.cpp.  test_interact.cpp
// drives townInteract against a FIXTURE -- one hand-written TownDoor at (25,4),
// the SNES's own tile -- and that is the right thing for it to do: it is testing
// the edge detector and the gate comparison, and a fixture keeps those tests
// legible and independent of what the districts happen to look like today.  The
// concern here is the opposite one.  It is about the REAL tables: that
// include/gen/doors.h and assets/gen/ds/<scene>doors.bin are the same table,
// that the landings land somewhere Sora can stand, and that the three districts
// join up into a ladder nobody can skip a rung of.  Those are properties of the
// shipped data, and a fixture cannot have them.
//
// So these read include/gen/doors.h, which tools/build_doors.py generates from
// assets/ds/town_doors.txt, and the emitted .bin planes beside it.  The two are
// produced by two different tools from two different files, which is exactly why
// something has to compare them: edit one and not the other and both tables are
// still the right length, so every index still resolves and the game quietly
// opens the wrong door.

#include "check.h"
#include "gen/assets.h"
#include "gen/doors.h"
#include "hostblob.h"

using namespace kh;

namespace {

unsigned char bufA[khhost::MAX_BLOB];
unsigned char bufB[khhost::MAX_BLOB];
unsigned char bufC[khhost::MAX_BLOB];

// The three districts, in SceneId order.  Everything below walks this rather
// than naming a district twice.
constexpr SceneId DISTRICTS[] = {SceneId::Town1, SceneId::Town2, SceneId::Town3};
constexpr int N_DISTRICTS = int(sizeof DISTRICTS / sizeof DISTRICTS[0]);

// SCENE_ASSETS is indexed by SceneId.  That is true today and nothing in the
// generated header says so, so every use of it below goes through here and the
// first test asserts it -- an assumption that is checked once is a fact, and one
// that is spread over four call sites is a bug waiting for somebody to add a
// scene in the middle.
const char* sceneName(SceneId s) {
    return SCENE_ASSETS[static_cast<int>(s)].name;
}

// "<scene>coll.bin" and friends, built without <string> because the device tier
// cannot have it and this file compiles with the same flags.
kh::Blob plane(SceneId s, const char* kind, unsigned char* buf, size_t cap) {
    char name[64];
    std::snprintf(name, sizeof name, "%s%s.bin", sceneName(s), kind);
    return khhost::load(name, buf, cap);
}

// One byte out of a 48x32 plane.  The width is a literal because the assertion
// that all three districts really are 48 wide is made once, below, rather than
// being re-derived at every read.
int at(kh::Blob b, int i, int j) {
    return int(b.data[size_t(j) * 48u + size_t(i)]);
}

// Every district is 48x32.  Asserted rather than assumed, once, here.
bool districtsAreFortyEightBy32() {
    for (SceneId s : DISTRICTS) {
        const SceneAsset& a = SCENE_ASSETS[static_cast<int>(s)];
        if (a.tilesW != 48 || a.tilesH != 32) return false;
    }
    return true;
}

// A SceneView holding references, as test_interact.cpp's does: made once and
// still current as the world under it changes.
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
        player = actors.spawn(ActType::Sora, tileCentre(20), tileCentre(20));
    }
    SceneView view() {
        return SceneView{actors, dialogue, pad, ground, rng, player, frame};
    }
    void putPlayer(int i, int j) {
        actors.x[player] = tileCentre(i);
        actors.y[player] = tileCentre(j);
    }
    void press(Button b) { pad.held = raw(b); pad.pressed = raw(b); }
    void release() { pad.held = 0; pad.pressed = 0; }
};

// Step off the tile and back on, so the next call counts as an ARRIVAL.  The
// edge detector is the whole reason a bolted door says its line once instead of
// on every frame (town.s:286-298), and a test that forgot to step off would be
// testing nothing after its first call.
StageStep stepOnto(TownMachine& m, Interact& st, Stub& w, SceneView& v,
                   const TownDoor* rows, int n, Tile t) {
    w.putPlayer(t.i, t.j + 1);
    townInteract(m, st, v, rows, n);
    w.putPlayer(t.i, t.j);
    return townInteract(m, st, v, rows, n);
}

// Run the transition to its midpoint and hand back the step that swaps the
// district.  Read through update() rather than through an accessor on purpose:
// SceneAction::EnterDistrict with the scene id in `arg` (stage_town.cpp:62) is
// how the caller actually learns where a door led, so this drives the path the
// game is on instead of a shortcut only a test can take.
StageStep runToSwap(TownMachine& m, SceneView& v, ScreenFx& fx) {
    for (int f = 0; f < DOOR_FADE * 2 + 2; ++f) {
        const StageStep s = m.update(v, fx);
        if (s.action == SceneAction::EnterDistrict) return s;
    }
    return StageStep{};
}

}  // namespace

// ===========================================================================
// The header and the binary
// ===========================================================================

KH_TEST(doors_the_header_and_the_binary_are_the_same_table) {
    // THE ONE CHECK THAT CATCHES A COMMITTED HEADER GONE STALE AGAINST A
    // GITIGNORED BINARY.  include/gen/doors.h is committed, because a clone must
    // be able to build the DS tier without Python; assets/gen/ds/*.bin is not,
    // because it is regenerated.  So the two halves of one table live under
    // different rules and are written by different tools, and after a clone the
    // header is the older of the two by definition.  Nothing else in the tree
    // would notice them disagreeing: both are walked by index, both are the
    // right length, and the symptom is a door that leads somewhere else.
    CHECK(districtsAreFortyEightBy32());

    for (SceneId s : DISTRICTS) {
        // ...and the indexing assumption everything else here leans on.
        CHECK(SCENE_ASSETS[static_cast<int>(s)].name != nullptr);

        char name[64];
        std::snprintf(name, sizeof name, "%sdoors.bin", sceneName(s));
        kh::Blob blob = khhost::load(name, bufA, sizeof bufA);
        if (blob.empty()) {
            std::printf("  MISSING %s -- run: python3 tools/build_assets.py\n",
                        name);
            CHECK(false);
            return;
        }
        Door bin[8];
        const int n = readDoors(blob, bin, 8);
        const TownDoors hdr = townDoorsFor(s);
        CHECK_EQ(hdr.count, n);
        if (hdr.count != n) continue;
        for (int d = 0; d < n; ++d) {
            // Same tiles, IN THE SAME ORDER.  Order is load-bearing and not a
            // convenience: the generator emits in <scene>doors.bin order so the
            // two can be zipped, and a reordering would pass a set comparison.
            CHECK_EQ(hdr.rows[d].at.i, bin[d].at.i);
            CHECK_EQ(hdr.rows[d].at.j, bin[d].at.j);
            // Every district door is in row 4 -- the only row of a building
            // block that shows a face (town.s:1386-1387).
            CHECK_EQ(hdr.rows[d].at.j, DOOR_ROW);
            // The binary's landing is the NEAR side and is a different tile
            // from the header's; asserting both shapes here is what keeps
            // anybody from "fixing" one to match the other.
            CHECK_EQ(bin[d].landing.i, bin[d].at.i);
            CHECK_EQ(bin[d].landing.j, DOOR_ROW + 1);
        }
    }

    // Six doors across the three districts, two of them shuttered.  A count is
    // the cheapest thing that notices a whole district's table going missing.
    int total = 0, shut = 0;
    for (SceneId s : DISTRICTS) {
        const TownDoors t = townDoorsFor(s);
        total += t.count;
        for (int d = 0; d < t.count; ++d)
            if (t.rows[d].to == SceneId::Count) ++shut;
    }
    CHECK_EQ(total, 6);
    CHECK_EQ(shut, 2);
    // A scene that is not a district has no doors, and gets zero rather than
    // somebody else's table.
    CHECK_EQ(townDoorsFor(SceneId::Island).count, 0);
    CHECK(townDoorsFor(SceneId::Island).rows == nullptr);
    CHECK_EQ(townDoorsFor(SceneId::Count).count, 0);
}

// ===========================================================================
// The landings
// ===========================================================================

KH_TEST(doors_a_landing_is_standable_where_it_lands) {
    // Checked against the EMITTED collision plane of the DESTINATION, which is
    // the only map the landing is a coordinate in.  Getting the scene wrong here
    // would be the easiest mistake in the file and the hardest to see: both maps
    // are 48x32 and most tiles are walkable in both, so a landing checked
    // against the wrong district usually passes.
    //
    // And it is not a cosmetic failure.  Nothing tests collision on a PLACEMENT
    // -- only on the next move -- so a door that puts Sora inside a wall shows
    // up somewhere else entirely, as a player who cannot walk out of a corner.
    for (SceneId from : DISTRICTS) {
        const TownDoors t = townDoorsFor(from);
        kh::Blob srcH = plane(from, "height", bufC, sizeof bufC);
        if (srcH.empty()) { CHECK(false); return; }
        for (int d = 0; d < t.count; ++d) {
            const TownDoor& row = t.rows[d];
            if (row.to == SceneId::Count) continue;      // no far side
            kh::Blob coll = plane(row.to, "coll", bufA, sizeof bufA);
            kh::Blob hgt = plane(row.to, "height", bufB, sizeof bufB);
            if (coll.empty() || hgt.empty()) { CHECK(false); return; }
            CHECK_EQ(coll.size, size_t(48 * 32));
            CHECK_EQ(hgt.size, size_t(48 * 32));

            SceneGround g;
            g.set(coll, hgt, 48, 32);
            CHECK(g.walkable(row.landing.i, row.landing.j));

            const int step = at(hgt, row.landing.i, row.landing.j)
                           - at(srcH, row.at.i, row.at.j);
            // A door is a teleport and the engine would not object -- which is
            // why it is checked here.  Walking through a doorway and arriving
            // two storeys up reads as the scene having loaded wrong.
            CHECK(step <= MAX_STEP && step >= -MAX_STEP);
        }
    }
}

KH_TEST(doors_a_landing_is_south_of_the_door_on_the_far_side) {
    // town.s:1386-1389, which is a RULE and not four coordinates: "every landing
    // is the tile directly south of the door on the far side, so a player who
    // walks straight through comes out facing the square."  So the landing is a
    // function of the reciprocal door, and the generator derives it rather than
    // letting anybody type it.  This is that rule, re-derived from the shipped
    // table and compared with what is in it.
    for (SceneId from : DISTRICTS) {
        const TownDoors t = townDoorsFor(from);
        for (int d = 0; d < t.count; ++d) {
            const TownDoor& row = t.rows[d];
            if (row.to == SceneId::Count) {
                // A shuttered door has no far side, so its landing is nowhere
                // -- deliberately NOT the near-side tile from the .bin, which
                // is the one conflation this whole table is shaped to prevent.
                CHECK_EQ(row.landing.i, 0);
                CHECK_EQ(row.landing.j, 0);
                continue;
            }
            const TownDoors back = townDoorsFor(row.to);
            int found = 0;
            Tile recip{};
            for (int b = 0; b < back.count; ++b) {
                if (back.rows[b].to != from) continue;
                ++found;
                recip = back.rows[b].at;
            }
            // EXACTLY ONE.  With none there is nowhere to put Sora down and the
            // district is one he cannot walk back out of; with two there is no
            // way to choose, and the pick would be silent.
            CHECK_EQ(found, 1);
            if (found != 1) continue;
            CHECK_EQ(row.landing.i, recip.i);
            CHECK_EQ(row.landing.j, DOOR_ROW + 1);
        }
    }
}

// ===========================================================================
// The ladder
// ===========================================================================

KH_TEST(doors_no_district_is_reached_before_its_stage) {
    // townStage is PROGRESS AND NOT LOCATION, which is only true if progress is
    // the only thing that lets you move.  Walked here as the player walks it: a
    // flood fill over the real tables, from the district the town is entered at
    // (SceneAction::EnterTown, stage.h) with every door whose gate the stage has
    // reached, at each of the eight stages in turn.
    //
    // A second, ungated route into the Second District would not fail any test
    // that looks at one door at a time -- every row would still be correct --
    // and it would make Cid optional.
    static const TownStage ALL[] = {
        TownStage::Arrive, TownStage::Look, TownStage::Second, TownStage::Third,
        TownStage::Meet, TownStage::Boss, TownStage::Won, TownStage::Over,
    };
    for (TownStage stage : ALL) {
        bool got[N_DISTRICTS] = {true, false, false};    // entered at Town1
        for (int pass = 0; pass < N_DISTRICTS; ++pass) {
            for (int a = 0; a < N_DISTRICTS; ++a) {
                if (!got[a]) continue;
                const TownDoors t = townDoorsFor(DISTRICTS[a]);
                for (int d = 0; d < t.count; ++d) {
                    if (t.rows[d].to == SceneId::Count) continue;
                    if (stage < t.rows[d].needs) continue;
                    for (int b = 0; b < N_DISTRICTS; ++b)
                        if (DISTRICTS[b] == t.rows[d].to) got[b] = true;
                }
            }
        }
        // First is where he washes up; the Second wants Cid; the Third wants the
        // wave spent.  BEHAVIOUR.md's stage table and town.s:1391/:1393.
        CHECK(got[0]);
        CHECK_EQ(got[1], stage >= TownStage::Second);
        CHECK_EQ(got[2], stage >= TownStage::Third);
    }

    // ...and the two gates, named.  One way into each gated district, so there
    // is nothing to walk around.
    int intoTwoFromOne = 0, intoThree = 0;
    for (SceneId from : DISTRICTS) {
        const TownDoors t = townDoorsFor(from);
        int out = 0;
        for (int d = 0; d < t.count; ++d) {
            if (t.rows[d].to == SceneId::Count) continue;
            ++out;
            if (t.rows[d].to == SceneId::Town2 && from == SceneId::Town1) {
                ++intoTwoFromOne;
                CHECK(t.rows[d].needs == TownStage::Second);
            }
            if (t.rows[d].to == SceneId::Town3) {
                ++intoThree;
                CHECK(t.rows[d].needs == TownStage::Third);
            }
            // Nothing leads out of a district and back into it.  On the Hotel's
            // square that would be a scene reload, which resets the wave -- an
            // exploit rather than a door.
            CHECK(t.rows[d].to != from);
        }
        // Anti-soft-lock: a district of nothing but shop fronts is a room the
        // player walks into and cannot leave.
        CHECK(out >= 1);
    }
    CHECK_EQ(intoTwoFromOne, 1);
    CHECK_EQ(intoThree, 1);

    // The backward doors are ungated, exactly as T_ARRIVE had them
    // (town.s:1392, :1394), and they lead only to districts already reached.
    const TownDoors two = townDoorsFor(SceneId::Town2);
    for (int d = 0; d < two.count; ++d)
        if (two.rows[d].to == SceneId::Town1)
            CHECK(two.rows[d].needs == TownStage::Arrive);
    const TownDoors three = townDoorsFor(SceneId::Town3);
    for (int d = 0; d < three.count; ++d)
        if (three.rows[d].to == SceneId::Town2)
            CHECK(three.rows[d].needs == TownStage::Arrive);
}

// ===========================================================================
// The two that lead nowhere
// ===========================================================================

KH_TEST(doors_a_shop_front_never_opens) {
    // The Accessory Shop (town1 16,4) and the Hotel (town2 24,4): NEW content,
    // painted into the districts, with nothing behind either of them.  There is
    // no fourth SceneId, no interior map and no cast, and there is no line in
    // the frozen ROM a shop front could say without lying -- build_scripts.py
    // checks every script byte for byte against kh.sfc, so a new one cannot be
    // written, and both existing shut lines are written for a specific gate.
    //
    // So: nothing, at EVERY stage.  Walked over all eight rather than over "a
    // couple", because the failure this rules out is a shop front that is shut
    // by having an out-of-range gate rather than by having no far side -- which
    // works until the stage it was hiding behind is reached.
    static const TownStage ALL[] = {
        TownStage::Arrive, TownStage::Look, TownStage::Second, TownStage::Third,
        TownStage::Meet, TownStage::Boss, TownStage::Won, TownStage::Over,
    };
    int shuttered = 0;
    for (SceneId from : DISTRICTS) {
        const TownDoors t = townDoorsFor(from);
        for (int d = 0; d < t.count; ++d) {
            if (t.rows[d].to != SceneId::Count) continue;
            ++shuttered;
            for (TownStage stage : ALL) {
                Stub w;
                Interact st;
                TownMachine m;
                m.begin(w.rng);
                m.setStage(stage);
                m.setDistrict(from);
                SceneView v = w.view();
                const StageStep s =
                    stepOnto(m, st, w, v, t.rows, t.count, t.rows[d].at);
                CHECK(s.action == SceneAction::None);
                CHECK(s.script == ScriptId::None);
                CHECK_EQ(m.doorTimer(), 0);
                CHECK(m.stage() == stage);          // and nothing moved
            }
        }
    }
    CHECK_EQ(shuttered, 2);
}

// ===========================================================================
// End to end, on the real tables
// ===========================================================================

KH_TEST(doors_the_ladder_runs_end_to_end) {
    // The whole of Traverse Town's traversal, driven through the shipped tables
    // rather than a fixture: the bolted door, Cid, the way through, the way on,
    // and both ways back.  Every earlier case here is a property of the data;
    // this is the one that proves the data and the code agree about what a
    // playthrough does.
    Stub w;
    Interact st;
    TownMachine m;
    m.begin(w.rng);
    m.setDistrict(SceneId::Town1);
    SceneView v = w.view();

    const TownDoors one = townDoorsFor(SceneId::Town1);
    const TownDoors two = townDoorsFor(SceneId::Town2);
    const TownDoors three = townDoorsFor(SceneId::Town3);

    // Find the rows by destination rather than by index, so this case says what
    // it means and does not quietly change meaning if the emitted order does.
    const TownDoor* toTwo = nullptr;
    for (int d = 0; d < one.count; ++d)
        if (one.rows[d].to == SceneId::Town2) toTwo = &one.rows[d];
    const TownDoor* toThree = nullptr;
    const TownDoor* backToOne = nullptr;
    for (int d = 0; d < two.count; ++d) {
        if (two.rows[d].to == SceneId::Town3) toThree = &two.rows[d];
        if (two.rows[d].to == SceneId::Town1) backToOne = &two.rows[d];
    }
    const TownDoor* backToTwo = nullptr;
    for (int d = 0; d < three.count; ++d)
        if (three.rows[d].to == SceneId::Town2) backToTwo = &three.rows[d];
    CHECK(toTwo != nullptr);
    CHECK(toThree != nullptr);
    CHECK(backToOne != nullptr);
    CHECK(backToTwo != nullptr);
    if (!toTwo || !toThree || !backToOne || !backToTwo) return;

    // 1. At Look the way out of the First District is bolted, and says so.
    m.setStage(TownStage::Look);
    StageStep s = stepOnto(m, st, w, v, one.rows, one.count, toTwo->at);
    CHECK(s.action == SceneAction::Say);
    CHECK(s.script == ScriptId::TownShut1);
    CHECK_EQ(m.doorTimer(), 0);

    // 2. Cid.  This is the only thing in the tree that sets Second, so without
    //    it every step below is unreachable -- which is the point of driving it
    //    here rather than calling setStage.
    w.putPlayer(17, 6);
    w.actors.spawn(ActType::Cid, tileCentre(17), tileCentre(6));
    w.press(Button::A);
    s = townInteract(m, st, v, one.rows, one.count);
    CHECK(s.action == SceneAction::HudChanged);
    CHECK(s.script == ScriptId::TownCid);
    CHECK(m.stage() == TownStage::Second);
    w.release();

    // 3. ...and now it opens, onto the tile the far side puts him on.
    ScreenFx fx;
    s = stepOnto(m, st, w, v, one.rows, one.count, toTwo->at);
    CHECK(s.action == SceneAction::None);
    CHECK_EQ(m.doorTimer(), DOOR_FADE * 2);
    StageStep sw = runToSwap(m, v, fx);
    CHECK(sw.action == SceneAction::EnterDistrict);
    CHECK_EQ(int(sw.arg), int(SceneId::Town2));
    // ...and the landing the caller is to stand him on, which was write-only in
    // the whole tree before this milestone.  28 is town2's own door BACK to the
    // First District: the far side's tile, not this door's.
    CHECK_EQ(m.doorLanding().i, 28);
    CHECK_EQ(m.doorLanding().j, DOOR_ROW + 1);

    // 4. In the Second District, the way on is bolted until the wave is spent,
    //    and it is the OTHER line: scriptShut2, town.s:337-346 picks on the gate.
    TownMachine m2;
    m2.begin(w.rng);
    m2.setStage(TownStage::Second);
    m2.setDistrict(SceneId::Town2);
    Interact st2;
    s = stepOnto(m2, st2, w, v, two.rows, two.count, toThree->at);
    CHECK(s.action == SceneAction::Say);
    CHECK(s.script == ScriptId::TownShut2);
    CHECK_EQ(m2.doorTimer(), 0);

    // 5. Wave spent: through to the Third District.
    m2.setStage(TownStage::Third);
    s = stepOnto(m2, st2, w, v, two.rows, two.count, toThree->at);
    CHECK(s.action == SceneAction::None);
    CHECK_EQ(m2.doorTimer(), DOOR_FADE * 2);
    sw = runToSwap(m2, v, fx);
    CHECK(sw.action == SceneAction::EnterDistrict);
    CHECK_EQ(int(sw.arg), int(SceneId::Town3));
    CHECK_EQ(m2.doorLanding().i, 23);
    CHECK_EQ(m2.doorLanding().j, DOOR_ROW + 1);

    // 6. Both ways back are ungated -- "the player can walk back through any
    //    door they have already opened and the town remembers where it had got
    //    to" -- and they are ungated at ARRIVE, the lowest stage there is.
    TownMachine m3;
    m3.begin(w.rng);
    m3.setStage(TownStage::Arrive);
    m3.setDistrict(SceneId::Town3);
    Interact st3;
    s = stepOnto(m3, st3, w, v, three.rows, three.count, backToTwo->at);
    CHECK(s.action == SceneAction::None);
    CHECK_EQ(m3.doorTimer(), DOOR_FADE * 2);
    sw = runToSwap(m3, v, fx);
    CHECK(sw.action == SceneAction::EnterDistrict);
    CHECK_EQ(int(sw.arg), int(SceneId::Town2));
    CHECK_EQ(m3.doorLanding().i, 5);
    CHECK_EQ(m3.doorLanding().j, DOOR_ROW + 1);

    TownMachine m4;
    m4.begin(w.rng);
    m4.setStage(TownStage::Arrive);
    m4.setDistrict(SceneId::Town2);
    Interact st4;
    s = stepOnto(m4, st4, w, v, two.rows, two.count, backToOne->at);
    CHECK(s.action == SceneAction::None);
    CHECK_EQ(m4.doorTimer(), DOOR_FADE * 2);
    sw = runToSwap(m4, v, fx);
    CHECK(sw.action == SceneAction::EnterDistrict);
    CHECK_EQ(int(sw.arg), int(SceneId::Town1));
    CHECK_EQ(m4.doorLanding().i, 24);
    CHECK_EQ(m4.doorLanding().j, DOOR_ROW + 1);

    // 7. A door is cleared by a death, landing included: a tile in a district
    //    that is no longer loaded must not survive into the next transition.
    //    TownRestart clears the door, town.s:94.
    m4.restart(fx);
    CHECK_EQ(m4.doorTimer(), 0);
    CHECK_EQ(m4.doorLanding().i, 0);
    CHECK_EQ(m4.doorLanding().j, 0);
}

// ===========================================================================
// The line riding on the redraw
// ===========================================================================
//
// WHY THIS CASE IS IN THE DOORS FILE.  Because Cid's step is the door table's
// power supply and this is the one property of it the ladder above cannot see.
// The ladder asserts that talking to Cid returns HudChanged carrying TownCid
// and that the door then opens; what it cannot assert is that Cid's is the ONLY
// HudChanged in the tree shaped that way -- and that sentence is load-bearing
// somewhere else entirely.
//
// perform() (platform/ds/host/trace_main.cpp) is the tree's only performer of a
// StageStep, and for a long time its HudChanged arm was a bare `return true`
// under a comment saying the redraw "changes no state the simulation can see".
// That was true of the redraw and false of the step, because the step can carry
// a ScriptId and the arm threw it away with everything else.  The arm now opens
// it, and auditPassengerSurvivesPerform() beside perform() proves it still does
// on every dstrace run -- but that check can only ever speak for the consumer.
// It cannot know whether some NEW step has started carrying a line on an arm
// that is implemented and silent (None, RespawnDistrict, RaiseArmor, BeginFall
// and SpawnMote would all drop one without a word today).
//
// So this is the producer half of the same contract, and the two together are
// what make it enforced rather than merely intended.  It drives all seven of
// the HudChanged steps the shipped tree can emit and pins which of them carries
// a passenger.  Add a line to one of the six bare ones and this fails and says
// to go and look at perform(); the alternative is that it works on the day it
// is written and is silently lost the first time anybody performs it.
//
// The six are driven through the machines' real entry points rather than being
// read off a table, because a table of "what the machines return" is a copy of
// the thing under test.

KH_TEST(doors_only_cids_hud_step_carries_a_line) {
    Stub w;
    SceneView v = w.view();
    ScreenFx fx;

    // Counted so that a site which quietly STOPS emitting HudChanged is a
    // failure too.  A case that silently exercises five of six sites still
    // passes every assertion it makes, which is the way this kind of test rots.
    int sites = 0;

    // ---- 1.  The town, waking up.  stage_town.cpp:159 ---------------------
    // Woke: the opening line has been dismissed, so the objective becomes FIND
    // SOMEBODY AWAKE.  Bare -- the SNES's TownUpdate calls HudUpdate here and
    // does not follow it with a Say.
    TownMachine mt;
    mt.begin(w.rng);
    mt.setStage(TownStage::Arrive);
    StageStep s = mt.update(v, fx);
    CHECK(s.action == SceneAction::HudChanged);
    CHECK_EQ(int(s.script), int(ScriptId::None));
    CHECK(mt.stage() == TownStage::Look);
    ++sites;

    // ---- 2.  The night, starting the search.  stage_night.cpp:173 ---------
    NightMachine mn;
    mn.begin(w.rng);
    mn.setStage(NightStage::Intro);
    s = mn.update(v, fx);
    CHECK(s.action == SceneAction::HudChanged);
    CHECK_EQ(int(s.script), int(ScriptId::None));
    CHECK(mn.stage() == NightStage::Seek);
    ++sites;

    // ---- 3.  Kairi hands over the list.  stage_island.cpp:158 -------------
    // THIS IS THE ONE CID IS MOST OFTEN CONFUSED WITH, and the confusion is the
    // reason interact.cpp's Cid arm carries its comment about not being the
    // island's shape: talkTo() bails out above the line lookup for a bare
    // HudChanged, so on the island "advance the state and redraw" says nothing
    // at all.  It must stay bare for that to keep being true.
    IslandMachine mi;
    mi.begin();
    mi.setState(QuestState::Idle);
    s = mi.talkToKairi(false);
    CHECK(s.action == SceneAction::HudChanged);
    CHECK_EQ(int(s.script), int(ScriptId::None));
    CHECK(mi.state() == QuestState::Active);
    ++sites;

    // ---- 4.  Day two's list handed in: Riku wants his race.  :145 ---------
    mi.setState(QuestState::Done);
    mi.setDay(2);
    s = mi.talkToKairi(false);
    CHECK(s.action == SceneAction::HudChanged);
    CHECK_EQ(int(s.script), int(ScriptId::None));
    CHECK(mi.state() == QuestState::RaceSet);
    ++sites;

    // ---- 5 and 6.  The countdown, ticking and then handing over -----------
    // stage_island.cpp:83 asks for a redraw on EVERY frame of the countdown --
    // the number on the race row is derived from the timer, so "the timer
    // moved" is the whole message -- and stage_island.cpp:78 asks once more on
    // the frame it starts the race.  Walking the whole countdown rather than
    // poking dayTimer_ means a passenger appearing on either is caught, and it
    // is COUNT_LEN + 1 frames because the terminal frame is the one that finds
    // the timer already at zero.
    IslandMachine mi2;
    mi2.begin();
    mi2.beginRace();
    CHECK(mi2.state() == QuestState::RaceSet);
    int ticks = 0;
    bool ticking = false;
    bool handedOver = false;
    for (int f = 0; f < COUNT_LEN + 4 && !handedOver; ++f) {
        s = mi2.update(v, fx);
        CHECK(s.action == SceneAction::HudChanged);
        CHECK_EQ(int(s.script), int(ScriptId::None));
        ++ticks;
        if (mi2.state() == QuestState::RaceRun) handedOver = true;
        else ticking = true;
    }
    CHECK(handedOver);
    CHECK(ticking);
    CHECK_EQ(ticks, COUNT_LEN + 1);
    sites += 2;

    // ---- 7.  Cid, and the reason all of this is here ----------------------
    // The SNES writes the stage, redraws the HUD and THEN speaks, all in one
    // eight-bit stretch of TalkTown's fall-through arm: `sta townStage` at
    // town.s:541, `jsr HudUpdate` at town.s:542, `jmp Say` at town.s:546.  One
    // StageStep, so the redraw is the action and the line is the passenger.
    Interact st;
    TownMachine mc;
    mc.begin(w.rng);
    mc.setDistrict(SceneId::Town1);
    mc.setStage(TownStage::Look);
    const TownDoors one = townDoorsFor(SceneId::Town1);
    w.putPlayer(17, 6);
    w.actors.spawn(ActType::Cid, tileCentre(17), tileCentre(6));
    w.press(Button::A);
    s = townInteract(mc, st, v, one.rows, one.count);
    w.release();
    CHECK(s.action == SceneAction::HudChanged);
    CHECK(s.script == ScriptId::TownCid);
    CHECK(mc.stage() == TownStage::Second);
    ++sites;

    // Seven sites, six bare and one carrying, and the count is asserted so that
    // deleting a site is as loud as changing one.  If this number has to move,
    // the thing to check before moving it is perform()'s arm for whatever the
    // new step's action is.
    CHECK_EQ(sites, 7);
}
