// The dialogue scripts.
//
// The generated header is already verified byte-for-byte against the frozen ROM
// by tools/build_scripts.py, which is a stronger check than anything that can be
// written here.  What is left for a test is the part the extractor cannot see:
// that the table and the enum agree, that every script is well formed as the
// INTERPRETER understands it, and that the scripts the stage machines name are
// the ones that get said.

#include <cstring>

#include "check.h"
#include "gen/scripts.h"
#include "stage.h"

using namespace kh;

namespace {

// Run a whole script through the real interpreter and hand back what it drew,
// pages joined by a form feed so a test can see where the breaks fell.
int readWholeScript(ScriptId id, char* out, size_t cap, int* pagesOut) {
    Dialogue d;
    Pad pad;
    d.open(scriptFor(id), TextMode::Message);
    int pages = 0;
    size_t n = 0;
    for (int guard = 0; guard < 20000 && d.busy(); ++guard) {
        if (d.state() == TextState::Wait) {
            char buf[256];
            d.text(buf, sizeof buf);
            const size_t len = std::strlen(buf);
            for (size_t i = 0; i < len && n + 2 < cap; ++i) out[n++] = buf[i];
            ++pages;
            if (d.moreToCome() && n + 2 < cap) out[n++] = '\f';
            // Tap A: press, then release, which is what txtHold requires.
            pad.held = raw(Button::A);
            pad.pressed = raw(Button::A);
            d.update(pad);
            pad.held = 0;
            pad.pressed = 0;
            continue;
        }
        d.update(pad);
    }
    if (cap) out[n] = '\0';
    if (pagesOut) *pagesOut = pages;
    return int(n);
}

}  // namespace

KH_TEST(scripts_the_table_and_the_enum_agree) {
    // The static_assert in the generated header already proves the lengths
    // match.  What it cannot prove is that None is null and that everything else
    // is not -- an off-by-one in the emitter would satisfy the size check and
    // shift every script by one.
    CHECK(scriptFor(ScriptId::None).empty());
    for (int i = 1; i < int(ScriptId::Count); ++i) {
        const Script s = scriptFor(ScriptId(i));
        CHECK(!s.empty());
        CHECK(s.data != nullptr);
        CHECK(s.size >= 1);
    }
    // Out of range is a null Script and not a read off the end.
    CHECK(scriptFor(ScriptId::Count).empty());
    CHECK(scriptFor(ScriptId(9999)).empty());

    // 76 scripts, plus None.  It was 70 until §M5's audit: the generator's
    // label pattern only matched `script*`, and dive.s calls the six dream
    // weapon descriptions descSwordTake, descSwordDrop and so on -- so they
    // were never extracted, and the weapon choice had nothing to say.
    CHECK_EQ(int(ScriptId::Count), 77);
}

KH_TEST(scripts_are_all_well_formed_for_the_interpreter) {
    // Every script must end in SC_END and contain no other terminator, or the
    // interpreter stops early and the rest is unreachable for a second reason.
    // Every byte must be a character or one of the three codes.
    int totalBytes = 0;
    int pageBreaks = 0;
    int withPages = 0;
    for (int i = 1; i < int(ScriptId::Count); ++i) {
        const Script s = scriptFor(ScriptId(i));
        totalBytes += int(s.size);
        CHECK_EQ(s.data[s.size - 1], SC_END);
        int pages = 0;
        for (size_t k = 0; k + 1 < s.size; ++k) {
            const uint8_t b = s.data[k];
            CHECK(b != SC_END);              // no early terminator
            CHECK(b >= 32 || b == SC_NL || b == SC_PAGE);
            if (b == SC_PAGE) ++pages;
        }
        pageBreaks += pages;
        if (pages) ++withPages;
    }
    // The figures tools/build_scripts.py reports, asserted from the other side.
    // 5352 before the six descriptions were found; they carry 422 characters
    // between them, and every one was unreachable on both machines.
    CHECK_EQ(totalBytes, 5774);
    CHECK_EQ(pageBreaks, 79);
    CHECK_EQ(withPages, 54);
}

KH_TEST(scripts_every_character_has_a_glyph) {
    // asciiToTile maps everything it does not know to CH_BLANK, so a stray
    // character does not corrupt anything -- it just leaves a hole in a sentence
    // that nobody notices.  Assert there are none.
    for (int i = 1; i < int(ScriptId::Count); ++i) {
        const Script s = scriptFor(ScriptId(i));
        for (size_t k = 0; k < s.size; ++k) {
            const uint8_t b = s.data[k];
            if (b < 32) continue;
            if (b == ' ') continue;          // legitimately CH_BLANK
            CHECK(glyphOf(char(b)) != CH_BLANK);
        }
    }
}

KH_TEST(scripts_fit_the_box_they_are_drawn_in) {
    // The box is 28 columns by 5 lines.  PutChar wraps mid-word at 28 and
    // NewLine CLAMPS at the last line rather than scrolling, so a page with too
    // many lines silently overwrites its own bottom row.  Nothing in the engine
    // complains; this is the only place it can be caught.
    for (int i = 1; i < int(ScriptId::Count); ++i) {
        const Script s = scriptFor(ScriptId(i));
        int col = 0, row = 0;
        for (size_t k = 0; k < s.size; ++k) {
            const uint8_t b = s.data[k];
            if (b == SC_NL) { col = 0; ++row; continue; }
            if (b == SC_PAGE || b == SC_END) { col = 0; row = 0; continue; }
            if (++col >= TEXT_W) { col = 0; ++row; }
        }
        CHECK(row < TEXT_H);
    }
}

KH_TEST(scripts_read_back_through_the_interpreter) {
    // The end-to-end check: drive a real script through the real Dialogue and
    // compare what lands on the page against the sentence it is supposed to be.
    // This is the one that would catch an extraction that produced valid bytes
    // in the wrong order.
    char buf[1024];
    int pages = 0;
    readWholeScript(ScriptId::DiveIntro, buf, sizeof buf, &pages);
    CHECK_EQ(pages, 3);                      // two page breaks, three pages
    CHECK(std::strcmp(buf,
                      "SO MUCH TO DO,\n"
                      "SO LITTLE TIME.\n"
                      "\n"
                      "TAKE YOUR TIME."
                      "\f"
                      "DON'T BE AFRAID.\n"
                      "\n"
                      "THE DOOR IS STILL SHUT."
                      "\f"
                      "POWER SLEEPS WITHIN YOU.\n"
                      "GIVE IT FORM, AND IT WILL\n"
                      "GIVE YOU STRENGTH.\n"
                      "\n"
                      "CHOOSE WELL.") == 0);

    // On the SNES only the first of those three pages was ever displayed.
    // docs/behaviour/divergences/005-sc-page.md.
    CHECK(pages > 1);
}

KH_TEST(scripts_every_one_can_be_read_to_its_end) {
    // A property over all seventy: the interpreter always terminates, always
    // reaches SC_END, and never trips the 512-iteration backstop.
    char buf[2048];
    for (int i = 1; i < int(ScriptId::Count); ++i) {
        int pages = 0;
        const int n = readWholeScript(ScriptId(i), buf, sizeof buf, &pages);
        CHECK(pages >= 1);
        CHECK(n > 0);                        // no script is silent
        // Every page break produced a page, and the last one ended the message.
        const Script s = scriptFor(ScriptId(i));
        int breaks = 0;
        for (size_t k = 0; k < s.size; ++k)
            if (s.data[k] == SC_PAGE) ++breaks;
        CHECK_EQ(pages, breaks + 1);
    }
}

KH_TEST(scripts_the_lookup_tables_point_where_they_should) {
    // The .word tables, generated from the assembly so their order cannot drift.
    // namedLines is the one the island machine indexes, and it is indexed by the
    // raft name minus one because RAFT_HIGHWIND is 1.
    CHECK_EQ(sizeof NAMEDLINES / sizeof NAMEDLINES[0], 3u);
    CHECK(NAMEDLINES[int(RaftName::Highwind) - 1] == ScriptId::IslandNamedHighwind);
    CHECK(NAMEDLINES[int(RaftName::Excalibur) - 1] == ScriptId::IslandNamedExcalibur);
    CHECK(NAMEDLINES[int(RaftName::Ragnarok) - 1] == ScriptId::IslandNamedRagnarok);

    // Kairi has four lines per day and the order is load-bearing: index 0 is the
    // rest-up line, 2 the list, 4 "that's everything", 6 "still something
    // missing" -- the assembly indexes them by 2 because they were words.
    CHECK_EQ(sizeof KAIRILINES / sizeof KAIRILINES[0], 4u);
    CHECK_EQ(sizeof KAIRI2LINES / sizeof KAIRI2LINES[0], 4u);
    CHECK(KAIRILINES[0] == ScriptId::IslandKairiRest);
    CHECK(KAIRILINES[1] == ScriptId::IslandKairiAsk);
    CHECK(KAIRILINES[2] == ScriptId::IslandKairiFinish);
    CHECK(KAIRILINES[3] == ScriptId::IslandKairiRemind);

    // Audit finding 48: kairiLines[0] is UNREACHABLE.  Day one's Done diverts to
    // EndOfDay before the player gets a frame of control, so the rest-up line can
    // never play -- and its unreachability is the proof that the day-one and
    // day-two paths really are asymmetric.  It is kept because deleting a line of
    // dialogue to satisfy a port is the wrong direction.
    CHECK(!scriptFor(KAIRILINES[0]).empty());

    // The eight "you got one" lines, one per collectable, in item order.
    CHECK_EQ(sizeof GOTLINES / sizeof GOTLINES[0], 8u);
    CHECK(GOTLINES[0] == ScriptId::IslandGotLog);
    CHECK(GOTLINES[7] == ScriptId::IslandGotFish);

    // The Secret Place wall: four lines for three props, because the door says
    // something different once the raft has a name.
    CHECK_EQ(sizeof PROPLINES / sizeof PROPLINES[0], 4u);
    CHECK(PROPLINES[0] == ScriptId::IslandDoor);
    CHECK(PROPLINES[1] == ScriptId::IslandDoor2);
}

KH_TEST(scripts_the_stage_machines_name_real_lines) {
    // Every ScriptId a machine returns must resolve to bytes.  A machine that
    // named None would open an empty box, which is a bug the interpreter cannot
    // distinguish from an intentionally silent beat.
    Actors actors;
    actors.clear();
    Dialogue dialogue;
    Pad pad;
    SceneGround ground;
    Rng rng;
    const int player = actors.spawn(ActType::Sora, tileCentre(11), tileCentre(12));
    SceneView view{actors, dialogue, pad, ground, rng, player, 0};
    ScreenFx fx;

    // Walk the night end to end and check every line it names.
    NightMachine n;
    n.begin(rng);
    int said = 0;
    for (int guard = 0; guard < 2000; ++guard) {
        const StageStep s = n.update(view, fx);
        if (s.script != ScriptId::None) {
            CHECK(!scriptFor(s.script).empty());
            ++said;
        }
        if (n.stage() == NightStage::Seek) n.talkToRiku();
        if (n.stage() == NightStage::Kairi) {
            const StageStep t = n.talkToKairi();
            if (t.script != ScriptId::None) {
                CHECK(!scriptFor(t.script).empty());
                ++said;
            }
        }
        if (n.stage() == NightStage::Over) break;
    }
    CHECK(n.stage() == NightStage::Over);
    CHECK(said >= 5);

    // And the Dive's, which has the longest chain of named lines.
    DiveMachine d;
    d.begin();
    d.setStage(DiveStage::S2Fight);
    for (int guard = 0; guard < 2000 && d.stage() != DiveStage::Arrived; ++guard) {
        const StageStep s = d.update(view, fx);
        if (s.script != ScriptId::None) CHECK(!scriptFor(s.script).empty());
    }
    CHECK(d.stage() == DiveStage::Arrived);
}
