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

from build_assets import MAP_H, MAP_W, TERRAIN, load_grid   # noqa: E402

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


def main() -> int:
    grid = load_grid()

    def cell(i: int, j: int) -> tuple[bool, int]:
        light = TERRAIN[grid[j][i]]
        return light[3], light[4]

    walkable, height = {}, {}
    for j in range(MAP_H):
        for i in range(MAP_W):
            walkable[(i, j)], height[(i, j)] = cell(i, j)

    if not walkable[SPAWN]:
        print(f"spawn {SPAWN} is not walkable ('{grid[SPAWN[1]][SPAWN[0]]}')")
        return 1

    seen = {SPAWN}
    queue = deque([SPAWN])
    while queue:
        i, j = queue.popleft()
        for di, dj in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            n = (i + di, j + dj)
            if not (0 <= n[0] < MAP_W and 0 <= n[1] < MAP_H) or n in seen:
                continue
            if not walkable[n]:
                continue
            if abs(height[n] - height[(i, j)]) > MAX_STEP:
                continue
            seen.add(n)
            queue.append(n)

    bad = []
    for name, pos in REACH.items():
        if pos not in seen:
            why = ("blocked" if not walkable[pos] else "walled off")
            bad.append(f"  {name} at {pos} '{grid[pos[1]][pos[0]]}' -- {why}")
    for name, pos in ADJACENT.items():
        i, j = pos
        near = [(i + di, j + dj) for di, dj in ((1, 0), (-1, 0), (0, 1), (0, -1))]
        near = [n for n in near if 0 <= n[0] < MAP_W and 0 <= n[1] < MAP_H]
        if not any(n in seen for n in near):
            bad.append(f"  {name} at {pos} -- nothing walkable beside it")

    if bad:
        print("unreachable:")
        print("\n".join(bad))
        return 1

    total = sum(1 for v in walkable.values() if v)
    print(f"map ok: {len(seen)} of {total} walkable tiles reachable from "
          f"{SPAWN}, all {len(REACH) + len(ADJACENT)} spawn points covered")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
