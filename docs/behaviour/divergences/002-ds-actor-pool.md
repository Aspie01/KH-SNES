---
id: 002
spec_section: "1,11"
trace_fields: [actorSlot, actorCount]
scenes: [*]          # the pool size is a property of the build, not of a scene
platform: ds
reason: the expanded DS maps ask for more actors than 32; the SNES limit was WRAM and cycles, not OAM
---

# The actor pool holds 128, not 32

`MAX_ACTORS = 32` on the SNES. **It was never an OAM limit.** An ordinary actor
is emitted as a single 32×32 sprite and only an `AF_HUGE` boss takes four
quadrants, so a completely full SNES pool used about 35 of the PPU's 128 OAM
entries. What bounded it was sixteen parallel arrays in the low WRAM bank and a
3.58 MHz CPU walking all of them every frame.

Neither of those is the DS's problem. The pool costs about 3 KB of 4 MB and the
ARM9 runs at 33 MHz.

**It is also not a choice.** The expanded island asks for **69 prop actors from
the map alone**, before a single person or item is placed on it, because a palm,
a coconut palm, a boulder, a rock and a lamp post are each one actor standing on
one blocked tile. At 32 the DS worlds cannot be populated at all — not "would
look sparse", cannot be loaded.

## What actually bounds it now

The pool holds the whole map; **OAM holds what is on screen**, and those are
different numbers. One DS screen is 256×192, which is 16×12 of our 16 px tiles —
17×13 counting the partial tile at each edge, because the camera does not stop on
tile boundaries. So the constraint moved from "how many actors exist" to "how
many are inside one camera window":

| | Value | Why |
| --- | --- | --- |
| `MAX_OBJECTS` | 128 | per 2D engine. The HUD is on the sub screen with its own 128, so the two never compete. |
| `OBJ_BUDGET_SCENERY` | 96 | what a cast table may put inside one 17×13 window. |
| the remaining 32 | — | `TRANSIENT_ACTORS`, a four-quadrant boss, and Sora. |

`tools/check_map.py` slides that window over every DS map and fails the build if
any position exceeds the budget. Current worst cases: island 33, districts 5–8.
There is a lot of headroom, and the headroom is the point — the check exists so
that a future content pass finds out at build time rather than as dropped
sprites on hardware.

## Behavioural consequence

The pool size is invisible **until the pool fills**, and then it is not:

- `spawn()` returns −1 on a full pool. `SpawnTable` ignored the SNES's
  clear-carry, so on the SNES a spawn past the 32nd slot silently did nothing —
  which is how the bottle went missing during the port's first actor test. On the
  DS the same table succeeds.
- Slot indices are otherwise **identical**. `spawn()` takes the lowest free slot,
  and with a larger array the first 32 allocations are the same 32 allocations.
  Depth is sorted by world Y, not by slot, so ordering does not shift either.

So for the oracle this divergence is **inert on the frozen fixtures**: the SNES
scenes' worst case is the island at 25 placed actors plus 6 transient slots, well
inside 32, and no shipped scene ever fills the pool. `tools/trace_diff.py` should
still treat a `spawn` that returns −1 on one platform and a slot on the other as
a **hard** divergence rather than an expected one — if that ever fires on a
fixture, the fixture has changed, not the pool.

`SNES_MAX_ACTORS = 32` is kept in `constants.h` so the diff can say which limit
it was comparing against.

## What this does not license

The pool being large is not a reason to place actors freely. Two limits still
bite and neither is the pool:

1. **The OBJ budget above**, which is per screen and enforced.
2. **Sprite VRAM.** Instances share tiles, so the cost is per *type* and not per
   actor — but a scene that wants many types at once still has to fit them, and
   the VRAM bank map is M4's problem, not solved here.
