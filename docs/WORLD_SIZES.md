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

## Consequences still open

- **The 2D ground renderer needs streaming** (§M7 of the porting brief). Until it
  exists, the DS island can be built and validated but not drawn.
- **The camera's clamp is per-scene now**, not a constant. `constants.h` carries
  the oracle fixture size and the island's separately; M3 makes it a runtime
  property of the loaded scene.
- **The scene tables need re-placing per map.** Done: all 50 island spawn points
  and all 72 district ones re-derived and checked, and every walkable tile on all
  four maps is reachable from where Sora starts.
- **More ground means more to fill.** 717 walkable tiles with the old cast
  density reads as empty, and the districts are emptier still — three of them
  hold 2647 walkable tiles between them and the shipped cast is Cid, two
  townspeople, eight Heartless, Donald, Goofy and a boss. The palms, rocks,
  bushes, crates and lamp posts are sprites placed from spawn tables, so this is
  a content pass, not an engine one.
- **The districts are not yet wired to a scene.** The maps and their spawn
  tables exist and are validated; `town.s`'s door table, which is what makes the
  three of them one town, is still written against the 32×16 originals. It moves
  with the scene port in M5.
