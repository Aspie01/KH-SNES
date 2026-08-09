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

    THE FOUR CAMERA COLUMNS ARE THE ONE EXCEPTION, and they are not exempt --
    they are checked HARDER.  §M3 mandates two divergences that make camY and
    bgVOfs differ by construction: the DS centres on playerY - 96 where the SNES
    centres on -112, and it does not reproduce the (camY - 1) & 0x3FF the SNES
    wrote for a PPU quirk.  So those two columns are lifted out of the byte
    comparison and checked against the ARITHMETIC instead, which is a statement
    about the divergence rather than a hole in the check.  camX and bgHOfs are
    not divergent and stay in the byte comparison, where they belong.

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
                 first=0, identical=True, first_divergence=None, note="",
                 snes_cam=(0, 32), ds_cam=(0, 64)):
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
        # The vertical camera bounds LoadScene set, on each machine.  A station
        # is pinned; everything else scrolls to the edges of a 32x16 map, and
        # the DS's floor is 32 lines lower because its screen is 32 shorter.
        self.snes_cam = snes_cam
        self.ds_cam = ds_cam


CASES = [
    Case("station", 130, "traces/station.txt", poke=["txtState=0"],
         snes_cam=(16, 16), ds_cam=(32, 32),
         note="the disc, the opening line already dismissed"),
    Case("darkside", 300, "traces/idle.txt",
         snes_cam=(16, 16), ds_cam=(32, 32),
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
    # strict=True and identical=True are BOTH defaults here and both deliberate.
    # The route never calls RestartScene, so no frame of it does a scene load and
    # nothing needs --no-strict; the oracle's busiest ordinary frame is #128 at
    # 38.5% of a frame.  And identical=True is not merely the stronger setting,
    # it is the only one under which this scenario measures anything: divergence
    # 002 has `scenes: [*]` and `trace_fields: [actorSlot, actorCount]`, which
    # FIELD_ALIASES maps onto actorIdx and nactors -- so under identical=False the
    # differ would suppress exactly the two columns the fall exists to pin, the
    # mote slot recycling and the pool occupancy, and the "first unexplained
    # divergence" pin would be structurally blind to them.
    Case("fall", 200, "traces/idle.txt",
         poke=["txtState=0", "2:diveStage=9"],
         first=2, snes_cam=(16, 16), ds_cam=(32, 32),
         note="the drop between the stations: 171 frames of DIVE_FALL, 43 motes "
              "off the eight-way spread, and the eleven slots they recycle "
              "through -- and idle.txt presses A twice inside the fall, so "
              "\"no input\" is measured and not assumed"),
    # ...and the port as it actually ships, where the differences are the point.
    Case("dive", 150, "traces/dive.txt", identical=False,
         snes_cam=(16, 16), ds_cam=(32, 32),
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


# The two columns §M3 mandates a divergence in, by name.  Lifted out of the byte
# comparison and checked as a relation instead -- see the module docstring.
DIVERGENT_CAMERA = ("camY", "bgVOfs")


def columns(path: Path) -> list[str]:
    for line in path.read_text().splitlines():
        if line.startswith("#fields"):
            return line.split("\t")[1:]
    raise SystemExit(f"{path}: no #fields header")


def body(path: Path, first: int, drop: tuple[str, ...] = ()) -> list[str]:
    """The data lines from `first` on, headers dropped, `drop` columns removed.

    The headers carry the platform and the revision and are meant to differ; the
    columns are checked by ds_trace.py --check-format and again by trace_diff.py.
    """
    cols = columns(path)
    idx = [i for i, c in enumerate(cols) if c in drop]
    out = []
    for line in path.read_text().splitlines():
        if line.startswith("#") or not line.strip():
            continue
        cells = line.split("\t")
        if int(cells[0]) < first:
            continue
        out.append("\t".join(c for i, c in enumerate(cells) if i not in idx))
    return out


def camera(path: Path, first: int) -> dict[int, dict[str, int]]:
    cols = columns(path)
    want = {c: i for i, c in enumerate(cols)
            if c in ("camX", "camY", "bgHOfs", "bgVOfs")}
    out: dict[int, dict[str, int]] = {}
    for line in path.read_text().splitlines():
        if line.startswith("#") or not line.strip():
            continue
        cells = line.split("\t")
        f = int(cells[0])
        if f >= first:
            out[f] = {c: int(cells[i]) for c, i in want.items()}
    return out


def player_y(path: Path, first: int) -> dict[int, int]:
    cols = columns(path)
    i = cols.index("py")
    out = {}
    for line in path.read_text().splitlines():
        if line.startswith("#") or not line.strip():
            continue
        cells = line.split("\t")
        f = int(cells[0])
        if f >= first:
            out[f] = int(cells[i])
    return out


def clamp(v: int, lo: int, hi: int) -> int:
    return lo if v < lo else (hi if v > hi else v)


def check_camera(case, snes: Path, ds: Path) -> list[str]:
    """camY and bgVOfs, against their DEFINITIONS rather than against each other.

    A range check would have passed a DS that centred on -112 like the SNES,
    because on an unclamped frame that produces the same number and a difference
    of zero is inside any tolerance.  So both sides are computed FROM py -- which
    is a column -- and compared exactly:

        camY   = clamp(playerY_px - SCREEN_H/2, loY, hiY)
        bgVOfs = camY on the DS, and (camY - 1) & 0x3FF on the SNES

    SCREEN_H/2 is 112 there and 96 here, and hiY is 32 lines larger here.  Both
    of those are divergence 001, and this is the first thing in the project to
    check either of them rather than assert them.
    """
    a, b = camera(snes, case.first), camera(ds, case.first)
    py = player_y(snes, case.first)
    bad = []
    for f in sorted(set(a) & set(b) & set(py)):
        yp = py[f] >> 4                             # Q12.4 to whole pixels
        want_s = clamp(yp - 112, case.snes_cam[0], case.snes_cam[1])
        want_d = clamp(yp - 96, case.ds_cam[0], case.ds_cam[1])
        if a[f]["camY"] != want_s:
            bad.append(f"frame {f}: the SNES's camY is {a[f]['camY']} and "
                       f"clamp(py/16 - 112, {case.snes_cam[0]}, "
                       f"{case.snes_cam[1]}) is {want_s} -- the oracle is not "
                       f"what this check assumes it is")
        if b[f]["camY"] != want_d:
            bad.append(f"frame {f}: the DS's camY is {b[f]['camY']} and "
                       f"clamp(py/16 - 96, {case.ds_cam[0]}, {case.ds_cam[1]}) "
                       f"is {want_d}")
        if b[f]["bgVOfs"] != b[f]["camY"]:
            bad.append(f"frame {f}: the DS's bgVOfs is {b[f]['bgVOfs']} and its "
                       f"camY is {b[f]['camY']}; the DS has no PPU bias to add")
        if a[f]["bgVOfs"] != (a[f]["camY"] - 1) & 0x3FF:
            bad.append(f"frame {f}: the SNES's bgVOfs is not (camY - 1) & 0x3FF")
        if bad:
            break
    return bad


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

    # --scene, so that only the divergences whose front matter says they apply
    # HERE can suppress anything.  Without it the differ loads all six, which is
    # how divergence 004 -- "a station is drawn smaller" -- came to excuse every
    # actor position in every scenario, 1604 of them in `dive` alone.
    diff = subprocess.run(
        [sys.executable, "tools/trace_diff.py", "--scene", case.name,
         str(snes), str(ds)],
        cwd=ROOT, capture_output=True, text=True)
    if verbose:
        print(diff.stdout)

    if case.identical:
        cam = check_camera(case, snes, ds)
        if cam:
            print(f"  {case.name}: FAILED -- the camera does not hold the "
                  f"relation divergence 001 describes")
            for c in cam:
                print(f"    {c}")
            return False
        a, b = (body(snes, case.first, DIVERGENT_CAMERA),
                body(ds, case.first, DIVERGENT_CAMERA))
        if a != b:
            print(f"  {case.name}: FAILED -- the two traces are not identical")
            print(diff.stdout.strip())
            return False
        # ...and the differ, which was run above, is required to have excused
        # everything -- which is divergence 001 doing the job it was written for
        # and never once did, because until v2 of the format neither of its
        # fields was a column.  (This used to spawn the differ a SECOND time
        # with byte-identical arguments and read the second one's exit code.
        # Two runs, one answer.)
        if diff.returncode != 0:
            print(f"  {case.name}: FAILED -- something outside the camera is "
                  f"unexplained")
            print(diff.stdout.strip())
            return False
        print(f"  {case.name}: {len(a)} frames, identical from frame "
              f"{case.first} (camera per divergence 001) -- {case.note}")
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
