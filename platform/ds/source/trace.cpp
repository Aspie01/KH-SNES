#include "trace.h"

namespace kh {
namespace {

// A cursor over the caller's buffer that goes sterile on the first overflow.
// Checking every append at the call site would be six lines of the same `if`
// per column; checking once at the end is the same guarantee with none of them,
// PROVIDED nothing after an overflow can write -- which is what `ok_` is for.
class Out {
public:
    Out(char* buf, size_t cap) : p_(buf), cap_(cap) {}

    void ch(char c) {
        if (n_ + 1 > cap_) { ok_ = false; return; }
        p_[n_++] = c;
    }
    void str(const char* s) {
        for (; *s; ++s) ch(*s);
    }
    // Unsigned only, and deliberately: every number in this format is either a
    // count, an enumerator or a Q12.4 position read as a 16-bit word.  There is
    // no signed column, and offering one would invite a caller to make a new one.
    void num(uint32_t v) {
        char tmp[10];
        int i = 0;
        do { tmp[i++] = char('0' + (v % 10)); v /= 10; } while (v);
        while (i) ch(tmp[--i]);
    }

    size_t done() {
        if (!ok_ || n_ >= cap_) return 0;
        p_[n_] = '\0';
        return n_;
    }

private:
    char* p_;
    size_t cap_;
    size_t n_ = 0;
    bool ok_ = true;
};

// A world position, as the oracle reads it.
//
// tools/snes_trace.py reads actX as a 16-bit WRAM word and prints it unsigned,
// because that is what the bytes are: the 65816 build stored Q12.4 in two bytes
// and had no wider register to sign-extend into.  World here is backed by an
// int32_t, so an actor that went negative -- an orb leaving the disc, a mote
// above the top of the map -- would print -16 against the oracle's 65520 and be
// reported as a divergence that is really an encoding difference.
//
// So the low sixteen bits are what goes out.  Every DS map is well inside them:
// the widest is 64 tiles, which is 16384 raw, half of the positive range.
uint32_t asOracleWord(World w) {
    return uint32_t(w.raw()) & 0xFFFFu;
}

}  // namespace

size_t traceHeader(char* out, size_t cap, const char* platform, const char* rev) {
    Out o(out, cap);
    o.str("#kh-trace\tv");
    o.num(uint32_t(TRACE_VERSION));
    o.str("\tplatform=");
    o.str(platform);
    o.str("\trev=");
    o.str(rev);
    // The column names, and they are the CONTRACT: trace_diff.py matches
    // columns by name across the two files, so a rename here silently stops a
    // column being compared rather than failing.  tools/ds_trace.py checks this
    // line against tools/snes_trace.py's byte for byte on every run, which is
    // the only cross-language check available and is worth more than a comment.
    o.str("\n#fields\tframe\tpx\tpy\tpz\tpdir\tpstate\tptimer\tphp"
          "\tdiveStage\tquestState\tnightStage\ttownStage\tsceneId\tbossHP"
          "\tcamX\tcamY\tbgHOfs\tbgVOfs"
          "\tnactors\tactor=idx/type/x/y/state/timer/hp...\n");
    return o.done();
}

size_t traceLine(char* out, size_t cap, uint32_t frame, const Actors& a,
                 int player, const TraceStage& stage, const Camera& cam) {
    Out o(out, cap);
    o.num(frame);

    // The player's own columns.  He appears again below as an ordinary actor,
    // and that duplication is deliberate: playerIdx is itself state, and a diff
    // that showed Sora's slot changing would otherwise have nowhere to show it.
    const bool live = player >= 0 && player < MAX_ACTORS;
    o.ch('\t'); o.num(live ? asOracleWord(a.x[player]) : 0);
    o.ch('\t'); o.num(live ? asOracleWord(a.y[player]) : 0);
    o.ch('\t'); o.num(live ? a.z[player] : 0);
    o.ch('\t'); o.num(live ? uint32_t(a.dir[player]) : 0);
    o.ch('\t'); o.num(live ? uint32_t(a.state[player]) : 0);
    o.ch('\t'); o.num(live ? a.timer[player] : 0);
    o.ch('\t'); o.num(live ? a.hp[player] : 0);

    o.ch('\t'); o.num(stage.diveStage);
    o.ch('\t'); o.num(stage.questState);
    o.ch('\t'); o.num(stage.nightStage);
    o.ch('\t'); o.num(stage.townStage);
    o.ch('\t'); o.num(stage.sceneId);
    o.ch('\t'); o.num(stage.bossHP);

    // The camera, in whole pixels.  Emitted as sixteen-bit words for the same
    // reason the positions are: the oracle reads two WRAM bytes and prints them
    // unsigned, and the SNES's bgVOfs is (camY - 1) & 0x3FF, which is 1023 on
    // any frame camY clamps to zero.  A DS that biased by one would match that;
    // it must not, so the difference has to be visible rather than encoded away.
    o.ch('\t'); o.num(uint32_t(cam.x) & 0xFFFFu);
    o.ch('\t'); o.num(uint32_t(cam.y) & 0xFFFFu);
    o.ch('\t'); o.num(uint32_t(cam.bgHOfs) & 0xFFFFu);
    o.ch('\t'); o.num(uint32_t(cam.bgVOfs) & 0xFFFFu);

    int n = 0;
    for (int i = 0; i < MAX_ACTORS; ++i)
        if (a.type[i] != ActType::None) ++n;
    o.ch('\t'); o.num(uint32_t(n));

    // IN SLOT ORDER, which is not an arbitrary choice.  The SNES updated actors
    // in slot order and allocated by first-free scan, so slot order is part of
    // the specification; a port that reordered them would be wrong for reasons
    // that have nothing to do with this file.
    for (int i = 0; i < MAX_ACTORS; ++i) {
        if (a.type[i] == ActType::None) continue;
        o.ch('\t');
        o.num(uint32_t(i));           o.ch('/');
        o.num(uint32_t(a.type[i]));   o.ch('/');
        o.num(asOracleWord(a.x[i]));  o.ch('/');
        o.num(asOracleWord(a.y[i]));  o.ch('/');
        o.num(uint32_t(a.state[i]));  o.ch('/');
        o.num(a.timer[i]);            o.ch('/');
        o.num(a.hp[i]);
    }
    return o.done();
}

}  // namespace kh
