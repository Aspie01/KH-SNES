# Destiny Islands — content notes

Reference for the island scenes. The Dive to the Heart comes first; this is
what the island needs once the player wakes up on the beach.

## Cast

Five islanders are present and talkable before anything else happens:

| Character | Role in the opening |
| --- | --- |
| Kairi | Gives the raft item lists, checks them off |
| Riku | Races Sora; the winner names the raft |
| Tidus | Sparring partner, up on the far-left platform |
| Selphie | Sparring partner |
| Wakka | Sparring partner |

## Day 1 — raft materials

Kairi's list, with where each piece is found:

| Item | Count | Location |
| --- | --- | --- |
| Logs | 2 | One near the shore past the wooden bridge; one on the small island where Riku sits |
| Cloth | 1 | Inside the treehouse near the centre of the island |
| Rope | 1 | On the high wooden platform near Tidus, on the far left |

## Day 2 — provisions

| Item | Count | Location |
| --- | --- | --- |
| Fish | 3 | Swimming in the shallow water near the shore |
| Mushrooms | 3 | One in the hollow behind a rock near Kairi; one in the bushes at the base of the tower; one inside the Secret Place |
| Coconuts | 2 | Hit the palm trees near Kairi until the gold coconuts fall |
| Seagull Egg | 1 | Atop a tree — climb the thin tree near the bridge and jump across |
| Drinking Water | 1 | Stand under the waterfall holding the empty bottle |

Day 2 also has the race against Riku; the winner gets to name the raft.

## What this implies for the engine

Each of these needs something the combat slice does not have:

- **Talkable NPCs** and a dialogue box — everything else depends on it. *Done*:
  `src/island.s` owns the five islanders and their lines; A talks to whoever is
  in range.
- **Item pickups with counts**, and a quest tracker that reads the tables above.
  *Done for both days*: walking into a thing takes it, and the HUD rows below
  Sora's gauge become Kairi's checklist once she has asked.
- **Multi-level terrain** — the rope is on a high platform and the bridge is
  raised, so the island is not a single flat plane. *Done*: see below.
- **Interactable scenery**: palms that drop coconuts when struck, a climbable
  thin tree, a waterfall to stand under, an enterable Secret Place. *Done* —
  see day two below.
- **A race mode** with a course and an opponent running a fixed path. *Done* —
  see the race below.

Shadow Heartless do **not** belong in the two days. They arrive on the night
the island falls, which is a later state of the same map -- see below.

## Raised ground

`assets/island.txt` gives every terrain code a height in eight-pixel steps, and
`tools/build_assets.py` emits it as `heightmap.bin` beside the collision map.
Three things use it:

- **Painting.** A raised tile is drawn `8 * height` pixels higher, with a cliff
  face filling the gap back down to where the tile would sit at ground level.
  Painting north to south then needs no other depth logic -- but it does mean a
  raised tile covers `8 * height` pixels of whatever is north of it, so the map
  puts water, rock or more of the same structure there. Both wooden decks are
  built over the head of a cove for exactly that reason.
- **Walking.** A move is refused unless the destination is within one step of
  where the actor is standing. A deck at +2 is therefore sealed off except
  across its +1 step tile, which is what makes a ladder out of a plank.
- **Drawing actors.** Each actor caches the height it last stepped onto in
  `actZ`; the sprite and its shadow are lifted by that much. The world stays a
  single flat plane as far as the geometry and depth sort are concerned.

Pickups compare heights exactly, so the rope cannot be lifted off the platform
by standing on the grass beneath it.

## The three interactions

Which one a thing answers to is decided by its actor type alone, so adding a
collectable is a table entry and a spawn:

| | |
| --- | --- |
| walk into it | everything in `ACT_LOG..ACT_BOTTLE` — the materials, mushrooms, a knocked-down coconut, the egg, the bottle |
| swing at it | fish out in the shallows, and palms still carrying coconuts |
| press A | the islanders, so a walk along the beach does not trip over five conversations |

`itemCount` is indexed by `actor type - ACT_LOG`, so a pickup tallies itself
without a lookup.

## Day one, as built

| Where | What |
| --- | --- |
| `(12,12)` beach | Kairi — gives the list, checks it off |
| `(27,8)` small island | Riku, past the raised bridge |
| `(7,4)` lookout platform | Tidus, with the rope beside him |
| `(14,13)` dock | Selphie |
| `(19,12)` past the footbridge | Wakka |
| `(20,12)` | Log — the shore past the little footbridge |
| `(28,9)` | Log — the small island where Riku sits |
| `(12,4)` treehouse deck | Cloth |
| `(8,4)` lookout platform | Rope |

Handing the list in ends the day: the screen dims, the island is rebuilt with
day two's set, and it comes back up on the morning after.

## Day two, as built

| Where | What |
| --- | --- |
| `(4,12)`, `(6,13)`, `(17,13)` shallows | Fish ×3 — swing at them from the shore |
| `(9,12)` | Mushroom, in the hollow behind the rock by Kairi |
| `(6,4)` | Mushroom, in the bushes at the foot of the tower |
| `(2,7)` | Mushroom, inside the Secret Place |
| `(9,10)`, `(13,10)` | Coconut palms — swing until the gold ones come down |
| `(21,6)` treetop | Seagull egg, up the leaning tree by the bridge |
| `(4,7)` pool | Bottle, under the waterfall |

Handing day two's list in is what Riku has been waiting for.

### A note on the actor table

The island's own cast -- Sora, five islanders, the palms and rocks, the three
things on the cave wall -- is seventeen actors and is up the whole time. Day
two's eight items go on top of that, which makes twenty-five, and `MAX_ACTORS`
was twenty-four. `SpawnActor` returns carry clear when the table is full and
`SpawnTable` does not check it, so the *last* entry in the table silently never
appeared: the bottle under the waterfall. Water was the one item on the list
with no way to obtain it, and nothing said so.

`MAX_ACTORS` is now twenty-eight, and `island.s` and `night.s` both assert
their populations plus a transient reserve against it at assembly time, so
over-subscribing the table fails the build instead of quietly dropping whatever
happens to be last.

Raising it turned up a second bug, which is worth writing down because of how
it presented. At thirty-two slots the sprite path corrupted Sora's cel and
scattered actors; at thirty and thirty-two it broke, at twenty-eight, twenty-nine
and thirty-one it did not; and padding `sortIdx` by four entries fixed
thirty-two while breaking twenty-eight. All of which pointed at a stray write
landing on whatever the BSS layout happened to put in its path.

It was none of that. `UpdateFish` had this shape:

```
        rep #$20
        .a16
        lda #FISH_SWIM
        bra @apply
    @west:
        ora #AF_HFLIP
```

The `.a16` is still in force at `@west`, so `ora #AF_HFLIP` was assembled three
bytes wide -- but the CPU reaches `@west` in eight-bit mode. It executed
`ORA #$08` and then ran into the leftover `00` as a **BRK**, and everything
after that was misaligned instruction bytes. Those bytes contain absolute
addresses of the actor arrays, which move with `MAX_ACTORS`, which is why the
damage looked like a layout problem. It fired the moment a fish first turned
around, about forty frames in.

`tools/check_modes.py` now catches this whole class: for every branch it takes
the register widths in force at the branch, walks forward from the target
alongside the assembler's own assumption, and complains the first time an
immediate is sized against a width the CPU does not have. The naive form of
that check reports thirty-odd sites here and all but one are correct code, so
the walk steps the CPU's widths through any `rep` / `sep` it meets and stops
once the two agree -- a target opening with `sep #$20` is doing the right
thing. `make` runs it before assembling anything.

Three of these needed terrain that did not exist:

- the **west cliff**, three steps up, with the **waterfall** coming down its
  face at `(4,6)` into a **pool** at `(4,7)`;
- the **Secret Place** at `(1..3, 6..7)`, walled by the cliff to the north and
  west and open to the sea to the south, so the only way in is through the
  pool under the fall;
- a **leaning tree** by the bridge — a step at `+1`, trunk at `+2` and a leafy
  top at `+3`, which the one-step rule turns into a climb.

The back wall of the chamber carries the three drawings, at `(1,6)`, `(2,6)`
and `(3,6)`. They are `AF_FLAT`, so they keep a height of zero and land on the
face of the cliff behind them rather than on top of it, and being one tile
apart along a straight wall is what lets a plain nearest-wins scan tell them
apart.

## The race

Talking to Kairi once day two is in triggers it. Both boys are put on the
start line beside her, she counts down on the HUD row, and the course runs out
along the east shore, over the footbridge and the raised bridge, round the paopu tree on
the small island and back to her.

Riku follows twenty markers laid over that route, closing the gap to each one
by at most one step a frame. He ignores the ground: every marker sits on a
walkable tile and every straight line between two of them stays on land, so
steering him round the boulder would cost more than it is worth. He runs at
about seven-tenths of Sora's pace, which is what makes the race winnable
without making it free.

Sora has no markers — he can take any line he likes. Getting within reach of
the paopu tree flips the objective, and getting back to Kairi after that wins.
Whichever of them finishes first is the one who names the raft: Sora gets the
choice of **Highwind**, **Excalibur** or **Ragnarok**; Riku, given the chance,
picks Excalibur without being asked.

That three-way choice is why the dialogue box's prompt is no longer hardwired
to yes/no. `txtMode` doubles as a menu id, so a scene asks for a list of
choices by opening the box with `TM_RAFT` instead of `TM_PROMPT`, and the
cursor wraps over however many the list holds.

## The night it falls

Once the raft has a name and everything is on it there is nothing left to do,
so Kairi's last line is the last line of the day: dismissing it puts the light
out, and what comes up is not the morning.

The whole scene runs on the same tileset, the same tilemap and the same
collision map the two days use. What makes it night is **one palette upload** --
`nightpal.bin` over CGRAM 0-127, plus a pair of OBJ palettes. The only new
ground in the game is the fragment at the end.

`nightStage` holds the sequence:

| | |
| --- | --- |
| `N_INTRO` | the storm; the raft is already gone, and so are the other two |
| `N_SEEK` | Shadows are arriving. A wooden sword goes straight through them, so the only thing to do is find Riku |
| `N_RIKU` | he has said his piece, and the dark is coming up round him |
| `N_KEY` | the Keyblade arrives, and the Shadows become killable |
| `N_KAIRI` | the Secret Place, and who is standing in it |
| `N_DOOR` | the door comes off the rock and she goes with it |
| `N_TEAR` | the island comes apart |
| `N_BOSS` | Darkside, on the last piece of it |
| `N_END` | beaten; the dark takes the rest |
| `N_OVER` | the card |

Riku is out past the raised bridge where he always sits; Kairi is in the Secret
Place with her back to the door. The chalk is still on the wall, which is the
point of its having been there for two days.

### The storm

Lightning is eight frames of additive white through the colour-math unit -- the
same path the whiteout at the end of the Dive takes -- with two strikes to a
flash and a random 150-277 frame gap between them. To make that possible
without tearing a seam across the frame it lands on, `CGWSEL` and `CGADSUB` are
now shadowed in RAM and written by the NMI, so the unit can be switched between
"translucent shadows" and "add white to everything" in vblank.

### The Shadows

They keep arriving up to six at a time, from ten spawn spots spread over the
island so one never appears in Sora's face. `tools/check_map.py` holds the same
ten and fails the build if a map edit strands one.

OBJ palette 1 is the problem the night has to solve: it belongs to the
islanders, and the Heartless need it too. Riku and Kairi are the only islanders
still out there, so the three colours the others were using -- Tidus's blond,
Selphie's brown, Wakka's orange -- are given over to the Shadows, and a second
cut of the same Shadow art is drawn against those indices. Which cut a Shadow
uses is `heartTile`, set per scene by `LoadScene`.

Two things here are deliberately not faithful:

- **The Shadows cannot hurt Sora before the Keyblade.** In the source they can.
  Here there would be no way to answer them, and a death loop in a corridor
  with one exit is not tension, it is a wall.
- **He keeps the drawn Keyblade in his hand throughout.** A second thirty-cel
  sheet of him holding a wooden sword is not worth 15 KiB of ROM.

### The last piece of it

`assets/fragment.txt` is a second 32x16 map in the same terrain codes plus one
new one: `*`, the dark, which is palette index 0 and therefore folds to a single
character. One round scrap of island with its ring of shallows still clinging
on, laid out to fill exactly one screen so the camera can be pinned on it the
way the Dive's is.

Darkside rises at `(15,7)` -- as far up the scrap as a 64-pixel sprite can
stand and still keep its head clear of the HUD.

### The ending

Subtractive colour math on BG1, the sprites and the backdrop, ramped to full
over sixty-two frames. BG3 is left out so the closing line stays readable.

The SNES only applies OBJ colour math to palettes 4-7, so Sora -- on palette 0
-- does not darken with the world. That was not the plan, but it is the right
picture: everything goes, and he is left standing in it holding the Keyblade.
