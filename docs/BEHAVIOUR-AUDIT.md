# Specification audit

`docs/BEHAVIOUR.md` was written by reading the assembly, and a port will trust it
absolutely, so it was then audited against the assembly by an independent pass
whose only job was to find what the document gets wrong or leaves out.

It found 41 items. The outright errors have been corrected in `BEHAVIOUR.md`
itself; everything else is recorded here verbatim, with the file and line of the
truth, because transcribing it into prose would lose the citations.

Findings **42 onwards** were added later, by the DS port: every milestone that
reimplements a system re-reads the assembly for it, and what the port finds is
appended in a dated section at the end rather than folded in — so the order of
discovery stays legible. Two of them correct findings in the original 41 (42
corrects 41, 44 corrects 16), two correct earlier *port* findings (52 corrects
45, 53 corrects 46), and three correct this document's own "must be told" list or
`BEHAVIOUR.md` directly. If a finding is contradicted by a later one, the later
one won; the numbering never changes.

**Read this alongside `BEHAVIOUR.md`, not instead of it.** Where the two
disagree, this document is the later and better-sourced of the two.

Five findings were re-verified by hand before anything was changed:

| Finding | Verified how |
| --- | --- |
| The Heartless touch box is 320x160, not 160x160 | `HeartlessTouchTest` halves `abs(dx)` with a bare `lsr a` before comparing to `TOUCH_X` |
| A boss marks its target on *entry* to the wind-up | `jsr AimAtPlayer` sits immediately before the `DSS_SLAM_UP` / `DS_SLAM_WIND` stores |
| Orbs travel at `dirVel * 2`, not `ORB_SPEED` | `SpawnOrb` does `lda dirVelX,y / asl a`; `ORB_SPEED` has one reference, its own definition |
| `ORB_SPEED`, `LINE_X`, `LINE_Y`, `KNOCKBACK`, `AF_SOLID` are dead | one reference each across all sources |
| The ROM is byte-reproducible from clean | `docs/oracle-baseline.sha256` |

---

## Findings

**1.** WRONG - the TOUCH box. §2 says TOUCH is 160x160 for 'a Heartless reaching Sora'. HeartlessTouchTest halves |dx| with a bare `lsr a` before comparing to TOUCH_X (world.s:1988), so the effective box is 320 x 160, not 160 x 160. It is asymmetric and the halving is uncommented.

**2.** WRONG/INCOMPLETE - a Shadow's true damage condition. HeartlessAimDir returning $FF (both axes inside the 208 deadzone) makes UpdateHeartless skip TryMoveActor AND TouchPlayer entirely (world.s:1796-1812), so a stationary Shadow can never damage. Combined with the halved X test, the ONLY way a Shadow hurts Sora is 208 <= |dx| <= 319 and |dy| <= 159 - it can never connect from directly north or south, and never point-blank. A Shadow that closes vertically parks at |dy| ~ 207 and stands there forever. The doc's '160x160 box, hit on overlap' description produces a completely different game.

**3.** WRONG - §2's TOUCH row also credits 'a boss fist landing' to TOUCH. Only Darkside's fist uses TOUCH_X/TOUCH_Y, inlined without the lsr (world.s:1638-1646). The Guard Armor's fist uses GA_HAND_X 448 x GA_HAND_Y 384 (town.s:1276-1285).

**4.** WRONG - §4 says the slam 'marks where Sora is standing at the end of the wind-up and lands there a beat later'. AimAtPlayer is called on ENTRY to DSS_SLAM_UP, i.e. 44 frames before impact (world.s:1002-1007). The mark is stale for the whole telegraph. Same for the Guard Armor: AimAtPlayer at the start of the 40-frame GAS_WIND (town.s:999). This inverts the dodge window and is the single most consequential error in the file.

**5.** WRONG - §4 says orbs 'travel at speed 40'. SpawnOrb uses `lda dirVelX,y / asl a` = 2x dirVel, i.e. 48 on a cardinal and 34 on a diagonal (world.s:1335-1340). ORB_SPEED = 40 (game.inc:275) is dead - grep finds one definition and zero uses.

**6.** WRONG - §7 says shake is '+/-3 for a shatter or a boss landing, +/-2 for something smaller'. The Dive's Shatter is +/-2 (dive.s:186-189). It is the night's Tear that is +/-3 (night.s:788-794). Armour landing +/-3 (town.s:975-981), pair landing +/-2 (town.s:793-799). All four use `frameCount and #$02` (4-frame period), which the doc never gives.

**7.** MISSING - the dive's weapon choice has NO consequence whatsoever. weaponTaken and weaponGiven are written once each (dive.s:848, 872) and read nowhere in the codebase. §6 lists DIVE_PICK/DIVE_DROP without saying the choice is inert. A porting agent will otherwise invent stat effects. Same for raftName: it only selects a confirmation line (island.s:1018-1030).

**8.** MISSING - the race's win condition. §6 gives waypoints and RIKU_NEAR but not the win test. RaceRun checks `rikuWp >= RACE_WPS` FIRST, so Riku wins a same-frame tie (island.s:858-863). Sora's course is two legs: within TAG_X/TAG_Y (448, i.e. 28 px per axis) of PAOPU (tile 27,7) sets raceLeg=1, then within the same 448 of FINISH (tile 12,12) wins (island.s:865-905). LINE_X/LINE_Y (game.inc:359-360) are dead - RaceRun reuses TAG_X/TAG_Y for both legs.

**9.** MISSING - Riku ignores terrain completely. UpdateRiku writes actX/actY directly with a per-axis clamp to +/-17 and never calls TryMoveActor or SetActorZ (island.s:1100-1130), so he walks through the boulder and his sprite lift is frozen at the start-line height. Sora is fully collided. Simulated end to end, Riku needs ~615 frames and Sora's ideal line ~360, so the race has >4 s of slack - 'quick enough to punish wandering' overstates it.

**10.** MISSING - PICK requires equal ground height. CheckPickups rejects any item whose actZ differs from the player's (island.s:302-304). This is why you cannot take the treehouse cloth from the grass. §2's PICK row says only '224x224, walked into'.

**11.** MISSING - selection rules differ per interaction and are not stated. CheckPickups, SwingAt, FindTalker, TalkTown and TalkTarget all take the FIRST match in slot order. FindProp takes the NEAREST by Manhattan |dx|+|dy| (island.s:478-516). FindWeapon takes the first despite its header comment claiming 'nearest' (dive.s:717-792).

**12.** MISSING - the interaction-range table is incomplete. §2 omits REACH 448x448 for the dream weapons (dive.s:28-29), TAG 448x448 for the race, SWEEP 704x448, DS_HURT 576x640, GA_HURT 640x704 and GA_HAND 448x384. It also omits the attack-centre offsets themselves: atkOfs is 256 on a cardinal and 176 on a diagonal (world.s:2241-2242) - without them 'one facing-offset ahead' is unimplementable.

**13.** MISSING - the Secret Place details §6 was supposed to cover. The three props are ACT_DOOR/FACES/SCRIBBLE at tiles (2,6)/(1,6)/(3,6), all AF_FLAT so actZ is forced to 0 (world.s:2320-2322; world.s:247-251). The door's line changes only at questState == Q_NAMED (island.s:544-549). A press checks FindTalker before FindProp, so an islander in range wins. At night the props are unreachable - NightUpdate has no prop path - and OpenTheDoor converts ACT_DOOR to ACT_DOOROPEN (31), which falls outside FindProp's 28..30 scan (night.s:672-689).

**14.** MISSING/WRONG - the questState diagram. DayIn ends with questState = Q_IDLE (island.s:244), which the diagram does not show, so day two re-enters IDLE->ACTIVE->DONE. Day one's DONE->DAYOUT fires automatically with no player action once the line is dismissed (island.s:103-110). Day two's DONE->RACE_SET requires talking to Kairi again (island.s:666-675). And RACE_OVER goes to NAMING only if Sora won; if Riku won it jumps straight to NAMED with RAFT_EXCALIBUR forced (island.s:977-1008).

**15.** MISSING - what the day change actually rebuilds. DayOut forces blank, ClearActors, then InitWorld - which re-seeds rngState to $ACE1 and teleports Sora back to tile (11,12) - then day2Spawns (island.s:198-222; world.s:42-101). itemCount is NOT cleared; it survives because day two uses different slots. §6's 'cast rebuilt at black' hides all three facts.

**16.** MISSING - Q_DUSK. §6 lists DUSK in the diagram but gives no timing: it is DAY_FADE=30 frames, brightness (dayTimer-1)>>1 from 14 to 0, then a direct jump to NightBegin (island.s:159-170). It is armed by talking to Kairi at Q_NAMED, not by anything else.

**17.** MISSING - the item requirements and the checklist. NEED_LOGS 2, NEED_CLOTH 1, NEED_ROPE 1, NEED_FISH 3, NEED_MUSH 3, NEED_NUT 2, NEED_EGG 1, NEED_WATER 1 (game.inc:474-481), checked by HaveAll per day (island.s:695-736). itemCount is indexed by (actType - ACT_LOG) so a pickup tallies itself; a coconut palm becomes ACT_PALM and tallies IT_NUT, a fish is removed and tallies IT_FISH (island.s:280-402). None of this is in the doc.

**18.** MISSING - the three-orb spread and the muzzle. FireOrbs takes the aim from a point 640 Q12.4 (40 px) above the boss's feet, buckets it with the 208 deadzone, substitutes DIR_S if the bucket is centre, then fires at facing-1, facing, facing+1 mod 8 (world.s:1206-1301). §4 says only 'fires 3'.

**19.** MISSING - the sweep is re-tested at the moment of impact, so leaving during the 26-frame wind-up escapes it, and DarksideSweep also spawns three ACT_SLASH arcs at X offsets -384/0/+384 and Y+160 with an 8-frame life (world.s:1149-1201). §4 implies a single check.

**20.** MISSING - Darkside's opening state and first attack. Both spawn sites set actTimer = DS_REST, so the first action is 56 frames after it appears; actAnim starts 0 and the `eor #$01` makes the FIRST ranged attack the fist, and a sweep does not disturb the alternation (world.s:997-1014; night.s:832-857; dive.s:679-714). Its crater Shadows are uncapped - SHADOW_MAX is only enforced by night.s's SpawnShadows, which does not run during N_BOSS.

**21.** MISSING - boss flinch is invulnerability. HurtBoss refuses damage while actHitT != 0 (world.s:922-923), so a boss takes at most 1 damage per 11 frames; combined with one DoAttackHit per 19-frame swing that floors both fights at ~684 frames. A Shadow's 14-frame recoil is NOT invulnerable (world.s:1707-1717). §3 and §4 describe both as 'recoil'/'flinch' without distinguishing.

**22.** MISSING - both bosses vanish instantly at 0 HP (`stz actType`, world.s:934-937); there is no death state, and the scene scripts detect the win by CountType returning zero.

**23.** BUG the doc is silent on - the Dive's WatchBoss does not sweep Darkside's leftover Shadows (dive.s:298-318), whereas the night's does (night.s:870-879). Surviving crater Shadows therefore persist into DIVE_DONE and through the 170-frame fall, where DamageSora tests only for ST_DEAD and not ST_FALL (world.s:2029-2031); a hit knocks Sora out of ST_FALL into ST_HURT and hands control back mid-fall, or kills him and retries the boss.

**24.** BUG the doc is silent on - RestartScene for SCENE_ISLAND calls InitWorld only, never IslandInit or the day's item table (dive.s:386-391), so a death on the island permanently destroys the remaining collectables. Unreachable today only because nothing damages Sora during the two days; any port that adds a damage source there inherits an unwinnable state.

**25.** IMPRECISE - §3's 'Animation: 4 cels, 10 frames each' is off by one. The timer is set to 10 and then counted to zero before the next advance, so each cel lasts 11 frames (world.s:1822-1823). Every animation timer in the engine has this +1.

**26.** IMPRECISE - §2's 'Attack length 18 frames'. ST_IDLE is restored on the same frame the timer reaches 0, so control is withheld for frames 0..18 inclusive = 19 frames (world.s:439-452).

**27.** IMPRECISE - §2's death dim 'ramping down but clamped at 3 of 15'. The ramp starts at 12, not 15: brightness = (timer-1)>>2 over 50 frames, so 12 down to 3, then held at 3 for the last 16 frames (world.s:475-485).

**28.** MISSING - the actual brightness and mosaic curves of the two dissolve effects, and the fact that they differ. Shatter over 96 frames reaches mosaic 12/15 and brightness 3 (dive.s:151-190); Tear over 120 frames reaches mosaic 15 and brightness 0 (night.s:757-795). Both are (elapsed>>3), so the length alone sets how far the effect gets.

**29.** MISSING - the lightning COLDATA levels. §6 gives the 8-frame shape but not the amounts: 2 frames at 16, 2 at 0, 2 at 26, 2 at 0, with cgwselVal=0 and cgadsubVal=$3F while lit and ShadowMath restoring CGWSEL_VAL/CGADSUB_VAL when dark (night.s:252-305). The closing fade's registers are also unstated: cgadsubVal=$B1, which excludes BG3 so the box stays readable, and OBJ palettes 0-3 are outside SNES OBJ colour math so Sora does not darken (night.s:881-891, 897-934).

**30.** MISSING - which night stages spawn Shadows. SpawnShadows is called only from SeekRiku (N_SEEK) and SeekKairi (N_KAIRI) (night.s:486, 643). Nothing arrives during N_RIKU, N_KEY, N_DOOR, N_TEAR or N_BOSS. §6's 'one every 70 frames' reads as continuous.

**31.** MISSING - the column-of-darkness beats. DARK_HOLD ticks only while no box is up; Riku's column flickers between TILE_DARK and TILE_DARK+4 on (frameCount & 4) while Kairi's does not animate at all; the Keyblade and Kairi's disappearance each fire an extra FLASH_LEN flash (night.s:535-603, 694-749).

**32.** MISSING - saidNoUse. Swinging at a Shadow during N_SEEK produces one line, once, and the proximity test is TALK range 384 rather than SWING range (night.s:508-527). §3 mentions saidNoUse only inside the bug note.

**33.** MISSING - only ONE hand strikes. OneHand routes actAnim==0 (the left) to the station-keeping branch even during GAS_WIND/GAS_SLAM (town.s:1196-1198); only the right hand goes to the mark. §5's 'the hands, which land wherever you are' is plural and misleading.

**34.** IMPRECISE - §5 says RaiseArmor stands the player 'in the middle of the square when the armour lands'. It runs when the armour STARTS its 40-frame descent, and the tiles are explicit: player to (16,10), armour to (16,7) - 3 tiles apart, not 2. The '2 tiles' refers to the door landing at (16,5) (town.s:866-907).

**35.** MISSING - WatchArmor uses townTimer as a one-shot raise flag (set to 1 by Meet and by TownRestart at T_BOSS), and on the win it sweeps every ACT_GAUNTLET (town.s:810-845). Meet also gates on CountType(ACT_DONALD) rather than a pure timer.

**36.** MISSING - townKills counts SPAWNS, not kills, despite the name (`inc townKills` after a successful SpawnActor, town.s:650-652), and TownRestart resets it to 0 so a retry replays the whole wave (town.s:104-109).

**37.** MISSING - the camera's vertical bias. bgVOfs is (camY - 1) & $03FF, not camY (grid.s:98-101). §7's formula is right for a port but any bgVOfs trace diff will be off by one.

**38.** MISSING - the depth-sort tie-break. InsertSorted stops on `prevY >= thisY` (oam.s:130-132), so equal-Y actors keep slot order with the lower index frontmost. §8 says only 'sorted by world Y descending'.

**39.** MISSING - the sprite anchor offsets §8 needs to be implementable: AF_LARGE actors are drawn at (-16,-32) from their feet, small at (-8,-16); shadow blobs at (-16,-24) and (-8,-12); AF_HUGE quadrants at (-32,-64),(0,-64),(-32,-32),(0,-32) with tile offsets +0,+4,+$40,+$44; the lift is actZ*8; culling is a +32-biased unsigned compare; the ceiling is 128 sprites and EmitBoss handles only the first AF_HUGE actor (oam.s:222-571).

**40.** PRECISION RISK for §8/§9 - 'nothing walkable in the top 24 px' is true in map coordinates but not on screen. The HUD occupies BG3 rows 1-3 = screen y 8..31 (nmi.s:152, 192-byte upload), and on a Station of Awakening the camera is pinned at camY=16, so walkable tile row j=2 (world y 32..47) sits inside the HUD band. Sora can already stand under the gauge there. §9's instruction to 're-check the margin' needs to say this is a camera-bound question, not only a map-authoring one.

**41.** DEAD CONSTANTS that will mislead a reimplementer: KNOCKBACK = 3 (world.s:35) is never used - both knockback sites use `asl a` = x2, which is what the doc correctly says; ORB_SPEED = 40; LINE_X/LINE_Y = 448; AF_SOLID = $04; shakeTimer and scriptWait are dead RAM (ram.s:50, 72). Each should be struck or explicitly marked dead in the spec.


---

## What the audit says a port must be told


- The main-loop order is part of the specification and must be stated: WaitVBlank -> ReadPad -> TextUpdate -> SceneUpdate -> UpdateWorld -> UpdateCamera -> BuildOam (main.s:134-144). The scene script runs BEFORE actor simulation every frame. padPressed is not consumed by scene scripts, so one B press both collects a fish (SceneUpdate/SwingAt) and starts a swing (UpdateWorld/UpdateSora) on the same frame.

- Actors are updated in slot index order 0..MAX_ACTORS-1 (world.s:292-360) and slots are claimed by first-free scan (world.s:157-166). Spawn order therefore determines update order and, for ties, draw order. A port must keep a fixed 32-slot array with first-free allocation, not a list or a pool with different reuse.

- hitStopTimer freezes ALL actor simulation (UpdateWorld returns immediately, world.s:285-289) but does NOT freeze scene scripts, camera, HUD or OAM. Values: 3 on a connect, 4 on a Shadow kill, 3 on damage to Sora, 3 on a boss hit, 6 on the pair landing, 6 on ArmorSlam, 8 on the armour landing.

- The RNG is load-bearing and must be reproduced bit-exactly for the §10 trace oracle: 16-bit Galois LFSR, `asl a; if carry then eor #$002D` returning the low byte (night.s:325-338). Seeded $ACE1 in InitWorld (world.s:51) and RE-seeded $1D57 in TownBegin (town.s:73). Spawn-spot selection is `Rand & $00FF` reduced by repeated subtraction of the spot count, not a modulo (night.s:365-375; town.s:603-613).

- The dialogue system gates every scene transition and must be specified: REVEAL_DELAY 1 means one character every 2 frames (text.s:21, 356-360); A or B fast-forwards the whole page in one frame (RevealAll, text.s:482-496); SC_PAGE waits then clears; TextClose masks A and B out of padPressed so the dismissing press cannot start the next interaction (text.s:104-120); txtHold ignores the button that opened the box until it is released (text.s:92-93, 308-317).

- Nearly every scene stage advances only while TextBusy is false. The two documented exceptions are the night's Tear and EndFade, which run THROUGH the box (night.s:180-188). **[corrected - there are five, see finding 53.]** The Dive's Shatter does NOT - it waits for the box (dive.s:77-99). A port must encode which beats ignore dialogue and which wait.

- LoadScene has scene-independent side effects that must be replicated: heightPtr defaults to flatHeights, heartTile defaults to TILE_HEART0, camera defaults to the pinned Station bounds, and keyGot is set to 1 (main.s:272-299). The night is the only scene that clears keyGot, and it does so itself after LoadScene (night.s:76).

- Death and retry is entirely undocumented and must be written down: on HP 0, ST_DEAD with a 50-frame dim, then deadFlag=1; SceneUpdate hands to GameOverUpdate ahead of the scene script (main.s:603-607); the card is shown (deadFlag=2), and on the next dismissal RestartScene reloads sceneId and restarts at a per-scene checkpoint (dive.s:324-429). Checkpoints: TOWN* -> TownRestart; NIGHT/FRAGMENT -> NightRestart; ISLAND -> InitWorld + diveStage=DIVE_ARRIVED; DIVE3 -> soraOnly + SpawnBoss + DIVE_BOSS; DIVE2 -> SpawnStation2 + DIVE_S2_FIGHT; DIVE1 -> diveSpawns + DIVE_PICK.

- NightRestart's rewind rules: stage < N_KAIRI rewinds to N_SEEK and CLEARS keyGot **[corrected - that branch cannot be reached, see finding 56]**; stage >= N_KAIRI rewinds to N_KAIRI, keeps the Keyblade and re-runs OpenTheDoor; on the fragment it re-raises Darkside at N_BOSS (night.s:102-159). Because it re-runs the whole nightSpawns table, Riku reappears on the small island after a death at N_KAIRI or later.

- Every animation rate is an N-then-count-down-to-zero timer, so the visible cel period is N+1 frames, not N. Sora walk: 4 cels x 7 frames (world.s:651-652). Shadow: 4 cels x 11 frames (world.s:1822-1823). Orb: 2 cels x 6. Mote: 2 cels x 4. Falling tumble: facing +1 every 9 frames. Fish and the dark column instead use a bit of a counter: fish cel on (timer & 7) == 0, column cel on (frameCount & 4).

- Sora's sprite sheet layout must be specified: 5 drawn facings x 6 cels = 30 cels of 32x32, frame index = drawFacing*6 + cel, cel 0 idle, 0-3 walk, 4 while actTimer >= 10 and 5 below that during an attack (world.s:667-761). sorachr.bin is exactly 30 x 512 bytes.

- Only AF_HUGE actors flash when hit - PAL_OBJ_FX on (frameCount & 2) for the duration of actHitT (oam.s:285-304). Shadows show no hit feedback at all.

- AF_SOLID ($04) is defined in game.inc:302 but appears in no typeFlags row and is never tested. Actors do not block each other anywhere in the game; all collision is against the tilemap. A port must NOT implement actor-vs-actor solidity, despite the game.inc comment and island.s:2306-2307 claiming the islanders are 'solid'.

- The one-step height rule uses the actor's cached actZ, so any code that repositions an actor must re-derive it via SetActorZ or the actor will be measured from a stale deck (world.s:244-277). PutActor and PlaceSora do this; UpdateRiku deliberately does not.

- ASR1 is a sign-preserving shift with two consequences a port must match if it diffs traces: -17>>1 = -9 (so a Shadow's westward diagonal is 9 while its eastward is 8), and -1 is a fixed point, so a negative knockback velocity decays to -1 and stays there rather than reaching 0 (macros.inc:49-52).

- The HUD gauge arithmetic must be spelled out: cell x is FULL when HP-2x >= 2, HALF when HP-2x == 1, EMPTY when HP-2x <= 0 (hud.s:89-125). Sora gets 10 cells at columns 5-14 with 'HP' at 2-3 and brackets at 4 and 15; Darkside 18 cells at 11-28; Guard Armor 18 cells at 13-30 (its name is longer). The HUD is three BG3 rows (1-3) and the row-2/3 content is scene-dependent: boss gauge, or Kairi's checklist, or a one-line objective from NightStageLabel / TownStageLabel.

- The town's world graph is data the doc never prints and a port cannot guess: doorTable is 4 rows of {map, door i, DOOR_ROW=4, target map, landing i, landing j=5, required stage} = TOWN1(25)->TOWN2(26,5) needs T_SECOND; TOWN2(26)->TOWN1(25,5) needs T_ARRIVE; TOWN2(5)->TOWN3(16,5) needs T_THIRD; TOWN3(16)->TOWN2(5,5) needs T_ARRIVE. A door opens when required <= townStage; otherwise it says scriptShut1 if the requirement is T_SECOND, else scriptShut2 (town.s:324-346, 1390-1395).

- Every spawn table is part of the spec and must be carried across verbatim (tile i, tile j triples): spawnTable, day1Spawns, day2Spawns (world.s:2361-2382; island.s:1211-1229), nightSpawns, fragSpawns, nightSpots (night.s:1022-1068), diveSpawns, soraOnlySpawns, station2Spawns, moteOfsX/Y (dive.s:902-932, 919-924), town1/2/3Spawns, pairSpawns, townSpots (town.s:1404-1457), raceWp (island.s:1177-1198). The item counts exactly equal the requirements - 2 logs, 1 cloth, 1 rope, 3 fish, 3 mushrooms, 2 coconut palms, 1 egg, 1 bottle - so there is zero slack and no item may be omitted or made unreachable.

## Correction found during §M5

- **Finding 42 corrects finding 41.** Finding 41 lists `SC_PAGE` as "waits then clears". It does not clear, and it does not continue. `EmitOne` sets `TS_WAIT` for `SC_PAGE` (text.s:449-455) exactly as it does for `SC_END` (text.s:432-437), and `TextUpdate`'s `TS_WAIT` branch closes the box for `TM_MESSAGE` unconditionally (text.s:466-473). `ClearTextArea` has one caller, `TextOpen` (text.s:97). There is no transition from `TS_WAIT` back to `TS_REVEAL` anywhere in the file, so a page break is an end of message and every byte after the first `SC_PAGE` in a script is unreachable: 54 of 70 scripts are affected and 3213 of 5044 dialogue characters — 63% — are never displayed. Confirmed on the running ROM, not only by reading: at boot, the Dive shows "SO MUCH TO DO, / SO LITTLE TIME. / TAKE YOUR TIME." and the next press closes the box rather than advancing to "DON'T BE AFRAID." Finding 41 repeated text.inc's comment rather than tracing the code; this is the one place in the audit where the intent and the behaviour were conflated. See BEHAVIOUR.md §13.

## Corrections and additions found during §M3

- **Finding 43: `TryMoveActor`'s "refuse" branch is nearly unreachable, and a blocked cardinal move slides on the *other* axis.** §1 lists the fourth outcome as "do not move", which is right about the result and misleading about the path. `@slideY` tests `(actX, tmp6)` where `tmp6 = actY + actVY` (grid.s:355-372), so when `actVY` is zero the candidate tile IS the tile the actor is standing on — walkable, zero height difference, so `StepOk` passes and `actY` is written back unchanged. A walk straight west into a wall therefore takes the vertical-slide branch and "succeeds" having moved nothing. `@stuck` is reached only when both velocities are non-zero and all three tests fail, i.e. on a diagonal into a corner where both orthogonal neighbours are also blocked. A port that reports which axis resolved — and the DS one does, because that is how the load-bearing order is tested — must expect the vertical case for every blocked cardinal move, or its own tests will assert the wrong thing.

- **`StoreZ` is not called on the refuse path** (grid.s:374-376), which is correct: the actor did not move, so the height it is standing on has not changed. But note the asymmetry with finding "the one-step height rule uses the actor's cached `actZ`" above: `TryMoveActor` maintains the cache on all three success paths and leaves it alone on failure, so the cache is only ever stale when something *else* moved the actor.

## Corrections and additions found during §M5's island and night machines

Two independent extractions of each machine were made from the assembly and
diffed against each other and against this document. Where all readings agreed
against the document, the document is wrong.

- **Finding 44 corrects finding 16.** Finding 16 gives Q_DUSK's brightness as
  "(dayTimer-1)>>1 from 14 to 0". It is `dayTimer>>1` from **15** to 0. The code
  is `lda dayTimer / beq @gone / dec dayTimer / lsr a / sta screenBright`
  (island.s:162-166): `dec dayTimer` is a direct-page MEMORY decrement and does
  not touch the accumulator, so the `lsr a` that follows halves the value loaded
  *before* the step. The same code appears in DayOut (island.s:191-195) and has
  the same result. `DayIn` is the one that differs — it reloads
  (`lda #DAY_FADE / sec / sbc dayTimer`, island.s:233-237) and so reads the
  POST-decrement value, ramping 0 up to 15. **The two halves of the day change do
  not mirror each other**, and finding 16 appears to have derived the formula
  from DayIn's shape and applied it to DayOut's code.

- **Finding 45: every one of the island's four timed beats is N+1 frames, not N.**
  All four use `lda / beq / dec`, testing before decrementing, so `DAY_FADE = 30`
  is 31 frames (30 of fading plus the frame that reads zero and transitions) and
  `COUNT_LEN = 192` is 193. §6's "30 frames out, 30 frames in" and "192 frames"
  are the constants, not the periods. The same is true of `DARK_HOLD` (97 frames),
  `TEAR_LEN` (121), `END_FADE` (63), `TOWN_GAP` (81) and `FALL_WAIT` (71) — this
  is a property of every timer in the game and not of any one scene.

- **Finding 46: the day change ignores the dialogue box, and the audit's list of
  exceptions is incomplete.** The note above says "the two documented exceptions
  are the night's Tear and EndFade". `Q_DAYOUT` and `Q_DAYIN` are dispatched
  ABOVE the `TextBusy` gate (island.s:69-76) and are therefore two more. Four
  beats in total run through an open box.

- **Finding 47: the transient `Q_IDLE` in DayOut is load-bearing.** island.s:215
  writes `Q_IDLE`, 216 calls `HudUpdate`, and 218 overwrites it with `Q_DAYIN` —
  three instructions apart, in one frame. It is not a redundant store: the HUD it
  draws is the one the player sees 31 frames later when the fade-in completes, and
  it has to be day two's empty checklist rather than a day-change state. A port
  that collapses the two writes loses a HUD row.

- **Finding 48: two scripts are unreachable, and their unreachability is the
  proof that the day-one/day-two asymmetry is real.** `kairiLines[0]`
  (`scriptKairiRest`) can never play, because `Q_DONE` on day one diverts to
  `EndOfDay` before the gameplay block is reached, so the player never gets a
  frame of control at `Q_DONE` on day one. `kairi2Lines[0]` is reached only on the
  path that offers the race. Both are dead data in the same way `ORB_SPEED` is.

- **Finding 49: `dayTimer` is time-shared by four unrelated machines** — the
  DayOut fade, the DayIn fade, the Dusk fade and the race countdown
  (island.s:181, 220, 637, 752). None can be live at once, so one field is the
  faithful representation; a port that gives each its own field is not wrong, but
  one that keeps a *fifth* user of the same byte would be.

- **Finding 50: the race's finish tolerance is `TAG_X`/`TAG_Y` for BOTH legs, and
  Sora starts the race inside the finish radius.** `tmp2`/`tmp3` are loaded once
  (island.s:866-872) and never reloaded, so the finish uses 448 — and his start
  line, `CELL(11,12)`, is only 256 from the finish at `CELL(12,12)`. The only
  thing preventing an instant win is `raceLeg` starting at 0. `RaceRun` also sets
  `raceLeg` and returns immediately (island.s:887-890), so tagging and winning
  cannot land on the same frame.

- **Finding 51: the countdown's HUD digit is not evenly divided.** It is
  `(dayTimer + 63) >> 6` (hud.s:479-489), which shows "3" for 63 frames, "2" for
  64, "1" for 64 and "0" for a single frame. The source comment at hud.s:478
  describes the intent; the arithmetic gives something else. `HudUpdate` is
  called *after* `dec dayTimer` (island.s:842-843), so the digit is read off the
  post-decrement value; the "3" the player actually sees first is the one
  `OfferRace` paints at `dayTimer = 192` (island.s:757), and it stays up for as
  long as `scriptChallenge` is on screen plus the 63 counting frames.

## Corrections and additions found during §M5's cross-check

A second pass over the island and night machines, run against the assembly with
every claim re-derived from the code rather than from this document. Three of
these correct findings written above.

- **Finding 52 corrects finding 45.** Finding 45 ends "this is a property of
  every timer in the game and not of any one scene." That generalisation is
  false, and a port that applies it uniformly gets two things wrong.

  The rule that *is* general is narrower: a timer written as `lda t / beq done /
  dec t` spends a frame reading zero, and on that frame it does nothing but
  transition — so the beat costs N+1 frames and the last one is empty. `DAY_FADE`,
  `COUNT_LEN`, `DARK_HOLD`, `TEAR_LEN`, `END_FADE`, `TOWN_GAP` and `FALL_WAIT`
  are all of that shape. (Note the constants are `DARK_HOLD = 96`,
  `TEAR_LEN = 120`, `END_FADE = 62`; the *periods* are 97, 121, 63.)

  Two counterexamples:

  1. **`flashTimer` has no +1.** It is the same `lda / beq / dec` shape, but the
     zero-reading frame is not spent: it falls straight through to `@waiting`,
     which decrements the gap counter on that same frame (night.s:255, 289-298).
     The flash is therefore exactly `FLASH_LEN = 8` frames of ramp and the
     zero-frame is the first frame of the gap. The ramp reads the POST-decrement
     value, so it is 16, 16, 0, 0, 26, 26, 0, 0 — and the comments at
     night.s:263-268 name the pre-decrement values for the first two branches
     and say "frames 4-2" for a branch that runs twice. Only "frames 1-0" is
     right. Same failure mode as finding 44.
  2. **Sora's attack timer and `ST_HURT` decrement BEFORE testing** (world.s:440-443
     and 495-498: `lda actTimer,x / dec a / sta actTimer,x / cmp`). Both still
     occupy N+1 frames, but for the opposite reason — the frame that *sets* the
     timer is the spent one and the frame that reaches zero does its work and
     transitions together. A port that models these with the `lda/beq/dec`
     shape gets the same duration and the wrong frame for the active hit:
     `ATK_ACTIVE` is tested against the post-decrement value.

  The distinction is what the DS `dusk()` needed a `handedOver_` flag for: with
  the first shape you may re-enter the zero-frame branch, with the second you
  may not.

- **Finding 53 corrects finding 46.** Finding 46 says the day change adds two to
  the list and makes "four beats in total" that run through an open dialogue box.
  It is **five**. `Lightning` is called above the `TextBusy` gate as well
  (night.s:175-178, gate at 189-191), so the storm keeps flashing while any line
  of the night is on screen — which is most of them. The full list is: the
  night's `Lightning`, `Tear` and `EndFade`, and the island's `Q_DAYOUT` and
  `Q_DAYIN` (island.s:72-78, gate at 80-82). Finding 46's citation of
  island.s:69-76 is off by three lines.

- **Finding 54: `COUNT_LEN = 192` is pinned by 8-bit arithmetic, not by taste.**
  The HUD digit is `clc / adc #63` with an 8-bit accumulator (hud.s:479-481).
  192 + 63 = 255 exactly. Raise `COUNT_LEN` to 193 and the add wraps: the
  countdown opens on "0" and counts *up*. The comment at hud.s:478 says only
  that 192 is a multiple of 64. A port that widens the arithmetic and then tunes
  the constant will not reproduce this, and should not want to — but a port that
  keeps 8-bit fields and raises the constant has a silent bug.

- **Finding 55: the walk-into coconut does not exist.** `ACT_COCONUT = 23`
  appears exactly once in all 21 sources — at its own definition, game.inc:194.
  There is no spawner, no `typeFlags` row, no draw case and no table entry. It
  is nonetheless inside the range `TryPickup` accepts (`ACT_LOG` through
  `ACT_BOTTLE`, island.s:298-312), so the *only* reason no coconut is walked into
  is that none is ever placed. Every `IT_NUT` in the game comes from `SwingAt`
  converting an `ACT_PALMC` to an `ACT_PALM` (island.s:389-394), and there are
  exactly two `ACT_PALMC` (world.s:2367-2368) against `NEED_NUT = 2`
  (game.inc:479) — zero slack, as with every other item.

  This matters twice over. **The constant must be kept**: `itemCount` is indexed
  by `actType - ACT_LOG` (island.s:312, ram.s:109), so `ACT_COCONUT` is what
  holds slot 4 open for `IT_NUT = 4`; deleting the "unused" type shifts `IT_EGG`
  and `IT_WATER` down and corrupts the tally. And **BEHAVIOUR.md §2's `PICK` row
  listed coconuts among the things walked into**, which came from island.s's file
  header rather than from the code — the same conflation of intent with behaviour
  as the `SC_PAGE` error in finding 42. A port implementing that row literally
  places two collectable coconuts, which makes day two completable *twice over*
  and the two `ACT_PALMC` pointless. §2 is corrected.

- **Finding 56: `NightRestart`'s pre-`N_KAIRI` rewind is unreachable.** The
  branch at night.s:136-139 rewinds to `N_SEEK` and clears `keyGot`. It can
  never run. `UpdateShadow` gates its `TouchPlayer` call on `keyGot`
  (world.s:1809-1811, "before the Keyblade they cannot reach him"), and
  `DamageSora` is the only writer of `ST_DEAD` anywhere (world.s:2038). On the
  night island the only Heartless are Shadows, so with `keyGot` clear nothing can
  damage Sora and there is no death to restart from. `GiveKeyblade` sets
  `keyGot = 1` and `nightStage = N_KAIRI` three instructions apart
  (night.s:623-626), so `nightStage < N_KAIRI` and `keyGot == 0` are the same
  condition — and `RestartScene` does not disturb it, because `LoadScene` only
  uploads VRAM and does not call `NightBegin` (main.s:305-338; it is `NightBegin`
  that calls `LoadScene`, not the reverse).

  A port should keep the branch — it is what the ROM does — but must know the
  trace oracle can never cover it, and must not "fix" the shape of it on the
  assumption it fires.

- **Finding 57: the night's post-boss sweep takes Shadows only.** `WatchBoss`
  clears every actor whose type is `ACT_SHADOW` (night.s:871-879) and nothing
  else. An `ACT_ORB` still in flight when Darkside's HP reaches zero survives
  into `N_END` and keeps travelling through the fade. Since `EndFade` runs above
  the dialogue gate and `DamageSora` tests only for `ST_DEAD`, a surviving orb
  can damage Sora during the closing fade. §12's first latent bug is about the
  Dive not sweeping at all; this is the narrower one in the scene that *does*
  sweep.

- **Finding 58: Riku runs the course in exactly 615 frames.** `RIKU_VX` and
  `RIKU_VY` are both 17 in Q12.4, clamped per axis per frame, and a marker is
  taken when both axes are within `RIKU_NEAR = 40` (island.s:1099-1151). From
  `START_RIKU_X/Y = CELL(13,12)` through all twenty `raceWp` entries that is 615
  frames of `Q_RACE_RUN` — about 10.25 seconds — after which `rikuWp` reaches
  `RACE_WPS` and `RaceRun` awards him the race. That is the budget Sora has, and
  the spec never states it.

  Two consequences: `RaceRun` tests Riku *before* Sora (island.s:858-863), so a
  same-frame tie goes to Riku; and `UpdateRiku` runs from `UpdateWorld`, which is
  not behind the dialogue gate, while `RaceRun` is — so Riku keeps running while
  a box is open and his win registers on the frame it closes.

## Found by the trace oracle, during §M3b

The first finding in this document that was not found by reading. §M6's headless
interpreter ran the frozen ROM and the DS port disagreed with it by three frames;
this is what the three frames were.

- **Finding 59: Darkside's fist cannot damage Sora. Ever.** `DarksideSlam` holds
  the impact point in `tmp0`/`tmp1`, spawns a Shadow there, and then reads those
  same two slots back for the damage test. In between, `SpawnActor` ends with
  `jsr SetActorZ` — whose call site is commented *"clobbers tmp0-tmp4, all of
  which are spent"*. In every other caller they are spent. Here they are not:
  `SetActorZ` writes the new actor's position **shifted right by four** into
  `tmp0`/`tmp1` (world.s, `SetActorZ`), so the test compares a Q12.4 coordinate
  against one sixteenth of one.

  With Sora at 4224 the clobbered value is 264 and the difference is 3960,
  against a `TOUCH_X` of 160. It is not marginal and it does not depend where he
  stands: **the fist misses by construction.** Only the sweep and the orbs can
  hurt him, because neither reads scratch after a spawn.

  Confirmed on the oracle rather than deduced: a fist landing exactly on Sora
  leaves him at 20 HP with `hitStopTimer` still zero. It was found because the DS
  port — which did not have the bug — ran its `DSS_SLAM_HIT` three frames long,
  the extra three being the hit-stop the SNES never incurred. No hand-written
  expectation would have produced that discrepancy, because the same misreading
  that wrote the port would have written the test.

  **The port reproduces it**, because the oracle is the specification and a
  silent fix would make every trace diff meaningless. The fix, if it is ever
  wanted, is to reload the mark after the spawn — two instructions — and it
  belongs in a divergence file, not in a quiet edit.

  This also makes §12's list of latent bugs incomplete in an instructive way:
  the two it lists are unreachable and this one is not merely reachable, it is
  the boss's headline attack. It has presumably never been noticed because the
  sweep and the orbs are enough to lose to.
