# Destiny Islands, the PS2 play island — a design reference

**WHAT THIS IS.** A reconstruction of the Kingdom Hearts 1 play island, assembled
from public descriptions — the Kingdom Hearts wikis, StrategyWiki, published
walkthroughs and guide scans — by thirteen researchers working area by area and
then checking each other. It is a REFERENCE FOR DESIGN, not a specification this
port must match, and it is not checked by anything in Gate 0.

**WHAT THE PORT ACTUALLY DOES IS `docs/DESTINY_ISLANDS.md`.** Where the two
differ, that file is the truth and this one is the thing it diverged from. The
demake compresses, drops and reshapes deliberately: it is a 16-bit top-down game
with a 256x192 screen, no new dialogue can ever be written (`build_scripts.py`
checks every script byte-for-byte against the frozen ROM), and several PS2 systems
have no room in it.

**NO ASSET HERE DERIVES FROM ANY OF THIS.** Every pixel in the tree is drawn in
code by `tools/build_assets.py`; the root `README.md` states the same. What this
document contributes is knowledge of a place — where the cove is relative to the
beach, which way the tunnel runs, who stands where on which day — which is the
part that cannot be invented and stayed wrong for as long as nobody looked it up.

**READ SECTION 0 AND SECTION 5 BEFORE BUILDING FROM ANY OF IT.**

* Section 0's compass is DECLARED, NOT CANON. KH1 has no compass and the wikis
  flag KH1 and KH2 as contradicting each other. The relational clusters are the
  reliable part; the labels are a convention this document adopts so it can be
  read consistently.
* Section 5 is where it admits to inventing, and it is the most valuable section
  in the file. Three examples of the kind of thing it caught:
  * the "small tunnel beside the star tree" leading from the Cove back to the
    Seashore is asserted by two sources and argued against by three — **do not
    build a working exit there**;
  * whether the Secret Place can be entered on day one at all is unknown;
  * "runs north then bends west" for the tunnel **was never sourced**, which is
    worth knowing because that is the shape `assets/ds/cave.txt` was already
    built to. It is an invention that happens to be reasonable, not a fact.

**IT ALSO CORRECTED THREE THINGS THIS PROJECT HAD WRONG** before a line was
written for the Cove: the raft is built in the COVE and not on the Seashore;
Tidus, Selphie and Wakka are all on the SEASHORE and never in the Cove, so the
sparring is a beach system; and the Seaside Shack does NOT join the Seashore to
the Cove — it is a stair-hut whose two doors both land on the Seashore map, and
the Cove is behind a separate hinged door in a timber palisade that Kairi blocks
for the whole of day one.

---

# BUILD SPEC — DESTINY ISLANDS PLAY ISLAND (top-down 16-bit demake)

## 0. DECLARED CONVENTIONS (read first — these are arbitrary, adopted for consistency)

No source establishes true compass bearings; KH1 has no compass or minimap and the wikis flag KH1/KH2 as contradicting each other. The following convention is **declared, not canon**, and is used everywhere in this spec:

**SEASHORE / SHACK / SECRET PLACE / ISLET:** open ocean = **SOUTH**, cliffs & island interior = **NORTH**, Tidus's lookout + jetty + great tree + dock = **WEST → CENTRE**, waterfall + shack + Secret Place + Cove door + terrace + bridge + islet = **EAST / SOUTH-EAST**.

**COVE:** door end = **EAST**, star tree = **WEST**, open sea = **NORTH**, cliff = **SOUTH**.

Prefer relational names in code/asset names — `LOOKOUT_END` / `BRIDGE_END`, `SEAWARD` / `INLAND`, `DOOR_END` / `STAR_END` — so the whole island can be mirrored later without a rewrite. Four of five area records agree on the *relational* clusters even where they disagree on the labels; the relations are the reliable part.

**Elevation tiers** (the Seashore is the only strongly layered area; use four):
- **T0** — sand / beach floor / water level.
- **T1** — low shelf, one step up (~1 tile of height). Cove-door shelf, ramp tops, dock deck, Cove entrance landing, Cove embankment.
- **T2** — terrace level, ~one storey (~2–3 tiles). East terrace, bridge deck, Seaside Shack roof, Secret Place mouth ledge, islet plateau.
- **T3** — high platforms. Tidus's lookout deck, tree-house balcony, Cove watchtower top.

---

## 1. CONNECTION GRAPH

Areas: **SEASHORE** (hub, contains the tree-house alcove, east terrace, bridge and paopu islet as sub-areas of one map), **SEASIDE SHACK** (interior), **SECRET PLACE**, **COVE**, plus two non-walkable destinations: **DARKSIDE ARENA (Night of Fate)** and **SORA'S ROOM**.

### Confirmed, reciprocal
```
SEASHORE --(hinged plank door + gabled awning + latch, set in tall timber palisade wall;
            loading screen; T1 shelf at top of a short sand ramp, EAST end behind/east of
            the shack, N/NE of the islet, just inland of the bridge foot; split-rail fence
            across the sand in front)--> COVE
COVE     --(same door; EAST end, above/behind the raised entrance landing at the near
            end of the wooden decks)--> SEASHORE
   GATE: Kairi stands in front of it and blocks it for the whole of Day 1. Open from Day 2.
   Night of Fate: treat as sealed (no Cove night map exists).

SEASHORE --(plank doorway at sand level, short wooden ramp up; EAST end at the cliff foot,
            immediately EAST of the waterfall plunge pool)--> SEASIDE SHACK (lower door)
SEASIDE SHACK --(lower doorway, S wall of the interior)--> SEASHORE (beach, T0)

SEASIDE SHACK --(upper doorway at the head of the internal stairs, rear/N-NE side,
            ~1 storey up)--> SEASHORE (raised EAST terrace, T2)
SEASHORE --(shack upper door, on the T2 east terrace)--> SEASIDE SHACK
   NOTE: both shack doors land on the SAME Seashore map. The shack is a pure vertical
   shortcut with one piece of unique content (the island's only Save Point).

SEASHORE --(low rock hole with heavy wooden lintel, inland/north face of the T2 ledge
            immediately WEST of and directly ABOVE the twin waterfall, at the foot of the
            great tree's root mass, directly above/behind Wakka's Day-2 spot; reached by
            ramp from sand then a one-ledge hop up)--> SECRET PLACE
SECRET PLACE --(cave mouth, S extremity of its map, raised)--> SEASHORE (T2 ledge)
   Night of Fate: a large door (the Dive-to-the-Heart door) stands in this opening; Sora
   passes through it into the same tunnel.

SEASHORE (T2 east terrace, bridge foot) --(walkable stilted plank bridge, SE)--> PAOPU ISLET
PAOPU ISLET (NE rim landing) --(same bridge, NE)--> SEASHORE east terrace
   NOT a room transition — the islet is inside the Seashore map. No load, no fade.

SEASHORE (whole S waterline) --(open water, swim)--> SHALLOWS --> SEASHORE / islet
PAOPU ISLET (SW seaward rim) --(fixed wooden ladder or short steps flat against the rim,
            the only break in the islet's edge)--> water, swim back to beach
```

### Confirmed, one-way / scripted (no reciprocal — correct as-is, not a missing door)
```
SEASHORE --(scripted map swap on the storm night; not a walkable door)--> DARKSIDE ARENA
SECRET PLACE --(cutscene ejection at the chamber's wooden door: the door bursts open behind
            Kairi and blows Sora out)--> DARKSIDE ARENA
SECRET PLACE chamber wooden door --> SEALED FOREVER. This is the world's Keyhole. No handle,
            no hinges, no keyhole in normal play. Never usable as an exit by the player.
COVE far-west ramp top --> DEAD END LEDGE. Build no exit here (see below).
SEASHORE --(scripted, end of Day 1 and end of Day 2)--> SORA'S ROOM (off-island, mainland)
```

### ⚠ EXITS REPORTED BY ONLY ONE SIDE — flagged for the map builder
| Exit | Reported by | Verdict |
|---|---|---|
| **Cove → Seashore, "small tunnel next to the star tree"** at the far/star end | Cove-side text only (khwiki, StrategyWiki). The Seashore record lists **no** second Cove entrance anywhere on the beach. | **DO NOT BUILD A WORKING EXIT.** Three separate records independently argue against it (Fandom's parallel article omits it; BradyGames says "follow the same path back"; every speedrun and JP route enters and leaves via the one door). Build the ramp top as a scenic ledge / dead end. If you ever do build it, it must emerge on the Seashore *far from* the Cove door, not beside it. |
| **Islet seaward ladder** — whether it lands on the **islet** or on the **bridge terrace** | Seashore + islet records say islet's seaward rock face; one Gamer Guides reading says it delivers you to the islet, khwiki's wording implies it delivers you to the bridge. | Build it **on the islet's seaward rim**. Majority reading; also the only version that makes the rim a closed ring with exactly one break. |
| **"Jump up from beside the waterfall onto the terrace / shack roof"** as a third mainland route to T2 | Seashore + Shack records assert it; the islet record traces both mentions to one blocked source and calls it unconfirmed. | Build the **shack-roof jump** (confirmed by two independents, used by every speedrun on all three days). Treat the waterfall-side jump as the same affordance — one jumpable ledge inland of the shack that reaches both the Secret Place mouth and the shack roof. |
| **SORA'S ROOM** exists in both datamined room lists (`di00_06` / room 09) but **no researcher gave it an exit.** | — | Pure gap. Wire it as a scripted bedtime/wake transition, not a door on the play island. |
| **Second wooden doorway on the east terrace** visible in concept art / the geometry rip | Two records saw **two** openings within a few metres at the east end. Assignment (rock-in-cliff = Secret Place, timber-with-hinged-leaf = Cove) is inference and "could be swapped." | Build both openings as described; if one turns out to be decoration, make the rock one the cave. |

---

## 2. RELATIVE LAYOUTS

### 2.1 SEASHORE (hub; the biggest map — build as a wide strip, ~4:1 or wider)

North edge = grey cliff + timber-clad wall (blocking). South edge = waterline → swim zone. All landmarks strung west→east.

| Cell | Content | State |
|---|---|---|
| **NW** | Grey cliff / plank-clad wall with a **wooden ladder** climbing it from the sand | blocking wall; ladder = climbable |
| **W** | **Two mushroom-shaped rock stacks**, foliage-capped, clad in wooden planking, rising out of the water off the west tip. The nearer (eastern) stack carries the **high wooden lookout deck** with a curved plank walkway on top — **T3**, the highest walkable point on the west side. **Rope** lies in a corner of the deck. Tidus stands here Day 1. | stacks blocking; deck raised T3, walkable, narrow, with fall-off edges |
| **SW** | **Plank walkway / jetty on stilts** ("桟橋"), running out SW from the west end of the sand across the shallows to the stacks, with a short set of steps. A plank walkway on stilts also crosses from the near stack westward to the outer stack. | raised T1, walkable |
| **N (centre-north)** | **The enormous tree** — blocking trunk set back on the terrace behind a retaining wall. **Broad wooden stairs on its WEST side** up from the terrace; **plank walkways + a ladder** spiral round the trunk to a **railed balcony (T3)**; behind the balcony a **walk-in opening in the trunk = the tree-house alcove**, with the **Cloth** hanging on the wall to the RIGHT as you enter. Root mass runs east into the cliff. | trunk blocking; stairs/walkways/ladder climbable; balcony + alcove raised T3, walkable; **alcove is an alcove of this map, NOT a room** |
| **Centre** | Open walkable sand. **Wooden crates** scattered, notably west of the great tree. Sora's Day-1 wake-up spot is mid-beach near the water's edge, dock and tree behind him. Practice duels happen out here. | walkable; crates small props |
| **S (centre-south)** | **Plank dock on stilts** standing in the wet sand/shallows directly in FRONT of the great tree; plank ramp/steps up at its **west** end. Selphie sits on it Day 1. **You can walk beneath it.** Fish swim in the shallows around it. | raised T1, walkable deck, walkable underneath, long and narrow (corridor arena) |
| **N-E of centre** | Solid grey cliff — **nothing breaks it between the tree and the shack** except the falls. **Twin waterfall**: two thin cascades from square notches high in the rock into a small round **plunge pool** at sand level, ringed with pink lotus. Walk under/into it to fill the Empty Bottle → **Drinking Water**. Fish also swim in front of it. | cliff blocking; falls pass-through; pool walkable-into |
| **E** | **Seaside Shack** at the cliff foot, immediately EAST of the pool. Small two-level plank hut, shingled/plank roof. Beach-facing door reached by a short wooden ramp up from the sand. **Roof ridge is level with T2.** More crates near the pool. | building blocking except doorway; roof solid + jumpable-onto, walkable |
| **NE (T2)** | **Raised east terrace** — walkable raised sand shelf held up by plank and log-crib retaining walls, backed by grey cliff and a tall timber-clad wall. The east-end hub. On/against it: the shack's **upper door**; a **cluster of palms** (one skinny white tree you climb ~75% up, then jump across to the palm holding the **Seagull Egg**); a **split-rail fence**; the **Secret Place mouth** (on the T2 ledge just west/inland, directly above the falls, with a bush just outside it); and the **landward foot of the bridge**. | raised T2, walkable; palms blocking trunks, climbable; fence blocking |
| **E / NE (T1, one tier BELOW the palm terrace)** | **Cove door shelf** — a small raised patch of sand behind/east of the shack, reached from the sand by a short ramp and from T2 by dropping down. Carries the **hinged wooden Cove door** with its plank awning, set into the tall timber palisade. A rustic split-rail fence runs across the sand in front. **Kairi stands here all of Day 1.** | raised T1, walkable; door = transition, gated Day 1 |
| **SE** | **Wooden bridge to the islet** springs from the T2 terrace and runs SE on trestle stilts over the shallows. **Log #1 lies on the sand directly UNDERNEATH it.** The bridge deck has one askew plank used as a cutscene trigger on the storm night. The bridge must be a *stageable space*, not just a corridor — the Riku paopu-fruit scene plays on it. | bridge raised T2, walkable; sand under it walkable |
| **Along the whole N edge** | **Retaining walls** in repeated sections — west end, under the great tree, around the shack and terrace. Alternating white rounded-cobble panels, vertical plank fencing, log crib. Treat as a recurring motif, not one continuous wall. | blocking, waist-high |
| **Whole S edge** | Waterline → swim zone. Widest shallows off the dock and off the waterfall. Swimming catches the three **Fish** and reaches the islet's seaward ladder. | swim |

**Traversal shortcuts to implement:** ramp from sand → T1 shelf; ramp + one-ledge hop → T2 Secret Place ledge; from that ledge, drop/run onto the shack roof → straight across onto the bridge, skipping the interior entirely (used Day 1, Day 2 and Night of Fate). Holding into a ledge edge makes Sora run up onto it rather than jump.

### 2.2 PAOPU ISLET (sub-area of the Seashore map — build as a small separate sub-rectangle offshore SE)

Elevated islet, plateau at **T2**, ringed by a low blocking rim (~one storey, a climb not a step). Ring is **continuous except one break**.

| Cell | Content | State |
|---|---|---|
| **NE rim** | **Bridge landing** — flat sand where the plank bridge meets the islet. The only level entry. | walkable |
| **Centre** | **Central sand plateau** — flat, open. **The arena**: Riku duel (Day 1), Tidus / Tidus+Wakka+Selphie 3-on-1 (Day 2), Riku's darkness scene (storm night). Must fit a 3-opponent brawl. This is the hard floor-area constraint. | walkable |
| **W / NW rim** | **Bent paopu tree** — thick smooth pale trunk leaving the rim at a shallow angle, staying near-horizontal, crown hanging out over open sea. **Sittable and climbable.** Riku sits here Day 1; the sunset conversation and the storm-night scene both stage here. | sittable/climbable prop |
| **S / SW (inner, among the trees, away from the bridge)** | **Log #2** | pickup |
| **On the plateau** | **Fat / thick coconut palms** — strike with the wooden sword to drop **Coconuts**. One of only two coconut sources in the world. | blocking trunks, shakeable |
| **SW rim (seaward)** | **Wooden ladder / short steps** flat against the rim, dropping into swimmable water. The only break in the rim; the third route onto the islet. | climbable both ways |
| **Rim + scattered** | Thinner palms and low shrubs fringing the rim. | scenery/blocking |
| **All around** | Swimmable water ring between islet and beach. | swim |

### 2.3 SEASIDE SHACK (interior — one tiny room, bare)

| Cell | Content | State |
|---|---|---|
| **S wall** | **Lower doorway** at sand level → Seashore beach. | transition |
| **E, just inside as you enter** | **Save Point** — green swirling crescents. **The island's only save point.** No collision; touching it saves and fully restores HP/MP. Usable on all three days. | walkable-through |
| **N / rear** | **Wooden staircase**, short flight, no landing, topping out directly at the upper door. | climbable |
| **N-NE, top of stairs (~1 storey up)** | **Upper doorway** → Seashore T2 east terrace. The climbable skinny tree and coconut palms stand DIRECTLY in front of it; the bridge starts immediately beyond. | transition |
| Everything else | **Bare.** No NPC, no chest, no item, no barrels/nets/hammock/upper floor in any source. | — |

Room is crossed in two or three seconds. Whole point: it is a staircase with a save point.

### 2.4 SECRET PLACE (one dead-end cave; day / night / "Past" dressings)

Mouth at the **S** extremity (raised, at the seam with the Seashore T2 ledge). Tunnel runs **N / inland**. Chamber at **N**.

| Cell | Content | State |
|---|---|---|
| **S** | **Cave mouth** — low hole in rock with a heavy wooden lintel, facing out over the beach and sea. Small/low ("tiny cave", Sora "climbs into" it) but walk-through in practice — no crouch animation exists. Approached by ramp then a one-ledge hop. A bush sits just outside. The shack roof is a drop-and-run away. | walkable transition. Storm night: a **large door** stands in this opening. |
| **S → N, centre** | **Entry tunnel** — plank-lined at the mouth, then rock corridor. Roughly one-Sora wide, **no branches, no side alcoves, no climbing.** Long or curved enough that speedrunners note the camera misbehaves. | walkable corridor |
| **N (chamber)** | **The chamber** — roughly circular/oval, flat reddish-brown earth floor, dim ambient light. Sits under the enormous tree. Big enough to walk, fight (no enemies ever spawn) and stage a cutscene. | walkable |
| **N back wall of chamber** | **The wooden Door / the Keyhole** — tall arch-topped door of vertical dark planks in a raised frame, a little taller than a teenager. No handle, no visible hinges, no keyhole in normal play; a glowing keyhole crest appears on it in cutscenes. Faces you across the floor as you arrive. | **blocking, never openable** |
| **NE — on the floor beside the door, on its RIGHT as you face it** | **Mushroom** (1 of 3). Picking it up fires the paopu-drawing cutscene and the cloaked-man encounter. | pickup + trigger |
| **N, on the rock immediately beside the door, low down at kneeling height** | **Sora-and-Kairi paopu drawing** — two child-drawn faces in profile facing each other, each with an arm reaching in with a star-shaped paopu. Sora adds his half in the cutscene. Kairi's hand only appears after the islands are restored. | non-interactive |
| **All chamber walls** | **White chalk drawings** on stacked wall stones, densest on the broad faces. Confirmed subjects: Donald, Goofy, a Chocobo, a dragon, Disney Castle, a crowned world, card-suit/fan shapes. | decorative |
| **Ceiling + upper walls all round; one thick root beside the door like a column; more crawling over the floor rim** | **Tree roots** — thick, pale, gnarled, from the giant tree standing over the cave outside. | decorative / blocking |
| **Scattered on the open floor, more banked at the wall feet** | **Loose rounded boulders**, waist-to-chest high, some chalk-scribbled. Obstacles, not climbing routes. | blocking |

### 2.5 COVE (one continuous outdoor obstacle course; Day 2 only. Build as a wide strip, ~3:1)

Sea = **N**, cliff = **S**, door = **E**, star tree = **W**.

| Cell | Content | State |
|---|---|---|
| **E (door)** | **Cove door** in the tall wooden plank palisade with diagonal bracing, above/behind the entrance landing. → Seashore. | transition |
| **E** | **Entrance landing** — raised patch of pale sand roughly level with the deck tops, its N (seaward) edge held by a low fence of vertical wooden stakes with loose stones scattered on it. **Race start AND finish line.** You either step onto deck 1 or run/hop off its edge down to sand-and-shallows level. | raised **T1**, walkable; **finish = landing back up on it** |
| **N of the decks, filling the bay between the landing (E) and the embankment (centre); a pale sandbar shows through mid-way** | **Shallow tidal inlet** — shin-deep sea over sand. The standard bypass for the whole deck section. Decks stand high enough to walk underneath them in the water. | walkable (shallow), slow-lane |
| **E → centre, spanning the inlet a body-height up on timber legs, standing out in front of (not flush against) the S cliff** | **Wooden decks on stilts** — **THREE distinct raised sections with open jumpable gaps**: a two-tier section nearest the entrance, a large middle section, then a smaller, slightly lower section ending at the embankment. **One plank is a trap that drops out from under whoever steps on it.** | raised, walkable, jumpable, one trap tile |
| **In the gap between deck 1 and deck 2, at water level** | **Two flat square plank pallets** resting on the shallows. | probably standable; treat as low scenery if unsure |
| **S cliff face at the back of the inlet, between deck 1 and deck 2, spilling to water level** | **Spring / water outlet** — dark hole in the rock with a thin waterfall running out. Walk into it with the Empty Bottle → **Drinking Water** (the island's second source). | non-blocking scenery + interact |
| **Centre; its N (seaward) face is a curved cobblestone retaining wall topped with plants** | **Embankment** — raised walkable platform higher than the beach, where the deck run lands. Drop off its **W** side onto the far beach. Carries the watchtower, the crate and the plant patch. | raised **T1/T2**, walkable |
| **N tip of the embankment, on its stone edge** | **Wooden steps** down to the sand/water — "right of where the wooden bridge ends". The shortcut that lets the whole deck run be skipped by wading. | walkable stairs |
| **Centre, on the embankment, backed against the S cliff** | **Watchtower (やぐら)** — tall timber lattice tower topped by a small roofed cabin with a round window and a windmill/pinwheel on the ridge; a tarp or coil of rope on the top platform. A **ladder** runs up it. Sora can jump from the end of the deck run to grab the ladder high up. **Standing on the ladder physically blocks Riku.** Top = zip line's upper anchor. Safe route passes on its **N (seaward)** side. | tower blocking; ladder climbable; top raised **T3** |
| **On the embankment at the tower's foot, in the foliage on the wall side, LEFT of the ladder** | **Mushroom** (1 of 3) | pickup, in non-blocking bushes |
| **On the embankment near the steps and the tower base** | **Wooden crate** — **pick up and CARRY it** (do not throw), set it against the rock wall under the high alcove, jump off it into the alcove. | carryable prop |
| **High in the S cliff wall above the embankment, out of normal jump reach; visible from the watchtower** | **High alcove with the treasure chest** — square hole in the rock holding the Cove's only chest: **Protect Chain** (Sora's first accessory). Reachable **only** by standing on the crate. | raised, reachable only via crate |
| **Strung from the tower top, running W and downhill over the far beach** | **Zip line (rope)** | ridable |
| **W of the tower, raised above the far beach, against the S cliff** | **Zip-line landing platform** — the "smaller wooden construction". Drop to sand from it, or jump straight into the coconut palm crowns. | raised, walkable |
| **Against the S cliff on the far beach, immediately W of the embankment, just before the raft** | **Boulder over a small dead-end cave** — shove it aside (push from the side) to open an alcove holding **Mushroom** (1 of 3). | pushable; alcove walkable |
| **Centre-W, between the boulder cave (E) and the palm grove (W), toward the cliff side** | **The raft** — half-built. Kairi stands/leans here after the race. The race route runs "right of the boat and up the ramp". | blocking prop + NPC anchor |
| **Centre-W → W, open sea lapping its N edge** | **Far beach** — wide flat walkable yellow sand. The long leg of the safe race route, both ways. | walkable |
| **W, between the raft and the foot of the ramp; a few more up on the ledges** | **Coconut palm grove** — roughly eight tall palms. **Crowns are jumpable platforms.** Attack a fat trunk to shake **Coconuts** down; each tree drops many. **Brushing a trunk while running makes Sora start climbing it** — a race-time penalty. | trunks blocking; crowns raised walkable |
| **Far W: a broad pale sand slope rising from the beach along the seaward edge of the headland, bordered by dense green, curving up to a small flat top with a grass-capped rock** | **Sand ramp up the headland** — walkable slope, **no jumping needed**. At the top you turn inland (S) for the ledges. | walkable slope |
| **SW, stepping up the cliff face above the ramp top: a first ledge, then a second higher one** | **Grassy / mossy rock ledges** — two hops from ramp top to the star tree's ledge. One hop is described as needing a sideways jump plus a midair turn. | jumpable platforms |
| **Extreme W/SW, on the highest ledge** | **Star tree** — a tiny tree with a **star-shaped lamp** on top. The race's **turnaround checkpoint**. Jump at it until it **lights up and chimes**; the touch only registers when it glows. Half-way point, not the finish. **NOT a climbing frame.** | interact target |
| **Along the S cliff wall, running E from the star-tree ledge back toward the tower/embankment** | **Treetop return route** — a line of tall trees and leafy platforms used as stepping stones. Fast but fiddly. | jumpable crowns |
| **Somewhere on the ground (spot undocumented)** | **Hidden Mickey** — flat decorative insignia. | non-interactive |
| **Ramp top / far W** | **DEAD END.** No second exit (see §1 flags). | — |

---

## 3. CHARACTER PLACEMENT BY DAY

### DAY 1 — raft materials (2 Logs, Cloth, Rope). Cove is **sealed**. The entire day is completable without a single room transition (except the optional shack).
| Who | Area | Exact spot | Doing |
|---|---|---|---|
| **Sora** | Seashore | Wakes lying on the sand mid-beach near the water's edge, dock and great tree behind him | Wakes from the Dive with Kairi leaning over him; Riku wades out of the surf with a log; then free roam |
| **Kairi** | Seashore | **T1 Cove-door shelf**, EAST end, at the top of the short ramp, in front of the wooden door | Blocks the Cove door; asks for the materials; hand-in here → **Hi-Potion** (fewer hints asked = better reward); then the sunset "call it a day" conversation |
| **Riku** | Seashore → **paopu islet** | Sitting on the bent paopu trunk | Repeatable wooden-sword duel (**90 HP, 5 EXP**, Potion per win). **Day-1 only.** Log #2 is nearby. Going too far out on the bridge or falling off the islet forfeits |
| **Tidus** | Seashore | **T3 lookout deck** on the western rock stack, far WEST end, up the ladder west of the jetty. **Rope** in the corner behind him | Spar (stick). Beat all three, then talk to Tidus for the 3-on-1 |
| **Selphie** | Seashore | Sitting on the **plank dock**, at its far end, centre of the beach | Spar (jump rope). Fish in the water around her |
| **Wakka** | Seashore | Out on the open sand. **CONTESTED** — Seashore record + khguides put him at the EAST end in front of the shack near the waterfall, below the Secret Place; khwiki + destinyislands just say "on the beach". **Build: open sand, centre-east, in front of the shack area** (he needs wide flat space for blitzball throws) | Spar (blitzball); his second dialogue option tells you where the others are |
| **Cove / Secret Place / Shack interior** | — | — | **Empty.** Cove unreachable |

### DAY 2 — provisions (Seagull Egg, 3 Mushrooms, 2 Coconuts, 3 Fish, Drinking Water). Cove is open.
| Who | Area | Exact spot | Doing |
|---|---|---|---|
| **Sora** | Seashore | Starts the day standing at the **dock** end | — |
| **Riku** | **Cove** | Just inside the door at the EAST end, on/beside the raised **entrance landing** | Argues about the raft's name → name-entry box → challenges Sora to the race; runs the fixed high route. Re-raceable from this spot indefinitely (1 Pretty Stone per win, up to 99). **NOT duel-able in the Cove** |
| **Kairi** | **Cove** | During the race: on the **entrance landing** (E), start = finish. After the race: **at the raft**, centre-west far beach, between the boulder cave and the palm grove | Explains the checkpoint rule, starts the race; afterwards hands over the **Empty Bottle** and the provisions list, gives hints if you say you're "totally clueless", pays a **Hi-Potion** on completion. **Nothing of hers is on the Seashore side on Day 2** |
| **Tidus** | Seashore → **paopu islet** | Standing on the islet plateau across the bridge | Spar; talking to him **here** starts the **3-on-1** |
| **Wakka** | Seashore | At the **base of the waterfall**, on the lower ledge in front of a bush, directly below/in front of the Secret Place mouth, EAST end | Spar; points you at "the secret place" behind him. Adjacent to the Drinking Water pickup — one trip |
| **Selphie** | Seashore | On the **beach/sand beside the dock**, not out on it. (One weak source says "on a bridge" — reject) | Spar |
| **Cloaked man (Ansem, hooded tan cloak)** | **Secret Place** | Standing in the chamber directly behind Sora as Sora stands up from the drawing | One cutscene only, fired by picking up the Mushroom: "came to see the door", "this world has been connected / tied to the darkness", then vanishes |
| **Shack interior** | — | — | **Empty** |

### DAY 3 / NIGHT OF FATE — storm. Cove closed/absent. Tidus, Wakka and Selphie are **gone permanently**.
| Who | Area | Spot |
|---|---|---|
| **Riku** | **Paopu islet** | Standing alone on the sand in front of the paopu tree, facing the sea, darkness pooling around him. Holds out his hand; both are swallowed; Sora is left holding the **Keyblade**. Sora's route: through/over the seaside shack and across the bridge |
| **Kairi** | **Secret Place** | Standing in the chamber in front of the wooden door, facing it, back to the tunnel. Hollow and unresponsive; the door blows open behind her and she dissolves into the wind of darkness, blasting Sora out |
| **Shadow Heartless** | Seashore (beach + terrace), endless spawns | **Immune to the wooden sword** until Sora has the Keyblade; they chase him |
| **Darkside (boss, 300 HP)** | **Darkside arena** | Fought after Sora gets the Keyblade on the islet and reaches Kairi in the cave. See §5 for the arena contradiction |

---

## 4A. THE RACE (Cove obstacle course) — implementable mechanics

1. **Availability:** Day 2 only. Day 1 the Cove is sealed by Kairi. The whole race stays inside the Cove — it is **not** a lap of the island.
2. **Start:** walk through the Cove door, talk to Riku on the entrance landing. He challenges Sora for the right to name the raft.
3. **Name entry:** a text-entry box appears **before** the race. Default pre-filled name reported as **"Excalibur"** (single source).
4. **Officiating:** Kairi stands on the entrance landing and starts the race with her count/whistle. Start point **is** the finish line.
5. **Objective:** touch the star-tree checkpoint at the far west end — **the star lamp must visibly light up and chime** or the touch does not register — then get back to Kairi first. Win is registered by **landing back up on the entrance ledge**; the scene fades out immediately if you won.
6. **No weapons.** Pure running and platforming.
7. **Stakes:** Sora wins → the raft (and later the default Gummi Ship) takes the typed name. Riku wins → **"Highwind"**. Story progression unaffected either way.
8. **Reward:** 1 **Pretty Stone** per Sora win (sells 30 munny original / **100 munny** Final Mix & 1.5) + a tick on the Sora-vs-Riku scoreboard. Nothing else.
9. **Count:** one race required to advance. Then talk to Kairi **at the raft** for the provisions list + Empty Bottle. **Rematches unlimited**, identical course, farmed to 99 stones.
10. **Riku AI:** a **fixed scripted high route** — decks (jumping the gaps) → tower ladder → zip line → sand ramp → mossy rocks → star → treetops → decks → finish. He is **not** a free-roaming racer.
11. **Riku handicap:** he **walks instead of running at scripted intervals**. This is what makes him beatable.
12. **Sabotage — trap plank:** one plank in the deck run collapses under whoever steps on it. If Sora jumps at the exact moment, **Riku** falls through and loses time. (Which section: reported as the second; unverified.)
13. **Sabotage — ladder block:** Sora standing on the tower ladder physically blocks Riku from climbing. **Sources conflict on the effect** — KHGuides says he takes the zip line *unless* blocked; khwiki says he *may* take it *if* blocked. Implement as "blocking changes his route"; pick a direction.
14. **Zip line handling:** jump to catch the handle, then **release ALL stick input** or Sora lets go early. Wait for a full stop, press Confirm (X/Cross) to drop off.
15. **Palm penalty:** brushing a palm trunk while running makes Sora start climbing it — a real time loss and the classic race-loser.
16. **Player route A (safe, widely recommended):** do not follow Riku onto the decks. **Run** (do not jump) straight off the landing edge into the shallow water → run the shore → up the wooden steps by the deck supports → past the raft → up the ramp → hop the mossy rocks → star → return the same low way → jump back up onto the embankment/ledge → hop the decks → Kairi. Skips all deck platforming but lets Riku run unimpeded.
17. **Player route B (fast, risky):** decks (jump the breaking plank) → tower ladder → zip line → ramp → rocks → star → **treetop hops** back to the zip-line-side structure → decks → Kairi. Fastest; the treetop hops are the hardest part. The zip line only pays off if you then take the treetops.
18. **Not implemented / unknown:** par time, countdown length, Riku's speed values, whether losing blocks progress (sources say the outcome "is unimportant" — assume it does not), whether the mandatory first race is the same instance as the rematches, and whether his walk intervals are fixed script or rubber-band.

## 4B. SPARRING (Tidus / Wakka / Selphie) — implementable mechanics

1. **Start:** walk up and talk; a dialogue prompt offers to spar; accept. No entry fee, no item requirement, no cooldown. Repeatable indefinitely, win or lose. The fight starts **on the spot where that NPC stands**, in the open world — no purpose-built ring, no walled arena, no loading transition. Opponent HP gauge appears on screen.
2. **Win:** drain the opponent's HP to 0 with the Wooden Sword and/or by reflecting their own weapon into them. No timer, no rounds, no hit-count target.
3. **Loss:** Sora's HP reaching 0 ends the match as a loss — **no Game Over, no story consequence.** Banked Tech Points / EXP are kept; re-challenge immediately. Consequence-free sandbox by design.
4. **Stats:** Tidus **60 HP / STR 5 / DEF 5 / 2 EXP**. Selphie **45 / 5 / 5 / 1**. Wakka **75 / 5 / 5 / 1**. 3-on-1 = all three at once, **180 HP total, 4 EXP + a Potion**.
5. **Core mechanic — the parry/deflect:** pressing Attack into an incoming attack at the right moment (weapon-on-weapon, or sword onto the flying blitzball) deflects it. A deflect (a) awards **Tech Points, which in KH1 are added to EXP**, and (b) **staggers/stuns the attacker**, opening a free combo window. Teaching the parry is the entire point of the system. A deflected blitzball flies back and stuns Wakka — and in the 3-on-1 can be aimed **into the other two**. Lock-on helps aim the return.
6. **Tech values.** Prose guides: ordinary swing/throw = **1 TP**, telegraphed strong attack = **2 TP**. khwiki instead gives multipliers against an unstated base: Tidus Downswing/Shake-Off **x1.0**, Frontflip/Backflip **x2.0**; Selphie Swinging **x2.0**, Plunge & Shake-Off **x0 (unparryable for points)**; Wakka Ball **x2.0**, High-speed Ball **x4.0**, Spin **x2.0**. The *ordering* (telegraphed = double) is solid; absolute integers are not.
7. **TIDUS — short-range melee rushdown.** Closes distance constantly, swings his pole; the most relentless of the three and the highest EXP. **Tires visibly if Sora keeps his distance** → opening. Moves: *Downswing* (straight down, Power x1.0), *Shake Off* (sideways/horizontal, Power x1.0), *Frontflip* (forward somersault + downward swing, Power **x0**, Tech x2.0), *Backflip* (backflip + upward swing, Power **x0**, Tech x2.0). Counter: sidestep the big swings, jump behind him and aerial-combo, or parry the horizontal slash and combo the stun. Hit-and-run ~3 hits then retreat.
8. **SELPHIE — medium-range mobile skirmisher.** Fastest and flightiest, lowest HP, weakest defence; circles, runs away and jumps rather than trading, so she is hard to hit but harmless once cornered. The jump rope has **surprisingly long reach and stuns Sora**. Moves: *Swinging Move* (advances while swinging the rope **three times**, Power x1.0 — the parryable one), *Plunge* (leaps forward and swings, used when Sora backs off, Power x1.0), *Shake Off* (swings in place, Power x1.0). Counter: run wide circles to make her whiff then punish with a 3-hit combo; or parry the rope, which flings it back and can bonk her head and stun her. She can be taken to ~1 HP by rope deflects alone → **fastest Tech Point farm**.
9. **WAKKA — long-range projectile zoner.** Backs off to open ground and throws; goes melee only if crowded. Moves: *Ball* (ordinary throw, Power x1.0), *High-speed Ball* (**jumps and shouts "Take this!"** as the telegraph, then hurls a much faster, harder ball, Power x2.0 / Tech x4.0 — deflecting it leaves him wide open), *Spin* (spins his body as a close-range swat, fast and hard to block, Power **x0**, Tech x2.0). Throws erratically when dazed. Highest HP but the safest to farm → the classic "grind on Wakka" level-up trick.
10. **The 3-on-1:** unlocked **only** after beating all three individually; then talk to **Tidus** again (most guides; khwiki's walkthrough says any of them offers it). All three at once, identical movesets and HP, so Sora is pressured at close/medium/long range simultaneously. Strategy: never get surrounded or cornered, keep moving, **use single swings rather than full combos** to stay mobile, lock on and kill one at a time to reduce it to 2-on-1 then 1-on-1, retreat to raised ground/ledges to drink a Potion. Kill order: most guides Selphie → Tidus → Wakka; khwiki says Tidus first as the most relentless; almost everyone saves Wakka for last because his ranged attacks are easiest to dodge and his balls can be reflected into the others. Rated 4/5 difficulty vs 2/5 for the singles.
11. **Rewards:** singles give **EXP + banked Tech Points only** — no item, no ability, no story change. The 3-on-1 gives 4 EXP **plus a Potion, repeatable on subsequent wins** — the island's only farmable item drop. The real payoff is early levels: because Tech Points feed EXP, players routinely reach level ~6–9 before the island falls.
12. **Availability window:** Day 1 and Day 2 only. From the storm night the three are gone and the matches are permanently unavailable; only earned levels/items carry forward.
13. **Arena as difficulty modifier — the terrain does the work:** Tidus's Day-1 **T3 lookout deck** is tight with fall-off edges (suits rushdown); Selphie's **dock** is a narrow corridor over water; Wakka's **open sand** is the only space wide enough for long throws; the Day-2 **islet plateau** must fit three opponents and offers raised ledges to break line of sight and drink.
14. **Not implemented / unknown:** whether an invisible battle boundary exists or Sora can simply run out of the area and abandon the fight (**no source addresses this** — pick one); the exact prompt/menu wording; the absolute TP integers; whether "Power x0" means genuinely zero damage (feint/reposition) or is a wiki data gap — other guides describe Wakka's Spin as a real, hard-to-block attack, contradicting x0; Tidus EXP (khwiki/khguides 2, destinyislands 1); whether TPs survive a loss (one summary says yes, unconfirmed); whether singles ever drop an item (strongly implied no, never positively ruled out).

---

## 5. CONTRADICTIONS AND GAPS — where the builder is inventing

### Resolved by majority in this spec (know that a side was picked)
1. **⚠ BIGGEST ONE — where the Cove door is.** The Seashore record moved it from a previous "west end by the dock" to the **EAST end past the shack**, on the strength of (a) a fan port of the actual PS2 geometry showing exactly one hinged wooden door with a plank awning and split-rail fence, at the east end past the shack, with the whole stretch from the west stacks to the shack unbroken cliff/tree/waterfall; (b) khwiki's Re:coded article ("entered through the wooden door north from the smaller island"); (c) BradyGames' "from Kairi's position, run down the ramp and under the pier to find the first Log", since every source puts Log #1 under the **bridge** at the east end. The Cove record independently agrees from the official concept painting (door on the same headland as the bridge and the coconut palms, a short distance right of the shack). **Adopted: east end, T1 shelf, beside the bridge foot.** The dissent: nobody could see the BradyGames area map that a previous pass used to place a transition marker near the west end, and the islet record argues the door is at the *opposite* end from everything else. **Build the door east, but keep the transition data-driven so it can be moved.**
2. **⚠ WHICH END IS WHICH — the mirror problem.** The Seashore/Shack/Secret Place records all put the shack + waterfall + Secret Place + bridge + islet cluster at the **east** end and Tidus's lookout + jetty + dock at the **west** end, far apart. The islet record argues from "climb the ladder to the left of the stairs up the big tree", "next to the Seaside Shack is a ramp and a path leading to a ladder", and "cross the bridge again and go left until you see a not-so-safe wooden ladder" that **Tidus's rope platform is adjacent to the shack**, i.e. the whole cluster (dock, Tidus, waterfall, Secret Place, shack, bridge, islet) is at ONE end and the Cove door is at the other. **This is a genuinely unresolved layout conflict, not a labelling quibble.** This spec adopts the strung-out version (4 of 5 records, plus the geometry rip's "unbroken cliff between tree and shack"). Consequence if wrong: Tidus's Day-1 platform and the Rope move from the far west to just west of the shack, and the beach is much shorter. **Keep NPC spawn points and item spawn points in a data table, not hard-coded.**
3. **Secret Place mouth — elevation.** Previous record had it at sand level on the back edge of the beach. **Five independent walkthroughs put it one terrace UP** ("jump up to the ledge right above the waterfall"; "up the first ramp, jump up to the second ledge"; "at the base of the big tree by the waterfall"; "a cave hidden between the large tree and the pool"; "the opening next to the waterfall"). **Adopted: raised T2, ramp + one-ledge hop.** Still unresolved: which *side* of the falls — text says variously "beside", "to the left of these waterfalls" (implying the tree side), "behind Wakka", and "the ledge right above". The geometry rip shows **no** ground-level opening anywhere between the tree and the shack, but does show a wooden-lintelled doorway with a plank-lined tunnel in a rock knoll on the terrace **behind/above the shack**. Spec places it there. It may instead be tucked on the tree side, or higher above the falls.
4. **"Behind the waterfall" is wrong.** Both wiki *articles* say the cave is hidden behind the falls; no *walkthrough* has you pass through water — gamerguides gives "run under the waterfall to obtain Drinking Water **then** head into the Secret Place" as two separate actions. **The falls are adjacent scenery, not a curtain door.**
5. **Which east-end opening is which.** Two openings sit within a few metres: a wooden-framed doorway in natural grey rock (elevated, closer to the shack and the falls) and a canopied hinged wooden door in a tall timber-clad wall (further east, split-rail fence, by the bridge foot). Assignment (rock = Secret Place, timber = Cove) is inference — guides always call Kairi's door "the wooden door" and the Secret Place a cave. **Could be swapped.** The second opening in concept art may simply be decoration.
6. **Wakka's Day-1 spot.** khwiki + destinyislands: "on the beach". khguides: "near the wooden shack by the waterfall". Both on the sand, different ends. Spec: open sand, centre-east.
7. **Selphie's Day-2 spot.** destinyislands "on the beach next to the dock" (adopted) vs khguides "the wooden pier along the beach" vs thegamer "on a bridge" (weakest source, rejected).
8. **Coconut drop colour.** khwiki says **green** ("brown ones will not be collected"), a JP guide says yellowish-green, the Seashore record says **yellow**. **All agree brown does not count.** Pick one ripe colour and make brown a dud.
9. **Selphie's dock — roofed or not.** The ripped geometry shows a **flat unroofed** plank platform on stilts in front of the tree, plus a separate low plank walkway out to the west stacks. Official KH1 artwork instead shows a clearly **ROOFED pier** jutting seaward west of the tree. Spec: unroofed central platform. Also unconfirmed whether the central platform and the west jetty are one continuous structure.

### Flat contradictions the builder must simply choose on
10. **⚠ IS THE SEASIDE SHACK ITS OWN MAP?** Two datamined lists disagree and **each researcher trusted a different one**.
    - *TCRF-derived* (Seashore record, second-hand via a search index; tcrf.net itself served a prompt-injection page and was correctly discarded): 12 Destiny Islands rooms **including 01 Seaside Shack (Day), 04 Seaside Shack (Sunset, Unused), 06 Seaside Shack (Night of Fate)**.
    - *OpenKH-derived* (Cove, Secret Place and islet records; fetched twice, identical): exactly **eight** maps — `di00_01` Seashore, `di00_02` Cove, `di00_03` Seashore (Sunset), `di00_04` Seashore (Night), `di00_05` Secret Place (Night), `di00_06` Sora's Room, `di00_07` Secret Place, `di00_08` Secret Place (Past) — **no Seaside Shack entry at all**, and the shack's Save Point is usable on the final night when no shack-night map would exist.
    - The Shack record's own header offers a **third**, unsourced numbering (di02 / di05 / di07 for the shack, di08 / di11 for the Secret Place) that matches neither list and contradicts the table it itself quotes.
    - **Recommendation for the demake:** it does not matter much — build the shack interior as a **tiny separate screen** (cheap in a tile engine, gives the save room its own tileset), but with **both doors returning to the Seashore**, and treat the transition as a fast fade, not a full load.
    - **What all lists agree on and this spec relies on:** there is **NO separate Tree House map** and **NO separate Small Island / islet map**. The tree-house alcove, the east terrace, the bridge and the paopu islet are all inside the Seashore map. khwiki has no Destiny Islands "Tree House" article. The KH speedrun wiki states "the entirety of Day I can be completed within one room."
11. **⚠ THE DARKSIDE ARENA.** Three incompatible accounts. (a) khwiki names the storm-night fragment the **Storm-tossed Island** (嵐の浮島, "an area that broke off from the rest of the islands"). (b) khwiki's walkthrough has the Secret Place door propel Sora straight out into a Darkside fight, implying the ordinary beach. (c) The TCRF list gives a **distinct Night-of-Fate Seashore variant (room 08, "Cutscene, Boss")**; the Secret Place record instead says the destroyed-island arenas live in the **End of the World** file set (`ew00_15` "Homecoming (Boss) — Destroyed Islands", `ew00_16` "Seashore", `ew00_19` "Homecoming (Purple Dark Water)"). **Do not assume the fight reuses the intact walkable beach unchanged.** Recommendation: build a dedicated small night arena — a broken beach fragment over dark water — with no walkable exits, entered by cutscene only. **Darkside: 300 HP.**
12. **The Cove "see-saw".** "Use the see-saw to return to Kairi" appears in exactly **one** source (the KHFMHD speedrun Day II route) and in no guide, wiki or JP page. Position and function undocumented; may be a runner's nickname for something else (a tilting plank definitely exists somewhere on the island). **Nice-to-have only** — the wooden steps already serve the same purpose.
13. **The Cove "rowboat".** The race record lists a rowboat as a landmark on the ground-bypass stretch; **no other record mentions any boat in the Cove**, and the raft sits in that stretch. Probably the raft. **Do not build a separate rowboat.**
14. **The race record's Cove geometry is partly wrong** and must not be built from: it places "the WATERFALL, the SECRET PLACE cave entrance beside/behind the waterfall" in the Cove. Those are on the **Seashore**. The Cove's water feature is the small **spring / water outlet** in the south cliff between deck sections 1 and 2. The race record's "pushable rock next to the raft hides a Mushroom" **is** correct.
15. **Third Day-2 Mushroom location.** gorillawiki says "in the room where the Cloth was" (the tree house). The speedrun route, destinyislands.com, GameRant and kingdom-hearts.net all put it in the **Secret Place**, beside the door. **Use the Secret Place.** gorillawiki is demonstrably unreliable here.
16. **Cove-side geometry of the tunnel mouth.** khwiki places the Cove end of the tunnel next to the little star-lamp tree at the **midpoint** of the obstacle course; the Cove record (from artwork + speedrun + every walkthrough) places it at the **door/entrance end** where Riku stands. **Use the entrance end.** The "next to the star tree" claim is the same unsupported khwiki sentence that generates the phantom second exit.
17. **Where the wooden crate sits in the Cove.** StrategyWiki "near the stairs", KHGuides "near the zip line", BradyGames "nearby" (captioned next to the boulder). Spec: on the embankment near the steps and tower base. It may instead be on the far beach by the boulder.
18. **Which deck section holds the trap plank.** Three sections confirmed by the concept painting; GamerGuides reportedly says the second; the speedrun only says "jump before the plank". **"Second section" is unverified.**

### Nobody could confirm — pure invention zones
19. **The storm-night door at the Secret Place mouth: does it gate?** The door's existence is corroborated three ways ("a large, strangely familiar door at the entrance"; "the door that has appeared in front of the cave entrance"; "the door from Sora's dream sequence"). Whether Sora **tries and fails** to open it before getting the Keyblade is **unconfirmed** — one search summary asserts it, unreadable directly. **Do not build a hard lock without checking video.** Also unknown whether it is a distinct object or the same arch-topped design as the interior door.
20. **Whether the Secret Place can be entered on Day 1 at all**, or on the storm night before the Riku/Keyblade scene. No walkthrough describes either. `di00_08` "Secret Place (Past)" is a third dressed variant of the same room whose purpose and dressing differences (fewer drawings? completed paopu drawing?) are **entirely unknown** — it adds no exits.
21. **Secret Place tunnel length and whether it bends, and which way.** Only "run through the tunnel" / "the end of the path". Speedrunners note the camera misbehaves in there, hinting at a bend, proving nothing. "Runs north then bends west" was never sourced. Chamber scale ~8–12 m across is an eyeball estimate. Any ceiling skylight/light shaft, and any standing water: unknown (art shows dim ambient light, dry earth floor).
22. **Which chalk drawings are on which wall**, and whether the paopu drawing is on the chamber wall itself or on a free-standing low rock beside the door. **Unknown — place freely.** The door's exact angle to the tunnel mouth ("dead ahead" vs "a quarter turn") is also unsettled; spec uses dead ahead.
23. **All Secret Place set-dressing** — tree roots through the ceiling, loose white boulders, reddish-brown floor, wall-stone colour — is read off concept art and screenshots. **No text source confirms any of it.**
24. **Seaside Shack interior furnishings.** Six English guides, two JP guides and the frame-level speedrun route mention **only** "a Save Point off to the right" and "go up the stairs". No barrels, nets, crates, windows, hammock or upper floor in any source. **Treat as bare** — anything added is invention. Dimensions unmeasured (a small square room crossed in 2–3 seconds; upper door ~one storey up). Which wall the upper door pierces (rear/inland vs east/toward the bridge) is inferred; what *is* confirmed is that the climbable trees stand directly in front of it. Whether the lower opening has a door leaf or is a framed opening: unknown.
25. **The bridge to the islet:** side railings, support type (single A-frame trestle vs multiple posts), whether the deck is level or rises toward the mainland, and the height of the mainland end above the beach — **all unconfirmed art readings.** The one askew plank used as a Night-of-Fate cutscene trigger is confirmed.
26. **The paopu tree's bearing and rooting point.** Every source agrees it is a crooked palm whose trunk is sittable and hangs over water; **none says which arc of the rim it grows from.** Spec puts it on the ocean-facing rim a few steps round from the bridge landing. "Leaning back toward the beach" is equally consistent. **Build nothing load-bearing off its bearing.** Also unknown whether visible paopu fruit hang on the tree during gameplay or only appear in Riku's hand in the cutscene.
27. **Islet dimensions, palm count, shrub-ring continuity, any "V-shaped notch" in the rim, loose flat stones near the tree base** — all art readings. "25–30 m across, about eight palms" is a sketch, not a measurement. The hard constraint is: **the plateau must fit a 3-opponent brawl.**
28. **Log #2's exact spot** — "toward the back of the small island", "near some trees in the area where Riku is". More precise than that is invention. **Log #1 is confirmed** on the mainland sand/shallows directly under the bridge, and may need a short swim.
29. **Whether the T2 east terrace connects continuously through to Tidus's rope platform / the elevated boardwalk.** Never stated. Spec does **not** connect them. If they do connect, the islet gains a fourth approach and the terrace becomes a junction rather than a dead end. Worth checking footage.
30. **Whether the Day-2 3-on-1 is fought on the islet** or whether talking to Tidus there warps the fight elsewhere. Sources only say you talk to him "across the bridge". Spec: fight on the islet plateau.
31. **Whether Riku offers anything on the islet after the Day-1 duel.** One source says flatly "Riku can only be fought on day one". The claim that he later switches to offering the race is **unsupported** — the race is a Day-2 Cove event.
32. **Exactly where Sora is lying when he wakes** — mid-beach near the water's edge is inference from cutscene framing, stated nowhere.
33. **Kairi's exact Day-1 stance** — "in front of" vs "behind" the Cove door varies by guide; whether she stands on the T1 shelf at the door or a step below on the ramp is unconfirmed.
34. **Cove zip line's lower end.** Confirmed that it runs tower-top → "another smaller wooden construction" and that you can jump from there into the palms. **No source describes that structure**, and it is not identifiable in the concept painting. Its height, length and position over the far beach are guesses.
35. **Cove specifics unknown:** exact Hidden Mickey spot (Seashore/Secret Place Mickey placements are also contradictory — GameRant says one is on the cave floor, Fandom says the easy one is on the Cove ground; **do not rely on a cave-floor Mickey**); the exact number of coconut palms; whether the two flat plank pallets in the inlet are standable geometry or scenery; whether the terrace-front retaining structure is specifically a vertical-plank fence (only the one-storey drop is confirmed by routing).
36. **Whether the falls are a single or twin cascade.** The Seashore record says **twin** (two thin streams from square notches, pink lotus round the pool); every other source says only "the waterfall". Spec uses twin.
37. **Coconut tree eligibility.** JP 攻略の虎 says coconuts drop randomly from **all** the FAT / round-crowned trees on the island, and khwiki lists Coconut x2 under Seashore, so the Seashore/islet palms **do** drop them. **Which specific trees count is unconfirmed** — make trunk fatness the flag.
38. **Version differences** between original PS2 KH1, Final Mix and 1.5 HD ReMIX: all sources describe them interchangeably; none were explicitly ruled out. Only confirmed delta: Pretty Stone sells for 30 munny original / 100 munny FM & 1.5.
39. **Source-hygiene warning, worth propagating:** two independent researchers report that **tcrf.net served a prompt-injection / hostile-instruction page** through this proxy, and both correctly discarded it. Anything traceable only to TCRF (notably the 12-room list, and therefore the "Seaside Shack is its own map" and "unused sunset shack" claims) is second-hand via search-index extracts. Separately, **several dark framed rectangles on the cliff and the west stack's plank wall in the geometry rip are the mod author's own signboards, not game exits** — the rip is not a pristine record of PS2 geometry and other small details in it may have been altered. GameFAQs, StrategyWiki, gamerguides, supercheats, trueachievements, khdatabase, Neoseeker, IGN, speedrun.com and all of Fandom returned 403/402/Cloudflare, and web.archive.org is blocked outright — so gaps above are gaps in **coverage**, not evidence of absence.

---

## 6. ITEM / COLLECTIBLE TABLE (for spawn-point data)

**Day 1 — raft materials, hand in to Kairi at the Cove door → Hi-Potion (fewer hints asked = better reward):**
| Item | Area | Spot |
|---|---|---|
| Log #1 | Seashore | On the sand at the east end, **directly under the bridge**. From Kairi's door, run down the ramp and pass under the bridge. May need a short swim |
| Log #2 | Seashore (paopu islet) | Toward the back of the islet among the trees, near where Riku sits |
| Cloth | Seashore (tree-house alcove, T3) | Hanging on the wall to your **right** as you enter the trunk opening |
| Rope | Seashore (lookout deck, T3, far west) | In a corner of the deck, behind Tidus |

**Day 2 — provisions list + Empty Bottle from Kairi at the Cove raft → Hi-Potion on completion:**
| Item | Count | Area | Spot |
|---|---|---|---|
| Seagull Egg | 1 | Seashore (T2 east terrace) | Top of a palm beside the shack's upper door. Climb the **skinny** tree alongside (~75% up) and jump across to the palm that holds it |
| Mushroom | 3 | Secret Place ×1, Cove ×2 | Secret Place: chamber floor, right of the wooden door (picking it up fires the Ansem cutscene). Cove: behind the pushable **boulder** by the raft; and in the **bushes left of the watchtower ladder** at its base |
| Coconut | 2 | Cove ×many, paopu islet | Strike **fat / round-crowned** palms — the Cove grove and the islet palms. Ripe (yellow/green) ones count; **brown ones do not.** Each tree drops many |
| Fish | 3 | Seashore (swim) | In the shallows around the dock and in front of the waterfall |
| Drinking Water | 1 | Seashore **or** Cove | Walk into the **twin waterfall** on the beach, **or** into the **spring** in the Cove's south cliff between deck sections 1 and 2. Requires the Empty Bottle |

**Other:**
| Item | Area | Spot |
|---|---|---|
| **Protect Chain** (chest — Sora's first accessory) | Cove | High square alcove in the south cliff above the embankment. **Carry** the crate (do not throw), set it against the wall beneath, jump off it into the alcove |
| Potion | Seashore (islet) | Per Riku duel win, Day 1, repeatable |
| Potion | Seashore (islet, Day 2) | Per 3-on-1 win, repeatable — the island's only farmable drop |
| Pretty Stone | Cove | Per race win, repeatable to 99. 30 munny original / 100 munny FM & 1.5 |
| **Save Point** | Seaside Shack interior only | Just inside the lower door on the right. The island's **only** save point; usable on all three days |