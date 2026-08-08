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

import sys
from collections import deque
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from build_assets import TERRAIN, load_grid   # noqa: E402

MAX_STEP = 1

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
# The Nintendo DS island: 64x32, four times the area, so every spawn point had
# to be re-placed.  This is the table that proves the expansion did not strand
# anything -- the Secret Place in particular, which is now sealed on three sides
# and entered only by wading round the waterfall.
# ---------------------------------------------------------------------------
DS_ISLAND_SPAWN = (24, 20)

DS_ISLAND = {
    "Sora": (24, 20),
    "Kairi": (25, 20),
    "Riku (the small island)": (55, 16),
    "Tidus (the lookout deck)": (10, 9),
    "Selphie": (30, 18),
    "Wakka": (20, 21),
    # day one
    "log (by the dock)": (21, 22),
    "log (the small island)": (54, 17),
    "cloth (the treehouse)": (25, 9),
    "rope (the lookout deck)": (11, 9),
    # day two
    "mushroom (the west grass)": (8, 11),
    "mushroom (the east grass)": (35, 17),
    "mushroom (the Secret Place)": (7, 5),
    "egg (the treetop)": (25, 8),
    "bottle (under the fall)": (12, 7),
    # the chamber wall, which Sora has to stand in front of
    "chalk faces": (7, 5),
    "the door": (8, 5),
    "scribbles": (9, 5),
    # the passage in, and the pool it is reached through
    "the cave passage": (11, 6),
    "the plunge pool": (11, 8),
    # the race
    "paopu landing": (55, 15),
    "the far end of the bridge": (52, 15),
    "the near end of the bridge": (44, 15),
    # the night
    "night Riku": (55, 16),
    "night Kairi": (8, 6),
    "shadow spot 1": (12, 13),
    "shadow spot 2": (20, 15),
    "shadow spot 3": (30, 12),
    "shadow spot 4": (38, 17),
    "shadow spot 5": (16, 19),
    "shadow spot 6": (28, 21),
    "shadow spot 7": (35, 14),
    "shadow spot 8": (18, 11),
    "shadow spot 9": (40, 12),
    "shadow spot 10": (24, 16),
}

DS_ISLAND_NEAR = {
    "coconut palm (west)": (15, 12),
    "coconut palm (centre)": (31, 12),
    "coconut palm (south)": (23, 18),
    "coconut palm (east)": (37, 13),
    "paopu tree": (56, 15),
    "fish (west shallows)": (5, 20),
    "fish (east shallows)": (42, 19),
    "fish (off the dock)": (24, 25),
    "boulder (west)": (10, 15),
    "boulder (centre)": (28, 16),
    "boulder (by the bridge)": (43, 15),
    "bush (the headland)": (8, 12),
    "bush (by the treehouse)": (30, 10),
    "rock (the south beach)": (27, 22),
    "rock (the small island)": (54, 19),
}


# ---------------------------------------------------------------------------
# The Nintendo DS districts: 48x32, three times the area.  A district is walled
# on all four sides, so unlike the island every one of these maps has exactly
# one way in and out and the whole map hangs off it -- a door in the wrong
# column is not a cosmetic mistake, it is a district with no exit.  The
# expansion also added +2 walkways, which the one-step rule can only admit
# through the +1 steps beneath them, and alleys behind those walkways that are
# reachable only round their southern end.  Both are checked from both sides.
# ---------------------------------------------------------------------------
DS_TOWN1 = {
    "Sora": (20, 20),
    "Cid (outside the Accessory Shop)": (17, 6),
    "a townsman": (10, 15),
    "a townswoman": (30, 20),
    "the Accessory Shop door": (16, 4),
    "the Accessory Shop landing": (16, 5),
    "the door to the Second District": (24, 4),
    "the landing from the Second District": (24, 5),
    # The north-east walkway is +2, so it is only enterable over the +1 steps.
    "the step up to the walkway (west)": (34, 13),
    "the step up to the walkway (east)": (39, 13),
    "the walkway (west end)": (30, 10),
    "the walkway (east end)": (43, 10),
    # The strip between the walkway's building and the east wall, which is
    # walled off to the north and only opens at its southern end.
    "the alley behind the walkway": (44, 8),
}

DS_TOWN2 = {
    "Sora": (28, 5),
    "the door back to the First District": (28, 4),
    "the landing from the First District": (28, 5),
    "the door to the Third District": (5, 4),
    "the landing from the Third District": (5, 5),
    "the Hotel door": (24, 4),
    "the Hotel landing": (24, 5),
    # The eight places the Heartless come up.  The fountain is much larger than
    # it was, so every one of these had to be re-placed off it.
    "shadow spot 1": (8, 12),
    "shadow spot 2": (28, 12),
    "shadow spot 3": (10, 21),
    "shadow spot 4": (20, 22),
    "shadow spot 5": (7, 16),
    "shadow spot 6": (30, 17),
    "shadow spot 7": (18, 26),
    "shadow spot 8": (36, 20),
    "the step up to the walkway (west)": (36, 15),
    "the step up to the walkway (east)": (41, 15),
    "the walkway (north end)": (33, 9),
    "the walkway (south end)": (43, 14),
    "the alley behind the walkway": (44, 6),
}

DS_TOWN3 = {
    "Sora": (23, 5),
    "the door back to the Second District": (23, 4),
    "the landing from the Second District": (23, 5),
    "Donald": (20, 20),
    "Goofy": (26, 20),
    # The Guard Armor drops into the middle of the floor, which the two
    # walkways look down on, so this tile has to stay clear of both of them.
    "the Guard Armor": (23, 18),
    "the west step up (near)": (8, 12),
    "the west step up (far)": (15, 12),
    "the east step up (near)": (32, 12),
    "the east step up (far)": (39, 12),
    "the west walkway": (5, 10),
    "the east walkway": (42, 10),
    "the west alley": (3, 6),
    "the east alley": (44, 6),
    "the corridor down from the door": (23, 11),
}

# Lamp posts and crates stand on their own blocked tiles, so like the palms
# they are checked for a walkable neighbour.  So is the fountain rim, which
# Sora has to be able to stand at without being able to stand on.
DS_TOWN1_NEAR = {
    "lamp (north-west)": (9, 9),
    "lamp (north-east)": (20, 9),
    "lamp (south-west)": (9, 22),
    "lamp (south-east)": (20, 22),
    "lamp (below the walkway)": (35, 24),
    "crates (by the shop row)": (3, 6),
    "crates (south-west)": (4, 26),
    "crates (south-east)": (43, 26),
}

DS_TOWN2_NEAR = {
    "lamp (north-west)": (8, 9),
    "lamp (north-east)": (28, 9),
    "lamp (south-west)": (8, 25),
    "lamp (south-east)": (30, 25),
    "lamp (below the walkway)": (39, 22),
    "the fountain rim (north)": (18, 13),
    "the fountain rim (south)": (18, 20),
    "crates (by the Third District door)": (4, 6),
    "crates (the south wall)": (27, 27),
    "crates (south-east)": (43, 26),
}

DS_TOWN3_NEAR = {
    "lamp (north-west)": (6, 16),
    "lamp (north-east)": (41, 16),
    "lamp (south-west)": (6, 25),
    "lamp (south-east)": (41, 25),
    "crates (south-west)": (4, 26),
    "crates (south-east)": (43, 26),
}


def check(label: str, grid, spawn: tuple[int, int],
          reach: dict, adjacent: dict) -> int:
    """Flood-fill one map with the engine's rule and check every spawn point.

    The map's dimensions come from the map, not from a constant: the DS worlds
    are larger than the SNES ones, and this checker has to serve both.
    """
    W, H = grid.w, grid.h
    walkable, height = {}, {}
    for j in range(H):
        for i in range(W):
            code = TERRAIN[grid[j][i]]
            walkable[(i, j)], height[(i, j)] = code[3], code[4]

    if not walkable[spawn]:
        print(f"{label}: spawn {spawn} is not walkable "
              f"('{grid[spawn[1]][spawn[0]]}')")
        return 1

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


def main() -> int:
    bad = check("island", load_grid(), SPAWN, REACH, ADJACENT)
    bad |= check("fragment", load_grid("fragment.txt"), FRAGMENT["Sora"],
                 FRAGMENT, {})
    for name, reach, near in (("town1", TOWN1, TOWN1_NEAR),
                              ("town2", TOWN2, TOWN2_NEAR),
                              ("town3", TOWN3, TOWN3_NEAR)):
        bad |= check(name, load_grid(f"{name}.txt"), reach["Sora"], reach, near)

    # ...and the DS worlds, which are a different size and live beside them.
    bad |= check("ds/island", load_grid("island.txt", subdir="ds"),
                 DS_ISLAND_SPAWN, DS_ISLAND, DS_ISLAND_NEAR)
    for name, reach, near in (("town1", DS_TOWN1, DS_TOWN1_NEAR),
                              ("town2", DS_TOWN2, DS_TOWN2_NEAR),
                              ("town3", DS_TOWN3, DS_TOWN3_NEAR)):
        bad |= check(f"ds/{name}", load_grid(f"{name}.txt", subdir="ds"),
                     reach["Sora"], reach, near)
    return bad


if __name__ == "__main__":
    raise SystemExit(main())
