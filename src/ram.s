;=============================================================================
; ram.s -- storage definitions
;
; Actors are kept as a structure of arrays rather than an array of structs:
; on the 65816 that turns every field access into abs,X with a constant base,
; which is both shorter and faster than computing a per-actor base pointer.
;=============================================================================
.p816
.include "ram.inc"

.segment "ZEROPAGE"

tmp0:         .res 2
tmp1:         .res 2
tmp2:         .res 2
tmp3:         .res 2
tmp4:         .res 2
tmp5:         .res 2
tmp6:         .res 2
tmp7:         .res 2
tmp8:         .res 2
tmp9:         .res 2
ptr0:         .res 3
ptr1:         .res 3

padHeld:      .res 2
padPrev:      .res 2
padPressed:   .res 2

frameCount:   .res 2
vblankFlag:   .res 1            ; set by NMI, cleared by the main loop
oamDirty:     .res 1            ; main loop finished a frame of OAM

camX:         .res 2
camY:         .res 2
bgHOfs:       .res 2
bgVOfs:       .res 2
camLoX:       .res 2
camHiX:       .res 2
camLoY:       .res 2
camHiY:       .res 2

streamPend:   .res 1            ; non-zero: NMI should upload a Sora frame
streamSrc:    .res 2
streamBank:   .res 1

playerIdx:    .res 1
soraFrameCur: .res 1            ; which frame is currently resident in VRAM
hitStopTimer: .res 1            ; freeze frames on a connect
shakeTimer:   .res 1
hudDirty:     .res 1            ; HUD row needs re-uploading
rngState:     .res 2
heartTile:    .res 1            ; sprite base a Shadow draws from, per scene

; Dialogue box state.
txtState:     .res 1            ; 0 closed, 1 revealing, 2 waiting
txtMode:      .res 1            ; 0 message, 1 yes/no prompt
txtPtr:       .res 3            ; current character in the script
txtCol:       .res 2
txtRow:       .res 2
txtDelay:     .res 1
txtDirty:     .res 1
txtChoice:    .res 1            ; highlighted option
txtResult:    .res 1            ; option confirmed by the last prompt
txtHold:      .res 1            ; debounce so one press is not read twice

; Scene and script progression.
sceneId:      .res 1
diveStage:    .res 1
weaponTaken:  .res 1
weaponGiven:  .res 1
scriptWait:   .res 1
pendActor:    .res 1            ; actor an open prompt refers to
pendWeapon:   .res 1
; Collision map of the scene currently loaded, as a long pointer so the
; walkability test does not have to know which scene it is in.
collPtr:      .res 3
; ...and the matching ground-height map, so a raised deck can be walked on.
heightPtr:    .res 3
; Height of the tile the actor being moved is standing on, so the step test
; has somewhere to compare against.  A word, so 16-bit arithmetic can use it.
stepZ:        .res 2

; Screen-wide effects, written by the NMI so they land in vblank.
screenBright: .res 1            ; INIDISP value
mosaicAmt:    .res 1            ; MOSAIC value
; The colour-math registers are shadowed here and written by the NMI, so a
; lightning flash can swap the unit between "translucent shadows" and "add
; white to everything" without tearing a seam across the frame.
cgwselVal:    .res 1
cgadsubVal:   .res 1
shatterTimer: .res 1
bossHP:       .res 1            ; mirrored for the gauge
shakeX:       .res 1
fadeTimer:    .res 1
coldataAmt:   .res 1            ; fixed colour-math colour, 0-31
deadFlag:     .res 1            ; 0 alive, 1 just died, 2 GAME OVER showing
fallTimer:    .res 1

; Destiny Islands
questState:   .res 1            ; Q_IDLE / Q_ACTIVE / Q_DONE / the day change
questDay:     .res 1            ; 1 or 2
dayTimer:     .res 1            ; frames left in the current half of a fade
pendTalk:     .res 1            ; islander the A press landed on
raceLeg:      .res 1            ; 0 out to the paopu tree, 1 back
rikuWp:       .res 1            ; which marker Riku is running for
raceWon:      .res 1            ; 0 undecided, 1 Sora, 2 Riku
raftName:     .res 1            ; RAFT_*, once it has one
; One slot per collectable, indexed by (actor type - ACT_LOG), so a pickup
; tallies itself without a lookup.
itemCount:    .res 8

; The night the island falls
nightStage:   .res 1            ; N_INTRO .. N_OVER
nightTimer:   .res 1            ; frames left in whatever the stage is waiting on
spawnTimer:   .res 1            ; frames until the next Shadow arrives
flashTimer:   .res 1            ; lightning: counts down through a flash
flashWait:    .res 2            ; ...and then to the next one
keyGot:       .res 1            ; 0 until the Keyblade comes; nothing connects
saidNoUse:    .res 1            ; the line about the sword, said once
curActor:     .res 2            ; actor being updated, survives calls that use tmp*

.segment "BSS"

; OAM shadow.  DMA'd wholesale during vblank: 512 bytes of low table plus the
; 32-byte high table that carries each sprite's size bit and X sign bit.
oamBuf:       .res 512
oamHigh:      .res 32

; Draw order, rebuilt every frame: word entries holding actor indices, sorted
; by world Y descending so slot 0 is the frontmost sprite.
;
; Resizing this, or MAX_ACTORS, is not as free as it looks.  At MAX_ACTORS = 32
; the sprite path reliably corrupts Sora's cel and drops actors while standing
; still, and padding this array by four entries fixes that but breaks 28
; instead -- so a stray write is landing on whatever the layout puts just past
; the end of it.  The writer has not been found.  28 with this sizing is the
; combination that has been played through, and the asserts in island.s and
; night.s keep the placed casts inside it.
sortIdx:      .res MAX_ACTORS * 2
sortCount:    .res 2

; Three 32-entry rows of the BG3 tilemap: Sora's gauge, then either a boss
; gauge or the day's checklist, which needs two rows of its own on day two.
hudRow:       .res 192

; The dialogue box: seven 32-entry rows of the BG3 tilemap.
txtBuf:       .res 7 * 32 * 2

actType:      .res MAX_ACTORS
actZ:         .res MAX_ACTORS           ; ground height in eight-pixel steps
actX:         .res MAX_ACTORS * 2       ; world pixels, Q12.4 signed
actY:         .res MAX_ACTORS * 2
actVX:        .res MAX_ACTORS * 2
actVY:        .res MAX_ACTORS * 2
actDir:       .res MAX_ACTORS
actAnim:      .res MAX_ACTORS
actAnimT:     .res MAX_ACTORS
actState:     .res MAX_ACTORS
actTimer:     .res MAX_ACTORS
actHP:        .res MAX_ACTORS
actFlags:     .res MAX_ACTORS
actTile:      .res MAX_ACTORS
actPal:       .res MAX_ACTORS
actHitT:      .res MAX_ACTORS
