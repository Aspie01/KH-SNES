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

.import SpawnActor, IsoToWorld
.import TextOpen, TextBusy, TextClose

.export DiveInit, DiveUpdate, DiveSpawnShadows

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

    ldy #0
@loop:
    lda diveSpawns,y
    cmp #$FF
    beq @done
    sta tmp6
    iny
    rep #$20
    .a16
    lda diveSpawns,y
    and #$00FF
    sta tmp0
    iny
    lda diveSpawns,y
    and #$00FF
    sta tmp1
    iny
    sty tmp7
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
    lda tmp6
    jsr SpawnActor
    ldy tmp7
    bra @loop
@done:
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
    bne @playable
    ; the opening message has been dismissed: the pedestals are now live
    lda #DIVE_PICK
    sta diveStage
    rts

@playable:
    cmp #DIVE_FIGHT
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
    ldx pendActor
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
    ldx pendActor
    stz actType,x
    lda #DIVE_FIGHT
    sta diveStage
    jsr DiveSpawnShadows
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

;-----------------------------------------------------------------------------
; DiveSpawnShadows -- the first Heartless rise from the glass.  A8/I16.
;-----------------------------------------------------------------------------
.proc DiveSpawnShadows
    .a8
    .i16
    ldy #0
@loop:
    lda shadowSpawns,y
    cmp #$FF
    beq @done
    iny
    rep #$20
    .a16
    lda shadowSpawns,y
    and #$00FF
    sta tmp0
    iny
    lda shadowSpawns,y
    and #$00FF
    sta tmp1
    iny
    sty tmp7
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
    lda #ACT_SHADOW
    jsr SpawnActor
    ldy tmp7
    bra @loop
@done:
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

shadowSpawns:
    .byte 0,  5,  5
    .byte 0, 11,  7
    .byte 0,  6, 11
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
