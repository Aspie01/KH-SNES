;=============================================================================
; text.s -- dialogue window on BG3
;
; A message is a run of bytes in ROM: anything >= 32 is a character, anything
; below is a control code (newline, page break, end).  Characters are revealed
; a few per frame; pressing a button either fast-forwards the reveal or
; advances past the wait.
;
; The window lives in txtBuf, a six-row slice of the BG3 tilemap that the NMI
; uploads whenever txtDirty is set -- 384 bytes, comfortably inside vblank.
;=============================================================================
.p816
.include "snes.inc"
.include "game.inc"
.include "ram.inc"
.include "macros.inc"
.include "text.inc"

.export TextInit, TextOpen, TextUpdate, TextBusy, TextClose

REVEAL_DELAY = 1                ; frames between characters

.segment "CODE"

;-----------------------------------------------------------------------------
; TextInit -- blank the box region and close any message.  A8/I16.
;-----------------------------------------------------------------------------
.proc TextInit
    .a8
    .i16
    stz txtState
    stz txtMode
    stz txtChoice
    stz txtResult
    stz txtHold
    jsr ClearBuf
    rts
.endproc

;-----------------------------------------------------------------------------
; ClearBuf -- fill the whole box region with the transparent cell.  A8/I16.
;-----------------------------------------------------------------------------
.proc ClearBuf
    .a8
    .i16
    rep #$30
    .a16
    .i16
    lda #(TXT_ATTR | CH_CLEAR)
    ldx #0
@loop:
    sta txtBuf,x
    inx
    inx
    cpx #(BOX_ROWS * 32 * 2)
    bcc @loop
    sep #$20
    .a8
    lda #$01
    sta txtDirty
    rts
.endproc

;-----------------------------------------------------------------------------
; TextBusy -- carry set while a message is on screen.  A8/I16.
;-----------------------------------------------------------------------------
.proc TextBusy
    .a8
    .i16
    lda txtState
    beq @no
    sec
    rts
@no:
    clc
    rts
.endproc

;-----------------------------------------------------------------------------
; TextOpen -- start a message.
; In (A8/I16): A = mode (TM_MESSAGE or TM_PROMPT); txtPtr already points at
; the script.  A16 is not required; the caller sets txtPtr itself.
;-----------------------------------------------------------------------------
.proc TextOpen
    .a8
    .i16
    sta txtMode
    lda #TS_REVEAL
    sta txtState
    stz txtChoice
    stz txtResult
    lda #$01
    sta txtHold                 ; ignore the button that opened the box
    stz txtDelay

    jsr DrawFrame
    jsr ClearTextArea
    rts
.endproc

;-----------------------------------------------------------------------------
; TextClose -- take the window down.  A8/I16.
;-----------------------------------------------------------------------------
.proc TextClose
    .a8
    .i16
    stz txtState
    ; Eat the button that dismissed the box.  Scene scripts test padPressed
    ; after this runs, and without this the same press that closed a message
    ; would immediately start the next conversation.
    rep #$20
    .a16
    lda padPressed
    and #((PAD_A | PAD_B) ^ $FFFF)
    sta padPressed
    sep #$20
    .a8
    jsr ClearBuf
    rts
.endproc


;-----------------------------------------------------------------------------
; DrawFrame -- lay the nine-patch border into txtBuf.  A8/I16.
;
; Walks txtBuf linearly with X rather than recomputing row*32+col per cell:
; the rows are contiguous, so one running index covers the whole box.
;-----------------------------------------------------------------------------
.proc DrawFrame
    .a8
    .i16
    rep #$30
    .a16
    .i16
    ldx #0                      ; running byte offset into txtBuf
    ldy #0                      ; row
@row:
    ; pick the left / middle / right piece for this row
    cpy #0
    bne :+
    lda #(TXT_ATTR | CH_WIN_TL)
    sta tmp0
    lda #(TXT_ATTR | CH_WIN_T)
    sta tmp1
    lda #(TXT_ATTR | CH_WIN_TR)
    sta tmp2
    bra @emit
:   cpy #(BOX_ROWS - 1)
    bne :+
    lda #(TXT_ATTR | CH_WIN_BL)
    sta tmp0
    lda #(TXT_ATTR | CH_WIN_B)
    sta tmp1
    lda #(TXT_ATTR | CH_WIN_BR)
    sta tmp2
    bra @emit
:   lda #(TXT_ATTR | CH_WIN_L)
    sta tmp0
    lda #(TXT_ATTR | CH_WIN_C)
    sta tmp1
    lda #(TXT_ATTR | CH_WIN_R)
    sta tmp2

@emit:
    lda tmp0
    sta txtBuf,x
    inx
    inx
    lda #30
    sta tmp3
    lda tmp1
@mid:
    sta txtBuf,x
    inx
    inx
    dec tmp3
    bne @mid
    lda tmp2
    sta txtBuf,x
    inx
    inx

    iny
    cpy #BOX_ROWS
    bcc @row

    sep #$20
    .a8
    lda #$01
    sta txtDirty
    rts
.endproc

;-----------------------------------------------------------------------------
; ClearTextArea -- blank the writable interior and rewind the cursor.  A8/I16.
;-----------------------------------------------------------------------------
.proc ClearTextArea
    .a8
    .i16
    rep #$30
    .a16
    .i16
    ldy #TEXT_ROW
@row:
    tya
    asl a
    asl a
    asl a
    asl a
    asl a                       ; row * 32
    clc
    adc #TEXT_COL
    asl a                       ; entries are two bytes
    tax
    lda #TEXT_W
    sta tmp0
    lda #(TXT_ATTR | CH_BLANK)
@col:
    sta txtBuf,x
    inx
    inx
    dec tmp0
    bne @col
    iny
    cpy #(TEXT_ROW + TEXT_H)
    bcc @row

    stz txtCol
    stz txtRow
    sep #$20
    .a8
    lda #$01
    sta txtDirty
    rts
.endproc

;-----------------------------------------------------------------------------
; PutChar -- write one tile at the cursor and advance it.
; In (A16/I16): A = tile number (low byte meaningful).
;-----------------------------------------------------------------------------
.proc PutChar
    .a16
    .i16
    and #$00FF
    ora #TXT_ATTR
    sta tmp1                    ; the cell value

    lda txtRow
    clc
    adc #TEXT_ROW
    asl a
    asl a
    asl a
    asl a
    asl a                       ; (TEXT_ROW + row) * 32
    sta tmp0
    lda txtCol
    clc
    adc #TEXT_COL
    clc
    adc tmp0
    asl a
    tax
    lda tmp1
    sta txtBuf,x

    lda txtCol
    inc a
    sta txtCol
    cmp #TEXT_W
    bcc @done
    jsr NewLine
@done:
    rts
.endproc

;-----------------------------------------------------------------------------
; NewLine -- A16/I16.
;-----------------------------------------------------------------------------
.proc NewLine
    .a16
    .i16
    stz txtCol
    lda txtRow
    inc a
    sta txtRow
    cmp #TEXT_H
    bcc @done
    lda #(TEXT_H - 1)           ; clamp; a page break is what should follow
    sta txtRow
@done:
    rts
.endproc

;-----------------------------------------------------------------------------
; TextUpdate -- one frame of the dialogue box.  A8/I16.
;-----------------------------------------------------------------------------
.proc TextUpdate
    .a8
    .i16
    lda txtState
    bne @active
    rts

@active:
    ; A press is only accepted once; txtHold blocks the button that opened
    ; the box from immediately dismissing it.
    lda txtHold
    beq @readpad
    rep #$20
    .a16
    lda padHeld
    and #(PAD_A | PAD_B)
    sep #$20
    .a8
    bne @noinput
    stz txtHold
@readpad:
    rep #$20
    .a16
    lda padPressed
    and #(PAD_A | PAD_B)
    sep #$20
    .a8
    bne @pressed
@noinput:
    lda #$00
    bra @dispatch
@pressed:
    lda #$01

@dispatch:
    sta tmp2                    ; non-zero if a button was pressed this frame
    lda txtState
    cmp #TS_REVEAL
    beq @reveal
    cmp #TS_WAIT
    beq @wait
    cmp #TS_PROMPT
    beq @prompt
    rts

    ;--- revealing ---------------------------------------------------------
@reveal:
    lda tmp2
    beq @tick
    ; fast-forward: dump the rest of the page in one frame
    jsr RevealAll
    rts
@tick:
    lda txtDelay
    beq @emit
    dec a
    sta txtDelay
    rts
@emit:
    jsr EmitOne
    lda #REVEAL_DELAY
    sta txtDelay
    rts

    ;--- message finished, waiting for a button ----------------------------
@wait:
    lda tmp2
    beq @out
    lda txtMode
    beq @closeit                ; TM_MESSAGE just goes away
    lda #TS_PROMPT
    sta txtState
    jsr DrawOptions
    rts
@closeit:
    jsr TextClose
@out:
    rts

    ;--- yes/no selector ---------------------------------------------------
@prompt:
    jsr MenuCount
    sta tmp3                    ; how many choices this menu has
    rep #$20
    .a16
    lda padPressed
    and #PAD_UP
    sep #$20
    .a8
    beq @down
    lda txtChoice
    bne :+
    lda tmp3                    ; wrap round the top
:   dec a
    sta txtChoice
    jsr DrawOptions
    rts
@down:
    rep #$20
    .a16
    lda padPressed
    and #PAD_DOWN
    sep #$20
    .a8
    beq @confirm
    lda txtChoice
    inc a
    cmp tmp3
    bcc :+
    lda #0
:   sta txtChoice
    jsr DrawOptions
    rts
@confirm:
    lda tmp2
    beq @out2
    lda txtChoice
    inc a                       ; 1-based; 0 means "no answer yet"
    sta txtResult
    jsr TextClose
@out2:
    rts
.endproc

;-----------------------------------------------------------------------------
; EmitOne -- consume one script byte.  A8/I16.
;-----------------------------------------------------------------------------
.proc EmitOne
    .a8
    .i16
    lda [txtPtr]
    cmp #32
    bcs @char

    cmp #SC_END
    bne @notEnd
    lda #TS_WAIT
    sta txtState
    jsr DrawAdvanceMark
    rts

@notEnd:
    cmp #SC_NL
    bne @notNl
    jsr Advance
    rep #$20
    .a16
    jsr NewLine
    sep #$20
    .a8
    rts

@notNl:
    ; SC_PAGE: hold here; the next button press clears and continues
    jsr Advance
    lda #TS_WAIT
    sta txtState
    jsr DrawAdvanceMark
    rts

@char:
    rep #$20
    .a16
    and #$00FF
    sec
    sbc #32
    tax
    sep #$20
    .a8
    lda asciiToTile,x
    rep #$20
    .a16
    jsr PutChar
    sep #$20
    .a8
    jsr Advance
    lda #$01
    sta txtDirty
    rts
.endproc

;-----------------------------------------------------------------------------
; RevealAll -- emit script bytes until the page ends.  A8/I16.
;-----------------------------------------------------------------------------
.proc RevealAll
    .a8
    .i16
    ldy #0
@loop:
    lda txtState
    cmp #TS_REVEAL
    bne @done
    jsr EmitOne
    iny
    cpy #512                    ; backstop against a script with no terminator
    bcc @loop
@done:
    rts
.endproc

;-----------------------------------------------------------------------------
; Advance -- step txtPtr one byte.  A8/I16.
;-----------------------------------------------------------------------------
.proc Advance
    .a8
    .i16
    rep #$20
    .a16
    lda txtPtr
    inc a
    sta txtPtr
    sep #$20
    .a8
    rts
.endproc

;-----------------------------------------------------------------------------
; DrawAdvanceMark -- the little "press a button" arrow.  A8/I16.
;-----------------------------------------------------------------------------
.proc DrawAdvanceMark
    .a8
    .i16
    rep #$30
    .a16
    .i16
    lda #((TEXT_ROW + TEXT_H - 1) * 32 + TEXT_COL + TEXT_W - 1)
    asl a
    tax
    lda #(TXT_ATTR | CH_ADVANCE)
    sta txtBuf,x
    sep #$20
    .a8
    lda #$01
    sta txtDirty
    rts
.endproc


;-----------------------------------------------------------------------------
; A prompt shows a short list of choices.  Yes/no is just the shortest list;
; the mode picks which one, so a scene asks for a menu by opening the box with
; TM_RAFT instead of TM_PROMPT.
;-----------------------------------------------------------------------------
OPT_COL  = TEXT_COL + 16

;-----------------------------------------------------------------------------
; MenuCount -- how many choices the open prompt offers.  A8/I16, result in A.
;-----------------------------------------------------------------------------
.proc MenuCount
    .a8
    .i16
    lda txtMode
    rep #$20
    .a16
    and #$00FF
    dec a                       ; TM_PROMPT is menu 0
    tax
    sep #$20
    .a8
    lda menuCount,x
    rts
.endproc

;-----------------------------------------------------------------------------
; DrawOptions -- the choices, with the cursor on the highlighted one.  A8/I16.
;-----------------------------------------------------------------------------
.proc DrawOptions
    .a8
    .i16
    lda txtMode
    rep #$20
    .a16
    and #$00FF
    dec a
    tax
    sep #$20
    .a8
    lda menuCount,x
    sta tmp3                    ; number of choices
    lda menuRow,x
    sta tmp4                    ; first box row they occupy
    txa
    asl a
    rep #$20
    .a16
    and #$00FF
    tax
    lda menuList,x
    sta tmp5                    ; the choices, as a table of addresses
    sep #$20
    .a8

    stz tmp2                    ; which choice we are drawing
@opt:
    ; txtBuf offset of this choice's line
    rep #$20
    .a16
    lda tmp4
    and #$00FF
    clc
    adc tmp2
    and #$00FF
    clc
    adc #TEXT_ROW
    asl a
    asl a
    asl a
    asl a
    asl a                       ; * 32 cells a row
    clc
    adc #OPT_COL
    asl a                       ; two bytes a cell
    sta tmp6

    ; the cursor, on this line only if it is the one selected
    tax
    sep #$20
    .a8
    lda tmp2
    cmp txtChoice
    rep #$20
    .a16
    bne :+
    lda #(TXT_ATTR | CH_CURSOR)
    bra :++
:   lda #(TXT_ATTR | CH_BLANK)
:   sta txtBuf,x

    ; ...then the text, padded out so a longer choice above is erased
    lda tmp2
    and #$00FF
    asl a
    tay
    lda (tmp5),y                ; this choice's text
    sta tmp7
    sep #$20
    .a8

    ldy #0
@char:
    lda (tmp7),y
    cmp #$FF
    beq @pad
    rep #$20
    .a16
    and #$00FF
    ora #TXT_ATTR
    sta tmp8
    tya
    asl a
    clc
    adc tmp6
    clc
    adc #2                      ; past the cursor cell
    tax
    lda tmp8
    sta txtBuf,x
    sep #$20
    .a8
    iny
    bra @char

@pad:
    cpy #OPT_W
    bcs @next
    rep #$20
    .a16
    tya
    asl a
    clc
    adc tmp6
    clc
    adc #2
    tax
    lda #(TXT_ATTR | CH_BLANK)
    sta txtBuf,x
    sep #$20
    .a8
    iny
    bra @pad

@next:
    inc tmp2
    lda tmp2
    cmp tmp3
    bcs @done
    jmp @opt
@done:
    lda #$01
    sta txtDirty
    rts
.endproc

;=============================================================================
; ASCII ($20-$7F) -> font tile.  Lower case folds onto upper case; anything
; without a glyph becomes a space.
;=============================================================================
.segment "RODATA"

;--- the menus a prompt can show ---------------------------------------------
menuCount:  .byte 2, 3
menuRow:    .byte PROMPT_ROW, MENU_ROW
menuList:   .word .loword(optYesNo), .loword(optRaft)

optYesNo:   .word .loword(sYes), .loword(sNo)
optRaft:    .word .loword(sHighwind), .loword(sExcalibur), .loword(sRagnarok)

sYes:       TXTSTR "YES"
sNo:        TXTSTR "NO"
sHighwind:  TXTSTR "HIGHWIND"
sExcalibur: TXTSTR "EXCALIBUR"
sRagnarok:  TXTSTR "RAGNAROK"

asciiToTile:
    ;      sp   !   "   #   $   %   &   '   (   )   *   +   ,   -   .   /
    .byte   0, 39,  0,  0,  0,  0,  0, 41,  0,  0,  0,  0, 38, 42, 37, 44
    ;       0   1   2   3   4   5   6   7   8   9   :   ;   <   =   >   ?
    .byte  27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 43,  0,  0,  0,  0, 40
    ;       @   A   B   C   D   E   F   G   H   I   J   K   L   M   N   O
    .byte   0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15
    ;       P   Q   R   S   T   U   V   W   X   Y   Z   [   \   ]   ^   _
    .byte  16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26,  0,  0,  0,  0,  0
    ;       `   a   b   c   d   e   f   g   h   i   j   k   l   m   n   o
    .byte   0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15
    ;       p   q   r   s   t   u   v   w   x   y   z   {   |   }   ~ DEL
    .byte  16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26,  0,  0,  0,  0,  0
