# Traverse Town — content notes

Where the game puts you once it has taken everything away. Three districts of
one screen each, joined by doors.

## Shape of it

| District | Map | What is in it |
| --- | --- | --- |
| First | `assets/town1.txt` | A shop row with one light still on. Cid and two residents. Nothing hostile |
| Second | `assets/town2.txt` | The fountain, and the Heartless. A wave of eight arrives, five at a time |
| Third | `assets/town3.txt` | Enclosed on both sides. Donald and Goofy come down out of the sky, and so does the Guard Armor |

Every district is the same 32×16 grid the island uses, so the town costs no new
engine: four binaries each (characters, tilemap, collision, height) and a row in
a table.

## The beats

`townStage` is progress, not location — the player can walk back through any
door they have already opened and the town remembers where it had got to.

| Stage | What is happening |
| --- | --- |
| `T_ARRIVE` | Face down on wet stone. The opening line |
| `T_LOOK` | Somebody in this town is still up; find them. **The door to the Second District will not open yet** |
| `T_SECOND` | Cid has said which way. The door opens |
| `T_THIRD` | The Second District is clear. The way on opens |
| `T_MEET` | Two people come down out of the sky |
| `T_BOSS` | ...and so does the Guard Armor |
| `T_WON` | The three of them are still standing |
| `T_OVER` | The card |

## Doors

A door is a walkable `d` tile in row 4 — the bottom row of a building block,
which is the only row of one that shows a face, so the doorway is painted into
the wall rather than onto the pavement. Stepping onto it starts a transition:
the screen fades over `DOOR_FADE` frames, the scene is swapped underneath it,
Sora is put down on the tile directly south of the door on the far side, and it
fades back in.

`doorTable` at the bottom of `src/town.s` is the whole of it — seven bytes a
row:

```
which map, the door's tile, where it leads, the tile to stand on over there,
and the stage the town has to have reached for it to open
```

| From | Tile | To | Lands on | Needs |
| --- | --- | --- | --- | --- |
| First | (25,4) | Second | (26,5) | `T_SECOND` |
| Second | (26,4) | First | (25,5) | always |
| Second | (5,4) | Third | (16,5) | `T_THIRD` |
| Third | (16,4) | Second | (5,5) | always |

A door that wants a later stage than the town has reached says so and stays
shut. That fires on the *step onto* the tile rather than on standing there, so
it is said once instead of on every frame the player leans on it —
`lastTileI`/`lastTileJ` are the edge detector.

Adding a fourth district is three lines of data, a text map and a spawn table.

### The doors on the DS

The districts were redrawn 32×16 → 48×32, so every tile in the table above
moved. What did **not** move is which district joins which and what each join
wants, and that is the part the DS reconstructs.

The four columns `<scene>doors.bin` carries — `(i, j, land_i, land_j)` — say
where a door is and where Sora stands *beside* it on the near side. They
deliberately do not say what is on the other side. The other three columns of
`town.s`'s seven-byte row are authored in **`assets/ds/town_doors.txt`** and
generated into **`platform/ds/include/gen/doors.h`** by
`tools/build_doors.py`, the same way the scripts are: committed header, and a
`--check` mode that fails if it has drifted.

| # | From | Tile | To | Lands on | Needs | Where it comes from |
| --- | --- | --- | --- | --- | --- | --- |
| 0 | First | (16,4) | — | — | — | **new**: the Accessory Shop |
| 1 | First | (24,4) | Second | (28,5) | `Second` | SNES row 0, `town.s:1391` |
| 2 | Second | (5,4) | Third | (23,5) | `Third` | SNES row 2, `town.s:1393` |
| 3 | Second | (24,4) | — | — | — | **new**: the Hotel |
| 4 | Second | (28,4) | First | (24,5) | always | SNES row 1, `town.s:1392` |
| 5 | Third | (23,4) | Second | (5,5) | always | SNES row 3, `town.s:1394` |

Rows are in `<scene>doors.bin` order, so the header and the binary are one
table read twice and a host test compares them index for index.

**The landings are derived, not written down.** `town.s` fixes the rule —
"every landing is the tile directly south of the door on the far side" — which
makes a landing a *function of the reciprocal door*, so the generator computes
it and refuses if a door does not have exactly one door back. No landing
coordinate is typed by hand anywhere. Note that this far-side landing and
`doors.bin`'s near-side `land_i/land_j` are two different tiles; nothing may
copy one into the other.

**Two of the six lead nowhere, on purpose.** The Accessory Shop and the Hotel
are new content: painted shop fronts with no interior. There is no fourth
`SceneId`, no map, no cast and no `.bin` behind either, and there is no line in
the frozen ROM one could say without lying — `scriptShut1` ends "FIND THEM
FIRST", stale the moment Cid has been found, and `scriptShut2` ends "WHILE THE
SQUARE BEHIND YOU IS MOVING", which is false once the wave is spent and
meaningless in the First District. `tools/build_scripts.py` checks every script
byte for byte against the ROM, so a new one cannot be written. An unresponsive
painted door is a smaller failure than a scripted lie, and it is what "a shop
row with one light still on" already says — Cid's is the one light, which is
why he stands at (17,6) beside the shop front rather than beside the exit as he
did on the SNES.

They are **declared** rather than left out, with `to = SceneId::Count`, so that
"nothing happens" is a decision on the record. That is the whole shape of the
design: `build_doors.py` refuses to emit anything if a `d` tile has no wiring
row or a wiring row has no `d` tile, so an unwired door is a build failure and a
shuttered one is a line of text with a reason attached. It also checks the
derived landings against the emitted collision and height planes, and — the
load-bearing one — that the DS's `(from, to, gate)` triples are exactly
`doorTable`'s, parsed live out of `town.s`. A DS gate cannot be edited without
the tool naming the SNES row it now contradicts.

## The Second District

Eight Heartless arrive, `TOWN_GAP` frames apart, at most `TOWN_SHADOWS` alive
at once, from the eight spots in `townSpots` — clear of the fountain, the crates
and the lamps, and `tools/check_map.py` keeps them that way. When the whole wave
has been out and none of it is left, the way to the Third District unbolts
itself.

They are the same Shadows the night used, drawn from the same second cut of the
sprite: the town's OBJ palette 1 carries the Heartless colours in exactly the
three slots the night put them in, so `heartTile` and every line of Heartless
code work here unchanged.

## The Guard Armor

A suit of armour with nobody in it. 64×64, emitted as four 32×32 sprites the
same way Darkside is, with its two hands as separate ordinary actors so the
depth sort draws them in front of the torso.

| | |
| --- | --- |
| HP | `GA_MAX_HP` (36 — the same length of fight Darkside was, so the gauge is the same eighteen cells) |
| Moves | Sideways only, at `GA_WALK` — a shade under Sora's own pace |
| Attack | A fist goes up over where you are standing, waits `GA_SLAM_WIND` frames, and comes down |
| Answer | Move. Then hit it while the fist is on the ground |

**Why it only tracks sideways.** A sprite this tall is anchored by its feet, so
sixty-four pixels of armour are drawn *above* wherever it is standing. Let it
walk north to meet somebody and its whole body goes over the top of them, and
the fight stops reading as a fight. Holding station at the head of the square
and following the player left and right keeps it a wall — which is what it
should look like anyway. The vertical work belongs to the hands, and they land
wherever you are.

For the same reason `RaiseArmor` stands the player back in the middle of the
square when the armour lands: the way in is exactly two tiles from where it
comes down, and two tiles is inside it.

## Cast

| Character | Where | What they are for |
| --- | --- | --- |
| Cid | First District, by the lit shop front | The reason the door opens |
| A townsman | First District | Warns you about the Second District |
| A townswoman | First District | Notices the Keyblade |
| Donald | Third District | Lands on the square |
| Goofy | Third District | Lands beside him |

The town's cast replaces the islanders on the second sprite page — same VRAM,
same palette slot, different 8 KiB. Nobody from the island is going to be in it.

## Editing the maps

Same idea as `assets/island.txt`: what you read is what you see, and `make`
recompiles all four binaries from it.

```
c  cobbles          p  warm paving, under a lamp    s  step +1
q  raised walkway +2                                d  a doorway to somewhere
w  building +3      e  ...with a lit window         o  roof +3
x  crates           l  a lamp post                  n  low parapet / fountain rim
v  water in the fountain
```

Two rules, both inherited from the island. A `+3` tile covers the row above it,
so a building has to be solid all the way up — and a doorway has to sit in the
bottom row of one, with open ground to the south, or the block below would paint
over the door. Nothing walkable above row 4, because the HUD owns the top 24
pixels.
