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

- **Talkable NPCs** and a dialogue box — everything else depends on it.
- **Item pickups with counts**, and a quest tracker that reads the tables above.
- **Interactable scenery**: palms that drop coconuts when struck, a climbable
  thin tree, a waterfall trigger, an enterable treehouse and Secret Place.
- **Multi-level terrain** — the rope is on a high platform and the bridge is
  raised, so the island is not a single flat plane. This is the biggest
  departure from the current engine, which assumes one ground height.
- **A race mode** with a course, a timer and an opponent running a fixed path.

Shadow Heartless do **not** belong in these scenes. They arrive on the night
the island falls, which is a later state of the same map.
