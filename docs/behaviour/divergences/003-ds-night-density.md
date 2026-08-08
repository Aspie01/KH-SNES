---
id: 003
spec_section: "3,6"
trace_fields: [shadowAlive, shadowSpot, nightTimer]
platform: ds
reason: the night's map is four times the area; six Shadows spread over it are one Shadow on screen
---

# The night is populated for a bigger island

`BEHAVIOUR.md` §3 gives the night's Shadows as **six alive at once**
(`SHADOW_MAX`), arriving **70 frames apart** (`SHADOW_GAP`), at one of **ten**
spots picked by `rng % NIGHT_SPOTS`. On the DS the map those numbers were tuned
against no longer exists.

## The arithmetic

| | SNES | DS |
| --- | --- | --- |
| walkable tiles | 196 | 670 |
| what one screen shows | 32×14 of a 32×16 map — nearly all of it | 17×13 of a 64×32 map — about 15% |
| Shadows alive | 6 | **20** |
| tiles per Shadow | 33 | 33 |
| typical Shadows on screen | ~6 | ~1 at six, **~3 at twenty** |
| spots | 10 | **35** |
| frames between arrivals | 70 | **42** |

**The count is derived from per-screen density, not scaled by area.** Six over
196 walkable is one Shadow per 33 tiles; 670 tiles at the same density is 20. Had
the count stayed at six, the night — the scene the whole opening builds to, whose
entire mechanic is being hunted across an island you cannot fight back on until
the Keyblade arrives — would show the player one Shadow at a time.

**The gap follows the same argument, not the count.** The fill should still take
about as long as crossing the map. The island is twice as wide and twice as tall,
so roughly twice the SNES's `6 × 70 = 420` frames; at 20 alive that is a gap of
42.

**The spot table grew because spots are places.** Ten of them over four times the
ground would cluster the arrivals into whichever quarter of the island happened
to hold them.

## The fragment keeps six

`SHADOW_MAX` was one constant on the SNES because the night and the fragment ran
on maps of similar size. They no longer do — the fragment is **deliberately
unexpanded**, because it is the last scrap of ground after the island comes apart
and small is the entire point (`docs/WORLD_SIZES.md`). So the constant splits:

- `SHADOW_MAX_NIGHT = 20` — the expanded island.
- `SHADOW_MAX_FRAG = 6` — unchanged, because the map is unchanged. These are the
  ones Darkside leaves in its craters, on 96 walkable tiles.

One number for both would either desert the island or bury the fragment.

## This is balance, and it is not measured

Nothing here has been played, on hardware or otherwise — the device tier does not
build yet. These are the derivation's answers and should be read as a starting
point with its reasoning attached, not as a tuned result. In particular:

- **Twenty may be too many to fight** once the Keyblade arrives. The SNES's six
  were also a difficulty number, not only a density one, and this note only
  argues the density half.
- **A 42-frame gap may read as a stream rather than as arrivals.** If it does,
  the fix is probably a burst pattern rather than a smaller number.
- Amend this file when either is measured. Do not quietly change the constants.

## Budgets

Both limits hold with room to spare, so neither number is constrained by
hardware — which is why the argument above had to be about the scene:

- **Pool:** 69 props + 6 placed + 20 Shadows + 6 transient = 101 of `MAX_ACTORS`
  128. The fragment: 3 + 6 + 6 = 15.
- **Objects on screen:** the night's worst 17×13 window holds 29 before any
  Shadow arrives; twenty more is 49 of `OBJ_BUDGET_SCENERY` 96, and they cannot
  all be in one window because the spots are spread. `tools/check_map.py`
  enforces the static half of this.

## Trace consequence

`shadowAlive` and `shadowSpot` diverge from the SNES trace on every night frame,
and `nightTimer`'s arrival cadence with them. This is a **content** divergence, so
the oracle fixtures do not exercise it: they run the frozen 32×16 maps and the
frozen tables, where `SNES_SHADOW_MAX`, `SNES_SHADOW_GAP` and `SNES_NIGHT_SPOTS`
apply and are kept in `constants.h` for exactly that comparison. A DS run against
a *fixture* that shows 20 Shadows has loaded the wrong scene data, and
`trace_diff.py` should treat it as a hard failure rather than an expected
divergence.
