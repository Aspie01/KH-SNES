#!/usr/bin/env python3
"""Compare two per-frame traces and report only the divergence nobody explained.

    tools/trace_diff.py out/snes.trace out/ds.trace

The two traces come from tools/snes_trace.py and from the DS host tier running
the same scripted input.  Most of the work here is not comparing them -- that is
four lines -- it is knowing which differences are ALREADY ACCOUNTED FOR, because
a diff that reports the deliberate divergences alongside the real ones is a diff
nobody reads twice.

So docs/behaviour/divergences/ is the authority.  Each file carries YAML front
matter with a `trace_fields` list, and a field named there is expected to differ:
the DS screen is 32 lines taller in world terms (001), its actor pool is a
different size (002), the night is denser (003), a station is drawn smaller and
its cast moved inward (004), SC_PAGE was fixed rather than reproduced (005).
Those are decisions, recorded when they were made, and this tool reads them
rather than being told them again.

WHAT IT WILL NOT DO IS HIDE THINGS QUIETLY.  Every suppressed difference is
counted and summarised by which divergence excused it, so a divergence that is
excusing far more than it should -- the sign that it has become a blanket -- is
visible.  `--strict` refuses to suppress anything at all.

THE FIRST DIVERGENCE IS THE ONLY ONE THAT MATTERS.  Once two simulations differ
they keep differing, so the tail is noise.  The report leads with the earliest
frame and field, and everything after it is a summary.
"""
from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DIVERGENCES = ROOT / "docs" / "behaviour" / "divergences"

# A trace column's name maps to the vocabulary the divergence files use, which
# is deliberately the SPEC's vocabulary and not this format's.  Keeping the two
# apart means a divergence written before this tool existed still applies.
FIELD_ALIASES = {
    "px": {"actorX"}, "py": {"actorY"}, "pz": {"actorZ"},
    "pstate": {"actorState"}, "ptimer": {"actorTimer"}, "php": {"actorHP"},
    "pdir": {"actorDir"},
    "nactors": {"actorCount"},
    "actorX": {"actorX", "collision"}, "actorY": {"actorY", "collision"},
    # The comparison names this column actorIdx, because it is the `idx` part of
    # an actor field; the divergences call the same thing a SLOT, because that is
    # what 002 is about.  Without this line 002 cannot excuse the one difference
    # it exists to excuse.
    "actorIdx": {"actorSlot"},
    "actorSlot": {"actorSlot"},
    "nightStage": {"nightTimer"},
}


class Divergence:
    def __init__(self, path: Path):
        self.path = path
        text = path.read_text()
        m = re.match(r"^---\n(.*?)\n---\n", text, re.S)
        if not m:
            raise SystemExit(f"{path.name}: no front matter, so nothing here "
                             f"knows what it excuses")
        fm = m.group(1)
        self.id = self._one(fm, "id")
        self.platform = self._one(fm, "platform")
        self.reason = self._one(fm, "reason")
        self.conditional = self._one(fm, "conditional", required=False)
        fields = re.search(r"^trace_fields:\s*\[(.*?)\]", fm, re.M)
        self.fields = {f.strip() for f in fields.group(1).split(",")} if fields else set()
        title = re.search(r"^#\s+(.*)$", text, re.M)
        self.title = title.group(1).strip() if title else path.stem

    @staticmethod
    def _one(fm: str, key: str, required: bool = True) -> str:
        m = re.search(rf"^{key}:\s*(.*)$", fm, re.M)
        if not m:
            if required:
                raise SystemExit(f"a divergence is missing its `{key}:`")
            return ""
        return m.group(1).strip().strip('"')

    def excuses(self, column: str) -> bool:
        if column in self.fields:
            return True
        return bool(FIELD_ALIASES.get(column, set()) & self.fields)


def load_divergences() -> list[Divergence]:
    if not DIVERGENCES.is_dir():
        raise SystemExit(f"{DIVERGENCES} is missing; this tool needs it to know "
                         f"which differences were decided on purpose")
    return [Divergence(p) for p in sorted(DIVERGENCES.glob("*.md"))]


class Trace:
    def __init__(self, path: Path):
        self.path = path
        self.meta: dict[str, str] = {}
        self.columns: list[str] = []
        self.frames: dict[int, dict[str, str]] = {}
        self.actors: dict[int, list[str]] = {}
        for raw in path.read_text().splitlines():
            if raw.startswith("#kh-trace"):
                for part in raw.split("\t")[1:]:
                    if re.fullmatch(r"v\d+", part):
                        self.meta["version"] = part[1:]
                        continue
                    k, _, v = part.partition("=")
                    self.meta[k] = v or k
                continue
            if raw.startswith("#fields"):
                self.columns = raw.split("\t")[1:]
                continue
            if raw.startswith("#") or not raw.strip():
                continue
            cells = raw.split("\t")
            # Everything up to and including nactors is a fixed column; the rest
            # are the live actors, however many there happen to be.
            n = self.columns.index("nactors") + 1
            row = dict(zip(self.columns[:n], cells[:n]))
            f = int(row["frame"])
            self.frames[f] = row
            self.actors[f] = cells[n:]
        if not self.columns:
            raise SystemExit(f"{path}: no #fields header, so the columns are "
                             f"unnamed and nothing can be compared")

    @property
    def platform(self) -> str:
        return self.meta.get("platform", "?")

    @property
    def rev(self) -> str:
        return self.meta.get("rev", "?")

    @property
    def version(self) -> str:
        return self.meta.get("version", "?")


def check_comparable(a: Trace, b: Trace) -> None:
    """Refuse a pair whose columns do not line up.

    THIS IS NOT PEDANTRY, it is the one way this tool could report success while
    comparing nothing.  Columns are matched BY NAME, and compare() skips a name
    that is missing from either side -- so an emitter that renamed `px` to `pX`
    would silently stop having its player position checked, and the run would
    print "no unexplained divergence" with a straight face.  A format is only a
    contract if somebody checks it.
    """
    if a.version != b.version:
        raise SystemExit(f"{a.path.name} is trace v{a.version} and "
                         f"{b.path.name} is v{b.version}.  The version changes "
                         f"when a column changes meaning, so these two do not "
                         f"describe the same thing.")
    if a.columns != b.columns:
        only_a = [c for c in a.columns if c not in b.columns]
        only_b = [c for c in b.columns if c not in a.columns]
        detail = ""
        if only_a:
            detail += f"\n  only in {a.platform}: " + ", ".join(only_a)
        if only_b:
            detail += f"\n  only in {b.platform}: " + ", ".join(only_b)
        if not detail:
            detail = "\n  same names, different order"
        raise SystemExit(f"the two traces do not have the same columns, so a "
                         f"comparison would silently skip the ones that differ."
                         f"{detail}")


ACTOR_PARTS = ("idx", "type", "x", "y", "state", "timer", "hp")


def compare(a: Trace, b: Trace, divs: list[Divergence], strict: bool):
    """Yields (frame, field, left, right, excuse-or-None) in frame order."""
    common = sorted(set(a.frames) & set(b.frames))
    for f in common:
        ra, rb = a.frames[f], b.frames[f]
        for col in a.columns:
            if col in ("frame", "actor=idx/type/x/y/state/timer/hp..."):
                continue
            if col not in ra or col not in rb:
                continue
            if ra[col] != rb[col]:
                yield f, col, ra[col], rb[col], _excuse(col, divs, strict)
        # Actors are positional within the frame, and a difference in HOW MANY
        # is reported once rather than as a landslide of missing slots.
        la, lb = a.actors[f], b.actors[f]
        for i, (x, y) in enumerate(zip(la, lb)):
            if x == y:
                continue
            for part, u, v in zip(ACTOR_PARTS, x.split("/"), y.split("/")):
                if u != v:
                    name = "actor" + part.capitalize()
                    yield f, f"{name}[{i}]", u, v, _excuse(name, divs, strict)


def _excuse(column: str, divs: list[Divergence], strict: bool):
    if strict:
        return None
    base = re.sub(r"\[\d+\]$", "", column)
    for d in divs:
        if d.excuses(base):
            return d
    return None


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("left", type=Path)
    ap.add_argument("right", type=Path)
    ap.add_argument("--strict", action="store_true",
                    help="report every difference, excused or not")
    ap.add_argument("--max", type=int, default=20,
                    help="how many unexplained differences to print in full")
    args = ap.parse_args(argv)

    a, b = Trace(args.left), Trace(args.right)
    check_comparable(a, b)
    divs = load_divergences()
    print(f"{a.platform} @ {a.rev}   vs   {b.platform} @ {b.rev}")
    print(f"{len(divs)} divergences on file, covering "
          f"{sum(len(d.fields) for d in divs)} field names")

    only_a = sorted(set(a.frames) - set(b.frames))
    only_b = sorted(set(b.frames) - set(a.frames))
    if only_a or only_b:
        print(f"frame coverage differs: {len(only_a)} only in {a.platform}, "
              f"{len(only_b)} only in {b.platform} -- comparing the overlap")

    unexplained: list[tuple] = []
    excused: dict[str, int] = {}
    first_field_at: dict[str, int] = {}
    for f, col, x, y, why in compare(a, b, divs, args.strict):
        if why is None:
            unexplained.append((f, col, x, y))
            first_field_at.setdefault(col, f)
        else:
            excused[why.id] = excused.get(why.id, 0) + 1

    if excused:
        print("\nexplained by a recorded divergence:")
        for d in divs:
            if d.id in excused:
                print(f"  {d.id}  {excused[d.id]:6} difference(s)  {d.title}")

    if not unexplained:
        print("\nno unexplained divergence.")
        return 0

    f0, c0, x0, y0 = unexplained[0]
    print(f"\nFIRST UNEXPLAINED DIVERGENCE: frame {f0}, field {c0}: "
          f"{a.platform}={x0} {b.platform}={y0}")
    print("Everything after this is downstream of it -- two simulations that "
          "have diverged keep diverging -- so fix this one first.\n")
    for f, col, x, y in unexplained[:args.max]:
        print(f"  frame {f:6}  {col:20} {a.platform}={x:12} {b.platform}={y}")
    if len(unexplained) > args.max:
        print(f"  ... and {len(unexplained) - args.max} more")
    print(f"\n{len(unexplained)} unexplained difference(s) across "
          f"{len(first_field_at)} field(s); earliest per field:")
    for col, f in sorted(first_field_at.items(), key=lambda kv: kv[1]):
        print(f"  frame {f:6}  {col}")
    return 1


if __name__ == "__main__":
    sys.exit(main())
