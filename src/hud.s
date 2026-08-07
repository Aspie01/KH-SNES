;=============================================================================
; hud.s -- HP gauge on BG3
;
; BG3 is the 2bpp layer and, with the mode-1 priority bit set, it draws above
; everything else -- which is exactly what a HUD wants.  Only one 32-entry
; tilemap row ever changes, so an update costs a single 64-byte DMA.
;=============================================================================
.p816
.include "snes.inc"
.include "game.inc"
.include "ram.inc"
.include "macros.inc"

.export HudInit, HudUpdate

; BG3 tilemap entry: vhopppcc cccccccc.  Bit 13 is the priority bit that lifts
; the tile onto the high BG3 layer; bits 12-10 pick the palette, and 4 is the
; one loaded with the HUD's own colours.
HUD_ATTR     = $3000

; Font tile numbers, matching the order tools/art_hud.py emits.
CH_BLANK     = 0
CH_A         = 1                ; letters run A..Z from here
CH_0         = 27               ; digits run 0..9 from here
CH_SLASH     = 37
CH_BAR_L     = 40               ; gauge end cap, left
CH_BAR_FULL  = 41
CH_BAR_HALF  = 42
CH_BAR_EMPTY = 43
CH_BAR_R     = 44               ; gauge end cap, right

CH_H         = CH_A + 'H' - 'A'
CH_P         = CH_A + 'P' - 'A'

HP_BAR_X     = 5                ; column of the first gauge cell
HP_BAR_CELLS = 10               ; each cell is worth two HP

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
    lda #(HUD_ATTR | CH_BLANK)
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
; HudUpdate -- rebuild the gauge row from Sora's HP and flag it for upload.
; A8/I16.
;-----------------------------------------------------------------------------
.proc HudUpdate
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
    beq @flag                   ; no player yet: leave the row alone
    lda actHP,x
    rep #$20
    .a16
    and #$00FF
    sta tmp4                    ; remaining HP

    ;--- blank the row ---
    lda #(HUD_ATTR | CH_BLANK)
    ldx #0
@blank:
    sta hudRow,x
    inx
    inx
    cpx #64
    bcc @blank

    ;--- label and end caps ---
    lda #(HUD_ATTR | CH_H)
    sta hudRow + 2 * 2
    lda #(HUD_ATTR | CH_P)
    sta hudRow + 3 * 2
    lda #(HUD_ATTR | CH_BAR_L)
    sta hudRow + (HP_BAR_X - 1) * 2
    lda #(HUD_ATTR | CH_BAR_R)
    sta hudRow + (HP_BAR_X + HP_BAR_CELLS) * 2

    ;--- gauge cells: each covers two HP, so it can be full, half or empty ---
    ldx #0
@cell:
    txa
    asl a
    sta tmp5                    ; HP already accounted for by earlier cells
    lda tmp4
    sec
    sbc tmp5                    ; HP left for this cell
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
    adc #(HP_BAR_X * 2)
    tay
    lda tmp6
    sta hudRow,y

    inx
    cpx #HP_BAR_CELLS
    bcc @cell

    sep #$20
    .a8
@flag:
    lda #$01
    sta hudDirty
    rts
.endproc
