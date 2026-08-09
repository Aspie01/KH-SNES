#!/usr/bin/env python3
"""Run the DS host simulation and emit a trace in the oracle's format.

    tools/ds_trace.py --scenario station --frames 130 \
        --input traces/station.txt -o out/ds.trace

The counterpart to tools/snes_trace.py.  That one runs the frozen ROM on a
headless 65816; this one runs the Tier-1 C++ simulation.  tools/trace_diff.py
compares the two.

WHY A WRAPPER AND NOT JUST THE BINARY.  Three things that are awkward in C++ and
trivial here, and one of them is load-bearing:

  * IT BUILDS FIRST, so a trace is never taken from a stale binary.  That is the
    failure mode with the longest debugging tail: a diff that reports a
    divergence fixed twenty minutes ago.

  * IT STAMPS THE GIT REVISION, using the SAME function tools/snes_trace.py uses.
    The brief asks for "the git revision of the run -- not a hard-coded
    constant", and two implementations of that would eventually disagree about
    what counts as dirty and put `3a18f45` beside `3a18f45-dirty` for the same
    tree.

  * IT CHECKS THE HEADER, and this is the load-bearing one.  trace_diff.py
    matches columns BY NAME and skips a name that is missing from one side, so
    an emitter that renamed a column would quietly stop having it compared.  The
    two `#fields` lines are written in different languages in different files;
    this compares them byte for byte on every run, which is the only place that
    check can live.

--check-format on its own does that last thing and nothing else, which is what
makes it usable from a test.
"""
from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from snes_trace import git_revision, header                # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
MAKEFILE = ROOT / "platform" / "ds" / "host" / "Makefile.host"
BINARY = ROOT / "platform" / "ds" / "host" / "build" / "dstrace"


def build() -> None:
    r = subprocess.run(["make", "-f", str(MAKEFILE), "trace"], cwd=ROOT,
                       capture_output=True, text=True)
    if r.returncode != 0:
        sys.stderr.write(r.stdout + r.stderr)
        raise SystemExit("the DS host tier does not build")


def emitted_header(rev: str) -> str:
    """The two header lines the binary writes, and nothing else.

    Taken by running the shortest possible trace rather than by parsing the C++,
    because what has to match is what it EMITS.
    """
    r = subprocess.run([str(BINARY), "--scenario", "station", "--frames", "1",
                        "--rev", rev], cwd=ROOT, capture_output=True, text=True)
    if r.returncode != 0:
        sys.stderr.write(r.stderr)
        raise SystemExit("the emitter would not run")
    return "".join(r.stdout.splitlines(keepends=True)[:2])


def check_format(rev: str) -> int:
    ours = emitted_header(rev)
    theirs = header("ds").replace(f"rev={git_revision()}", f"rev={rev}")
    if ours == theirs:
        print("the two emitters agree on the header, byte for byte")
        return 0
    print("THE TWO EMITTERS DISAGREE ABOUT THE FORMAT.  trace_diff.py matches\n"
          "columns by name, so whichever ones differ would stop being compared\n"
          "rather than being reported.\n", file=sys.stderr)
    for name, text in (("snes_trace.py", theirs), ("dstrace", ours)):
        for line in text.splitlines():
            print(f"  {name:14} {line}", file=sys.stderr)
    return 1


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--scenario", default=None,
                    help="which situation to simulate; --list says what there is")
    ap.add_argument("--frames", type=int, default=150)
    ap.add_argument("--input", type=Path, default=None,
                    help="button script, `keys:frames` a line -- THE SAME FILE "
                         "tools/snes_trace.py takes, because the DS pad's bits "
                         "are the SNES's own")
    ap.add_argument("-o", "--out", type=Path, default=None)
    ap.add_argument("--list", action="store_true",
                    help="the scenarios, and the oracle run each one pairs with")
    ap.add_argument("--check-format", action="store_true",
                    help="compare the two emitters' headers and stop")
    ap.add_argument("--no-build", action="store_true")
    args = ap.parse_args(argv)

    if not args.no_build:
        build()
    if not BINARY.exists():
        raise SystemExit(f"{BINARY} is not built")

    if args.list:
        return subprocess.run([str(BINARY), "--list"], cwd=ROOT).returncode

    rev = git_revision()
    if args.check_format:
        return check_format(rev)
    # Never emit a trace whose columns are not the oracle's.  It would compare
    # clean on the columns that survived and say nothing about the rest.
    if check_format(rev) != 0:
        return 1

    if not args.scenario:
        raise SystemExit("--scenario is required; --list says what there is")

    cmd = [str(BINARY), "--scenario", args.scenario,
           "--frames", str(args.frames), "--rev", rev]
    if args.input:
        cmd += ["--input", str(args.input)]
    if args.out:
        cmd += ["-o", str(args.out)]
    return subprocess.run(cmd, cwd=ROOT).returncode


if __name__ == "__main__":
    sys.exit(main())
