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

## It runs

`platform/ds/kh.nds` boots and plays in **melonDS**. The Third District draws,
Sora is on screen, the HUD and the controls work, and L/R walk the eleven scenes.

**Use melonDS, not DeSmuME.** DeSmuME's last release predates libnds 2.0's
Calico startup and never gets past the ROM header — it shows two white screens
and no diagnostic, which is indistinguishable from a broken build and cost
several rounds to identify.

**Two bugs kept the top screen blank, not one**, and both shapes will recur. The
first fix looked like it had failed, because the second fault was underneath it.

`TilemapGround::writeColumn` wrote four thousand map entries into VRAM through a
plain `uint16_t*`, and nothing in the program ever reads that memory back — so
the optimiser was entitled to discard every store, and did. The display
controller is not a reader GCC knows about. Everything else in the port reaches
video memory either by `dmaCopy` or through `device/mmio.h`'s volatile stores,
which is why the bottom screen worked perfectly. **Any new path that writes VRAM
directly must be `volatile`.**

And `OVERLAY_MAP` — main BG1, priority 1 — was **enabled with nothing writing
it**. That does not give a blank layer. A powered-on map entry of `0x0000` is
character 0 in sub-palette 0, and character 0 of the shared font is a solid
block of colour index 3, because every glyph carries an opaque background so text
can sit in the dialogue window. So BG1 was an opaque 256×192 curtain in whichever
colour the current scene put in main BG sub-palette 0, covering the ground and
every sprite — a flat, plausible, *scene-coloured* screen with every diagnostic
reading correct. `MINIMAP_MAP` on the sub engine was the same, hidden behind the
HUD. Both are configured-but-disabled now. **A layer may be enabled only once
something writes it** — not once it is configured; configured and empty is the
failure — and `devinit_enables_exactly_the_layers_something_writes` enforces it
in both directions on every host run.

If nothing draws again, **render the scene on the host first**: composite
`<scene>map.bin` + `<scene>chr.bin` + `<scene>pal.bin` at the camera the panel
reports and look at the PNG. That eliminates the map, the tileset, the palette
and the camera in one step, which is what it did here.

## Building it

Two prerequisites, and they are needed by different lines below:

* **Pillow**, for the asset pipeline — `sudo apt install python3-pil`, or
  `pip install Pillow`, or a venv if pip refuses with
  `externally-managed-environment`. `tools/pixel.py` draws every asset as an
  indexed image and imports `PIL`; without it `build_assets.py` stops on
  `ModuleNotFoundError` before emitting anything. The root `README.md` has said
  this since the SNES build and it is repeated here because this is the page
  that tells you to run the pipeline.
* **devkitPro**, for the compiler. `python3 tools/check_device.py` says whether
  this machine has one and names every missing piece; it exits 0 only when a
  build is actually possible.

```sh
git pull                          # BEFORE the pipeline, not after -- see below
python3 tools/build_assets.py     # the .bin tables the cartridge links in
python3 tools/check_link.py       # every symbol the ARM9 declares is on disk
make -C platform/ds               # -> platform/ds/kh.nds
```

**On Windows that is two shells, not one**, and the block above is the Linux and
macOS form. The `make` line needs devkitPro's MSYS2 and the two Python lines
cannot run there — see *The Python lines are not bound by any of this* below for
the Windows version of the same four commands. `ds_encode.py` says so too, if
you find out by running it.

**The pipeline is not optional and skipping it used to look like a code bug.**
`assets/gen/` is gitignored, so a pull never brings the `.bin` tables; and
`arm9/Makefile` finds them with a wildcard that runs when make *parses* the
file, not when it links. An empty `assets/gen/ds` therefore produces no asset
objects at all, and the build compiles everything perfectly and then dies with
about 150 lines of

```
undefined reference to 'station1coll_bin'
undefined reference to 'hudchr_bin'
...
```

every one of them blaming `main.cpp`, which is the only file that is right. The
tell is that *every* symbol is undefined rather than some. `arm9/Makefile` now
refuses that case before compiling anything and says which directory it looked
in — run the two Python lines above and build again.

**Pull before you regenerate.** `build_assets.py` writes the `.bin` tables into
`assets/gen/`, which is gitignored — but it also writes the review images into
`assets/src/`, and *those are tracked*. They are tracked on purpose:
`docs/WORLD_SIZES.md` calls `assets/src/ds_<scene>_cast.png` the thing that makes
cast placement reviewable ("placement is reviewed by looking at it"), and the root
`README.md` offers them as the hook for replacing a piece of art with your own.

So running the pipeline and *then* pulling leaves locally-regenerated PNGs in the
way of a commit that changed the same maps, and git stops with

```
error: Your local changes to the following files would be overwritten by merge:
        assets/src/ds_island_cast.png
        ...
```

They are generated, so throwing them away costs nothing — the authored thing is
`assets/ds/*.txt`:

```sh
git checkout -- assets/src/
git pull
python3 tools/build_assets.py
```

If they come back modified straight after that sequence, the cause is a different
one and worth knowing about: the PNG encoder. They are byte-reproducible for a
given Pillow, so two machines on the same version agree and two on different
versions may not. `python3 -c "import PIL; print(PIL.__version__)"` on both is the
check.

**On Windows, `make` must run in devkitPro's OWN MSYS2 shell.** Not Git Bash,
not PowerShell. This is the single most expensive thing to get wrong here, and
it does not announce itself.

devkitPro installs its own MSYS2 at `C:\devkitPro\msys2` and puts
`C:\devkitPro\msys2\usr\bin` on the Windows `PATH`. That makes its `make`,
`bash` and `python3` reachable from Git Bash — which is a *different* MSYS2,
with its own `msys-2.0.dll`, root and mount table. Run the build there and you
get devkitPro's make driving Git Bash's shell, and the two disagree about what
the filesystem is. It surfaces as four unrelated-looking failures — a file make
can open that bash says does not exist, exported variables vanishing, the build
pass stopping with "No targets", gcc falling back to `C:\WINDOWS\` for its
temporary files — each with a plausible local workaround, every one of them
wrong.

`platform/ds/Makefile` refuses with a paragraph if it detects this, by asking
the shell whether it can see `$DEVKITARM` when make demonstrably can.

```
Start Menu -> devkitPro -> MSYS2       (or C:\devkitPro\msys2\msys2_shell.cmd)
cd /c/Users/<you>/path/to/KH-SNES      # /home there is NOT Git Bash's /home
make -C platform/ds
```

`which make` should print `/usr/bin/make` in that shell. A `/c/...` path means
you are in the wrong one.

**Only `make` needs that shell.** Git, Python and an editor can be anywhere
pointed at the same directory, and it is often easier that way: devkitPro's
MSYS2 has no credential helper, so `git pull` over HTTPS fails there with
`Password authentication is not supported`, while Git Bash has Windows
Credential Manager already wired up. Pulling in one shell and building in the
other is not a workaround — the two tools genuinely do not care about each
other.

**One working tree.** The pipeline writes `assets/gen/ds/*.bin` and the link
step reads those exact files, so running them against different checkouts
produces a build that cannot find assets that plainly exist. A WSL clone under
`/home/…` and a Windows clone under `C:\Users\…` look identical and are two
trees; `assets/gen/` is gitignored, so the `.bin` files never travel between
them.

**The Python lines are not bound by any of this**, because no make is involved:
they are ordinary processes that read and write files. devkitPro's MSYS2 Python
is a dead end for them — `pacman -S python3` gives a `/usr/bin/python3` with no
pip, and the trimmed devkitPro package set has no `python-pillow` — so run those
two with the ordinary Windows Python, which installs Pillow with one
`pip install Pillow`, pointed at the same folder:

```sh
python tools/build_assets.py     # Windows Python, has Pillow
python tools/check_link.py
make -C platform/ds              # devkitPro's MSYS2
```

Windows Python inherits the real Win32 working directory and both scripts
resolve their own location from `__file__`, so relative paths work unchanged.

**Or do it all in the one shell**, since a Windows `python.exe` runs perfectly
well from MSYS2 — it is the *make* that is picky, not the python. Ask the
pipeline where that python is: run it in MSYS2, let it refuse, and read the
answer off the refusal.

```sh
python3 tools/build_assets.py    # devkitPro's MSYS2; refuses, and names the one that works
/c/Users/<you>/AppData/Local/Programs/Python/Python313/python.exe tools/build_assets.py
```

`tools/ds_encode.py` finds the candidates (the `py` launcher, a non-MSYS2
`python` on `PATH`, then the installer's per-user and all-users prefixes), *runs*
each one to see which can `import PIL`, and prints the first that can as a
command to paste. It names none rather than the wrong one, so if the second line
above is missing from the refusal, there is no Pillow anywhere on the box and
`pip install Pillow` in the Windows shell is the actual first step.

The `.nds` runs on melonDS, DeSmuME or a flashcart. It boots straight into the
first Station of Awakening; **L and R step through the eleven scenes and SELECT
restarts the loaded one**, because there is no save system and a person testing
a build needs to reach the Third District without playing to it. The bottom
screen carries the HUD and, in the panel below it, the name of the loaded scene
and of any beat the build could not perform — see "what is not wired" below.

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

**Only the ARM9 is compiled.** libnds 2.0 runs on Calico and supplies the ARM7
itself: `$DEVKITARM/ds_rules` passes `-7 $CALICO/bin/ds7_maine.elf` to `ndstool`
without being asked, and devkitPro's own combined template ships an *empty*
`arm7/source` directory to make the point. `ds_rules` also builds the cartridge
— adding the icon and banner, rewriting `-specs=ds_arm9.specs` into Calico's,
and appending `-lcalico_ds9` — so `platform/ds/Makefile` delegates to `arm9/`
and calls no tools of its own. A hand-written ARM7 lived here briefly and was
deleted: the functions it called (`irqInit`, `fifoInit`, `readUserSettings`,
`installSystemFIFO`) do not exist in 2.0, because the runtime does that work.

**Almost all of the device tier is in the host tier too.** `device/*.cpp` is
compiled and asserted on by every host run against a recording MMIO stub and
against the real asset files — the frame loop, the scene table, the performer,
the depth sort, the attribute packing, the ground streamer, the HUD, the
dialogue box and the screen effects. Two files are not: `device/mmio_device.cpp`,
which is three volatile stores and is syntax-checked on every host run anyway,
and `arm9/source/main.cpp`, which includes `nds.h` and therefore cannot be. That
second one is the only place in the port where a mistake survives a green run,
which is why it is kept to input, copies and a vblank wait, and why
`tools/check_link.py` exists to check the one thing in it that is checkable
without a compiler.

### What is not wired

`device/boot.cpp` performs most of the `SceneAction`s and **refuses ten of them
by name**: the night's three column beats, Donald and Goofy's descent, and the
four retry paths. Those are beats with a body in the assembly that this port has
not written. They are refused rather than ignored on purpose — an arm that
returned success would let the machine's timer run out over a beat that never
happened, which is invisible — and the ARM9 puts the refused action's name on the
bottom screen, so a person testing the build sees which beat is missing, in the
scene it is missing from, on the frame it was wanted.

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
