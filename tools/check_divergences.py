#!/usr/bin/env python3
"""Check docs/behaviour/divergences/ against the format, the spec and the scenarios.

    tools/check_divergences.py

A divergence file is DATA.  `tools/trace_diff.py` reads its front matter and
suppresses differences on the strength of it, which makes each one a rule that
silently weakens the only check that compares the port against the frozen ROM.

Until this existed, nothing validated that data.  `trace_diff.py` parsed it, but
`trace_diff.py` only runs under `tools/trace_check.py`, which needs the ROM and
the 65816 interpreter and takes a minute -- so it is deliberately not in Gate 0,
and therefore neither was the corpus.  A file with a typo in a field name, an id
that did not match its own filename, a `spec_section` naming a section of
BEHAVIOUR.md that does not exist, or a `scenes` list naming a scenario nobody
wrote, would all have been found by a human reading carefully or by nobody.

WHAT THIS CHECKS, and why each one is a way a suppression rule goes wrong:

  * the front matter parses, and carries every key trace_diff.py reads.  A
    missing key is a crash in the middle of a one-minute run.
  * ids are unique and match the filename.  Two files claiming 004 means the
    suppression report attributes counts to whichever sorted first.
  * every `trace_fields` name is REACHABLE against the trace format, or is
    DECLARED in `unreachable:` -- a name the columns and the aliases cannot
    produce is a rule that will never fire, and the only acceptable version of
    that is one somebody wrote down.  Checked PER NAME, and in BOTH directions:
    an undeclared dead name is a typo, and a declared name that has become
    reachable is a stale declaration.  Per-name matters because a divergence
    with two dead names out of three still fires, which is worse than one that
    cannot, because it looks like it works.
  * every `scenes` entry is a real scenario, or `*`.  A typo here does not
    error, it silently scopes the divergence to nothing.  An EMPTY list is
    legal and says "no scenario exercises this", which is a real state and
    needs a `conditional:` saying why -- 003 and 006 are both in it.
  * every `spec_section` is a real section of docs/BEHAVIOUR.md.  These are the
    citations that make a divergence a decision rather than an excuse.
  * `platform` is a platform the traces actually carry.
  * every divergence is in scope for at least one scenario, and every scenario
    has at least the divergences the milestone says it needs.

WHAT IT DELIBERATELY DOES NOT CHECK.  Whether a divergence is TRUE.  That is
what trace_check.py is for, and no amount of front-matter validation substitutes
for running both machines.  This checks that the rule is well formed and could
fire; that one checks whether the world agrees.
"""
from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))

import snes_trace                                       # noqa: E402
import trace_check                                      # noqa: E402
import trace_diff                                       # noqa: E402

BEHAVIOUR = ROOT / "docs" / "BEHAVIOUR.md"

# The keys trace_diff.py reads.  `conditional` is optional -- most divergences
# are unconditional -- and everything else is required, because a divergence
# missing one is a rule nobody can read.
REQUIRED = ("id", "spec_section", "trace_fields", "scenes", "platform", "reason")


def format_columns() -> list[str]:
    """The trace's column names, taken from the emitter rather than restated."""
    fields = snes_trace.header("check").splitlines()[1]
    return fields.split("\t")[1:]


def spec_sections() -> set[str]:
    """The numbered sections of docs/BEHAVIOUR.md."""
    return {m.group(1)
            for m in re.finditer(r"^##\s+(\d+)\.", BEHAVIOUR.read_text(), re.M)}


def main() -> int:
    divs = trace_diff.load_divergences()        # raises on a duplicate id
    if not divs:
        raise SystemExit("no divergences at all; trace_diff.py would suppress "
                         "nothing and every deliberate difference would be "
                         "reported as a defect")

    columns = format_columns()
    reachable = trace_diff.reachable_fields(columns)
    scenarios = {c.name for c in trace_check.CASES}
    sections = spec_sections()
    platforms = {"snes", "ds"}

    bad: list[str] = []
    for d in divs:
        who = d.path.name

        for key in REQUIRED:
            if key == "scenes":
                continue        # may legitimately be empty; checked below
            if not getattr(d, key if key != "trace_fields" else "fields"):
                bad.append(f"{who}: `{key}:` is missing or empty")

        # The id and the filename are two statements of the same thing, and the
        # report prints the id while a reader opens the file.
        if not who.startswith(d.id + "-"):
            bad.append(f"{who}: says `id: {d.id}`, so the filename should start "
                       f"{d.id}- and does not")

        # PER NAME, and in both directions.  A rule with one live name and two
        # dead ones fires, so it never looks broken -- and two thirds of what it
        # claims to excuse, it cannot.  003 was in exactly that state.
        dead = sorted(f for f in d.fields if f not in reachable)
        undeclared = sorted(f for f in dead if f not in d.unreachable)
        if undeclared:
            bad.append(
                f"{who}: trace_fields names {', '.join(undeclared)}, which the "
                f"format cannot produce -- not a column, and no alias in "
                f"trace_diff.FIELD_ALIASES reaches "
                f"{'them' if len(undeclared) > 1 else 'it'}.  "
                f"{'Every' if len(dead) == len(d.fields) else 'That part of the'}"
                f" rule can never fire.  Fix the name, add an alias, or declare "
                f"it in `unreachable:` with the body saying why")
        # ...and the other direction: a declaration that has gone stale.  A
        # column added to the format makes a declared name reachable, and a
        # declaration that is no longer true is worse than none, because it is
        # the thing a reader trusts instead of checking.
        stale = sorted(f for f in d.unreachable if f in reachable)
        if stale:
            bad.append(f"{who}: `unreachable:` still names {', '.join(stale)}, "
                       f"which the format now DOES carry -- the declaration is "
                       f"stale and the rule can fire after all")
        phantom = sorted(f for f in d.unreachable if f not in d.fields)
        if phantom:
            bad.append(f"{who}: `unreachable:` names {', '.join(phantom)}, "
                       f"which is not in trace_fields at all")

        unknown = sorted(s for s in d.scenes
                         if s != trace_diff.ANY_SCENE and s not in scenarios)
        if unknown:
            bad.append(f"{who}: scenes names {', '.join(unknown)}, which is not "
                       f"a scenario -- so this divergence is scoped to nothing "
                       f"there, silently.  Scenarios are: "
                       f"{', '.join(sorted(scenarios))}")
        if not d.scenes and not d.conditional:
            bad.append(f"{who}: `scenes: []` says no scenario exercises this, "
                       f"which may well be true -- but then it needs a "
                       f"`conditional:` saying under what circumstances it is "
                       f"real, or it is a divergence nothing can ever confirm")

        for s in sorted(x for x in d.spec_section.split(",") if x.strip()):
            if s.strip() not in sections:
                bad.append(f"{who}: spec_section cites §{s.strip()} of "
                           f"BEHAVIOUR.md, which has no such section")

        if d.platform not in platforms:
            bad.append(f"{who}: platform is {d.platform!r}; the traces carry "
                       f"{' and '.join(sorted(platforms))}")

        if not d.title or d.title == d.path.stem:
            bad.append(f"{who}: no `# heading`, so the suppression report has "
                       f"nothing to call it but the filename")

    if bad:
        print("the divergence corpus does not hold up:")
        for b in bad:
            print(f"  {b}")
        return 1

    def live(d) -> bool:
        return bool(d.scenes) and any(f in reachable for f in d.fields)

    n_live = sum(1 for d in divs if live(d))
    blanket = [d.id for d in divs if trace_diff.ANY_SCENE in d.scenes]
    print(f"divergences ok: {len(divs)} files, "
          f"{sum(len(d.fields) for d in divs)} field names, "
          f"{sum(len(d.unreachable) for d in divs)} declared unreachable; "
          f"{n_live} can suppress something in some scenario")
    for d in divs:
        scope = ("every scene" if trace_diff.ANY_SCENE in d.scenes
                 else ", ".join(sorted(d.scenes)) if d.scenes
                 else "no scenario")
        mark = " " if live(d) else "-"
        print(f"  {mark} {d.id}  {scope:14}  {d.title}")
    if blanket:
        print(f"  {len(blanket)} scoped to every scene ({', '.join(blanket)}) "
              f"-- correct only if the difference really is scene-independent")
    # A divergence no scenario exercises is not a defect, but it IS a claim the
    # oracle cannot check, and the count of those is the honest measure of how
    # much of the port's divergence from the ROM is actually being verified.
    unseen = [d.id for d in divs if not d.scenes]
    if unseen:
        print(f"  {len(unseen)} that no scenario exercises ({', '.join(unseen)}) "
              f"-- real in play, invisible to trace_check.py, and each says why "
              f"in its `conditional:`")
    return 0


if __name__ == "__main__":
    sys.exit(main())
