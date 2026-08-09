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
python3 tools/check_constants.py            # game.inc vs the DS, BY VALUE
python3 tools/check_divergences.py          # the divergence corpus, against
                                            #   the format and the scenarios
python3 tools/check_worldsizes.py           # docs/WORLD_SIZES.md's figures,
                                            #   recomputed from what owns them
python3 tools/build_scripts.py --check      # gen/scripts.h vs the frozen ROM
python3 tools/build_doors.py --check        # gen/doors.h vs the authored
                                            #   assets/ds/town_doors.txt
python3 tools/snes_opcodes.py               # the table vs what ca65 emitted
make -f platform/ds/host/Makefile.host run  # the host suite, both orders
```

**The doors line closes a hole that the obvious reading hides**, and it is worth
stating exactly, because "the host suite already checks the doors" is true and
still leaves it open. `host/tests/test_doors.cpp` compares the committed
`platform/ds/include/gen/doors.h` against the emitted
`assets/gen/ds/<scene>doors.bin` index for index, so a header whose door *tiles*
or *count* have drifted away from the maps fails there, loudly. What that
comparison cannot see is the half of the table the binary does not carry:
`<scene>doors.bin` is `(i, j, land_i, land_j)` and says nothing about the
destination, the gate, or the decision that two of the six district doors are
shop fronts with nothing behind them. Those three columns exist in exactly one
place a human writes — `assets/ds/town_doors.txt` — and exactly one tool reads
it. **So edit that file alone and every other gate exits zero.** Measured, not
assumed: moving the First District's exit from tile 24 to tile 16 and shuttering
24 in its place leaves `check_map.py`, `check_modes.py`, `check_constants.py`,
`check_divergences.py`, `check_worldsizes.py`, `build_scripts.py --check`,
`snes_opcodes.py` and the whole host suite all passing, while
`build_doors.py --check` alone says `platform/ds/include/gen/doors.h has drifted
from assets/ds/town_doors.txt`. Without the line in this block, the authored
table and the shipped table disagree in silence: the header is committed, so a
stale one compiles, links, passes every test and ships a door that leads
somewhere the source of truth says it does not. A generator that refuses nine
numbered classes of wrong wiring refuses nothing at all on a commit where nobody
runs it.

**The worldsizes line is the same argument applied to a document.**
`docs/WORLD_SIZES.md` is mostly arithmetic — walkable counts, unique characters,
cast sizes, map extents — every figure of it printed by `build_assets.py` or
`check_map.py` and then transcribed by hand, with nothing comparing the two
afterwards. `tools/check_worldsizes.py` recomputes them from the things that own
them, and its second layer is what makes it a check on the *document* rather than
on the figures somebody happened to think of: every digit run in the file must be
inside a checked span or named in its `EXCUSED` list with a prose reason, so a
new figure written into the file with no source fails at its line. It takes about
two and a half seconds, most of it `check_map`'s window sweep called in-process,
and needs neither the ROM nor the 65816 interpreter.

**And when the simulation changes**, `python3 tools/trace_check.py` — about a
minute, both machines, eight scenarios, camera included since format v2. Not in Gate 0 because it needs the ROM
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
id: 001
spec_section: "7,9"
trace_fields: [camY, bgVOfs]
scenes: [*]
platform: ds
reason: 256x192 screen; the camera centres on playerY-96, not playerY-112
---

# A one-line heading, which is what the suppression report calls it
```

Every key there is **required**, and `tools/check_divergences.py` in Gate 0 is
what makes that true — the template above used to be missing `platform:` and
`scenes:` and would now be rejected by the tool it is a template for.

- **`scenes:`** is the list of `trace_check.py` scenarios this applies to, `[*]`
  for all of them, or `[]` for "no scenario exercises this" — which is a real
  and honest state and then needs a `conditional:` saying why. There is
  deliberately no default: a divergence that applied everywhere because nobody
  said where it applied is the exact failure this key was added to fix.
- **`unreachable:`** is optional and lists the `trace_fields` names the trace
  format cannot carry. Declaring them turns a silently dead suppression rule
  into a stated one, and the check runs both ways: an undeclared dead name is a
  typo, and a declared name that has become a column is a stale declaration.

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

# §M0 — Prove the host tier, and nothing else — **LANDED**, and audited

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

## The audit: the build was lying in three places, and the harness in one

§M0 is the smallest milestone and the one everything else stands on, so its
failures are the ones that look like other people's bugs.

- **`trace_main.o` had no dependency tracking at all.** `-include
  $(OBJS:.o=.d)` covered the test binary's objects; the emitter's object was in
  `TRACE_OBJS` and nowhere else, so its `.d` file was written on every build and
  read on none of them. Editing `trace.h`, `stage.h`, `world.h` or
  `constants.h` left it stale and silent. For a *test* binary a stale object
  shows up as a failing assertion; for the trace emitter it does not — it is the
  tool the whole oracle comparison trusts, and a stale `trace_main.o` linked
  against a fresh library disagrees about the layout of `SceneView` and `Camera`
  rather than failing to build. **The failure mode is not red, it is a trace
  that is quietly wrong.**

- **Deleting a test file did not relink.** Every remaining prerequisite is older
  than the target, so make is right to do nothing — and the binary goes on
  running the deleted file's cases. Demonstrated at 188 cases from 187 files.
  A stamp of the source *set* is now a prerequisite of both binaries.

- **Nothing ran the cases in any order but one.** They share a process and a
  good deal of file-scope state, so a case that only passes because an earlier
  one left something behind is a real failure that goes red the day somebody
  inserts a test above it — and the blame lands on the insertion. `run` now runs
  the suite twice, forwards and reversed, and requires both.

- **Two cases could share a name.** `KH_TEST` makes the function `static`, so
  the linker will not catch it, and a failure in either would be reported as the
  other. `ktest::add` checks.

## And the compile-fail checks, automated — which found something at once

The brief asked for them "documented not automated", and documented is where
they stayed. A type-safety property nothing exercises is a property that has
already stopped holding by the time anybody notices — the same shape as a table
with no consumer, a divergence that cannot fire, a routine with no caller.

`platform/ds/host/nocompile/` holds one file per rejection and
`make -f Makefile.host typecheck` requires the compiler to refuse each. Five:
`World w = 3;`, `World + Render`, `TownStage == DiveStage`, arithmetic on an
`ActType`, and a bare `int` where a `Dir` is wanted. All five verified by
loosening each in turn and watching the target catch it.

Writing them found the thing this section is really about. They are the first
code in the project to spell `kh::World` **from outside the namespace**, and it
does not compile: **`fixed.h` was the only header not in `namespace kh`.** It
worked because `constants.h` includes it before opening the namespace, so
unqualified lookup from inside found the global one, and every user is either
inside `kh` or says `using namespace kh`. Nobody had ever written the qualified
name — except four files' worth of comments, which were wrong. `World`,
`Render`, `TILE_PX` and `tileOf` are short generic global names in a target that
links libnds, which is C and full of short generic global names.
  If either does compile, `fixed.h` is wrong — report it, do not work around it.

**Exit criteria.** `make -f platform/ds/host/Makefile.host && ./<binary>` exits
0. Gate 0 still passes. No file outside `platform/ds/host/` and
`platform/ds/source/` was modified.

---

# §M1 — The engine's data model — **LANDED**, and now checked by a tool

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

## The audit: nothing was checking the table of numbers

The brief was met — every constant transcribed, the SNES name in a comment
beside each — and then **nothing checked it afterwards**, which is the only way
a table of numbers ever fails. `tools/check_constants.py` is that check, and it
now runs in Gate 0: it parses every `NAME = value` out of `game.inc`, resolves
`$hex` and `CELL_X(n)`, and compares against the DS headers **by value**.

Why by value. A *missing* constant is the cheap failure — the build breaks, or a
reviewer notices. The expensive one is a constant that is **present and wrong**,
44 where the assembly says 40, because that is a game which plays almost right
and a trace that diverges four hundred frames later.

It resolves 300 of `game.inc`'s 326 and excuses 24 **with a reason each**, and a
stale excuse — one for a constant that no longer exists — is itself an error,
because an exclusion list nobody prunes is how a checker quietly stops checking.
It handles the four shapes the port uses: `enum class` families matched by
enumerator, the two opaque-identifier namespaces (`sprite`, `pal`), the `NEED[]`
array indexed by `Item`, and constants the DS **derives** rather than names —
the Shadow's cels 1-3 are `Heart0 + 2n`, and the derivation is checked rather
than waved through. The eight numbers that differ **on purpose** name their
divergence and are checked against the DS's preserved-SNES-value alias
(`SNES_SHADOW_MAX`, `SNES_DIVE_R`, `SNES_MAX_ACTORS`, …).

## What it found

**Eight constants had gone missing**, all of them world positions:
`START_SORA_X/Y`, `START_RIKU_X/Y`, `PAOPU_X/Y`, `FINISH_X/Y`. They were not
absent so much as *un-named* — I had written `tileCentre(27), tileCentre(7)` at
the one place that used them, in §M5's interaction layer, the commit before
this one. That is the failure this checker is for: not wrong, just anonymous,
and the next person to move the paopu tree moves one of the two copies.

**And the reason two of them had no home: `PlaceRacers` was never ported.**
`OfferRace` runs it between arming the countdown and saying the challenge line,
and nothing in this port called it — so the race began with Riku wherever he
happened to be sitting. §M3b's race fixture had already *noticed*, and recorded
it as a property of the fixture rather than as a defect: "he is running from
where he SITS — tile (27,8) on the small island — rather than from the start
line". It is ported now, velocity-clearing included, which is the half of
`PutActor` people forget: a racer still carrying a step slides off the line.

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

## The audit: the camera had never been compared against anything

Every routine in `grid.s` has a counterpart — `TileIndex`, `TileWalkable`,
`TileHeight`, `StepOk`, `StoreZ`, `TryMoveActor`, `TileToWorld`, `UpdateCamera`
— and movement and collision are checked against the oracle on every frame of
every scenario. **The camera was not**, and the reason was structural rather
than an oversight: `camX` and `camY` were not columns in the trace format, so
the emitter had no reason to run `updateCamera` and did not.

Two consequences, and the second is worse than the first:

- The one part of §M3 with two **mandated divergences** — centring on
  `playerY - 96` instead of `-112`, and not reproducing the SNES's
  `(camY - 1) & 0x3FF` — was verified only by host tests written against the
  same reading that wrote the code.
- **Divergence 001 could never fire.** Its `trace_fields` are `[camY, bgVOfs]`,
  and neither was a column, so it excused nothing and its suppression count was
  always zero. A suppression rule that cannot suppress looks exactly like one
  that has nothing to suppress.

**Trace format v2** adds `camX`, `camY`, `bgHOfs` and `bgVOfs`, and the emitter
runs `updateCamera` where `MainLoop` does — between `UpdateWorld` and
`BuildOam`. `camX` and `bgHOfs` are **byte-identical to the SNES** across all
2925 compared frames. `camY` and `bgVOfs` differ by construction, so
`trace_check.py` lifts those two out of the byte comparison and checks them
against their **definitions on both sides** instead:

```
camY   = clamp(playerY_px - SCREEN_H/2, loY, hiY)     112 there, 96 here
bgVOfs = camY here, and (camY - 1) & 0x3FF there
```

A tolerance would not have been enough. A DS that centred on `-112` like the
SNES produces the *same* `camY` on any unclamped frame, so a range check passes
it; computing both sides from `py` — which is a column — catches it at frame 43
of the town. That was checked by making the change and watching it fail.

**Two smaller things the columns found the moment they existed.** The reset path
runs `UpdateCamera` once before `MainLoop` (main.s:120), so the oracle's frame 0
already has the camera on the player and the DS's had it at the origin. And
`bgVOfs` reaching **1023** whenever `camY` clamps to zero — which the Second
District does — is the PPU quirk in the one place it is most visible.

**And the generalisation, now reported rather than discovered.** `trace_diff.py`
prints which divergences *cannot fire against this format*, because every field
they name is state the trace does not carry. Today that is 005 (`txtState`,
`txtPtr`, `txtRow`) and 006 (`mosaicAmt`) — both legitimately describing things
the format has no column for, and both now saying so out loud instead of
reporting a suppression count of zero that reads like agreement.

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

**Done.** `platform/ds/include/vram_map.h`, `host/tests/test_vram.cpp` — 14 cases,
`host/tests/test_oam.cpp` — 4 more.
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

**And then a re-audit**, asking the question the rest of this sweep has been
asking: *is everything present used, and is everything used present?* The answer
was no in both directions, in five places, and they share one cause — the file
allocated everything it had a `Region` for and left everything else as a bare
address.

- **OAM was named and never allocated.** Two lines, `OAM_MAIN` and `OAM_SUB`, with
  no size, no entry count, no partition, no assertion, and no reader anywhere in
  the tree. Everything else in the header is allocated to the byte. And OAM is
  the half of the sprite budget that actually binds: object VRAM is 32 KiB of
  resident cels uploaded once, while OAM is **128 entries per engine re-competed
  for every frame**. It is allocated now — `OAM_BYTES`, `OAM_ENTRIES`,
  `OAM_ENTRY_BYTES`, `oamEntry()` — and the first thing that fell out is that the
  two OAMs are **adjacent**: a 129th main-engine entry *is* the sub engine's
  entry 0, so the symptom of overrunning is a sprite on the other screen.
- **An entry is 8 bytes and only 6 of them are yours.** The affine matrices are
  *interleaved* through OAM — matrix *n* is the fourth halfword of entries
  4*n*…4*n*+3 — so clearing OAM clears the matrices, and writing 128 packed
  6-byte records fits in 768 bytes, faults nothing, and puts every sprite after
  the first at the wrong address. **The SNES cannot catch this**: its entry was
  4 bytes plus 2 bits in a separate high table and it had no matrices at all, so
  there is no oracle behind this one and it had to be pinned rather than diffed.
- **`MAX_OBJECTS = 128` was a hardware number in the gameplay header**, which
  `constants.h`'s own docstring forbids — *"Nothing SNES-hardware-specific is
  here: no VRAM addresses, no PPU register values."* It is the OAM entry count.
  Worse, it was asserted against **nothing**: `constants.h`'s
  `OBJ_BUDGET_SCENERY + TRANSIENT_ACTORS + 4 + 1 <= MAX_OBJECTS` compared the
  budget with a 128 typed two lines above it, which is a tautology wearing a
  check's clothes. Proven by setting it to 127 — the old assertion passed, the
  new one fires. Tied through a guarded block keyed on `KH_CONSTANTS_H_INCLUDED`,
  the same idiom `gen/assets.h` already used, and **in that direction on
  purpose**: `constants.h` could include `vram_map.h` and get the tie
  unconditionally, but then the whole platform-neutral simulation would need the
  DS's VRAM layout to compile.
- **The four window base addresses reached nothing.** Every `Region` in the file
  is an *offset*, and `MAIN_BG_BASE`, `MAIN_OBJ_BASE`, `SUB_BG_BASE` and
  `SUB_OBJ_BASE` are what those offsets are from — and no code, test or
  assertion performed the addition. Four transcribed addresses with no consumer
  is four chances for a wrong digit to reach §M7 and surface as a layer drawing
  the wrong thing, which is the exact failure this file exists to prevent and the
  one kind of it the file was not defending against. `windowBase()` and
  `address()` compose them now, all fourteen regions have their real address
  asserted, and `ADDRESS_MAP` states the seven spans of the whole graphics
  address space and proves them disjoint — so a wrong digit lands inside another
  span and stops compiling.
- **`Assignment::ofs` and `Assignment::offset` were written nine times and read
  none.** Eighteen numbers, all zero, checked by nothing — and the recovery paths
  at the bottom of the file are an invitation to set one. Three traps wait there,
  all of them in the transcribed table already: **E, H and I have no OFS field at
  all** ("Offset not used by VRAM-E,H,I"), so a non-zero `ofs` is a bit the
  silicon ignores; **the legal range depends on the use**, since A and B take 0–3
  as main BG and only 0–1 as main OBJ; and **F and G are not linear**, their
  offset being `4000h*OFS.0 + 10000h*OFS.1`, so OFS 2 is 64 KiB and not 32 and
  their palette slot is `OFS.0 + OFS.1*4` — which is *why* they reach slots 0, 1,
  4 and 5 and never 2 or 3. `ofsMax()`, `bankWindowOffset()` and `bankSlot()`
  encode all three, and `everyOffsetIsLegal()` checks every row against them.
  It also caught a second statement of the same fact: `TEXTURE_SLOT` and
  `ASSIGNMENTS`' row for bank A both said where the texture lives, and nothing
  tied them, so moving the texture to bank C for the extra slot would have left
  `TEXTURE_SLOT` saying 0.

**One consequence of an already-documented recovery path turned out to be
undocumented.** The character-ceiling finding above — *a region's size in bytes
is not the limit a caller runs into* — had not been applied to sprites. An OBJ
tile number is ten bits too, but it counts in units of the 1D boundary, so the
ceiling is whichever binds first: the reach the boundary buys, or the bank behind
the window. On the main engine the reach binds, which is what makes
`OBJ_BOUNDARY64` a reserve. **On the sub engine the bank binds, by a factor of
two** — bank I is 16 KiB, so tile numbers 512–1023 name addresses past the end of
it and engine B draws whatever unmapped VRAM returns. So `SUB_OBJ_TILES = 512`,
stated rather than inferred. And raising the boundary to 64 — which *is* the
documented way to get a fourth resident object page — quietly halves that to 256.
Every assertion in the file passed at boundary 64 before this pass, so the trade
was invisible; it is in *WHAT THIS GIVES UP* now.

**Nine assertions were broken on purpose and every one fired**, each reverted:
moving `OAM_SUB` a kilobyte, mis-stating the affine interleave as 16 slots,
putting `SUB_BG_BASE` on the main OBJ window (which fired five region addresses
*and* the disjointness proof), setting `ofs = 1` on bank E, giving bank B an
offset its OFS does not produce, pointing `TEXTURE_SLOT` at slot 2, raising
`SUB_OBJ_TILES` past its bank, dropping `MAX_OBJECTS` to 127, and regenerating at
boundary 64. The last two are the ones worth noting: **127 passes every check
that existed before this pass**, and **boundary 64 passed every check that
existed before this pass** — both are exactly the silent-wrong-picture failure
the file was written to make impossible.

`vram_map.h` stays frozen and every edit here was additive: no address moved, no
region resized, no assignment changed. The file now says what it always meant.

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
    in three more bytes per row. **That reconstruction is done** — the three
    missing columns are authored in `assets/ds/town_doors.txt` and
    `tools/build_doors.py` emits `platform/ds/include/gen/doors.h` from them, so
    do not build a second table. Read `docs/TRAVERSE_TOWN.md`'s "The doors on the
    DS" before touching either file; the far-side landing is *derived* from the
    reciprocal door and must never be typed by hand, and it is a different tile
    from `<scene>doors.bin`'s near-side `land_i/land_j`. `build_doors.py --check`
    is in Gate 0.
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

## What is still open, and why it is no longer a data question

`CheckDoors` needs four things per door — where it is, where it lands, **which
district it leads to**, and **which stage the town must have reached**. The
SNES's `doorTable` row is seven bytes and carries all four. The DS's
`<scene>doors.bin` row is four and carries the first two, because `scene.h`
decided that "which district it leads to is scene logic and the table
deliberately does not say".

That decision stands, so `townInteract` takes a `TownDoor[]` — position,
destination, landing, gate — and the mechanism is complete and tested. **This
section used to end here, saying that nothing built that array for the DS's own
maps and that filling it in belonged with whoever drew them. It has been
built.** The three columns the binary does not carry are authored in
`assets/ds/town_doors.txt`; `tools/build_doors.py` resolves the stage constants
out of `game.inc`, parses `doorTable` live out of `town.s`, refuses nine
numbered classes of wrong wiring before it will emit anything, and writes
`platform/ds/include/gen/doors.h` in `<scene>doors.bin` order so the header and
the binary are one table read twice. The First District's two door tiles were
resolved as one SNES join plus one shop front that does nothing at any stage —
a content decision, recorded in the authored file and in `docs/TRAVERSE_TOWN.md`
rather than left to be inferred. `build_doors.py --check` is in Gate 0, and
`host/tests/test_doors.cpp` walks the shipped tables including a per-stage
reachability fixpoint over the real data. **Do not build a second table.**

**What remains open is the other half of the protocol, and it is a code
question.** Both halves of the door data now exist and neither has a production
consumer:

- **`townInteract` has no non-test caller.** Every call to it in the tree is in
  `host/tests/test_doors.cpp` or `host/tests/test_interact.cpp`; nothing in
  production hands it `townDoorsFor(scene)`. This is worth more than it sounds,
  because `townInteract` is also where talking to Cid advances `TownStage::Look`
  to `TownStage::Second` (`source/interact.cpp:383-394`, from `town.s:537-546`),
  and `town.s:541` is the only write of that stage in the SNES source. So the
  missing caller is *also* the reason the gate on the First District's exit is
  unreachable in production: the table is live only when somebody calls the
  function that opens it.
- **`SceneAction::EnterDistrict` is emitted and never performed.**
  `TownMachine::doorStep` returns it with the destination in `arg`
  (`source/stage_town.cpp:71`) at the midpoint of the transition `openDoor`
  started. The only performer in the tree is `perform()` in
  `host/trace_main.cpp:398-491`, whose switch handles eight of the twenty-eight
  actions and sends the rest — `EnterDistrict` among them — to a `default:` arm
  that stops the run and names the action. That is deliberate and loud: a
  silently skipped transition would put the trace on a different timeline and
  the diff would blame a frame hundreds later.
- **There is no `LoadScene`.** No function of that name exists anywhere in
  `platform/ds/`; every occurrence of the word is a comment about what the
  SNES's did. There is no scene record naming a map, a palette and a cast, and
  nothing walks one — which is why exactly one of the nine cast files is read at
  runtime, and only by the trace harness (`host/trace_main.cpp:747-752`).

So the town's doors are complete, generated, checked and tested, and at runtime
they still lead nowhere. That is **one** open item and not three, it is the same
hole §M6 found in §M3 and this milestone one layer up again, and
`docs/WORLD_SIZES.md` tracks it under "Consequences still open" as *nothing
loads a scene*. Do not re-open it as a data question: the data has been finished
twice.

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
NTSC frame. Measured: ordinary frames run at **31.8%** (31.9% before the
re-audit below corrected the cycle rule), the reset path at 584%
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

**That is now closed, and it was worse than the paragraph above thought.**
See the re-audit below.

---

## The re-audit: the divergence corpus was data nothing validated

The question the rest of this sweep asks — *is everything present used, and is
everything used present?* — has an unusually sharp answer here, because a
divergence file is not documentation. It is **data that switches off part of the
only check comparing the port against the frozen ROM.** Six files, each a rule,
and until this pass the only thing that read them was `trace_diff.py` — which
runs only under `trace_check.py`, which needs the ROM and the 65816 interpreter
and is deliberately not in Gate 0. So neither was the corpus.

**The scope hole was load-bearing, and here is the proof.** Take the `station`
pair — a scenario that holds the *oracle's own* collision map and cast positions
equal precisely so that any difference at all is a difference in the code — and
move Sora sixteen raw units south on one frame. Before this pass:

```
$ trace_diff.py snes-station.trace ds-station-bent.trace
  004       1 difference(s)  The Stations of Awakening are drawn smaller
no unexplained divergence.
$ echo $?
0
```

**Exit 0, on a trace with the player in the wrong place.** Divergence 004
describes a *disc radius* and it silently excused a movement bug, in a scenario
whose entire purpose is that content is held equal. With `--scene station` the
same pair reports `FIRST UNEXPLAINED DIVERGENCE: frame 60, field py: snes=3304
ds=3320` and exits 1. In `dive`, where 004 genuinely does apply, it excuses 1604
differences — so the count alone could never have distinguished the two cases.

`--scene NAME` or `--any-scene` is now **required**: a blanket you asked for is a
different thing from one you got by default.

**Three of the five front-matter keys were parsed and dropped on the floor.**
`reason:` — the one line every file carries saying *why* the difference is
deliberate — was read and never printed; the report showed the `# heading`
instead. `conditional:` was read and discarded entirely, so divergence 006's
"applies only when the 3D quad `GroundRenderer` is the active one" bound nothing.
`platform:` was read and never compared against the pair being diffed.
`spec_section:` was not parsed at all, by anything, in any of the six files. All
four are now consumed, and the suppression report prints *what* each divergence
excused and *over which frames* rather than only how many — because a count
cannot tell "excusing the thing it describes" from "excusing everything".

**The dead-field report could only see a wholly dead divergence.** It listed a
divergence when *every* field it named was unreachable, so 003 — `nightTimer`
reaches a column through an alias, `shadowAlive` and `shadowSpot` reach nothing —
looked healthy with two of its three names inert. A partly dead rule is the more
dangerous kind, because it fires. Reported per name now.

**And two divergences turned out to describe things no scenario can see.**
003 is the night's density, and the night *fixture* calls
`NightMachine::setDensity()` to run at the SNES's six-at-seventy — which is what
makes an oracle comparison of the night possible at all. 006 needs the 3D
renderer, which no scenario runs. Both are `scenes: []` now, each with a
`conditional:` saying why, and `check_divergences.py` prints the count of them:
**two of six divergences are real in play and invisible to `trace_check.py`**,
which is the honest measure of how much of the port's deviation is actually
verified. A field name the format cannot carry is likewise declared, in
`unreachable:`, and checked *both* ways — an undeclared dead name is a typo, and
a declared name that has become a column is a stale declaration.

**`tools/check_divergences.py` is the standing check, and it is in Gate 0**
because it needs no ROM: front matter complete, ids unique and matching their
filenames, every field name reachable or declared, every scene a real scenario,
every `spec_section` a real section of `BEHAVIOUR.md`, every `platform` one the
traces carry. Eight deliberate breakages, all fired.

**The Gate 0 block in §0.5 was three steps out of date** and the divergence
template in §0.6 was missing `platform:` and `scenes:` — it would have been
rejected by the tool it is a template for. Both corrected.

*And then it happened twice more.* `tools/build_doors.py --check` and
`tools/check_worldsizes.py` were each written after the block, each written on the
stated assumption that it would run from Gate 0 — "`--check` belongs in Gate 0"
(`tools/build_doors.py:39`), "this is run from Gate 0"
(`tools/check_worldsizes.py:253`) — and neither was added to the block by the
pass that wrote it. Both went in later, on a review. **The pass that writes a new
check is the only one that knows the check exists**, so if it does not amend §0.5
in the same change, nothing will until somebody notices the gap by hand. Add the
line with the tool, not after it.

## ...and the interpreter was deciding operand width by hand

`snes_opcodes.py` exists so that instruction decoding comes from the assembler
rather than from a reference, and its docstring calls the hard-error on an
unknown byte "the strongest self-check available". One part of decoding was
outside it: **operand width**, the only property of an instruction's length that
is not a property of its opcode byte.

- `operand_len()` was **called by nothing**, and `IMM_WIDTH_FLAG`, the table it
  consulted, fed nothing else.
- `snes_cpu.py` **imported `FIXED_LEN` and never used it**.
- Instead the interpreter answered the width question at **nine hand-written
  call sites** — `self._operand("cpx", mode, self.x8)` and eight like it — plus
  a **tenth, separate, six-mnemonic literal** for the cycle penalty. Two lists,
  differing (the cycle one adds `stx`/`sty`), with nothing saying why.

A wrong answer there does not raise. The interpreter reads one byte where the
CPU read two, the decoder desynchronises, and the next opcode it decodes is an
operand — the exact failure the module was written to make impossible.

There is one statement now, `WIDTH_FLAG`, and it is **total with no default**,
because `.get(mnem, "m")` is how an index-register instruction added later
becomes accumulator-width in silence. `check_widths()` in Gate 0 asserts it
covers every mnemonic reaching a sized addressing mode and contains nothing else.

**Making it total found a real defect.** `jmp` and `jsr` take `abs`, so both
reached the cycle rule, and the rule asked *"is the operand eight bits?"* of an
instruction whose operand is a **destination**. Every jump executed with a
16-bit accumulator was charged an extra bus access. Conservative — the frame-fit
assertion could only ever have fired early, never late — but the busiest
ordinary frame of the Dive measures **113520 master cycles, 31.8% of a frame**,
not the 113970 and 31.9% quoted above. `WIDTH_FLAG` has a third value, `'a'`,
for an operand the P register does not size, and `eight_bit()` refuses to answer
for one. Traces before and after are **byte-identical**, verified across the
whole run as it then stood — seven scenarios; `fall` was added later and brought
it to eight.

---

# §M6b — The DS trace emitter — **LANDED**

**Done.** `platform/ds/include/trace.h`, `platform/ds/source/trace.cpp`,
`platform/ds/host/trace_main.cpp`, `tools/ds_trace.py`, `tools/trace_check.py`,
`traces/*.txt`.

§M6 built the oracle and left the other half unbuilt: the format existed, the
differ existed, nothing on the DS side emitted a line. This is that half.

**The exit criterion is met, and by more than it asked for.** Seven scenarios are
**byte-identical to the SNES over 3323 frames**, with nothing suppressed and no
divergence file rescuing them — the two camera columns §M3 mandates a divergence
in are lifted out of the byte comparison and checked against the *arithmetic*
instead, which is a harder test and not a hole:

| scenario | frames | what it exercises |
| --- | --- | --- |
| `station` | 130 | the walk, the rim of the disc, one swing |
| `darkside` | 285 | rest → fist → orbs, the Shadow the slam leaves, an orb expiring |
| `armor` | 385 | `TownRestart` re-raising the armour, the drop, the landing freeze, the walk, the fist that connects |
| `town` | 885 | the Second District's wave off the `$1D57` seed, one refusal, the cap at five |
| `night` | 770 | the storm, six Shadows arriving off the `$ACE1` LFSR, the ceiling where the draws stop |
| `race` | 670 | Riku's whole waypoint course, every frame of it |
| `fall` | 198 | the drop between the stations: `DIVE_FALL`, the eight-way mote spread, and the slots the motes recycle through |

Each is identical from the first frame it emits — frame 0 for `station`, 15 for
the three that poke a scene in, 30 for the two that poke twice, and 2 for
`fall`, which is where its deferred poke lands. **`fall` was added after this
section was first written**, which is why the count says seven and not six; it
found no divergence at all, and adding a scenario nobody expects to fail is the
argument for it.

`tools/trace_check.py` runs all seven plus the eighth — `dive`, below — against
both machines in about a minute. It is not in Gate 0 because it needs the ROM
and the interpreter; run it whenever the simulation changes.

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

The traces agreed over 3125 frames when this audit was run — six scenarios then,
seven and 3323 frames once `fall` was added, which is the figure §M6b now carries
— and that is a statement about the code they *execute*. It is not a statement about the code that is missing: a routine with
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

# §M7 — The device tier — **Tier 2, still blocked, and now verifiably so**

Requires devkitPro. **Verify with `python3 tools/check_device.py`**, which exits
0 only when a device build is actually possible; if it exits 1, report blocked
and stop.

## The verification, run

The block was six sentences of prose in `platform/ds/README.md`. Re-running them
one at a time found **two had gone stale** — the image has acquired a Docker
client since, and `desmume` turns out to be one `apt-get` away in Ubuntu
universe. Neither changes the answer, and that is exactly what makes a stale
claim about a block dangerous: right for the wrong reasons, until the day it is
not, and nobody re-reads prose. So the block is a program.

| | |
| --- | --- |
| devkitPro | absent; `$DEVKITPRO` unset and nothing in the usual places |
| devkitARM | absent. apt's `gcc-arm-none-eabi` accepts `-march=armv5te` and ships **no matching multilib**, so it compiles and does not link |
| libnds | absent — and this is the piece people forget, because a `.nds` is not a bare ELF: it needs libnds's crt0, linker scripts, specs and headers |
| ndstool | absent |
| `apt.devkitpro.org` | **HTTP 403** through the egress proxy — an organisation *policy* denial. `/root/.ccr/README.md` is explicit: report the blocked host, do not route around it |
| Docker | a **client** is installed; there is no daemon at `/var/run/docker.sock`, and the error it prints reads like a permissions problem rather than an absent daemon |
| an emulator | absent, and **not required to build** |

Four of four required components are missing. §M7 is blocked, the host tier is
unaffected, and `check_device.py` also proves its *positive* path — given a
complete toolchain it reports "not blocked" and exits 0, and given apt's
`arm-none-eabi-gcc` it rejects it by name rather than accepting a compiler that
cannot link.

## What was deliverable without a toolchain, and was

`vram_map.h` opens by saying "the device tier turns these numbers into VRAMCNT
and BGxCNT writes and **adds nothing of its own**." That was a promise the file
did not keep. It carried the ingredients — a bank, a use, an MST, an OFS — and
left the composition to the one tier that has no way to check it. The first
thing §M7 does is write nine bytes, every one fully determined by the frozen
table, and not one of them was written down.

They are now: `vramcnt(Bank)` composes `MST | OFS<<3 | enable<<7`, and the nine
results are asserted individually so they are values somebody has looked at
rather than an expression nobody has evaluated — `A=0x83 B=0x81 C=0x80 D=0x80
E=0x82 F=0x83 G=0x80 H=0x81 I=0x82`.

**And the register addresses are not nine consecutive bytes.** `0x04000247` is
**WRAMCNT**, sitting between `VRAMCNT_G` and `VRAMCNT_H`. A loop writing nine
bytes from `0x04000240` does not merely misplace bank H — it writes H's control
byte into WRAMCNT and repartitions the 32 KiB the two processors share. Bank H's
byte is `0x81`, so bits 0–1 are 1: the ARM9 keeps only the *second* 16 KiB and
the first is handed to the ARM7 mid-initialisation, while the ARM9 is using it.
Not the worst of the four allocations — a byte ending in 3 would take all of it
— and that is precisely what makes it bad, because half a region disappearing
corrupts rather than halts. It is the most destructive one-line mistake
available in DS initialisation, it is written as the natural loop, and this is
the file that exists to stop it. `VRAMCNT_ADDR[]` has the gap and
`vramcntAddressesSkipWramcnt()` asserts it.

**One more constraint fell out of composing the byte.** "Bit2 not used by
VRAM-A,B,H,I" means those four have a **two-bit** MST field. MST 4 — sub BG on
C, sub OBJ on D — does not fit in two bits, and on such a bank the 4 loses bit 2
and selects mode 0, which is LCDC: the layer that wanted the bank draws nothing.
`everyMstFitsItsField()` asserts it, and the breakage that proves it is the
file's own warning made real — `mstFor`'s `case Use::SubBg: return b == Bank::C
? 4 : 1;` simplified to `return 4;`, which is the "a single *sub = 4* rule would
half work" mistake the comment beside it has always described. It now fails to
compile.

Five deliberate breakages, all fired: a VRAMCNT loop walking over WRAMCNT, the
addresses made consecutive, that `sub = 4` simplification, the enable bit
dropped, and bank F moved to palette slot 1 without its offset following.

## Step one is written, and it is tested — against a recording MMIO stub

`platform/ds/device/` exists now: `init.cpp`'s `initScreens()` is the whole
two-screen initialisation, and the host suite checks it on every run.

**There is no libnds stub, deliberately.** The obvious way to write device code
without a toolchain is to declare `videoSetMode()`, `vramSetBankA()` and the
rest and compile against them. That is a trap: a stub is a *claim about somebody
else's header*, unverifiable while the real one is absent, and code that
compiles against my declaration and not theirs is a green light with nothing
behind it — green in exactly the situation where nobody can tell.

So the initialisation writes **hardware registers**, which are not an API: an
address and a bit layout, quoted from GBATEK, with the addresses already
transcribed and asserted in `vram_map.h`. The only thing that differs between
device and host is what a store to `0x04000240` *does*, and that is one class
with two definitions chosen at **link** time — `device/mmio_device.cpp` (three
volatile stores, nothing else) and `host/mmio_host.cpp` (records the write).

**What the recorder buys**, and each item is something a careful person gets
wrong silently:

| | |
| --- | --- |
| the **values** | the nine VRAMCNT bytes are compared against `vramcnt(Bank)`, not against a copy |
| the **widths** | VRAMCNT is 8-bit; a 16-bit store to `0x04000240` configures bank B too, so `mmioTouches()` answers by **overlap** and sees a write that lands on an address without being addressed to it |
| the **omissions** | `WRAMCNT` is asserted *never written*. `vram_map.h` could only assert that about the address **table**; a log asserts it about the **code**, and a loop over `0x04000240 + i` passes every `static_assert` in the tree and fails here |
| the **order** | forced blank first on both engines, power, banks, layers, and only then the real `DISPCNT`s — so no frame is ever composited from a half-configured set of layers |

**Six deliberate breakages, all fired.** The natural `0x04000240 + i` loop (walks
over WRAMCNT), a 16-bit store to an 8-bit register, forced blank dropped, engine
A's held-back BG2 switched on, `DISPCNT`'s engine-wide char base made non-zero
(the consumer of the 62 KiB rule), and the box pointed at the overlay's map —
that last one fails at *compile* time, because the BGxCNT values are `constexpr`
and pinned.

**And the file nothing links is compiled anyway.** `mmio_device.cpp` is filtered
out of the host build — it would fault here — which means nothing touched it and
it would rot until the day somebody with a toolchain found that the one half of
the seam they could not test is also the half that no longer builds. It is now
in `typecheck` as the mirror image of the `nocompile/` files: required to
**succeed**, syntax-only. Proven by breaking it.

**What a green run here does NOT mean.** Nothing in this container has executed
an ARM instruction. These cases prove `initScreens()` writes what `vram_map.h`
says it should. They are no evidence that GBATEK is right, that the values are
the ones the hardware wants, or that a picture appears.

## Step two: the 2D ground and its streamer

`device/ground.cpp`. A DS text background addresses at most 64×64 characters —
512×512 pixels — and the island is **128×64**. So the window is a sliding view,
and the sliding is the class.

**The window is a torus, not a buffer.** The hardware wraps a 512×512 background
at 512 pixels with no help and no cost, so nothing is ever *moved*: what moves is
which map column each window column *holds*. A scroll of one character rewrites
one column — sixty-four entries — instead of blitting a screen. That is the same
technique the SNES used on its 64×32 window, and it is why 8 KiB of map RAM can
scroll a 1024-pixel map.

| property | why it is not obvious |
| --- | --- |
| a still frame writes **nothing** | 33 visible columns in a 64-column window is 31 of slack; a streamer that rewrote the visible columns every frame would look identical and spend 33 columns of DMA forever |
| the slide is a `while`, not an `if` | a door or the Shatter moves the camera further in one frame than the slack absorbs, and an `if` slides one column and leaves the rest of the window holding another part of the map — visibly, and only on the frames that jump |
| a jump past the window **refills** | the `while` would still be *correct*, walking one column at a time and writing hundreds nobody sees; the refill bounds the cost at one window |
| the scroll register gets `cam.bgHOfs`, not `cam.x` | they differ by `shakeX` — the Shatter and the Tear. `bgHOfs` **is** a trace column, so a streamer using `cam.x` would drop the shake and the oracle diff could never see it: the diff compares the simulation's value and never the register |
| `wrap()` is a positive modulus | C++'s `%` keeps the dividend's sign, so `-1 % 64` is `-1` and indexes before the window. This is the likeliest way to write a streamer that is perfect until something reaches an edge |

**The map is not in `SceneGround`, and that is deliberate.** `SceneGround` carries
collision and height — what the simulation needs and what the *3D* backend needs.
A character map is what the *2D* backend needs and what the 3D one would never
read. Widening the shared type would hand every consumer a field one of them
cannot use and make `GroundRenderer::load()` mean different things to its two
implementations. So the common part stays in the virtual and `CharMap` arrives
through `setMap()`.

**One overflow is refused rather than handled.** There is no row logic here at
all: the window is as tall as the tallest map anyone has authored, and a
65-character map would wrap onto itself and draw its top rows under its bottom
ones — a seam a third of the way up the screen that reads as corrupt data. It is
refused at load with the reason, and `ground_every_streaming_scene_in_the_pipeline_actually_fits`
walks all nine scenes to prove the claim holds today.

**Six breakages, all fired**, and each is a real way to write this wrong: a
signed `%`, the partial right-hand column forgotten, `cam.x` for `cam.bgHOfs`, an
`if` for the `while`, a flat row-major window ignoring the hardware's block
layout, and the height refusal removed.

**And the sixth found a defect in the test rather than the code.** Removing the
height rule made the suite exit **139 with zero output** — `CHECK` does not
abort, so `CHECK(g.error() != nullptr)` failed and the next line called
`std::strstr(nullptr, …)` anyway. A case that segfaults on the failure it exists
to detect is worse than no case: it reports nothing at the moment it fires. The
message assertions go through a `saysWhy()` helper now.

The cases run against the **real** `islandmap.bin`, every entry compared, walking
the camera pixel-by-pixel across the whole island and back. A streamer tested on
a synthetic grid would only agree with itself.

## Step three: the depth sort

`device/oam.cpp`. **This is the first piece of the device tier with a real
oracle.** The initialisation had GBATEK and the streamer had arithmetic; neither
had a frozen implementation to be right or wrong against. Depth does —
`platform/snes/src/oam.s` is 689 lines that already decided every one of these
questions, so the DS's only job is to answer them the same way, and every rule
is cited to the line that establishes it.

The scheme, in that file's own words: "sprites are drawn strictly in OAM order:
slot 0 is frontmost. Seen from three-quarters overhead, being further down the
screen means being nearer, so sorting actors by world Y descending … makes a
character walk behind a palm when north of it and in front of it when south,
with no per-object layer authoring at all."

| rule | why it is not obvious |
| --- | --- |
| the sort is **stable** | `oam.s:132`'s `bcs @place` stops on *equal*, so ties keep scan order — the actor's own slot number. Two actors on one row is the island's palm rows and the town's wave arriving on one line; `std::sort` would be free to swap them differently each frame and two still sprites would flicker past each other |
| the cull happens **before** a slot is taken | `oam.s:452-464` wraps the write, and the cursor advances only on a write. An off-screen actor is **free**, so 128 limits what is *drawn*, not what *exists* — backwards, that is a big map running out of sprites while showing almost none |
| depth is the **ground** position | the key is `lda actY,x`, before the `z*8` lift `feet()` applies. Someone on a raised deck keeps the deck's depth |
| the boss takes no actor slot | `oam.s:398-402` returns without advancing for a `Huge` actor, which is what lets it be in the sort at all |
| shadows are in **sort order** too | they are all behind the actors, but they overlap *each other* |

Two divergences reach here and both are forced: the cull rectangle is **32 lines
shorter** (001), so an actor between y 192 and 224 is drawn on the SNES and
culled here; and the ceiling is now **reachable** (002), because the pool is 128
against the oracle's 32, so a busy frame has four times the candidates for the
same 128 entries. The SNES could not fill OAM from a 32-actor pool; the DS can.

**Seven breakages. Six fired immediately — and the seventh is the useful one.**

Mutating the sort key to subtract the height lift — the mutation standing for
"sorted on screen Y" — **did not fire**. Chasing it found two real things:

1. **No test could tell world-Y sorting from screen-Y sorting.** A camera
   subtracts the same offset from every actor, so both orderings agree on every
   scrolling case that can be written. They part company only on *height*.
   `oamsort_depth_is_the_ground_position_and_ignores_the_height_lift` is the
   case that distinguishes them, and it did not exist.
2. **The comparison was written twice.** The loop compared
   `a.y[out[j-1]].raw()` against a key computed as `a.y[i].raw()` — the same
   comparison in two forms, agreeing only because both were the raw Y. The
   mutation changed one side and not the other and produced the right order by
   accident. Add a bias or a tie-break to the key and only one side follows,
   which is not an ordering error you can see but a comparator that is no longer
   transitive. It is a named `sortKey()` used on both sides now, and with that
   the probe fires.

A probe that does not fire is worth as much as one that does.

### ...and the attribute packing

`device/oam_pack.cpp`. `oam.cpp` decided *who* is drawn and in what order; this
decides what the hardware is told. It is the first sprite work where the SNES's
answer cannot simply be copied — the two machines lay an entry out differently
enough that this is a **translation**, and the four places they part company are
what the file is about:

1. **There is no high table.** X is nine bits inside `attr1` and the size is two
   more, so the whole `oamHigh` apparatus — the shift-by-slot, the
   read-modify-write, the 32 extra bytes DMA'd every frame — goes away. The one
   place the DS is straightforwardly simpler.
2. **There is no name-page bit.** The SNES reached its second 256-tile page
   through bit 0 of the attribute byte; a DS tile number is ten flat bits and
   the pipeline has already re-serialised each page cel-contiguous. So `AF_PAGE1`
   stops being a bit that is *written* and becomes a bit that *selects a base* —
   a port still looking for a bit to set would put every islander on top of the
   Heartless.
3. **The palette field is four bits, not three.** Sixteen sub-palettes against
   the SNES's eight. Nothing uses the extra eight, which is the correct amount
   of use for them until something needs one.
4. **The coordinate wrap is the behaviour, not a guard.** Y is eight bits and X
   is nine, so y = −16 is stored as 240 and the hardware draws rows 240–255 (off
   a 192-line screen) then 0–15 at the top. That is what makes a
   partially-off-screen sprite work and why the cull can accept anything from
   −32 without a second thought. A clamp would pin it to the edge and it would
   slide along instead of leaving.

The page bases are **derived** from `SPRITE_ASSETS` — Sora's 30-cel sheet, then
two 8 KiB pages, divided by the 1D boundary — because typing 480 and 736 would
be two numbers that stop being right the moment a page is resized. Raise the
boundary to 64 and both halve along with every cel index.

**`updateSoraFrame()` finally has a consumer.** `world.cpp:905` discards its
return with a comment saying the rest "is the device tier's half, because
`soraFrameCur` is a fact about VRAM and not about the world." This is that half.

**Unused slots are hidden explicitly**, which `ClearOamBuffer` does at the *top*
of `BuildOam` for a reason its name does not give: OAM holds what the previous
frame left in it, so a frame with fewer sprites would go on drawing the tail of
the last one, frozen. The park is 224 on both machines for different reasons —
the SNES's screen height, and here the largest value at which a 32-tall sprite
does not wrap back across the top. `static_assert`ed, and moving it to 240 fails
to compile.

**Nine breakages, all fired**: a clamp for the wrap, `& 1` for the flash parity,
`AF_PAGE1` as a bit, the page base added before `dsTileFor` instead of after, a
shadow inheriting the actor's palette, a shadow inheriting its mirror bit,
unused slots left alone, priority 1 instead of 2, and the park at 240.

## Step four: the bottom screen — and the half of the specification nobody was checking

**The bottom screen is three reservations and only one of them is a port.**
`vram_map.h` reserves `HUD_MAP`, `MENU_MAP` and `MINIMAP_MAP`; the HUD *is* a
port, because `hud.s` is 598 lines that already decided what it shows and where.
The command menu is **new** — the SNES had no such thing, and inventing one here
would be designing a game rather than porting one. The minimap is new too,
though less arbitrarily: it is a downscale of data that exists, but what it
should *look* like still needs a screen. Both stay reservations, which is a
better state than a guess: an empty region with a name gets filled in, a wrong
one has to be noticed first.

What moved is only *where*. On the SNES the HUD was BG3 of the only screen —
"the 2bpp layer and, with the mode-1 priority bit set, it draws above everything
else, which is exactly what a HUD wants." The DS has a second screen, so the
content is unchanged and the layer-priority argument evaporates.

### The real find: `text.inc` was checked by nothing

Building the gauge needed `CH_BAR_FULL`, and it was not there. Nor were
twenty-one others. **23 of `text.inc`'s 48 constants had never reached the DS**
— every gauge glyph, the entire nine-patch the dialogue window is drawn from,
`CH_CLEAR`, and all seven box and menu geometry numbers.

It was invisible from **both** directions at once, which is why six milestones
went by:

- `check_constants.py` read `game.inc` **and only `game.inc`**. `text.inc` — the
  other half of the specification's numbers — was checked by nothing.
- Its DS-side reader looked in **three headers by name** (`fixed.h`,
  `constants.h`, `actor.h`). Anything in any other header was invisible, so the
  tool reported `SC_END` as missing while it sat in `text.h`. That is a false
  negative that makes a check *worse* than useless: the honest way to clear it
  is to add an excuse for something that is not missing, and then the excuse
  goes stale in silence.

It is obvious in hindsight why the gap existed. §M5 ported the dialogue
*interpreter* and did it carefully — `SC_END`, `TS_REVEAL`, `TM_RAFT`, `TEXT_W`,
`TEXT_H` are all present and correct — and left *rendering* to §M7. So the file
looked ported. The half that was missing was the half nothing needed yet, which
is exactly the half a checker is for.

Both halves are fixed: the tool reads both includes and globs every DS header,
`TS_`/`TM_` join the enum families, the 22 portable constants are ported, and
`TXT_ATTR` is excused with the reason (a SNES BG3 attribute is a priority bit
plus a 3-bit palette; a DS entry has no priority bit — priority is per *layer* —
and four palette bits, so there is no number to carry). **347 constants checked,
up from 300.**

### The HUD

Seven breakages, all fired: `== 0` for `<= 0` (a death would *refill* the
gauge, because an actor sits at negative HP for the frame between the killing
hit and the death being processed), blanking with the opaque cell instead of
`CH_CLEAR`, dropping the blank pass entirely, drawing a gauge for an empty
player slot, one bar position for both bosses, the SNES's palette number
carried across, and a cell worth one point instead of two — that last one at
compile time, because `HP_BAR_CELLS * 2 == SORA_MAX_HP` is a `static_assert`
and `hud.s:22` asks for exactly that check in prose.

Two things the run turned up in the harness rather than the code. The suite hit
`MAX_CASES = 256`: `add()` diagnosed it correctly and counted each dropped
registration as a failure, but the bottom line then read "10 failures", which
looks like ten broken assertions rather than five cases that never ran. Raised
to 512, with the arithmetic written down. And `check_worldsizes.py` caught a
`constants.h:477` citation that my own insertion had shifted — the check earning
its place the first time something moved under it.

## What §M7 still has to do, when a toolchain exists

~~two-screen init~~ → ~~2D tilemap ground renderer~~ → ~~sprites and the
Y-sort~~ **three done — see the sections above** → the bottom screen (HUD,
command menu, minimap) → touch input → the 3D quad backend as a second
`GroundRenderer`.

"Sprites and the Y-sort" is complete: the depth logic and the attribute
packing. What is *not* written is the frame glue that calls them and DMAs the
result into OAM — a dozen lines that need a `swiWaitForVBlank` and therefore
libnds, which is the first thing in this milestone genuinely blocked rather than
merely unverifiable.

The bottom screen's other two panels — the command menu and the minimap — are
**new content**, not deferred work, and they need a screen to judge. What
remains of the HUD itself is the boss's name and Kairi's checklist, both of
which are script strings: `PutLabel` walks them out of the same table the
dialogue does, and putting a second string layout in `hud.cpp` would be the
beginning of a second text renderer.

**The rest is where the stub stops paying**, and that is the honest
reason they are not written rather than a shortage of effort. Each needs
decisions a screen would inform — how the streamer schedules its column and row
rewrites, what the Y-sort does with a tie, what the bottom screen's furniture
looks like, where a touch counts as a press — and a recording MMIO log can check
that a renderer wrote *something coherent* but never that it wrote the right
picture. Guessing at them here would be inventing work rather than doing it.

The exception is arithmetic, which is testable now and always was:
`bgEntryIndex()` in `gen/assets.h` already does the block layout a 64×64
streaming window needs, and `vram_map.h`'s `address()` composes the destination.
A streamer's *decision* about which entries to rewrite is host-testable in full;
only the DMA that carries them out is not.

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
