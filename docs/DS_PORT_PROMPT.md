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

# §M2 — A DS backend for the asset pipeline — **Tier 1**

`tools/build_assets.py` currently emits SNES formats. Add a second backend so the
same `assets/*.txt` produce DS-consumable data.

**Read `tools/build_assets.py` before deciding what to change.** The
platform-neutral half — `load_grid`, `TERRAIN`, `GROUP`, `FACE`, the `draw_*`
tile art, `build_world` — is reused unchanged. Only the encoding is
SNES-specific: `tile_4bpp`, the 4 bpp packing, and critically
`dedupe_tilemap`'s output ordering, which interleaves two 32×32 screens because
that is how the SNES PPU stores a 64×32 map. **The DS does not use that
layout.** Read the comment at that code and understand it before you write the
DS emitter.

Deliverables:

- A `--target ds` flag (default stays `snes`, and the SNES path must be
  bit-identical afterwards — Gate 0 proves it).
- DS output: tile character data, a tilemap in the DS's own screen layout, and
  palettes in DS 15-bit BGR order. Collision and height maps are plain
  `MAP_W * MAP_H` byte arrays and carry over unchanged.
- A `.h`/`.bin` pair per scene, or a single archive — your choice, but document
  it and keep it stable.

**Exit criteria.** `python3 tools/build_assets.py` with no flag reproduces
`docs/oracle-baseline.sha256` exactly. `--target ds` emits files for all five
maps. `python3 tools/check_map.py` still passes. A host test loads a generated
collision map and asserts a known-walkable and a known-blocked tile from
`assets/island.txt`.

---

# §M3 — Movement, collision and the camera — **Tier 1**

The heart of the port, and it must be bit-exact.

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

# §M4 — The VRAM map — **Tier 1 (a header), blocking all of Tier 2**

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

- Stage machines as `enum class`, transitions exactly as `BEHAVIOUR.md` §6 gives
  them — **and read `BEHAVIOUR-AUDIT.md` findings 7, 8, 14, 15, 16 first**, which
  correct that section's day-change and race transitions.
- The dialogue script format is unchanged: bytes ≥ 32 are characters, below are
  control codes (`SC_END`, `SC_NL`, `SC_PAGE`). The scripts themselves port
  verbatim from the assembly's `.byte` runs.
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

---

# §M6 — The trace oracle — **Tier 1**

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
