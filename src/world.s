;=============================================================================
; world.s -- actor spawning, Sora, Heartless, and the combat loop
;=============================================================================
.p816
.include "snes.inc"
.include "game.inc"
.include "ram.inc"
.include "macros.inc"

.import TryMoveActor, IsoToWorld
.import HudUpdate
.import TextBusy
.import soraChr

.export InitWorld, UpdateWorld, SpawnActor

;--- attack tuning -----------------------------------------------------------
ATK_ACTIVE   = 12               ; timer value on which the swing connects
ATK_REACH_X  = 200              ; Q12.4, compared after un-squashing X
ATK_REACH_Y  = 160
TOUCH_X      = 176
TOUCH_Y      = 144
HEART_DEAD_X = 384              ; AI deadzone, Q12.4
HEART_DEAD_Y = 192
KNOCKBACK    = 3                ; velocity multiplier on a hit

.segment "CODE"

;-----------------------------------------------------------------------------
; InitWorld -- populate the island from the spawn table.  A8/I16.
;-----------------------------------------------------------------------------
.proc InitWorld
    .a8
    .i16
    stz playerIdx
    lda #$FF
    sta soraFrameCur            ; force the first frame upload
    stz hitStopTimer
    rep #$20
    .a16
    lda #$ACE1
    sta rngState
    sep #$20
    .a8

    ldy #0
@loop:
    lda spawnTable,y
    cmp #$FF
    beq @done
    sta tmp6                    ; type
    iny

    ; isometric tile coordinates -> world pixels
    rep #$20
    .a16
    lda spawnTable,y
    and #$00FF
    sta tmp0                    ; i
    iny
    lda spawnTable,y
    and #$00FF
    sta tmp1                    ; j
    iny
    sty tmp7                    ; SpawnActor clobbers Y, so park the cursor
    jsr IsoToWorld

    ; IsoToWorld lands on the diamond's top corner; step to its centre and
    ; convert to Q12.4.
    lda tmp0
    clc
    adc #16
    asl a
    asl a
    asl a
    asl a
    sta tmp0
    lda tmp1
    clc
    adc #8
    asl a
    asl a
    asl a
    asl a
    sta tmp1
    sep #$20
    .a8

    lda tmp6
    jsr SpawnActor
    ldy tmp7
    bra @loop
@done:
    jsr HudUpdate
    rts
.endproc

;-----------------------------------------------------------------------------
; SpawnActor -- claim a free slot.
; In (A8/I16): A = type, tmp0 = world X (Q12.4), tmp1 = world Y (Q12.4)
; Out: X = actor index, or carry clear if the table is full.
;-----------------------------------------------------------------------------
.proc SpawnActor
    .a8
    .i16
    sta tmp2                    ; requested type
    ldx #0
@find:
    lda actType,x
    beq @got
    inx
    cpx #MAX_ACTORS
    bcc @find
    clc
    rts

@got:
    lda tmp2
    sta actType,x
    ; the type tables are indexed by type id
    phx
    rep #$20
    .a16
    lda tmp2
    and #$00FF
    tay
    sep #$20
    .a8
    lda typeTile,y
    sta tmp3
    lda typePal,y
    sta tmp4
    lda typeFlags,y
    sta tmp5
    lda typeHP,y
    sta tmp6
    plx

    lda tmp3
    sta actTile,x
    lda tmp4
    sta actPal,x
    lda tmp5
    sta actFlags,x
    lda tmp6
    sta actHP,x

    stz actDir,x
    stz actAnim,x
    stz actAnimT,x
    stz actState,x
    stz actTimer,x
    stz actHitT,x

    phx
    txa
    rep #$20
    .a16
    and #$00FF
    asl a
    tax
    lda tmp0
    sta actX,x
    lda tmp1
    sta actY,x
    stz actVX,x
    stz actVY,x
    sep #$20
    .a8
    plx

    ; remember where the player landed
    lda tmp2
    cmp #ACT_SORA
    bne @notplayer
    txa
    sta playerIdx
@notplayer:
    sec
    rts
.endproc

;-----------------------------------------------------------------------------
; UpdateWorld -- one frame of simulation.  A8/I16.
;-----------------------------------------------------------------------------
.proc UpdateWorld
    .a8
    .i16
    ; Impact freeze: a couple of held frames on a connect reads as weight.
    lda hitStopTimer
    beq @run
    dec hitStopTimer
    rts

@run:
    ldx #0
@loop:
    lda actType,x
    beq @next
    cmp #ACT_SORA
    bne @notSora
    phx
    jsr UpdateSora
    plx
    bra @next
@notSora:
    cmp #ACT_SHADOW
    bne @notHeart
    phx
    jsr UpdateHeartless
    plx
    bra @next
@notHeart:
    cmp #ACT_SLASH
    bne @next
    phx
    jsr UpdateSlash
    plx
@next:
    inx
    cpx #MAX_ACTORS
    bcc @loop

    jsr UpdateSoraFrame
    rts
.endproc

;-----------------------------------------------------------------------------
; UpdateSora -- In (A8/I16): X = actor index.
;-----------------------------------------------------------------------------
.proc UpdateSora
    .a8
    .i16
    stx curActor

    ; A dialogue box takes control away from the player entirely.
    jsr TextBusy
    bcc :+
    ldx curActor
    jsr ClearVelocity
    rts
:   ldx curActor

    lda actState,x
    cmp #ST_ATTACK
    beq @attacking
    cmp #ST_HURT
    beq @hurt

    ;--- free movement ---
    rep #$20
    .a16
    lda padPressed
    and #PAD_B
    sep #$20
    .a8
    beq @move
    ; start a swing
    lda #ST_ATTACK
    sta actState,x
    lda #ATTACK_FRAMES
    sta actTimer,x
    jsr ClearVelocity
    jsr SpawnSlash
    rts

@move:
    jsr ReadMoveDir             ; returns A = direction, or $FF for standing
    cmp #$FF
    bne @walking
    lda #ST_IDLE
    sta actState,x
    stz actAnim,x
    jsr ClearVelocity
    rts

@walking:
    sta actDir,x
    lda #ST_WALK
    sta actState,x
    jsr SetVelFull              ; leaves X as the actor's word offset
    rep #$30
    .a16
    .i16
    jsr TryMoveActor
    sep #$20
    .a8
    ldx curActor
    jsr AnimateWalk
    rts

    ;--- mid-swing: no movement, one active frame ---
@attacking:
    lda actTimer,x
    dec a
    sta actTimer,x
    cmp #ATK_ACTIVE
    bne @atk_end
    jsr DoAttackHit
    ldx curActor
@atk_end:
    lda actTimer,x
    bne @atk_done
    lda #ST_IDLE
    sta actState,x
@atk_done:
    rts

    ;--- knocked back ---
@hurt:
    lda actTimer,x
    dec a
    sta actTimer,x
    bne @hurt_move
    lda #ST_IDLE
    sta actState,x
@hurt_move:
    rep #$30
    .a16
    .i16
    lda curActor
    asl a
    tax
    jsr TryMoveActor
    ; bleed the knockback off
    lda actVX,x
    ASR1
    sta actVX,x
    lda actVY,x
    ASR1
    sta actVY,x
    sep #$20
    .a8
    ldx curActor
    rts
.endproc

;-----------------------------------------------------------------------------
; ReadMoveDir -- map the d-pad to one of eight facings.
; Out (A8): A = direction, or $FF when no direction is held.
;-----------------------------------------------------------------------------
.proc ReadMoveDir
    .a8
    .i16
    ldy #1                      ; horizontal: 0 = left, 1 = centre, 2 = right
    rep #$20
    .a16
    lda padHeld
    and #PAD_LEFT
    beq @noleft
    ldy #0
@noleft:
    lda padHeld
    and #PAD_RIGHT
    beq @noright
    ldy #2
@noright:
    sty tmp0

    ldy #1                      ; vertical: 0 = up, 1 = centre, 2 = down
    lda padHeld
    and #PAD_UP
    beq @noup
    ldy #0
@noup:
    lda padHeld
    and #PAD_DOWN
    beq @nodown
    ldy #2
@nodown:
    ; index = vertical * 3 + horizontal
    tya
    sta tmp1
    asl a
    clc
    adc tmp1                    ; vertical * 3
    clc
    adc tmp0                    ; + horizontal
    tay
    sep #$20
    .a8
    lda dirTable,y
    rts
.endproc

;-----------------------------------------------------------------------------
; SetVelFull / SetVelHalf -- load the per-direction velocity vector.
; In (A8/I16): X = actor index.  Out: X = actor word offset, A16 cleared.
;-----------------------------------------------------------------------------
.proc SetVelFull
    .a8
    .i16
    lda actDir,x
    rep #$20
    .a16
    and #$00FF
    asl a
    tay
    txa
    asl a
    tax
    lda dirVelX,y
    sta actVX,x
    lda dirVelY,y
    sta actVY,x
    sep #$20
    .a8
    rts
.endproc

.proc SetVelHalf
    .a8
    .i16
    lda actDir,x
    rep #$20
    .a16
    and #$00FF
    asl a
    tay
    txa
    asl a
    tax
    lda dirVelX,y
    ASR1
    sta actVX,x
    lda dirVelY,y
    ASR1
    sta actVY,x
    sep #$20
    .a8
    rts
.endproc

;-----------------------------------------------------------------------------
; ClearVelocity -- In (A8/I16): X = actor index.  Preserves X.
;-----------------------------------------------------------------------------
.proc ClearVelocity
    .a8
    .i16
    phx
    txa
    rep #$20
    .a16
    and #$00FF
    asl a
    tax
    stz actVX,x
    stz actVY,x
    sep #$20
    .a8
    plx
    rts
.endproc

;-----------------------------------------------------------------------------
; AnimateWalk -- In (A8/I16): X = actor index.
;-----------------------------------------------------------------------------
.proc AnimateWalk
    .a8
    .i16
    lda actAnimT,x
    beq @advance
    dec a
    sta actAnimT,x
    rts
@advance:
    lda #6
    sta actAnimT,x
    lda actAnim,x
    inc a
    and #$03
    sta actAnim,x
    rts
.endproc

;-----------------------------------------------------------------------------
; UpdateSoraFrame -- pick the animation frame and, if it changed, ask the NMI
; to stream it into the sprite page.  A8/I16.
;
; Only one 32x32 frame is ever resident in VRAM.  That is 512 bytes per change
; instead of the ~15 KiB a full resident sheet would need.
;-----------------------------------------------------------------------------
.proc UpdateSoraFrame
    .a8
    .i16
    lda playerIdx
    rep #$20
    .a16
    and #$00FF
    tax
    sep #$20
    .a8

    lda actType,x
    beq @out                    ; player is gone; nothing to stream

    ; facing -> drawn facing plus a mirror flag
    lda actDir,x
    rep #$20
    .a16
    and #$00FF
    tay
    sep #$20
    .a8
    lda drawFacing,y
    sta tmp0
    lda actFlags,x
    and #(~AF_HFLIP & $FF)
    sta actFlags,x
    lda drawFlip,y
    beq @noflip
    lda actFlags,x
    ora #AF_HFLIP
    sta actFlags,x
@noflip:

    ; frame within the facing
    lda actState,x
    cmp #ST_ATTACK
    bne @notatk
    lda actTimer,x
    cmp #10
    bcc @atk_late
    lda #4
    bra @haveframe
@atk_late:
    lda #5
    bra @haveframe
@notatk:
    cmp #ST_WALK
    bne @idle
    lda actAnim,x
    bra @haveframe
@idle:
    lda #0

@haveframe:
    ; frame index = facing * 6 + frame
    sta tmp1
    lda tmp0
    asl a                       ; *2
    sta tmp2
    asl a                       ; *4
    clc
    adc tmp2                    ; *6
    clc
    adc tmp1

    cmp soraFrameCur
    beq @out
    sta soraFrameCur

    rep #$20
    .a16
    and #$00FF
    ; byte offset = frame * 512
    asl a
    asl a
    asl a
    asl a
    asl a
    asl a
    asl a
    asl a
    asl a
    clc
    adc #.loword(soraChr)
    sta streamSrc
    sep #$20
    .a8
    lda #^soraChr
    sta streamBank
    lda #$01
    sta streamPend
@out:
    rts
.endproc

;-----------------------------------------------------------------------------
; SpawnSlash -- a short-lived keyblade arc in front of Sora.  A8/I16.
; In: X = Sora's actor index.
;-----------------------------------------------------------------------------
.proc SpawnSlash
    .a8
    .i16
    jsr AttackPoint             ; tmp0 / tmp1 = hit centre, Q12.4
    lda #ACT_SLASH
    jsr SpawnActor
    bcc @out
    lda #6
    sta actTimer,x
@out:
    ldx curActor
    rts
.endproc

;-----------------------------------------------------------------------------
; AttackPoint -- centre of Sora's swing, in world Q12.4.
; Out: tmp0 = X, tmp1 = Y.  A8/I16 in and out.
;-----------------------------------------------------------------------------
.proc AttackPoint
    .a8
    .i16
    lda playerIdx
    rep #$20
    .a16
    and #$00FF
    tax
    sep #$20
    .a8
    lda actDir,x
    rep #$20
    .a16
    and #$00FF
    asl a
    tay
    txa
    asl a
    tax
    lda actX,x
    clc
    adc atkOfsX,y
    sta tmp0
    lda actY,x
    clc
    adc atkOfsY,y
    sta tmp1
    sep #$20
    .a8
    rts
.endproc

;-----------------------------------------------------------------------------
; DoAttackHit -- damage every Heartless inside the swing.  A8/I16.
;-----------------------------------------------------------------------------
.proc DoAttackHit
    .a8
    .i16
    jsr AttackPoint             ; tmp0 / tmp1

    ldx #0
@loop:
    lda actType,x
    cmp #ACT_SHADOW
    bne @next
    phx
    jsr HeartlessInRange
    plx
    bcc @next
    phx
    jsr HurtHeartless
    plx
@next:
    inx
    cpx #MAX_ACTORS
    bcc @loop
    rts
.endproc

;-----------------------------------------------------------------------------
; HeartlessInRange -- In (A8/I16): X = actor index; tmp0/tmp1 = hit centre.
; Out: carry set when inside the swing.  Clobbers A, X, Y, tmp2, tmp3.
;-----------------------------------------------------------------------------
.proc HeartlessInRange
    .a8
    .i16
    txa
    rep #$20
    .a16
    and #$00FF
    asl a
    tax

    lda actX,x
    sec
    sbc tmp0
    bpl @xpos
    eor #$FFFF
    inc a
@xpos:
    lsr a                       ; the ground is squashed 2:1 horizontally
    cmp #ATK_REACH_X
    bcs @miss

    lda actY,x
    sec
    sbc tmp1
    bpl @ypos
    eor #$FFFF
    inc a
@ypos:
    cmp #ATK_REACH_Y
    bcs @miss

    sep #$20
    .a8
    sec
    rts
@miss:
    sep #$20
    .a8
    clc
    rts
.endproc

;-----------------------------------------------------------------------------
; HurtHeartless -- In (A8/I16): X = actor index.
;-----------------------------------------------------------------------------
.proc HurtHeartless
    .a8
    .i16
    lda actHP,x
    beq @kill
    dec a
    sta actHP,x
    beq @kill

    lda #14
    sta actHitT,x

    ; knock it away along Sora's facing
    phx
    lda playerIdx
    rep #$20
    .a16
    and #$00FF
    tax
    sep #$20
    .a8
    lda actDir,x
    rep #$20
    .a16
    and #$00FF
    asl a
    tay
    sep #$20
    .a8
    plx
    txa
    rep #$20
    .a16
    and #$00FF
    asl a
    tax
    lda dirVelX,y
    asl a
    sta actVX,x
    lda dirVelY,y
    asl a
    sta actVY,x
    sep #$20
    .a8

    lda #3
    sta hitStopTimer
    rts

@kill:
    stz actType,x
    lda #4
    sta hitStopTimer
    rts
.endproc

;-----------------------------------------------------------------------------
; UpdateHeartless -- In (A8/I16): X = actor index.
;-----------------------------------------------------------------------------
.proc UpdateHeartless
    .a8
    .i16
    stx curActor

    lda actHitT,x
    beq @think

    ; recoiling: coast on the knockback velocity and bleed it off
    dec a
    sta actHitT,x
    rep #$30
    .a16
    .i16
    lda curActor
    asl a
    tax
    jsr TryMoveActor
    lda actVX,x
    ASR1
    sta actVX,x
    lda actVY,x
    ASR1
    sta actVY,x
    sep #$20
    .a8
    ldx curActor
    bra @animate

@think:
    jsr HeartlessAimDir         ; A = direction, or $FF
    ldx curActor
    cmp #$FF
    beq @animate
    sta actDir,x
    jsr SetVelHalf              ; X becomes the word offset
    rep #$30
    .a16
    .i16
    jsr TryMoveActor
    sep #$20
    .a8
    ldx curActor
    jsr TouchPlayer
    ldx curActor

@animate:
    lda actAnimT,x
    beq @adv
    dec a
    sta actAnimT,x
    bra @tile
@adv:
    lda #10
    sta actAnimT,x
    lda actAnim,x
    inc a
    and #$03
    sta actAnim,x
@tile:
    ; frames sit two tiles apart in the sprite page
    lda actAnim,x
    asl a
    clc
    adc #TILE_HEART0
    sta actTile,x
    rts
.endproc

;-----------------------------------------------------------------------------
; HeartlessAimDir -- steer toward Sora with a deadzone on each axis, so the
; result snaps to the same eight facings the player uses.
; In (A8/I16): X = actor index.  Out: A = direction or $FF.
;-----------------------------------------------------------------------------
.proc HeartlessAimDir
    .a8
    .i16
    txa
    rep #$20
    .a16
    and #$00FF
    asl a
    tax                         ; this actor, word offset
    lda actX,x
    sta tmp2
    lda actY,x
    sta tmp3

    sep #$20
    .a8
    lda playerIdx
    rep #$20
    .a16
    and #$00FF
    asl a
    tax
    lda actX,x
    sec
    sbc tmp2
    sta tmp2                    ; dx toward Sora
    lda actY,x
    sec
    sbc tmp3
    sta tmp3                    ; dy toward Sora

    ; horizontal bucket
    ldy #1
    lda tmp2
    bmi @xneg
    cmp #HEART_DEAD_X
    bcc @xdone
    ldy #2
    bra @xdone
@xneg:
    eor #$FFFF
    inc a
    cmp #HEART_DEAD_X
    bcc @xdone
    ldy #0
@xdone:
    sty tmp4

    ; vertical bucket
    ldy #1
    lda tmp3
    bmi @yneg
    cmp #HEART_DEAD_Y
    bcc @ydone
    ldy #2
    bra @ydone
@yneg:
    eor #$FFFF
    inc a
    cmp #HEART_DEAD_Y
    bcc @ydone
    ldy #0
@ydone:
    ; index = vertical * 3 + horizontal
    tya
    sta tmp5
    asl a
    clc
    adc tmp5                    ; vertical * 3
    clc
    adc tmp4                    ; + horizontal
    tay
    sep #$20
    .a8
    lda dirTable,y
    rts
.endproc

;-----------------------------------------------------------------------------
; TouchPlayer -- contact damage.  In (A8/I16): X = Heartless index.
;-----------------------------------------------------------------------------
.proc TouchPlayer
    .a8
    .i16
    ; ignore contact while Sora is already reeling
    phx
    lda playerIdx
    rep #$20
    .a16
    and #$00FF
    tax
    sep #$20
    .a8
    lda actState,x
    cmp #ST_HURT
    beq @out_pop
    lda actType,x
    beq @out_pop

    rep #$20
    .a16
    txa
    asl a
    tax
    lda actX,x
    sta tmp0
    lda actY,x
    sta tmp1
    sep #$20
    .a8
    plx
    phx

    jsr HeartlessTouchTest
    bcc @out_pop
    plx
    jsr DamageSora
    rts

@out_pop:
    plx
    rts
.endproc

;-----------------------------------------------------------------------------
; HeartlessTouchTest -- In (A8/I16): X = Heartless index, tmp0/tmp1 = Sora.
; Out: carry set on overlap.
;-----------------------------------------------------------------------------
.proc HeartlessTouchTest
    .a8
    .i16
    txa
    rep #$20
    .a16
    and #$00FF
    asl a
    tax
    lda actX,x
    sec
    sbc tmp0
    bpl @xpos
    eor #$FFFF
    inc a
@xpos:
    lsr a
    cmp #TOUCH_X
    bcs @miss
    lda actY,x
    sec
    sbc tmp1
    bpl @ypos
    eor #$FFFF
    inc a
@ypos:
    cmp #TOUCH_Y
    bcs @miss
    sep #$20
    .a8
    sec
    rts
@miss:
    sep #$20
    .a8
    clc
    rts
.endproc

;-----------------------------------------------------------------------------
; DamageSora -- In (A8/I16): X = the Heartless that connected.
;-----------------------------------------------------------------------------
.proc DamageSora
    .a8
    .i16
    ; push Sora away along the Heartless's own heading
    lda actDir,x
    sta tmp6

    lda playerIdx
    rep #$20
    .a16
    and #$00FF
    tax
    sep #$20
    .a8

    lda actHP,x
    beq @nohp
    dec a
    sta actHP,x
@nohp:
    lda #ST_HURT
    sta actState,x
    lda #HURT_FRAMES
    sta actTimer,x

    rep #$20
    .a16
    lda tmp6
    and #$00FF
    asl a
    tay
    txa
    asl a
    tax
    lda dirVelX,y
    asl a
    sta actVX,x
    lda dirVelY,y
    asl a
    sta actVY,x
    sep #$20
    .a8

    lda #3
    sta hitStopTimer
    jsr HudUpdate
    rts
.endproc

;-----------------------------------------------------------------------------
; UpdateSlash -- In (A8/I16): X = actor index.
;-----------------------------------------------------------------------------
.proc UpdateSlash
    .a8
    .i16
    lda actTimer,x
    beq @die
    dec a
    sta actTimer,x
    cmp #3
    bcc @late
    lda #TILE_SLASH0
    sta actTile,x
    rts
@late:
    lda #TILE_SLASH1
    sta actTile,x
    rts
@die:
    stz actType,x
    rts
.endproc

;=============================================================================
; Tables
;=============================================================================
.segment "RODATA"

; Screen-space velocity per facing, Q12.4.  Horizontal steps are twice the
; vertical ones because the isometric ground plane is squashed 2:1, which
; keeps the apparent ground speed equal in all eight directions.
dirVelX:    .word   0,  34,  48,  34,   0, .loword(-34), .loword(-48), .loword(-34)
dirVelY:    .word  24,  17,   0, .loword(-17), .loword(-24), .loword(-17),   0,  17

; Offset from Sora to the centre of a swing, Q12.4.
atkOfsX:    .word   0, 352, 512, 352,   0, .loword(-352), .loword(-512), .loword(-352)
atkOfsY:    .word 256, 176,   0, .loword(-176), .loword(-256), .loword(-176),   0, 176

; (vertical bucket * 3 + horizontal bucket) -> facing; $FF means "standing".
dirTable:   .byte DIR_NW, DIR_N, DIR_NE
            .byte DIR_W,  $FF,   DIR_E
            .byte DIR_SW, DIR_S, DIR_SE

; Only five facings are drawn; the western three mirror the eastern three.
drawFacing: .byte 0, 1, 2, 3, 4, 3, 2, 1
drawFlip:   .byte 0, 0, 0, 0, 0, 1, 1, 1

;                    -      Sora        Heartless    Palm        BigRock      Rock        Slash
typeTile:   .byte $00, TILE_SORA,  TILE_HEART0, TILE_PALM,  TILE_ROCKBIG, TILE_ROCK,  TILE_SLASH0
            .byte TILE_PEDESTAL, TILE_SWORD, TILE_SHIELD, TILE_STAFF
typePal:    .byte $00, PAL_OBJ_SORA, PAL_OBJ_HEART, PAL_OBJ_SCENE, PAL_OBJ_SCENE, PAL_OBJ_SCENE, PAL_OBJ_FX
            .byte PAL_OBJ_DIVE, PAL_OBJ_DIVE, PAL_OBJ_DIVE, PAL_OBJ_DIVE
typeFlags:  .byte $00, AF_LARGE|AF_SHADOW, AF_SHADOW, AF_LARGE|AF_SHADOW, AF_LARGE|AF_SHADOW, AF_SHADOW, $00
            ; the weapons hover, so they cast no shadow of their own
            .byte AF_LARGE|AF_SHADOW, AF_LARGE|AF_TALK, AF_LARGE|AF_TALK, AF_LARGE|AF_TALK
typeHP:     .byte $00, SORA_MAX_HP, HEART_MAX_HP, $00, $00, $00, $00
            .byte $00, $00, $00, $00

; type, isometric i, isometric j -- terminated by $FF
spawnTable:
    .byte ACT_SORA,     7,  8
    .byte ACT_PALM,     6,  4
    .byte ACT_PALM,    10,  5
    .byte ACT_PALM,     4,  7
    .byte ACT_PALM,     7,  9
    .byte ACT_ROCKBIG,  6,  6
    .byte ACT_ROCKBIG,  8,  8
    .byte ACT_ROCK,     7, 10
    .byte ACT_SHADOW,   9,  6
    .byte ACT_SHADOW,   5,  9
    .byte ACT_SHADOW,   8,  5
    .byte $FF
