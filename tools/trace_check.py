#!/usr/bin/env python3
"""Run every scenario on both machines and check the traces agree.

    tools/trace_check.py

This is §M6's exit criterion, executable: "a trace from each platform for the
same scripted input, and trace_diff.py reporting either zero unexplained
divergences or a specific list with the frame and field of each."  It runs
tools/snes_trace.py against the frozen ROM, tools/ds_trace.py against the Tier-1
simulation, and compares them.

TWO FAMILIES OF SCENARIO, and the expectation differs because the question does:

  * THE ORACLE'S GEOMETRY -- the SNES's own collision map and cast positions.
    Content is held equal so that any difference at all is a difference in the
    CODE, and the bar is the strictest available: the two files must be
    IDENTICAL over their common frames, byte for byte, with nothing suppressed.
    A divergence file cannot rescue these.

  * AS THE DS SHIPS IT -- the DS's smaller disc, its own cast.  Differences are
    the point, and what is checked is that the FIRST UNEXPLAINED one is still
    the one recorded below.  Pinning the first rather than the count is
    deliberate: two simulations that have diverged keep diverging, so the count
    is noise and the first is the fact.

Runs in about a minute, nearly all of it the 65816 interpreter.  Not part
of Gate 0 for that reason -- run it whenever the simulation changes, and it is
the check that would catch a "helpful" repair of a reproduced SNES bug: fixing
Darkside's fist makes the darkside pair differ at frame 116, by name.
"""
from __future__ import annotations

import argparse
import re
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


class Case:
    def __init__(self, name, frames, script, poke=(), poke16=(), strict=True,
                 first=0, identical=True, first_divergence=None, note=""):
        self.name = name
        self.frames = frames            # frames the ORACLE runs
        self.script = script
        self.poke = list(poke)
        self.poke16 = list(poke16)
        self.strict = strict
        self.first = first              # the frame the DS scenario starts at
        self.identical = identical
        self.first_divergence = first_divergence
        self.note = note


CASES = [
    Case("station", 130, "traces/station.txt", poke=["txtState=0"],
         note="the disc, the opening line already dismissed"),
    Case("darkside", 300, "traces/idle.txt",
         poke=["sceneId=2", "deadFlag=2"], strict=False, first=15,
         note="the fist, the orbs, and the Shadow the slam leaves behind"),
    Case("armor", 400, "traces/idle.txt",
         poke=["sceneId=6", "townStage=5", "deadFlag=2"], strict=False, first=15,
         note="TownRestart re-raising the armour, then the drop, the landing "
              "freeze, the walk and the fist that connects"),
    Case("town", 900, "traces/town.txt",
         poke=["sceneId=7", "townStage=2", "deadFlag=2"],
         poke16=["rngState=0x1D57"], strict=False, first=15,
         note="the Second District's wave off the $1D57 seed: eight spots, one "
              "refusal, and the cap at five where the draws stop"),
    Case("night", 800, "traces/idle.txt",
         poke=["sceneId=3", "deadFlag=2", "30:sceneId=4", "30:deadFlag=2"],
         strict=False, first=30,
         note="the storm, the search, the LFSR they both draw from, and the "
              "ceiling at six where the draws stop"),
    Case("race", 700, "traces/idle.txt",
         poke=["sceneId=3", "deadFlag=2", "30:questState=6", "30:rikuWp=0"],
         strict=False, first=30,
         note="Riku's whole waypoint walk, every frame of it"),
    # ...and the port as it actually ships, where the differences are the point.
    Case("dive", 150, "traces/dive.txt", identical=False,
         first_divergence=(21, "diveStage"),
         note="divergence 005 reaches the stage bytes: SC_PAGE pages on the DS, "
              "so the intro takes six presses and not two, and every beat that "
              "waits on it lands later"),
]


def run(cmd: list[str], what: str) -> str:
    r = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True)
    if r.returncode != 0:
        sys.stderr.write(r.stdout + r.stderr)
        raise SystemExit(f"{what} failed")
    return r.stdout


def body(path: Path, first: int) -> list[str]:
    """The data lines from `first` on, headers dropped.

    The headers carry the platform and the revision and are meant to differ; the
    columns are checked by ds_trace.py --check-format and again by trace_diff.py.
    """
    out = []
    for line in path.read_text().splitlines():
        if line.startswith("#") or not line.strip():
            continue
        if int(line.split("\t", 1)[0]) >= first:
            out.append(line)
    return out


def check(case: Case, outdir: Path, verbose: bool) -> bool:
    snes = outdir / f"snes-{case.name}.trace"
    ds = outdir / f"ds-{case.name}.trace"

    cmd = [sys.executable, "tools/snes_trace.py", "--frames", str(case.frames),
           "--input", case.script, "-o", str(snes)]
    for p in case.poke:
        cmd += ["--poke", p]
    for p in case.poke16:
        cmd += ["--poke16", p]
    if not case.strict:
        cmd.append("--no-strict")
    run(cmd, f"the oracle for {case.name}")

    run([sys.executable, "tools/ds_trace.py", "--scenario", case.name,
         "--frames", str(case.frames - case.first), "--input", case.script,
         "-o", str(ds)], f"the DS emitter for {case.name}")

    diff = subprocess.run(
        [sys.executable, "tools/trace_diff.py", str(snes), str(ds)]
        + (["--strict"] if case.identical else []),
        cwd=ROOT, capture_output=True, text=True)
    if verbose:
        print(diff.stdout)

    if case.identical:
        a, b = body(snes, case.first), body(ds, case.first)
        if a != b:
            print(f"  {case.name}: FAILED -- the two traces are not identical")
            print(diff.stdout.strip())
            return False
        if diff.returncode != 0:
            print(f"  {case.name}: FAILED -- trace_diff disagrees with a byte "
                  f"comparison, which means the differ is wrong")
            print(diff.stdout.strip())
            return False
        print(f"  {case.name}: {len(a)} frames, identical from frame "
              f"{case.first} -- {case.note}")
        return True

    m = re.search(r"FIRST UNEXPLAINED DIVERGENCE: frame (\d+), field (\w+)",
                  diff.stdout)
    got = (int(m.group(1)), m.group(2)) if m else None
    if got != case.first_divergence:
        print(f"  {case.name}: FAILED -- the first unexplained divergence is "
              f"{got}, and {case.first_divergence} was recorded")
        print(diff.stdout.strip())
        return False
    print(f"  {case.name}: first unexplained divergence still frame "
          f"{got[0]}, {got[1]} -- {case.note}")
    return True


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("-o", "--outdir", type=Path, default=None,
                    help="keep the traces here instead of a temporary directory")
    ap.add_argument("--only", default=None, help="one scenario by name")
    ap.add_argument("-v", "--verbose", action="store_true")
    args = ap.parse_args(argv)

    cases = [c for c in CASES if args.only is None or c.name == args.only]
    if not cases:
        raise SystemExit(f"no scenario called {args.only!r}")

    tmp = None
    if args.outdir:
        args.outdir.mkdir(parents=True, exist_ok=True)
        outdir = args.outdir
    else:
        tmp = tempfile.TemporaryDirectory()
        outdir = Path(tmp.name)

    print(f"{len(cases)} scenario(s), both machines:")
    ok = True
    for c in cases:
        ok &= check(c, outdir, args.verbose)
    if tmp:
        tmp.cleanup()
    print("all agree." if ok else "SOMETHING DISAGREES.")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
