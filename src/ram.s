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

streamPend:   .res 1            ; non-zero: NMI should upload a Sora frame
streamSrc:    .res 2
streamBank:   .res 1

playerIdx:    .res 1
soraFrameCur: .res 1            ; which frame is currently resident in VRAM
hitStopTimer: .res 1            ; freeze frames on a connect
shakeTimer:   .res 1
hudDirty:     .res 1            ; HUD row needs re-uploading
rngState:     .res 2

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

; Screen-wide effects, written by the NMI so they land in vblank.
screenBright: .res 1            ; INIDISP value
mosaicAmt:    .res 1            ; MOSAIC value
shatterTimer: .res 1
bossHP:       .res 1            ; mirrored for the gauge
shakeX:       .res 1
fadeTimer:    .res 1
coldataAmt:   .res 1            ; fixed colour-math colour, 0-31
deadFlag:     .res 1            ; 0 alive, 1 just died, 2 GAME OVER showing
fallTimer:    .res 1
curActor:     .res 2            ; actor being updated, survives calls that use tmp*

.segment "BSS"

; OAM shadow.  DMA'd wholesale during vblank: 512 bytes of low table plus the
; 32-byte high table that carries each sprite's size bit and X sign bit.
oamBuf:       .res 512
oamHigh:      .res 32

; Draw order, rebuilt every frame: word entries holding actor indices, sorted
; by world Y descending so slot 0 is the frontmost sprite.
sortIdx:      .res MAX_ACTORS * 2
sortCount:    .res 2

; Two 32-entry rows of the BG3 tilemap: Sora's gauge, then the boss's.
hudRow:       .res 128

; The dialogue box: seven 32-entry rows of the BG3 tilemap.
txtBuf:       .res 7 * 32 * 2

actType:      .res MAX_ACTORS
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
