---
id: 001
spec_section: "7,9"
trace_fields: [camY, bgVOfs]
scenes: [*]          # the screen is 32 lines shorter in every scene there is
platform: ds
reason: the DS is 256x192; the SNES was 256x224
---

# The screen is 32 lines shorter

`docs/BEHAVIOUR.md` §7 gives the camera as

```
camX = clamp(playerX_px - 128, camLoX, camHiX)
camY = clamp(playerY_px - 112, camLoY, camHiY)
```

`112` is half of 224. On the DS it is **96**, and the vertical clamp widens from
`0..32` to `0..64`.

## What follows from that

**Thirty-two more rows of every map become reachable by the camera.** The maps
were authored against a camera that could never look below world row 14; it can
now reach row 16. Nothing in the map data changes, but more of it is visible than
was ever composed to be seen, and the southern edge of each map should be looked
at before the 2D ground renderer is called finished.

**The pinned cameras still centre correctly.** `DIVE_CAM_Y` is
`(WORLD_H - SCREEN_H) / 2`, so it moves from 16 to 32 and the Station of
Awakening stays centred. That one is arithmetic, not a decision.

**`FRAG_CAM_Y = 24` is now wrong and is carried unchanged on purpose.** It was
hand-tuned on the SNES to keep Darkside's head clear of the HUD while it stood as
far up the island fragment as the land allowed. On the DS the HUD is on the other
screen and the play area is shorter, so the constraint it was solving no longer
exists. **Open item:** retune it when the fragment scene renders, and amend this
file rather than silently changing the constant.

**§8's "nothing walkable in the top 24 px" may be reclaimable**, because the HUD
that owned those pixels is on the bottom screen now. But note the audit's finding
40: that rule was true in *map* coordinates and not on screen — on a Station of
Awakening the camera is pinned such that a walkable row already sat inside the
HUD band. It is a camera-bound question, not only a map-authoring one, so it
cannot be settled until the camera is running.

## Trace consequence

`camY` will differ from the SNES trace on every frame where the player is south
of world y 208, and `bgVOfs` differs always — the SNES writes `(camY - 1) & 0x3FF`
because of a PPU quirk the DS does not share. `tools/trace_diff.py` must treat
both fields as expected-divergent for `platform=ds` and compare the simulation
fields regardless.
