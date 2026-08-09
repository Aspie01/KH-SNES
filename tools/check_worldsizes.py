#!/usr/bin/env python3
"""Every measured figure in docs/WORLD_SIZES.md, recomputed from what owns it.

    python3 tools/check_worldsizes.py

WHY THIS EXISTS.  `docs/WORLD_SIZES.md` is the one document in this tree that is
mostly arithmetic: walkable counts, unique-character counts, map extents, cast
sizes, the radius the station disc had to shrink to.  Every one of those numbers
is printed by something -- `tools/build_assets.py` prints the walkable and unique
counts and the cast breakdown on every run, `tools/check_map.py` prints the worst
camera window per map -- and every one of them was transcribed into the document
by hand.  Nothing compared the two afterwards.  The file's own closing section
says so, and says it was measured rather than assumed: turning one cobble into a
crate in `assets/ds/town3.txt` takes that district from 877 walkable tiles to
876, and `build_assets.py`, `check_map.py`, `check_modes.py`,
`check_divergences.py`, `build_doors.py --check` and the whole host suite all
still exit zero with the document claiming 877.

That is this project's recurring defect in its purest form: a stated fact with no
consumer.  It is worse here than usual because the numbers were JUST corrected --
717 became 670, 912/840/895 became 891/822/877, 242 became 247 -- so the file is
on its second transcription and its first day of being right, guarded by exactly
the same nothing that let the first transcription rot.

WHAT IT CHECKS, and what each class of figure is guarded against:

  * MAP EXTENTS.  The size table, and every sentence that repeats a shape.  Taken
    from the grids themselves via `build_assets.load_grid`, with the pixel and
    character columns derived from `TILE`, so a map that is widened and a table
    that is not disagree here rather than in somebody's head six months later.
  * WALKABLE COUNTS.  Summed over `build_assets.TERRAIN` exactly as
    `build_world` does it.  This is the number the reviewer moved with a
    one-character edit, and it is the reason this tool exists.
  * UNIQUE CHARACTERS.  Recomputed through `dedupe_tilemap_ds`, which is the
    only thing that knows how many distinct 8x8 characters a painted map folds
    to.  A recoloured tile changes this and changes nothing else that is visible.
  * THE CAST TABLE.  Props, placed, spots and peak, per scene, against
    `derive_props` and `load_cast` -- the two halves whose agreement the whole
    "a T with no palm on it is an invisible wall" argument rests on.
  * THE DISTRICT DOOR ROW.  The six door tiles, their exact coordinates, the
    `e` columns beside them, and `DOOR_ROW` itself.  The document had to correct
    a claim about this row once already ("every district has exactly one way in"
    was never true of these maps), so the corrected version is the one that most
    deserves a consumer.
  * DERIVED AND HISTORICAL ARITHMETIC.  717 - 47 = 670 and 912/840/895 -> 891/
    822/877 name numbers no longer in the tree -- the maps before the props went
    into them.  Those cannot be recomputed, but the arithmetic tying them to the
    numbers that CAN be is checked, so an edit that moves 670 and forgets 717
    fails here.
  * CONSTANTS QUOTED WITH THEIR VALUES.  Any `NAME = 123` in backticks is looked
    up in game.inc, the DS headers and gen/assets.h and compared by value, which
    is `tools/check_constants.py`'s argument applied to prose.
  * SOURCE CITATIONS.  Every `file:line` resolves to a file that exists with a
    line that exists, including the bare `:248` continuations that hang off the
    previously named file.

WHAT IT DELIBERATELY DOES NOT CHECK.  Whether a citation's line SAYS what the
sentence beside it claims.  That is a reading, not a measurement, and pretending
otherwise would be the very defect the house style calls the worst available.
What this catches is the mechanical half: a file renamed or deleted out from
under a citation, and a line number past the end of its file.  The one exception
is `constants.h:477`, which the document cites AS the definition of `DOOR_ROW`;
that one is opened and required to contain the name, because there the line
number and the claim are the same statement.

AND THE THING THAT MAKES IT A CHECK RATHER THAN A SAMPLE.  After every figure
above has been compared, a CENSUS sweeps every digit in the document and demands
that each one was either consumed by a check or is named in EXCUSED below WITH A
REASON.  A number nobody can source is not skipped silently -- it is reported, at
the line it appears on, as "stated and unchecked", which is the state this whole
document was in before this file existed.  An excuse that no longer matches
anything is itself an error, on `check_constants.py`'s argument: a stale
exclusion list is how a checker quietly stops checking.

Every ANCHOR is required to match the document EXACTLY ONCE.  A reworded sentence
therefore fails loudly instead of silently checking nothing, which is the failure
mode a document checker is otherwise guaranteed to end up in.
"""
from __future__ import annotations

import argparse
import io
import re
import sys
from contextlib import redirect_stdout
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))

import build_assets as ba                                # noqa: E402
import build_doors                                       # noqa: E402
import check_constants                                   # noqa: E402
import check_map                                         # noqa: E402
import trace_check                                       # noqa: E402

DOC = ROOT / "docs" / "WORLD_SIZES.md"
SNES_SRC = ROOT / "platform" / "snes" / "src"
DS_INC = ROOT / "platform" / "ds" / "include"
DIVERGENCES = ROOT / "docs" / "behaviour" / "divergences"

# Where a path in a citation might live.  The document cites the DS port the way
# a reader of it would -- `include/grid.h`, `source/grid.cpp`,
# `host/trace_main.cpp`, all relative to `platform/ds/` -- and the SNES with no
# directory at all, `town.s`.  Ordered, and the first hit wins; a path that
# matches nowhere and carries a line number is reported rather than skipped.
CITE_DIRS = ("", "platform/ds/include/", "platform/ds/", "platform/ds/host/",
             "platform/ds/source/", "platform/snes/src/", "tools/", "docs/",
             "assets/", "assets/ds/")

# The document spells small counts as words in prose and as digits in tables.
# Both are claims; only the digits are visible to the census, so the worded ones
# are checked here and simply do not need excusing.
NUMWORD = {"one": 1, "two": 2, "three": 3, "four": 4, "five": 5, "six": 6,
           "seven": 7, "eight": 8, "nine": 9, "ten": 10}

# The tile codes that ARE a prop, on both machines: the map puts the character
# down and the pipeline derives the actor.  Used to split the SNES's
# hand-written spawn tables into the document's "props" and "placed" columns.
SNES_PROP_ACT = ("ACT_PALM", "ACT_PALMC", "ACT_ROCKBIG", "ACT_ROCK", "ACT_LAMP")

# ---------------------------------------------------------------------------
# Numbers the document states that this tool does not source, EACH WITH A
# REASON.  This list is the honest half of the report: its SIZE is printed on
# every successful run, exactly the way check_constants.py prints "24 excused
# with a reason", and `-v` prints the reasons -- so "unchecked" is a visible
# quantity rather than an absence, and nothing is quietly ignored.
#
# An entry whose pattern matches nothing is an ERROR.  The document is edited by
# people; an excuse for a sentence that no longer exists is a checker pretending
# to have looked at something.
#
# AND THAT MAKES EXCUSED WEAKER THAN IT LOOKS, IN THE RIGHT DIRECTION.  Every
# excuse for a figure spells the figure out -- "4 MB of RAM", "the engine's 128",
# "208 cases, 113086 checks", the four SNES cast rows cell by cell -- rather than
# matching a `\d+`.  So an excused number cannot be edited silently either: change
# it and the excuse matches nothing, and the run fails saying so.  "Excused" here
# means "this tool cannot recompute it", not "nobody is watching it".  The only
# patterns that match a digit CLASS are the ones where the digit is not a
# quantity at all: a milestone number, a list marker, a scene called town3.
# ---------------------------------------------------------------------------
EXCUSED: tuple[tuple[str, str], ...] = (
    (r"§M\d|\bM5\b|\bR[1-9] to R[1-9]\b",
     "a milestone or rule number in another document, not a measurement"),
    (r"\bGate 0\b",
     "the name of the gate, not a quantity"),
    (r"\bpython3\b",
     "the interpreter in a command line"),
    (r"^\d+\. \*\*", "a numbered list item in this document"),
    (r"version of \(2\) is",
     "a back-reference to this section's own numbered list"),
    (r"roadmap item 2",
     "an item number in the porting brief's roadmap"),
    (r"\b[23]D\b",
     "a rendering dimensionality -- 2D and 3D are words here"),
    (r"\b(?:town|station|fragment|island|night|dive|day|BG|Town|Dive)\d\b"
     r"|\bstation\d(?:cast)?\.(?:txt|bin)\b|\b\d{3}-[a-z-]+\.md\b",
     "an identifier with a digit in it -- a scene, a background or a filename"),
    (r"4 MB of RAM",
     "the DS's main RAM.  A hardware fact; nothing in this tree owns it"),
    (r"3\.58 MHz",
     "the SNES's CPU clock.  A hardware fact; nothing in this tree owns it"),
    (r"about 15% of it",
     "an approximation with no stated denominator.  A 17x13-tile window is 221 "
     "of the expanded island's 2048 tiles, which is 10.8%, so the sentence is "
     "an impression of what a screen shows and not a measurement of it"),
    (r"about 35 of 128 entries",
     "OAM holds 128 entries on both machines, which is hardware; 'about 35' is "
     "a hand estimate of a full 32-actor pool once Sora's larger cel is counted"),
    (r"the engine's 128",
     "the DS's 128 OAM entries.  A hardware fact; the arithmetic around it -- "
     "128 - 96 = 32 left for the transients -- IS checked"),
    (r"a 64×64 boss",
     "the Guard Armor, which is four 32x32 cels in a square -- so 64 px is the "
     "cel size doubled.  The quadrant layout is the engine's, not the asset "
     "pipeline's, and nothing emits the boss's overall extent as a figure"),
    (r"above 511 there",
     "the last tile number bank I reaches: 16 KiB at 32 bytes a character is "
     "512 tiles, so 511 is the last.  vram_map.h derives SUB_OBJ_TILES from "
     "the bank rather than writing 512 down, and static_asserts it against the "
     "ten-bit index; host/tests/test_vram.cpp is the other consumer"),
    (r"\{0, 32 KiB\}|\{32 KiB, 32 KiB\}|\{0, 16 KiB\}|boundary 32\b"
     r"|OBJ_BOUNDARY64|raised to 64|boundary 64",
     "a VRAM region's extent, transcribed from vram_map.h.  Its fits() "
     "assertions and host/tests/test_vram.cpp already consume these"),
    (r"208 cases, 113086 checks",
     "the host suite's own totals.  Obtaining them costs a C++ build, which is "
     "Gate 0's last line and deliberately not this tool's business"),
    (r"exits 1\b",
     "another tool's exit status; tools/check_device.py is its own gate"),
    (r"DOOR_ROW \+ 1",
     "the tile one row south of a door.  build_doors.py's R3 refuses a landing "
     "that is not exactly that, so the rule is the number's consumer"),
    (r"HTTP 403",
     "a proxy status code quoted from a log"),
    (r"reads the width from row 0",
     "an index into load_grid's own parsing, not a measurement of a map"),
    (r"Q12\.4",
     "the name of a fixed-point format"),
    (r"\(1/2/3\)",
     "the cast table's own row label, naming which district each triple is for"),
    (r"twenty-nine commits ago|\bfifteen places\b",
     "a count of commits or of call sites at a moment in history; neither is "
     "recoverable from the tree as it stands"),
    # The SNES half of the cast table.  This is the largest single excuse here
    # and it is the one worth reading, because it is NOT "this was hard".
    #
    # The four SNES rows reduce hand-written `.byte ACT_*, i, j` tables in
    # island.s, night.s, town.s and dive.s to four columns, and the convention
    # they use is not stated and is not uniform ACROSS THE DOCUMENT'S OWN ROWS.
    # Sora is inside "placed" on the night row -- 8 props and 6 placed is
    # exactly nightSpawns' fourteen rows split by type, Sora among the six --
    # and outside it on the district row, where town1Spawns' six rows are 2
    # lamps, Sora, and the 3 the table calls placed.  The district row then
    # counts Donald and Goofy, who are in a different table again.  "Peak" is
    # per-row arithmetic of the same kind: the island's 25 is ISLAND_CAST plus
    # the eight day-two items, the night's 20 counts SHADOW_MAX on top of the
    # cast, and the DS night row three lines above does NOT count its twenty.
    #
    # Two of those columns ARE uniform and they are checked rather than excused:
    # every SNES row's props column is its spawn table's prop-typed rows, and
    # island/night/fragment's placed column is the rest of that table.  What is
    # excused is the district row's placed cell and the peak column, and the
    # reason they can be excused rather than corrected is that platform/snes/**
    # is FROZEN: those tables cannot change, so these figures cannot rot.  That
    # is the only argument for excusing a number, and it does not extend one
    # line further -- every DS figure in the same table is checked.
    (r"\| SNES district \(1/2/3\) \| 2 / 2 / 2 \| 3 / 0 / 2 \|",
     "the SNES district row's 'placed' cell excludes Sora and counts "
     "pairSpawns, where the night row's includes Sora; the convention is not "
     "stated and is not uniform, and platform/snes/** is frozen so it cannot rot"),
    (r"\| SNES island \| 8 \| 9 \| 10 \| 25 \|",
     "the SNES island's spots cell: island.s has no spot table -- the day "
     "island has no Heartless -- so the 10 is the night's, quoted against the "
     "same island.  Its peak, 25, is ISLAND_CAST + DAY2_ITEMS, hand-derived"),
    (r"\| SNES night \| 8 \| 6 \| 10 \| 20 \|",
     "the SNES night's peak counts SHADOW_MAX on top of the cast, which the DS "
     "night row two lines below does not do with its twenty"),
    (r"\| SNES fragment \| 1 \| 1 \| – \| 8 \|",
     "the SNES fragment's peak is FRAG_CAST + SHADOW_MAX and leaves out the "
     "boss that night.s's own .assert counts"),
)


# ---------------------------------------------------------------------------
# The tree, measured
# ---------------------------------------------------------------------------

def walkable_of(grid) -> int:
    """Walkable tiles, counted the way build_world's collision plane counts.

    Not `sum(build_world(grid)[1])`, because that paints the entire map first
    and this is run from Gate 0.  It is the same expression -- TERRAIN[code][3]
    per cell -- and `measure()` below cross-checks it against the real
    collision plane for every DS scene, so the shortcut cannot drift from the
    thing it is a shortcut for.
    """
    return sum(1 for j in range(grid.h) for i in range(grid.w)
               if ba.TERRAIN[grid[j][i]][3])


def snes_spawn_rows(source: str, label: str) -> list[tuple[str, int, int]]:
    """One `.byte ACT_*, i, j` table out of the frozen assembly, to its $FF."""
    lines = (SNES_SRC / source).read_text().splitlines()
    try:
        start = next(n for n, ln in enumerate(lines)
                     if ln.startswith(label + ":"))
    except StopIteration:
        raise SystemExit(f"{source}: no table called {label}; the document's "
                         f"SNES cast figures have nothing to come from")
    rows = []
    for ln in lines[start + 1:]:
        body = ln.split(";", 1)[0].strip()
        if not body:
            continue
        if body.startswith(".byte $FF") or body.endswith(":"):
            break
        m = re.fullmatch(r"\.byte\s+(ACT_\w+),\s*(\d+),\s*(\d+)", body)
        if not m:
            raise SystemExit(f"{source}: {label} has a row this cannot read: "
                             f"{body!r}")
        rows.append((m.group(1), int(m.group(2)), int(m.group(3))))
    return rows


def snes_pair_rows(source: str, label: str) -> list[tuple[int, int]]:
    """One `.byte i, j` spot table, to its `<label>End:`."""
    lines = (SNES_SRC / source).read_text().splitlines()
    start = next(n for n, ln in enumerate(lines) if ln.startswith(label + ":"))
    rows = []
    for ln in lines[start + 1:]:
        body = ln.split(";", 1)[0].strip()
        if not body:
            continue
        if body.endswith(":"):
            break
        m = re.fullmatch(r"\.byte\s+(\d+),\s*(\d+)", body)
        if not m:
            raise SystemExit(f"{source}: {label} has a row this cannot read: "
                             f"{body!r}")
        rows.append((int(m.group(1)), int(m.group(2))))
    return rows


def word_table(source: str, label: str) -> list[int]:
    """A `.word` table's values, sign-extended out of `.loword(-n)`."""
    lines = (SNES_SRC / source).read_text().splitlines()
    start = next(n for n, ln in enumerate(lines) if ln.startswith(label + ":"))
    out: list[int] = []
    for ln in lines[start + 1:]:
        body = ln.split(";", 1)[0].strip()
        if not body.startswith(".word"):
            break
        for tok in body[len(".word"):].split(","):
            tok = tok.strip()
            m = re.fullmatch(r"\.loword\((-?\d+)\)", tok) or \
                re.fullmatch(r"(-?\d+)", tok)
            if not m:
                raise SystemExit(f"{source}: {label} has an entry this cannot "
                                 f"read: {tok!r}")
            out.append(int(m.group(1)))
    return out


def worst_windows() -> dict[str, int]:
    """The per-map worst camera window, from the tool that owns the sweep.

    check_map.py slides the 17x13 window and prints "worst window N of M" for
    every scene.  Re-implementing that sweep here would be a second source of
    truth for one number, which is the shape of defect this whole file is about,
    so its report is parsed instead.  If check_map is itself failing its report
    is not evidence of anything and this refuses to use it.
    """
    buf = io.StringIO()
    with redirect_stdout(buf):
        rc = check_map.main()
    if rc != 0:
        raise SystemExit("check_map.py is failing, so its report cannot be "
                         "used as a source; fix that first")
    out = {}
    for line in buf.getvalue().splitlines():
        m = re.match(r"ds/(\w+) ok:.*worst window (\d+) of (\d+)", line)
        if m:
            out[m.group(1)] = int(m.group(2))
    if not out:
        raise SystemExit("check_map.py printed no 'worst window' lines; its "
                         "report format has changed and this cannot read it")
    return out


def constant_sources() -> dict[str, list[tuple[str, int]]]:
    """Every named constant either machine defines, and where.

    game.inc through check_constants' own parser, the DS headers through its
    ds_scalars(), and gen/assets.h separately because it is generated and
    check_constants does not read it.  A name both machines define keeps both
    values: the document says which one it means in English -- "`MAX_ACTORS =
    32` on the SNES" is the SNES's 32 and not the DS's 128 -- and this cannot
    read English.  What it catches is a quoted value that matches NEITHER.
    """
    out: dict[str, list[tuple[str, int]]] = {}

    snes: dict[str, int] = {}
    for name, rhs in check_constants.snes_constants():
        v = check_constants.evaluate(rhs, snes)
        if v is not None:
            snes[name] = v
            out.setdefault(name, []).append(("game.inc", v))

    for name, v in check_constants.ds_scalars().items():
        out.setdefault(name, []).append(("the DS headers", v))

    # gen/assets.h derives rather than emitting literals -- OBJ_REACH is
    # (MAP_TILE_MASK + 1) * OBJ_BOUNDARY precisely so that a boundary change
    # moves it -- so the right-hand sides go through the same evaluator
    # check_constants.py uses on game.inc, with names resolved as they are met.
    gen: dict[str, int] = {}
    for m in re.finditer(r"constexpr\s+(?:\w+\s+)+(\w+)\s*=\s*([^;]+);",
                         (DS_INC / "gen" / "assets.h").read_text()):
        v = check_constants.evaluate(m.group(2).strip(), gen)
        if v is not None:
            gen[m.group(1)] = v
            out.setdefault(m.group(1), []).append(("gen/assets.h", v))
    return out


def measure() -> dict:
    """Everything the document states, as the tree computes it today."""
    m: dict = {
        "tile_px": ba.TILE,
        "step_px": ba.STEP,
        "tilemap_w": ba.TILEMAP_W,
        "tilemap_h": ba.TILEMAP_H,
        "bg_tiles": ba.DS_BG_TILES,
        "bg_chars": ba.DS_BG_MAX_CHARS,
        "screen_tiles": check_map.SCREEN_TILES,
        "dive_default_r": int(ba.build_dive_platform.__defaults__[1]),
        "ds_dive_r": int(ba.DS_DIVE_R),
    }

    snes = {}
    for name in ("island", "fragment", "town1", "town2", "town3"):
        g = ba.load_grid(f"{name}.txt")
        snes[name] = {"w": g.w, "h": g.h, "walkable": walkable_of(g),
                      "grid": g}
    m["snes"] = snes

    ds = {}
    for scene in ba.ds_scenes():
        world, coll, hmap, g = scene.painted()
        cast = ba.load_cast(scene.name)
        props = ba.derive_props(g, scene.props)
        e = {"w": g.w, "h": g.h, "walkable": sum(coll), "grid": g,
             "props": len(props), "base": len(cast.base),
             "spots": len(cast.spots), "doors": len(cast.doors),
             "actors": len(cast.actors),
             "peak": len(props) + cast.peak,
             "tables": {k: len(v) for k, v in cast.tables.items()}}
        # The shortcut in walkable_of() against the plane build_world actually
        # emits.  One line, and it is what lets every other walkable figure in
        # this file be counted without painting a map.
        if walkable_of(g) != e["walkable"]:
            raise SystemExit(f"ds/{scene.name}: the collision plane says "
                             f"{e['walkable']} walkable and TERRAIN says "
                             f"{walkable_of(g)}; this tool's shortcut is wrong")
        if scene.ground is None:
            chars, tilemap, n = ba.dedupe_tilemap_ds(world)
            e.update(chars=n, chr_bytes=len(chars), map_bytes=len(tilemap),
                     coll_bytes=len(coll), height_bytes=len(hmap))
        ds[scene.name] = e
    m["ds"] = ds

    m["worst"] = worst_windows()
    m["const"] = constant_sources()

    m["snes_cast"] = {
        "island": snes_spawn_rows("world.s", "spawnTable"),
        "night": snes_spawn_rows("night.s", "nightSpawns"),
        "fragment": snes_spawn_rows("night.s", "fragSpawns"),
        "town1": snes_spawn_rows("town.s", "town1Spawns"),
        "town2": snes_spawn_rows("town.s", "town2Spawns"),
        "town3": snes_spawn_rows("town.s", "town3Spawns"),
        "station1": snes_spawn_rows("dive.s", "diveSpawns"),
        "station2": snes_spawn_rows("dive.s", "station2Spawns"),
        "station3": snes_spawn_rows("dive.s", "soraOnlySpawns"),
    }
    m["snes_spots"] = {"night": snes_pair_rows("night.s", "nightSpots"),
                       "town2": snes_pair_rows("town.s", "townSpots")}
    m["mote_x"] = word_table("dive.s", "moteOfsX")
    m["mote_y"] = word_table("dive.s", "moteOfsY")

    eq = build_doors.snes_equates()
    m["door_rows"] = build_doors.snes_door_rows(eq)
    m["scene_town2"] = eq["SCENE_TOWN2"]
    stages = {k: v for k, v in eq.items() if k.startswith("T_")}
    m["wiring"] = build_doors.load_wiring(("town1", "town2", "town3"), stages)
    m["door_row"] = eq["DOOR_ROW"]
    return m


# ---------------------------------------------------------------------------
# The document, read
# ---------------------------------------------------------------------------

class Doc:
    """The document, plus what has been accounted for in it.

    Every anchor consumes a span.  Anything left over at the end is either
    excused with a reason or reported, which is the whole difference between
    this and a tool that checks the five numbers somebody happened to think of.
    """

    def __init__(self, text: str):
        self.text = text
        self.spans: list[tuple[int, int]] = []
        self.bad: list[str] = []
        self.groups: dict[str, int] = {}
        self.notes: list[str] = []
        self.ok: list[str] = []

    # -- locating ----------------------------------------------------------
    def at(self, pos: int) -> int:
        return self.text.count("\n", 0, pos) + 1

    def anchor(self, pattern: str, what: str) -> re.Match | None:
        """The one place the document says this.  Zero or two is an error.

        A document checker's characteristic failure is a pattern that stops
        matching after a rewrite and therefore stops checking without saying so.
        Requiring exactly one match turns that into a loud failure.
        """
        found = list(re.finditer(flex(pattern), self.text))
        if len(found) != 1:
            self.bad.append(
                f"{what}: the document has {len(found)} places matching this "
                f"check's pattern and it needs exactly one.  The sentence has "
                f"been reworded, so this figure is no longer being checked at "
                f"all -- update the pattern in tools/check_worldsizes.py "
                f"together with the wording.  /{pattern}/")
            return None
        self.spans.append(found[0].span())
        return found[0]

    def every(self, pattern: str) -> list[re.Match]:
        """All the places the document says this; each consumes its span."""
        found = list(re.finditer(flex(pattern), self.text))
        for m in found:
            self.spans.append(m.span())
        return found

    # -- comparing ---------------------------------------------------------
    def eq(self, group: str, what: str, stated, actual, source: str) -> bool:
        if stated != actual:
            self.bad.append(f"{what}: the document says {stated}, {source} "
                            f"says {actual}")
            return False
        self.groups[group] = self.groups.get(group, 0) + 1
        self.ok.append(f"  {group:24} {what} = {stated}   [{source}]")
        return True

    def covered(self, start: int, end: int) -> bool:
        return any(s <= start and end <= e for s, e in self.spans)


def i(m: re.Match, n: int) -> int:
    return int(m.group(n))


def num(word: str):
    """A count the document spells as a word.  Unknown is None, and fails."""
    return NUMWORD.get(word.lower())


def flex(pattern: str) -> str:
    """One pattern, with every literal space allowed to be a line break.

    The document is hard-wrapped at 80 columns and REFLOWS whenever anybody
    edits a sentence, so a pattern written with plain spaces would break the
    day a word was added three lines earlier.  That is a false failure, and a
    checker that fails on a reflow is a checker whose patterns get loosened
    until they match nothing.  Every anchor below is therefore written as
    ordinary prose and made whitespace-insensitive here.
    """
    return pattern.replace(" ", r"\s+")


# ---------------------------------------------------------------------------
# The checks
# ---------------------------------------------------------------------------

def check_extents(d: Doc, w: dict) -> None:
    """The size table, and every sentence in the file that repeats a shape."""
    tile, snes, ds = w["tile_px"], w["snes"], w["ds"]
    G = "map extents"

    # Every frozen map is the same shape, and the table says so in one row.
    shapes = {(v["w"], v["h"]) for v in snes.values()}
    if len(shapes) != 1:
        d.bad.append(f"the document's 'SNES, all five maps' row assumes one "
                     f"shape and assets/ has {sorted(shapes)}")
        return
    sw, sh = shapes.pop()

    m = d.anchor(r"The SNES maps are (\d+)×(\d+) tiles — (\d+)×(\d+) pixels, "
                 r"which is one\s+(\d+)×(\d+) PPU tilemap", "the opening line")
    if m:
        d.eq(G, "the SNES maps in tiles", (i(m, 1), i(m, 2)), (sw, sh),
             "assets/*.txt")
        d.eq(G, "the SNES maps in pixels", (i(m, 3), i(m, 4)),
             (sw * tile, sh * tile), f"{sw}x{sh} tiles of {tile} px")
        d.eq(G, "the SNES BG1 tilemap", (i(m, 5), i(m, 6)),
             (w["tilemap_w"], w["tilemap_h"]), "build_assets.TILEMAP_W/H")

    rows = (
        (r"\| SNES, all five maps \| (\d+)×(\d+) \| (\d+)×(\d+) \| "
         r"(\d+)×(\d+) \| (\d+)× \|", "the SNES row", (sw, sh)),
        (r"\| \*\*A single DS 2D background, maximum\*\* \| \*\*(\d+)×(\d+)\*\* "
         r"\| \*\*(\d+)×(\d+)\*\* \| \*\*(\d+)×(\d+)\*\* \| (\d+)× \|",
         "the largest non-streaming row", (w["bg_tiles"], w["bg_tiles"])),
        (r"\| DS Traverse Town, each district \| (\d+)×(\d+) \| (\d+)×(\d+) \| "
         r"(\d+)×(\d+) \| \*\*(\d+)×\*\* \|", "the district row",
         (ds["town1"]["w"], ds["town1"]["h"])),
        (r"\| DS Destiny Islands \| (\d+)×(\d+) \| (\d+)×(\d+) \| (\d+)×(\d+) "
         r"\| \*\*(\d+)×\*\* \|", "the island row",
         (ds["island"]["w"], ds["island"]["h"])),
    )
    for pattern, what, (tw, th) in rows:
        m = d.anchor(pattern, what)
        if not m:
            continue
        d.eq(G, f"{what}: tiles", (i(m, 1), i(m, 2)), (tw, th),
             "the grid, or build_assets.DS_BG_TILES")
        d.eq(G, f"{what}: pixels", (i(m, 3), i(m, 4)), (tw * tile, th * tile),
             f"{tw}x{th} tiles of {tile} px")
        d.eq(G, f"{what}: characters", (i(m, 5), i(m, 6)),
             (tw * tile // 8, th * tile // 8), "8 px to a hardware character")
        d.eq(G, f"{what}: area", i(m, 7), (tw * th) // (sw * sh),
             f"{tw}x{th} over the SNES's {sw}x{sh}")

    # The three districts really are all one shape, which the table's single
    # row asserts and nothing else in the tree does.
    # The premise of the table's single district row, checked before the row is
    # compared against town1 alone: three districts, one shape.
    district = {(ds[n]["w"], ds[n]["h"]) for n in ("town1", "town2", "town3")}
    d.eq(G, "the three districts really are one shape", len(district), 1,
         "assets/ds/town*.txt")

    m = d.anchor(r"Our tiles are (\d+) px, so each is (\d+)×(\d+) hardware "
                 r"characters, and a DS\s+background tops out at (\d+)×(\d+) "
                 r"characters", "the tile-size sentence")
    if m:
        d.eq(G, "our tile in pixels", i(m, 1), tile, "build_assets.TILE")
        d.eq(G, "our tile in characters", (i(m, 2), i(m, 3)),
             (tile // 8, tile // 8), "8 px to a hardware character")
        d.eq(G, "the largest DS background", (i(m, 4), i(m, 5)),
             (w["bg_chars"], w["bg_chars"]), "build_assets.DS_BG_MAX_CHARS")

    m = d.anchor(r"is\s+therefore (\d+)×(\d+) of our tiles", "the no-stream size")
    if m:
        d.eq(G, "the largest map that needs no streaming", (i(m, 1), i(m, 2)),
             (w["bg_tiles"], w["bg_tiles"]), "build_assets.DS_BG_TILES")

    m = d.anchor(r"So the DS island is (\d+)×(\d+), the districts are (\d+)×"
                 r"(\d+)", "the island-and-districts sentence")
    if m:
        d.eq(G, "the DS island", (i(m, 1), i(m, 2)),
             (ds["island"]["w"], ds["island"]["h"]), "assets/ds/island.txt")
        d.eq(G, "the DS districts", (i(m, 3), i(m, 4)),
             (ds["town1"]["w"], ds["town1"]["h"]), "assets/ds/town1.txt")

    m = d.anchor(r"\*\*The SNES is stuck at (\d+)×(\d+) permanently", "the freeze")
    if m:
        d.eq(G, "the frozen map size", (i(m, 1), i(m, 2)), (sw, sh),
             "assets/*.txt")
    m = d.anchor(r"BG1's\s+tilemap is (\d+)×(\d+) characters", "BG1's tilemap")
    if m:
        d.eq(G, "BG1's tilemap in characters", (i(m, 1), i(m, 2)),
             (w["tilemap_w"], w["tilemap_h"]), "build_assets.TILEMAP_W/H")
    m = d.anchor(r"rather than demanding (\d+)×(\d+)\.", "load_grid's old assert")
    if m:
        d.eq(G, "what load_grid used to demand", (i(m, 1), i(m, 2)), (sw, sh),
             "assets/*.txt")
    m = d.anchor(r"writes the two (\d+)×(\d+)\s+screens", "the SNES layout")
    if m:
        d.eq(G, "one SNES tilemap screen", (i(m, 1), i(m, 2)),
             (w["tilemap_w"] // 2, w["tilemap_h"]),
             "half of TILEMAP_W by TILEMAP_H")
    m = d.anchor(r"not exactly (\d+)×(\d+) characters, instead", "the refusal")
    if m:
        d.eq(G, "what dedupe_tilemap('snes') insists on", (i(m, 1), i(m, 2)),
             (w["tilemap_w"], w["tilemap_h"]), "build_assets.TILEMAP_W/H")
    m = d.anchor(r"only by the (\d+) px face at its southe", "the walkway face")
    if m:
        d.eq(G, "the face at a tile's southern edge", i(m, 1), tile,
             "build_assets.TILE")

    # The four planes the expanded island costs, in the units the document uses.
    isl = ds["island"]
    m = d.anchor(r"(\d+)×(\d+) is (\d+) KB of collision, (\d+) KB of height and"
                 r"\s+(\d+) KB of tilemap", "the data cost")
    if m:
        d.eq(G, "the island, again", (i(m, 1), i(m, 2)), (isl["w"], isl["h"]),
             "assets/ds/island.txt")
        d.eq(G, "islandcoll.bin", i(m, 3), isl["coll_bytes"] // 1024,
             "the emitted collision plane, in KB")
        d.eq(G, "islandheight.bin", i(m, 4), isl["height_bytes"] // 1024,
             "the emitted height plane, in KB")
        d.eq(G, "islandmap.bin", i(m, 5), isl["map_bytes"] // 1024,
             "the emitted tilemap, in KB")
    m = d.anchor(r"does not duplicate (\d+) KB of ground", "the shared ground")
    if m:
        total = (isl["chr_bytes"] + isl["map_bytes"]
                 + isl["coll_bytes"] + isl["height_bytes"])
        d.eq(G, "what the night does not re-emit", i(m, 1),
             round(total / 1024), f"{total} bytes of chr, map, coll and height")


def check_walkable(d: Doc, w: dict) -> None:
    """The counts the reviewer moved with a one-character edit."""
    G = "walkable ground"
    snes, ds = w["snes"], w["ds"]

    m = d.anchor(r"\*\*Destiny Islands\*\* — (\d+)×(\d+)\. (\d+) walkable tiles "
                 r"against (\d+), so ([\d.]+)× the", "the island's counts")
    if m:
        d.eq(G, "the DS island's shape", (i(m, 1), i(m, 2)),
             (ds["island"]["w"], ds["island"]["h"]), "assets/ds/island.txt")
        d.eq(G, "the DS island's walkable tiles", i(m, 3),
             ds["island"]["walkable"], "build_assets.TERRAIN over the grid")
        d.eq(G, "the SNES island's walkable tiles", i(m, 4),
             snes["island"]["walkable"], "build_assets.TERRAIN over the grid")
        d.eq(G, "the ratio of the two", m.group(5),
             f"{ds['island']['walkable'] / snes['island']['walkable']:.1f}",
             "the two counts, to one decimal")

    m = d.anchor(r"\*\*Traverse Town's three districts\*\* — (\d+)×(\d+) each, "
                 r"(\d+)× the area, (\d+) / (\d+) / (\d+)\s+walkable tiles "
                 r"against (\d+) / (\d+) / (\d+)\. Not (\d+) wide",
                 "the districts' counts")
    if m:
        d.eq(G, "a district's shape", (i(m, 1), i(m, 2)),
             (ds["town1"]["w"], ds["town1"]["h"]), "assets/ds/town1.txt")
        d.eq(G, "a district's area", i(m, 3),
             (ds["town1"]["w"] * ds["town1"]["h"])
             // (snes["town1"]["w"] * snes["town1"]["h"]),
             "the two grids")
        for stem, grp in (("town1", 4), ("town2", 5), ("town3", 6)):
            d.eq(G, f"DS {stem}: walkable", i(m, grp), ds[stem]["walkable"],
                 f"build_assets.TERRAIN over assets/ds/{stem}.txt")
        for stem, grp in (("town1", 7), ("town2", 8), ("town3", 9)):
            d.eq(G, f"SNES {stem}: walkable", i(m, grp), snes[stem]["walkable"],
                 f"build_assets.TERRAIN over assets/{stem}.txt")
        d.eq(G, "the width a district was not given", i(m, 10),
             ds["island"]["w"], "the island's width, which is the alternative")

    # The closing section's own demonstration, which is where the measurement
    # that justified this tool is written down.
    m = d.anchor(r"takes the district from (\d+) walkable to (\d+), and",
                 "the reviewer's demonstration")
    if m:
        d.eq(G, "town3 before the demonstration edit", i(m, 1),
             ds["town3"]["walkable"], "assets/ds/town3.txt")
        d.eq(G, "town3 after it", i(m, 2), ds["town3"]["walkable"] - 1,
             "one cobble turned into a crate")
    m = d.anchor(r"with this file claiming (\d+)\.", "the claim that survived")
    if m:
        d.eq(G, "the figure the gates did not defend", i(m, 1),
             ds["town3"]["walkable"], "assets/ds/town3.txt")


def check_history(d: Doc, w: dict) -> None:
    """717, 912/840/895 and 242: numbers of maps that no longer exist.

    These cannot be recomputed -- they are the maps before the props went into
    them, and the props went in one commit later.  What CAN be checked is the
    arithmetic that ties them to the numbers that are recomputable, and that is
    the half that actually rots: an edit that moves 670 and leaves 717 alone
    leaves a subtraction that no longer works, and the document uses that
    subtraction to argue that nothing shrank.
    """
    G = "the props' arithmetic"
    ds = w["ds"]

    old = d.anchor(r"— (\d+) for the island and (\d+) / (\d+) / (\d+) for the\s+"
                   r"districts", "the former counts")
    m = d.anchor(r"the island gained (\d+) `T`, (\d+) `r`, (\d+) `b`, (\d+) `R` "
                 r"and (\d+) `Y`, which is (\d+) tiles and (\d+) − (\d+) = "
                 r"(\d+), and", "the island's props")
    if m:
        parts = [i(m, n) for n in (1, 2, 3, 4, 5)]
        d.eq(G, "the island's five prop characters sum to the stated total",
             i(m, 6), sum(parts), " + ".join(str(p) for p in parts))
        d.eq(G, "the old island count, restated", i(m, 7),
             old and i(old, 1), "the sentence above")
        d.eq(G, "the gain, restated", i(m, 8), i(m, 6), "the same sentence")
        d.eq(G, "old minus the props is the island's count today", i(m, 9),
             ds["island"]["walkable"], "build_assets over assets/ds/island.txt")
        d.eq(G, "and the subtraction works", i(m, 7) - i(m, 8), i(m, 9),
             "arithmetic")

    m = d.anchor(r"each district gained (\d+) lamp posts and (\d+) to (\d+) "
                 r"crates, which is the (\d+) / (\d+) / (\d+)\s+that takes "
                 r"(\d+) / (\d+) / (\d+) to (\d+) / (\d+) / (\d+) exactly",
                 "the districts' props")
    if m:
        gains = [i(m, 4), i(m, 5), i(m, 6)]
        lamps = i(m, 1)
        crates = sorted(g - lamps for g in gains)
        d.eq(G, "the crates per district, low", i(m, 2), crates[0],
             f"the stated gains {gains} less {lamps} lamps each")
        d.eq(G, "the crates per district, high", i(m, 3), crates[-1],
             f"the stated gains {gains} less {lamps} lamps each")
        for n, stem in enumerate(("town1", "town2", "town3")):
            d.eq(G, f"{stem}: today's count, stated", i(m, 10 + n),
                 ds[stem]["walkable"], f"build_assets over assets/ds/{stem}.txt")
            d.eq(G, f"{stem}: old minus the gain", i(m, 7 + n) - gains[n],
                 i(m, 10 + n), "arithmetic")
            if old:
                d.eq(G, f"{stem}: the old count, restated", i(m, 7 + n),
                     i(old, 2 + n), "the sentence above")

    said = d.anchor(r"\(This file said (\d+) for the island", "the old unique count")
    m = d.anchor(r"would have caught (\d+), (\d+) / (\d+) / (\d+) and\s+(\d+) "
                 r"on the commit", "the closing section's restatement")
    if m and old and said:
        for n in range(4):
            d.eq(G, f"the closing section's figure {n + 1}", i(m, n + 1),
                 i(old, n + 1), "the 'What is expanded' section")
        d.eq(G, "the closing section's unique count", i(m, 5), i(said, 1),
             "the 'What had to change' section")


def check_unique(d: Doc, w: dict) -> None:
    """The character dedupe, which nothing else in the tree publishes."""
    G = "unique characters"
    ds = w["ds"]
    m = d.anchor(r"the (\d+)×(\d+) island folds to \*\*(\d+) unique characters"
                 r"\*\* from (\d+) cells, and the\s+districts to (\d+), (\d+) "
                 r"and (\d+) from (\d+) each", "the dedupe's figures")
    if not m:
        return
    isl = ds["island"]
    d.eq(G, "the island's shape", (i(m, 1), i(m, 2)), (isl["w"], isl["h"]),
         "assets/ds/island.txt")
    d.eq(G, "the island's unique characters", i(m, 3), isl["chars"],
         "build_assets.dedupe_tilemap_ds")
    d.eq(G, "the island's cells", i(m, 4),
         isl["w"] * w["tile_px"] // 8 * (isl["h"] * w["tile_px"] // 8),
         "the painted map in 8x8 characters")
    for n, stem in enumerate(("town1", "town2", "town3")):
        d.eq(G, f"{stem}'s unique characters", i(m, 5 + n), ds[stem]["chars"],
             "build_assets.dedupe_tilemap_ds")
    t1 = ds["town1"]
    d.eq(G, "a district's cells", i(m, 8),
         t1["w"] * w["tile_px"] // 8 * (t1["h"] * w["tile_px"] // 8),
         "the painted map in 8x8 characters")


def check_door_row(d: Doc, w: dict) -> None:
    """The row the whole town hangs off, and the six tiles in it."""
    G = "the district door row"
    ds, grids = w["ds"], {n: w["ds"][n]["grid"] for n in
                          ("town1", "town2", "town3")}

    m = d.anchor(r"\*\*A \+(\d+) building cannot stand south of anything "
                 r"walkable\.\*\*", "the building height")
    if m:
        d.eq(G, "a building's height", i(m, 1), ba.TERRAIN["w"][4],
             "build_assets.TERRAIN['w']")
    m = d.anchor(r"is painted (\d+)h pixels above its own", "the height step")
    if m:
        d.eq(G, "one height step in pixels", i(m, 1), ba.STEP,
             "build_assets.STEP")
    m = d.anchor(r"The south side is a height-(\d+) parapet", "the parapet")
    if m:
        worst = max(ba.TERRAIN[c][4]
                    for g in grids.values() for c in g.rows[g.h - 1])
        d.eq(G, "the tallest thing in a district's southern row", i(m, 1),
             worst, "TERRAIN over row 31 of the three districts")

    m = d.anchor(r"whole map is reached through row (\d+) — `DOOR_ROW`, "
                 r"`constants\.h:(\d+)`", "DOOR_ROW and its citation")
    if m:
        d.eq(G, "DOOR_ROW", i(m, 1), w["door_row"], "game.inc")
        # The one citation this tool opens.  Here the line number and the claim
        # are the same statement -- "DOOR_ROW is defined at constants.h:477" --
        # so checking that the file has that many lines would be checking
        # nothing.  Everywhere else a citation points at prose and this tool
        # deliberately does not read it; see the module docstring.
        line = (DS_INC / "constants.h").read_text().splitlines()[i(m, 2) - 1]
        d.eq(G, "constants.h's cited line defines DOOR_ROW",
             "DOOR_ROW" in line, True, f"constants.h:{i(m, 2)} is {line.strip()!r}")

    m = d.anchor(r"row (\d+) of each of the three maps is unbroken \+(\d+)\s+"
                 r"building except at town1 \((\d+),(\d+)\) and \((\d+),(\d+)\), "
                 r"town2 \((\d+),(\d+)\), \((\d+),(\d+)\) and \((\d+),(\d+)\),"
                 r"\s+and town3 \((\d+),(\d+)\)", "the door tiles")
    if m:
        d.eq(G, "the row the doors are in", i(m, 1), w["door_row"], "game.inc")
        d.eq(G, "the height row 4 is otherwise", i(m, 2), ba.TERRAIN["w"][4],
             "build_assets.TERRAIN['w']")
        stated = {"town1": [(i(m, 3), i(m, 4)), (i(m, 5), i(m, 6))],
                  "town2": [(i(m, 7), i(m, 8)), (i(m, 9), i(m, 10)),
                            (i(m, 11), i(m, 12))],
                  "town3": [(i(m, 13), i(m, 14))]}
        for stem, want in stated.items():
            g = grids[stem]
            doors = [(x, j) for j in range(g.h) for x in range(g.w)
                     if g[j][x] == "d"]
            d.eq(G, f"{stem}'s door tiles", sorted(want), sorted(doors),
                 f"the 'd' tiles in assets/ds/{stem}.txt")
            # "unbroken +3 building except at" -- everything else in the row is
            # a building face, which is what makes a district a courtyard with
            # a handful of single tiles for exits.
            other = {g.rows[w["door_row"]][x] for x in range(g.w)
                     if (x, w["door_row"]) not in want}
            d.eq(G, f"{stem}'s row {w['door_row']} is otherwise unbroken",
                 all(ba.TERRAIN[c][4] == ba.TERRAIN["w"][4]
                     and not ba.TERRAIN[c][3] for c in other), True,
                 f"the row holds {''.join(sorted(other))} beside the doors")
        d.eq(G, "six door tiles in all",
             sum(len(v) for v in stated.values()),
             sum(1 for g in grids.values() for j in range(g.h)
                 for x in range(g.w) if g[j][x] == "d"),
             "the 'd' tiles in the three district maps")

    m = d.anchor(r"appears in row (\d+), at (\w+) columns in the First District, "
                 r"(\w+) in the Second\s+and (\w+) in the Third", "the e columns")
    if m:
        d.eq(G, "the row again", i(m, 1), w["door_row"], "game.inc")
        for n, stem in enumerate(("town1", "town2", "town3")):
            g = grids[stem]
            d.eq(G, f"{stem}'s lit windows in row {w['door_row']}",
                 num(m.group(2 + n)),
                 sum(1 for c in g.rows[w["door_row"]] if c == "e"),
                 f"the 'e' tiles in row {w['door_row']} of assets/ds/{stem}.txt")

    m = d.anchor(r"the same\s+tuple as `TERRAIN\['w'\]`: height (\d+), not "
                 r"walkable", "the e/w equivalence")
    if m:
        d.eq(G, "TERRAIN['e'] is TERRAIN['w']", ba.TERRAIN["e"],
             ba.TERRAIN["w"], "build_assets.TERRAIN")
        d.eq(G, "and its height", i(m, 1), ba.TERRAIN["e"][4],
             "build_assets.TERRAIN['e']")
        d.eq(G, "and it is not walkable", ba.TERRAIN["e"][3], False,
             "build_assets.TERRAIN['e']")

    m = d.anchor(r"`doorTable` is\s+(\w+) rows and (\w+) of them arrive in the "
                 r"Second District", "the frozen doorTable")
    if m:
        d.eq(G, "doorTable's rows", num(m.group(1)),
             len(w["door_rows"]), "build_doors.snes_door_rows over town.s")
        d.eq(G, "doorTable rows arriving in the Second District",
             num(m.group(2)),
             sum(1 for r in w["door_rows"] if r[2] == w["scene_town2"]),
             "their destination column")

    m = d.anchor(r"(\w+) of those six door tiles are the SNES's joins and (\w+) "
                 r"are shop fronts", "the six doors' fates")
    if m:
        wires = w["wiring"]
        d.eq(G, "doors that are joins", num(m.group(1)),
             sum(1 for x in wires if x.routed), "assets/ds/town_doors.txt")
        d.eq(G, "doors that are shop fronts", num(m.group(2)),
             sum(1 for x in wires if not x.routed), "assets/ds/town_doors.txt")


def check_stations(d: Doc, w: dict) -> None:
    """The disc that had to be redrawn, and the arithmetic that sized it."""
    G = "the stations"
    const = w["const"]

    def one(name: str) -> int | None:
        vals = {v for _, v in const.get(name, ())}
        return vals.pop() if len(vals) == 1 else None

    snes_h = one("SNES_SCREEN_H")
    ds_h = [v for src, v in const.get("SCREEN_H", ()) if src != "game.inc"]
    ds_h = ds_h[0] if len(ds_h) == 1 else None

    m = d.anchor(r"a (\d+) px disc inside (\d+) lines\. On the DS's (\d+) it is "
                 r"clipped by\s+(\d+) px top and bottom", "the clipped disc")
    if m:
        d.eq(G, "the SNES disc across", i(m, 1), 2 * w["dive_default_r"],
             "twice build_dive_platform's default radius")
        d.eq(G, "the SNES screen", i(m, 2), snes_h, "SNES_SCREEN_H")
        d.eq(G, "the DS screen", i(m, 3), ds_h, "constants.h's SCREEN_H")
        d.eq(G, "what is clipped at each end", i(m, 4),
             (i(m, 1) - i(m, 3)) // 2, "half the overhang")

    m = d.anchor(r"the DS draws a radius-(\d+) disc, (\d+) px across",
                 "the DS disc")
    if m:
        d.eq(G, "the DS radius", i(m, 1), w["ds_dive_r"],
             "build_assets.DS_DIVE_R")
        d.eq(G, "the DS disc across", i(m, 2), 2 * w["ds_dive_r"],
             "twice DS_DIVE_R")
    m = d.anchor(r"takes the radius as an argument defaulting to the\s+SNES's "
                 r"(\d+),", "the default radius")
    if m:
        d.eq(G, "build_dive_platform's default", i(m, 1), w["dive_default_r"],
             "its signature")
    m = d.anchor(r"already sits well inside (\d+) lines", "the fragment's ground")
    if m:
        d.eq(G, "the DS screen, again", i(m, 1), ds_h, "constants.h's SCREEN_H")


def check_fall(d: Doc, w: dict) -> None:
    """The drop between the stations, now that a scenario measures it."""
    G = "the fall"
    const = w["const"]
    fall_len = {v for _, v in const.get("FALL_LEN", ())}
    fall_len = fall_len.pop() if len(fall_len) == 1 else None
    case = next((c for c in trace_check.CASES if c.name == "fall"), None)
    if case is None:
        d.bad.append("tools/trace_check.py has no scenario called 'fall'; the "
                     "document's whole 'the fall is measured' entry rests on it")
        return

    m = d.anchor(r"the (\d+)-frame fall \*between\*", "the fall's length")
    if m:
        d.eq(G, "FALL_LEN", i(m, 1), fall_len, "game.inc and constants.h")
    m = d.anchor(r"(\d+) frames of `MOTE_LIFE` specks rising past Sora from up "
                 r"to (\d+) px below him", "the fall, restated")
    if m:
        d.eq(G, "FALL_LEN, restated", i(m, 1), fall_len,
             "game.inc and constants.h")
        d.eq(G, "the furthest a speck starts below him", i(m, 2),
             max(w["mote_y"]) // 16, "dive.s's moteOfsY, in Q12.4")

    # THE FALL CASE IS 200 FRAMES AND THE DOCUMENT SAYS 198, and the difference
    # is not a mistake in either of them: the scenario starts comparing at frame
    # 2, so 198 frames are compared.  That is not a reading imposed here --
    # trace_check.py runs the DS side for exactly `case.frames - case.first`
    # frames (`tools/trace_check.py:254`) and its own report prints
    # "fall: 198 frames, identical from frame 2" (`:293`).  So the figure the
    # document quotes is the one the oracle prints, and the sentence beside it
    # -- "byte-identical to the SNES from frame 2" -- names the subtraction.
    m = d.anchor(r"\), (\d+) frames, `strict=True`", "the scenario's length")
    if m:
        d.eq(G, "frames the fall scenario compares", i(m, 1),
             case.frames - case.first,
             f"trace_check's fall Case: {case.frames} frames from {case.first}")
    m = d.anchor(r"byte-identical to the SNES from frame (\d+)\*\*",
                 "where it becomes identical")
    if m:
        d.eq(G, "the fall's first compared frame", i(m, 1), case.first,
             "trace_check's fall Case")
        d.eq(G, "and it is compared byte for byte", case.identical, True,
             "trace_check's fall Case")

    m = d.anchor(r"X spreads ±(\d+) px, Y is (\d+) to (\d+) px below him",
                 "the mote spread")
    if m:
        d.eq(G, "the widest a speck spreads", i(m, 1),
             max(abs(v) for v in w["mote_x"]) // 16, "dive.s's moteOfsX")
        d.eq(G, "the nearest a speck starts", i(m, 2), min(w["mote_y"]) // 16,
             "dive.s's moteOfsY")
        d.eq(G, "the furthest", i(m, 3), max(w["mote_y"]) // 16,
             "dive.s's moteOfsY")

    py = None
    m = d.anchor(r"is pinned at (\d+) throughout the drop", "Sora's pinned py")
    if m:
        py = i(m, 1)
        j = {row[2] for name in ("station1", "station2", "station3")
             for row in w["snes_cast"][name] if row[0] == "ACT_SORA"}
        d.eq(G, "Sora's world y during the drop", py,
             (j.pop() * 256 + 128) if len(j) == 1 else None,
             "the tile centre of the row every station spawns him on")
    m = d.anchor(r"bottom edge is at (\d+)\s+and the DS's at (\d+), and the "
                 r"specks appear at (\d+)…(\d+) px on both", "the screen edges")
    if m:
        snes_cam = {v for s, v in w["const"].get("DIVE_CAM_Y", ())
                    if s == "game.inc"}
        ds_cam = {v for s, v in w["const"].get("DIVE_CAM_Y", ())
                  if s != "game.inc"}
        snes_h = {v for s, v in w["const"].get("SNES_SCREEN_H", ())}
        ds_h = {v for s, v in w["const"].get("SCREEN_H", ()) if s != "game.inc"}
        if len(snes_cam) == len(ds_cam) == len(snes_h) == len(ds_h) == 1:
            d.eq(G, "the SNES's bottom edge during the drop", i(m, 1),
                 snes_cam.pop() + snes_h.pop(),
                 "game.inc's DIVE_CAM_Y plus SCREEN_H")
            d.eq(G, "the DS's", i(m, 2), ds_cam.pop() + ds_h.pop(),
                 "constants.h's DIVE_CAM_Y plus SCREEN_H")
        d.eq(G, "where the nearest speck appears", i(m, 3),
             py // 16 + min(w["mote_y"]) // 16, "his pinned y plus the spread")
        d.eq(G, "and the furthest", i(m, 4),
             py // 16 + max(w["mote_y"]) // 16, "his pinned y plus the spread")

    m = d.anchor(r"the `& \$(\d+)` mask", "the mote table's mask")
    if m:
        d.eq(G, "the mask over the spread table", int(m.group(1), 16),
             len(w["mote_x"]) - 1, "dive.s's moteOfsX has that many entries")
    m = d.anchor(r"on a (\d+)-line-shorter screen", "the screen difference")
    if m:
        snes_h = {v for s, v in w["const"].get("SNES_SCREEN_H", ())}
        ds_h = {v for s, v in w["const"].get("SCREEN_H", ()) if s != "game.inc"}
        if len(snes_h) == len(ds_h) == 1:
            d.eq(G, "how much shorter the DS screen is", i(m, 1),
                 snes_h.pop() - ds_h.pop(), "SNES_SCREEN_H less SCREEN_H")


def check_cast(d: Doc, w: dict) -> None:
    """The cast table: props, placed, spots and peak, per scene.

    THE PLACED COLUMN HAS TWO READINGS AND THE DOCUMENT USES BOTH, so this says
    out loud which one it applies where.  Every row marked DS is the scene's
    ENTRY cast, `[base]` -- the island's 9 is its nine base rows and not the
    twenty-one authored ones, and the Third District's 1 is Sora and not Sora
    plus Donald and Goofy.  The `station 1 / 2 / 3` row is the one row that
    names no machine, and the sentence directly under the table defines it:
    "the stations are the only ones with no props at all ... so they are also
    the only ones where every actor is placed by hand".  For that row, and only
    that row, placed is the WHOLE authored cast, which is why station 3 reads 2
    -- Sora and the Darkside that rises out of his shadow.

    That reading is only licensed while the sentence is true and present, so
    both are required: the sentence is anchored, and every station is required
    to have no props.  A station that gained a prop tile would falsify the
    sentence, and this check would fail with it rather than quietly carrying on
    under a premise that had stopped holding.

    AND THE HONEST CAVEAT, because it is a judgement and not a measurement.
    Station 3's cell is the only one in the table where the two readings give
    different numbers: its entry cast is 1 (Sora) and its whole authored cast is
    2 (Sora and the Darkside in `[boss]`).  The document says 2 there, and its
    peak column says 2 as well, so the cell is either the whole authored cast --
    which is what the sentence under the table licenses -- or it is the peak
    column copied one cell to the left.  If the document's owner meant the entry
    cast, that cell is wrong and the fix here is one line: compare against
    `e["base"]` like every DS row above.  Whoever settles it should settle it in
    both places at once, which is the only reason this paragraph is as long as
    it is.
    """
    G = "the cast table"
    ds, sc = w["ds"], w["snes_cast"]

    def props_of(rows) -> int:
        return sum(1 for r in rows if r[0] in SNES_PROP_ACT)

    rows = (
        (r"\| SNES island \| (\d+) \| (\d+) \| 10 \| 25 \|", "SNES island",
         props_of(sc["island"]), len(sc["island"]) - props_of(sc["island"])),
        (r"\| SNES night \| (\d+) \| (\d+) \| (\d+) \| 20 \|", "SNES night",
         props_of(sc["night"]), len(sc["night"]) - props_of(sc["night"])),
        (r"\| SNES fragment \| (\d+) \| (\d+) \| – \| 8 \|", "SNES fragment",
         props_of(sc["fragment"]),
         len(sc["fragment"]) - props_of(sc["fragment"])),
    )
    for pattern, what, props, placed in rows:
        m = d.anchor(pattern, what)
        if not m:
            continue
        d.eq(G, f"{what}: props", i(m, 1), props,
             "the prop-typed rows of its spawn table")
        d.eq(G, f"{what}: placed", i(m, 2), placed,
             "the rest of its spawn table")
        if what == "SNES night":
            d.eq(G, f"{what}: spots", i(m, 3), len(w["snes_spots"]["night"]),
                 "night.s's nightSpots")

    m = d.anchor(r"\| SNES district \(1/2/3\) \| (\d+) / (\d+) / (\d+) \| "
                 r"3 / 0 / 2 \| – / (\d+) / – \| 6 / 5 / 7 \|", "SNES districts")
    if m:
        for n, stem in enumerate(("town1", "town2", "town3")):
            d.eq(G, f"SNES {stem}: props", i(m, 1 + n), props_of(sc[stem]),
                 f"the prop-typed rows of {stem}Spawns")
        d.eq(G, "SNES town2: spots", i(m, 4), len(w["snes_spots"]["town2"]),
             "town.s's townSpots")

    ds_rows = (
        (r"\| \*\*DS island\*\* \| \*\*(\d+)\*\* \| (\d+) \| (\d+) \| "
         r"\*\*(\d+)\*\* \|", "DS island", "island"),
        (r"\| \*\*DS night\*\* \| \*\*(\d+)\*\* \| (\d+) \| \*\*(\d+)\*\* \| "
         r"\*\*(\d+)\*\* \|", "DS night", "night"),
        (r"\| DS fragment \| (\d+) \| (\d+) \| – \| (\d+) \|", "DS fragment",
         "fragment"),
    )
    for pattern, what, stem in ds_rows:
        m = d.anchor(pattern, what)
        if not m:
            continue
        e = ds[stem]
        d.eq(G, f"{what}: props", i(m, 1), e["props"],
             "build_assets.derive_props over the map")
        d.eq(G, f"{what}: placed", i(m, 2), e["base"],
             f"the [base] table of assets/ds/{stem}_cast.txt")
        if stem == "fragment":
            d.eq(G, f"{what}: peak", i(m, 3), e["peak"], "props + the whole cast")
        else:
            d.eq(G, f"{what}: spots", i(m, 3), e["spots"],
                 f"the [spots] table of assets/ds/{stem}_cast.txt")
            d.eq(G, f"{what}: peak", i(m, 4), e["peak"],
                 "props + the whole cast")

    m = d.anchor(r"\| \*\*DS district \(1/2/3\)\*\* \| \*\*(\d+) / (\d+) / "
                 r"(\d+)\*\* \| (\d+) / (\d+) / (\d+) \| – / \*\*(\d+)\*\* / – "
                 r"\| (\d+) / (\d+) / (\d+) \|", "DS districts")
    if m:
        for n, stem in enumerate(("town1", "town2", "town3")):
            e = ds[stem]
            d.eq(G, f"DS {stem}: props", i(m, 1 + n), e["props"],
                 "build_assets.derive_props over the map")
            d.eq(G, f"DS {stem}: placed", i(m, 4 + n), e["base"],
                 f"the [base] table of assets/ds/{stem}_cast.txt")
            d.eq(G, f"DS {stem}: peak", i(m, 8 + n), e["peak"],
                 "props + the whole cast")
        d.eq(G, "DS town2: spots", i(m, 7), ds["town2"]["spots"],
             "the [spots] table of assets/ds/town2_cast.txt")

    licence = d.anchor(r"they are also the only\s+ones where every actor is "
                       r"placed by hand", "what licenses the station row")
    m = d.anchor(r"\| station 1 / 2 / 3 \| – \| (\d+) / (\d+) / (\d+) \| – \| "
                 r"(\d+) / (\d+) / (\d+) \|", "the station row")
    if m:
        for n, stem in enumerate(("station1", "station2", "station3")):
            e = ds[stem]
            # The premise the sentence states, checked before it is relied on.
            d.eq(G, f"{stem} has no props", e["props"], 0,
                 "build_assets.derive_props over the generated glass")
            if licence:
                d.eq(G, f"{stem}: placed by hand", i(m, 1 + n), e["actors"],
                     f"every authored row of assets/ds/{stem}_cast.txt")
            d.eq(G, f"{stem}: peak", i(m, 4 + n), e["peak"],
                 "the whole cast; there are no props to add")


def check_density(d: Doc, w: dict) -> None:
    """The night's Shadow count, which is a derivation the document shows."""
    G = "the night's density"
    const = w["const"]

    def val(name: str, snes: bool):
        vals = [v for s, v in const.get(name, ())
                if (s == "game.inc") == snes]
        return vals[0] if len(vals) == 1 else None

    m = d.anchor(r"(\d+) walkable\s+over (\d+) is one per (\d+), and (\d+) at "
                 r"that density is (\d+)\.", "the tiles-per-Shadow derivation")
    if m:
        d.eq(G, "the SNES island's walkable tiles", i(m, 1),
             w["snes"]["island"]["walkable"], "build_assets.TERRAIN")
        d.eq(G, "the SNES's SHADOW_MAX", i(m, 2), val("SHADOW_MAX", True),
             "game.inc")
        d.eq(G, "tiles per Shadow", i(m, 3), round(i(m, 1) / i(m, 2)),
             "the two above, rounded")
        d.eq(G, "the DS island's walkable tiles", i(m, 4),
             w["ds"]["island"]["walkable"], "build_assets.TERRAIN")
        d.eq(G, "the DS's SHADOW_MAX_NIGHT", i(m, 5),
             val("SHADOW_MAX_NIGHT", False), "constants.h")
        d.eq(G, "and it is that density", i(m, 5), i(m, 4) // i(m, 3),
             "the DS island over the SNES's tiles per Shadow")

    m = d.anchor(r"roughly (\d+)× the SNES's (\d+) frames, which at (\d+) alive "
                 r"is (\d+)\.", "the SHADOW_GAP derivation")
    if m:
        gap, mx = val("SHADOW_GAP", True), val("SHADOW_MAX", True)
        d.eq(G, "how long the SNES fill takes", i(m, 2),
             gap * mx if gap and mx else None,
             "game.inc's SHADOW_GAP times SHADOW_MAX")
        d.eq(G, "the DS's SHADOW_MAX_NIGHT, restated", i(m, 3),
             val("SHADOW_MAX_NIGHT", False), "constants.h")
        d.eq(G, "the DS's SHADOW_GAP", i(m, 4), val("SHADOW_GAP", False),
             "constants.h")
        d.eq(G, "and it is the stated arithmetic", i(m, 4),
             i(m, 1) * i(m, 2) // i(m, 3), "2 x 420 over 20")

    m = d.anchor(r"The spot table went from (\d+) to (\d+) because", "the spots")
    if m:
        d.eq(G, "the SNES's NIGHT_SPOTS", i(m, 1), val("NIGHT_SPOTS", True),
             "game.inc")
        d.eq(G, "the DS night's spots", i(m, 2), w["ds"]["night"]["spots"],
             "the [spots] table of assets/ds/night_cast.txt")


def check_pool(d: Doc, w: dict) -> None:
    """The actor pool and the two budgets a dense map has to answer for."""
    G = "the pool and the budgets"
    const = w["const"]

    def val(name: str, snes: bool):
        vals = [v for s, v in const.get(name, ())
                if (s == "game.inc") == snes]
        return vals[0] if len(vals) == 1 else None

    m = d.anchor(r"The DS pool holds \*\*(\d+)\*\*", "the DS pool")
    if m:
        d.eq(G, "the DS's MAX_ACTORS", i(m, 1), val("MAX_ACTORS", False),
             "constants.h")
    m = d.anchor(r"asks for (\d+) prop actors before anybody is placed on it, "
                 r"so at (\d+) it cannot be\s+loaded", "why the pool grew")
    if m:
        d.eq(G, "the island's prop actors", i(m, 1), w["ds"]["island"]["props"],
             "build_assets.derive_props over assets/ds/island.txt")
        d.eq(G, "the SNES's MAX_ACTORS", i(m, 2), val("MAX_ACTORS", True),
             "game.inc")
    # An ordinary actor's cel, which gen/assets.h does publish -- as bytes, so
    # the check is the other way round: 32x32 at 4bpp is 512 bytes.
    m = d.anchor(r"an ordinary actor is one (\d+)×(\d+) sprite", "the cel size")
    if m:
        cel = [v for s, v in w["const"].get("OBJ_CEL_BYTES", ())
               if s == "gen/assets.h"]
        d.eq(G, "an ordinary actor's cel", i(m, 1) * i(m, 2) // 2,
             cel[0] if len(cel) == 1 else None,
             "gen/assets.h's OBJ_CEL_BYTES, at 4bpp")

    m = d.anchor(r"tiles so the (\d+) palms and rocks cost", "the VRAM entry")
    if m:
        d.eq(G, "the island's prop actors, restated", i(m, 1),
             w["ds"]["island"]["props"], "build_assets.derive_props")

    for pattern, what in ((r"slides a (\d+)×(\d+)-tile camera window",
                           "the camera window"),
                          (r"counts OAM entries inside a (\d+)×(\d+)-tile "
                           r"camera window", "the camera window, restated")):
        m = d.anchor(pattern, what)
        if m:
            d.eq(G, what, (i(m, 1), i(m, 2)), w["screen_tiles"],
                 "check_map.SCREEN_TILES")

    m = d.anchor(r"`OBJ_BUDGET_SCENERY = (\d+)` of the engine's (\d+), leaving "
                 r"(\d+) for the transients", "the OBJ budget")
    if m:
        d.eq(G, "what is left for the transients", i(m, 3), i(m, 2) - i(m, 1),
             "the engine's OAM entries less the scenery budget")

    m = d.anchor(r"Worst cases today: island (\d+), districts (\d+)–(\d+)\.",
                 "the worst camera windows")
    if m:
        worst = w["worst"]
        d.eq(G, "the island's worst window", i(m, 1), worst.get("island"),
             "check_map.py's own sweep")
        district = sorted(worst[n] for n in ("town1", "town2", "town3"))
        d.eq(G, "the districts' worst window, low", i(m, 2), district[0],
             "check_map.py's own sweep")
        d.eq(G, "the districts' worst window, high", i(m, 3), district[-1],
             "check_map.py's own sweep")


def check_quoted_constants(d: Doc, w: dict) -> None:
    """Any `NAME = 123` in backticks, looked up by value."""
    G = "constants quoted with a value"
    for m in d.every(r"`([A-Z][A-Z0-9_]*) = (\d+)`"):
        name, stated = m.group(1), int(m.group(2))
        where = w["const"].get(name)
        if not where:
            d.bad.append(f"the document quotes `{name} = {stated}` and nothing "
                         f"in game.inc, the DS headers or gen/assets.h defines "
                         f"a constant of that name")
            continue
        if stated not in {v for _, v in where}:
            d.bad.append(
                f"the document quotes `{name} = {stated}`; "
                + " and ".join(f"{s} says {v}" for s, v in where))
            continue
        src = ", ".join(s for s, v in where if v == stated)
        d.eq(G, f"`{name} = {stated}`", stated, stated, src)


def check_citations(d: Doc, w: dict) -> None:
    """Every `file:line` names a file that exists, with a line that exists.

    Bare continuations -- "`source/stage_night.cpp:21` and `:248`" -- hang off
    the last path named, so the last resolved path is carried forward.  A
    continuation with nothing before it is an error rather than a skip: that is
    a citation pointing at whatever the reader last had open.
    """
    G = "source citations"
    last: Path | None = None
    n_lines = 0
    for m in d.every(r"`([A-Za-z0-9_./-]*)((?::\d+(?:[-–]\d+)?)*)`"):
        path, nums = m.group(1), m.group(2)
        target: Path | None = None
        if path and ("/" in path or "." in path):
            for prefix in CITE_DIRS:
                candidate = ROOT / (prefix + path)
                if candidate.is_file():
                    target = candidate
                    break
            if target is not None:
                last = target
            elif nums:
                d.bad.append(f"line {d.at(m.start())}: `{path}{nums}` cites a "
                             f"file this cannot find under any of {CITE_DIRS}")
                continue
        if not nums:
            continue
        if target is None:
            target = last
        if target is None:
            d.bad.append(f"line {d.at(m.start())}: `{nums}` is a bare line "
                         f"citation with no file named before it")
            continue
        have = len(target.read_text().splitlines())
        for num in re.findall(r"\d+", nums):
            n_lines += 1
            if int(num) > have:
                d.bad.append(
                    f"line {d.at(m.start())}: `{path}{nums}` cites line {num} "
                    f"of {target.relative_to(ROOT)}, which has {have}")
    d.groups[G] = d.groups.get(G, 0) + n_lines


def check_divergence_files(d: Doc, w: dict) -> None:
    """Every divergence the document names is a file on disk."""
    G = "divergence files"
    have = sorted(p.name for p in DIVERGENCES.glob("*.md"))
    for m in d.every(r"`docs/behaviour/divergences/(\d{3})[a-z0-9-]*(?:\.md)?`"
                     r"|\bdivergence (\d{3})\b"):
        ident = m.group(1) or m.group(2)
        if not any(n.startswith(ident + "-") for n in have):
            d.bad.append(f"line {d.at(m.start())}: divergence {ident} has no "
                         f"file in docs/behaviour/divergences/")
            continue
        d.eq(G, f"divergence {ident}", True, True,
             next(n for n in have if n.startswith(ident + "-")))


def check_refusal_classes(d: Doc, w: dict) -> None:
    """R1 to R9: the classes build_doors.py refuses before it emits."""
    G = "the door generator's refusals"
    m = d.anchor(r"before it will emit anything, R(\d) to R(\d)",
                 "the range of refusal classes")
    if not m:
        return
    lo, hi = i(m, 1), i(m, 2)
    stated = {int(x) for x in re.findall(r"\(R(\d)\)", d.text)}
    for x in re.finditer(r"\(R(\d)\)", d.text):
        d.spans.append(x.span())
    source = {int(x) for x in
              re.findall(r"\bR([1-9]) --", (ROOT / "tools" / "build_doors.py")
                         .read_text())}
    d.eq(G, "the classes the document lists", stated, set(range(lo, hi + 1)),
         f"R{lo} to R{hi}")
    d.eq(G, "the classes build_doors.py refuses on", stated, source,
         "the 'RN --' labels in its own messages")


CHECKS = (check_extents, check_walkable, check_history, check_unique,
          check_door_row, check_stations, check_fall, check_cast,
          check_density, check_pool, check_quoted_constants,
          check_citations, check_divergence_files, check_refusal_classes)


# ---------------------------------------------------------------------------
# The census
# ---------------------------------------------------------------------------

def census(d: Doc) -> int:
    """Every digit in the document is checked, excused, or reported.

    This is what makes the file above a check on the DOCUMENT rather than a
    check on the fourteen things somebody remembered.  A number added to
    docs/WORLD_SIZES.md that nothing can recompute fails here, at its line, and
    the way to make it pass is to give it a source or to write down why it has
    none.  Both are better than the state this document was in.
    """
    used = 0
    for pattern, _reason in EXCUSED:
        hits = list(re.finditer(flex(pattern), d.text, re.M))
        if not hits:
            d.bad.append(
                f"nothing in the document matches the excuse /{pattern}/ any "
                f"more.  A stale exclusion is how a checker stops checking: "
                f"delete it, or fix it to match what the document now says")
            continue
        used += 1
        for m in hits:
            d.spans.append(m.span())

    unaccounted = []
    for m in re.finditer(r"\d+", d.text):
        if not d.covered(m.start(), m.end()):
            unaccounted.append((d.at(m.start()), m.group(0),
                                d.text[max(0, m.start() - 48):m.end() + 24]
                                .replace("\n", " ")))
    for line, num, ctx in unaccounted:
        d.bad.append(f"line {line}: {num} is stated and unchecked -- nothing "
                     f"here recomputes it and no excuse names it.  ...{ctx}...")
    return used


# ---------------------------------------------------------------------------

def main(argv=None) -> int:
    ap = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("-v", "--verbose", action="store_true",
                    help="print every figure as it is checked")
    args = ap.parse_args(argv)

    d = Doc(DOC.read_text())
    world = measure()
    for check in CHECKS:
        check(d, world)
    excused = census(d)

    if d.bad:
        print(f"docs/WORLD_SIZES.md does not hold up: {len(d.bad)} problem(s)")
        for b in d.bad:
            print(f"  {b}")
        return 1

    if args.verbose:
        for line in d.ok:
            print(line)
    total = sum(d.groups.values())
    print(f"worldsizes ok: {total} figures in docs/WORLD_SIZES.md recomputed "
          f"from the tree, {excused} excused with a reason")
    for group in sorted(d.groups):
        print(f"  {d.groups[group]:4}  {group}")
    print(f"  every one of the {len(re.findall(r'[0-9]+', d.text))} digit runs "
          f"in the file is accounted for: checked above, or excused with a "
          f"reason (-v prints them)")
    if args.verbose:
        for _pattern, reason in EXCUSED:
            print(f"  -  {reason}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
