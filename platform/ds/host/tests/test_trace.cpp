// The trace format -- §M6's contract between two emitters written in two
// languages.
//
// tools/ds_trace.py --check-format compares this side's `#fields` line against
// tools/snes_trace.py's byte for byte, which is the check that matters and the
// only one that can see both.  What it cannot do is run in Gate 0 without a
// Python interpreter and a built binary, so the contract is ALSO pinned here:
// the string below is the format, and changing it should take two edits and a
// moment's thought rather than one and none.
//
// The rest of this file is about the things a formatter gets wrong quietly --
// column order, an encoding that differs from the oracle's, a buffer that
// truncates instead of failing.

#include <cstring>

#include "check.h"
#include "trace.h"

using namespace kh;

namespace {

// The contract, verbatim.  If this needs changing, tools/snes_trace.py's
// header() needs the same change in the same commit, and TRACE_VERSION needs
// bumping if a column changed MEANING rather than merely appearing.
const char FIELDS[] =
    "#fields\tframe\tpx\tpy\tpz\tpdir\tpstate\tptimer\tphp"
    "\tdiveStage\tquestState\tnightStage\ttownStage\tsceneId\tbossHP"
    "\tnactors\tactor=idx/type/x/y/state/timer/hp...\n";

char buf[TRACE_LINE_MAX];

// The nth tab-separated field of a formatted line.  Returns "" past the end, so
// a test that asks for a column the line does not have fails on the comparison
// rather than reading off the end.
const char* field(const char* line, int n) {
    static char out[64];
    const char* p = line;
    for (int i = 0; i < n; ++i) {
        p = std::strchr(p, '\t');
        if (!p) { out[0] = '\0'; return out; }
        ++p;
    }
    const char* end = std::strchr(p, '\t');
    const size_t len = end ? size_t(end - p) : std::strlen(p);
    const size_t n2 = len < sizeof out - 1 ? len : sizeof out - 1;
    std::memcpy(out, p, n2);
    out[n2] = '\0';
    return out;
}

}  // namespace

KH_TEST(trace_the_header_names_the_run_and_pins_the_columns) {
    const size_t n = traceHeader(buf, sizeof buf, "ds", "deadbee-dirty");
    CHECK(n != 0);
    CHECK_EQ(std::strncmp(buf, "#kh-trace\tv1\tplatform=ds\trev=deadbee-dirty\n",
                          43), 0);
    const char* fields = std::strchr(buf, '\n');
    CHECK(fields != nullptr);
    if (fields) CHECK_EQ(std::strcmp(fields + 1, FIELDS), 0);
    // The version is emitted, not assumed: a reader that cannot tell v1 from v2
    // would compare two formats and report the difference as a divergence.
    CHECK_EQ(TRACE_VERSION, 1);
}

KH_TEST(trace_a_header_that_would_not_fit_fails_rather_than_truncating) {
    char small[40];
    CHECK_EQ(traceHeader(small, sizeof small, "ds", "deadbee"), size_t(0));
}

KH_TEST(trace_the_fixed_columns_are_in_the_oracles_order) {
    Actors a;
    a.clear();
    const int sora = a.spawn(ActType::Sora, World::fromRaw(4224),
                             World::fromRaw(2944));
    a.z[sora] = 3;
    a.dir[sora] = Dir::NW;
    a.state[sora] = ActState::Attack;
    a.timer[sora] = 7;
    a.hp[sora] = 19;

    TraceStage st{};
    st.diveStage = 8;
    st.questState = 6;
    st.nightStage = 4;
    st.townStage = 5;
    st.sceneId = 2;
    st.bossHP = 36;

    const size_t n = traceLine(buf, sizeof buf, 137, a, sora, st);
    CHECK(n != 0);
    CHECK_EQ(std::strcmp(field(buf, 0), "137"), 0);
    CHECK_EQ(std::strcmp(field(buf, 1), "4224"), 0);
    CHECK_EQ(std::strcmp(field(buf, 2), "2944"), 0);
    CHECK_EQ(std::strcmp(field(buf, 3), "3"), 0);
    CHECK_EQ(std::strcmp(field(buf, 4), "5"), 0);          // Dir::NW
    CHECK_EQ(std::strcmp(field(buf, 5), "2"), 0);          // ActState::Attack
    CHECK_EQ(std::strcmp(field(buf, 6), "7"), 0);
    CHECK_EQ(std::strcmp(field(buf, 7), "19"), 0);
    CHECK_EQ(std::strcmp(field(buf, 8), "8"), 0);
    CHECK_EQ(std::strcmp(field(buf, 9), "6"), 0);
    CHECK_EQ(std::strcmp(field(buf, 10), "4"), 0);
    CHECK_EQ(std::strcmp(field(buf, 11), "5"), 0);
    CHECK_EQ(std::strcmp(field(buf, 12), "2"), 0);
    CHECK_EQ(std::strcmp(field(buf, 13), "36"), 0);
    CHECK_EQ(std::strcmp(field(buf, 14), "1"), 0);         // nactors
    // ...and the player appears AGAIN as an ordinary actor, which is how a diff
    // gets somewhere to show playerIdx changing.
    CHECK_EQ(std::strcmp(field(buf, 15), "0/1/4224/2944/2/7/19"), 0);
}

KH_TEST(trace_a_position_is_encoded_the_way_the_oracle_reads_one) {
    // snes_trace.py reads actX as a 16-bit WRAM word and prints it unsigned,
    // because the 65816 build had nothing wider to sign-extend into.  World here
    // is an int32_t, so an actor that went negative would print -16 against the
    // oracle's 65520 and be reported as a divergence that is really an encoding
    // difference.  The low sixteen bits are what goes out.
    Actors a;
    a.clear();
    const int m = a.spawn(ActType::Mote, World::fromRaw(-16), World::fromRaw(-1));
    TraceStage st{};
    CHECK(traceLine(buf, sizeof buf, 0, a, m, st) != 0);
    CHECK_EQ(std::strcmp(field(buf, 1), "65520"), 0);
    CHECK_EQ(std::strcmp(field(buf, 2), "65535"), 0);
}

KH_TEST(trace_live_actors_come_out_in_slot_order_with_the_holes_skipped) {
    Actors a;
    a.clear();
    const int sora = a.spawn(ActType::Sora, tileCentre(4), tileCentre(4));
    const int one = a.spawn(ActType::Shadow, tileCentre(5), tileCentre(5));
    const int two = a.spawn(ActType::Shadow, tileCentre(6), tileCentre(6));
    a.type[one] = ActType::None;        // a hole in the middle, as a kill leaves
    CHECK_EQ(sora, 0);
    CHECK_EQ(two, 2);

    TraceStage st{};
    CHECK(traceLine(buf, sizeof buf, 1, a, sora, st) != 0);
    CHECK_EQ(std::strcmp(field(buf, 14), "2"), 0);
    // Slot order, and the surviving Shadow keeps slot 2 rather than being
    // renumbered -- the SNES updated in slot order and allocated by first-free
    // scan, so a slot IS state and the trace has to show it.
    CHECK_EQ(std::strncmp(field(buf, 15), "0/", 2), 0);
    CHECK_EQ(std::strncmp(field(buf, 16), "2/2/", 4), 0);
    CHECK_EQ(std::strcmp(field(buf, 17), ""), 0);
}

KH_TEST(trace_a_full_pool_still_fits_the_advertised_buffer) {
    // TRACE_LINE_MAX exists so a caller can size a buffer once and never think
    // about it.  That is only true if it is right, and the expensive way to find
    // out is a trace that stops on the frame a wave peaks.
    Actors a;
    a.clear();
    for (int i = 0; i < MAX_ACTORS; ++i)
        CHECK(a.spawn(ActType::Shadow, World::fromRaw(65535),
                      World::fromRaw(65535)) >= 0);
    for (int i = 0; i < MAX_ACTORS; ++i) {
        a.timer[i] = 255;
        a.hp[i] = 255;
        a.state[i] = ActState::Fall;
    }
    TraceStage st{};
    const size_t n = traceLine(buf, sizeof buf, 4294967295u, a, 0, st);
    CHECK(n != 0);
    CHECK(n < TRACE_LINE_MAX);
    CHECK_EQ(std::strcmp(field(buf, 14), "128"), 0);
}

KH_TEST(trace_a_line_that_would_not_fit_fails_rather_than_truncating) {
    // Half a line is not a short line, it is a line that diffs against
    // something.  The caller has to be able to tell.
    Actors a;
    a.clear();
    a.spawn(ActType::Sora, tileCentre(4), tileCentre(4));
    TraceStage st{};
    char small[20];
    CHECK_EQ(traceLine(small, sizeof small, 1, a, 0, st), size_t(0));
    // ...and one byte short of enough is still a failure, not a silent trim.
    const size_t need = traceLine(buf, sizeof buf, 1, a, 0, st);
    CHECK(need != 0);
    CHECK_EQ(traceLine(buf, need, 1, a, 0, st), size_t(0));
    CHECK_EQ(traceLine(buf, need + 1, 1, a, 0, st), need);
}
