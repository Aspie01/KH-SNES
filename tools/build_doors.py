#!/usr/bin/env python3
"""Wire Traverse Town's doors, and refuse to emit if the wiring does not hold.

WHAT THIS IS FOR.  A DS door lives in two places that nothing joined up.  The
map and the cast file say WHERE it is -- a `d` tile in row DOOR_ROW and a
`[doors]` row beside it, emitted to assets/gen/ds/<scene>doors.bin as
(i, j, land_i, land_j).  Nothing anywhere said what is on the other side: that
was three more bytes a row on the SNES (platform/snes/src/town.s:42-44 --
"which map it is in, where in that map, where it leads, where to stand on the
far side, and the stage it wants") and scene.h:37-38 deliberately left it out of
the binary, because "which district it leads to is scene logic".

So the destination and the gate are authored in assets/ds/town_doors.txt and
this tool joins the two halves, checks nine ways that they agree, and emits
platform/ds/include/gen/doors.h.

THE FAILURE MODE THE WHOLE DESIGN IS SHAPED AROUND is not a wrong number, it is
a SILENT OMISSION.  A door in the map with nothing wiring it does not crash, it
does not warn, and it does not look different from a door that is deliberately
shut: the player walks onto it and nothing happens, and the only way to tell
"the author meant this" from "somebody forgot" is to ask the author.  Two of
Traverse Town's six doors ARE deliberately dead -- the Accessory Shop and the
Hotel, new content with no interior behind them -- which means the two states
are actually both present in this game and must be distinguishable by machine.
They are, because a `d` tile with no row in town_doors.txt is a build failure
(R1/R2) and a dead door is a row that says `shut` out loud.

WHY IT IS A NEW TOOL AND NOT AN EXTENSION OF THE PIPELINE.  tools/check_map.py
is frozen and its door check only reaches near-side reachability.  The
`[doors]` parser in tools/build_assets.py refuses a fifth field, an unknown
`[section]` becomes a cast table and is then rejected as actors, and an
unrecognised emitted table is refused -- so a new authored file read by a new
tool is the only route that touches no existing function body.  build_assets is
IMPORTED FROM and not edited, exactly as tools/check_map.py:20-21 already does.

WHY THE HEADER IS COMMITTED.  Same reason gen/scripts.h is: a clone can build
the DS tier without Python or the assembler, and assets/gen/ is gitignored so a
.bin would not survive one.  And for the same reason build_scripts.py has it,
`--check` belongs in Gate 0 -- a committed generated file that nothing re-checks
is a committed generated file that rots.

    python3 tools/build_doors.py            # emit
    python3 tools/build_doors.py --check     # fail if the header has drifted
"""
from __future__ import annotations

import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from build_assets import (                  # noqa: E402
    TERRAIN, ds_scenes, load_cast, load_grid,
)

ROOT = Path(__file__).resolve().parent.parent
SNES = ROOT / "platform" / "snes" / "src"
GEN = ROOT / "assets" / "gen" / "ds"
WIRING = ROOT / "assets" / "ds" / "town_doors.txt"
CONSTANTS = ROOT / "platform" / "ds" / "include" / "constants.h"
OUT = ROOT / "platform" / "ds" / "include" / "gen" / "doors.h"

# The literal a wiring row uses for "there is no far side".  Not a SceneId and
# not a district name: it has to be unmistakable in the authored file, because
# the entire point of declaring a shuttered door is that a reader can tell it
# from an omission at a glance.
SHUT = "shut"
NO_GATE = "-"

# The engine's own step rule, and the reason it is repeated rather than shared:
# tools/check_map.py:24 has the same constant for the same reason and is frozen,
# and constants.h:114 has it as MAX_STEP with the citation.  Checked against the
# header below rather than trusted.
MAX_STEP = 1


# ---------------------------------------------------------------------------
# Reading the frozen definitions, rather than repeating them
# ---------------------------------------------------------------------------
def snes_equates() -> dict[str, int]:
    """Every `NAME = <number>` equate in game.inc.

    SCENE_TOWN1, T_SECOND and DOOR_ROW are read out of the frozen assembly and
    not restated here, because a wiring table checked against a stale copy of
    the scene numbering is worse than no check at all -- it would agree with
    itself and with nothing else.
    """
    text = (SNES / "game.inc").read_text()
    out: dict[str, int] = {}
    for m in re.finditer(r"^(\w+)\s*=\s*(\$?[0-9A-Fa-f]+)\s*(?:;.*)?$", text, re.M):
        raw = m.group(2)
        try:
            out[m.group(1)] = int(raw[1:], 16) if raw[0] == "$" else int(raw, 10)
        except ValueError:
            continue
    for need in ("DOOR_ROW", "SCENE_TOWN1", "T_ARRIVE"):
        if need not in out:
            raise SystemExit(f"game.inc: no '{need}' -- the wiring cannot be "
                             f"resolved against the frozen constants")
    return out


def cpp_enum(name: str) -> dict[str, int]:
    """One `enum class NAME : type { ... };` out of the DS constants header."""
    text = CONSTANTS.read_text()
    m = re.search(rf"enum class {name}\s*:\s*\w+\s*\{{(.*?)\}};", text, re.S)
    if not m:
        raise SystemExit(f"constants.h: no 'enum class {name}' to emit against")
    body = re.sub(r"//[^\n]*", "", m.group(1))
    return {k: int(v) for k, v in re.findall(r"(\w+)\s*=\s*(\d+)", body)}


def snes_door_rows(eq: dict[str, int]) -> list[tuple[int, int, int, int, int, int, int]]:
    """doorTable out of town.s, as (from, i, dest, land_i, land_j, needs, line).

    Parsed live from the frozen source rather than transcribed into a comment,
    because R9 below compares the DS's routing against it: a promise in prose
    that the two agree is a promise, and this is a check.

    THE LINE NUMBER IS PART OF THE ROW and is carried out of here rather than
    reconstructed by the callers, because both of them CITE it -- R9's refusal
    names the doorTable row the DS has come to contradict, and every routed row
    of the emitted header carries a `town.s:NNNN` comment that a reader is
    expected to be able to open.  Reconstructing it as `1391 + k` would work
    today and would be a fabricated citation the moment anything is inserted
    above doorTable or a blank line appears between its rows: the header would
    still build, the numbers would still look like citations, and they would
    point at the wrong lines.  A citation that does not support its claim is
    worse than no citation, so the one number that must never be invented here
    is derived from the same match that read the row.
    """
    lines = (SNES / "town.s").read_text().splitlines()
    try:
        start = next(n for n, ln in enumerate(lines) if ln.startswith("doorTable:"))
    except StopIteration:
        raise SystemExit("town.s: no doorTable -- has the oracle moved?")
    rows = []
    for n in range(start + 1, len(lines)):
        b = re.match(r"^\s*\.byte\s+(.*?)\s*(?:;.*)?$", lines[n])
        if not b:
            break
        toks = [t.strip() for t in b.group(1).split(",")]
        if toks == ["$FF"]:
            break
        if len(toks) != 7:
            raise SystemExit(f"town.s:{n + 1}: doorTable row has {len(toks)} "
                             f"fields, not the seven of town.s:42-44")
        vals = []
        for t in toks:
            if t in eq:
                vals.append(eq[t])
            elif re.fullmatch(r"\d+", t):
                vals.append(int(t))
            else:
                raise SystemExit(f"town.s:{n + 1}: cannot resolve '{t}'")
        rows.append((vals[0], vals[1], vals[3], vals[4], vals[5], vals[6], n + 1))
    if not rows:
        raise SystemExit("town.s: doorTable is empty")
    return rows


# ---------------------------------------------------------------------------
# The authored wiring
# ---------------------------------------------------------------------------
class Wire:
    """One row of assets/ds/town_doors.txt, before the landing is derived."""

    __slots__ = ("frm", "i", "j", "to", "needs", "line")

    def __init__(self, frm, i, j, to, needs, line):
        self.frm = frm          # district name
        self.i = i
        self.j = j
        self.to = to            # district name, or SHUT
        self.needs = needs      # a T_* name, or None when shuttered
        self.line = line        # town_doors.txt:<n>, for the refusal message

    @property
    def routed(self) -> bool:
        return self.to != SHUT

    @property
    def where(self) -> str:
        return f"{WIRING.name}:{self.line}"


def load_wiring(districts: tuple[str, ...], stages: dict[str, int]) -> list[Wire]:
    rows: list[Wire] = []
    # R2's multiplicity half.  check_pairing() below compares the cast's doors
    # and the wiring's doors as SETS, which answers "is every door mentioned in
    # both places" and is blind to a tile mentioned TWICE here.  That blindness
    # is not academic and it is the worst-behaved failure this tool can have: a
    # second row for a tile that is already wired passes R1 (the tile really is
    # a 'd'), passes R2 (the sets are still equal), passes R5 (the reciprocal
    # search walks `wires` and still finds exactly one door back), passes R8 and
    # passes R9 (the routing graph still contains the join the first row made) --
    # and then emit() indexes by_tile, which is a dict, so the LAST row silently
    # wins the emitted table.  Append `town1 24 4 shut -` under the real row and
    # the generator prints "7 doors ... every (from, to, gate) matched", exits
    # zero, and writes a header in which the only way out of the First District
    # is a shop front.  Every refusal above is about a wiring that says the wrong
    # thing; this one is about a wiring that says two things, where the check
    # reads one of them and the emitter reads the other.
    seen: dict[tuple[str, int, int], int] = {}
    for n, raw in enumerate(WIRING.read_text().splitlines(), 1):
        line = raw.split("#", 1)[0].strip()
        if not line:
            continue
        f = line.split()
        where = f"{WIRING.name}:{n}"
        if len(f) != 5:
            raise SystemExit(f"{where}: a wiring row is 'from i j to needs', "
                             f"got {len(f)} fields -- {line!r}")
        frm, i, j, to, needs = f[0], f[1], f[2], f[3], f[4]
        if frm not in districts:
            raise SystemExit(f"{where}: '{frm}' is not a district; the districts "
                             f"are {', '.join(districts)}")
        # R4.  A typo in a destination must not become a fourth district by
        # accident, and it must not become a shuttered door by accident either:
        # the only two things a destination may say are a district that exists
        # and the literal '{SHUT}'.
        if to not in districts and to != SHUT:
            raise SystemExit(f"{where}: R4 -- '{to}' is neither a district nor "
                             f"'{SHUT}'.  There is no fourth SceneId and no "
                             f"interior map: a door either leads to one of "
                             f"{', '.join(districts)} or is declared shuttered")
        if not re.fullmatch(r"\d+", i) or not re.fullmatch(r"\d+", j):
            raise SystemExit(f"{where}: '{i} {j}' is not a tile")
        key = (frm, int(i), int(j))
        if key in seen:
            raise SystemExit(
                f"{where}: R2 -- {frm} ({i},{j}) is already wired at "
                f"{WIRING.name}:{seen[key]}.  One door is one row: the checks "
                f"below compare the two halves of the table as sets and would "
                f"not notice, and the emitter takes whichever row comes last, "
                f"so a second row for a tile silently decides what the door "
                f"does while everything that is supposed to be guarding it "
                f"reads the first one and agrees")
        seen[key] = n
        # R7, half of it.  The three columns have to agree about what kind of
        # row this is, or a shuttered door acquires a gate that reads like
        # permission and a routed door loses the one thing that gates it.
        if to == SHUT:
            if needs != NO_GATE:
                raise SystemExit(
                    f"{where}: R7 -- a shuttered door names the gate "
                    f"'{needs}'.  It has no far side, so there is no TownStage "
                    f"that could ever open it; write '{NO_GATE}' and let the "
                    f"'{SHUT}' destination be the only thing that says so")
            rows.append(Wire(frm, int(i), int(j), to, None, n))
            continue
        if needs == NO_GATE:
            raise SystemExit(
                f"{where}: R7 -- a door to {to} with no gate.  Every routed "
                f"door carries the stage the town must have reached; the "
                f"always-open ones say T_ARRIVE (town.s:1392, :1394) rather "
                f"than saying nothing")
        if needs not in stages:
            raise SystemExit(f"{where}: '{needs}' is not a townStage equate in "
                             f"game.inc")
        rows.append(Wire(frm, int(i), int(j), to, needs, n))
    if not rows:
        raise SystemExit(f"{WIRING.name}: no wiring rows")
    return rows


# ---------------------------------------------------------------------------
# The checks
# ---------------------------------------------------------------------------
def check_tiles(wires: list[Wire], grids: dict, casts: dict,
                door_row: int) -> None:
    """R1 and R3: the map, the cast file and the wiring name the same doors."""
    for w in wires:
        grid = grids[w.frm]
        if not (0 <= w.i < grid.w and 0 <= w.j < grid.h):
            raise SystemExit(f"{w.where}: R1 -- ({w.i},{w.j}) is off "
                             f"{w.frm}.txt, which is {grid.w}x{grid.h}")
        code = grid[w.j][w.i]
        if code != "d":
            raise SystemExit(
                f"{w.where}: R1 -- {w.frm} ({w.i},{w.j}) is '{code}', not a 'd' "
                f"doorway.  A wiring row for a tile that is not a door wires "
                f"nothing: the interaction layer matches on the tile the player "
                f"is standing on, so this door would never fire")
        # R3.  DOOR_ROW is not a convention, it is the only row of a building
        # block that shows a face (town.s:1386-1387); a door anywhere else is
        # painted onto the pavement or covered by the block above it.
        if w.j != door_row:
            raise SystemExit(f"{w.where}: R3 -- ({w.i},{w.j}) is not in "
                             f"DOOR_ROW = {door_row}")

    # ...and the other direction: every 'd' tile in a district map has to be
    # accounted for.  This is the omission check -- a door drawn into the art
    # with nothing behind it is exactly what a reader cannot distinguish from a
    # door that is meant to be dead.
    wired = {(w.frm, w.i, w.j) for w in wires}
    for name, grid in grids.items():
        for j in range(grid.h):
            for i in range(grid.w):
                if grid[j][i] != "d":
                    continue
                if (name, i, j) not in wired:
                    raise SystemExit(
                        f"R1 -- {name}.txt has a 'd' doorway at ({i},{j}) with "
                        f"no row in {WIRING.name}.  Every door is wired or is "
                        f"declared shuttered; there is no third state, because "
                        f"'nothing happens' has to be a decision and not an "
                        f"oversight")

    # R3's other half: the NEAR-side landing in <scene>doors.bin.  This is a
    # different tile from TownDoor::landing and the two must never be
    # conflated -- the binary's is beside the door in the door's OWN map, and
    # the header's is on the far side, in the destination's.
    for name, cast in casts.items():
        for (di, dj, li, lj) in cast.doors:
            if dj != door_row:
                raise SystemExit(f"{name}_cast.txt: R3 -- a [doors] row at "
                                 f"({di},{dj}) is not in DOOR_ROW = {door_row}")
            if (li, lj) != (di, door_row + 1):
                raise SystemExit(
                    f"{name}_cast.txt: R3 -- the door at ({di},{dj}) lands on "
                    f"({li},{lj}); a [doors] landing is the NEAR side and is "
                    f"always (i, DOOR_ROW + 1), which test_scene.cpp's "
                    f"scene_reads_the_spot_and_door_tables "
                    f"asserts of the emitted binary")


def check_pairing(wires: list[Wire], casts: dict) -> None:
    """R2: the cast file's doors and the wiring's doors are the same set.

    THE DRIFT THIS WHOLE DESIGN EXISTS TO PREVENT.  <scene>doors.bin is
    regenerated from the cast file by build_assets.py and the header is
    generated from the wiring by this tool; nothing else compares them.  Edit
    one and not the other and the two tables are the same length, so every
    index still resolves, and the game quietly opens the wrong door.
    """
    for name, cast in casts.items():
        in_cast = {(di, dj) for (di, dj, _, _) in cast.doors}
        in_wiring = {(w.i, w.j) for w in wires if w.frm == name}
        missing = sorted(in_cast - in_wiring)
        extra = sorted(in_wiring - in_cast)
        if missing:
            raise SystemExit(
                f"R2 -- {name} has a [doors] row at "
                f"{', '.join(str(t) for t in missing)} with nothing in "
                f"{WIRING.name}.  That door is in the emitted "
                f"{name}doors.bin and would be a tile the player can stand on "
                f"that does nothing, with no record of anybody deciding so")
        if extra:
            raise SystemExit(
                f"R2 -- {WIRING.name} wires {name} "
                f"{', '.join(str(t) for t in extra)}, which is not in "
                f"{name}_cast.txt's [doors].  The header and "
                f"{name}doors.bin index the same table, so a row here with no "
                f"row there puts the two permanently out of step")


def derive_landings(wires: list[Wire], door_row: int) -> dict[int, tuple[int, int]]:
    """R5, and the far-side landing that comes out of it.

    town.s:1386-1389: "every landing is the tile directly south of the door on
    the far side, so a player who walks straight through comes out facing the
    square."  That makes the landing a FUNCTION of the reciprocal door rather
    than an independent coordinate, so it is derived here and never authored.
    The SNES's four rows obey it and the DS's must, and deriving it is the only
    way to make that a fact rather than a habit.
    """
    out: dict[int, tuple[int, int]] = {}
    for n, w in enumerate(wires):
        if not w.routed:
            continue
        back = [b for b in wires
                if b.routed and b.frm == w.to and b.to == w.frm]
        if len(back) != 1:
            raise SystemExit(
                f"{w.where}: R5 -- {w.frm} ({w.i},{w.j}) leads to {w.to}, and "
                f"{w.to} has {len(back)} doors back to {w.frm}, not one.  The "
                f"landing IS the reciprocal door's tile (town.s:1386-1389), so "
                f"with none there is nowhere to put Sora down and with two "
                f"there is no way to choose -- and a door out of a district "
                f"with no way back is a district the player is stranded in")
        out[n] = (back[0].i, door_row + 1)
    return out


def check_standable(wires: list[Wire], landings: dict, grids: dict) -> None:
    """R6: the derived landing is somewhere Sora can actually be put down.

    Read out of the EMITTED assets/gen/ds/<scene>coll.bin and height.bin rather
    than off the text map, for the same reason build_scripts.py compares against
    the ROM instead of against the source: the binary is the artefact the engine
    loads, and checking the thing that produced it against itself proves only
    that it is self-consistent.

    A landing on a blocked tile is not a visible bug at the moment it happens --
    Sora is simply standing inside a wall, and the collision test only runs on
    the next MOVE, so the symptom appears somewhere else entirely.
    """
    for n, w in enumerate(wires):
        if n not in landings:
            continue
        li, lj = landings[n]
        dest = w.to
        coll = read_plane(dest, "coll", grids[dest])
        hmap = read_plane(dest, "height", grids[dest])
        src_h = read_plane(w.frm, "height", grids[w.frm])
        gw = grids[dest].w
        if coll[lj * gw + li] == 0:
            raise SystemExit(
                f"{w.where}: R6 -- the landing ({li},{lj}) in {dest} is blocked "
                f"in {dest}coll.bin.  A door that puts Sora inside a wall does "
                f"not fail where it happens: nothing tests collision on a "
                f"placement, only on the next move")
        step = abs(int(hmap[lj * gw + li])
                   - int(src_h[w.j * grids[w.frm].w + w.i]))
        if step > MAX_STEP:
            raise SystemExit(
                f"{w.where}: R6 -- the landing ({li},{lj}) in {dest} is {step} "
                f"height steps from the door at ({w.i},{w.j}) in {w.frm}, and "
                f"MAX_STEP is {MAX_STEP}.  A door is a teleport and not a move, "
                f"so the engine would allow it -- which is exactly why it needs "
                f"checking here: walking through a doorway and arriving two "
                f"storeys up reads as the scene having loaded wrong")


def read_plane(scene: str, kind: str, grid) -> bytes:
    path = GEN / f"{scene}{kind}.bin"
    if not path.exists():
        raise SystemExit(
            f"{path} is missing -- run `python3 tools/build_assets.py` first.\n"
            f"The standability check is not optional: without the emitted "
            f"planes this tool would be checking the wiring against the same "
            f"text map the wiring was written from.")
    data = path.read_bytes()
    if len(data) != grid.w * grid.h:
        raise SystemExit(f"{path}: {len(data)} bytes for a "
                         f"{grid.w}x{grid.h} map")
    return data


def check_ladder(wires: list[Wire], districts: tuple[str, ...],
                 eq: dict[str, int], entered: str) -> dict[str, int]:
    """R8: nobody is stranded and nobody is stuck, and the ladder is reported.

    Two halves, and the second is the one that is easy to get wrong.

    ANTI-STRAND is a district nothing leads to.  ANTI-SOFT-LOCK is a district
    with nothing but shuttered doors out of it, which is the specific hazard the
    two new shop fronts introduce: they are the first doors in the game that
    lead nowhere, and a district that acquired a third one and lost its exit
    would be a room the player walks into and cannot leave.  Neither shows up in
    a test that only drives one door at a time.

    The gate LADDER -- which districts a player at each TownStage can get to --
    is computed and printed rather than asserted, because R9 has already pinned
    the routed graph to doorTable's, so the ladder is the SNES's ladder by
    construction and an assertion here would be checking this tool against
    itself.  The shape of it is asserted where it is worth asserting: against
    the real emitted tables, in test_doors.cpp.
    """
    for name in districts:
        mine = [w for w in wires if w.frm == name]
        if not mine:
            raise SystemExit(f"R8 -- {name} has no doors at all")
        if not any(w.routed for w in mine):
            raise SystemExit(
                f"R8 -- every door out of {name} is shuttered, so the player "
                f"cannot leave it.  A shop front is content; a district with "
                f"nothing but shop fronts is a soft lock")
        if name != entered and not any(w.routed and w.to == name
                                       for w in wires):
            raise SystemExit(
                f"R8 -- nothing leads to {name}, and it is not the district "
                f"the town is entered at.  It would be unreachable")

    # The first TownStage at which each district can be walked to from the one
    # the town is entered at.  A district that never becomes reachable is a
    # district behind a gate nothing sets.
    top = max(v for k, v in eq.items() if k.startswith("T_"))
    first: dict[str, int] = {}
    for s in range(top + 1):
        seen = {entered}
        again = True
        while again:
            again = False
            for w in wires:
                if not w.routed or w.frm not in seen or w.to in seen:
                    continue
                if eq[w.needs] > s:
                    continue
                seen.add(w.to)
                again = True
        for name in seen:
            first.setdefault(name, s)
    for name in districts:
        if name not in first:
            raise SystemExit(
                f"R8 -- {name} is never reachable from {entered}, at any "
                f"TownStage.  Its way in is gated on a stage nothing sets")
    return first


def check_fidelity(wires: list[Wire], eq: dict[str, int],
                   snes_rows: list) -> None:
    """R9: the DS routes exactly what doorTable routed, gate for gate.

    THE LOAD-BEARING CHECK.  Everything above proves the DS's wiring is
    self-consistent; this is the only thing that proves it is the SNES's.  The
    tiles moved -- the districts were redrawn 32x16 to 48x32 -- so the tile
    columns cannot be compared, but the (source district, destination district,
    gate) triple is exactly what did not move and exactly what a port silently
    gets wrong.  A DS gate cannot now be edited without this naming the
    doorTable row it has come to contradict.
    """
    def scene_of(name: str) -> int:
        key = f"SCENE_{name.upper()}"
        if key not in eq:
            raise SystemExit(f"game.inc has no {key}")
        return eq[key]

    ds = {}
    for w in wires:
        if not w.routed:
            continue
        ds[(scene_of(w.frm), scene_of(w.to))] = (eq[w.needs], w)
    snes = {}
    for (frm, _i, dest, _li, _lj, needs, line) in snes_rows:
        snes[(frm, dest)] = (needs, line)

    for key, (needs, line) in snes.items():
        if key not in ds:
            raise SystemExit(
                f"R9 -- town.s:{line} routes scene {key[0]} to scene {key[1]} "
                f"and the DS has no such door.  The districts were redrawn, "
                f"not rejoined: every join in doorTable is still a join here")
        if ds[key][0] != needs:
            got = ds[key][1]
            name = next((n for n, v in eq.items()
                         if n.startswith("T_") and v == needs), str(needs))
            raise SystemExit(
                f"{got.where}: R9 -- {got.frm} -> {got.to} is gated on "
                f"{got.needs} ({eq[got.needs]}); town.s:{line} gates the same "
                f"join on {name} ({needs}).  The gate is fidelity, not a tuning "
                f"knob: it is what makes townStage progress and not location")
    for key, (_needs, w) in ds.items():
        if key not in snes:
            raise SystemExit(
                f"{w.where}: R9 -- {w.frm} -> {w.to} is a join doorTable never "
                f"had.  A new route between existing districts is a second way "
                f"round whatever gate the first one carried")


# ---------------------------------------------------------------------------
# Emission
# ---------------------------------------------------------------------------
def cpp_scene(name: str) -> str:
    """town1 -> Town1, the SceneId enumerator."""
    return name[0].upper() + name[1:]


def cpp_stage(t: str) -> str:
    """T_SECOND -> Second, the TownStage enumerator."""
    stem = t[2:].lower()
    return stem[0].upper() + stem[1:]


def emit(districts, casts, wires, landings, eq, snes_rows) -> str:
    by_tile = {(w.frm, w.i, w.j): (n, w) for n, w in enumerate(wires)}
    # (from scene, to scene) -> the town.s line the row is actually on, read out
    # of the file by snes_door_rows.  See its docstring for why this is not
    # `1391 + k`: these end up in the header as citations.
    snes_line = {(row[0], row[2]): row[6] for row in snes_rows}

    L: list[str] = []
    w_ = L.append
    w_("#pragma once")
    w_("// GENERATED by tools/build_doors.py -- do not edit.")
    w_("//")
    w_("// Traverse Town's doorTable, for the 48x32 districts.  The other three")
    w_("// columns of platform/snes/src/town.s:42-44's seven-byte row -- where a")
    w_("// door leads, where it puts you down, and the stage the town has to have")
    w_("// reached -- which assets/gen/ds/<scene>doors.bin deliberately does not")
    w_("// carry (scene.h:37-38).  Authored in assets/ds/town_doors.txt.")
    w_("//")
    w_("// THE ROWS ARE IN <scene>doors.bin ORDER, so this header and that binary")
    w_("// are the same table and compare index for index.  A host test asserts")
    w_("// exactly that, because they are generated by two different tools from two")
    w_("// different files and nothing else would notice them drifting apart.")
    w_("//")
    w_("// THE LANDING IS DERIVED, NEVER AUTHORED.  town.s:1386-1389: \"every")
    w_("// landing is the tile directly south of the door on the far side, so a")
    w_("// player who walks straight through comes out facing the square.\"  So it")
    w_("// is the reciprocal door's own tile, and the generator refuses if a routed")
    w_("// door does not have exactly one reciprocal.")
    w_("//")
    w_("// NOTE: TownDoor::landing is the FAR side, in the destination's map.")
    w_("// <scene>doors.bin's land_i/land_j are the NEAR side, beside the door in")
    w_("// its own map -- always (i, DOOR_ROW + 1), which test_scene.cpp's")
    w_("// scene_reads_the_spot_and_door_tables")
    w_("// asserts.  They are different tiles and no code may copy one into the")
    w_("// other.")
    w_("//")
    w_("// A SHUTTERED DOOR has `to = SceneId::Count`: NEW content with no far side")
    w_("// anywhere in the tree -- no fourth SceneId, no interior map, no cast, no")
    w_("// .bin -- and no line in the frozen ROM it could say without lying, since")
    w_("// build_scripts.py checks every script byte for byte against kh.sfc and a")
    w_("// new one cannot be written.  Its `needs` below is inert: townInteract")
    w_("// tests `to == SceneId::Count` BEFORE the gate, so no TownStage can ever")
    w_("// open one.  It is recorded here rather than left out so that \"nothing")
    w_("// happens\" is a decision on the record and never the silent result of a")
    w_("// door somebody forgot to wire.")
    w_("//")
    w_("// Regenerate with:")
    w_("//")
    w_("//     python3 tools/build_assets.py && python3 tools/build_doors.py")
    w_("")
    w_('#include "interact.h"')
    w_("")
    w_("namespace kh {")
    w_("namespace door {")
    w_("")

    for name in districts:
        rows = casts[name].doors
        pretty = {"town1": "First", "town2": "Second",
                  "town3": "Third"}.get(name, name)
        w_(f"// The {pretty} District.  {len(rows)} door"
           f"{'' if len(rows) == 1 else 's'}, in {name}doors.bin order.")
        w_(f"constexpr TownDoor {name.upper()}[] = {{")
        for (di, dj, _li, _lj) in rows:
            n, wire = by_tile[(name, di, dj)]
            if not wire.routed:
                w_(f"    // ({di},{dj}) -- NEW content, shuttered.  "
                   f"{WIRING.name}:{wire.line}.")
                w_("    // The landing is EMPTY and not the near-side tile: a")
                w_("    // TownDoor landing is the far side, and there is no far")
                w_("    // side, so writing (i, DOOR_ROW + 1) here would be the")
                w_("    // one conflation this table exists to prevent.  The")
                w_("    // TownStage is inert -- townInteract returns on")
                w_("    // `to == SceneId::Count` before it reaches the gate.")
                w_(f"    {{{{{di}, DOOR_ROW}}, SceneId::Count, {{}}, "
                   f"TownStage::Arrive}},")
                continue
            li, lj = landings[n]
            src = f"SCENE_{wire.frm.upper()}"
            dst = f"SCENE_{wire.to.upper()}"
            line = snes_line[(eq[src], eq[dst])]
            w_(f"    // town.s:{line} -- {src}, ..., {dst}, ..., {wire.needs}."
               f"  Landing derived")
            w_(f"    // from the reciprocal door at {wire.to} "
               f"({li},{lj - 1}); tile from {wire.frm}_cast.txt.")
            w_(f"    {{{{{di}, DOOR_ROW}}, SceneId::{cpp_scene(wire.to)}, "
               f"{{{li}, DOOR_ROW + 1}}, TownStage::{cpp_stage(wire.needs)}}},")
        w_("};")
        w_(f"static_assert(sizeof {name.upper()} / sizeof {name.upper()}[0] "
           f"== {len(rows)},")
        w_(f"              \"{name}_cast.txt's [doors] has {len(rows)} "
           f"row{'' if len(rows) == 1 else 's'}\");")
        w_("")
    w_("}  // namespace door")
    w_("")
    w_("// Which district's table to use.  A scene that is not one of the three has")
    w_("// no doors and gets an empty span rather than a default: the interaction")
    w_("// layer walks `count` rows and zero is the correct number for the island.")
    w_("struct TownDoors {")
    w_("    const TownDoor* rows = nullptr;")
    w_("    int count = 0;")
    w_("};")
    w_("")
    w_("constexpr TownDoors townDoorsFor(SceneId s) {")
    for k, name in enumerate(districts):
        lead = "    return" if k == 0 else "         :"
        w_(f"{lead} s == SceneId::{cpp_scene(name)} "
           f"? TownDoors{{door::{name.upper()}, {len(casts[name].doors)}}}")
    w_("         : TownDoors{};")
    w_("}")
    w_("")
    w_("}  // namespace kh")
    return "\n".join(L) + "\n"


# ---------------------------------------------------------------------------
def main() -> int:
    eq = snes_equates()
    door_row = eq["DOOR_ROW"]

    # The DS's own MAX_STEP and scene numbering, checked rather than assumed.
    if int(re.search(r"^constexpr int MAX_STEP = (\d+);",
                     CONSTANTS.read_text(), re.M).group(1)) != MAX_STEP:
        raise SystemExit("constants.h: MAX_STEP has moved; this tool's copy is "
                         "stale and its landing check would be wrong")
    scenes = cpp_enum("SceneId")
    stages_h = cpp_enum("TownStage")

    # The districts are the scenes that HAVE doors, discovered from the pipeline
    # rather than listed: a fourth district would then be checked the day its
    # cast file appears, which is the day it can first be wrong.
    casts = {}
    grids = {}
    for scene in ds_scenes():
        cast = load_cast(scene.name)
        if not cast.doors:
            continue
        casts[scene.name] = cast
        grids[scene.name] = scene.grid()
    districts = tuple(casts)
    if not districts:
        raise SystemExit("no scene has a [doors] section")

    for name in districts:
        key = f"SCENE_{name.upper()}"
        if key not in eq:
            raise SystemExit(f"game.inc has no {key} for district {name}")
        if scenes.get(cpp_scene(name)) != eq[key]:
            raise SystemExit(
                f"SceneId::{cpp_scene(name)} = {scenes.get(cpp_scene(name))} "
                f"but {key} = {eq[key]}; the two numberings have diverged and "
                f"the emitted header would name the wrong district")

    stages = {k: v for k, v in eq.items() if k.startswith("T_")}
    for t, v in stages.items():
        if stages_h.get(cpp_stage(t)) != v:
            raise SystemExit(f"TownStage::{cpp_stage(t)} does not equal {t}")

    wires = load_wiring(districts, stages)
    check_tiles(wires, grids, casts, door_row)
    check_pairing(wires, casts)
    landings = derive_landings(wires, door_row)
    check_standable(wires, landings, grids)
    # The town is entered at the First District, from the night --
    # SceneAction::EnterTown, stage.h:125 -- which is why nothing has to lead
    # into it and why its [base] Sora is mid-plaza rather than on a landing.
    #
    # STRUCTURE BEFORE FIDELITY, deliberately.  A wiring that strands the player
    # and a wiring that contradicts doorTable are both refusals, but they are not
    # equally bad and the message that comes out should be the worse one: R9 is
    # "this is not the game the SNES shipped", R8 is "this is not a game".  It
    # also keeps R8 reachable at all -- R9 pins the routed graph to doorTable's,
    # so with the three districts as they stand, anything R8 could catch would
    # have tripped R9 first and the anti-strand check would be dead code that
    # only wakes up for a fourth district.
    first = check_ladder(wires, districts, eq, districts[0])
    snes_rows = snes_door_rows(eq)
    check_fidelity(wires, eq, snes_rows)

    routed = sum(1 for w in wires if w.routed)
    print(f"doors:    {len(wires)} doors across {len(districts)} districts, "
          f"{routed} routed and {len(wires) - routed} shuttered")
    print(f"          {len(snes_rows)} doorTable rows in town.s, every "
          f"(from, to, gate) matched")
    for n, w in enumerate(wires):
        if w.routed:
            li, lj = landings[n]
            print(f"          {w.frm} ({w.i},{w.j}) -> {w.to} ({li},{lj}) "
                  f"needs {w.needs}")
        else:
            print(f"          {w.frm} ({w.i},{w.j}) -> shuttered")
    stage_name = {v: k for k, v in eq.items() if k.startswith("T_")}
    print("          reachable from " + districts[0] + ": "
          + ", ".join(f"{n} at {stage_name[s]}"
                      for n, s in sorted(first.items(), key=lambda kv: kv[1])))

    OUT.parent.mkdir(parents=True, exist_ok=True)
    text = emit(districts, casts, wires, landings, eq, snes_rows)
    same = OUT.exists() and OUT.read_text() == text

    if "--check" in sys.argv:
        if same:
            print(f"          {OUT.relative_to(ROOT)} is up to date")
            return 0
        print(f"{OUT.relative_to(ROOT)} has drifted from "
              f"{WIRING.relative_to(ROOT)}.\n"
              f"Run: python3 tools/build_doors.py", file=sys.stderr)
        return 1

    if same:
        print(f"          {OUT.relative_to(ROOT)} unchanged")
    else:
        OUT.write_text(text)
        print(f"          wrote {OUT.relative_to(ROOT)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
