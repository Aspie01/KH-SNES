#!/usr/bin/env python3
"""Walk the island the way the engine does and report what cannot be reached.

Every spawn point in the game is a tile coordinate written by hand, and the
one-step height rule means a single wrong character can seal off a deck or
strand an item over water.  That is expensive to find by playing and cheap to
find here, so the spawn tables are mirrored below and checked against a flood
fill that uses the engine's own rule: a move is allowed only onto a walkable
tile whose height is within MAX_STEP of the one being left.
"""
from __future__ import annotations

import re
import sys
from collections import deque
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from build_assets import (                  # noqa: E402
    ACT, PROP_ACTOR, TERRAIN, derive_props, ds_scenes, load_cast, load_grid,
)

MAX_STEP = 1

ROOT = Path(__file__).resolve().parent.parent

# What one DS screen can show at once, in 16 px tiles: 256x192 is 16x12, plus a
# partial tile on each axis because the camera does not stop on tile boundaries.
# This is the window the OBJ budget applies to -- OAM holds what is visible, not
# what exists, so it is the number a dense map has to answer for.
SCREEN_TILES = (17, 13)

# Where Sora wakes up, and everything the two days expect him to get to.
SPAWN = (11, 12)

REACH = {
    # day one
    "Kairi": (12, 12),
    "Riku": (27, 8),
    "Tidus": (7, 4),
    "Selphie": (14, 13),
    "Wakka": (19, 12),
    "log (past the footbridge)": (20, 12),
    "log (small island)": (28, 9),
    "cloth (treehouse)": (12, 4),
    "rope (lookout platform)": (8, 4),
    # day two
    "mushroom (behind the rock)": (9, 12),
    "mushroom (foot of the tower)": (6, 4),
    "mushroom (Secret Place)": (2, 7),
    "egg (treetop)": (21, 6),
    "bottle (under the fall)": (4, 7),
    # the Secret Place wall, which Sora has to stand in front of
    "chalk faces": (1, 6),
    "the door": (2, 6),
    "scribbles": (3, 6),
    # the race
    "paopu landing": (26, 7),
    # the night the island falls: where the Shadows come up, and where the
    # three of them are standing when it does
    "night Riku": (27, 8),
    "night Kairi": (2, 7),
    "shadow spot 1": (7, 9),
    "shadow spot 2": (17, 9),
    "shadow spot 3": (10, 10),
    "shadow spot 4": (5, 10),
    "shadow spot 5": (14, 11),
    "shadow spot 6": (19, 11),
    "shadow spot 7": (12, 8),
    "shadow spot 8": (26, 8),
    "shadow spot 9": (9, 6),
    "shadow spot 10": (15, 7),
}

# Blocked tiles are reached with the keyblade rather than by standing on them,
# so they are checked for a walkable neighbour instead.
ADJACENT = {
    "coconut palm (west)": (9, 10),
    "coconut palm (east)": (13, 10),
    "fish (west shallows)": (4, 12),
    "fish (south shallows)": (6, 13),
    "fish (inlet)": (17, 13),
    "paopu tree": (27, 7),
    "boulder": (15, 9),
    "bush": (5, 4),
    "small rock": (8, 12),
}


# The last piece of the island is its own map: Sora lands on it and Darkside
# rises on it, and both have to be standable.
FRAGMENT = {
    "Sora": (15, 12),
    "Darkside": (15, 7),
}


# Traverse Town.  Three districts joined by doors, and a door is a walkable
# tile in an otherwise solid row -- so a wrong character does not wall off a
# corner of one map, it strands the player in a district with no way out.
# Every door here is checked from both sides: the tile itself, and the tile on
# the far side that town.s stands Sora on after the swap.
TOWN1 = {
    "Sora": (14, 12),
    "Cid": (23, 6),
    "a townsman": (8, 9),
    "a townswoman": (18, 13),
    "the door to the Second District": (25, 4),
    "the landing from the Second District": (25, 5),
}

TOWN2 = {
    "Sora": (26, 5),
    "the door back to the First District": (26, 4),
    "the landing from the First District": (26, 5),
    "the door to the Third District": (5, 4),
    "the landing from the Third District": (5, 5),
    "shadow spot 1": (6, 6),
    "shadow spot 2": (20, 6),
    "shadow spot 3": (9, 9),
    "shadow spot 4": (22, 9),
    "shadow spot 5": (6, 12),
    "shadow spot 6": (24, 12),
    "shadow spot 7": (16, 13),
    "shadow spot 8": (11, 11),
}

TOWN3 = {
    "Sora": (16, 5),
    "the door back to the Second District": (16, 4),
    "the landing from the Second District": (16, 5),
    "Donald": (14, 9),
    "Goofy": (18, 9),
    "the Guard Armor": (16, 7),
}

# The lamp posts stand on their own blocked tiles, so like the palms they are
# checked for a walkable neighbour rather than for being stood on.
TOWN1_NEAR = {"lamp (west)": (3, 7), "lamp (east)": (27, 7)}
TOWN2_NEAR = {"lamp (west)": (3, 7), "lamp (east)": (28, 7)}
TOWN3_NEAR = {"lamp (west)": (7, 7), "lamp (east)": (24, 7)}


# ---------------------------------------------------------------------------
# The Nintendo DS worlds
#
# These are NOT mirrored by hand.  The scenes' spawn data lives in
# assets/ds/<scene>_cast.txt and the prop actors are derived from the map
# itself, so this file reads the same bytes the DS build will and the class of
# bug where the mirror drifts from the table cannot happen.
#
# What stays hand-written is the part that is an assertion about the map rather
# than data the game reads: routes.  "The bridge can be crossed" and "the cave
# can be entered" are claims about connectivity that no spawn table states, and
# they are exactly the claims an edit somewhere else quietly breaks.
# ---------------------------------------------------------------------------
DS_ROUTES = {
    "island": {
        "the cave passage": (11, 6),
        "the plunge pool": (11, 8),
        "the near end of the bridge": (44, 15),
        "the far end of the bridge": (52, 15),
        "the paopu landing": (55, 15),
        "where Kairi stands the night it falls": (8, 6),
        "the south end of the dock": (21, 26),
        # The treehouse floor, its treetop and the lookout deck are not listed:
        # the cloth, the egg and the rope stand on them, so the cast covers them.
    },
    # The night is the island's map, so the same connectivity has to hold -- but
    # for a different reason, and that reason is the whole scene: Riku is past
    # the bridge and Kairi is in the chamber, so if either route breaks the night
    # cannot be finished.  Their own tiles are checked as cast; these are the
    # steps in between, which no table mentions.
    "night": {
        "the cave passage": (11, 6),
        "the plunge pool": (11, 8),
        "the near end of the bridge": (44, 15),
        "the far end of the bridge": (52, 15),
    },
    "town1": {
        "the walkway (west end)": (30, 10),
        "the walkway (east end)": (43, 10),
        "the step up to the walkway (west)": (34, 13),
        "the step up to the walkway (east)": (39, 13),
        "the alley behind the walkway": (44, 8),
    },
    "town2": {
        "the walkway (north end)": (33, 9),
        "the walkway (south end)": (43, 14),
        "the step up to the walkway (west)": (36, 15),
        "the step up to the walkway (east)": (41, 15),
        "the alley behind the walkway": (44, 6),
    },
    "town3": {
        "the west walkway": (5, 10),
        "the east walkway": (42, 10),
        "the west step up (near)": (8, 12),
        "the west step up (far)": (15, 12),
        "the east step up (near)": (32, 12),
        "the east step up (far)": (39, 12),
        "the west alley": (3, 6),
        "the east alley": (44, 6),
        "the corridor down from the door": (23, 11),
        "the floor the Guard Armor lands on": (23, 18),
    },
}

# The blocked scenery Sora has to be able to stand beside without standing on:
# crates and the fountain rim.  Prop actors are not listed -- they come off the
# map, and the checker finds them there.
DS_ADJACENT = {
    "town1": {
        "crates (by the shop row)": (3, 6),
        "crates (south-west)": (4, 26),
        "crates (south-east)": (43, 26),
    },
    "town2": {
        "the fountain rim (north)": (18, 13),
        "the fountain rim (south)": (18, 20),
        "crates (by the Third District door)": (4, 6),
        "crates (the south wall)": (27, 27),
        "crates (south-east)": (43, 26),
    },
    "town3": {
        "crates (south-west)": (4, 26),
        "crates (south-east)": (43, 26),
    },
}


def ds_limits() -> tuple[int, int, int]:
    """MAX_ACTORS, TRANSIENT_ACTORS and the object budget, read from the header.

    Read rather than repeated: a pool budget checked against a stale copy of the
    pool size is worse than no check at all.
    """
    text = (ROOT / "platform" / "ds" / "include" / "constants.h").read_text()
    out = {}
    for name in ("MAX_ACTORS", "TRANSIENT_ACTORS", "OBJ_BUDGET_SCENERY"):
        m = re.search(rf"^constexpr int {name} = (\d+);", text, re.M)
        if not m:
            raise SystemExit(f"constants.h: no '{name}' to check the cast against")
        out[name] = int(m.group(1))
    return out["MAX_ACTORS"], out["TRANSIENT_ACTORS"], out["OBJ_BUDGET_SCENERY"]


def walk(grid, spawn: tuple[int, int]):
    """The engine's own reachability: walkable, and within MAX_STEP of height.

    Returns (walkable, height, reached), with reached None if the spawn itself
    is not standable -- there is nothing to say about a map nobody can enter.
    """
    W, H = grid.w, grid.h
    walkable, height = {}, {}
    for j in range(H):
        for i in range(W):
            code = TERRAIN[grid[j][i]]
            walkable[(i, j)], height[(i, j)] = code[3], code[4]
    if not walkable[spawn]:
        return walkable, height, None

    seen = {spawn}
    queue = deque([spawn])
    while queue:
        i, j = queue.popleft()
        for di, dj in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            n = (i + di, j + dj)
            if not (0 <= n[0] < W and 0 <= n[1] < H) or n in seen:
                continue
            if not walkable[n]:
                continue
            if abs(height[n] - height[(i, j)]) > MAX_STEP:
                continue
            seen.add(n)
            queue.append(n)
    return walkable, height, seen


def stacks(below: str, above: str) -> bool:
    """Is this pair of actors deliberately on one tile?

    Exactly one pair is: a dream weapon hovers over its dais, and dive.s places
    both at the same coordinates.  Nothing else may share a tile, so this is a
    whitelist rather than a relaxed rule -- the check it guards is what caught
    the Secret Place's mushroom sitting on the chalk faces.
    """
    return below == "Pedestal" and above in ("Sword", "Shield", "Staff")


def neighbours(grid, pos):
    i, j = pos
    return [(i + di, j + dj) for di, dj in ((1, 0), (-1, 0), (0, 1), (0, -1))
            if 0 <= i + di < grid.w and 0 <= j + dj < grid.h]


def check(label: str, grid, spawn: tuple[int, int],
          reach: dict, adjacent: dict) -> int:
    """Flood-fill one map with the engine's rule and check every spawn point.

    The map's dimensions come from the map, not from a constant: the DS worlds
    are larger than the SNES ones, and this checker has to serve both.
    """
    W, H = grid.w, grid.h
    walkable, height, seen = walk(grid, spawn)
    if seen is None:
        print(f"{label}: spawn {spawn} is not walkable "
              f"('{grid[spawn[1]][spawn[0]]}')")
        return 1

    bad = []
    for name, pos in reach.items():
        if pos not in seen:
            why = "blocked" if not walkable[pos] else "walled off"
            bad.append(f"  {name} at {pos} '{grid[pos[1]][pos[0]]}' -- {why}")
    for name, pos in adjacent.items():
        i, j = pos
        near = [(i + di, j + dj) for di, dj in ((1, 0), (-1, 0), (0, 1), (0, -1))]
        near = [n for n in near if 0 <= n[0] < W and 0 <= n[1] < H]
        if not any(n in seen for n in near):
            bad.append(f"  {name} at {pos} -- nothing walkable beside it")

    if bad:
        print(f"{label}: unreachable:")
        print("\n".join(bad))
        return 1

    total = sum(1 for v in walkable.values() if v)
    print(f"{label} ok: {W}x{H}, {len(seen)} of {total} walkable tiles reachable "
          f"from {spawn}, all {len(reach) + len(adjacent)} spawn points covered")
    return 0


def check_ds(scene, routes: dict, adjacent: dict) -> int:
    """Check one DS scene against its own cast file and the map it stands on.

    Nothing here is a mirror of anything: the props come off the map, the people
    and items and spots come out of assets/ds/<stem>_cast.txt, and the pool and
    object budgets come out of the DS headers.  What is checked:

      1. Sora is placed, and every walkable tile is reachable from him.  An
         unreachable pocket is not automatically wrong, but on these maps it has
         always been a mistake, so it is reported.
      2. Every actor on a walkable tile is reachable; every actor on a blocked
         one has a reachable neighbour.  Which case applies is derived from the
         tile, not declared -- so it cannot be declared wrongly.
      3. No two entries stand on the same tile, and nothing authored stands on a
         prop tile, which is blocked and would leave that actor stuck inside a
         tree.
      4. Doors and their landings are reachable, from both sides.
      5. The cast fits the actor pool, and no camera window holds more objects
         than the OBJ budget.
    """
    grid = scene.grid()
    cast = load_cast(scene.name)
    props = derive_props(grid, scene.props)
    label = f"ds/{scene.name}"

    sora = [(i, j) for name, i, j, _ in cast.base if name == "Sora"]
    if len(sora) != 1:
        print(f"{label}: {len(sora)} Sora in [base]; there must be exactly one")
        return 1
    spawn = sora[0]

    walkable, height, seen = walk(grid, spawn)
    if seen is None:
        print(f"{label}: Sora at {spawn} is not walkable "
              f"('{grid[spawn[1]][spawn[0]]}')")
        return 1

    bad = []

    # --- 2, 3: every placed thing, and nothing on top of anything else -------
    at: dict[tuple[int, int], str] = {}
    for who, rows in (("a prop", props), ("the cast", cast.actors)):
        for name, i, j, _ in rows:
            pos = (i, j)
            if not (0 <= i < grid.w and 0 <= j < grid.h):
                bad.append(f"  {name} at {pos} is off a {grid.w}x{grid.h} map")
                continue
            code = grid[j][i]
            if who == "the cast" and code in PROP_ACTOR:
                bad.append(f"  {name} at {pos} stands on '{code}', which is a "
                           f"{PROP_ACTOR[code]} -- blocked, so it would be "
                           f"stuck inside one")
            elif pos in at and not stacks(at[pos], name):
                bad.append(f"  {name} at {pos} is on top of {at[pos]}")
            elif walkable[pos]:
                if pos not in seen:
                    bad.append(f"  {name} at {pos} '{code}' -- walled off")
            elif not any(n in seen for n in neighbours(grid, pos)):
                bad.append(f"  {name} at {pos} '{code}' -- blocked, and nothing "
                           f"walkable beside it")
            at[pos] = name

    # --- the Heartless, the doors, and the routes ---------------------------
    for i, j in cast.spots:
        if (i, j) not in seen:
            why = "blocked" if not walkable[(i, j)] else "walled off"
            bad.append(f"  a Heartless spot at {(i, j)} "
                       f"'{grid[j][i]}' -- {why}")
    for di, dj, li, lj in cast.doors:
        for pos, what in (((di, dj), "a door"), ((li, lj), "its landing")):
            if pos not in seen:
                bad.append(f"  {what} at {pos} '{grid[pos[1]][pos[0]]}' -- "
                           f"{'blocked' if not walkable[pos] else 'walled off'}")
    for name, pos in routes.items():
        if pos not in seen:
            bad.append(f"  {name} at {pos} '{grid[pos[1]][pos[0]]}' -- "
                       f"{'blocked' if not walkable[pos] else 'walled off'}")
    for name, pos in adjacent.items():
        if not any(n in seen for n in neighbours(grid, pos)):
            bad.append(f"  {name} at {pos} -- nothing walkable beside it")

    # --- 5: the two budgets -------------------------------------------------
    max_actors, transient, obj_budget = ds_limits()
    peak = len(props) + cast.peak
    if peak + transient > max_actors:
        bad.append(f"  {peak} actors at peak + {transient} transient slots "
                   f"exceeds MAX_ACTORS = {max_actors}")

    # A camera window is what the hardware has to draw at once.  The pool holds
    # the whole map; OAM holds what is on screen, so this is the number that
    # matters and it is not the one the pool budget checks.
    win_w, win_h = SCREEN_TILES
    resident = props + cast.base + [r for t in cast.tables.values() for r in t]
    worst, where = 0, (0, 0)
    for j0 in range(max(1, grid.h - win_h + 1)):
        for i0 in range(max(1, grid.w - win_w + 1)):
            n = sum(1 for _, i, j, _ in resident
                    if i0 <= i < i0 + win_w and j0 <= j < j0 + win_h)
            if n > worst:
                worst, where = n, (i0, j0)
    if worst > obj_budget:
        bad.append(f"  {worst} objects inside the {win_w}x{win_h} window at "
                   f"{where} exceeds the OBJ budget of {obj_budget}")

    if bad:
        print(f"{label}: cast problems:")
        print("\n".join(bad))
        return 1

    total = sum(1 for v in walkable.values() if v)
    stranded = total - len(seen)
    print(f"{label} ok: {grid.w}x{grid.h}, {len(seen)} of {total} walkable tiles "
          f"reachable from Sora at {spawn}"
          f"{f' ({stranded} STRANDED)' if stranded else ''}, "
          f"{len(props)} props + {len(cast.base)} placed"
          f"{f' + {cast.peak - len(cast.base)} later' if cast.peak > len(cast.base) else ''}"
          f", {len(cast.spots)} spots, {len(cast.doors)} doors, "
          f"{len(routes) + len(adjacent)} routes; peak {peak}+{transient} of "
          f"{max_actors}, worst window {worst} of {obj_budget}")
    return 1 if stranded else 0


def main() -> int:
    bad = check("island", load_grid(), SPAWN, REACH, ADJACENT)
    bad |= check("fragment", load_grid("fragment.txt"), FRAGMENT["Sora"],
                 FRAGMENT, {})
    for name, reach, near in (("town1", TOWN1, TOWN1_NEAR),
                              ("town2", TOWN2, TOWN2_NEAR),
                              ("town3", TOWN3, TOWN3_NEAR)):
        bad |= check(name, load_grid(f"{name}.txt"), reach["Sora"], reach, near)

    # ...and the DS worlds, which are a different size, live beside them, and
    # carry their cast in data rather than in a table copied into this file.
    for scene in ds_scenes():
        bad |= check_ds(scene, DS_ROUTES.get(scene.name, {}),
                        DS_ADJACENT.get(scene.name, {}))
    return bad


if __name__ == "__main__":
    raise SystemExit(main())
