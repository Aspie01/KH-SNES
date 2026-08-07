# Kingdom Hearts — SNES demake

An isometric 2.5D demake of Kingdom Hearts for the Super Nintendo. This builds
a real `.sfc` ROM that boots in any SNES emulator or on a flash cart — not a
SNES-styled game running on a modern engine.

![Station of Awakening](docs/screenshot-dive.png)

The game opens where it should: the **Dive to the Heart**. Sora stands on a
stained-glass platform, a voice speaks, and three pedestals offer the Dream
Sword, Shield and Rod. Walking up to one describes its power and asks whether
you want it. Take one, give one up, and the platform breaks apart underfoot.

![Darkside](docs/screenshot-darkside.png)

He lands on a second station where the first Shadows are waiting. Clear them
and his own shadow rises into **Darkside**. It alternates two attacks: a fist
that telegraphs, marks the ground where Sora is standing and lands there a beat
later leaving a new Shadow in the crater, and a volley of three dark orbs spat
from the hole in its chest. Both are dodged by moving, which is the only thing
a boss that never walks can ask of you.

![Destiny Islands](docs/screenshot-island.png)

Beat it and the light takes him: the screen washes to white, and he wakes on
the beach at **Destiny Islands**.

The island is currently a movement sandbox: Sora walks it in eight directions
with depth sorting against palms and rocks, translucent shadows, and a Keyblade
swing. It is not yet the real island opening -- there is nobody to talk to and
nothing to collect. See `docs/DESTINY_ISLANDS.md` for what that needs.

## Build

Needs `cc65` (for `ca65`/`ld65`), Python 3 and Pillow.

```sh
sudo apt-get install cc65
pip install Pillow

make            # regenerate assets, assemble, link, fix the checksum
make assets     # regenerate art and map binaries only
make clean
```

The output is `kh.sfc` — 256 KiB, LoROM, FastROM, with a valid header and
internal checksum.

## Play

```sh
mednafen kh.sfc          # or snes9x, bsnes, Mesen-S, ...
make run                 # same thing
```

| Button | Action |
| --- | --- |
| D-pad | Move (eight directions); pick an option in a prompt |
| A | Talk / examine a pedestal; advance and confirm dialogue |
| B | Swing the Keyblade; also advances dialogue |

## Testing without a screen

`tools/playtest.sh` runs the ROM under a virtual X display, drives it with
scripted controller input, and writes PNG screenshots — so rendering and game
feel can be checked in CI or over SSH.

```sh
tools/playtest.sh kh.sfc shots/ "down:50" "up:100" "b:12"
```

Each step is `keys:frames`; keys are held for that many 60 Hz frames and a shot
is taken at the end. `none` just waits. Keys: `up down left right a b x y start
select`, combined with `+`.

For a headless capture with no X at all, mednafen can record raw video and
`tools/recframes.py` pulls individual frames out of it:

```sh
SDL_VIDEODRIVER=dummy mednafen -qtrecord rec.mov -qtrecord.vcodec raw kh.sfc
python3 tools/recframes.py rec.mov --frames 250 --out shots/
```

## How the isometric view works

The ground is a **background layer**, not sprites. That is possible because of
one number: the isometric diamonds are **32×16 pixels**. A 32×16 diamond
lattice steps 16 pixels horizontally and 8 vertically between tiles, and both
are multiples of 8 — so every diamond lands exactly on the PPU's 8×8 character
grid.

```
world_x = (i - j) * 16 + ORIGIN_X
world_y = (i + j) * 8
```

`tools/build_assets.py` paints the whole island into a 512×256 image, slices it
into 8×8 characters, and folds duplicates (matching against horizontal,
vertical and both flips, since the tilemap carries a flip bit per axis). The
entire island collapses to **56 unique characters**.

Everything that stands up off the ground — Sora, Heartless, palms, boulders —
is a sprite, and sprites draw strictly in OAM order. Sorting actors by world Y
descending and writing them out in that order makes Sora pass behind a palm
when he is north of it and in front when he is south, with no per-object layer
authoring at all.

Standing-on-tile queries invert the projection, and because both divisors are
powers of two it costs shifts instead of a divide:

```
a = world_x - ORIGIN_X
i = (a + 2 * world_y) >> 5
j = (2 * world_y - a) >> 5
```

## Hardware techniques used

- **Sprite streaming.** Sora is a 32×32 sprite with 30 animation cels. Keeping
  them all resident would cost 15 KiB of VRAM, so only the *current* cel lives
  in the sprite page; changing frames uploads 512 bytes during vblank as four
  128-byte rows (a 32×32 sprite occupies a 4×4 block of a 16-wide tile page).
- **Colour-math shadows.** Shadows are sprites on OBJ palette 4 drawn in black,
  with half-add colour math against BG1 on the sub screen. The result is the
  ground at 50% brightness — a real translucent shadow, not a dark blob.
- **Mode 1 with BG3 priority.** BG1 carries the ground, BG3 carries the HUD
  and dialogue above everything, and sprites sit between them at priority 2.
- **Two transitions, both from registers.** The platform shattering is MOSAIC
  coarsening BG1 while brightness falls; the ending is colour math adding a
  fixed colour to every layer, ramped to white. Note the whiteout only takes
  the backgrounds: the SNES restricts OBJ colour math to palettes 4-7, so
  sprites ride through it unwashed.
- **A shatter built from registers, not art.** The platform coming apart is
  MOSAIC coarsening BG1 into ever larger blocks while brightness falls and the
  screen shakes — no debris tileset needed.
- **A boss larger than a sprite.** The SNES caps a sprite at 64×64, and taking
  that size slot would cost the 16×16 one the Shadows need. Darkside is
  therefore four 32×32 sprites emitted as a block, after every sorted actor so
  it always lands behind them — correct nearly always, since Sora fights at
  its feet.
- **FastROM** in banks `$80+`, LoROM mapping.

## Layout

```
src/
  main.s        reset, hardware bring-up, frame loop, scene loading
  nmi.s         vblank: OAM, scroll, sprite streaming, HUD and text upload
  iso.s         isometric projection, camera, ground collision
  oam.s         depth sort and sprite table construction
  world.s       actors, Sora, Heartless, combat
  dive.s        Station of Awakening: script, pedestals, weapon choice
  text.s        dialogue window, typewriter reveal, yes/no prompts
  hud.s         HP gauge on BG3
  pad.s         controller input
  ram.s         storage; game.inc / ram.inc / snes.inc  declarations
  header.s      cartridge header and vectors
tools/
  build_assets.py   all art and map data (original pixel art, drawn in code)
  pixel.py          canvas, palettes, SNES tile/palette encoders
  fixrom.py         internal checksum
  playtest.sh       scripted input + screenshots
  recframes.py      frame extraction from a mednafen recording
assets/
  island.txt        the play space, editable as plain text
  src/              indexed PNG previews (editable, re-importable)
```

## ROM budget

| Segment | Bank | Size |
| --- | --- | --- |
| CODE + RODATA | `$80` | 3.6 KiB |
| BG graphics + HUD font | `$81` | 3.8 KiB |
| Sprite page | `$82` | 8 KiB |
| Tilemap, collision, palettes | `$83` | 4.8 KiB |
| Sora animation sheet | `$84` | 15 KiB |

VRAM is fully mapped: BG1 characters at `$0000` (512 tiles -- the stained
glass needs 299 of them where the island's terrain folds to 56), the 2bpp font
at `$2000`, the ground tilemap at `$2400`, the BG3 tilemap at `$2C00`, and the
sprite page at `$4000`.

## Scenes and dialogue

`LoadScene` swaps BG characters, tilemap, palette and collision map during
forced blank, so a scene is just a set of four binaries plus a script. The
collision map is reached through a long pointer, which is why the walkability
test does not care which scene it is in.

Dialogue scripts are plain bytes in ROM: anything from 32 up is a character,
below that are control codes (`SC_NL` newline, `SC_PAGE` wait-and-clear,
`SC_END`). A message can be opened as a plain box or as a yes/no prompt whose
answer lands in `txtResult`. Writing a new scene is mostly writing a table of
spawns and a few strings -- see `src/dive.s`.

## Editing the island

`assets/island.txt` is a 16×16 grid of terrain codes; `make` recompiles the
tilemap, the collision map and the tileset from it.

```
~  deep water     -  shallow water   .  sand      ,  grass    =  wood dock
#  cliff rock     T  palm (blocked)  R  boulder   r  rock
```

Actor spawns live in `spawnTable` at the bottom of `src/world.s`, in isometric
tile coordinates.

## Roadmap

The slice is deliberately bounded by one constraint: a 64×32 tilemap is 512×256
pixels, which is exactly one screen of isometric ground, so the world currently
fits in VRAM with no streaming. In rough order:

1. **The real Destiny Islands opening** — the arrival works, but the island is
   still empty of people and things to do.
2. **Dive polish** — a third station, and a close-range sweep for Darkside so
   standing under it is not safe. Sora also has no death state yet: his HP
   floors at zero and he keeps going.
   The cast, both raft-material lists and where each piece is found are written
   up in `docs/DESTINY_ISLANDS.md`. The blocker is multi-level terrain: the
   rope and the bridge are not on the ground plane.
3. **Tilemap streaming** — upload columns and rows as the camera crosses tile
   boundaries, which lifts the world-size ceiling entirely.
4. **Combat depth** — three-hit ground combo, lock-on targeting, MP and a magic
   slot, Heartless that telegraph and dodge.
5. **Audio** — SPC700 driver, which is a self-contained project of its own.

## A note on the art

Every asset here is original, generated by `tools/build_assets.py`: nothing is
traced, ripped, or imported from a commercial release. The palettes and
proportions were written from scratch to fit SNES limits (16 colours per
palette, 4bpp tiles, 256-tile pages). The art is drawn in code so it can be
regenerated and tuned, and each piece is also exported as an indexed PNG under
`assets/src/` — replace one of those with your own and it flows straight
through the pipeline.

This is a fan project, not affiliated with or endorsed by Square Enix or
Disney. Kingdom Hearts and its characters are their property. Worth being
clear-eyed about the legal position: copyright infringement does not require
making money, so "non-commercial" is not by itself a defence — what actually
keeps a project like this out of trouble is not distributing it.
