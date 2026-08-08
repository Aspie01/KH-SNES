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

- **Destiny Islands** — 64×32. 717 walkable tiles against 196, so 3.7× the
  ground you can actually stand on. Real coastline instead of a rectangle, a
  water channel with the footbridge crossing it, and the Secret Place sealed on
  three sides so it is entered by wading round the waterfall rather than walked
  into from the lawn.
- **Traverse Town's three districts** — 48×32 each, 3× the area, 912 / 840 / 895
  walkable tiles against 303 / 292 / 273. Not 64 wide: a district is a walled
  courtyard, and past a certain width the walls stop enclosing it and start
  being a border on a field. What the extra ground bought is depth rather than
  distance — raised walkways along the north wall with steps up to them, alleys
  behind those walkways reachable only round the southern end, a fountain in the
  Second District big enough to walk around instead of past, and an open floor
  in the Third wide enough that the Guard Armor can be fought rather than
  cornered.

Two structural notes about the districts, both learned by getting them wrong:

- **A +3 building cannot stand south of anything walkable.** A tile at height h
  is painted 8h pixels above its own cell, so a wall along the southern edge is
  drawn over the square it is supposed to enclose. The south side is a height-0
  parapet instead — which is what the original SNES districts did, for the same
  reason. The same arithmetic is why a terrace's building is filled all the way
  back to the north wall rather than being one row deep, and why doors and
  windows are kept out of the columns a terrace covers. In the First District
  that last one had hidden the only exit.
- **Every district has exactly one way in.** The island tolerates a mistake in a
  corner; a district does not, because the whole map hangs off a single door
  tile in an otherwise solid row. `check_map.py` now checks each door from both
  sides — the tile itself and the tile the scene code stands Sora on after the
  swap — along with the walkways, the steps that are the only way up onto them,
  and the alleys behind them.

**Not expanded**, because they are not places:

- **The three Stations of Awakening.** A station is a stained-glass disc in a
  void, deliberately smaller than the screen, with the camera pinned so the void
  never scrolls into view. Making one bigger would mean showing more nothing.
- **The island fragment.** It is the last scrap of ground left after the island
  comes apart, sized so Darkside can stand on it and Sora cannot retreat. Small
  is the entire point.

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
the 64×32 island folds to **242 unique characters** from 8192 cells, and the
districts to 85, 107 and 85 from 6144 each.

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
| SNES district (1/2/3) | 2 / 2 / 2 | 3 / 0 / 2 | – / 8 / – | 6 / 5 / 7 |
| **DS district (1/2/3)** | **13 / 13 / 12** | 8 / 1 / 1 | – / **12** / – | 21 / 14 / 15 |

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

- **The 2D ground renderer needs streaming** (§M7 of the porting brief). Until it
  exists, the DS island can be built and validated but not drawn.
- **The camera's clamp is per-scene now**, not a constant. `constants.h` carries
  the oracle fixture size and the island's separately; M3 makes it a runtime
  property of the loaded scene.
- **The districts are not yet wired to a scene.** The maps, their cast and their
  door tiles exist and are validated, but which district each door leads to is
  scene logic and the `[doors]` section deliberately does not say. `town.s`'s
  door table — seven bytes a row, carrying the destination scene and the stage
  that gates it — is still written against the 32×16 originals. It moves with the
  scene port in M5.
- **`variant` is authored and unread.** The fourth byte of a cast row selects a
  line of dialogue, which is what makes six townspeople six people rather than
  one sentence six times: on the SNES `TalkTown` dispatched on actor *type*, so
  it could not have told them apart. The field is filled in and nothing consumes
  it until M5. It is there now because adding it later means re-authoring every
  file.
- **The night is not populated.** It runs on the island's map with its own cast —
  Riku, Kairi, the columns of darkness, and far more Heartless than the two days
  ever show — and that is a fifth cast file that does not exist yet. The pool was
  sized with it in mind (90 of 128 used by the busiest daytime scene), but the
  numbers are unverified until it is written.
- **Sprite VRAM is still M4's problem.** Instances share tiles, so the 69 palms
  and rocks cost per *type* and not per actor — but the bank map that says where
  those tiles live has not been written, and the OBJ budget check above says
  nothing about it.
