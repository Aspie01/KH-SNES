# Nintendo DS target

The SNES build is finished and frozen at commit `b8f2b68`, tagged `snes-final`.
This is the port, and the reason for it is fidelity: the DS can render the source material's camera,
which the SNES could not, and that is why the SNES version had to abandon an
isometric view for a locked three-quarter one.

**Most of it is written.** §M0–§M6 have all landed and been re-audited — the
fixed-point type, the actor simulation, movement and the camera, the VRAM map,
the four scene machines and the dialogue interpreter, and the oracle diff that
holds eight scenarios against the frozen ROM. §M7 is blocked on a toolchain and
its first step is written anyway; see the note below. `docs/DS_PORT_PROMPT.md`
carries the state of each milestone and what its audit found.

(This paragraph said "nothing here is written yet" for far longer than it was
true, which is the failure this project keeps catching in its own documents. It
is why `tools/check_worldsizes.py` exists and why every count quoted in
`docs/WORLD_SIZES.md` is now recomputed from the tree on every Gate 0 run.)

What follows is the set of decisions already made, so that they are not
relitigated, plus the constraints that will actually bite.

**`../../docs/DS_PORT_PROMPT.md` is the agent brief** — the standing constraints
plus one prompt per milestone, with mechanically checkable exit criteria.

**The toolchain is not installed in the development container.** Run
`python3 ../../tools/check_device.py` — the block is a program, not this
paragraph, and it exits 0 the day a toolchain appears.

It reports what is missing and why each piece is needed. As of the §M7 pass:
no devkitPro on disk and none in apt; `apt.devkitpro.org` returns **403**, which
is an egress *policy* denial and is to be reported rather than routed around;
and a Docker **client** exists in the image but there is no daemon at
`/var/run/docker.sock`, so the `devkitpro/devkitarm` image cannot be run. apt's
`gcc-arm-none-eabi` is a Cortex-M/R toolchain: it will accept `-march=armv5te`
but ships no matching multilib, so it compiles and does not link — and a
compiler alone is not enough anyway, because a `.nds` needs libnds's crt0,
linker scripts, specs and headers plus `ndstool`. An emulator is *not* required
to build; `desmume` is one `apt-get` away in Ubuntu universe if one is wanted.

That last pair of sentences is why this is a program now. Two of the six claims
this paragraph used to make had gone stale — the Docker one and the emulator one
— without changing the answer, which is the most dangerous way for a stated
block to be wrong: right for the wrong reasons, until the day it is not.

**The device tier is no longer empty.** `device/init.cpp` is the two-screen
initialisation, and it is checked on every host run against a *recording* MMIO
stub — there is no libnds stub, because a stub is a claim about somebody else's
header and code that compiles against mine and not theirs is a green light with
nothing behind it. Registers are not an API; the seam is one class with two
link-time definitions. See §M7 in the brief for what that buys and what it
emphatically does not.

The port is therefore split into a **host tier** — the whole simulation, built
with the system `g++` and tested against the oracle — and a **device tier** that
only builds where devkitPro is present. The host tier is most of the port and
all of the risk; see the brief.

**Read `../../docs/BEHAVIOUR.md` first.** It is the specification — every frame
count, range and state machine from the SNES build, extracted before any of that
code is deleted. If this build disagrees with a number in it, this build is
wrong, and if a disagreement is deliberate it belongs in that document.

---

## The hardware, accurately

| | SNES | DS |
| --- | --- | --- |
| **Floating point** | none | **none** — every coordinate is fixed point on both |
| Integer divide | none | **none** on ARMv5TE; `/` is a libgcc call |
| CPU | 3.58 MHz 65816 | 67 MHz ARM9 + 33 MHz ARM7 |
| RAM | 128 KiB | 4 MB |
| Screens | 256×224, one | 256×**192**, two |
| 2D colour | 16/palette, 4bpp | still palette-based for tiles and sprites; extended palettes give 16 slots × 256 colours. Direct colour is bitmap modes and 3D textures only |
| Sprites | 128 OAM | 128 OAM **per engine**, so 256 across both screens |
| 3D | none | hardware, but **2048 polygons and 6144 vertices per frame**, and it renders to **one screen only** |
| VRAM | 64 KiB, flat | 656 KiB in nine banks with fixed roles; 3D textures live in A–D |

The two entries that shape everything: no FPU, and 3D to one screen only.

The absence of an FPU is not a hardship here — it is continuity. The 65816 had
no divide either, so this engine was already integer and Q12.4 throughout. The
movement, collision and hit-box arithmetic ports as arithmetic rather than being
redesigned, which is the single biggest reason this port is tractable.

---

## Language: C++, restricted

C++ is here for one reason and it is not general expressiveness. With no FPU
every quantity is a scaled integer, and mixing scales is silently wrong. A type
with no implicit conversions turns that into a compile error. `include/fixed.h`
is that type and it is the justification.

**The contract.** These are not preferences.

```
-fno-exceptions -fno-rtti
```

- **No allocation after init.** No `new`, no `malloc`, in any frame path. Fixed
  pools sized at compile time, exactly as `MAX_ACTORS` was.
- **No STL containers anywhere in the frame loop.** No `std::vector`, no
  `std::string`, no `std::map`, no `std::function`. The actor table is a
  structure of arrays because that is what indexes fastest, and that does not
  change for having a nicer language available.
- **C++ is for four things only:** the fixed-point type; RAII around VRAM bank
  handles, because the DS's nine banks have fixed roles and leaking one is an
  afternoon lost; `constexpr` table generation, so the spawn tables and terrain
  vocabulary are compiled rather than parsed; and `enum class` for the state
  machines, so `townStage` cannot be compared against a `diveStage`.
- **Everything else is C with a C++ compiler.** Plain functions, plain structs,
  no inheritance, no virtual dispatch — with the single exception below.

The one sanctioned virtual: the **ground renderer interface**. It exists to be
swapped, it is called a handful of times a frame rather than per actor, and the
indirection is the point.

---

## Staging, and why the 3D question is not a fork

The choice is not "keep the map files or get the camera". The DS way is **3D
rendering of 2D-authored data**: each tile becomes a textured quad at a height
taken from its terrain code. `assets/*.txt` stays valid verbatim and the camera
gains orbit, tilt, zoom and fog, with characters as billboards freed from the
sprite ceiling.

So it sequences rather than branches:

1. **Flat 2D tilemaps first.** Every map file works on day one, which proves the
   content pipeline survived the platform change. This is the milestone that
   matters, because it is the one that de-risks everything else.
2. **Ground renderer behind an interface immediately** — before it is needed,
   while there is only one implementation and the seam is free.
3. **3D quad backend after.** The stage machines do not care what draws the
   floor. Same shape as `tools/build_assets.py` gaining a second backend.

Two weeks to a playable 2D DS build beats two months to a half-finished 3D one,
and the option stays open.

### The polygon budget, worked out

2048 polygons and 6144 vertices per frame. A quad is one polygon, so a whole
64×64 ground submitted naively is 4096 quads — **2× over**, not 4×.

But nothing requires submitting the whole map. A 256×192 screen at 16 px tiles
sees 16×12 = 192 tiles; allow generously for tilt and zoom widening the view and
call it 400. That is 400 quads and 1600 vertices — comfortably inside both
limits.

**So frustum culling is the load-bearing piece, and coplanar tile merging is an
optimisation for when tilt widens the view far enough to matter.** Merging is
not a precondition for the 3D path, which reorders the work: build the visible-set
walk first, measure, and only merge if the measurement asks for it.

---

## Screen assignment — decided, not to be revisited

The 3D engine renders to one screen at a time, and alternating per frame halves
the framerate. So gameplay owns one screen permanently and the other is 2D.

- **Top:** the world. 3D when that backend lands, 2D tilemaps until then.
- **Bottom:** command menu, HP/MP, party status, minimap in a corner. All 2D, so
  it is not either/or.

Moving Kingdom Hearts' persistent command list off the play area is a
straightforward improvement on the original, and touch input for it is free.

Note what this frees: `docs/BEHAVIOUR.md` §8 records that nothing is walkable in
the top 24 px of any map because the HUD owned it. With the HUD on the other
screen that margin may be reclaimable — but §9 also notes that a 192-line screen
lets the camera reach 32 more rows of every map, so the maps will show more of
themselves than they were authored to. Both need re-checking in
`tools/check_map.py`, which already flood-fills every map against every spawn
point and is the right place for it.

---

## What ports, and what does not

**Ports essentially unchanged** — which is most of the actual work of the last
several months:

- All map data (`assets/*.txt`) and the whole terrain-code vocabulary.
- Every scene and stage machine, with its frame counts. Both platforms are
  60 Hz.
- The dialogue scripts verbatim.
- `tools/check_map.py` — the flood fill is engine-independent.
- All movement, collision and hit-box arithmetic, because it is already integer.
- The content design: item lists, the race course, door gating, boss attack
  patterns and timings.

**Rewritten:** the rendering, the input plumbing, the audio (the SPC700 driver
was never written; the DS has 16 hardware channels and is a far easier target).

**Deleted, and only after `docs/BEHAVIOUR.md` is complete:** the 65816 assembly.

**Does not exist on ARM at all:** `tools/check_modes.py`. Register-width tracking
is a 65816 problem — the assembler sizing an immediate for a CPU mode it does not
arrive in. There is no ARM equivalent, and the tool stays only to keep the SNES
oracle building.

---

## The oracle

Both platforms run at 60 Hz, every timing in the specification is in frames, and
the spawn tables are identical. That makes the frozen SNES build a **behavioural
oracle** rather than a keepsake:

1. Drive both with the same scripted input.
2. Dump a per-frame state trace — player position, actor positions and states,
   stage bytes, HP.
3. Diff.

Any divergence is a port bug or a deliberate change. This is why gameplay stays
`Fixed<4>`: bit-exact parity with the old simulation is what makes the diff
meaningful, and it costs nothing a 16 px tile can perceive.

The SNES side already has the harness for step 1 (`tools/playtest.sh`) and a
route to machine state for step 2 — mednafen save states are gzipped, WRAM is
locatable inside them, and `platform/snes/build/kh.map` gives every symbol's
address. That was built to chase a bug and turns out to be half the test rig.

---

## Layout

```
platform/ds/
  include/fixed.h       the fixed-point scalar; read the header comment
  include/constants.h   every tuning number, ported from game.inc
  include/actor.h       the actor table and the four type tables
  source/actor.cpp
  host/Makefile.host    the host build; globs its sources
  host/check.h  .cpp    the test harness: a registry and an assertion
  host/tests/           one file per task, self-registering
  README.md             this file
```

Build and run the host tier from the repository root:

```sh
make -f platform/ds/host/Makefile.host run
```

Milestones M0 (host build, fixed-point parity) and M1 (the data model) are done:
16 cases, 239 checks. M2 onward are in the brief.

Shared with the SNES target, one level up:

```
assets/              map data and dialogue -- the same files both targets read
tools/               build_assets.py (two backends), check_map.py
docs/BEHAVIOUR.md    the specification
```
