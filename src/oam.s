;=============================================================================
; oam.s -- depth sorting and sprite table construction
;
; This is what sells the isometric illusion.  The ground is a background
; layer, so everything that stands up off it -- Sora, Heartless, palms, rocks
; -- is a sprite, and sprites are drawn strictly in OAM order: slot 0 is
; frontmost.  Sorting actors by world Y descending and writing them out in
; that order makes a character walk behind a palm when north of it and in
; front of it when south, with no per-object layer authoring at all.
;
; Ground shadows are emitted after every actor, so they land in higher OAM
; slots and therefore behind all of them, while still sitting above BG1.
;=============================================================================
.p816
.include "snes.inc"
.include "game.inc"
.include "ram.inc"
.include "macros.inc"

.export BuildOam, ClearOamBuffer

OAM_HIDE_Y = $E0        ; 224: a 32-tall sprite here ends at 255 and never
                        ; wraps back onto the visible 224 lines

.segment "CODE"

;-----------------------------------------------------------------------------
; ClearOamBuffer -- park all 128 sprites off-screen.  A8/I16.
;-----------------------------------------------------------------------------
.proc ClearOamBuffer
    .a8
    .i16
    ldx #0
    lda #OAM_HIDE_Y
@hide:
    sta oamBuf+1,x
    inx
    inx
    inx
    inx
    cpx #512
    bcc @hide

    ldx #0
    lda #$00
@high:
    sta oamHigh,x
    inx
    cpx #32
    bcc @high
    rts
.endproc

;-----------------------------------------------------------------------------
; BuildOam -- rebuild the whole sprite table for this frame.  A8/I16.
;-----------------------------------------------------------------------------
.proc BuildOam
    .a8
    .i16
    jsr ClearOamBuffer
    rep #$30
    .a16
    .i16
    jsr BuildSortList
    stz tmp7                    ; next free OAM slot
    jsr EmitActors
    jsr EmitBoss
    jsr EmitShadows
    sep #$20
    .a8
    rts
.endproc

;-----------------------------------------------------------------------------
; BuildSortList -- collect live actors into sortIdx, sorted by world Y
; descending (frontmost first).  A16/I16.
;-----------------------------------------------------------------------------
.proc BuildSortList
    .a16
    .i16
    stz sortCount
    ldx #0
@scan:
    sep #$20
    .a8
    lda actType,x
    rep #$20
    .a16
    and #$00FF
    beq @next
    phx                         ; InsertSorted reuses X for its own indexing
    jsr InsertSorted
    plx
@next:
    inx
    cpx #MAX_ACTORS
    bcc @scan
    rts
.endproc

;-----------------------------------------------------------------------------
; InsertSorted -- insertion-sort one actor into sortIdx.
; In: X = actor index.  A16/I16.  Clobbers A, X, Y, tmp0, tmp1.
;
; With at most 24 actors and near-sorted input frame to frame, insertion sort
; costs far less than the setup for anything cleverer.
;-----------------------------------------------------------------------------
.proc InsertSorted
    .a16
    .i16
    stx tmp1                    ; the actor being inserted
    txa
    asl a
    tax
    lda actY,x
    sta tmp0                    ; its sort key

    lda sortCount
    asl a
    tay                         ; Y = byte offset of the next free slot

@shift:
    cpy #0
    beq @place
    ldx sortIdx-2,y             ; actor already occupying the slot before us
    txa
    asl a
    tax
    lda actY,x
    cmp tmp0
    bcs @place                  ; it is at least as far forward: stop here
    lda sortIdx-2,y             ; otherwise push it back one slot
    sta sortIdx,y
    dey
    dey
    bra @shift

@place:
    lda tmp1
    sta sortIdx,y
    lda sortCount
    inc a
    sta sortCount
    rts
.endproc

;-----------------------------------------------------------------------------
; EmitActors -- write one sprite per sorted actor.  A16/I16.
;-----------------------------------------------------------------------------
.proc EmitActors
    .a16
    .i16
    lda sortCount
    asl a
    sta tmp4                    ; byte length of sortIdx
    ldy #0
@loop:
    cpy tmp4
    bcs @done
    lda tmp7
    cmp #128                    ; hardware sprite ceiling
    bcs @done
    ldx sortIdx,y
    phy
    jsr EmitActorSprite
    ply
    iny
    iny
    bra @loop
@done:
    rts
.endproc

;-----------------------------------------------------------------------------
; EmitShadows -- one flattened blob per actor that casts a shadow.  Emitted
; after every actor so they occupy higher OAM slots and render behind them.
; A16/I16.
;-----------------------------------------------------------------------------
.proc EmitShadows
    .a16
    .i16
    lda sortCount
    asl a
    sta tmp4
    ldy #0
@loop:
    cpy tmp4
    bcs @done
    lda tmp7
    cmp #128
    bcs @done
    ldx sortIdx,y
    sep #$20
    .a8
    lda actFlags,x
    and #AF_SHADOW
    rep #$20
    .a16
    and #$00FF
    beq @next
    phy
    jsr EmitShadowSprite
    ply
@next:
    iny
    iny
    bra @loop
@done:
    rts
.endproc

;-----------------------------------------------------------------------------
; EmitBoss -- Darkside is 64x64, which no single sprite can be, so it goes out
; as four 32x32 quadrants.
;
; It is emitted after every sorted actor rather than inside the sort, so it
; always lands in higher OAM slots and therefore behind them.  That is the
; right answer nearly always: it towers over Sora and he fights at its feet.
; A16/I16.
;-----------------------------------------------------------------------------
.proc EmitBoss
    .a16
    .i16
    ldx #0
@scan:
    sep #$20
    .a8
    lda actType,x
    rep #$20
    .a16
    and #$00FF
    cmp #ACT_DARKSIDE
    beq @found
    inx
    cpx #MAX_ACTORS
    bcc @scan
    rts

@found:
    stx tmp3                    ; actor index, for WriteOamRaw
    txa
    asl a
    tax
    lda actX,x
    lsr a
    lsr a
    lsr a
    lsr a
    sec
    sbc camX
    sta tmp8                    ; screen X of its feet
    lda actY,x
    lsr a
    lsr a
    lsr a
    lsr a
    sec
    sbc camY
    sta tmp9

    ; Flash while it is flinching.  Set once: WriteOamRaw reads tmp6 but never
    ; writes it, and Y is the quadrant cursor from here on.
    sep #$20
    .a8
    ldy tmp3
    lda actHitT,y
    beq @normalPal
    lda frameCount
    and #$02
    bne @flashPal
@normalPal:
    lda #PAL_OBJ_HEART
    bra @setPal
@flashPal:
    lda #PAL_OBJ_FX
@setPal:
    rep #$20
    .a16
    and #$00FF
    sta tmp6

    ldy #0                      ; quadrant, as a byte offset into the tables
@quad:
    lda tmp7
    cmp #128
    bcs @done
    lda tmp8
    clc
    adc quadX,y
    sta tmp0
    lda tmp9
    clc
    adc quadY,y
    sta tmp1

    lda tmp1
    clc
    adc #32
    cmp #(SCREEN_H + 32)
    bcs @next
    lda tmp0
    clc
    adc #32
    cmp #(SCREEN_W + 32)
    bcs @next

    lda quadT,y
    sta tmp2
    lda #$0002                  ; each quadrant is a large (32x32) sprite
    sta tmp5
    phy
    jsr WriteOamRaw
    ply
@next:
    iny
    iny
    cpy #8
    bcc @quad
@done:
    rts
.endproc

; Quadrant offsets from the boss's feet, and the tile each one starts at.
quadX: .word .loword(-32), .loword(0), .loword(-32), .loword(0)
quadY: .word .loword(-64), .loword(-64), .loword(-32), .loword(-32)
quadT: .word TILE_DARKSIDE, TILE_DARKSIDE+$04, TILE_DARKSIDE+$40, TILE_DARKSIDE+$44

;-----------------------------------------------------------------------------
; EmitActorSprite -- In: X = actor index.  A16/I16.
; Clobbers A, X, Y, tmp0-tmp3, tmp5, tmp6.  Advances tmp7 only if drawn.
;-----------------------------------------------------------------------------
.proc EmitActorSprite
    .a16
    .i16
    stx tmp3                    ; actor index
    txa
    asl a
    tax                         ; word offset

    lda actX,x
    lsr a
    lsr a
    lsr a
    lsr a
    sec
    sbc camX
    sta tmp0                    ; screen X of the actor's feet
    lda actY,x
    lsr a
    lsr a
    lsr a
    lsr a
    sec
    sbc camY
    sta tmp1                    ; screen Y of the actor's feet

    ; Standing on a raised deck lifts the sprite without moving the actor:
    ; the world stays one flat plane as far as the geometry is concerned.
    ldy tmp3
    sep #$20
    .a8
    lda actZ,y
    rep #$20
    .a16
    and #$00FF
    asl a
    asl a
    asl a                       ; height * 8 pixels
    sta tmp2
    lda tmp1
    sec
    sbc tmp2
    sta tmp1

    ldx tmp3
    sep #$20
    .a8
    lda actFlags,x
    rep #$20
    .a16
    and #$00FF
    sta tmp2

    ; Anchor the sprite by its bottom centre so it stands on its position.
    and #AF_LARGE
    beq @small
    lda tmp0
    sec
    sbc #16
    sta tmp0
    lda tmp1
    sec
    sbc #32
    sta tmp1
    lda #$0002                  ; high-table size bit: large
    sta tmp5
    bra @cull
@small:
    lda tmp0
    sec
    sbc #8
    sta tmp0
    lda tmp1
    sec
    sbc #16
    sta tmp1
    stz tmp5

    ; Bias by 32 so one unsigned compare rejects both the negative and the
    ; past-the-edge cases at once.
@cull:
    lda tmp1
    clc
    adc #32
    cmp #(SCREEN_H + 32)
    bcs @skip
    lda tmp0
    clc
    adc #32
    cmp #(SCREEN_W + 32)
    bcs @skip

    jsr WriteOamEntry
@skip:
    rts
.endproc

;-----------------------------------------------------------------------------
; EmitShadowSprite -- In: X = actor index.  A16/I16.
;-----------------------------------------------------------------------------
.proc EmitShadowSprite
    .a16
    .i16
    stx tmp3
    sep #$20
    .a8
    lda actFlags,x
    and #AF_LARGE
    rep #$20
    .a16
    and #$00FF
    sta tmp5                    ; non-zero for a large actor

    lda tmp3
    asl a
    tax
    lda actX,x
    lsr a
    lsr a
    lsr a
    lsr a
    sec
    sbc camX
    sta tmp0
    lda actY,x
    lsr a
    lsr a
    lsr a
    lsr a
    sec
    sbc camY
    sta tmp1

    ldy tmp3                    ; the shadow rides the deck too
    sep #$20
    .a8
    lda actZ,y
    rep #$20
    .a16
    and #$00FF
    asl a
    asl a
    asl a
    sta tmp2
    lda tmp1
    sec
    sbc tmp2
    sta tmp1

    ; Both blob tiles put the ellipse centre on the sprite's bottom edge, so
    ; the offsets here drop it exactly onto the actor's feet.
    lda tmp5
    beq @small
    lda tmp0
    sec
    sbc #16
    sta tmp0
    lda tmp1
    sec
    sbc #24
    sta tmp1
    lda #TILE_SHADOWBIG
    sta tmp2
    lda #$0002                  ; high-table size bit: large
    sta tmp5
    bra @cull
@small:
    lda tmp0
    sec
    sbc #8
    sta tmp0
    lda tmp1
    sec
    sbc #12
    sta tmp1
    lda #TILE_SHADOW
    sta tmp2
    stz tmp5

@cull:
    lda tmp1
    clc
    adc #32
    cmp #(SCREEN_H + 32)
    bcs @skip
    lda tmp0
    clc
    adc #32
    cmp #(SCREEN_W + 32)
    bcs @skip

    lda #PAL_OBJ_SHADOW
    sta tmp6
    jsr WriteOamRaw
@skip:
    rts
.endproc

;-----------------------------------------------------------------------------
; WriteOamEntry -- emit the actor in tmp3 using its own tile and palette.
; In: tmp0 = OAM X, tmp1 = OAM Y, tmp3 = actor index, tmp5 = size bit.
; A16/I16.
;-----------------------------------------------------------------------------
.proc WriteOamEntry
    .a16
    .i16
    ldy tmp3
    sep #$20
    .a8
    lda actTile,y
    rep #$20
    .a16
    and #$00FF
    sta tmp2

    ldy tmp3
    sep #$20
    .a8
    lda actPal,y
    rep #$20
    .a16
    and #$00FF
    sta tmp6
    ; fall through
.endproc

;-----------------------------------------------------------------------------
; WriteOamRaw -- push one sprite into the shadow OAM.
; In: tmp0 = X, tmp1 = Y, tmp2 = tile, tmp6 = palette, tmp5 = size bit,
;     tmp7 = destination slot (advanced on return).
; A16/I16.
;-----------------------------------------------------------------------------
.proc WriteOamRaw
    .a16
    .i16
    lda tmp7
    asl a
    asl a
    tax                         ; X = byte offset of the slot

    sep #$20
    .a8
    lda tmp0
    sta oamBuf,x                ; X, low 8 bits
    lda tmp1
    sta oamBuf+1,x              ; Y
    lda tmp2
    sta oamBuf+2,x              ; tile (all our sprite tiles live below $100)

    ; attribute byte: v h oo ppp N
    lda tmp6
    and #$07
    asl a                       ; palette into bits 3-1
    ora #$20                    ; priority 2: above BG1, below the BG3 HUD
    sta tmp2
    ldy tmp3
    lda actFlags,y
    and #AF_HFLIP
    beq @noflip
    lda tmp2
    ora #$40
    sta tmp2
@noflip:
    ; Bit 0 of the attribute byte is the name-table select: the islanders and
    ; the raft materials live on the second 256-tile page.
    lda actFlags,y
    and #AF_PAGE1
    beq @page0
    lda tmp2
    ora #$01
    sta tmp2
@page0:
    lda tmp2
    sta oamBuf+3,x

    ;--- high table: X sign bit and size bit, two bits per sprite ---
    rep #$20
    .a16
    lda tmp0
    and #$0100                  ; valid X spans -32..255, so bit 8 == negative
    beq @nosign
    lda tmp5
    ora #$0001
    sta tmp5
@nosign:
    lda tmp7
    and #$0003
    asl a
    tay                         ; Y = shift amount
    lda tmp5
    cpy #0
    beq @noshift
@shift:
    asl a
    dey
    bne @shift
@noshift:
    sta tmp5
    lda tmp7
    lsr a
    lsr a
    tax                         ; X = high-table byte index
    sep #$20
    .a8
    lda tmp5
    ora oamHigh,x
    sta oamHigh,x
    rep #$20
    .a16

    lda tmp7
    inc a
    sta tmp7
    rts
.endproc
