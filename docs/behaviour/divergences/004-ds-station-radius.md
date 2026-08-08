---
id: 004
spec_section: "7,8"
trace_fields: [collision, actorX, actorY]
platform: ds
reason: a 220 px disc fits 224 lines and not 192; the Station of Awakening was clipped
---

# The Stations of Awakening are drawn smaller

`docs/WORLD_SIZES.md` says a station is not expanded, because "a station is a
stained-glass disc in a void, deliberately smaller than the screen, with the
camera pinned so the void never scrolls into view."

**On the DS it was not smaller than the screen.** This is the first consequence of
divergence 001 that actually broke something rather than merely widening a clamp.

| | Value |
| --- | --- |
| disc centre | world (256, 128) |
| SNES radius | 110 → the disc spans y **18 … 238**, 220 px across |
| SNES camera | pinned at y 16, 224 lines → visible **16 … 240**. Fits, by 2 px at each end. |
| DS camera | pinned at y 32, 192 lines → visible **32 … 224**. **Clipped 14 px at the top and 14 at the bottom.** |

The clipped band is the outer golden lip and the dark spoked ring — the two
features that make the thing read as a stained-glass window rather than as a
coloured circle. So the DS draws a **radius 92** disc: 184 px across, 4 px of void
above and below it, and still a circle.

`build_dive_platform()` takes the radius as an argument with the SNES's 110 as
the default, so the frozen build is untouched and its ROM stays byte-identical —
verified, 45/45 baseline fingerprints.

## The cast moved inward with it

Standable tiles are those whose centre sits `DIVE_INSET` = 14 px inside the rim,
so shrinking the disc shrinks the standable set from 96 tiles to 76 — and two of
the SNES's spawn tiles fell off it:

| | SNES | distance from centre | DS |
| --- | --- | --- | --- |
| Sora | (16, 11) | 56.6 | (16, 11) — unchanged |
| pedestal, west | (11, 8) | 72.4 | **(12, 8)** |
| pedestal, north-east | (19, 5) | 68.8 | **(18, 5)** |
| pedestal, south-east | (19, 11) | **79.2 — off** | **(18, 10)** |
| Shadow, west | (11, 7) | 72.4 | (11, 7) — unchanged |
| Shadow, east | (21, 7) | **88.4 — off** | **(20, 7)** |
| Shadow, north | (16, 4) | 56.6 | (16, 4) — unchanged |
| Darkside | (16, 7) | 11.3 | (16, 7) — unchanged |

Scaling the composition by the same 78/96 the standable radius shrank by lands
Sora and **all three pedestals at exactly 56.6 px** from the centre, and the two
flanking Shadows at 72.4. The SNES triangle was not symmetric; this one is. That
was luck, not design, but it is the better arrangement and it is what shipped.

Darkside's tile is unchanged and worth a note of its own. `dive.s` explains
(16, 7) as being as far down the platform as it could stand, because the sprite
is 64 px above its feet and the HUD owned the top 24 px of the screen, so any
higher hid its head behind the gauge whenever Sora backed off. **That reason is
gone** — the DS HUD is on the sub screen. The position is kept because on the
smaller disc it is all but dead centre, which is where it should rise from
anyway. Unlike `FRAG_CAM_Y` in divergence 001, this one needs no retuning: the
constraint disappeared and the value was already right for a different reason.

## What a station has instead of a map

There is no `station1.txt`. The glass is generated from a circle, so the scene
carries a builder rather than a filename, and `grid_from_coll()` turns its
collision array into the same `Grid` interface every authored map presents — `G`
for standable glass, `*` for the void, neither ever drawn. That is what lets the
cast checker, the reachability walk and the prop derivation treat a station like
anywhere else. It also means a station has **no prop tiles**, so nothing in these
three scenes is derived; every actor is placed.

One pairing is deliberately stacked: a dream weapon and its dais occupy the same
tile, because the weapon hovers over the stone. `tools/check_map.py` whitelists
exactly `Pedestal` + `Sword`/`Shield`/`Staff` and still rejects every other
co-location — the check it guards is the one that caught the Secret Place's
mushroom sitting on top of the chalk faces.

## Trace consequence

The station collision map differs from the SNES's — 76 standable tiles against 96
— and four spawn coordinates differ with it. Both are **content** divergences:
the oracle fixtures run the frozen 32×16 maps and the frozen tables at radius
110, so a fixture run must produce the SNES numbers. `trace_diff.py` should treat
a station whose standable count is 76 during a *fixture* comparison as a hard
failure: it means the wrong scene data was loaded, not that the port diverged.

The one thing to watch when the renderer exists is the **falls between stations**.
`FALL_LEN` is 170 frames of Sora descending with `MOTE_LIFE` specks rising past
him, and the motes' start offsets (`moteOfsX`/`moteOfsY`) are in absolute Q12.4
pixels relative to Sora — up to 2560, which is 160 px below him. On a screen
32 lines shorter they begin further outside the visible area than they used to,
which is harmless, and end in the same place, which is what matters. Nothing to
change; it is listed here so that the next person to read it does not have to
re-derive it.
