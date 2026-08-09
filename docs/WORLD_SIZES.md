# World sizes

The SNES maps are 32×16 tiles — 512×256 pixels, which is one 64×32 PPU tilemap
exactly. That was never a design choice about how big a world should be; it was
the largest thing that fit in VRAM without streaming, and the whole slice was
bounded by it. Two screens wide is not a world.

## What the DS changes, and what it does not

| | Tiles | Pixels | Characters | Area |
| --- | --- | --- | --- | --- |
| SNES, all five maps | 32×16 | 512×256 | 64×32 | 1× |
| **A single DS 2D background, maximum** | **32×32** | **512×512** | **64×64** | 2× |
| DS Traverse Town, each district | 48×32 | 768×512 | 96×64 | **3×** |
| DS Destiny Islands | 64×32 | 1024×512 | 128×64 | **4×** |

Our tiles are 16 px, so each is 2×2 hardware characters, and a DS background
tops out at 64×64 characters. **The largest map that needs no streaming is
therefore 32×32 of our tiles — only twice the current area.** That is not an
expansion worth the word.

So the DS island is 64×32, the districts are 48×32, and the 2D ground renderer
has to stream a window across all four as the camera crosses tile boundaries.
That is a deliberate cost:

- It pulls forward what was roadmap item 2. The 2D backend is no longer the
  trivial one.
- The 3D quad ground has no equivalent limit — it submits the visible set and
  nothing else — so this cost is temporary and only the 2D path pays it.
- The data is trivial either way: 64×32 is 2 KB of collision, 2 KB of height and
  16 KB of tilemap against 4 MB of RAM.

**The SNES is stuck at 32×16 permanently, and its maps are unchanged.** BG1's
tilemap is 64×32 characters and BG3's map sits immediately after it in VRAM, so
there is nowhere for a wider one to go. That is why the expanded maps live in
`assets/ds/` beside the originals rather than replacing them: the frozen build
keeps producing a byte-identical ROM, which is the only thing that makes it
usable as an oracle.

## What is expanded, and what is deliberately not

**Expanded**, because they are places:

- **Destiny Islands** — 64×32. 670 walkable tiles against 196, so 3.4× the
  ground you can actually stand on. Real coastline instead of a rectangle, a
  water channel with the footbridge crossing it, and the Secret Place sealed on
  three sides so it is entered by wading round the waterfall rather than walked
  into from the lawn.
- **Traverse Town's three districts** — 48×32 each, 3× the area, 891 / 822 / 877
  walkable tiles against 303 / 292 / 273. Not 64 wide: a district is a walled
  courtyard, and past a certain width the walls stop enclosing it and start
  being a border on a field. What the extra ground bought is depth rather than
  distance — raised walkways along the north wall with steps up to them, alleys
  behind those walkways reachable only round the southern end, a fountain in the
  Second District big enough to walk around instead of past, and an open floor
  in the Third wide enough that the Guard Armor can be fought rather than
  cornered.

Those four counts are of the **populated** maps, and they are smaller than the
numbers this file used to carry — 717 for the island and 912 / 840 / 895 for the
districts. Nothing shrank. A prop tile is not walkable, and the props and crates
went into the maps one commit *after* the maps were expanded: the island gained
24 `T`, 11 `r`, 8 `b`, 2 `R` and 2 `Y`, which is 47 tiles and 717 − 47 = 670, and
each district gained 8 lamp posts and 10 to 13 crates, which is the 21 / 18 / 18
that takes 912 / 840 / 895 to 891 / 822 / 877 exactly. The old figures were true
of empty ground, were copied here by hand while they were true, and then stopped
being true underneath the sentence that quoted them. `tools/build_assets.py`
prints all four on every run and has done all along. See **How this section went
stale** at the foot of the file: the numbers here and the list of open questions
down there rotted by the same mechanism, and it is worth noticing that a
document's *arithmetic* goes stale exactly the way its *prose* does — quietly,
and without contradicting itself loudly enough for anyone to notice.

Two structural notes about the districts, both learned by getting them wrong:

- **A +3 building cannot stand south of anything walkable.** A tile at height h
  is painted 8h pixels above its own cell, so a wall along the southern edge is
  drawn over the square it is supposed to enclose. The south side is a height-0
  parapet instead — which is what the original SNES districts did, for the same
  reason. The same arithmetic is why a terrace's building is filled all the way
  back to the north wall rather than being one row deep, and why doors and
  windows are kept out of the columns a terrace covers. In the First District
  that last one had hidden the only exit.
- **A district hangs off its door row, and there are six door tiles in all.**
  The island tolerates a mistake in a corner; a district does not, because the
  whole map is reached through row 4 — `DOOR_ROW`, `constants.h:477` — and
  every other column of that row is wall. Checked, not assumed, against
  `build_assets.TERRAIN`: row 4 of each of the three maps is unbroken +3
  building except at town1 (16,4) and (24,4), town2 (5,4), (24,4) and (28,4),
  and town3 (23,4). It is not literally all `w`, and the first draft of this
  sentence said it was. `e` — the same building face with a lit window — also
  appears in row 4, at four columns in the First District, four in the Second
  and two in the Third, and `TERRAIN['e']` is character-for-character the same
  tuple as `TERRAIN['w']`: height 3, not walkable. A window changes the picture
  and not the reachability, so the *claim* survives and the *wording* did not,
  which is exactly the shorthand-that-drifts this bullet exists to correct: a
  reader who checked "solid `w`" against the map would have found it false and
  had no way to tell whether the door list beside it was false too. Four of
  those six door tiles are the SNES's joins and two are shop fronts that lead
  nowhere; which is which was not decided until `assets/ds/town_doors.txt` was
  written, long after the maps.

  **This bullet used to say "every district has exactly one way in", and that
  was never true of these maps.** It was written in the same commit that drew
  two door tiles into the First District, and nothing has ever compared the
  sentence to the map. It is not true of the frozen game either: `doorTable` is
  four rows and two of them arrive in the Second District, so even the original
  town had a district with two ways in. What the sentence was reaching for, and
  what *is* true, is that a district's only connection to the rest of the game
  is a handful of single tiles, so one of them mis-drawn strands a whole map
  with nothing to fall back on. `check_map.py` checks each door from both sides
  — the tile itself and the tile the scene code stands Sora on after the swap —
  along with the walkways, the steps that are the only way up onto them, and
  the alleys behind them. `tools/build_doors.py` now adds the check that would
  have caught the sentence: it refuses to emit anything at all if a `d` tile
  has no wiring row, or a wiring row names a tile that is not a `d`, so the
  door count in the maps and the door count in the wiring cannot disagree, and
  a district that cannot be left is a build failure rather than a claim.

**Not expanded**, because they are not places:

- **The three Stations of Awakening.** A station is a stained-glass disc in a
  void, deliberately smaller than the screen, with the camera pinned so the void
  never scrolls into view. Making one bigger would mean showing more nothing.

  They did, however, have to be drawn **smaller** — which is a different thing,
  and it is the first place divergence 001 broke something instead of merely
  widening a clamp. "Smaller than the screen" was true by two pixels at each end
  on the SNES: a 220 px disc inside 224 lines. On the DS's 192 it is clipped by
  14 px top and bottom, and what gets clipped is the outer golden lip and the
  spoked ring — the two features that make the thing read as stained glass rather
  than as a coloured circle. So the DS draws a radius-92 disc, 184 px across, and
  the cast moved inward with it because two spawn tiles fell off the standable
  set. `build_dive_platform()` takes the radius as an argument defaulting to the
  SNES's 110, so the frozen ROM is byte-identical.
  See `docs/behaviour/divergences/004-ds-station-radius.md`. The one thing that
  file left as a derivation — the 170-frame fall *between* two stations, which
  is the other half of "the camera never shows the void" — has since been
  measured against the oracle and agrees byte for byte; see the `fall` entry
  under **Consequences that closed** below.
- **The island fragment.** It is the last scrap of ground left after the island
  comes apart, sized so Darkside can stand on it and Sora cannot retreat. Small
  is the entire point. It needed no redraw — it is authored, not generated, and
  its walkable ground already sits well inside 192 lines.

## What had to change to allow any of this

Map dimensions were a module-level global in `tools/build_assets.py`, used in
fifteen places, and `load_grid` asserted them. A map now carries its own size:

- `Grid` holds `rows`, `w`, `h` and does the off-map test the rim needs.
- `load_grid` reads the width from row 0 and requires every row to match, rather
  than demanding 32×16.
- `build_world` takes its extents from the grid.
- `dedupe_tilemap` grew a `layout` parameter. `"snes"` writes the two 32×32
  screens side by side that the PPU requires — and now refuses anything that is
  not exactly 64×32 characters, instead of silently shredding a larger map into
  diagonal bands. `"rowmajor"` is what the DS wants and what any larger map must
  use.
- `check_map.py` takes its bounds from the grid, so one checker serves both
  targets.

Two terrain characters also had to be recoloured. `s` (a step) and `q` (a raised
walkway) were drawn in the cobbles' own colours, so a walkway differed from the
square around it only by the 16 px face at its southern edge — invisible at the
district's new size. They are warm paving now. No frozen map uses either
character, so the SNES ROM is unaffected.

The character dedupe is unchanged and still earns its keep at the larger size:
the 64×32 island folds to **247 unique characters** from 8192 cells, and the
districts to 85, 107 and 85 from 6144 each. (This file said 242 for the island
until now. That was correct when it was written and stopped being correct in the
same commit that broke the walkable counts above — twenty-four more palms, eleven
more rocks and eight more bushes are new shapes against the ground they sit on.
The districts' three happen to have survived unchanged, which is luck and not a
reason to trust them: they are quoted from `build_assets.py`'s report, which
prints all four every time it runs.)

## Filling them: the cast

An expanded map is empty ground until something stands on it. The SNES kept two
hand-written halves — a table of `(type, i, j)` triples in assembly and the map —
with nothing checking that they agreed. They did agree: every `T` tile carried
exactly one `ACT_PALM`, every `R` an `ACT_ROCKBIG`, `Y` a `PALMC`, `r` a `ROCK`,
`l` a `LAMP`, one each, no exceptions. But only because somebody kept them in
step by hand, and a `T` with no palm on it is an **invisible wall**.

So on the DS that correspondence is a rule instead of a coincidence:

- **Prop actors are derived from the map.** The tile says what stands on it and
  the cast file never mentions a palm. Adding a tree is adding one character;
  `build_assets.py` emits the actor and `check_map.py` finds it there. The file
  refuses a hand-written `Palm` row rather than accepting a second source.
- **The cast file holds only what cannot be derived** — people, items, the
  Heartless spots, the doors — in `assets/ds/<scene>_cast.txt`, in sections that
  mirror the tables `island.s` and `town.s` deliberately kept apart, because
  merged they would spawn the second day's mushrooms on the first.
- **`check_map.py` reads those files** rather than mirroring them. It derives
  reach-versus-adjacent from the tile under each entry, so that distinction
  cannot be *declared* wrongly; and it rejects two entries on one tile, an
  authored actor standing on a prop tile, an unreachable door or landing, and a
  cast that does not fit the budgets. It found two real bugs the moment it ran:
  the Secret Place's mushroom sitting on top of the chalk faces, and a route
  claim pointing at open water instead of the end of the dock.

What that bought:

| | Props | Placed | Spots | Peak actors |
| --- | --- | --- | --- | --- |
| SNES island | 8 | 9 | 10 | 25 |
| **DS island** | **69** | 9 | 10 | **90** |
| SNES night | 8 | 6 | 10 | 20 |
| **DS night** | **69** | 6 | **35** | **75** |
| SNES fragment | 1 | 1 | – | 8 |
| DS fragment | 1 | 1 | – | 3 |
| SNES district (1/2/3) | 2 / 2 / 2 | 3 / 0 / 2 | – / 8 / – | 6 / 5 / 7 |
| **DS district (1/2/3)** | **13 / 13 / 12** | 8 / 1 / 1 | – / **12** / – | 21 / 14 / 15 |
| station 1 / 2 / 3 | – | 7 / 4 / 2 | – | 7 / 4 / 2 |

Every scene now has a cast. The stations are the only ones with **no props at
all** — a disc of stained glass has no prop tiles — so they are also the only
ones where every actor is placed by hand.

The island's palms and rocks are placed by grove rather than on a lattice —
evenly spaced trees read as an orchard — and each placement is flood-filled and
reverted unless it costs exactly the tile it stands on. Four were refused that
way; two of them had boxed in a pocket of the small island that the first pass
shipped. Crates and the lamps' pools of warm paving are terrain rather than
actors, so they cost no sprite and no pool slot, and they are the cheapest thing
that stops a courtyard reading as a car park. The Third District's arena is
deliberately left bare: a 64×64 boss slams down on it and two walkways look into
it.

Placement is reviewed by looking at it. `assets/src/ds_<scene>_cast.png` is the
ground with a marker stamped on every cast entry, colour-keyed by kind.

### A scene is not a map

The night is the same island. Its tiles, tilemap, collision and height are
*byte-identical* to the two days' — what makes it night is one palette upload,
which is how the SNES did it too. So a DS scene is a record rather than a
filename: it names a map, a palette, a cast, and optionally another scene whose
ground it shares and therefore does not re-emit. Four consequences fall out of
that, and each of them was a bug waiting to happen:

- **The night does not duplicate 28 KB of ground.** Sharing is declared, not
  inferred from the maps happening to match.
- **The night's palms are bare.** The map says `Y` — a palm carrying coconuts —
  but by nightfall day two has picked them, and `night.s` accordingly spawned a
  plain `ACT_PALM` at both trees the days give a `PALMC`. The scene carries a
  prop override for that: `Y → Palm`. Seven coconut palms become seven plain ones
  and nothing else changes. Derived props would otherwise have quietly restored
  fruit the player harvested.
- **The fragment reads `assets/fragment.txt`**, from the SNES directory, because
  there is no expanded version and there should not be. A scene naming its map
  makes that expressible; a scene *being* its map would have needed a special
  case.
- **A station has no map at all.** There is no `station1.txt`: the glass is
  generated from a circle, so the scene carries a *builder* rather than a
  filename, and `grid_from_coll()` turns the resulting collision array into the
  same `Grid` interface every authored map presents — `G` for standable glass,
  `*` for the void, neither ever drawn. That is what lets the cast checker and
  the reachability walk treat a station like anywhere else without knowing it is
  procedural.

Two of the night's actors are deliberately not in its cast file, because they are
**transformed rather than placed** and doing both would double them: the columns
of darkness (`night.s` reads Riku's position, deletes him, and spawns a `Dark` on
the spot — then the same to Kairi), and the open door, which retypes the `Door`
already standing on the wall.

### The night's Shadows are a scene decision, not a scaling factor

`SHADOW_MAX = 6` was tuned against a map where one screen showed nearly the whole
island. On the expanded one a screen shows about 15% of it, so six spread over
the whole map means typically **one on screen** — in the scene whose entire
mechanic is being hunted across ground you cannot fight back on until the
Keyblade arrives.

The DS figure preserves **tiles per Shadow** rather than the count: 196 walkable
over 6 is one per 33, and 670 at that density is 20. `SHADOW_GAP` follows the
same argument and not the count — the fill should still take about as long as
crossing the map, so roughly 2× the SNES's 420 frames, which at 20 alive is 42.
The spot table went from 10 to 35 because spots are places, and ten of them over
four times the ground would cluster every arrival into one quarter of the island.

The fragment keeps six, because its map is unchanged — so the one SNES constant
splits into `SHADOW_MAX_NIGHT` and `SHADOW_MAX_FRAG`. One number for both would
either desert the island or bury the fragment.

**None of this has been played.** It is the derivation's answer with its
reasoning attached, not a measured result, and the difficulty half of the
original six is not something the density argument speaks to at all. See
`docs/behaviour/divergences/003-ds-night-density.md`, and amend that file rather
than quietly changing the constants.

### The pool had to grow, and that is a divergence

`MAX_ACTORS = 32` on the SNES was never an OAM limit — an ordinary actor is one
32×32 sprite, so a full pool used about 35 of 128 entries. It was WRAM and a
3.58 MHz CPU. The DS pool holds **128**, which is not a tuning choice: the island
asks for 69 prop actors before anybody is placed on it, so at 32 it cannot be
loaded at all.

The pool holds the whole map; **OAM holds what is on screen.** Those are
different numbers, and the second is the one a dense map has to answer for, so
`check_map.py` slides a 17×13-tile camera window over every map and enforces
`OBJ_BUDGET_SCENERY = 96` of the engine's 128, leaving 32 for the transients, a
four-quadrant boss and Sora. Worst cases today: island 33, districts 5–8.

See `docs/behaviour/divergences/002-ds-actor-pool.md` for what changing the pool
size does and does not do to the oracle.

## Consequences still open

This list had seven items and four of them had quietly closed. What follows is
what is open **today**, checked one claim at a time against the tree rather than
against the last person's memory of it; what closed is recorded in the section
after, because knowing what was once uncertain is worth more than a short list.

Three remain, and the first of them owns most of the other two.

- **The 2D ground renderer needs streaming** (§M7 of the porting brief). Until
  it exists, the DS island can be built and validated but not drawn. The block
  is no longer a paragraph of prose that nobody rereads: `python3
  tools/check_device.py` **is** the gate, it exits 1 today, and it names four of
  four required components absent — devkitPro, devkitARM, libnds and ndstool —
  with `apt.devkitpro.org` returning HTTP 403 through the egress proxy, which is
  an organisation *policy* denial and not a network fault. What is missing is a
  backend and a toolchain, not a place to put them: the seam exists and is
  occupied. `GroundRenderer` and `NullGroundRenderer` are in
  `platform/ds/include/grid.h:152-178`, the host tier runs against the null one,
  and it *records* that it was asked to load and draw — so a scene that forgets
  its ground fails a test instead of silently drawing the previous scene's.

- **Nothing loads a scene**, and this needs saying precisely, because three
  things standing next to it have moved a great deal. All four stage machines
  are landed — `source/stage_dive.cpp`, `stage_town.cpp`, `stage_island.cpp`,
  `stage_night.cpp` — and so is the caller they spent a milestone without:
  `include/interact.h` and `source/interact.cpp`, added when §M5 was re-audited
  and found complete on one side only. What does not exist is `LoadScene`. There
  is no function of that name anywhere in `platform/ds/`; every occurrence of
  the word is a comment about what the *SNES's* did (`source/stage_night.cpp:21`
  and `:248`, `include/world.h:46`). There is no scene record naming a map, a
  palette and a cast, and nothing walks one.

  Two symptoms, both of which can be pointed at rather than asserted:

  - **Of the nine cast files, exactly one is read at runtime**, and only by the
    trace harness: `station1cast.bin`, at `host/trace_main.cpp:747-752`, which
    is the single `spawnCast` call in the whole non-test tree. Seven of the
    eight trace scenarios build hand-written `CastRow[]` tables instead, and
    deliberately: they have to reproduce the *oracle's* cast, not the DS's.
  - **`SceneAction::EnterDistrict` is returned by nothing that performs it.**
    `TownMachine` emits it at `source/stage_town.cpp:71` and it is consumed only
    in tests. `perform()` at `host/trace_main.cpp:398-491` handles eight of the
    twenty-eight `SceneAction` values — `None`, `Say`, `HudChanged`,
    `RespawnDistrict`, `RaiseArmor`, `BeginFall`, `SpawnMote` and
    `SweepGauntlets` — and its `default:` arm stops the run with "the scenario
    cannot perform SceneAction::…". Every action that means *load a scene*
    lands in that arm. (This said "six" until the `fall` scenario added the
    `BeginFall` and `SpawnMote` arms in the same working tree this section was
    written against, which is the same drift the section is about, arriving
    inside it before it was even committed. Naming the arms rather than
    counting them is the cheap defence: a list is falsified by reading, a
    number is falsified only by someone who bothers to count.)

  So the town's door table is complete, generated, checked and tested, and at
  runtime it still leads nowhere — the swap is the caller's half of the
  protocol, and the caller is the piece that does not exist. That is the same
  shape as the hole §M6 found in §M3 and §M5, one layer up again, and it is the
  reason this item is phrased as "nothing loads a scene" rather than as anything
  about the data: the data has been finished twice now.

- **`variant` is parsed and still not consulted**, which is a narrower claim
  than the one this file used to make and is the more useful one. The fourth
  byte of a cast row selects a line of dialogue, which is what would make six
  townspeople six people rather than one sentence six times: on the SNES
  `TalkTown` dispatched on actor *type*, so it could not have told two townsmen
  apart. `spawnCast` now reads the byte (`source/scene.cpp:39`) and copies it
  into an optional `variantOut` array (`:57`, declared
  `include/scene.h:95-96`) — so "authored and unread" is no longer true at the
  spawn layer. But **the pointer is defaulted to null and only a test ever
  passes one** (`host/tests/test_scene.cpp:92-93`); the one production call
  (`host/trace_main.cpp:752`) omits it, so no variant survives the spawn. And
  the layer the field exists for still dispatches on type exactly as the SNES
  did: `townInteract` indexes a three-entry `LINES[]` by
  `int(a.type[who]) - int(ActType::Cid)` at `source/interact.cpp:395-398`.
  The bounds check §M5 asks for is likewise absent, and correctly so for now —
  nothing indexes anything with a variant, so there is nothing to bound yet.
  Read the item this way: the transport is built, the consumer is not, and the
  field is still costing nothing but the four bytes it was authored into.

## Consequences that closed, and what closed them

Removed from the list above, kept here, because a project that deletes its
uncertainties loses the record of what it did not know. Each of these now has a
standing check named beside it — which is the difference between a thing that is
closed and a thing that merely looks closed today.

- **The camera's clamp is a runtime property now, and no longer a constant.**
  `CameraBounds` is a struct of four ints (`include/grid.h:98-103`); a map larger
  than the screen gets `scrollingBounds(mapW, mapH)` (`:117-120`) and one smaller
  gets `pinnedBounds(x, y)` (`:123-125`), which is why they are *bounds* and not
  a flag — a station pins itself by setting low equal to high. `updateCamera`
  takes them as an argument (`:141-142`, `source/grid.cpp:106`) rather than
  reading a global. `constants.h` still carries the oracle fixture's extents and
  the island's separately, exactly as this file said it did, and that is now the
  input to a function instead of the clamp itself. **Settled by §M3**, and it
  survived the re-audit that found the camera had never been compared against
  anything. **Standing check:** `host/tests/test_grid.cpp:269-349`, and — more to
  the point — the oracle compares the camera columns on every scenario, so each
  of the eight sets its bounds explicitly and a wrong one is a trace diff.
  *Caveat, and it is the honest half:* the bounds are set by whatever stands the
  scene up, and what stands a scene up today is a `setup*()` function in the
  trace harness. The scene *record* that ought to carry them is the thing the
  open item above says does not exist.

- **The districts are wired, and the wiring is generated data rather than
  scene logic.** This file used to say the `[doors]` section deliberately does
  not name a destination and that `town.s`'s seven-byte row "moves with the
  scene port in M5". The first half is still true and is the whole design; the
  second half happened, and not the way the sentence implies. The three missing
  columns are authored in `assets/ds/town_doors.txt` — six rows of
  `from i j to needs`, the four SNES joins each cited to `town.s:1391-1394` and
  the two shop fronts declared `shut / -` — and `tools/build_doors.py` resolves
  the constants out of `game.inc`, parses `doorTable` live out of `town.s`, and
  emits `platform/ds/include/gen/doors.h` in `<scene>doors.bin` order so the
  header and the binary are one table read twice. `townInteract` takes the array
  (`source/interact.cpp:307-308`) and `TownMachine::openDoor` now carries the
  landing across the swap (`include/stage.h:379-383`), which it did not before:
  `TownDoor::landing` was written by the table and read by nothing.
  **Standing checks, and there are three kinds.** `python3 tools/build_doors.py
  --check` fails if the committed header has drifted, on the `build_scripts.py`
  pattern. The generator itself refuses nine numbered classes of wrong wiring
  before it will emit anything, R1 to R9 — a door tile with no wiring row or a
  wiring row on a tile that is not a door (R1); a `[doors]` section and the
  wiring naming different sets of doors (R2); a door outside `DOOR_ROW`, or a
  near-side landing that is not `(i, DOOR_ROW + 1)` (R3); a destination that is
  neither a district nor `shut` (R4); a routed door with no reciprocal, or with
  two (R5); a landing that is blocked, or more than `MAX_STEP` in height from
  its door (R6); a gate on a shuttered door, or a routed door without one (R7);
  a district that is stranded, soft-locked or unreachable at every stage (R8);
  and a gate that disagrees with `town.s`'s own `doorTable` (R9). And
  `host/tests/test_doors.cpp` walks the shipped tables, including a per-stage
  reachability fixpoint over the real data.
  *Two things are worth carrying forward.* The first is that the landing has two
  meanings and they are different tiles — `<scene>doors.bin`'s is the near side,
  `TownDoor`'s is the far side — and nothing may copy one into the other. The
  second is that the DS's two extra door tiles were resolved as **shop fronts
  that do nothing at any stage**, on the grounds that no line in the frozen ROM
  can be said at one without lying and an unresponsive painted door is a smaller
  failure than a scripted lie. That is a content decision, it is recorded in
  `assets/ds/town_doors.txt` and `docs/TRAVERSE_TOWN.md`, and it is the reason
  the "one way in" bullet further up this file had to be corrected.

- **The fall between the stations is measured.** This item said `FALL_LEN` is
  170 frames of `MOTE_LIFE` specks rising past Sora from up to 160 px below him,
  that on a 32-line-shorter screen they start further outside the visible area
  and end in the same place, and that all of this was "derived, not seen".
  It has been seen. `fall` is the eighth scenario in `tools/trace_check.py`
  (`:109-115`), 198 frames, `strict=True` and `identical=True`, and it is
  **byte-identical to the SNES from frame 2**. The spread table is transcribed
  from `dive.s:919-924` into `source/stage_dive.cpp:67-76` with the Q12.4
  arithmetic written out: X spreads ±115 px, Y is 120 to 160 px below him, and
  because Sora's `py` is pinned at 2944 throughout the drop those are absolute
  screen facts and not merely relative ones — the SNES's bottom edge is at 240
  and the DS's at 224, and the specks appear at 304…344 px on both. So the
  derivation was right, and it is now a measurement.
  **Standing checks:** the `fall` scenario itself; `host/tests/test_fall.cpp`,
  four cases over where each speck goes; and four `static_assert`s
  tying the table's length to the `& $07` mask (`source/stage_dive.cpp:84-90`)
  plus a pool-headroom assert (`:105-107`) that fires if the fall could ever
  reach `dive.s:534`'s silent skip, which nothing measures.
  *The interesting part is what it did not find:* no divergence. `updateSora`'s
  `ActState::Fall` branch and `updateMote` were already correct and no scenario
  had ever reached either of them, which is the argument for adding a scenario
  that nobody expects to fail. `docs/behaviour/divergences/004`'s closing
  paragraph — "nothing to change; it is listed here so the next person does not
  have to re-derive it" — is now backed by a trace instead of by reasoning.

- **Sprite VRAM has a bank map, and it is checked in bytes as well as in
  objects.** This item said instances share tiles so the 69 palms and rocks cost
  per *type* and not per actor — still true — "but the bank map that says where
  those tiles live has not been written". It is written. §M4 froze
  `platform/ds/include/vram_map.h`: main-engine sprites are bank E, split into
  `OBJ_RESIDENT{0, 32 KiB}` (`:667`) — exactly the reach of a ten-bit tile number
  at boundary 32 — and `OBJ_BOUNDARY64{32 KiB, 32 KiB}` (`:670`), a reserve that
  is unusable until the boundary is raised. The bottom screen's sprites are bank
  I, `SUB_OBJ_CHR{0, 16 KiB}` (`:687`), and a later pass added `SUB_OBJ_TILES`
  (`:792`) to record the thing the main engine's numbers hide: **on the sub
  engine the bank binds and not the index**, so a tile number above 511 there
  reads unmapped VRAM and draws whatever comes back.
  **Standing checks:** `fits()` assertions place every region inside its bank
  (`:976-989`); the guarded cross-file block ties the header to the pipeline, so
  `OBJ_RESIDENT_BYTES = 31744` (`gen/assets.h:198`) is asserted against
  `OBJ_REACH = 32768` (`gen/assets.h:75`) at `vram_map.h:1182` and a boundary
  change moves both or compiles neither; and `host/tests/test_vram.cpp` and
  `test_oam.cpp` are in the host suite.
  **The other half of this item was right and stays right:** the OBJ *budget*
  check says nothing about VRAM bytes, and should not. `OBJ_BUDGET_SCENERY = 96`
  counts OAM entries inside a 17×13-tile camera window; `OBJ_RESIDENT_BYTES`
  counts bytes of character data. They are two different ceilings a dense map
  can hit independently, and it now takes both checks to clear a map — which is
  the distinction the original sentence was making, arriving at the wrong
  conclusion only because one of the two did not exist yet.

## How this section went stale, and what would stop it

*The diagnosis below is kept as written, because it is the reason the tool at the
end of it exists. Read it in the past tense: **`tools/check_worldsizes.py` now
points here**, it is in Gate 0, and the closing paragraphs say exactly which of
these sentences it made false.*

`docs/WORLD_SIZES.md` was last edited twenty-nine commits ago, at "Add the three
station casts, and redraw the disc so it fits the DS screen". Everything that
closed four of its seven open items landed after that — §M3's camera, §M4's VRAM
map, the door table, the `fall` scenario — and so did the work that *narrowed*
two of the three that remain: §M5's stage machines got the caller they were
missing, and `spawnCast` learned to read `variant`. None of it came back to read
this file. That is not carelessness. **Nothing pointed here.** Every one of those
commits had a check that told it when it was done, and not one of those checks
knew this document existed.

The two halves of the file rotted by the same mechanism, which is worth stating
plainly because it makes the fix obvious:

- **The numbers** were correct when copied and were copied by hand.
  `tools/build_assets.py` prints all of them on every run — walkable counts,
  unique characters, props, placed, spots, peak actors, the OBJ page budget —
  and `tools/check_map.py` prints the per-map worst camera window. The maps
  changed one commit later and the printed numbers changed with them; the
  transcriptions did not.
- **The open items** were correct when written and had no owner afterwards. An
  item closes in a *different* file, and the closing agent has no reason to
  grep the docs for a bullet that describes the world before its change.

The mechanical answer to both is the same one this project had already applied
to the scripts and to the doors, and since this section was first written it has
been applied to this file as well:

1. **Generate the numbers, or check them.** `tools/build_scripts.py --check` and
   `tools/build_doors.py --check` both fail when a committed artefact has
   drifted from its source. This paragraph used to say that only the first of
   the two was in Gate 0, and that whoever owned Gate 0 should add the line.
   **The line has been added.** The Gate 0 block at
   `docs/DS_PORT_PROMPT.md:125-142` now lists `build_scripts.py --check`,
   `build_doors.py --check` and a third check that did not exist when this
   paragraph was written, and it carries the reason for the doors line beside
   it so that the argument does not have to be rediscovered from here.

   **What the paragraph was arguing is still the right lesson; only its ending
   has changed.** A generator that refuses nine classes of wrong wiring refuses
   nothing at all on a commit where nobody runs it, and the failure mode is the
   quiet one: the header in `platform/ds/include/gen/` is committed, so a stale
   one compiles, links, passes the host suite and ships a door that leads to the
   wrong district. The exposure is narrower than "the doors are unchecked" and
   worse for being narrow, and it is worth carrying forward now that it is
   closed, because the same shape will recur. The host suite *does* compare the
   committed header against the emitted `<scene>doors.bin` index for index, so a
   drifted tile or a lost door has always failed there. What nothing outside
   `build_doors.py` reads is `assets/ds/town_doors.txt` — the destination, the
   gate and the shop-front decision, the three columns the binary deliberately
   does not carry, and the only place a human writes any of them. Editing that
   file alone moved nothing any other check could see: the authored table and
   the shipped table would have described different towns with every gate
   exiting zero. A check that catches half a table is not a smaller version of
   a check that catches the table; it is a reason to stop looking.

   A `--check` that parses the tables out of this file and compares them
   against the pipeline's own report would have caught 717, 912 / 840 / 895 and
   242 on the commit that broke them, because the pipeline had already printed
   the right answers in the same terminal. **That is the third check, and it now
   exists**: `tools/check_worldsizes.py`, in Gate 0 beside the other two.
   Anything a tool prints and a document repeats is a copy waiting to go stale,
   and the pattern for fixing it now exists in three tools rather than two.
2. **Make an open item name the check that will close it.** Every bullet in
   "still open" above cites something falsifiable — `check_device.py` exits 1,
   `perform()`'s `default:` arm rejects `EnterDistrict`, `spawnCast`'s
   `variantOut` is null at its only production call. That is deliberate. An open
   item stated as a condition can be *tested*; an open item stated as a mood
   cannot, and a prose list of moods with nothing checking it is precisely the
   failure class this project keeps finding — the invisible wall, the two
   hand-kept halves, the door with no far side, the milestone brief that was
   three steps out of date. This file was one more instance of it.

The first of those is implemented; the second is still a habit and not a
mechanism, which is the honest thing to say about it. **The corrected numbers
above were exactly as unguarded as the wrong ones were, and that was measured
rather than assumed.** Turning one cobble into a crate in `assets/ds/town3.txt`
— a one-character edit — takes the district from 877 walkable to 876, and
`build_assets.py`, `check_map.py`, `check_modes.py`, `check_divergences.py`,
`build_doors.py --check` and the whole host suite (208 cases, 113086 checks) all
still exit zero with this file claiming 877. That measurement is what
`tools/check_worldsizes.py` was written from, and re-running the same edit now is
what shows the hole closed: the tool fails five times over on it, naming both of
the figures in **What is expanded** that the count feeds — the district's
walkable tiles, and the props arithmetic that arrives at them — then both halves
of the demonstration sentence you have just read, and finally the figure this
paragraph says the gates did not defend. The demonstration is itself one of the
figures the tool recomputes, so this paragraph can no longer go on describing a
district the maps do not have.

**"Nothing in the tree reads this document" is the sentence that stopped being
true.** It was accurate when it was written: the mentions of `WORLD_SIZES.md` in
`tools/` and `platform/` were six prose cross-references in comments, and all six
are still exactly that. What is new is a mention of a different kind.
`tools/check_worldsizes.py` names this file in order to **open** it, not to point
at it, and it does not only recompute the figures it was taught. Every digit run
in this document has to fall inside a span some check consumed or carry a written
excuse naming the figure, so a number added here with nothing behind it fails at
its own line — and an excused figure cannot be edited quietly either, because
every excuse for one spells the figure out rather than matching a digit class. So
the four numbers in **What is expanded** and the one in **the cast** are still on
their second transcription — but they are the first figures in this file with a
consumer, and the honest remaining gap is the prose around them rather than the
arithmetic inside them.

The immediate, cheap version of (2) is that the next agent to close one of the
three items above should treat editing this section as part of closing it, in
the same commit, the way
`docs/behaviour/divergences/` entries are amended rather than quietly
contradicted.
