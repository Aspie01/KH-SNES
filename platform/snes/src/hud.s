;=============================================================================
; hud.s -- HP gauges on BG3
;
; BG3 is the 2bpp layer and, with the mode-1 priority bit set, it draws above
; everything else -- which is exactly what a HUD wants.  Two tilemap rows ever
; change: Sora's gauge and, while a boss is alive, its own.  An update costs a
; single 128-byte DMA.
;=============================================================================
.p816
.include "snes.inc"
.include "game.inc"
.include "ram.inc"
.include "macros.inc"
.include "text.inc"

.import NightStageLabel, TownStageLabel

.export HudInit, HudUpdate

CH_H         = CH_A + 'H' - 'A'
CH_P         = CH_A + 'P' - 'A'
HUD_ATTR     = TXT_ATTR

; Each gauge cell is worth two points, so these counts have to match the
; maximum HP values in game.inc.
HP_BAR_X       = 5              ; first gauge cell of Sora's row
HP_BAR_CELLS   = 10             ; 10 cells * 2 = SORA_MAX_HP

BOSS_ROW       = 64             ; byte offset of the second row within hudRow
QUEST_ROW2     = 128            ; ...and the third, which day two needs
BOSS_LABEL_X   = 1
BOSS_BAR_X     = 11
BOSS_BAR_CELLS = 18             ; 18 cells * 2 = DS_MAX_HP
; The Guard Armor has a longer name and the same length of fight, so its bar
; starts further along the row.  Both are runtime values, not the constants
; the cell writes used to be built from.
ARMOR_BAR_X    = 13             ; caps at 12 and 31: the row exactly
ARMOR_BAR_CELLS = 18            ; 18 cells * 2 = GA_MAX_HP

; The second row does double duty: a boss gauge in the Dive, and Kairi's
; checklist on the island.  Offsets are into the label string, which starts
; at column QUEST_X.  Day two needs five counts, so it spills onto a third.
QUEST_X        = 1
QUEST_LOGS     = 5
QUEST_CLOTH    = 16
QUEST_ROPE     = 26
D2_FISH        = 5
D2_MUSH        = 21
D2_NUT         = 5
D2_EGG         = 14
D2_WATER       = 25
RACE_COUNT     = 6              ; where the countdown digit sits

.segment "CODE"

;-----------------------------------------------------------------------------
; HudInit -- clear the BG3 tilemap.  Must run during forced blank.  A8/I16.
;-----------------------------------------------------------------------------
.proc HudInit
    .a8
    .i16
    lda #$80
    sta VMAIN
    ldx #VRAM_BG3_MAP
    stx VMADDL

    rep #$20
    .a16
    lda #(HUD_ATTR | CH_CLEAR)
    ldx #0
@clear:
    sta VMDATAL                 ; a 16-bit store feeds $2118 then $2119
    inx
    cpx #1024
    bcc @clear
    sep #$20
    .a8

    jsr HudUpdate
    rts
.endproc

;-----------------------------------------------------------------------------
; DrawGauge -- fill a run of cells from a hit-point total.
; In (A16/I16): tmp0 = byte offset of the first cell inside hudRow,
;               tmp1 = number of cells, tmp4 = remaining HP.
; Clobbers A, X, Y, tmp5, tmp6.
;-----------------------------------------------------------------------------
.proc DrawGauge
    .a16
    .i16
    ldx #0
@cell:
    txa
    asl a
    sta tmp5                    ; HP already accounted for by earlier cells
    lda tmp4
    sec
    sbc tmp5                    ; HP left for this one
    bmi @empty
    beq @empty
    cmp #2
    bcs @full
    lda #(HUD_ATTR | CH_BAR_HALF)
    bra @put
@full:
    lda #(HUD_ATTR | CH_BAR_FULL)
    bra @put
@empty:
    lda #(HUD_ATTR | CH_BAR_EMPTY)
@put:
    sta tmp6
    txa
    asl a
    clc
    adc tmp0
    tay
    lda tmp6
    sta hudRow,y

    inx
    cpx tmp1
    bcc @cell
    rts
.endproc

;-----------------------------------------------------------------------------
; HudUpdate -- rebuild both gauge rows and flag them for upload.  A8/I16.
;-----------------------------------------------------------------------------
.proc HudUpdate
    .a8
    .i16
    rep #$30
    .a16
    .i16

    ;--- blank every row ---
    lda #(HUD_ATTR | CH_CLEAR)
    ldx #0
@blank:
    sta hudRow,x
    inx
    inx
    cpx #192
    bcc @blank

    ;--- Sora ---
    sep #$20
    .a8
    lda playerIdx
    rep #$20
    .a16
    and #$00FF
    tax
    sep #$20
    .a8
    lda actType,x
    beq @boss                   ; no player yet: leave the row blank
    lda actHP,x
    rep #$20
    .a16
    and #$00FF
    sta tmp4

    lda #(HUD_ATTR | CH_H)
    sta hudRow + 2 * 2
    lda #(HUD_ATTR | CH_P)
    sta hudRow + 3 * 2
    lda #(HUD_ATTR | CH_BAR_L)
    sta hudRow + (HP_BAR_X - 1) * 2
    lda #(HUD_ATTR | CH_BAR_R)
    sta hudRow + (HP_BAR_X + HP_BAR_CELLS) * 2

    lda #(HP_BAR_X * 2)
    sta tmp0
    lda #HP_BAR_CELLS
    sta tmp1
    jsr DrawGauge

    ;--- the second row: Kairi's list on the island, a boss gauge below ---
@boss:
    sep #$20
    .a8
    lda sceneId
    cmp #SCENE_TOWN1
    bcc :+
    jmp @townrow
:   cmp #SCENE_NIGHT
    beq @nightrow
    cmp #SCENE_ISLAND
    beq :+
    jmp @bossgauge
:   lda questState
    beq @toFlag                 ; she has not asked yet
    cmp #Q_RACE_SET
    bcc @list                   ; the checklist, until the race takes over
    jsr DrawRace
    bra @toFlag
@list:
    cmp #Q_DAYOUT
    bcs @toFlag                 ; the day is turning over
    jsr DrawQuest
    bra @toFlag

@nightrow:
    jsr DrawNight

    ; The boss gauge is long enough now that the end of the row is out of a
    ; short branch's reach from up here.
@toFlag:
    jmp @flag

    ; The town's row is the objective until something with a gauge turns up.
@townrow:
    lda bossHP
    bne @bossgauge
    jsr DrawTown
    bra @toFlag

@bossgauge:
    lda bossHP
    bne :+
    jmp @flag
:   rep #$20
    .a16
    and #$00FF
    sta tmp4

    ; Which boss, and therefore where its bar starts.  Only Traverse Town has
    ; a second one, so the scene decides.
    sep #$20
    .a8
    lda sceneId
    cmp #SCENE_TOWN1
    bcs @armor
    rep #$20
    .a16
    lda #.loword(bossName)
    sta tmp9
    lda #BOSS_BAR_X
    sta tmp8
    lda #BOSS_BAR_CELLS
    sta tmp3
    bra @named
@armor:
    rep #$20
    .a16
    lda #.loword(armorName)
    sta tmp9
    lda #ARMOR_BAR_X
    sta tmp8
    lda #ARMOR_BAR_CELLS
    sta tmp3

@named:
    ldy #0
@label:
    sep #$20
    .a8
    lda (tmp9),y
    cmp #$FF
    beq @caps
    rep #$20
    .a16
    and #$00FF
    ora #HUD_ATTR
    sta tmp6
    tya
    asl a
    clc
    adc #(BOSS_ROW + BOSS_LABEL_X * 2)
    tax
    lda tmp6
    sta hudRow,x
    iny
    bra @label

@caps:
    rep #$20
    .a16
    lda tmp8                    ; one cell before the bar
    dec a
    asl a
    clc
    adc #BOSS_ROW
    tax
    lda #(HUD_ATTR | CH_BAR_L)
    sta hudRow,x
    lda tmp8                    ; ...and one after it
    clc
    adc tmp3
    asl a
    clc
    adc #BOSS_ROW
    tax
    lda #(HUD_ATTR | CH_BAR_R)
    sta hudRow,x

    lda tmp8
    asl a
    clc
    adc #BOSS_ROW
    sta tmp0
    lda tmp3
    sta tmp1
    jsr DrawGauge

@flag:
    sep #$20
    .a8
    lda #$01
    sta hudDirty
    rts
.endproc

;-----------------------------------------------------------------------------
; DrawQuest -- Kairi's raft checklist along the second HUD row.  A8/I16.
;-----------------------------------------------------------------------------
.proc DrawQuest
    .a8
    .i16
    lda questDay
    cmp #2
    beq @day2

    rep #$20
    .a16
    lda #.loword(questLabel)
    sta tmp7
    lda #BOSS_ROW
    sta tmp8
    sep #$20
    .a8
    jsr PutLabel
    lda itemCount + IT_LOG
    ldx #QUEST_LOGS
    jsr PutDigit
    lda itemCount + IT_CLOTH
    ldx #QUEST_CLOTH
    jsr PutDigit
    lda itemCount + IT_ROPE
    ldx #QUEST_ROPE
    jsr PutDigit
    rts

@day2:
    rep #$20
    .a16
    lda #.loword(quest2Label)
    sta tmp7
    lda #BOSS_ROW
    sta tmp8
    sep #$20
    .a8
    jsr PutLabel
    lda itemCount + IT_FISH
    ldx #D2_FISH
    jsr PutDigit
    lda itemCount + IT_MUSH
    ldx #D2_MUSH
    jsr PutDigit

    rep #$20
    .a16
    lda #.loword(quest3Label)
    sta tmp7
    lda #QUEST_ROW2
    sta tmp8
    sep #$20
    .a8
    jsr PutLabel
    lda itemCount + IT_NUT
    ldx #D2_NUT
    jsr PutDigit
    lda itemCount + IT_EGG
    ldx #D2_EGG
    jsr PutDigit
    lda itemCount + IT_WATER
    ldx #D2_WATER
    jsr PutDigit
    rts
.endproc

;-----------------------------------------------------------------------------
; DrawNight -- the one thing worth doing, while the island falls.  A8/I16.
;-----------------------------------------------------------------------------
.proc DrawNight
    .a8
    .i16
    jsr NightStageLabel
    cmp #$FF
    beq @out                    ; mid-conversation: leave the row empty
    rep #$20
    .a16
    and #$00FF
    asl a
    tax
    lda nightLines,x
    sta tmp7
    lda #BOSS_ROW
    sta tmp8
    sep #$20
    .a8
    jmp PutLabel
@out:
    rts
.endproc

;-----------------------------------------------------------------------------
; DrawTown -- which district Traverse Town wants him in.  A8/I16.
;-----------------------------------------------------------------------------
.proc DrawTown
    .a8
    .i16
    jsr TownStageLabel
    cmp #$FF
    beq @out                    ; mid-cutscene: leave the row empty
    rep #$20
    .a16
    and #$00FF
    asl a
    tax
    lda townLines,x
    sta tmp7
    lda #BOSS_ROW
    sta tmp8
    sep #$20
    .a8
    jmp PutLabel
@out:
    rts
.endproc

;-----------------------------------------------------------------------------
; DrawRace -- the countdown, then whichever half of the course is left.
; A8/I16.
;-----------------------------------------------------------------------------
.proc DrawRace
    .a8
    .i16
    rep #$20
    .a16
    lda #BOSS_ROW
    sta tmp8
    sep #$20
    .a8

    lda questState
    cmp #Q_RACE_SET
    beq @count
    cmp #Q_RACE_RUN
    bne @done                   ; the result is in the dialogue box, not here

    rep #$20
    .a16
    lda #.loword(raceOut)
    sta tmp7
    sep #$20
    .a8
    lda raceLeg
    beq :+
    rep #$20
    .a16
    lda #.loword(raceBack)
    sta tmp7
    sep #$20
    .a8
:   jmp PutLabel

@count:
    rep #$20
    .a16
    lda #.loword(raceSet)
    sta tmp7
    sep #$20
    .a8
    jsr PutLabel
    ; COUNT_LEN is a multiple of 64, so rounding up gives three, two, one.
    lda dayTimer
    clc
    adc #63
    lsr a
    lsr a
    lsr a
    lsr a
    lsr a
    lsr a
    ldx #RACE_COUNT
    jmp PutDigit

@done:
    rts
.endproc

;-----------------------------------------------------------------------------
; PutLabel -- lay a $FF-terminated run of font tiles into a HUD row.
; In (A8/I16): tmp7 = the run's address in this bank, tmp8 = row byte offset.
;-----------------------------------------------------------------------------
.proc PutLabel
    .a8
    .i16
    ldy #0
@loop:
    lda (tmp7),y
    cmp #$FF
    beq @done
    rep #$20
    .a16
    and #$00FF
    ora #HUD_ATTR
    sta tmp6
    tya
    asl a
    clc
    adc tmp8
    clc
    adc #(QUEST_X * 2)
    tax
    lda tmp6
    sta hudRow,x
    sep #$20
    .a8
    iny
    bra @loop
@done:
    rts
.endproc

;-----------------------------------------------------------------------------
; PutDigit -- one count into the checklist.
; In (A8/I16): A = value 0-9, X = offset along the label, tmp8 = row.
;-----------------------------------------------------------------------------
.proc PutDigit
    .a8
    .i16
    clc
    adc #CH_0
    rep #$20
    .a16
    and #$00FF
    ora #HUD_ATTR
    sta tmp6
    txa
    asl a
    clc
    adc tmp8
    clc
    adc #(QUEST_X * 2)
    tay
    lda tmp6
    sta hudRow,y
    sep #$20
    .a8
    rts
.endproc

.segment "RODATA"
; Stored as font tile numbers rather than ASCII: the label never changes, so
; running it through the conversion table at runtime would buy nothing.
questLabel:
    TXTSTR "LOGS 0/2  CLOTH 0/1  ROPE 0/1"
quest2Label:
    TXTSTR "FISH 0/3   MUSHROOMS 0/3"
quest3Label:
    TXTSTR "NUTS 0/2  EGG 0/1  WATER 0/1"
raceSet:
    TXTSTR "READY 0"
raceOut:
    TXTSTR "RACE   TAG THE PAOPU TREE"
raceBack:
    TXTSTR "RACE   BACK TO KAIRI"

; The night, indexed by what NightStageLabel returns.
nightLines:
    .word .loword(nightRiku), .loword(nightCave)
nightRiku:
    TXTSTR "FIND RIKU"
nightCave:
    TXTSTR "THE SECRET PLACE"

; Traverse Town, indexed by what TownStageLabel returns.
townLines:
    .word .loword(townCid), .loword(townSecond), .loword(townThird)
townCid:
    TXTSTR "FIND SOMEBODY AWAKE"
townSecond:
    TXTSTR "THE SECOND DISTRICT"
townThird:
    TXTSTR "THE THIRD DISTRICT"

; Boss names.  The gauge writes these itself rather than going through
; PutLabel, because it has to know where the name ends to put the bar after
; it -- and a space is CH_BLANK, which is zero, so the run ends on $FF like
; every other one here rather than on the first gap in the name.
bossName:
    TXTSTR "DARKSIDE"
armorName:
    TXTSTR "GUARD ARMOR"
