;=============================================================================
; world.s -- actor spawning, Sora, Heartless, and the combat loop
;=============================================================================
.p816
.include "snes.inc"
.include "game.inc"
.include "ram.inc"
.include "macros.inc"

.import TryMoveActor, TileToWorld, TileHeight
.import UpdateRiku
.import HudUpdate
.import TextBusy
.import soraChr

.export InitWorld, UpdateWorld, SpawnActor, ClearActors, CountType, SetActorZ
.export SpawnTable, PlayerPos, NearPlayer
; Exported only so the scene scripts can count it against MAX_ACTORS.
.export spawnTable, spawnTableEnd

;--- attack tuning -----------------------------------------------------------
ATK_ACTIVE   = 12               ; timer value on which the swing connects
; The swing is generous on purpose: the arc is centred just off Sora's body
; rather than out at arm's length, so an enemy pressed right up against him is
; still inside it.
ATK_REACH_X  = 272              ; Q12.4 half-extents of the arc's hit box
ATK_REACH_Y  = 272
TOUCH_X      = 160
TOUCH_Y      = 160
HEART_DEAD_X = 208              ; AI deadzone, Q12.4
HEART_DEAD_Y = 208
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

    ; tile coordinates -> world pixels
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
    jsr TileToWorld

    ; TileToWorld hands back whole pixels; actors are Q12.4.
    lda tmp0
    asl a
    asl a
    asl a
    asl a
    sta tmp0
    lda tmp1
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
; ClearActors -- empty the table so a new scene starts from nothing.  A8/I16.
;-----------------------------------------------------------------------------
.proc ClearActors
    .a8
    .i16
    ldx #0
@loop:
    stz actType,x
    stz actFlags,x
    stz actHitT,x
    stz actState,x
    stz actTimer,x
    inx
    cpx #MAX_ACTORS
    bcc @loop
    stz playerIdx
    lda #$FF
    sta soraFrameCur            ; force the next cel upload
    stz hitStopTimer
    rts
.endproc

;-----------------------------------------------------------------------------
; CountType -- In (A8/I16): A = type.  Out: A = how many are alive.
;-----------------------------------------------------------------------------
.proc CountType
    .a8
    .i16
    sta tmp2
    ldy #0
    ldx #0
@loop:
    lda actType,x
    cmp tmp2
    bne @next
    iny
@next:
    inx
    cpx #MAX_ACTORS
    bcc @loop
    tya
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
    lda tmp2
    cmp #ACT_SHADOW
    bne :+
    lda heartTile               ; which cut of the Shadow this scene loaded
    sta actTile,x
:   lda tmp4
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
    jsr SetActorZ               ; clobbers tmp0-tmp4, all of which are spent
    sec
    rts
.endproc

;-----------------------------------------------------------------------------
; SetActorZ -- read the ground height under an actor and remember it, so the
; renderer can lift the sprite and the next step has something to compare to.
; In (A8/I16): X = actor index.  Clobbers A, Y, tmp0-tmp4.  X is preserved.
;-----------------------------------------------------------------------------
.proc SetActorZ
    .a8
    .i16
    lda actFlags,x
    and #AF_FLAT
    beq :+
    stz actZ,x                  ; hung on a wall, not standing on anything
    rts
:   phx
    txa
    rep #$20
    .a16
    and #$00FF
    asl a
    tax
    lda actX,x
    lsr a
    lsr a
    lsr a
    lsr a
    sta tmp0
    lda actY,x
    lsr a
    lsr a
    lsr a
    lsr a
    sta tmp1
    jsr TileHeight
    sep #$20
    .a8
    plx
    sta actZ,x
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
    cmp #ACT_DARKSIDE
    bne @notBoss
    phx
    jsr UpdateDarkside
    plx
    bra @next
@notBoss:
    cmp #ACT_ORB
    bne @notOrb
    phx
    jsr UpdateOrb
    plx
    bra @next
@notOrb:
    cmp #ACT_MOTE
    bne @notMote
    phx
    jsr UpdateMote
    plx
    bra @next
@notMote:
    cmp #ACT_FISH
    bne @notFish
    phx
    jsr UpdateFish
    plx
    bra @next
@notFish:
    cmp #ACT_RIKU
    bne @notRiku
    phx
    jsr UpdateRiku
    plx
    bra @next
@notRiku:
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
    cmp #ST_DEAD
    bne :+
    jmp @dying
:   cmp #ST_FALL
    bne :+
    jmp @falling
:   cmp #ST_ATTACK
    beq @attacking
    cmp #ST_HURT
    bne :+
    jmp @hurt
:

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

    ;--- the scene is carrying him: no control, and a slow tumble ---
@falling:
    jsr ClearVelocity
    ldx curActor
    lda actAnimT,x
    beq @turn
    dec a
    sta actAnimT,x
    rts
@turn:
    lda #8
    sta actAnimT,x
    lda actDir,x
    inc a
    and #$07
    sta actDir,x                ; UpdateSoraFrame will stream the new facing
    rts

    ;--- out of HP: no control, and the screen goes down with him ---
@dying:
    lda actTimer,x
    beq @down
    dec a
    sta actTimer,x
    lsr a
    lsr a                       ; 50 frames -> brightness 12 down to 0
    cmp #3
    bcs :+
    lda #3                      ; stop short of black so GAME OVER can be read
:   sta screenBright
    rts
@down:
    lda deadFlag
    bne :+                      ; already handed over to the GAME OVER script
    lda #$01
    sta deadFlag
:   rts

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
    cmp #ACT_DARKSIDE
    bne @notBoss
    phx
    jsr BossInRange
    plx
    bcc @next
    phx
    jsr HurtBoss
    plx
    bra @next
@notBoss:
    cmp #ACT_SHADOW
    bne @next
    ldy keyGot
    beq @next                   ; a wooden sword goes straight through them
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
; BossInRange / HurtBoss -- In (A8/I16): X = actor index.
;-----------------------------------------------------------------------------
.proc BossInRange
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
    bpl :+
    eor #$FFFF
    inc a
:   cmp #DS_HURT_X
    bcs @miss
    lda actY,x
    sec
    sbc tmp1
    bpl :+
    eor #$FFFF
    inc a
:   cmp #DS_HURT_Y
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

.proc HurtBoss
    .a8
    .i16
    lda actHitT,x
    bne @out                    ; still flinching from the last hit
    lda actHP,x
    beq @out
    dec a
    sta actHP,x
    sta bossHP
    lda #10
    sta actHitT,x
    lda #3
    sta hitStopTimer
    ; HudUpdate reloads X with the player, so settle the boss's fate first.
    lda actHP,x
    bne @alive
    stz actType,x               ; defeated
@alive:
    jsr HudUpdate
@out:
    rts
.endproc

;-----------------------------------------------------------------------------
; UpdateDarkside -- In (A8/I16): X = actor index.
;
; It never walks.  It alternates two attacks: a fist that telegraphs, marks
; where Sora is standing and lands there a beat later, and a volley of three
; dark orbs spat from the hole in its chest.  Both are dodged by moving, which
; is the only thing a stationary boss can ask of the player.
;-----------------------------------------------------------------------------
.proc UpdateDarkside
    .a8
    .i16
    stx curActor

    lda actHitT,x
    beq :+
    dec a
    sta actHitT,x
:
    lda actState,x
    cmp #DSS_SLAM_UP
    beq @slamUp
    cmp #DSS_SLAM_HIT
    beq @slamHit
    cmp #DSS_ORB_UP
    beq @orbUp
    ; The later states sit past a short branch's reach, so they go via jmp.
    cmp #DSS_ORB_FIRE
    bne :+
    jmp @orbFire
:   cmp #DSS_SWEEP_UP
    bne :+
    jmp @sweepUp
:   cmp #DSS_SWEEP_HIT
    bne :+
    jmp @sweepHit
:

    ;--- resting ---
    lda actTimer,x
    beq @choose
    dec a
    sta actTimer,x
    rts
@choose:
    ; Standing underneath is answered immediately, ahead of the alternation.
    jsr PlayerUnderBoss
    ldx curActor
    bcc @alternate
    lda #DSS_SWEEP_UP
    sta actState,x
    lda #DS_SWEEP_WIND
    sta actTimer,x
    rts

@alternate:
    lda actAnim,x
    eor #$01                    ; alternate fist / orbs
    sta actAnim,x
    beq @beginOrbs
    jsr AimAtPlayer
    ldx curActor
    lda #DSS_SLAM_UP
    sta actState,x
    lda #DS_SLAM_WIND
    sta actTimer,x
    rts
@beginOrbs:
    lda #DSS_ORB_UP
    sta actState,x
    lda #DS_ORB_WIND
    sta actTimer,x
    rts

    ;--- fist raised ---
@slamUp:
    lda actTimer,x
    beq @slamNow
    dec a
    sta actTimer,x
    rts
@slamNow:
    jsr DarksideSlam
    ldx curActor
    lda #DSS_SLAM_HIT
    sta actState,x
    lda #DS_SLAM_HOLD
    sta actTimer,x
    rts

@slamHit:
    lda actTimer,x
    beq @rest
    dec a
    sta actTimer,x
    rts

    ;--- chest gathering ---
@orbUp:
    lda actTimer,x
    beq @fireNow
    dec a
    sta actTimer,x
    rts
@fireNow:
    jsr FireOrbs
    ldx curActor
    lda #DSS_ORB_FIRE
    sta actState,x
    lda #DS_ORB_REST
    sta actTimer,x
    rts

@orbFire:
    lda actTimer,x
    beq @rest
    dec a
    sta actTimer,x
    rts

    ;--- arm drawn back ---
@sweepUp:
    lda actTimer,x
    beq @sweepNow
    dec a
    sta actTimer,x
    rts
@sweepNow:
    jsr DarksideSweep
    ldx curActor
    lda #DSS_SWEEP_HIT
    sta actState,x
    lda #DS_SWEEP_HOLD
    sta actTimer,x
    rts

@sweepHit:
    lda actTimer,x
    beq @rest
    dec a
    sta actTimer,x
    rts

@rest:
    stz actState,x
    lda #DS_REST
    sta actTimer,x
    rts
.endproc

;-----------------------------------------------------------------------------
; PlayerUnderBoss -- is Sora inside the sweep's reach?
; In (A8/I16): X = boss index.  Out: carry set when he is.  Clobbers X.
;-----------------------------------------------------------------------------
.proc PlayerUnderBoss
    .a8
    .i16
    txa
    rep #$20
    .a16
    and #$00FF
    asl a
    tax
    lda actX,x
    sta tmp0
    lda actY,x
    sta tmp1

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
    sbc tmp0
    bpl :+
    eor #$FFFF
    inc a
:
    cmp #SWEEP_X
    bcs @no
    lda actY,x
    sec
    sbc tmp1
    bpl :+
    eor #$FFFF
    inc a
:   cmp #SWEEP_Y
    bcs @no
    sep #$20
    .a8
    sec
    rts
@no:
    sep #$20
    .a8
    clc
    rts
.endproc

;-----------------------------------------------------------------------------
; DarksideSweep -- the arm comes across the ground at its feet.  A8/I16.
;-----------------------------------------------------------------------------
.proc DarksideSweep
    .a8
    .i16
    ; three arcs across the front of the boss, as the tell that it connected
    lda curActor
    rep #$20
    .a16
    and #$00FF
    asl a
    tax
    lda actX,x
    sta tmp8
    lda actY,x
    clc
    adc #160                    ; 10 px in front of its feet
    sta tmp9
    sep #$20
    .a8

    ldy #0
@arc:
    sty tmp7
    rep #$20
    .a16
    lda tmp8
    clc
    adc sweepOfs,y
    sta tmp0
    lda tmp9
    sta tmp1
    sep #$20
    .a8
    lda #ACT_SLASH
    jsr SpawnActor
    bcc @next
    lda #8
    sta actTimer,x
@next:
    ldy tmp7
    iny
    iny
    cpy #6
    bcc @arc

    ; anyone still underneath takes it
    ldx curActor
    jsr PlayerUnderBoss
    bcc @out
    ldx curActor
    jsr DamageSora
@out:
    rts
.endproc

;-----------------------------------------------------------------------------
; FireOrbs -- three orbs from the chest, aimed at Sora with a spread.  A8/I16.
;-----------------------------------------------------------------------------
.proc FireOrbs
    .a8
    .i16
    ; the hole in the chest, about 40 px above the boss's feet
    lda curActor
    rep #$20
    .a16
    and #$00FF
    asl a
    tax
    lda actX,x
    sta tmp8
    lda actY,x
    sec
    sbc #640                    ; 40 px in Q12.4
    sta tmp9

    ; direction toward Sora, snapped to the same eight facings the player uses
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
    sbc tmp8
    sta tmp2
    lda actY,x
    sec
    sbc tmp9
    sta tmp3

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
    cmp #$FF
    bne :+
    lda #DIR_S                  ; directly underneath: fire downward
:   sta tmp7

    ; centre orb, then one either side
    lda tmp7
    sec
    sbc #1
    and #$07
    jsr SpawnOrb
    lda tmp7
    jsr SpawnOrb
    lda tmp7
    clc
    adc #1
    and #$07
    jsr SpawnOrb
    rts
.endproc

;-----------------------------------------------------------------------------
; SpawnOrb -- In (A8/I16): A = facing; tmp8/tmp9 = the muzzle position.
;-----------------------------------------------------------------------------
.proc SpawnOrb
    .a8
    .i16
    pha                         ; SpawnActor reuses tmp2-tmp6
    rep #$20
    .a16
    lda tmp8
    sta tmp0
    lda tmp9
    sta tmp1
    sep #$20
    .a8
    lda #ACT_ORB
    jsr SpawnActor
    pla                         ; PLA leaves carry alone, so the test survives
    bcc @out                    ; actor table full
    sta actDir,x
    lda #ORB_LIFE
    sta actTimer,x

    lda actDir,x                ; A was overwritten above; reload the facing
    rep #$20
    .a16
    and #$00FF
    asl a
    tay                         ; Y = facing * 2
    txa
    asl a
    tax                         ; X = word offset
    lda dirVelX,y
    asl a                       ; an orb outruns a walk
    sta actVX,x
    lda dirVelY,y
    asl a
    sta actVY,x
    sep #$20
    .a8
@out:
    rts
.endproc

;-----------------------------------------------------------------------------
; UpdateOrb -- In (A8/I16): X = actor index.  Orbs ignore the ground, so they
; carry on out over the void until they burn out.
;-----------------------------------------------------------------------------
.proc UpdateOrb
    .a8
    .i16
    stx curActor
    lda actTimer,x
    bne @alive
    stz actType,x
    rts
@alive:
    dec a
    sta actTimer,x

    lda actAnimT,x
    bne @tick
    lda #5
    sta actAnimT,x
    lda actAnim,x
    eor #$01
    sta actAnim,x
    asl a
    clc
    adc #TILE_ORB
    sta actTile,x
    bra @move
@tick:
    dec a
    sta actAnimT,x

@move:
    rep #$30
    .a16
    .i16
    lda curActor
    asl a
    tax
    lda actX,x
    clc
    adc actVX,x
    sta actX,x
    lda actY,x
    clc
    adc actVY,x
    sta actY,x
    sep #$20
    .a8
    ldx curActor
    jsr OrbHitPlayer
    rts
.endproc

;-----------------------------------------------------------------------------
; UpdateMote -- In (A8/I16): X = actor index.
;
; The specks of light that streak past Sora while he falls.  They are pure
; decoration: no collision, no ground, they just drift on their velocity until
; the timer runs out.
;-----------------------------------------------------------------------------
.proc UpdateMote
    .a8
    .i16
    stx curActor
    lda actTimer,x
    bne @alive
    stz actType,x
    rts
@alive:
    dec a
    sta actTimer,x

    ; Flicker between the two streak cels so the fall reads as motion even
    ; where a mote's own drift is slow.
    lda actAnimT,x
    bne @tick
    lda #3
    sta actAnimT,x
    lda actAnim,x
    eor #$01
    sta actAnim,x
    asl a
    clc
    adc #TILE_STREAK
    sta actTile,x
    bra @move
@tick:
    dec a
    sta actAnimT,x

@move:
    rep #$30
    .a16
    .i16
    lda curActor
    asl a
    tax
    lda actX,x
    clc
    adc actVX,x
    sta actX,x
    lda actY,x
    clc
    adc actVY,x
    sta actY,x
    sep #$20
    .a8
    ldx curActor
    rts
.endproc

;-----------------------------------------------------------------------------
; UpdateFish -- In (A8/I16): X = actor index.
;
; A fish is scenery that has to look alive: it swims a few pixels one way, a
; few back, and flicks its tail.  It ignores the ground entirely -- the whole
; point is that it is out in water Sora cannot walk on.
;-----------------------------------------------------------------------------
.proc UpdateFish
    .a8
    .i16
    stx curActor
    inc actTimer,x

    lda actTimer,x
    and #$07
    bne @swim
    lda actAnim,x
    eor #$01
    sta actAnim,x
    asl a
    clc
    adc #TILE_FISH
    sta actTile,x

@swim:
    ; Half of a 64-frame cycle each way, with the sprite turned to match.
    ;
    ; Both directions do their flag work in eight-bit mode and pick an index,
    ; and the single sixteen-bit block that follows is the only one.  Written
    ; the obvious way -- a rep #$20 at the end of each branch -- the assembler
    ; carries .a16 across the label the other branch lands on, and emits its
    ; immediates three bytes wide for a CPU that is still eight-bit there.
    lda actTimer,x
    and #$20
    bne @west
    lda actFlags,x
    and #<(~AF_HFLIP)
    sta actFlags,x
    ldy #0
    bra @apply
@west:
    lda actFlags,x
    ora #AF_HFLIP
    sta actFlags,x
    ldy #2
@apply:
    rep #$20
    .a16
    lda fishVel,y
    sta tmp0
    lda curActor
    asl a
    tax
    lda actX,x
    clc
    adc tmp0
    sta actX,x
    sep #$20
    .a8
    ldx curActor
    rts
.endproc

;-----------------------------------------------------------------------------
; OrbHitPlayer -- In (A8/I16): X = orb index.
;-----------------------------------------------------------------------------
.proc OrbHitPlayer
    .a8
    .i16
    phx
    lda playerIdx
    rep #$20
    .a16
    and #$00FF
    tax
    sep #$20
    .a8
    lda actType,x
    beq @out
    lda actState,x
    cmp #ST_HURT
    beq @out                    ; already reeling

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
    jsr HeartlessTouchTest      ; the same overlap test; nothing Shadow-specific
    bcc @out
    plx
    phx
    jsr DamageSora              ; X is the orb, whose facing drives the knockback
    plx
    stz actType,x               ; the orb bursts
    rts
@out:
    plx
    rts
.endproc

;-----------------------------------------------------------------------------
; AimAtPlayer -- park Sora's position in the boss's unused velocity slots.
; In (A8/I16): X = boss index.
;-----------------------------------------------------------------------------
.proc AimAtPlayer
    .a8
    .i16
    lda playerIdx
    rep #$20
    .a16
    and #$00FF
    asl a
    tax
    lda actX,x
    sta tmp0
    lda actY,x
    sta tmp1
    sep #$20
    .a8
    lda curActor
    rep #$20
    .a16
    and #$00FF
    asl a
    tax
    lda tmp0
    sta actVX,x
    lda tmp1
    sta actVY,x
    sep #$20
    .a8
    rts
.endproc

;-----------------------------------------------------------------------------
; DarksideSlam -- the fist lands on the marked spot.  A8/I16.
;-----------------------------------------------------------------------------
.proc DarksideSlam
    .a8
    .i16
    lda curActor
    rep #$20
    .a16
    and #$00FF
    asl a
    tax
    lda actVX,x
    sta tmp0                    ; where the fist comes down
    lda actVY,x
    sta tmp1
    sep #$20
    .a8

    ; A Shadow crawls out of the impact.
    lda #ACT_SHADOW
    jsr SpawnActor

    ; Anyone still standing there takes the hit.
    lda playerIdx
    rep #$20
    .a16
    and #$00FF
    asl a
    tax
    lda actX,x
    sec
    sbc tmp0
    bpl :+
    eor #$FFFF
    inc a
:   cmp #TOUCH_X
    bcs @clear
    lda actY,x
    sec
    sbc tmp1
    bpl :+
    eor #$FFFF
    inc a
:   cmp #TOUCH_Y
    bcs @clear
    sep #$20
    .a8
    ldx curActor
    jsr DamageSora
    rts
@clear:
    sep #$20
    .a8
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
    lda keyGot
    beq :+                      ; before the Keyblade they cannot reach him
    jsr TouchPlayer
    ldx curActor
:

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
    ; frames sit two tiles apart in the sprite page, from whichever base this
    ; scene's copy of the art starts at
    lda actAnim,x
    asl a
    clc
    adc heartTile
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

    lda actState,x
    cmp #ST_DEAD
    beq @out                    ; already down; nothing more to take
    lda actHP,x
    beq @kill
    dec a
    sta actHP,x
    bne @reel
@kill:
    lda #ST_DEAD
    sta actState,x
    lda #DEATH_FRAMES
    sta actTimer,x
    bra @knock
@reel:
    lda #ST_HURT
    sta actState,x
    lda #HURT_FRAMES
    sta actTimer,x
@knock:

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
@out:
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
; Shared by every scene's script
;=============================================================================

;-----------------------------------------------------------------------------
; SpawnTable -- walk a table of (type, tile i, tile j) triples until
; $FF.  In (A8/I16): tmp8 = the table's address in this bank.
;-----------------------------------------------------------------------------
.proc SpawnTable
    .a8
    .i16
    ldy #0
@loop:
    lda (tmp8),y
    cmp #$FF
    beq @done
    sta tmp6                    ; type
    iny
    lda (tmp8),y
    sta tmp4                    ; i
    iny
    lda (tmp8),y
    sta tmp5                    ; j
    iny
    sty tmp7                    ; SpawnActor clobbers Y, so park the cursor

    rep #$20
    .a16
    lda tmp4
    and #$00FF
    sta tmp0
    lda tmp5
    and #$00FF
    sta tmp1
    jsr TileToWorld
    ; TileToWorld hands back whole pixels; actors are Q12.4.
    lda tmp0
    asl a
    asl a
    asl a
    asl a
    sta tmp0
    lda tmp1
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
    rts
.endproc

;-----------------------------------------------------------------------------
; PlayerPos -- Sora's world position into tmp0/tmp1.  A8/I16.
;-----------------------------------------------------------------------------
.proc PlayerPos
    .a8
    .i16
    lda playerIdx
    rep #$20
    .a16
    and #$00FF
    asl a
    tax
    lda actX,x
    sta tmp0
    lda actY,x
    sta tmp1
    sep #$20
    .a8
    rts
.endproc

;-----------------------------------------------------------------------------
; NearPlayer -- is this actor within tmp2 by tmp3 of Sora?
; In (A8/I16): X = actor index, tmp0/tmp1 = Sora's position,
;              tmp2 = X range, tmp3 = Y range (both Q12.4)
; Out: carry set on a hit.  X is preserved.
;-----------------------------------------------------------------------------
.proc NearPlayer
    .a8
    .i16
    phx
    txa
    rep #$20
    .a16
    and #$00FF
    asl a
    tax

    lda actX,x
    sec
    sbc tmp0
    bpl :+
    eor #$FFFF
    inc a
:
    cmp tmp2
    bcs @miss

    lda actY,x
    sec
    sbc tmp1
    bpl :+
    eor #$FFFF
    inc a
:   cmp tmp3
    bcs @miss

    sep #$20
    .a8
    plx
    sec
    rts

@miss:
    sep #$20
    .a8
    plx
    clc
    rts
.endproc

;=============================================================================
; Tables
;=============================================================================
.segment "RODATA"

; Velocity per facing, Q12.4.  The ground is square, so both axes carry
; WALK_SPEED and a diagonal is that scaled by 1/sqrt(2) -- which keeps the
; apparent speed equal in all eight directions.
dirVelX:    .word   0,  17,  24,  17,   0, .loword(-17), .loword(-24), .loword(-17)
dirVelY:    .word  24,  17,   0, .loword(-17), .loword(-24), .loword(-17),   0,  17

; Offset from Sora to the centre of a swing, Q12.4 -- one tile ahead of him.
atkOfsX:    .word   0, 176, 256, 176,   0, .loword(-176), .loword(-256), .loword(-176)
atkOfsY:    .word 256, 176,   0, .loword(-176), .loword(-256), .loword(-176), 0, 176

; Which way a fish is drifting, Q12.4.
fishVel:    .word FISH_SWIM, .loword(-FISH_SWIM)

; Screen offsets of the three arcs the sweep leaves behind, Q12.4.
sweepOfs:   .word .loword(-384), 0, 384

; (vertical bucket * 3 + horizontal bucket) -> facing; $FF means "standing".
dirTable:   .byte DIR_NW, DIR_N, DIR_NE
            .byte DIR_W,  $FF,   DIR_E
            .byte DIR_SW, DIR_S, DIR_SE

; Only five facings are drawn; the western three mirror the eastern three.
drawFacing: .byte 0, 1, 2, 3, 4, 3, 2, 1
drawFlip:   .byte 0, 0, 0, 0, 0, 1, 1, 1

;                    -      Sora        Heartless    Palm        BigRock      Rock        Slash
typeTile:   .byte $00, TILE_SORA,  TILE_HEART0, TILE_PALM,  TILE_ROCKBIG, TILE_ROCK,  TILE_SLASH0
            .byte TILE_PEDESTAL, TILE_SWORD, TILE_SHIELD, TILE_STAFF, TILE_DARKSIDE
            .byte TILE_ORB
            .byte TILE_STREAK
            ; the islanders and the raft materials, on sprite page one
            .byte TILE_KAIRI, TILE_RIKU, TILE_TIDUS, TILE_SELPHIE, TILE_WAKKA
            .byte TILE_LOG, TILE_CLOTH, TILE_ROPE, TILE_MUSH, TILE_COCONUT
            .byte TILE_EGG, TILE_BOTTLE, TILE_FISH, TILE_PALM
            .byte TILE_DOOR, TILE_FACES, TILE_SCRIBBLE
            ; the night
            .byte TILE_DOOROPEN, TILE_DARK
typeTileEnd:
typePal:    .byte $00, PAL_OBJ_SORA, PAL_OBJ_HEART, PAL_OBJ_SCENE, PAL_OBJ_SCENE, PAL_OBJ_SCENE, PAL_OBJ_FX
            .byte PAL_OBJ_DIVE, PAL_OBJ_DIVE, PAL_OBJ_DIVE, PAL_OBJ_DIVE, PAL_OBJ_HEART
            .byte PAL_OBJ_HEART
            .byte PAL_OBJ_FX
            .byte PAL_OBJ_ISLE, PAL_OBJ_ISLE, PAL_OBJ_ISLE, PAL_OBJ_ISLE
            .byte PAL_OBJ_ISLE
            .byte PAL_OBJ_ISLE, PAL_OBJ_ISLE, PAL_OBJ_ISLE, PAL_OBJ_ISLE
            .byte PAL_OBJ_ISLE, PAL_OBJ_ISLE, PAL_OBJ_ISLE, PAL_OBJ_ISLE
            ; a coconut palm is drawn with the ordinary palm's art
            .byte PAL_OBJ_SCENE
            .byte PAL_OBJ_ISLE, PAL_OBJ_ISLE, PAL_OBJ_ISLE
            .byte PAL_OBJ_ISLE, PAL_OBJ_ISLE
typePalEnd:
typeFlags:  .byte $00, AF_LARGE|AF_SHADOW, AF_SHADOW, AF_LARGE|AF_SHADOW, AF_LARGE|AF_SHADOW, AF_SHADOW, $00
            ; the weapons hover, so they cast no shadow of their own
            .byte AF_LARGE|AF_SHADOW, AF_LARGE|AF_TALK, AF_LARGE|AF_TALK, AF_LARGE|AF_TALK
            ; the boss is drawn by EmitBoss, so it carries no sprite flags of
            ; its own -- only its shadow.  Orbs float, so no shadow either.
            .byte AF_SHADOW
            .byte $00
            ; motes are pure light, so no shadow
            .byte $00
            ; the islanders stand their ground, so they are solid as well as
            ; talkable; the materials are small and just lie there
            .byte AF_LARGE|AF_SHADOW|AF_TALK|AF_PAGE1
            .byte AF_LARGE|AF_SHADOW|AF_TALK|AF_PAGE1
            .byte AF_LARGE|AF_SHADOW|AF_TALK|AF_PAGE1
            .byte AF_LARGE|AF_SHADOW|AF_TALK|AF_PAGE1
            .byte AF_LARGE|AF_SHADOW|AF_TALK|AF_PAGE1
            .byte AF_SHADOW|AF_PAGE1, AF_SHADOW|AF_PAGE1, AF_SHADOW|AF_PAGE1
            .byte AF_SHADOW|AF_PAGE1, AF_SHADOW|AF_PAGE1
            .byte AF_SHADOW|AF_PAGE1, AF_SHADOW|AF_PAGE1
            ; fish float in the shallows, so no ground shadow
            .byte AF_PAGE1
            .byte AF_LARGE|AF_SHADOW
            ; hung on the cave wall: no shadow, and no ground under them
            .byte AF_LARGE|AF_TALK|AF_PAGE1|AF_FLAT
            .byte AF_LARGE|AF_TALK|AF_PAGE1|AF_FLAT
            .byte AF_LARGE|AF_TALK|AF_PAGE1|AF_FLAT
            ; the open door hangs on the same wall; the darkness stands on the
            ; ground and throws no shadow of its own
            .byte AF_LARGE|AF_TALK|AF_PAGE1|AF_FLAT
            .byte AF_LARGE|AF_PAGE1
typeFlagsEnd:
typeHP:     .byte $00, SORA_MAX_HP, HEART_MAX_HP, $00, $00, $00, $00
            .byte $00, $00, $00, $00, DS_MAX_HP
            .byte $00
            .byte $00
            .byte $00, $00, $00, $00, $00
            .byte $00, $00, $00, $00
            .byte $00, $00, $00, $00
            .byte $00, $00, $00, $00
            .byte $00, $00

typeHPEnd:

; A type added to game.inc without a row in every table would spawn with
; whatever byte happens to follow, so make the assembler check.
.assert (typeTileEnd  - typeTile)  = (ACT_DARK + 1), error, "typeTile"
.assert (typePalEnd   - typePal)   = (ACT_DARK + 1), error, "typePal"
.assert (typeFlagsEnd - typeFlags) = (ACT_DARK + 1), error, "typeFlags"
.assert (typeHPEnd    - typeHP)    = (ACT_DARK + 1), error, "typeHP"

; type, tile i, tile j -- terminated by $FF
; Sora wakes on the sand. No Heartless: they arrive the night the island
; falls, which is a later state of this same map.
spawnTable:
    .byte ACT_SORA,    11, 12        ; Kairi is standing right over him
    .byte ACT_PALM,    15,  4        ; the tree the treehouse is built round
    .byte ACT_PALM,     6,  7
    .byte ACT_PALM,    16, 10
    .byte ACT_PALM,    27,  7        ; the paopu tree, out on the small island
    .byte ACT_PALMC,    9, 10        ; the two by Kairi still carry coconuts
    .byte ACT_PALMC,   13, 10
    .byte ACT_ROCKBIG, 15,  9
    .byte ACT_ROCK,     8, 12
    ; the five islanders
    .byte ACT_KAIRI,   12, 12        ; down by the water
    .byte ACT_RIKU,    27,  8        ; the small island past the bridge
    .byte ACT_TIDUS,    7,  4        ; up on the lookout platform
    .byte ACT_SELPHIE, 14, 13        ; out on the dock
    .byte ACT_WAKKA,   19, 12        ; across the little footbridge
    ; the back wall of the Secret Place
    .byte ACT_FACES,    1,  6
    .byte ACT_DOOR,     2,  6
    .byte ACT_SCRIBBLE, 3,  6
    .byte $FF
spawnTableEnd:
