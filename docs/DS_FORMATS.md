# The DS's data formats, verified

`tools/ds_encode.py` was written from what I believed about the Nintendo DS's 2D
engines. That is not good enough for a format, because **every one of these
mistakes produces a plausible-looking picture rather than an error** — the tile
sizes match, the entry widths match, nothing overruns, and the first frame on
hardware is recognisable but wrong. There is no test a host can run that would
catch it, because the host has no PPU to disagree with.

So the formats were checked: three independent readings, one from GBATEK v3.06
(re-fetched, 4,154,086 bytes, from problemkaputt.de), one from the libnds headers
and sources, one from prior knowledge with no sources open, then a fourth pass
that adjudicated the three against the primary text rather than by vote. The
SNES side was checked against `fullsnes` the same way.

**Result: every format the pipeline emits is correct. Four defects were found,
all of them in the surrounding code rather than in the encoders**, and all four
are fixed. What follows is the settled answer and the four defects, because the
next person to touch this needs the citations and not my confidence.

---

## The five formats

### 1. 4bpp character data — differs from the SNES, and dangerously

32 bytes per 8×8 character, **linear**, 4 bytes a row, 8 rows, top row first.
`byte = y*4 + (x>>1)`, and the **low nibble is the LEFT pixel**. GBATEK, "LCD
VRAM Character Data": *"Each byte representing two dots, the lower 4 bits define
the color for the left (!) dot, the upper 4 bits the color for the right dot."*
The exclamation mark is theirs.

BG and OBJ characters are **byte-identical**. The DS has no separate sprite
character format; only the VRAM region and the index arithmetic differ.

The SNES is planar and its bit order is reversed — fullsnes, "VRAM 8×8 Pixel Tile
Data": *"Plane 0 stored in bytes 00h,02h,...,0Eh / Plane 1 in 01h,03h,...,0Fh /
Plane 2 in 10h,...,1Eh / Plane 3 in 11h,...,1Fh"* and *"In each byte, bit7 is
left-most."* Both are 32 bytes, so a SNES character uploaded to a DS **does not
error and does not look like noise**. There is no shortcut: it is a full
transpose per character. `tile_4bpp_ds()` builds from pixels rather than
converting, which sidesteps it entirely.

**The DS has no 2bpp mode at all** — BGxCNT bit 7 offers 16/16 or 256/1 and
nothing else. The HUD font is 2bpp on the SNES and had to double in size. The
pipeline already did this; it is recorded here because it is the kind of thing
that gets "optimised" back.

### 2. 8bpp — available, and not worth taking

64 bytes a character, `byte = y*8 + x`. Plain 8bpp is a bad trade: twice the
VRAM, and the map entry's palette field goes dead (*"Not used in 256 color/1
palette mode"*) while the character index stays ten bits — so you halve the byte
budget and gain no indices. It only pays with **extended palettes** (DISPCNT bit
30), which resurrect bits 12–15 as a selector over sixteen 256-colour palettes.
This game's art is authored to 15-colour sub-palettes, so there is nothing to
gain. **Stay 4bpp.**

If extended palettes are ever needed: they are not palette RAM. Map the bank to
LCDC (E `0x06880000`, F `0x06890000`, G `0x06894000`, H `0x06898000`,
I `0x068A0000`), copy, remap, then set DISPCNT bit 30/31 — GBATEK: *"When
allocating extended palettes, the allocated memory is not mapped to the CPU bus."*
Load-time data only.

### 3. Palette entries — byte-identical to the SNES

16 bits: bits 0–4 red, 5–9 green, 10–14 blue, bit 15 unused. Little-endian.
GBATEK "Color Definitions" and fullsnes "CGRAM Palette Entries" assign the same
fields to the same bits with the same 5-bit ranges and the same dead bit 15;
libnds `video.h:133` is `RGB15(r,g,b) ((r)|((g)<<5)|((b)<<10))`. **A raw SNES
CGRAM blob is a valid DS palette blob with zero transformation**, which is why
`pixel.py`'s `palette_bytes()` is reused verbatim across the frozen boundary.

Two things that do not affect the bytes: keep bit 15 zero, because the identical
word in **direct-colour bitmap** modes uses it as alpha (*"Direct Color values
0..7FFFh are NOT displayed"*); and the DS LCD's gamma is not a CRT's, so the
bytes port and the appearance does not.

Addresses, from GBATEK's "Other Video RAM" — 2 KiB total, four separate regions:

| Address | Region |
| --- | --- |
| `0x05000000` | engine A BG palette, 256 entries |
| `0x05000200` | engine A OBJ palette |
| `0x05000400` | engine B BG palette |
| `0x05000600` | engine B OBJ palette |

**Sprite palettes are a separate region at +0x200, not the upper half of a shared
table** as on the SNES (where OBJ palettes live at CGRAM `81h`–`FFh`). The
pipeline emits them as separate blobs already.

### 4. Map entries — differ from the SNES above the low ten bits

GBATEK, "Text BG Screen": bits 0–9 character number, **bit 10 horizontal flip,
bit 11 vertical flip, bits 12–15 palette**. libnds `background.h:141-156` agrees
(`TILE_FLIP_H BIT(10)`, `TILE_FLIP_V BIT(11)`, `TILE_PALETTE(n) ((n)<<12)`).

The SNES puts X-flip at 14, Y-flip at 15, priority at 13 and a 3-bit palette at
10–12. The low ten bits coincide, which is exactly what makes reuse tempting and
wrong: every flipped character comes out unflipped, on the wrong palette.

**The SNES's per-tile priority bit has no DS equivalent** — DS priority is
per-layer, BGxCNT bits 0–1. This game is unaffected because the DS pipeline
repaints from the canvas rather than converting SNES entries, so a priority bit
never reaches it. A port that converted entries would have to split those
characters onto their own layer.

**1023 characters is a hard ceiling per layer per character base.** The DS gives
two things back for free before you have to work for it: flip-aware dedup (bits
10/11 make a character and its three mirrors one index — the pipeline does this)
and sixteen sub-palettes instead of the SNES's eight, so a recolour costs no
index. Beyond that, each layer gets its own character base.

### 5. Screen blocks — identical to the SNES, and the easiest thing to get wrong

The primitive is fixed: *"A Text BG Map always consists of 32x32 entries (256x256
pixels), 400h entries = 800h bytes."* Placement: *"Whereas SC0 is defined by the
normal BG Map base address, SC1 uses same address +2K, SC2 address +4K, SC3
address +6K."*

| Size | Pixels | Characters | Entry index |
| --- | --- | --- | --- |
| 0 | 256×256 | 32×32 | `y*32 + x` |
| 1 | 512×256 | 64×32 | `((x>>5)&1)*1024 + y*32 + (x&31)` |
| 2 | 256×512 | 32×64 | `((y>>5)&1)*1024 + (y&31)*32 + x` |
| 3 | 512×512 | 64×64 | `((y>>5)&1)*2048 + ((x>>5)&1)*1024 + (y&31)*32 + (x&31)` |

The SNES is the same in every respect — fullsnes `2107h BG1SC`: *"0: SC0 SC0 /
SC0 SC0   1: SC0 SC1 / SC0 SC1   2: SC0 SC0 / SC1 SC1   3: SC0 SC1 / SC2 SC3"* —
same 32×32 primitive, same 2 KiB stride, same quadrant order, same row-major
interior, **even the same size-code numbering**. `bg_entry_index()` is shared
between the two machines for that reason.

**What going wrong looks like, precisely.** Emit a flat 64-entry-wide row-major
array for a 512-wide layer and the hardware reads entry `y*32+x` for the left
block: even screen rows show source row `y/2` columns 0–31, odd rows show source
row `(y-1)/2` columns 32–63, and the right block does the same for source rows
16–31. Horizontally halved, vertically doubled, split by row band. Recognisable,
and wrong.

**Sizes 1 and 2 both put their second block at +0x800** and differ only in
whether it is the right half or the bottom half. A map stored without its size
code renders correctly in one and transposed in the other — which is why
`SceneAsset` now carries `bgSize`.

**Max text background: 64×64 characters, 8 KiB of map, 1024 unique characters.**
The escape hatch is an **extended affine** BG (modes 3–5, BGxCNT bit 7 clear),
which reaches 128×128 characters with a 16-bit entry and **no block split** — the
map is plain row-major. It costs BGxHOFS/BGxVOFS in favour of the affine matrix.
Caveat: GBATEK never restates the extended-affine entry's bit layout. libnds
reuses `TileMapEntry16` for it, which is the conventional reading, but that is
corroboration and not documentation. **Verify on hardware before depending on
flips in that mode.**

### 6. Sprites — 1D mapping, and the boundary that renumbers everything

GBATEK, "DS Video OBJs": `TileVramAddress = TileNumber * BoundaryValue`, with the
boundary from DISPCNT bits 20–21 (32/64/128/256) once bit 4 selects 1D. OBJ VRAM
is at `0x06400000` (engine A, 256 KiB) and `0x06600000` (engine B, 128 KiB).

**The mapping bit is DISPCNT bit 4 on the DS, not bit 6.** GBATEK's "LCD OBJ -
VRAM Character (Tile) Mapping" section says bit 6 because it is the GBA chapter;
the DS reassigns it (bit 4 tiled, bit 6 bitmap). Anyone copying that sentence
programs the wrong bit.

Under 1D at boundary 32, a 4bpp 32×32 cel is **512 contiguous bytes** and sixteen
consecutive tile numbers, in reading order:

```
bytes   0.. 31  tile N+0    x 0-7,   y 0-7
bytes  32.. 63  tile N+1    x 8-15,  y 0-7      <- the character to the RIGHT
...
bytes 480..511  tile N+15   x 24-31, y 24-31
```

`encode_cels_ds()` emits exactly this. The SNES's own sheet order carries over
unchanged for Sora — its streaming DMA needed cel-contiguity too — but the object
pages are 16-character-wide grids and had to be re-serialised, which **renumbers
every object**. `dsTileFor()` is the translation, so `actor.h`'s forty-one
`tileFor()` values stay cited against the assembly.

The SNES has no equivalent: its OBJ VRAM is a 16-character-wide grid with *"no
carry-outs from 'x+1' to 'y'"*. **Sprite tile numbers must be recomputed, never
copied.**

**The ten-bit reach is the real constraint, and the margin is 1024 bytes.** At
boundary 32 a tile number addresses only the first 32 KiB of object VRAM,
whatever the machine has. This game's resident set:

```
sorachr    15360
objchr      8192
obj2chr     8192   (the town overwrites this one -- main.s:540 -- so it is
objtownchr  8192    a substitution, not a fourth page)
           -----
           31744 of 32768   1024 bytes spare
```

Four resident pages would not fit. The way out is boundary 64, and **that halves
every cel's tile number** — so `OBJ_BOUNDARY` is a generated constant,
`dsTileFor()` is derived from it, `check_ds_obj_reach()` fails the build if the
resident set outgrows the reach, and `assets.h` carries a `static_assert` in case
someone hand-edits the constant. Nothing recorded this coupling before.

**OAM:** 128 entries, 8 bytes apart, 6 used (the gaps hold affine parameters);
`0x07000000` and `0x07000400`. Attr1 bit 12 H-flip, bit 13 V-flip — but only when
attr0 bit 8 is clear, because bits 9–13 become the affine index when it is set.
**VRAM, palette RAM and OAM all ignore 8-bit writes** (*"can be written to only in
16bit and 32bit units"*), so OAM has to be built in a main-RAM shadow and DMAd in
VBlank.

---

## The four defects, and what was done about them

**1. `ds_encode.py` claimed index 0 was opaque on the backmost layer.** It is
transparent on every layer of both machines. GBATEK: *"Color 0 of all BG and OBJ
palettes is transparent"*, and separately *"Color 0 of BG Palette 0 is used as
backdrop color."* What shows through is the backdrop, which is entry 0 of
**sub-palette 0 specifically** — so a scene moved to sub-palette 1 does not take
its own colour 0 with it and loses its backdrop silently. Only the hard-coded
`palette=0` in `dedupe_tilemap_ds()` was making the wrong docstring harmless.
*Fixed: the docstring says what the hardware does, and the reason the hard-coded
zero matters is now written down where someone would change it.*

**2. `bgOffset()` in `assets.h` dropped the Python's bound check** — the
`+ 0 * heightChars` term was there to silence an unused-parameter warning, so
`y = 64` in a 64×64 background computed block 4 and wrote 2 KiB past the end of
the map, into whatever background follows it. *Fixed: both mirrors now apply the
hardware's wrap, which is what a streamer scrolling off an edge needs anyway, and
`assets_a_coordinate_past_the_background_wraps_rather_than_overruns` proves it —
reverting the wrap fails 10252 checks.*

**3. Neither helper said what unit it returned.** Both return **entries**; a
caller wanting bytes doubles. Off by a factor of two still lands inside the map
and still draws something. *Fixed: renamed `bg_entry_index()` / `bgEntryIndex()`.*

**4. Nothing recorded the 1024-byte object-VRAM margin, or that the tile
numbering is a function of the boundary.** *Fixed as described above.*

---

## What is still unverified

**Nothing here has been seen on hardware or in an emulator.** These are the
formats the primary documentation describes, cross-checked three ways and against
libnds's implementation, and that is a much stronger position than §M2 shipped
in — but it is not the same as a first frame. Two specific things to watch when
one exists:

- the extended-affine map entry's bit layout, which GBATEK never states and
  libnds only implies;
- the DS LCD's gamma against palettes chosen on a CRT-era machine.

Everything else in this document is quoted from GBATEK or fullsnes and can be
checked against the line references in
`tools/ds_encode.py` and `platform/ds/host/tests/test_assets.cpp`.
