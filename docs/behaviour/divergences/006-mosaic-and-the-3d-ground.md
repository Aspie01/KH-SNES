---
id: 006
spec_section: "6,7"
trace_fields: [mosaicAmt]
platform: ds
reason: the DS 3D layer cannot be mosaicked, and two beats coarsen the ground with mosaic
conditional: applies only when the 3D quad GroundRenderer is the active one
---

# The 3D ground cannot be mosaicked, so two beats need the 2D one

This is a conditional divergence. It costs nothing today, because the 2D tilemap
`GroundRenderer` is the only one that exists. It becomes real the day the 3D quad
backend ships, and it is recorded now because that is when it was found — while
allocating VRAM for both renderers in `platform/ds/include/vram_map.h`.

## The constraint

GBATEK, on the DS's 3D layer:

> The lower 2bit of the BG0CNT register (4000008h) control the priority relative
> to other BGs and OBJs, so the 3D layer can be in front of or behind 2D layers.
> **All other bits in BG0CNT have no effect on 3D, namely, mosaic cannot be used
> on the 3D layer.**

The 3D image occupies BG0 of the main engine and nothing else — engine B has no
3D at all ("BG0 is always Text (no 3D support)"). So there is one 3D layer, it is
BG0, and BG0's mosaic bit does nothing.

## What uses mosaic

Two beats, and in both of them the mosaic **is** the effect rather than a garnish
on it. Both write the same shape:

```
    asl a / asl a / asl a / asl a
    ora #$01                    ; mosaic size in the high nibble, BG1 enabled
    sta mosaicAmt
```

- **The Dive's Shatter** (dive.s:147-175). The platform breaks under Sora: the
  ground coarsens into ever larger blocks while the brightness falls, and then he
  is falling. The source comment says it outright — "Done with hardware rather
  than art: MOSAIC coarsens BG1 into ever larger blocks."
- **The night's Tear** (night.s:752-794). The island comes apart the same way,
  with a ±3 shake on a four-frame period over the top.

`ora #$01` is the layer-enable nibble and bit 0 is BG1 — **the ground, and only
the ground.** Sprites and the HUD stay sharp while the floor dissolves, which is
what makes it read as the world failing rather than as the screen failing.

## What this means for the port

The 2D tilemap renderer reproduces both exactly: the ground is a text background,
BGxCNT bit 6 enables mosaic on it, and the MOSAIC register takes the same value
the SNES wrote. **Nothing to do.**

The 3D renderer cannot reproduce either. Three ways out, in the order they should
be tried:

1. **Run those two beats on the 2D renderer.** The renderers are swappable by
   construction — that is what `GroundRenderer` is for — and `vram_map.h` keeps
   both sets of resources live simultaneously, so the swap needs no VRAM remap.
   The Shatter and the Tear are the two moments the camera is doing nothing
   interesting, so losing the 3D view during them costs the least of anything
   that could be lost. **This is the recommended answer** and it is why the
   allocation reserves `GROUND_CHR` and `GROUND_MAP` even in a build that intends
   to ship 3D.
2. **Capture and re-display.** The DS's capture unit can write the 3D output to
   VRAM, which could then be shown as a bitmap background with mosaic applied.
   Correct, and it costs a VRAM bank the allocation does not currently have spare
   plus a frame of latency.
3. **Do it in geometry** — displace or scale the quads to fake coarsening. This
   is not the same picture and should not be pretended to be one.

## Trace consequence

`mosaicAmt` is a trace field and the machines that drive it — `Shatter` and
`Tear` — are platform-neutral, so **the trace does not diverge under either
renderer.** The stage machine goes on computing the same mosaic value every
frame; what differs is only whether the device tier can act on it. A 3D build
that silently drops the value would still pass the oracle, which is exactly the
kind of gap the oracle cannot see and this document exists to cover.
