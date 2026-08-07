;=============================================================================
; dive.s -- Station of Awakening
;
; The opening scene: Sora stands on a stained-glass platform, a voice speaks,
; and three pedestals offer the Dream Sword, Shield and Rod.  One power is
; taken and one is given up; then the first Shadows appear.
;=============================================================================
.p816
.include "snes.inc"
.include "game.inc"
.include "ram.inc"
.include "macros.inc"
.include "text.inc"

.import SpawnActor, IsoToWorld, ClearActors, CountType, InitWorld
.import LoadScene
.import TextOpen, TextBusy, TextClose
.import HudUpdate

.export DiveInit, DiveUpdate

REACH_X = 320                   ; interaction range, Q12.4 (un-squashed X)
REACH_Y = 260

.segment "CODE"

;-----------------------------------------------------------------------------
; DiveInit -- place Sora and the three pedestals.  A8/I16.
;-----------------------------------------------------------------------------
.proc DiveInit
    .a8
    .i16
    stz diveStage
    lda #$FF
    sta soraFrameCur            ; force the first cel upload
    stz hitStopTimer
    stz weaponTaken
    stz weaponGiven
    stz pendActor
    stz pendWeapon
    stz scriptWait

    rep #$20
    .a16
    lda #.loword(diveSpawns)
    sta tmp8
    sep #$20
    .a8
    jsr SpawnFromTable
    jsr HudUpdate               ; the player exists now, so the gauge can fill
    ; open on the voice
    lda #<scriptIntro
    sta txtPtr
    lda #>scriptIntro
    sta txtPtr+1
    lda #^scriptIntro
    sta txtPtr+2
    lda #TM_MESSAGE
    jsr TextOpen
    rts
.endproc


;-----------------------------------------------------------------------------
; SpawnFromTable -- walk a table of (type, isometric i, isometric j) triples
; until $FF.  In (A8/I16): tmp8 = the table's address in this bank.
;-----------------------------------------------------------------------------
.proc SpawnFromTable
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
    rts
.endproc

;-----------------------------------------------------------------------------
; DiveUpdate -- one frame of the scene's script.  A8/I16.
;-----------------------------------------------------------------------------
.proc DiveUpdate
    .a8
    .i16
    jsr TextBusy
    bcc @idle
    rts                         ; a box is up; nothing else happens

@idle:
    ; A prompt that just closed leaves its answer in txtResult.
    lda txtResult
    beq @noanswer
    jsr HandleAnswer
    stz txtResult
    rts

@noanswer:
    lda diveStage
    cmp #DIVE_INTRO
    bne :+
    ; the opening message has been dismissed: the pedestals are now live
    lda #DIVE_PICK
    sta diveStage
    rts
:   cmp #DIVE_SHATTER
    bne :+
    jmp Shatter
:   cmp #DIVE_S2_INTRO
    bne :+
    lda #DIVE_S2_FIGHT
    sta diveStage
    rts
:   cmp #DIVE_S2_FIGHT
    bne :+
    jmp WatchShadows
:   cmp #DIVE_BOSS
    bne :+
    jmp WatchBoss
:   cmp #DIVE_DONE
    bne :+
    jmp BeginFade               ; the victory line has been dismissed
:   cmp #DIVE_FADE
    bne :+
    jmp FadeOut
:   cmp #DIVE_ARRIVED
    beq @out

    ; PICK or DROP: pressing A next to a weapon asks about it
    rep #$20
    .a16
    lda padPressed
    and #PAD_A
    sep #$20
    .a8
    beq @out
    jsr FindWeapon
    bcc @out
    jsr AskAbout
@out:
    rts
.endproc

;-----------------------------------------------------------------------------
; Shatter -- the platform comes apart underfoot.
;
; Done with hardware rather than art: MOSAIC coarsens BG1 into ever larger
; blocks while the brightness falls and the screen shakes, which reads as the
; glass breaking up without needing a second tileset for the debris.  A8/I16.
;-----------------------------------------------------------------------------
.proc Shatter
    .a8
    .i16
    lda shatterTimer
    beq @land
    dec shatterTimer

    ; elapsed = SHATTER_LEN - remaining, scaled to the 0-15 the registers take
    lda #SHATTER_LEN
    sec
    sbc shatterTimer
    lsr a
    lsr a
    lsr a                       ; /8 -> 0..12
    cmp #16
    bcc :+
    lda #15
:   sta tmp2

    asl a
    asl a
    asl a
    asl a
    ora #$01                    ; mosaic size in the high nibble, BG1 enabled
    sta mosaicAmt

    lda #15
    sec
    sbc tmp2
    sta screenBright

    ; a shake that alternates either side of centre
    lda frameCount
    and #$02
    beq :+
    lda #$FE                    ; -2
    bra :++
:   lda #$02
:   sta shakeX
    rts

@land:
    stz shakeX
    stz mosaicAmt
    lda #$8F                    ; forced blank while VRAM is rewritten
    sta screenBright
    sta INIDISP

    lda #SCENE_DIVE2
    jsr LoadScene
    jsr ClearActors
    jsr SpawnStation2

    lda #$0F
    sta screenBright
    lda #DIVE_S2_INTRO
    sta diveStage

    lda #<scriptStation2
    sta txtPtr
    lda #>scriptStation2
    sta txtPtr+1
    lda #^scriptStation2
    sta txtPtr+2
    lda #TM_MESSAGE
    jsr TextOpen
    rts
.endproc

;-----------------------------------------------------------------------------
; WatchShadows -- once the last Shadow is gone, Darkside rises.  A8/I16.
;-----------------------------------------------------------------------------
.proc WatchShadows
    .a8
    .i16
    lda #ACT_SHADOW
    jsr CountType
    bne @out

    jsr SpawnBoss
    lda #DIVE_BOSS
    sta diveStage
    lda #<scriptBoss
    sta txtPtr
    lda #>scriptBoss
    sta txtPtr+1
    lda #^scriptBoss
    sta txtPtr+2
    lda #TM_MESSAGE
    jsr TextOpen
@out:
    rts
.endproc

;-----------------------------------------------------------------------------
; WatchBoss -- A8/I16.
;-----------------------------------------------------------------------------
.proc WatchBoss
    .a8
    .i16
    lda #ACT_DARKSIDE
    jsr CountType
    bne @out

    stz bossHP
    lda #DIVE_DONE
    sta diveStage
    lda #<scriptVictory
    sta txtPtr
    lda #>scriptVictory
    sta txtPtr+1
    lda #^scriptVictory
    sta txtPtr+2
    lda #TM_MESSAGE
    jsr TextOpen
@out:
    rts
.endproc

;-----------------------------------------------------------------------------
; BeginFade -- arm the whiteout that carries Sora off the platform.  A8/I16.
;
; Colour math switches from the half-add that draws shadows to a plain add
; against the fixed colour, so ramping COLDATA washes every layer to white.
;-----------------------------------------------------------------------------
.proc BeginFade
    .a8
    .i16
    lda #DIVE_FADE
    sta diveStage
    lda #FADE_LEN
    sta fadeTimer
    stz coldataAmt
    stz CGWSEL                  ; second operand is the fixed colour
    lda #$3F                    ; add, no halving, backdrop + OBJ + every BG
    sta CGADSUB
    rts
.endproc

;-----------------------------------------------------------------------------
; FadeOut -- white out, swap to Destiny Islands at the midpoint, fade back in.
; A8/I16.
;-----------------------------------------------------------------------------
.proc FadeOut
    .a8
    .i16
    lda fadeTimer
    beq @finish
    dec fadeTimer

    lda fadeTimer
    cmp #(FADE_LEN / 2)
    beq @swap
    bcc @backIn

    ; first half: 0 -> 31 as the timer falls from FADE_LEN to 32
    lda #FADE_LEN
    sec
    sbc fadeTimer
    sta coldataAmt
    rts

@backIn:
    ; second half: the timer itself is already 31 down to 0
    lda fadeTimer
    sta coldataAmt
    rts

@swap:
    ; Fully white, so nothing of the swap is visible.
    lda #$FF
    sta coldataAmt              ; clamped to 31 by the register's five bits
    lda #$8F
    sta screenBright
    sta INIDISP

    lda #SCENE_ISLAND
    jsr LoadScene
    jsr ClearActors
    jsr InitWorld

    lda #$0F
    sta screenBright
    rts

@finish:
    stz coldataAmt
    lda #CGWSEL_VAL             ; back to the shadow set-up
    sta CGWSEL
    lda #CGADSUB_VAL
    sta CGADSUB

    lda #DIVE_ARRIVED
    sta diveStage
    lda #<scriptWake
    sta txtPtr
    lda #>scriptWake
    sta txtPtr+1
    lda #^scriptWake
    sta txtPtr+2
    lda #TM_MESSAGE
    jsr TextOpen
    rts
.endproc

;-----------------------------------------------------------------------------
; SpawnStation2 / SpawnBoss -- A8/I16.
;-----------------------------------------------------------------------------
.proc SpawnStation2
    .a8
    .i16
    rep #$20
    .a16
    lda #.loword(station2Spawns)
    sta tmp8
    sep #$20
    .a8
    jsr SpawnFromTable
    jsr HudUpdate
    rts
.endproc

.proc SpawnBoss
    .a8
    .i16
    rep #$20
    .a16
    lda #8                      ; isometric (8,3): the far side of the platform
    sta tmp0
    lda #3
    sta tmp1
    jsr IsoToWorld
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
    lda #ACT_DARKSIDE
    jsr SpawnActor
    lda #DS_MAX_HP
    sta bossHP
    lda #DS_REST
    sta actTimer,x
    jsr HudUpdate
    rts
.endproc

;-----------------------------------------------------------------------------
; FindWeapon -- nearest dream weapon within reach of Sora.
; Out: carry set and pendActor / pendWeapon filled in.  A8/I16.
;-----------------------------------------------------------------------------
.proc FindWeapon
    .a8
    .i16
    ; Sora's position
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

    ldx #0
@scan:
    lda actType,x
    cmp #ACT_SWORD
    bcc @next
    cmp #ACT_STAFF + 1
    bcs @next

    stx tmp4                    ; candidate actor
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
:   lsr a                       ; the ground is squashed 2:1 across X
    cmp #REACH_X
    bcs @miss
    lda actY,x
    sec
    sbc tmp1
    bpl :+
    eor #$FFFF
    inc a
:   cmp #REACH_Y
    bcs @miss
    sep #$20
    .a8
    plx
    ; hit
    lda tmp4
    sta pendActor
    lda actType,x
    sec
    sbc #(ACT_SWORD - WEAPON_SWORD)
    sta pendWeapon
    sec
    rts
@miss:
    sep #$20
    .a8
    plx
@next:
    inx
    cpx #MAX_ACTORS
    bcc @scan
    clc
    rts
.endproc

;-----------------------------------------------------------------------------
; AskAbout -- describe the weapon in pendWeapon and ask about it.  A8/I16.
;-----------------------------------------------------------------------------
.proc AskAbout
    .a8
    .i16
    lda pendWeapon
    dec a                       ; WEAPON_SWORD(1) -> 0
    rep #$20
    .a16
    and #$00FF
    tax                         ; the description tables are byte-wide
    sep #$20
    .a8

    lda diveStage
    cmp #DIVE_DROP
    beq @drop
    lda descTakeLo,x
    sta txtPtr
    lda descTakeHi,x
    sta txtPtr+1
    bra @go
@drop:
    lda descDropLo,x
    sta txtPtr
    lda descDropHi,x
    sta txtPtr+1
@go:
    lda #^scriptIntro           ; every script shares one bank
    sta txtPtr+2
    lda #TM_PROMPT
    jsr TextOpen
    rts
.endproc

;-----------------------------------------------------------------------------
; HandleAnswer -- act on the yes/no the player just gave.  A8/I16.
;-----------------------------------------------------------------------------
.proc HandleAnswer
    .a8
    .i16
    lda txtResult
    cmp #1                      ; 1 = yes, 2 = no
    beq @yes
    rts

@yes:
    lda diveStage
    cmp #DIVE_DROP
    beq @drop

    ;--- taking a power ---
    lda pendWeapon
    sta weaponTaken
    lda pendActor
    rep #$20
    .a16
    and #$00FF
    tax
    sep #$20
    .a8
    stz actType,x               ; the weapon leaves its pedestal
    lda #DIVE_DROP
    sta diveStage
    lda #<scriptGiveUp
    sta txtPtr
    lda #>scriptGiveUp
    sta txtPtr+1
    lda #^scriptGiveUp
    sta txtPtr+2
    lda #TM_MESSAGE
    jsr TextOpen
    rts

    ;--- giving one up ---
@drop:
    lda pendWeapon
    sta weaponGiven
    lda pendActor
    rep #$20
    .a16
    and #$00FF
    tax
    sep #$20
    .a8
    stz actType,x
    lda #DIVE_SHATTER
    sta diveStage
    lda #SHATTER_LEN
    sta shatterTimer
    lda #<scriptChosen
    sta txtPtr
    lda #>scriptChosen
    sta txtPtr+1
    lda #^scriptChosen
    sta txtPtr+2
    lda #TM_MESSAGE
    jsr TextOpen
    rts
.endproc

;=============================================================================
; Scene data
;=============================================================================
.segment "RODATA"

; type, isometric i, isometric j -- the platform centre is (7,7)/(8,8)
diveSpawns:
    .byte ACT_SORA,      8, 10
    .byte ACT_PEDESTAL,  4,  8
    .byte ACT_SWORD,     4,  8
    .byte ACT_PEDESTAL, 10,  4
    .byte ACT_SHIELD,   10,  4
    .byte ACT_PEDESTAL, 10, 10
    .byte ACT_STAFF,    10, 10
    .byte $FF

; Station two: Sora lands alone, and three Shadows are already waiting.
station2Spawns:
    .byte ACT_SORA,    8, 10
    .byte ACT_SHADOW,  5,  6
    .byte ACT_SHADOW, 11,  6
    .byte ACT_SHADOW,  8,  4
    .byte $FF

;--- scripts.  SC_NL breaks a line, SC_PAGE waits and clears, SC_END ends. ---
scriptIntro:
    .byte "SO MUCH TO DO,", SC_NL
    .byte "SO LITTLE TIME.", SC_NL
    .byte SC_NL
    .byte "TAKE YOUR TIME.", SC_PAGE
    .byte "DON'T BE AFRAID.", SC_NL
    .byte SC_NL
    .byte "THE DOOR IS STILL SHUT.", SC_PAGE
    .byte "POWER SLEEPS WITHIN YOU.", SC_NL
    .byte "GIVE IT FORM, AND IT WILL", SC_NL
    .byte "GIVE YOU STRENGTH.", SC_NL
    .byte SC_NL
    .byte "CHOOSE WELL.", SC_END

scriptGiveUp:
    .byte "YOUR PATH IS SET.", SC_NL
    .byte SC_NL
    .byte "NOW, WHAT WILL YOU", SC_NL
    .byte "GIVE UP IN EXCHANGE?", SC_END

scriptStation2:
    .byte "YOU GOT IT.", SC_NL
    .byte SC_NL
    .byte "THE CLOSER YOU GET TO", SC_NL
    .byte "THE LIGHT, THE GREATER", SC_NL
    .byte "YOUR SHADOW BECOMES.", SC_END

scriptBoss:
    .byte "BUT DON'T BE AFRAID.", SC_PAGE
    .byte "YOUR SHADOW HAS RISEN.", SC_NL
    .byte SC_NL
    .byte "IT WILL NOT BE BEATEN", SC_NL
    .byte "BY RUNNING FROM IT.", SC_END

scriptWake:
    .byte "DESTINY ISLANDS.", SC_PAGE
    .byte "SORA!", SC_NL
    .byte SC_NL
    .byte "ARE YOU DREAMING AGAIN?", SC_END

scriptVictory:
    .byte "THE DOOR IS OPENING.", SC_PAGE
    .byte "YOUR ADVENTURE BEGINS", SC_NL
    .byte "AT DAWN.", SC_END

scriptChosen:
    .byte "YOU HAVE CHOSEN.", SC_NL
    .byte SC_NL
    .byte "YOUR ADVENTURE BEGINS.", SC_PAGE
    .byte "BUT REMEMBER...", SC_NL
    .byte SC_NL
    .byte "THE CLOSER YOU GET TO", SC_NL
    .byte "THE LIGHT, THE GREATER", SC_NL
    .byte "YOUR SHADOW BECOMES.", SC_END

;--- weapon descriptions, asked as yes/no prompts ---------------------------
descSwordTake:
    .byte "THE POWER OF THE WARRIOR.", SC_NL
    .byte "INVINCIBLE COURAGE.", SC_NL
    .byte "IS THIS THE POWER YOU SEEK?", SC_END
descShieldTake:
    .byte "THE POWER OF THE GUARDIAN.", SC_NL
    .byte "KINDNESS TO AID FRIENDS.", SC_NL
    .byte "IS THIS THE POWER YOU SEEK?", SC_END
descStaffTake:
    .byte "THE POWER OF THE MYSTIC.", SC_NL
    .byte "INNER STRENGTH.", SC_NL
    .byte "IS THIS THE POWER YOU SEEK?", SC_END

descSwordDrop:
    .byte "THE POWER OF THE WARRIOR.", SC_NL
    .byte "INVINCIBLE COURAGE.", SC_NL
    .byte "GIVE UP THIS POWER?", SC_END
descShieldDrop:
    .byte "THE POWER OF THE GUARDIAN.", SC_NL
    .byte "KINDNESS TO AID FRIENDS.", SC_NL
    .byte "GIVE UP THIS POWER?", SC_END
descStaffDrop:
    .byte "THE POWER OF THE MYSTIC.", SC_NL
    .byte "INNER STRENGTH.", SC_NL
    .byte "GIVE UP THIS POWER?", SC_END

descTakeLo: .byte <descSwordTake, <descShieldTake, <descStaffTake
descTakeHi: .byte >descSwordTake, >descShieldTake, >descStaffTake
descDropLo: .byte <descSwordDrop, <descShieldDrop, <descStaffDrop
descDropHi: .byte >descSwordDrop, >descShieldDrop, >descStaffDrop
