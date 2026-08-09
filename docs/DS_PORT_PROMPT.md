# DS port — agent brief

Paste **§0 verbatim at the top of every prompt**, then one milestone section
below it. Run the milestones in order. Within a milestone, parallelise freely.

Do not paste the whole file as one prompt. A fleet given the entire port at once
starts the interesting parts and finishes none of the dependencies.

---

# §0 — Standing preamble (paste every time)

You are one of several agents porting a **finished** SNES game to the Nintendo
DS. The SNES build works, is complete, and stays working: it is the regression
oracle for your work, not legacy to be cleaned up.

## §0.1 Where things are

Repository root `/home/user/KH-SNES`. Work in the **checked-out tree on the
current branch**. Do not `git checkout` a tag or another commit.

| Path | What it is |
| --- | --- |
| `docs/BEHAVIOUR.md` | **The specification.** Every tuning number, extracted from the assembly |
| `docs/BEHAVIOUR-AUDIT.md` | 41 corrections and omissions found by auditing that document. Read both |
| `docs/oracle-baseline.sha256` | Fingerprints proving the oracle has not drifted |
| `platform/snes/src/` | 21 files of 65816 assembly. **The reference implementation.** Read it freely; change nothing |
| `platform/ds/` | Your target. Currently `README.md` and `include/fixed.h` only |
| `assets/*.txt` | The map data. Shared by both targets, valid verbatim on the DS |
| `tools/` | The shared asset pipeline and checkers |
| `traces/*.txt` | Button scripts, `keys:frames` a line. **Read by both emitters** — the DS pad's bits are the SNES's own |

The tag `snes-final` marks the same tree you are working from. Earlier commits
have a different layout (`src/` at the root, no `docs/BEHAVIOUR.md`) — if a
command shows you that, you are reading history, not the project.

## §0.2 The toolchain is not installed, and cannot be installed here

Verified in this environment:

- No devkitPro, devkitARM, libnds, `ndstool`, `grit`, or `dkp-pacman`. Nothing on
  disk, no environment variables.
- Not available from apt. `apt.devkitpro.org` returns **403** (their Cloudflare
  refusing this egress IP, not a proxy policy).
- Docker CLI exists but there is **no daemon**, so the `devkitpro/devkitarm`
  image is not an option.
- No NDS emulator. Mednafen is installed but has no `nds` module.
- apt's `gcc-arm-none-eabi` is a **Cortex-M/R** toolchain and is *not* a
  substitute: the DS is ARMv5TE (ARM946E-S) plus ARMv4T (ARM7TDMI).

**Therefore the port is built in two tiers, and you must know which one you are
in before you write a line:**

| Tier | Builds where | Contains |
| --- | --- | --- |
| **Tier 1 — host** | here, with the system `g++` | Everything platform-neutral: fixed point, the actor system, collision, movement, the scene and stage machines, dialogue script interpretation, the asset pipeline's DS backend. Tested against the oracle |
| **Tier 2 — device** | only where devkitPro is installed | Rendering, VRAM allocation, input, audio, the two-screen setup, the 3D backend |

**Tier 1 is most of the port and all of the risk.** Do it first and do it
completely. Never write Tier-2 code that Tier-1 code depends on. If a milestone
below is marked **Tier 2**, verify `arm-none-eabi-g++ --version` succeeds and
`$DEVKITPRO` is set before starting; if either fails, **stop and report that the
milestone is blocked** — do not improvise a toolchain, do not vendor a compiler,
do not write code you cannot compile.

## §0.3 Prohibitions

These are not style preferences. The target has **no FPU** and **no hardware
divide**, 4 MB of RAM, and a 67 MHz CPU.

- **No exceptions, no RTTI.** `-fno-exceptions -fno-rtti`. Assume they are off.
- **No allocation after init.** No `new`, no `malloc`, no `std::make_*`, in any
  code that runs during a frame. Fixed-size pools sized at compile time, the way
  `MAX_ACTORS` is.
- **No STL containers anywhere in the frame loop.** No `std::vector`,
  `std::string`, `std::map`, `std::function`, `std::optional`. The actor table is
  a structure of arrays because that indexes fastest; a nicer language does not
  change that.
- **No floating point.** No `float`, no `double`, no `<cmath>`. If you find
  yourself wanting one, you want `Fixed` — see `platform/ds/include/fixed.h`.
- **No division in a frame path.** ARMv5TE has no divide instruction; `/` is a
  libgcc call. Every ratio in the SNES engine is a shift. Keep it that way.
- **C++ is used for exactly five things:** the `Fixed` type; RAII around VRAM
  bank handles; `constexpr` table generation; `enum class` for state machines so
  a `TownStage` cannot be compared to a `DiveStage`; and **one** virtual
  interface, the ground renderer (§M3). Everything else is C with a C++
  compiler — plain functions, plain structs, no inheritance, no templates beyond
  `Fixed`.

If you believe a prohibition should be broken, **stop and say so**. Do not break
it and explain afterwards.

## §0.4 Fixed point, and why gameplay stays Q12.4

`platform/ds/include/fixed.h` defines `World = Fixed<4>` (Q12.4, the SNES
format) and `Render = Fixed<12>` (the DS's 1.19.12 vertex and matrix format).

**All gameplay arithmetic is `World`.** Convert to `Render` only when submitting
geometry, and never convert back. This is not conservatism: bit-exact parity with
the SNES simulation is what makes the oracle diff meaningful, and 12 fractional
bits buy nothing a 16-pixel tile can perceive.

Every number in `docs/BEHAVIOUR.md` is a raw `World` value and can be used
directly as `World::fromRaw(...)`.

## §0.5 What you must not break

The oracle is only an oracle while it still builds and still produces the same
ROM. **Do not modify:**

```
platform/snes/**          the SNES engine and its build
assets/*.txt              the map data
tools/build_assets.py     the shared asset generator
tools/pixel.py            palettes and encoders
tools/check_map.py        the map checker
tools/check_modes.py      SNES-only, keeps the oracle building
```

You will need to *extend* `tools/build_assets.py` with a DS backend in §M2. That
is the one sanctioned edit, and the rule is: **add functions, change none.** The
existing `draw_*`, `build_world`, `dedupe_tilemap`, `load_grid` and `TERRAIN`
must behave identically afterwards.

**Gate 0 — run before you start and before you hand off:**

```sh
make -C platform/snes                       # must succeed
sha256sum -c docs/oracle-baseline.sha256    # all 45 lines must say OK
python3 tools/check_map.py                  # all 5 maps OK
python3 tools/check_modes.py                # must say modes ok
```

**And when the simulation changes**, `python3 tools/trace_check.py` — about a
minute, both machines, seven scenarios. Not in Gate 0 because it needs the ROM
and the 65816 interpreter, but it is the check that notices a change in
behaviour rather than a change in output.

Two traps. `make -C platform/snes clean` **deletes `assets/gen/`**, which is
gitignored — the baseline is how you prove the regenerated assets are the same
ones. And `build/kh.dbg` is **not** byte-reproducible (`ca65 -g` embeds source
mtimes), which is why it is absent from the baseline and why `build/kh.map` is
the symbol source for anything that needs addresses.

## §0.6 Specification discipline

**Never invent a tuning number.** If `docs/BEHAVIOUR.md` and
`docs/BEHAVIOUR-AUDIT.md` are both silent on something you need:

1. Read the 65816 source and derive it.
2. Cite the file and line in your report.
3. Add it to `docs/BEHAVIOUR.md` in the same change.

If you must deliberately diverge from the specification — the 192-line screen
forces some of this — **do not edit `BEHAVIOUR.md`'s prose.** Create
`docs/behaviour/divergences/NNN-short-name.md` with front matter:

```
---
id: 003
spec_section: "7"
trace_fields: [camY, bgVOfs]
reason: 256x192 screen; the camera centres on playerY-96, not playerY-112
---
```

One file per divergence, so twenty agents never edit the same file, and so the
trace comparison in §M6 can read the directory and know which differences are
expected. `BEHAVIOUR.md` is on the critical path for every milestone; treat it as
append-only and never rewrite a section another agent may be reading.

## §0.7 File ownership

Concurrent agents must not contend for files. Rules:

- **One owner per file per milestone.** State which files you own in your first
  message. If you need a change in a file you do not own, ask for it.
- **Never edit a shared build file to add sources.** Build files glob:
  `$(wildcard source/engine/*.cpp source/scenes/*.cpp host/tests/*.cpp)`. Add
  your source, do not touch the Makefile.
- **Tests are per-task files.** `platform/ds/host/tests/test_<yourtask>.cpp`.
  Never a shared test file.
- **`platform/ds/include/vram_map.h` has exactly one author**, in §M4, and is
  read-only to everyone else forever after.

## §0.8 How to report

Finish with: what you built, the exit criteria you ran and their literal output,
anything you could not do and why, any specification amendment or divergence you
filed, and any assumption you had to make. If you did not run a check, say so —
do not describe intent as though it were a result.

---

# §M0 — Prove the host tier, and nothing else — **Tier 1**

Deliberately tiny. Its only job is to establish that a platform-neutral build
exists and runs, before anyone writes engine code into a vacuum.

Create:

```
platform/ds/host/Makefile.host      glob-driven, C++17, -fno-exceptions -fno-rtti
                                    -Wall -Wextra -Werror -O2
platform/ds/host/tests/test_fixed.cpp
```

`Makefile.host` builds every `platform/ds/source/**/*.cpp` (none yet) plus every
`platform/ds/host/tests/*.cpp` into one binary that returns non-zero on any
failed assertion. No test framework — a `CHECK(expr)` macro that prints file,
line and the expression is sufficient and adds no dependency.

`test_fixed.cpp` must prove, against `include/fixed.h`:

- `World::fromInt(16).raw() == 256` — Q12.4 parity.
- `tileOf(World::fromInt(-1)) == -1` — floors, does not truncate toward zero.
- `tileOf(World::fromInt(511)) == 31` and `tileOf(World::fromInt(512)) == 32` —
  the map is 32 tiles wide, so 32 is off the east edge.
- Multiplication is exact for the engine's real magnitudes: the largest world
  coordinate is `World::fromInt(512)`, and squaring the largest velocity
  (`raw() == 24`) must not overflow.
- `toRender(World::fromInt(1)).raw() == 4096`.
- **A compile-fail check, documented not automated:** state in a comment that
  `World w = 3;` must not compile, and that `World + Render` must not compile.
  If either does compile, `fixed.h` is wrong — report it, do not work around it.

**Exit criteria.** `make -f platform/ds/host/Makefile.host && ./<binary>` exits
0. Gate 0 still passes. No file outside `platform/ds/host/` and
`platform/ds/source/` was modified.

---

# §M1 — The engine's data model — **Tier 1**

Translate the actor system and the constants. No behaviour yet.

Derive everything from `platform/snes/src/game.inc` and `ram.s`. Deliverables:

- `platform/ds/include/constants.h` — every constant from `game.inc` as
  `constexpr`, grouped as it is there, with the SNES name preserved in a comment
  so a reader can cross-reference. Ranges and speeds are `World`.
  **Exclude the dead constants** listed in `BEHAVIOUR.md` §11 — `ORB_SPEED`,
  `LINE_X`, `LINE_Y`, `KNOCKBACK`, `AF_SOLID` — and leave a comment saying they
  were dead and why.
- `platform/ds/include/actor.h` — the actor table as a **structure of arrays**
  matching `ram.s` field for field (`type, z, x, y, vx, vy, dir, anim, animT,
  state, timer, hp, flags, tile, pal, hitT`), `MAX_ACTORS = 32`.
  `enum class ActType : uint8_t` and `enum class ActState : uint8_t` with the
  exact numeric values from `game.inc` — the values are load-bearing, because the
  engine does range comparisons on them (`ACT_LOG..ACT_BOTTLE` is "walked into",
  `ACT_CID..ACT_TOWNWOMAN` is "lives here"). Preserve the ordering and add
  `static_assert`s pinning each range.
- The four type tables (`typeTile`, `typePal`, `typeFlags`, `typeHP`) as
  `constexpr` arrays indexed by `ActType`, with a `static_assert` that each has
  an entry for every type up to `ACT_GAUNTLET`.
- `AF_*` flags as `enum class ActFlags : uint8_t` with bitwise operators.

**Exit criteria.** Host build passes. A test asserts every type-table array
length, every actor-type range boundary, and that `sizeof` the actor arrays
totals what you expect. Gate 0 passes.

---

# §M2 — A DS backend for the asset pipeline — **LANDED**

**Done.** `tools/ds_encode.py` (the DS formats, kept out of the frozen
`pixel.py`), a DS pass in `tools/build_assets.py`, `include/gen/assets.h`, and
`host/tests/test_assets.cpp`.

`python3 tools/build_assets.py` emits both targets; `--target snes` is a true
subset for the oracle. `--target ds` also regenerates the SNES artefacts as a
by-product — the DS pass consumes canvases the SNES pass paints — and says so
rather than pretending otherwise.

**Three formats differ, and every one of them fails by producing a plausible
picture rather than an error:**

| | SNES | DS |
| --- | --- | --- |
| 4bpp character | **planar** — bitplanes 0/1 interleaved by row for 16 bytes, then 2/3 | **linear** — two pixels a byte, left pixel in the low nibble |
| map entry | bit 15 V, 14 H, 13 priority, 12–10 palette | bit 11 V, **10 H**, **15–12 palette** |
| palette entry | 15-bit BGR little-endian | **identical** — the bytes are reused verbatim |

Two more things a later milestone will trip over:

- **The emitted map is ROW-MAJOR and that is not the hardware layout.** A text
  background is built from 32×32-character blocks; every scene but the stations
  and the fragment is wider than the 64 characters one background holds, so the
  map cannot be uploaded as-is whatever order it is in. `bgEntryIndex()` in
  `gen/assets.h` is where the block layout is defined, once, for the streamer and
  for anything uploading directly. It returns **entries**; double for bytes. A
  scene that *does* fit in one background carries its BGxCNT size code in
  `SceneAsset::bgSize`, because sizes 1 and 2 both put their second block at
  +0x800 and a map without its code renders transposed in one of them.
- **1D sprite mapping renumbers the objects.** A 32×32 sprite is sixteen
  *consecutive* characters, so the object pages are re-serialised cel-contiguous.
  `actor.h`'s `tileFor()` still returns SNES page offsets — they are cited against
  the assembly and should stay citable — and `dsTileFor()` is the translation.
  Sora's sheet needed no reordering: the SNES already stored it cel-contiguous.
- **The object numbering is a function of the 1D boundary, and the margin is
  1024 bytes.** A tile number is ten bits, so at boundary 32 it reaches the first
  32 KiB of object VRAM and no further; the resident set — Sora's sheet, the first
  object page, and the second page that the town *substitutes* rather than adds
  to — is 31744 of it. A fourth resident page forces boundary 64, which halves
  every cel's tile number. Whatever sets DISPCNT must set it to `OBJ_BOUNDARY`,
  because `dsTileFor()` is derived from it. This is §M4's to nail down; the
  pipeline fails the build if the set outgrows the reach.

**How far the verification goes, and where it stops.** The encoders are
cross-checked against the *known-good SNES ones* on the fragment, which is the
one scene both machines paint from the same map: 112 characters, decoded with
each machine's own decoder, pixel-for-pixel equal. Every scene's emitted bytes
are also decoded back and compared against the painted world, so an encoding and
a packing error would have to compensate exactly to survive.

That proves the pipeline is self-consistent and agrees with the SNES. It does not
prove these are the formats the *hardware* wants, so **the formats were then
checked against the primary documentation** — three independent readings of
GBATEK v3.06, libnds and `fullsnes`, adjudicated against the quoted text rather
than by vote. **Every format the pipeline emits is correct.** Four defects were
found in the surrounding code and fixed: the index-0 docstring, a missing bound
check that let `bgOffset` write 2 KiB past a map, the unit of the block-offset
helpers, and the unrecorded coupling between the sprite tile numbering and the 1D
boundary. `docs/DS_FORMATS.md` is the settled answer with citations, and it is
what §M4 and §M7 should be read against.

**What is still unverified is the first frame.** Nothing has been seen on
hardware or in an emulator. Two specific things to watch when one exists: the
extended-affine map entry's bit layout, which GBATEK never states and libnds only
implies, and the DS LCD's gamma against palettes chosen for a CRT.

## The audit, run again against what the pipeline emits

The formats were already checked against the primary documentation and are
right. This walk asked the other question — **is everything emitted described,
and is everything described used?** — the one that found `UpdateSoraFrame` in
§M3b and the missing caller in §M5.

The inventory itself is clean: 80 DS artefacts, every one of them a scene file
matching the nine-scenes-by-twelve-kinds grid or a palette or a sprite sheet
named in a table, and nothing the SNES emits without a DS counterpart. Two
things were emitted and **not described**, and both fail on the device rather
than here.

- **`SceneAsset` did not say which optional tables a scene has.** Every scene
  emits a cast, a collision map and a height map; spots, doors, a boss row, the
  pair and the island's two days are per scene. On the host a loader can try to
  open the file and treat absence as "none" — which is what the tests do. **On
  the device it cannot**: there is no filesystem, a `.bin` is a linked symbol,
  and a symbol that does not exist is a link error. `SceneTable` is that
  description, and the generator now refuses to emit a table it has no flag for
  rather than dropping it silently.

- **`PALETTE_ASSETS` had no consumer anywhere — and could not have had one.**
  It carried a name and a colour count, and `palFor()` returns a *sub-palette
  number*: nothing said which file supplied which slot. `build_ds_palettes`'
  docstring deferred that to §M4; §M4 settled the BG half — the ground is
  reloaded into sub-palette 0 per scene — and left the OBJ half. **A handoff
  neither milestone collected**, and it was the only table in `assets.h` with no
  reference outside it, not even in a test.

  The answer was never open. `main.s:270` lays `objPal` over OBJ sub-palettes
  0–5 in the order SORA/HEART/SCENE/FX/SHADOW/DIVE — the same six numbers
  `constants.h`'s `pal::` carries — and the three scene overrides name their
  target as a CGRAM offset, `128+16` being sub-palette 1 and `128+32` being 2.
  So the island lays ISLE over 1, the night NIGHT over 1 and SCENE_NIGHT over 2,
  the town TOWN over 1 and ARMOR over 2. All eleven are now recorded with their
  region, their slot and which scenes select them, and the generator refuses two
  *always-up* palettes claiming one slot — an override sharing a slot is what an
  override is, and only a simultaneous clash is a bug.

- **One slot had to be chosen rather than read off the assembly, and choosing it
  wrong would have been invisible.** The font is 4bpp on the DS and resident on
  **both** screens — the dialogue box on the main one, the HUD on the sub — so
  it needs a sub-palette free in both. On the main screen sub-palette 0 is the
  ground's, reloaded on every scene load, so a font there would change colour
  with the scenery. `UI_SUBPALETTE` is 15: the far end, leaving 1–14 contiguous
  for a scene that one day wants a second resident ground palette.

# §M3 — Movement, collision and the camera — **LANDED**

The heart of the port, and it must be bit-exact.

**Done.** `include/grid.h`, `source/grid.cpp`, `host/tests/test_grid.cpp` —
13 cases. `tileHeight`, `tileWalkable`, `stepOk`, `tryMoveActor`, `setActorZ`,
`nearPoint`, `updateCamera`, `asr1`, `GroundRenderer` and `NullGroundRenderer`.
Two things worth knowing before building on it:

- **The resolution order is proven, not asserted.** Reversing steps 2 and 3 in
  `tryMoveActor` fails exactly three assertions in
  `grid_a_diagonal_into_a_corner_resolves_horizontally` and nothing else — that
  was checked and reverted. The fixture is a diagonal pinch on the real island:
  tile (16,8), east and north both free, the north-east target a palm trunk. It
  is the only geometry where the order is observable at all.
- **`MoveResult::Refused` is rarer than it reads, and `YOnly` does not mean
  "moved".** A blocked cardinal move takes the vertical-slide branch and writes Y
  back unchanged, because step 3's candidate Y *is* the current Y. See audit
  finding 43. Do not write a stage machine that treats `YOnly` as movement.

The rest of this section is the original brief, kept for the reasoning.

Port from `platform/snes/src/grid.s`: `TileIndex`, `TileWalkable`, `TileHeight`,
`StepOk`, `StoreZ`, `TryMoveActor`, `UpdateCamera`.

**The resolution order in `TryMoveActor` is load-bearing** — both axes, then X
only, then Y only, then refuse. Reversing the last two changes how the island's
walkways feel. `BEHAVIOUR.md` §1 states it; the assembly is the authority.

Also port from `world.s`: `SetActorZ`, `PlayerPos`, `NearPlayer`, and the eight-
facing `dirVelX`/`dirVelY` tables verbatim.

Two mandated divergences — file them under §0.6:

- The camera centres on `playerY - 96`, not `- 112`, and clamps to
  `0 .. WORLD_H - 192`.
- `bgVOfs` is `(camY - 1) & 0x3FF` on the SNES because of a PPU quirk. The DS
  does not share it; use `camY`, and file the divergence so the trace comparison
  expects it.

Define the ground renderer interface here, with one implementation that does
nothing:

```cpp
struct GroundRenderer {                 // the one sanctioned virtual
    virtual void load(const SceneData&) = 0;
    virtual void draw(World camX, World camY) = 0;
    virtual ~GroundRenderer() = default;
};
```

It exists now, with the seam free, so the 3D backend in §M7 is a swap and not a
refactor.

**Exit criteria.** Host tests that, for `assets/island.txt`: walking into the west
cliff slides along it; a `+2` deck is unreachable except across its `+1` step; a
negative coordinate is rejected by the bounds check; and a full diagonal blocked
on one axis resolves horizontally. Every assertion cites the `BEHAVIOUR.md`
section it tests.

---

# §M4 — The VRAM map — **LANDED**

**Done.** `platform/ds/include/vram_map.h`, `host/tests/test_vram.cpp` — 8 cases.
The allocation, and the constraint that forces each line of it:

| Bank | Size | Use | MST | Why it could not be elsewhere |
| --- | --- | --- | --- | --- |
| A | 128K | 3D texture, slot 0 | 3 | texture is A–D only and the granule is a whole bank |
| B | 128K | main BG @ `0x06000000` | 1 | the least flexible large bank — B is main BG, main OBJ or texture and nothing else |
| C | 128K | LCDC, held | 0 | the only 128K bank that can be sub BG |
| D | 128K | LCDC, held | 0 | the only 128K bank that can be sub OBJ |
| E | 64K | main OBJ @ `0x06400000` | 2 | sprites are A/B/E/F/G only; 64K is exactly the reach at boundary 64 |
| F | 16K | texture palette, slot 0 | 3 | 512 bytes needed; spending E here would cost the sprite bank |
| G | 16K | LCDC, held | 0 | the only remaining main OBJ extended palette |
| H | 32K | sub BG @ `0x06200000` | 1 | H can be LCDC, sub BG or sub BG ext palette and nothing else |
| I | 16K | sub OBJ @ `0x06600000` | 2 | 512 sprite numbers; the alternative is D, worth more held |

Four things a later milestone must not re-derive:

- **C and D cannot be main OBJ, and their MST 2 is not unused.** GBATEK's
  main-OBJ rows list A, B, E, F, G and no others, which is what stops the two big
  idle banks taking the sprites. The header encodes the whole capability matrix
  so that mapping is a *compile* error. And the trap inside the trap: MST 2 means
  main OBJ on A, B and E, but on C and D it means **ARM7 work RAM** — so
  pattern-matching that value onto bank C does not give a bank absent from the
  OBJ window, it gives a bank the other CPU now owns.
- **Bank E has no OFS field**, so it can only ever sit at the base of its window.
  Anything a later task adds to the main OBJ window must go above it; A or B at
  OFS 0 lands exactly on top. No assertion can catch that — the second bank
  would be mapped by the later task, not by this file — so it is written down.
- **Everything stays under 62 KiB in a BG window.** The map base is five bits of
  2 KiB units, so that is BGxCNT's reach; past it a region needs DISPCNT's
  64 KiB term, which is engine-wide and moves all four layers — and engine B has
  no such term at all. Both DISPCNT base fields stay zero.
- **Both `GroundRenderer`s are live at once and neither needs a remap.** The 2D
  one uses `GROUND_CHR` and `GROUND_MAP`; the 3D one uses BG0 and the texture
  slot. Disjoint, so switching is a call and not a VRAMCNT write.
- **The 3D layer cannot be mosaicked**, and the Shatter and the Tear both
  coarsen the ground with mosaic. That is why the 2D ground's regions are
  reserved even in a build that intends to ship 3D. See divergence 006.

**The adversarial pass has now run**, on all three lenses, and the freeze is no
longer provisional. Arithmetic and legality came back clean: every address was
recomputed from GBATEK's table transcribed fresh rather than re-read from this
header, and there is no illegal MST, no overlap, nothing past a bank or a window,
and every base aligned and expressible. Four things were found and fixed:

- **The header reserved bytes and said nothing about LAYERS**, which was the real
  gap: with 3D on, engine A has only three tilemap layers left, so layers are
  scarcer than bytes and two later tasks picking their own would collide exactly
  the way two picking their own addresses would. Assigned now — and **BG0 is the
  ground under both renderers**, a text background with the 2D one and the 3D
  image itself with the other, which is what makes them alternatives rather than
  rivals. The box takes BG3, the highest priority, because priority is per-layer
  here where the SNES had it per-tile.
- **A 3D rear-plane bitmap needs texture slots 2 AND 3 — both, or neither.**
  GBATEK: it is two 256×256 16-bit bitmaps, colour in slot 2 and depth in slot 3,
  and "requires VRAM to be allocated to Texture Slot 2 and 3 ... in that case the
  VRAM is used as Rear-plane, and cannot be used for Textures." Under this
  allocation that is banks C **and** D together, so it spends the whole remaining
  texture budget and both of the sub engine's expansion banks at once. The
  recovery path had treated those two as independent 128 KiB increments. A fog
  gradient behind the 3D ground is exactly what the night would ask for, so this
  is worth knowing before wanting one — and `CLEAR_COLOR` costs no VRAM at all.
- **The character ceilings were an inference.** `GROUND_CHR` was given the full
  1024 precisely so it could not be outgrown; the same reasoning had not been
  applied to `UI_CHR` or `SUB_CHR`, and a layer indexes ten bits whatever its
  reservation is. Stated as numbers now, with the arithmetic showing that
  over-indexing `UI_CHR` reads **the ground's streaming window** as glyphs.
- **The scene palette is reloaded, not partitioned.** Nine OBJ and seven BG
  sub-palettes both fit the sixteen a region holds, but the pipeline hard-codes
  sub-palette 0, so all seven BG palettes want to *be* it at different times.
  That is what the SNES did and it is why index 0 keeps working as the backdrop —
  written down now, because a later task wanting two grounds resident must also
  emit a backdrop it will no longer inherit.

Three assertions were broken on purpose and reported: an overlap (moving
`BOX_MAP` onto the streaming window fired *the streaming window over the box
map* and *BG map base*), an illegal MST (assigning the sprites to bank C fired
*a bank is assigned a use its silicon does not implement*), and a base past
BGxCNT's reach. All reverted. A fourth attempt — raising the 1D boundary in
`ds_encode.py` — **did not fire**, which found a real defect: `OBJ_REACH` was
emitted as a literal rather than derived from `OBJ_BOUNDARY`, so the two could
disagree silently. It is derived now, and boundary 128 fails the build.

---

## The original brief

**One agent. One file. Never edited again.**

`platform/ds/include/vram_map.h` — a frozen `constexpr` allocation of the DS's
nine VRAM banks (A–I), with every base address in its real hardware units
(background map bases in 2 KiB steps, tile bases in 16 KiB steps), and
`static_assert`s proving no two regions overlap within an engine's window.

It must reserve up front, before any renderer exists:

- 3D texture memory, which can only come from banks **A–D** and is capped at
  512 KiB.
- Texture palettes, which can only be in **E, F or G**.
- The sub engine's backgrounds, which can only use **C, H or I**, and its
  sprites, which can only use **D or I**.
- Extended palette space, and a note that loading it requires the bank to be
  mapped to LCDC first and remapped afterwards.

This exists because five later tasks all allocate VRAM, and without a single
owner they will collide and force each other to be rewritten. Everyone else
consumes this header read-only.

**Exit criteria.** Host build compiles the header standalone; the overlap
`static_assert`s are proven to fire by temporarily breaking one (report that you
did, and revert). Every reservation cites the hardware constraint that forces it.

---

# §M5 — Scenes, dialogue and the stage machines — **Tier 1**

Port `dive.s`, `island.s`, `night.s`, `town.s` and `text.s`'s interpreter.

This is the largest milestone and the most parallel: the four scene scripts are
independent of each other once §M1 and §M3 exist. One agent per scene, plus one
for the dialogue interpreter.

**Already landed — do not rewrite these:**

| File | What it is |
| --- | --- |
| `include/pad.h` | `Pad` — held and pressed, SNES bit order, and `consume()` |
| `include/text.h`, `source/text.cpp` | the dialogue interpreter, complete |
| `include/scene.h`, `source/scene.cpp` | `SceneGround`, `spawnCast`, `readSpots`, `readDoors` |
| `host/hostblob.h` | reading a scene's tables off disk, host tier only |
| `host/tests/test_text.cpp`, `test_scene.cpp` | 24 cases against the real emitted data |

**Two of the four stage machines are also landed:** `include/stage.h`,
`source/stage_dive.cpp`, `source/stage_town.cpp`, `host/tests/test_stage.cpp` —
19 cases, every transition driven and every beat's frame count asserted. Also in
`stage.h`: `Rng` (the Galois LFSR, bit-exact, with the repeated-subtraction spot
pick), `ScreenFx` (the one place contended effects are arbitrated), and the
`SceneAction` / `StageStep` protocol.

**A machine is pure logic.** It reads the world, advances its stage and timers,
writes `ScreenFx`, and returns an *action* for the caller to perform. It never
loads a scene, opens a box or touches a register. Follow that shape for the other
two: it is what makes "no renderer required" true, and it keeps the second
virtual out of the codebase.

Two things the two landed machines learned that the other two will hit:

- **A timer set to N and counted down to zero has a period of N + 1.** The
  Second District's wave is `TOWN_GAP = 80` and arrives every **81** frames. The
  audit records this for animation rates; it is true of every timer in the game.
- **An action the caller does not perform is a state the machine never leaves.**
  `RaiseArmor` is a one-shot, and `WatchArmor` only stops waiting once an
  `ActType::Armor` exists — so a test that ignores the action sails past the
  stage. That is the protocol working, not a bug, but it catches you once.

**All four machines are landed**, and so are the scripts:
`source/stage_island.cpp`, `source/stage_night.cpp`, `source/stage.cpp`
(the spot spawner the night and the Second District share), and
`include/gen/scripts.h` — every one of the 70 scripts as a `constexpr uint8_t[]`,
with a generated `ScriptId` enum, the `.word` lookup tables, and `scriptFor()`.

**Do not hand-edit `gen/scripts.h`, and do not transcribe a script.**
`tools/build_scripts.py` parses them out of the `.byte` runs and checks every one
**byte for byte against the frozen ROM**, using ca65's debug file for the
addresses. `--check` fails if the committed header has drifted, and belongs in
Gate 0. The header is committed so a clone can build the DS tier without Python
or the assembler.

Two traps that cost time here:
- **The labels are not unique across files.** `island.s` and `night.s` both have
  a `scriptRiku`, and they are different lines. The extractor keys on
  (module, label) via the debug file's scope→mod chain; keying on the label alone
  verifies one against the other's bytes and reports success.
- **A string literal cannot initialise a `uint8_t[]`.** The scripts are emitted
  as character literals with the readable text in a trailing comment.

§M5 is finished.

- Stage machines as `enum class`, transitions exactly as `BEHAVIOUR.md` §6 gives
  them — **and read `BEHAVIOUR-AUDIT.md` findings 7, 8, 14, 15, 16 first**, which
  correct that section's day-change and race transitions.
- The dialogue script format is unchanged: bytes ≥ 32 are characters, below are
  control codes (`SC_END`, `SC_NL`, `SC_PAGE`). The scripts themselves port
  verbatim from the assembly's `.byte` runs, and `BEHAVIOUR.md` §13 now specifies
  the interpreter — that section did not exist when this brief was written.
- **`SC_PAGE` works here and did not on the SNES**, which means a conversation
  holds its scene for several times as many frames as the oracle's did. That is
  divergence 005 and it is the one a trace diff cannot be told to ignore; read it
  before writing a stage machine that waits on `TextBusy`.
- **`SceneGround` needs `spawnCast` to have run before anything reads an actor's
  height.** `spawnCast` resolves it once, at spawn, because that is where the
  scene is; the version that runs during movement is §M3's `SetActorZ`.
- **Spawn tables do not port. They are already data.** `tools/build_assets.py`
  emits them from `assets/ds/<scene>_cast.txt` into `assets/gen/ds/` as
  `(type, i, j, variant)` rows terminated by `$FF` — the same three-byte walk
  `SpawnTable` did, with a fourth byte. Load the binary; do not transcribe the
  assembly, and do not add a `constexpr` copy beside it.
  - `<scene>cast.bin` is placed on entry and after a death. It **already
    contains the prop actors**, which are derived from the map: `T` is a palm,
    `Y` a coconut palm, `R` a boulder, `r` a rock, `l` a lamp post. Do not
    synthesise them a second time and do not place them by hand — the cast file
    refuses a hand-written `Palm` row for that reason.
  - `<scene><table>.bin` is one deferred table per section — `day1`, `day2`,
    `pair` — and *when* each is spawned is your scene's business, which is
    exactly why the pipeline does not decide it.
  - `<scene>spots.bin` is `(i, j)` pairs: where the Heartless come up. **Read
    the count from the file**, not from a constant — the night carries 35 of
    them against the SNES's 10, and `SNES_NIGHT_SPOTS` is kept only so the
    oracle diff can name the fixture's value.
  - **A scene is not a map.** `ds_scenes()` in `build_assets.py` is the list, and
    two entries can share one map: `night` runs on the island's ground, tilemap,
    collision and height byte-for-byte and differs by one palette upload, so
    there are no `nightchr.bin`/`nightmap.bin`/`nightcoll.bin`/`nightheight.bin`
    to load — use the island's. `fragment` reads `assets/fragment.txt`, from the
    SNES directory, because it is deliberately unexpanded.
  - The night's `Y` tiles give a plain **`Palm`**, not a `PalmC`: day two picked
    the coconuts, and `night.s` spawned exactly that. This is already baked into
    `nightcast.bin` — do not re-derive props yourself and do not "fix" the
    difference.
  - **`SHADOW_MAX` no longer exists.** It is `SHADOW_MAX_NIGHT` (20) and
    `SHADOW_MAX_FRAG` (6), because the island grew four times and the fragment
    did not. Read
    `docs/behaviour/divergences/003-ds-night-density.md` before touching either;
    both are derived, neither is measured.
  - **The Stations of Awakening are drawn at radius 92, not 110**, because the
    SNES's disc is clipped 14 px top and bottom on a 192-line screen. Four spawn
    tiles moved inward as a result, and the emitted collision map has 76
    standable tiles rather than 96. Do not "restore" the SNES coordinates from
    `dive.s` — two of them are off the platform.
    `docs/behaviour/divergences/004-ds-station-radius.md`.
  - **A station has no map file.** Its ground is generated; `station<n>chr.bin`
    and friends are emitted like any other scene's, and the cast has no derived
    props because a disc of glass has no prop tiles.
  - **A dream weapon and its dais share a tile on purpose.** Both appear at the
    same coordinates in `station1cast.bin`; the weapon hovers over the stone.
    Nothing else in any scene co-locates, and the checker enforces that.
  - `<scene>doors.bin` is `(i, j, land_i, land_j)`. It says where the doors are
    and where Sora stands beside one; it does **not** say what is on the other
    side. `town.s`'s `doorTable` carried a destination scene and a gating stage
    in three more bytes per row, and reconstructing that wiring for the 48×32
    districts is this milestone's job.
  - `variant`, the fourth byte, selects a line. `TalkTown` dispatched on actor
    *type*, so its three residents could not tell two townsmen apart; the DS
    districts place several of each and distinguish them here. Bounds-check it.
- `MAX_ACTORS` is **128**, not the SNES's 32, and the pool is not the binding
  limit — `OBJ_BUDGET_SCENERY` is. Read
  `docs/behaviour/divergences/002-ds-actor-pool.md` before you place anything.
- **`spawn()` returns −1 on a full pool and every caller must check it.** The
  SNES signalled this with a clear carry that `SpawnTable` ignored, which is how
  the bottle under the waterfall went missing for a whole day.
- **Both bosses mark their slam target on entry to the wind-up**, 44 and 40
  frames before impact. `BEHAVIOUR.md` §4 is corrected on this; getting it
  backwards inverts the dodge window and is the single most consequential error a
  port can make here.

**Exit criteria.** For each scene, a host test drives its stage machine through
every transition with synthetic input and asserts the frame count of each beat
against the specification. No renderer required — these are logic tests.

## The audit, run again against the four scene files

The same walk that found `UpdateSoraFrame` missing from §M3b, applied to
`dive.s`, `island.s`, `night.s` and `town.s`: every `.proc`, one at a time.

The stage machines are complete. **What was missing was the caller.** §M5's own
rule — a machine "reads the world, advances its stage and its timers … and
returns an ACTION for the caller to perform" — was met on one side only:
`IslandMachine::talkToKairi`, `talkToRiku`, `openDoor`, `arriveAtThird`,
`tagPaopu`, `reachHome` and `DiveMachine::choose` were all public **with nothing
calling them**, and `Item`, `NEED[]` and `itemOf()` were data with no consumer.
The island's quest was unreachable end to end: you could not pick up a log, so
the raft never got finished, so the race never started except by poking WRAM.
It is the hole §M6 found in §M3 — `tryMoveActor` with no caller — one layer up.

**`platform/ds/include/interact.h` and `source/interact.cpp`** are that caller:
`CheckPickups`, `Collect`, `SwingAt`, `FindTalker`, `FindProp`, `LookAt`,
`TalkTo`, `TalkKairi`, `HaveAll`, `SoraNear` (island.s), `CheckDoors`,
`TalkTown` (town.s), `TalkTarget` (night.s), `FindWeapon`, `AskAbout`,
`HandleAnswer` (dive.s), and `GameOverUpdate`, which runs *instead of* all of
them. It keeps §M5's rule: nothing in it opens a box or loads a scene — every
entry point returns a `StageStep` and the caller performs it, which is what lets
all of it be tested with no renderer. 15 cases.

## Six scripts that were never extracted

`tools/build_scripts.py` matched labels against `^script\w+`. `dive.s` names the
six dream-weapon descriptions `descSwordTake`, `descSwordDrop` and so on — so
**they were never in the generated header at all**, and the weapon choice had
nothing to say. Its two index tables were missed for a second, independent
reason: `descTakeLo`/`descTakeHi` are split low/high `.byte` arrays with the
label and the data on one line, and the extractor only knew `.word` tables whose
label sat on its own.

Both are fixed and the count is now **76 scripts, 5774 bytes, verified byte for
byte against the ROM** — up from 70 and 5352. That check is what makes the
recovery trustworthy: the generator does not transcribe, it parses and then
compares against the artefact.

## Things the audit confirmed rather than changed

- **`FindProp` is the one search that does not take the first hit.** Three
  drawings hang within arm's reach of each other on the cave wall, so it takes
  whichever is closest by `|dx| + |dy|` — and an equal distance keeps the earlier
  slot, because the comparison is `bcs`.
- **`CheckPickups` tests the deck as well as the distance.** Reaching up onto the
  treehouse from the grass below it does not count.
- **The two days ask for different lists**, and they do not overlap: day one is
  timber, day two is provisions. Checking all eight on both days would make day
  one unfinishable.
- **Kairi's line is chosen from the state *before* the machine changes it.**
  `talkToKairi` is what turns `Idle` into `Active`, so a caller that read the
  state afterwards would hand over the list and then say "still something
  missing" about it.
- **Talking wins the frame over a door under the player's feet.** `TownUpdate`
  re-tests `TextBusy` between the two, which is what stops a district change
  happening under an open box.
- **`GameOverUpdate` is two passes and not one**: the card, then the retry.

## What is still open, and why it is a data question

`CheckDoors` needs four things per door — where it is, where it lands, **which
district it leads to**, and **which stage the town must have reached**. The
SNES's `doorTable` row is seven bytes and carries all four. The DS's
`<scene>doors.bin` row is four and carries the first two, because `scene.h`
decided that "which district it leads to is scene logic and the table
deliberately does not say".

That decision stands, so `townInteract` takes a `TownDoor[]` — position,
destination, landing, gate — and the mechanism is complete and tested. **What
does not exist yet is anything that builds that array for the DS's own maps**,
which have two doors in the First District where the SNES had one. Filling it in
is a content decision about maps this milestone did not draw, and it belongs
with whoever draws them.

---

# §M6 — The trace oracle — **LANDED**

**Done.** `tools/snes_opcodes.py`, `tools/snes_cpu.py`, `tools/snes_trace.py`,
`tools/trace_diff.py`.

**The driving mechanism.** The brief asked for one and said establishing it was
part of the milestone. It is not Mednafen: no emulator here can be stepped
frame-accurately from outside, and `playtest.sh`'s wall-clock sleeps drift, so it
can say what the game looks like after about two seconds but never what WRAM held
on frame 137. So the ROM runs on a **headless 65816 interpreter** instead, and
four properties of this ROM — each checked, not assumed — make that far cheaper
than it sounds:

- **The APU is never touched.** `APUIO0-3` are defined in `snes.inc` and
  referenced nowhere, so there is no SPC700 handshake and no boot ROM. This is
  the thing that normally makes headless SNES emulation hard.
- **Exactly three registers are read** — `HVBJOY`, `RDNMI`, `JOY1L`. Everything
  else is write-only to the ROM, so a sink is faithful. DMA is the one exception,
  and only because `CLEAR_WRAM` zeroes 128 KiB through the WRAM port.
- **89 opcodes, 9 addressing modes**, no long calls, no decimal, no indirect
  jumps, no block moves.
- **All WRAM mutation is frame-synchronous**, because `WaitVBlank` spins on a
  WRAM byte the NMI sets.

The frame boundary is therefore *detected* — run until the CPU is idling in
`WaitVBlank`, reading the flag and writing nothing — rather than counted.

**But that alone does not make the trace exact, and the first version of this
claimed it did.** An independent review of the machine model caught it: the
assertion "the CPU was parked when the NMI fired" is **vacuous**, because the
driver runs until the CPU parks. It proved nothing.

The condition that actually matters is whether **a frame's work fits in a
frame**. If it does not, hardware fires the NMI mid-work and `WaitVBlank`'s
opening `stz vblankFlag` *throws that flag away* — so one game update consumes
two NMIs, and `frameCount` advances by two while the logic advances by one.
`frameCount` is not cosmetic: `frameCount & 2` picks `shakeX` (dive.s:183,
night.s:788, town.s:793, town.s:975), the flash palette (oam.s:292), the mote
spread (dive.s:512) and the dark column's cel (night.s:577). One swallowed NMI
flips a parity that never recovers.

So the interpreter now charges cycles, dominated **not by instructions but by
DMA at a fixed eight master cycles a byte**, and asserts each frame against one
NTSC frame. Measured: ordinary frames run at **31.9%**, the reset path at 584%
(exempt — it clears 128 KiB through a byte port before NMI is armed). A
`LoadScene` call moves ~31 KiB, which is ~70% on its own, so **scene-transition
frames are the case to watch** and are close to the limit. When one crosses it
the run stops and names the frame rather than emitting a trace it cannot stand
behind — proven by lowering the budget, which fires with exactly that diagnosis.

**The opcode table is taken from the assembler, not from a reference.** ca65
assembled this ROM, so its listings say exactly which byte it emitted for every
instruction in it; `snes_opcodes.py --regen` re-assembles the frozen sources into
a temporary directory with `-l` and reads the bytes back, and the default action
checks the committed table still matches. Coverage is therefore exact by
construction, and **the decoder hard-errors on any byte outside the 89** — which
is the strongest self-check available, because a desynchronised decoder is
reported at the instruction that caused it rather than as a corrupt trace two
hundred frames later.

**Verified against the specification, independently.** After boot the oracle has
`frameCount` exact, `sceneId` = `SCENE_DIVE`, Sora at `(4224, 2944)` = exactly
`CELL_X(16)`/`CELL_Y(11)`, and the Dive's seven-actor cast live. Driven with a
scripted input he walks east at **+24 Q12.4 per frame** — `WALK_SPEED = 24` — and
stops at the platform edge, and the dialogue box dismisses on the second press,
which is `txtHold` behaving as §13 describes.

**`trace_diff.py` reads `docs/behaviour/divergences/`** and suppresses only what a
recorded divergence excuses, counting every suppression by which divergence
excused it so a divergence that has quietly become a blanket is visible.
`--strict` suppresses nothing. Proven by perturbation: on a trace with `px` and
`php` altered from frame 40, the `px` change is excused by divergence 004 and the
`php` change is reported with its earliest frame.

One smaller thing found while building the differ: **a divergence's
`trace_fields` cannot say which scenes it applies to.** Divergence 004 lists
`actorX`/`actorY` because a station's cast moved inward, and the differ therefore
suppresses actor positions *everywhere*, including on the island where nothing
moved. The front matter needs a `scenes:` key. Until it has one, read the
per-divergence suppression counts the differ prints rather than trusting them.

---

# §M6b — The DS trace emitter — **LANDED**

**Done.** `platform/ds/include/trace.h`, `platform/ds/source/trace.cpp`,
`platform/ds/host/trace_main.cpp`, `tools/ds_trace.py`, `tools/trace_check.py`,
`traces/*.txt`.

§M6 built the oracle and left the other half unbuilt: the format existed, the
differ existed, nothing on the DS side emitted a line. This is that half.

**The exit criterion is met, and by more than it asked for.** Six scenarios are
**byte-identical to the SNES over 3125 frames**, under `--strict`, with nothing
suppressed and no divergence file involved:

| scenario | frames | what it exercises |
| --- | --- | --- |
| `station` | 130 | the walk, the rim of the disc, one swing |
| `darkside` | 285 | rest → fist → orbs, the Shadow the slam leaves, an orb expiring |
| `armor` | 385 | `TownRestart` re-raising the armour, the drop, the landing freeze, the walk, the fist that connects |
| `town` | 885 | the Second District's wave off the `$1D57` seed, one refusal, the cap at five |
| `night` | 770 | the storm, six Shadows arriving off the `$ACE1` LFSR, the ceiling where the draws stop |
| `race` | 670 | Riku's whole waypoint course, every frame of it |

`tools/trace_check.py` runs all of them plus the seventh against both machines
in about a minute. It is not in Gate 0 because it needs the ROM and the
interpreter; run it whenever the simulation changes.

**Why a scenario and not a game.** The oracle boots the ROM and the ROM does the
rest. There is no equivalent here, because the code that would tie the four
stage machines together is the device tier and the device tier is blocked. A
scenario is that tying-together for one situation: `ReadPad → TextUpdate →
SceneUpdate → UpdateWorld`, sampled where the oracle samples, and **a
`SceneAction` it cannot perform stops the run and names it** rather than being
dropped — a skipped transition would put the trace on a different timeline and
the diff would blame a frame hundreds later.

**Two families, and the difference is the point.** `station`, `darkside`,
`armor` and `race` hold the CONTENT equal — the SNES's own collision maps, the
SNES's own cast positions — so that any difference is a difference in the CODE.
`dive` is the scene as the DS actually ships it, smaller disc and all, and its
job is to show the recorded divergences being correctly excused.

**One input script drives both emitters with no translation table**, because the
DS pad's bits are the SNES's own (`pad.h`); `trace_main.cpp` static_asserts all
twelve so that cannot quietly stop being true.

## What building it found

- **`diveStage` is a gate, not a label.** `SceneUpdate` runs `IslandUpdate` only
  when `diveStage == DIVE_ARRIVED` and otherwise runs `DiveUpdate` *on the
  island* (main.s:622-628). The first `race` run differed on exactly one column
  for 670 straight frames, which is what that looks like from outside.
- **`trace_diff.py` could report success while comparing nothing.** Columns are
  matched by name and a name missing from one side was skipped, so renaming `px`
  to `pX` on one emitter produced "no unexplained divergence" — demonstrated,
  then fixed: the pair is now refused unless the column lists and the version
  match. `tools/ds_trace.py --check-format` additionally compares the two
  `#fields` lines byte for byte before every run, which is the only check that
  can see both languages.
- **The differ named a column the divergences could not.** Actor slots are
  emitted as `actorIdx` and divergence 002 calls them `actorSlot`, so 002 could
  never have excused the one thing it exists for. Aliased.
- **Divergence 005 reaches the stage bytes**, measured rather than predicted:
  the `dive` pair's first unexplained difference is frame 21, `diveStage`,
  because `scriptIntro` has two `SC_PAGE`s and the DS box needs six presses
  where the SNES needed two. The field list of 005 is deliberately **not**
  widened to cover it — see that file's "Measured, once both emitters existed".
- **A position had to be encoded as the oracle reads one.** The SNES stored
  Q12.4 in two bytes and the trace prints them unsigned; `World` here is an
  `int32_t`, so a negative would have printed `-16` against `65520` and been
  reported as a divergence that was really an encoding difference.

**Proven by perturbation, three ways:** renaming a column makes
`--check-format` and the differ both refuse the pair; swapping two columns or
changing the position encoding fails the host tests; and *fixing* Darkside's
reproduced fist bug makes `trace_check.py` fail the `darkside` pair at frame
116, by name. That last one is the whole point of the machinery.

## The night, and the RNG it took to get there

The brief names exactly one determinism hazard: "the RNG is a 16-bit Galois LFSR
seeded to `$ACE1` by `InitWorld` and to `$1D57` by `TownBegin` — seed yours
identically and advance it at the same points, or the Heartless spawn positions
diverge immediately and the diff is worthless." **Nothing before the night
tested it**, because nothing before the night drew from it: the stations, both
bosses and the race are fully deterministic. The night draws from two places at
once — the flash wait and the spawn spot — so a trace that matches proves the
sequence *and* the order of the draws.

It now matches for 770 frames, through six arrivals, one refusal-and-retry, and
305 frames at the ceiling. Four things had to be fixed first, and none of them
would have been found by reading:

- **`NightMachine::begin()` and `restart()` did not draw.** `ArmLightning` is one
  `Rand` and it is called from three places — `NightBegin` (night.s:83),
  `NightRestart` (night.s:108) and the end of a wait (night.s:302). Only the
  third was reproduced. `restart()` set the wait to `FLASH_GAP_MIN` under a
  comment that said it drew. Both now take the `Rng`, which is why their
  signatures changed.
- **`begin()` left the spawn timer at zero**, so the first Shadow of the search
  arrived on its opening frame instead of seventy-one frames into it. `NightBegin`
  sets `spawnTimer = SHADOW_GAP` before anything runs (night.s:81).
- **`begin()` did not arm the opening flash.** "No fade back in: the storm
  arrives with the first flash of lightning" — `flashTimer = FLASH_LEN`,
  night.s:89.
- **The density was hard-coded, so `SHADOW_MAX_FRAG` could not be selected.**
  `spawnShadows` always used `SHADOW_MAX_NIGHT`, which meant the fragment would
  have run at island density on a map that was never expanded, and an oracle
  fixture could not run at the SNES's six-at-seventy at all.
  `NightMachine::setDensity()` is the seam; divergence 003 is where the three
  pairs of numbers come from.

**And one trap that is the harness's and not the game's, worth writing down
because the next fixture will hit it.** `rngState` is seeded by `InitWorld`,
`InitWorld` runs when the ISLAND is entered or restarted, and `RestartScene`'s
night branch calls `NightRestart` instead. Poke straight into the night and the
LFSR is still **zero** — and a Galois register at zero is a fixed point, so every
flash waits exactly `FLASH_GAP_MIN` and every Shadow of the whole night comes up
on spot zero. `TownBegin` carries a comment warning about precisely this
(town.s:68). The first version of the night scenario did exactly that, matched
nothing, and looked like an RNG bug; the fix is a **two-stage poke** — restart
onto the island so `InitWorld` runs, then restart into the night — so the seeding
is the game's own code. On the shipped path the Dive's `@swap` calls `InitWorld`
(dive.s:610), so this never happens in play.

**A fifth thing, found on the way and now part of the model.** The first emitted
frame of a poked scenario is the frame `RestartScene` ran, and on that frame
`GameOverUpdate` runs *instead of* the scene script — so the world updated and
the stage machine did not. The other three scenarios never noticed, because
their machines do nothing on a frame with a live boss; the night has a running
timer, and one extra update at the start put every Shadow of the next four
hundred frames one frame early. `Scenario::first` is now an enum with that case
in it.

## Traverse Town, and the second seed

The night proved the `$ACE1` sequence. The Second District proves `$1D57`, and
a **different consumer** of it: `TownShadows` has a wave counter *and* an alive
cap where `SpawnShadows` has only a cap, and it increments the counter only on a
spawn that succeeded — so a spot refused for being on top of the player costs a
draw and buys no progress. 885 frames, five arrivals, one refusal, and 467
frames sitting at the cap where the draws stop.

The input script is not the idle one: `traces/town.txt` walks Sora around the
square, because the refusal rule tests the spot against *where he is standing*,
so a player who moves is what turns the LFSR sequence into a different set of
arrivals. It deliberately never presses **up** — every district door is in row 4
and Sora arrives on row 5, and a door is a scene load, which is the one thing a
trace scenario cannot follow.

- **`TownMachine::restart()` did not exist.** The Dive and the night both had
  one; the town did not, though `TownRestart` (town.s:94) is a real routine
  doing five things. The one nobody guesses is that **the wave counter goes back
  to zero** — the assembly says why: *"half a wave of survivors left standing
  while the counter says the district is nearly clear would be a retry that is
  easier than the attempt."* It also re-arms the spawn timer, clears the door,
  puts the screen-wide effects back, and arms `townTimer` to **1** if the
  district is on its boss. `SceneAction::RespawnDistrict` is the action it
  returns.

- **The `armor` scenario had been agreeing for the wrong reason.** It spawned
  the Guard Armor and its two hands in setup and called its first frame 16. The
  retry frame is 15, and on 16 the ROM ran `WatchArmor` → `RaiseArmor`, which
  spawns all three *and moves Sora* to tile (16,10) — the staging that puts the
  armour between him and the door he came in by. Setting up the outcome instead
  of the route produced the same state one frame later and hid a whole action.
  The scenario now starts at 15 with six actors and lets the machine raise it;
  `perform()` implements `RaiseArmor` and `SweepGauntlets` for real.

- **Sora's town1 spawn tile was transcribed from after the move.**
  `town1Spawns` puts him at (14,12), "face down in the middle of the square";
  (16,10) is where `RaiseArmor` puts him a frame later. Four differences at one
  frame, which is what a trace is for.

- **`--poke16`.** `rngState` is two bytes and `--poke` writes one. Only
  `TownBegin` writes `$1D57` and it runs an entire night earlier, while
  `TownRestart` deliberately does not re-seed — but nothing between the two
  draws, so `$1D57` with no draws *is* the state the player reaches the square
  in. The new flag lets a fixture say that exactly, and its help text says why
  getting it wrong is quiet rather than loud.

---

# §M3b — The actor simulation — **COMPLETE**, re-audited against `world.s`

This milestone did not exist. §M6 found the hole: §M3 delivered the movement
*primitive* and §M5 the scene-level machines, and nothing delivered the code in
between — so `tryMoveActor` sat in `grid.cpp` **with no caller**, and there was
no simulation to trace.

**Done.** `platform/ds/include/world.h`, `source/world.cpp`,
`host/tests/test_world.cpp` — 5 cases. `updateWorld` with its slot-order
dispatch and hit-stop freeze, `updateSora` (free movement, attack, hurt, fall,
dying), `updateHeartless`, `updateSlash`, `damageSora`, and the shared vocabulary
— `readMoveDir`, `setVelFull`, `setVelHalf`, `clearVelocity`, `animateWalk`,
`attackPoint`, `heartlessAimDir`, `heartlessTouchTest`, `hurtHeartless`,
`doAttackHit`.

**It is checked against the oracle, not against my reading of `world.s`.** That
is the whole point of having built §M6 first, and it is a different kind of test
from everything else in the suite: `tools/snes_trace.py` ran the frozen ROM for
150 frames on a scripted input, and the fixture in `test_world.cpp` is its
output. The script exercises a cardinal walk, the disc rim stopping it, a
diagonal, and a swing. **The DS simulation reproduces all of it exactly** — same
Q12.4 position, same facing, same state, on every sampled frame.

Two deliberate breakages confirmed the test bites. Giving the diagonal full speed
instead of the 17/24 scaling fails on position. Reversing `updateSora`'s
attack-timer order to test-then-decrement — **precisely the mistake audit finding
52 warns about** — fails on a *one-frame* state difference, which is the kind a
hand-written expectation would have got wrong in the same direction as the code.

The test loads the **SNES's** `divecoll.bin` rather than the DS's, on purpose: the
DS station is radius 92 against 110 (divergence 004), so the two collision maps
differ by design. Using the SNES's isolates the movement code, which must be
identical, from the content decision, which must not be.

## The completeness audit, run again once the traces existed

The traces agree over 3125 frames, and that is a statement about the code they
*execute*. It is not a statement about the code that is missing: a routine with
no caller and a type that no scenario spawns are both invisible to a diff. So
every `.proc` in `world.s` was walked against this tier, one at a time.

**Thirty-eight of the thirty-nine are present** — as a function of the same name
where the shape carried across, and as inline behaviour where it did not
(`BossInRange` is the extent test inside `doAttackHit`, `OrbHitPlayer` the touch
test inside `updateOrb`, `PlayerPos` an actor-table read). The nine-way dispatch
matches the assembly's comparison chain type for type, and every `sta actFlags`
in the SNES sources has a counterpart: `SpawnActor`'s initial flags,
`UpdateFish`'s mirror, `UpdateRiku`'s mirror.

**The thirty-ninth was `UpdateSoraFrame`, and it was not written.** `world.h`'s
own header listed it in the specification — "UpdateSoraFrame runs ONCE, after
the loop, not per actor" — and `updateWorld` called nothing after the loop.

It matters because of what it writes. Eight compass directions, **five drawn
facings**: west is east mirrored and so are the two diagonals on that side,
which is why `sorachr.bin` is 15360 bytes and not 24576. The mirror is a bit in
`actFlags`, beside `Large` and `Shadow` — **simulation state, in the actor
table** — and this routine is the only thing that writes it for the player.
Without it Sora walks west in the eastern art and nothing in the actor table
disagrees. `updateSoraFrame()` now returns the cel index (`facing * 6 + frame`,
0..29) and writes the bit; residency — `soraFrameCur` and the DMA request — is
§M7's, because that is a fact about VRAM and not about the world.

**The traces did not notice, and could not have.** `actFlags` is not a trace
column, and adding one would be redundant rather than useful: the flip is a pure
function of fields the trace already carries — Sora's from `pdir`, the fish's
from its `timer`, Riku's from his position — so a divergence in the *table*
would show up as a divergence in the input to it. What was untested was the
table itself, and that is a host test's job. Three of them now cover the five
facings, the clear-before-set (an `ora` without the `and` leaves him mirrored
for ever), the swing's two cels and the once-a-frame-not-on-a-frozen-one rule.

**One stale comment was worse than the gap.** `updateWorld`'s default case still
read *"Darkside, Armor, Orb, Mote, Fish and Riku have behaviour in world.s and
island.s and are NOT here yet"* — directly above the six cases that handle them.

**And two paths that were right but had no test.** Nothing kills Sora in any
scenario, because at one point of damage a cycle the Guard Armor would need four
thousand frames to do it, so the death ramp and the knockback decay were read
against the assembly instead: the brightness floors at **3** rather than going to
black so GAME OVER stays readable, `deadFlag` is handed over once and never
knocked back down from the 2 the scene writes, and `asr1` is an *arithmetic*
shift — **-1 is its fixed point**, so a westward knockback decays to exactly one
unit a frame and stays there while an eastward one reaches zero. Replacing the
shift with a division fails that test on the sixth frame.

## Darkside, and the bug the oracle found

`updateDarkside`, `updateOrb`, `playerUnderBoss`, `hurtBoss` and the slam, sweep
and orb volley are ported, and every interval matches the oracle exactly:
`SlamUp` 45 frames, `SlamHit` 23, `Rest` 57, `OrbUp` 41, `OrbFire` 31 — each its
constant plus one — with a Shadow crawling out of the fist and three orbs from
the volley.

Reaching the boss for a fixture would have meant a four-hundred-frame input
script full of guesses, so the oracle gained `--poke`: two WRAM bytes,
`sceneId` and `deadFlag`, make the ROM walk its **own** retry path into the
fight. The setup is the game's code, not a hand-built state.

**And the port disagreed with the oracle by three frames, which turned out to be
a bug in the SNES build.** `DarksideSlam` holds the impact point in `tmp0`/`tmp1`
and spawns a Shadow there — and `SpawnActor` ends with `jsr SetActorZ`, whose
call site is commented *"clobbers tmp0-tmp4, all of which are spent"*. Here they
are **not** spent: the damage test reads them back, and `SetActorZ` has left the
position shifted right by four. The test compares a Q12.4 coordinate against one
sixteenth of one and misses by ~3960 against a tolerance of 160.

**Darkside's fist cannot damage Sora, wherever he stands.** Only the sweep and
the orbs can. The three frames were the hit-stop the SNES never incurred. No
hand-written expectation would have caught it, because the same misreading that
wrote the port would have written the test. Audit finding 59; the port
reproduces it, because a silent fix would make every trace diff meaningless.

## The Guard Armor, and a hole in the opcode table

`updateArmor`, `placeHands`, `stepArmor` and `armorSlam`. Reached the same way —
`--poke sceneId=6 --poke townStage=5 --poke deadFlag=2`, and `TownRestart` brings
the armour down from the top by itself. Every leg of its cycle matches the
oracle: Drop 41, Walk 105, Wind 41, Slam 24, Rest 51, Walk 97.

Two of those carry a hit-stop and the numbers are how you can tell: Walk's first
pass is 97 + **8** for the landing freeze, the heaviest in the game, and Slam is
21 + **3** for the fist connecting. **The Armor's fist does connect**, where
Darkside's cannot — `ArmorSlam` spawns nothing, so the scratch it reads back is
still the mark. That contrast is what makes finding 59 specific to the Shadow
crawling out of the other boss's fist rather than a general flaw.

The hands are traced too, and the oracle settled two things reading would have
left ambiguous: only the **right** hand strikes, and `placeHands` is **not**
called during the drop, so both gauntlets arrive with the body instead of
reaching out ahead of it.

**The first Armor run desynced the interpreter**, at the same address the
Darkside run had — and it was a real gap. `ror` is in the ROM and no source line
says so: it comes from the `ASR1` macro, `cmp #$8000 / ror a`, and a macro puts
several instructions on one listing line under the macro's own name, so the
extractor skipped the line whole. `snes_opcodes.py` now also **decodes every byte
run a macro emitted** and reports what will not decode, which closes the class
rather than the instance. 90 opcodes, and `ror` is implemented — an arithmetic
shift right, which is why `grid.h`'s `asr1()` floors.

That longer run also re-measured the scene-load overrun at **113.5%** of a frame.

## The last three, and the whole of `UpdateWorld`

`updateFish`, `updateMote` and `updateRiku`, with the `raceWp` table carried
across verbatim — it was the one spawn table finding 58 named that had not been.

**Riku is checked against the oracle**, reached with a two-stage poke: restart
onto the island so `InitWorld` builds the cast, then start the race thirty frames
later on top of it. He therefore runs from where he *sits*, tile (27,8), not from
the start line — which exercises the waypoint walk from an arbitrary position and
took 165 frames of fixture to pin down. **A single Q12.4 unit off his step fails
8 checks.** He is driven with **no `SceneGround` at all**, deliberately: he never
calls `tryMoveActor`, so if a future change made him collide the test would stop
matching at once (finding 9).

The fish caught me out in a way worth recording: its timer is **incremented then
tested**, so the turn happens *on* frame 32 rather than after it. Thirty-one out
and one back is +30, not the +32 I first asserted. A 64-frame cycle still closes
exactly — 31 east, 32 west, 1 east.

**`updateWorld` is now complete.** Every actor type `world.s` and `town.s` give
behaviour to has it here: Sora, the Shadows, both bosses, the slash, the orb, the
fish, the mote and Riku. Everything else — props, pickups, islanders, gauntlets,
the dark column — is inert in the assembly too, and is inert here by having no
case rather than by being skipped.

The dispatcher is written so their absence is **inert rather than wrong**: an
actor whose type has no case is simply not updated, which is exactly what the
assembly's comparison chain does for a prop. So the Station of Awakening, the
night's search and the Second District's wave simulate completely; a boss fight
simulates everything except the boss. And now that the oracle exists, the trace
will say *which frame* it first matters on rather than leaving it to be found.

---

## The original brief

Make the SNES build testable against yours.

Two emitters producing the same format:

- **SNES side:** drive `tools/playtest.sh` and dump per-frame state. Mednafen
  save states are gzipped; WRAM is locatable inside one, and
  `platform/snes/build/kh.map` gives every symbol's address. Note
  `playtest.sh`'s step timings are **wall-clock sleeps**, not frame counts, so
  they drift — a frame-accurate harness needs a different driving mechanism, and
  establishing one is part of this milestone.
- **Host side:** the same fields, from the Tier-1 simulation.

Format: one line per frame, tab-separated, with a version header naming the
platform and the git revision of the run — not a hard-coded constant.

Fields: frame number, player x/y/z/dir/state/timer/hp, then per live actor its
index/type/x/y/state/timer/hp, then the stage bytes and `bossHP`.

`tools/trace_diff.py` compares two traces and **reads
`docs/behaviour/divergences/` to know which field differences are expected**,
reporting only unexplained divergence.

Determinism hazards to handle explicitly: the RNG is a 16-bit Galois LFSR seeded
to `$ACE1` by `InitWorld` and to `$1D57` by `TownBegin` — seed yours identically
and advance it at the same points, or the Heartless spawn positions diverge
immediately and the diff is worthless.

**Exit criteria.** A trace from each platform for the same scripted input, and
`trace_diff.py` reporting either zero unexplained divergences or a specific list
with the frame and field of each.

---

# §M7 — The device tier — **Tier 2, blocked here**

Requires devkitPro. Verify first; if absent, report blocked and stop.

In order: two-screen init consuming `vram_map.h` → 2D tilemap ground renderer
implementing `GroundRenderer` → sprites and the Y-sort → the bottom screen (HUD,
command menu, minimap) → touch input → the 3D quad backend as a second
`GroundRenderer`.

On the 3D backend: the limits are **2048 polygons and 6144 vertices per frame**.
A quad is one polygon, so a whole 64×64 ground is 4096 quads — 2× over. But the
visible window at 256×192 is ~192 tiles, ~400 with tilt and zoom margin, which is
comfortably inside. **Frustum culling is therefore the load-bearing piece and
coplanar tile merging is an optimisation.** Build the visible-set walk, measure,
and only merge if the measurement asks for it.

---

# Appendix — task graph

```
M0 (host build)
 └─ M1 (data model)
     ├─ M2 (asset backend)      independent of M3
     ├─ M3 (movement/camera) ── M5 (scenes) ── M6 (oracle)
     └─ M4 (vram_map.h)     ── M7 (device tier, blocked)
```

`M2` and `M3` are genuinely parallel. `M5`'s four scenes are parallel with each
other. `M4` is one agent and blocks all of `M7`. `M6` needs `M5` because there is
nothing worth tracing until the stage machines run.

Nothing in `M0`–`M6` requires a DS toolchain. If work stalls waiting for one,
the sequencing has been broken.
