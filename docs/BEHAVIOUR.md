# Behavioural specification

Every tuning decision in the SNES build, written down. The assembly is
disposable; this is not. It was extracted from
`platform/snes/` at commit `b8f2b68`, tagged `snes-final`, and is the authority
for any port — if the DS build disagrees with a number here, the DS build is
wrong.

> **Audited, and it was wrong in places.** An independent pass checked this
> document against the assembly and found 41 errors and omissions. The outright
> errors are corrected below; the rest, with citations, is in
> `docs/BEHAVIOUR-AUDIT.md`. **Read both.** Five corrections were re-verified by
> hand and are marked **[corrected]** where they appear.

**Units.** Positions and velocities are **Q12.4** fixed point: 16 pixels of
world per unit of 16, so `256` means 16 px and `1` means 1/16 px. Every range
below is Q12.4 unless it says frames or pixels. Times are **frames at 60 Hz**,
and both target platforms run at 60 Hz, so they transfer unchanged.

**Why fixed point, and why it stays.** The 65816 has no divide and no floating
point, so the whole engine is integer. The DS has no FPU either. The numbers
below are therefore not an artefact of the old platform being weak — they are
the actual design, and re-deriving them in floats would lose the exactness that
makes two implementations comparable.

---

## 1. Geometry and collision

| | |
| --- | --- |
| Tile | 16×16 px, square. `world = tile * 16`, `tile = world >> 4` |
| Map | 32 wide × 16 tall, i.e. 512×256 px |
| Screen | 256×224 (SNES). DS is 256×192 — **see §9** |
| Height | Per-tile, in 8 px steps, 0–3 |
| `MAX_STEP` | 1. A move is refused unless the destination height is within one step of the origin's |

A tile at height *h* is **painted `8h` px above its cell** with a face filling
the gap back down, so it covers `8h` px of the cell to its **north**. Map
authoring depends on this: the cell above any raised structure is always water,
rock, or more of the same structure. Actors cache the height they stepped onto
and their sprite is lifted `8 * height`; geometry and depth sorting still see one
flat plane.

**Movement resolution order** — `TryMoveActor`, and the order is load-bearing:

1. Try the full move on **both** axes. If the destination tile is walkable and
   within one step, commit both and store the new height.
2. Else try **X only** (candidate X, current Y). If that passes, commit X.
3. Else try **Y only** (current X, candidate Y). If that passes, commit Y.
4. Else do not move.

That is what produces wall sliding, and steps 2-and-3 in that order mean a
diagonal into a corner resolves horizontally. Reversing them changes how the
island's walkways feel.

**Off-map rejection** is one unsigned compare per axis: a negative coordinate
shifts to a large unsigned value, so `>= MAP_W` catches both edges at once.

---

## 2. Sora

| | Value |
| --- | --- |
| Max HP | 20 |
| Walk speed | 24 (1.5 px/frame) on both axes; a diagonal is the pair scaled ~1/√2 |
| Attack length | 18 frames, no movement during it |
| Attack active frame | timer == 12, i.e. **frame 6 of 18** — one active frame only |
| Attack reach | 272 × 272 half-extents, centred on a point one facing-offset ahead of him |
| Hurt stun | 20 frames |
| Death dim | 50 frames, brightness ramping down but clamped at 3 of 15 so GAME OVER stays readable |
| Knockback | the attacker's facing velocity × 2 |
| Hit stop | 3 frames on a connect, 4 on a kill |

Eight facings. Only N, NE, E, SE, S are drawn; W, SW, NW reuse the eastern art
mirrored. Facing order is `S, SE, E, NE, N, NW, W, SW` = 0–7, and the velocity
tables are indexed by it directly:

```
dirVelX:  0,  17,  24,  17,   0, -17, -24, -17
dirVelY: 24,  17,   0, -17, -24, -17,   0,  17
```

The swing arc is **deliberately generous**: it is centred just off his body
rather than at arm's length, so an enemy pressed against him is still inside it.
Do not "fix" this by moving the arc outward.

Interaction is split by *how* a thing is reached, and the actor type alone
decides which:

| Range | Value | Used by |
| --- | --- | --- |
| `TALK` | 384 × 384 | press A near a person |
| `PICK` | 224 × 224 | walked into — the raft materials, mushrooms, the egg, the bottle. **Not coconuts**, see below |
| `SWING` | 544 × 544 | answered with the Keyblade — fish, palms still carrying coconuts |
| `PROP` | 416 × 416 | examined — the chalk drawings, the door in the Secret Place |
| `TOUCH` | **320 × 160** | a Heartless reaching Sora — **[corrected]**, see below |
| `TOUCH` | 160 × 160 | Darkside's fist only. The Guard Armor's uses 448 × 384 |
| `REACH` | 448 × 448 | the three dream weapons in the Dive |
| attack centre | 256 cardinal / 176 diagonal | the offset the swing arc is centred at, ahead of his facing |

**[corrected] The Heartless touch box is asymmetric and twice as wide as it
looks.** `HeartlessTouchTest` halves `abs(dx)` with a bare `lsr a` before
comparing it to `TOUCH_X`, so the effective box is 320 × 160. Worse, a Shadow
whose facing bucket comes back "stand still" — both axes inside the 208 deadzone
— skips its move *and* its touch test entirely, so it can never damage Sora
point-blank or from directly north or south. The only geometry that connects is
`208 <= abs(dx) <= 319` with `abs(dy) <= 159`. A Shadow closing vertically parks
at `abs(dy) ~ 207` and stands there. Reproduce that exactly or the night plays
like a different game.

**[corrected] There is no coconut you can walk into.** `ACT_COCONUT = 23` sits
inside the walk-into range that `TryPickup` tests (`ACT_LOG..ACT_BOTTLE`,
island.s:298-312), so one *would* be collectable — but no coconut actor is ever
spawned. `ACT_COCONUT` appears exactly once in the whole tree, at its own
definition (game.inc:194). The only source of `IT_NUT` is `SwingAt` turning an
`ACT_PALMC` into an `ACT_PALM` (island.s:389-394), and the spawn table holds
exactly two `ACT_PALMC` against `NEED_NUT = 2` (world.s:2367-2368,
game.inc:479). The constant is nonetheless **load-bearing and must be kept**:
`itemCount` is indexed by `actType - ACT_LOG`, so `ACT_COCONUT` is what holds
slot 4 open for `IT_NUT`. A port that "tidies up" the unused type renumbers
every collectable above it. See audit finding 55.

---

## 3. Shadow Heartless

| | Value |
| --- | --- |
| HP | 3 |
| Speed | half Sora's, per axis (`dirVel` >> 1) |
| AI deadzone | 208 × 208 — inside it that axis contributes nothing |
| Animation | 4 cels, 10 frames each |
| Recoil | 14 frames, coasting on knockback velocity halved each frame |

The AI is a 3×3 bucket lookup, not a vector: each axis is classified as
negative / inside the deadzone / positive against `HEART_DEAD`, and
`index = vertical * 3 + horizontal` selects one of nine facings (the centre
being "stand still"). That gives eight-way movement with a dead centre and no
arithmetic beyond two compares — and it is why they approach in visibly
straight lines rather than curving in.

Before the Keyblade exists (`keyGot == 0`) they **cannot be hurt and cannot
hurt Sora**. That is a deliberate departure from the source: in the original
they can hurt you during the night, but with no way to answer them a death loop
in a corridor with one exit is not tension.

> **Known bug, fixed late:** the `keyGot` test was a 16-bit load of a one-byte
> variable, so it read the adjacent flag as part of the value. Once the player
> had been told the wooden sword was useless, the wooden sword started working.
> Any port must keep these two flags logically separate.

---

## 4. Darkside — the Dive boss

64×64, drawn as four 32×32 quadrants. **It never walks.** Everything it does is
dodged by moving, which is the only thing a stationary boss can ask of the
player.

| | Value |
| --- | --- |
| HP | 36 |
| Hurt box | 576 × 640 half-extents, centred on its feet |
| Flinch | 10 frames, flashing |
| Rest between attacks | 56 frames |

States and timings:

| State | Wind-up | Active | Then |
| --- | --- | --- | --- |
| Fist slam | 44 | 22 on the ground, vulnerable | rest |
| Dark orbs | 40 (chest gathers) | fires 3, then 30 | rest |
| Arm sweep | 26 | 16 | rest |

- **[corrected] The slam marks its target on ENTRY to the wind-up**, not at the
  end: `AimAtPlayer` runs 44 frames before impact, so the mark is stale for the
  whole telegraph. This inverts the dodge — the player escapes by leaving the
  spot they occupied when the wind-up *started*, not by outrunning a tracking
  fist. The Guard Armor is the same, 40 frames early. Getting this backwards
  makes both fights feel wrong in a way that is hard to name, which is why it is
  called out here.
- Anyone inside `TOUCH` of the mark is hit, and **a Shadow crawls out of the
  impact**. Those crater Shadows are uncapped: `SHADOW_MAX` is enforced only by
  the night's own spawner, which does not run during the boss.
- **[corrected] Orbs** live 110 frames and travel at `dirVel * 2` — 48 on a
  cardinal, 34 on a diagonal. `ORB_SPEED = 40` is a dead constant.
- The three orbs are fired at `facing-1`, `facing`, `facing+1` mod 8, aimed from
  a point 40 px above the boss's feet.
- **Flinch is invulnerability.** `HurtBoss` refuses damage while the flinch timer
  runs, so a boss takes at most 1 damage per 11 frames, which floors both fights
  at roughly 684 frames. A Shadow's 14-frame recoil is *not* invulnerable.
- Both bosses vanish the instant they hit 0 HP. There is no death state; the
  scene scripts detect the win by counting the type and finding none.
- The **sweep** is the punish for standing underneath, and it is checked
  *before* the normal alternation: 704 × 448, wide and shallow, 44 px either
  side and 28 px out from its feet. Its wind-up is shorter than the fist's on
  purpose.
- Otherwise the two ranged attacks strictly **alternate** (a one-bit flip), so
  the fight is learnable.

The hurt box is deeper than the sweep is long. That is required, not incidental:
if the sweep reached further than the player could hit from, there would be
nowhere to stand that could reach it without being swept.

---

## 5. The Guard Armor — the Traverse Town boss

64×64, drawn the same way, with its **two hands as separate ordinary actors** so
the depth sort draws them in front of its own torso. Nothing drives the hands;
the body places them every frame from its own state.

| | Value |
| --- | --- |
| HP | 36 — the same length of fight as Darkside, so the gauge is the same 18 cells |
| Hurt box | 640 × 704 half-extents, centred on its feet |
| Descent | 40 frames, height ramping as `timer >> 2` from 10 steps, screen shaking ±3 |
| Tracking | **sideways only**, at speed 20 (Sora walks 24) |
| Stop distance | 512 (32 px) — inside that it does not jitter |
| Walk phase | 96 frames before it strikes |
| Slam | 40 wind-up, 20 on the ground, then 50 rest |
| Hand station | 640 out from the body, 448 up the torso |
| Wound-up fist | 10 height steps above the mark |
| Hand hit box | 448 × 384 around the mark |

**Why it only tracks sideways** — this is the single most important note in this
file, because it looks like a limitation and is a fix. A sprite this tall is
anchored by its feet, so 64 px of armour is drawn *above* wherever it stands.
Let it walk north to meet the player and its whole body is drawn over the top of
them, and the fight stops reading as a fight. Holding station at the head of the
square and following left and right keeps it a wall — which is what it should
look like — and the vertical work belongs to the hands, which land wherever you
are.

**On the DS this constraint may lift.** With a 3D camera and billboarded
characters, depth is real rather than implied by draw order, so the armour
*could* be allowed to close on the player from any side. If it is, the fight has
to be re-tuned: the stop distance, the hand reach and the walk phase were all
chosen against a boss that cannot approach vertically.

For the same reason, `RaiseArmor` **stands the player back in the middle of the
square** when the armour lands: the way in is exactly two tiles from where it
comes down, and two tiles is inside it.

---

## 6. Scene state machines

Each is a single byte advanced by one script. The stages are the specification
of the opening's pacing; the frame counts beside them are the only timings.

### The Dive (`diveStage`)

```
INTRO -> PICK -> DROP -> SHATTER -> S2_INTRO -> S2_FIGHT -> SHATTER2
      -> S3_INTRO -> BOSS -> DONE -> FALL -> FADE -> ARRIVED
```

| | Frames |
| --- | --- |
| Platform shatter | 96 — MOSAIC coarsening the ground while brightness falls and the screen shakes |
| Fall | 170 of descent before the light takes over |
| Fade | 64, and **the scene swaps at the halfway point** |
| Rising motes | 40 frames each, 96 Q12.4 upward per frame (6 px) |

### Destiny Islands (`questState`)

```
IDLE -> ACTIVE -> DONE -> DAYOUT -> DAYIN            (day one into day two)
     -> RACE_SET -> RACE_RUN -> RACE_OVER -> NAMING -> NAMED -> DUSK
```

- Day change: 30 frames out, 30 frames in, cast rebuilt at black. **[corrected]**
  Both halves are 31 frames — every timer in the game tests before it decrements,
  so a constant of N runs N+1 frames; see audit finding 45. The two halves also do
  not mirror each other: DayOut dims 15→0 reading the timer *before* the step and
  DayIn brightens 0→15 reading it *after*. And the day change is dispatched above
  the dialogue gate, so it runs through an open box — audit finding 46.
- Race countdown: 192 frames, 64 to a number — 193 in practice, and the digit is
  `(timer + 63) >> 6`, which is not an even third. Audit findings 45 and 51.
- Riku runs a fixed 20-waypoint course at 17/17 per axis — a little slower than
  a clean line, so the race is winnable, and quick enough to punish wandering.
  A waypoint counts as reached within 40.
- Day two's fish drift at 16 per frame, reversing on a timer bit. They do not
  race.

### The night the island falls (`nightStage`)

```
INTRO -> SEEK -> RIKU -> KEY -> KAIRI -> DOOR -> TEAR -> BOSS -> END -> OVER
```

| | Frames |
| --- | --- |
| Lightning flash | 8, as bright / out / brighter / out — two strikes in one flash |
| Between flashes | 150 + random 0–127 |
| Shadows | 6 alive at once, one every 70 frames, from 10 spawn spots |
| Column of darkness holds | 96 |
| Island tearing apart | 120 |
| Closing fade | 62, subtractive to full |

Two beats depend on the colour-math unit being **shadowed in RAM** rather than
written directly, because a flash and a fade both want it in the same frame. On
any platform, the rule is: *effects that contend for one global register must be
arbitrated in one place per frame, not written where they are decided.*

A flash is skipped from `END` onward — a strike resetting the unit under the
closing fade undid the fade every time one landed.

### Traverse Town (`townStage`)

```
ARRIVE -> LOOK -> SECOND -> THIRD -> MEET -> BOSS -> WON -> OVER
```

`townStage` is **progress, not location.** The player may walk back through any
door already opened, and the town remembers where it had got to. Doors are gated
by "the stage the town must have reached", stored in the door table itself, so
the world is gated with no lock flags anywhere.

| | Value |
| --- | --- |
| Heartless wave | 8 arrive, 5 alive at once, one every 80 frames, 8 spawn spots |
| Way on opens | when the whole wave has been out **and** none is left |
| Door transition | 30 frames out, swap, 30 frames in |
| Pair falling | 70 frames of wait, then 24 of descent from 12 height steps |

A door fires on the **step onto** its tile, not on standing there, so a bolted
one says its line once instead of every frame the player leans on it. That needs
a remembered previous tile.

Spawn placement refuses a spot within 64 px of the player and retries in 12
frames — one arriving in your face reads as a bug rather than as a Heartless.

#### What advances `LOOK` → `SECOND`: talking to Cid, once

*Added after the fact, under §0.6 of the porting brief. The opening line of §6
above — "each is a single byte advanced by one script" — was true, and did not
say advanced by **what**. Neither this section nor `BEHAVIOUR-AUDIT.md` named
Cid anywhere, and his branch turned out to be the town's most load-bearing
transition. Derived from `platform/snes/src/town.s`, cited line by line below.*

`TalkTown` scans the actor table for a slot that is occupied, carries `AF_TALK`
and passes `NearPlayer` (`town.s:506-518`), then dispatches on actor **type**.
Townsman, townswoman, Donald and Goofy are branch targets; **Cid is the
fall-through** (`town.s:522-546`), and his is the only arm that does anything
besides speak — the other four are each `rep #$20` / `lda #.loword(script…)` /
`jmp Say` and nothing more (`town.s:552-571`). With the two `rep #$20` / `.a16`
width switches elided, Cid's arm is:

```
    lda townStage
    cmp #T_LOOK
    bne @cidAgain
    lda #T_SECOND
    sta townStage
    jsr HudUpdate
    lda #.loword(scriptCid)
    jmp Say
@cidAgain:
    lda #.loword(scriptCid2)
    jmp Say
```

(`town.s:537-551`.) Three things happen on **one frame, in this order**, and all
three are part of the transition rather than decoration around it:

1. `townStage` becomes `T_SECOND` (`town.s:540-541`).
2. `HudUpdate` runs (`town.s:542`). This is visible, not bookkeeping: `HudUpdate`
   reaches `DrawTown` for a town scene with no boss up (`hud.s:187`, `:214-217`),
   `DrawTown` calls `TownStageLabel` (`hud.s:415`), and `TownStageLabel` is a
   pure function of `townStage` (`town.s:1356-1374`) indexing `townLines`
   (`hud.s:582-589`). So the objective row changes from `FIND SOMEBODY AWAKE` to
   `THE SECOND DISTRICT` on the same frame the stage does. Separate the two and
   the HUD spends a frame describing a town that has already moved on.
3. `Say` opens a box on `scriptCid` (`town.s:545-546`). `Say` stores `txtPtr`
   — including its bank byte, since every line in the file shares one — and calls
   `TextOpen` in `TM_MESSAGE` mode, and does nothing else at all
   (`town.s:1339-1350`). **The box is opened here or not at all.**

The test is `cmp #T_LOOK` and not "later than `T_LOOK`", so inside the branch
`@cidAgain` is the arm for every stage that is not `T_LOOK`. **What that does not
mean is that a player can reach it at every stage**, and the difference is worth
spelling out, because the branch read on its own says the wrong thing about the
game and an earlier draft of this section said it. `TalkTown` has exactly one
caller and that caller gates it. `TownUpdate` returns while a box is up
(`town.s:212-214`), then sends `T_ARRIVE` to `Woke`, `T_MEET` to `Meet`, `T_BOSS`
to `WatchArmor`, `T_WON` to `AfterArmor` and `T_OVER` to an immediate `rts`
(`town.s:217-232`), and only falls through to `jsr TalkTown` on the three stages
its own comment calls walkable — "T_LOOK, T_SECOND, T_THIRD: the town is walkable
and the doors are live" (`town.s:234-240`). So **`@cidAgain` is reachable at
`T_SECOND` and `T_THIRD` and nowhere else.**

`T_ARRIVE` is out of reach twice over. `TownBegin` writes it and opens
`scriptWake` in the same routine (`town.s:60`, `:86-87`), so the stage never
exists without a box in front of it; and the first frame on which the box is gone
is the frame `TownUpdate` jumps to `Woke`, which writes `T_LOOK` before anything
else in the scene runs (`town.s:217-220`, `:254-255`).

That is what makes the equality safe rather than sloppy, and it is the part to
carry into a port. `scriptCid2` is "THE DOOR AT THE END OF THE ROW. IT'S OPEN
NOW." (`town.s:1479-1481`), and the door at the end of the row is `doorTable`
row 0, gated on `T_SECOND` (`town.s:1391`). At both stages where the line can be
said the gate has already been passed, so the frozen game never says it while the
door is still bolted. A port that keeps the equality test but drops the caller's
stage dispatch has therefore not reproduced the SNES — it has invented a Cid who
announces an open door on `T_ARRIVE`, the one stage the original never lets him
speak on, and the invented line is the more convincing kind of wrong because it
is a real string from the real ROM.

**Why this is the town's load-bearing transition.** `town.s:541` is the **only**
write of `T_SECOND` to `townStage` in the whole SNES source. The other seven
writes are `stz` for `T_ARRIVE` (`town.s:60`), `T_LOOK` from `Woke`
(`town.s:254-255`), `T_MEET` (`town.s:429-430`), `T_THIRD` (`town.s:680-681`),
`T_BOSS` (`town.s:725-726`), `T_WON` (`town.s:836-837`) and `T_OVER`
(`town.s:853-854`). The First District's only exit is `doorTable` row 0, gated on
`T_SECOND` (`town.s:1391`), and a door opens when the stage it wants is less than
or equal to `townStage` — `cmp townStage` / `beq @open` / `bcs @shut`, so a
requirement *greater* than the current stage is the only shut case
(`town.s:325-328`). Remove this one branch and the town stops at `T_LOOK` for
ever: `Woke` reaches `T_LOOK` and stops (`town.s:251-258`), the exit says its
bolted line, and the Second and Third Districts are unreachable in a town whose
own rule is that every district already opened stays reachable.

**For a port.** The stage write, the HUD rebuild and the line are one atomic
beat, and a protocol that carries one action per step has to carry all three
anyway. Whichever of the three the protocol makes the "action", the other two
ride along and the consumer must honour them. Dropping the line loses more than
the line: with no box opened, whatever flag the port uses for "a conversation is
on screen" stays false, so the player acts on the frames the original spent
reading and every input after that lands early.

---

## 7. Camera

Centre on the player, then clamp per axis to scene bounds:

```
camX = clamp(playerX_px - 128, camLoX, camHiX)
camY = clamp(playerY_px - 112, camLoY, camHiY)
```

**There is no easing.** The camera is a hard snap every frame. Equal low and
high bounds mean a pinned camera, which is how the Stations of Awakening, the
island fragment and the shop interiors keep the void around them off screen.

| Scene | Bounds |
| --- | --- |
| Island, night, the three districts | full 512×256, so 0–256 and 0–32 |
| Stations of Awakening | pinned at 128, 16 |
| The island fragment | pinned at 128, 24 — lower, to keep Darkside's head clear of the HUD |

Screen shake is a signed byte added to the horizontal scroll, normally zero,
alternating sign on `frameCount & 2` — a **4-frame period**. **[corrected]** The
amplitudes are: the Dive's platform shatter ±2, the night's island tearing ±3,
the Guard Armor landing ±3, Donald and Goofy landing ±2.

**This is the part most likely to need new work on the DS.** A hard snap is fine
for a locked overhead view; an orbiting camera wants easing, and easing wants a
tunable that does not exist yet.

---

## 8. Presentation rules that are really design

- **Depth is Y-sort, nothing else.** Actors are insertion-sorted by world Y
  descending, frontmost first. Further down the screen means nearer. No
  per-object layer authoring anywhere in the game.
- Big bosses are emitted **after** every sorted actor, so they always land
  behind them. Correct nearly always, because the player fights at their feet.
- **Nothing walkable in the top 24 px of any map.** The HUD owns it.
- Ground shadows are a separate pass drawn after all actors, so they occupy
  higher slots and render behind everything.

---

## 9. What a 256×192 screen changes

The SNES build assumes 224 lines. The DS has 192, so the camera centres on
`playerY - 96` rather than `- 112`, and the vertical clamp becomes
`0 .. WORLD_H - 192` = 0–64 rather than 0–32.

That is not just an offset: **32 more rows of the map become reachable by the
camera**, so the maps show more of themselves and the "nothing walkable in the
top rows" margin needs re-checking against the new HUD, which is on the other
screen entirely and may free those rows completely.

`tools/check_map.py` is the place to encode the new margin, since it already
flood-fills every map against every spawn point.

---

## 10. The trace oracle

Both platforms run at 60 Hz, every timing above is in frames, and the spawn
tables are identical. That makes the SNES build a **behavioural oracle** rather
than a keepsake:

1. Drive both with the same scripted input.
2. Dump a per-frame state trace — player position, actor positions and states,
   stage bytes, HP.
3. Diff.

Any divergence is either a port bug or a deliberate change, and if it is
deliberate it belongs in this file. The SNES side already has the harness for
step 1 (`tools/playtest.sh`) and a way to read machine state for step 2 (save
states parse to WRAM, and the linker map gives every symbol's address).


---

## 11. Dead constants — do not implement these

Each has exactly one reference across all 21 sources: its own definition. A
reimplementer reading `game.inc` will otherwise wire them up and wonder why
nothing changes.

| Constant | What actually happens |
| --- | --- |
| `ORB_SPEED = 40` | orbs use `dirVel * 2` |
| `LINE_X` / `LINE_Y = 448` | the race reuses `TAG_X`/`TAG_Y` for both legs |
| `KNOCKBACK = 3` | both knockback sites use `asl a`, i.e. × 2 |
| `AF_SOLID = $04` | no actor blocks movement; nothing tests this bit |
| `shakeTimer`, `scriptWait` | dead RAM, written at most and never read |

One that belongs on the list by the letter of the rule and **must not be
removed**: `ACT_COCONUT = 23` also has exactly one reference, its own
definition, and no coconut actor is ever spawned — but `itemCount` is indexed by
`actType - ACT_LOG`, so deleting it renumbers `IT_EGG` and `IT_WATER`. Keep the
constant; do not write a spawner for it. See §2.

Two further things that look like systems and are not: **the dream weapon choice
has no mechanical consequence** — `weaponTaken` and `weaponGiven` are each
written once and read nowhere — and **the raft's name only selects a
confirmation line**. Do not invent stat effects for either.

## 12. Two latent bugs, inherited unless fixed — and one that is not latent

**[added] Darkside's slam cannot damage Sora**, because `DarksideSlam` reads
`tmp0`/`tmp1` back as the impact point after `SpawnActor` has let `SetActorZ`
overwrite them with the position shifted right by four. The fist misses by
roughly 3960 Q12.4 units against a 160 tolerance, wherever he stands. Only the
sweep and the orbs can hurt him. Found by the §M6 oracle, not by reading; see
audit finding 59. The two bugs below are unreachable, and this one is the boss's
main attack.



Both are unreachable in the SNES build and become reachable in a port that adds
a damage source where there is not one today.

1. **The Dive does not sweep Darkside's crater Shadows** (the night's script
   does). A survivor persists into the 170-frame fall, where `DamageSora` tests
   only for `ST_DEAD` and not `ST_FALL` — so a hit knocks Sora out of the fall
   and hands control back mid-air, or kills him and retries the boss.
2. **A death on the island restarts the scene without the day's item table**, so
   the remaining collectables are destroyed permanently and the day cannot be
   completed. Unreachable only because nothing damages Sora during the two days.

## 13. The dialogue system

The audit's last finding asked for this section: the dialogue box gates every
scene transition, so it is part of the specification and not presentation. This
was derived from `text.s` and then **verified against the running ROM**, which
is how the bug below was found.

A script is a byte stream. Anything **≥ 32 is a character**; anything below is a
control code. Only three codes exist:

| | | |
| --- | --- | --- |
| `SC_END` | `0` | the message is over |
| `SC_NL` | `1` | newline |
| `SC_PAGE` | `2` | *intended*: wait, clear the text area, keep going |

`txtState` runs `TS_CLOSED → TS_REVEAL → TS_WAIT → (TS_PROMPT) → TS_CLOSED`.

- **`TS_REVEAL`** emits one script byte, then waits `REVEAL_DELAY = 1` frames —
  so **one character every two frames**. A press of A or B instead calls
  `RevealAll`, which dumps the rest of the page in a single frame, bounded at 512
  iterations against a script with no terminator.
- **`txtHold`** blocks input until the button that opened the box is *released*.
  Without it the press that started a conversation would also dismiss it.
- **`TS_WAIT`** is entered by both `SC_END` and `SC_PAGE`. A press closes the box
  for `TM_MESSAGE`, or raises the selector for a prompt mode.
- **`TS_PROMPT`** tests Up, then Down, then the confirm, and **each returns** — so
  a frame carrying both Up and A moves the cursor and does not answer. `txtResult`
  is `choice + 1`, because 0 has to mean "not answered yet".
- **`TextClose` masks A and B out of `padPressed`.** This is a write to shared
  input state and scenes depend on it: without it, the press that closed a
  message immediately starts the next conversation.

Layout is 28 characters by 5 lines. `PutChar` wraps **mid-word** at column 28;
`NewLine` **clamps** at the last line rather than scrolling, so a page that runs
long overwrites its own bottom row. `asciiToTile` folds lower case onto upper and
maps everything unrecognised to `CH_BLANK`.

### `SC_PAGE` does not page. 63% of the dialogue is unreachable

There is **no path from `TS_WAIT` back to `TS_REVEAL`**. `SC_PAGE` sets
`TS_WAIT`; the next press reaches the `TM_MESSAGE` branch and calls `TextClose`.
`ClearTextArea` is called from `TextOpen` and from nowhere else.

So a page break ends the message, and **everything after the first `SC_PAGE` in
every script is dead**. 54 of the 70 scripts use it. Counting the characters:
1831 are reachable and **3213 are never shown**.

Verified, not deduced: booting the ROM shows the Dive's opening line, and one
press closes the box instead of advancing to "DON'T BE AFRAID."

This is the one place the audit was wrong — finding 41 recorded `SC_PAGE` as
"waits then clears", which is what `text.inc`'s comment claims and not what the
code does. Corrected as audit finding 42.

**The SNES build is not fixed.** It is frozen as the behavioural oracle and its
ROM must stay byte-identical, so the fix would have to be a deliberate,
separately-decided change. The DS port implements paging as intended and files
it as `docs/behaviour/divergences/005-sc-page.md`.
