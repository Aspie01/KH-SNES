#pragma once
// The per-frame trace: the DS half of §M6's oracle diff.
//
// tools/snes_trace.py runs the frozen ROM headlessly and writes one line a frame
// of everything that matters.  This writes the same lines from the Tier-1
// simulation, and tools/trace_diff.py compares the two knowing which fields
// docs/behaviour/divergences/ has already accounted for.  Two emitters, one
// format, and the format is the contract between them.
//
// WHY IT IS HERE AND NOT IN host/.  Formatting a line touches nothing but the
// actor table and five bytes of scene state, so it costs nothing to keep
// platform-neutral -- and keeping it here means the host test suite links it and
// can assert the format directly, which is the only way the contract gets
// checked on this side at all.  The DRIVER is host-only, because scenarios and
// files are, and it lives in platform/ds/host/trace_main.cpp.
//
// NO stdio.  Not because a DS cannot printf -- libnds can -- but because the
// only thing that makes the two emitters comparable is that they agree digit for
// digit, and hand-written integer conversion is the version with no locale, no
// padding rules and no format-string to get subtly wrong.

#include <cstddef>
#include <cstdint>

#include "actor.h"
#include "grid.h"

namespace kh {

// Bumped when a column is added, removed or reinterpreted.  tools/snes_trace.py
// carries the same number and trace_diff.py refuses a pair that disagrees about
// what the columns mean.
constexpr int TRACE_VERSION = 2;

// v2 added the four camera columns.  They were outside the format for as long as
// the format existed, which meant §M3's camera had never been compared against
// anything -- and divergence 001 named `camY` and `bgVOfs` as the fields it
// excused when neither was a column, so it excused nothing and its suppression
// count was always zero.
//
// The five scene bytes the oracle samples out of WRAM, plus the boss gauge.
// They are one struct rather than six arguments because the ORDER is the
// column order, and a caller that swapped two of them would produce a trace
// that diffs clean against a shifted one.
struct TraceStage {
    uint8_t diveStage = 0;
    uint8_t questState = 0;
    uint8_t nightStage = 0;
    uint8_t townStage = 0;
    uint8_t sceneId = 0;
    uint8_t bossHP = 0;
};

// The two header lines, terminated by a newline each.  `platform` is the name
// trace_diff.py prints ("ds"), and `rev` is the git revision of the run -- which
// the caller is given rather than deriving, because the only revision worth
// stamping is the one the whole comparison was made at and this file cannot see
// it.
//
// Returns the length written, or 0 if it did not fit.  A truncated header is a
// trace nothing can read, so the caller must treat 0 as fatal rather than as a
// short write.
size_t traceHeader(char* out, size_t cap, const char* platform, const char* rev);

// One frame, with NO trailing newline -- the caller adds it, because on the host
// that is one fputc and in a future device build it might be a packet boundary.
//
// Returns the length written, or 0 if it did not fit.  0 is fatal for the same
// reason: half a line is a line that diffs against something.
size_t traceLine(char* out, size_t cap, uint32_t frame, const Actors& a,
                 int player, const TraceStage& stage, const Camera& cam);

// How big a line can get: the fixed columns, then every actor in the pool.  A
// caller that sizes its buffer from this cannot be truncated by a full pool.
constexpr size_t TRACE_LINE_MAX = 128 + size_t(MAX_ACTORS) * 40;

}  // namespace kh
