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

Shadow Heartless do **not** belong in these scenes. They arrive on the night
the island falls, which is a later state of the same map.

## Raised ground

`assets/island.txt` gives every terrain code a height in eight-pixel steps, and
`tools/build_assets.py` emits it as `heightmap.bin` beside the collision map.
Three things use it:

- **Painting.** A raised tile's diamond is drawn `8 * height` pixels higher,
  with the side of the block filled in underneath, so the deck sits on posts.
  Because the block still reaches back down to where the tile would sit at
  ground level, the existing back-to-front paint order needs no changes.
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
| `(8,10)` beach | Kairi — gives the list, checks it off |
| `(12,1)` small island | Riku, past the raised bridge |
| `(3,5)` high platform | Tidus, with the rope beside him |
| `(7,12)` dock | Selphie |
| `(11,10)` past the footbridge | Wakka |
| `(11,11)` | Log — the shore past the little wooden bridge |
| `(12,2)` | Log — the small island where Riku sits |
| `(7,6)` treehouse deck | Cloth |
| `(3,6)` high platform | Rope |

Handing the list in ends the day: the screen dims, the island is rebuilt with
day two's set, and it comes back up on the morning after.

## Day two, as built

| Where | What |
| --- | --- |
| `(4,11)`, `(3,10)`, `(12,10)` shallows | Fish ×3 — swing at them from the shore |
| `(6,11)` | Mushroom, in the hollow behind the rock by Kairi |
| `(4,5)` | Mushroom, in the bushes at the foot of the tower |
| `(1,7)` | Mushroom, inside the Secret Place |
| `(7,9)`, `(9,9)` | Coconut palms — swing until the gold ones come down |
| `(7,2)` treetop | Seagull egg, up the leaning tree by the bridge |
| `(2,7)` pool | Bottle, under the waterfall |

Handing day two's list in is what Riku has been waiting for.

Three of these needed terrain that did not exist:

- a **rock tower** at `(2,5)`/`(1,6)` with the **waterfall** coming down
  `(2,6)`, and its **pool** at `(2,7)`;
- the **Secret Place** at `(1,7)`, walled in by the tower on every side but
  the pool, so the only way in is through the fall;
- a **leaning tree** by the bridge — trunk sections at `+1` and `+2` and a
  leafy top at `+3`, which the one-step rule turns into a climb.

## The race

Talking to Kairi once day two is in triggers it. Both boys are put on the
start line beside her, she counts down on the HUD row, and the course runs out
along the east shore, over the raised bridge, round the paopu tree on the small
island and back to her.

Riku follows eighteen markers laid over that route, closing the gap to each one
by at most one step a frame. He ignores the ground: every marker sits on a
walkable tile by construction, so steering him round the boulder would cost
more than it is worth. He is a little slower than a clean line, which is what
makes the race winnable without making it free.

Sora has no markers — he can take any line he likes. Getting within reach of
the paopu tree flips the objective, and getting back to Kairi after that wins.
Whichever of them finishes first is the one who names the raft: Sora gets the
choice of **Highwind**, **Excalibur** or **Ragnarok**; Riku, given the chance,
picks Excalibur without being asked.

That three-way choice is why the dialogue box's prompt is no longer hardwired
to yes/no. `txtMode` doubles as a menu id, so a scene asks for a list of
choices by opening the box with `TM_RAFT` instead of `TM_PROMPT`, and the
cursor wraps over however many the list holds.
