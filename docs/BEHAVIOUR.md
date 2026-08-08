# Behavioural specification

Every tuning decision in the SNES build, written down. The assembly is
disposable; this is not. It was extracted from `platform/snes/` at tag
`snes-final` and is the authority for any port — if the DS build disagrees with
a number here, the DS build is wrong.

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
| `PICK` | 224 × 224 | walked into — the raft materials, mushrooms, coconuts, the egg, the bottle |
| `SWING` | 544 × 544 | answered with the Keyblade — fish, palms still carrying coconuts |
| `PROP` | 416 × 416 | examined — the chalk drawings, the door in the Secret Place |
| `TOUCH` | 160 × 160 | a Heartless reaching Sora, and a boss fist landing |

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

- The **slam** marks where Sora is standing at the end of the wind-up and lands
  there a beat later. Anyone inside `TOUCH` of the mark is hit, and **a Shadow
  crawls out of the impact**.
- **Orbs** live 110 frames and travel at speed 40 along one of the eight
  facings.
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

- Day change: 30 frames out, 30 frames in, cast rebuilt at black.
- Race countdown: 192 frames, 64 to a number.
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
alternating sign on a frame-count bit: ±3 for a shatter or a boss landing, ±2
for something smaller.

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
