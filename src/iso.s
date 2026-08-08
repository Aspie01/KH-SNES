;=============================================================================
; iso.s -- isometric geometry, camera, and ground collision
;
; Screen space is the authority: actors store world *pixel* coordinates in
; Q12.4 fixed point, so drawing needs no projection at all.  The isometric
; grid only comes back into play when we ask "what tile am I standing on",
; which inverts
;
;   world_x = (i - j) * 16 + ORIGIN_X
;   world_y = (i + j) * 8
;
; into
;
;   a = world_x - ORIGIN_X
;   i = (a + 2 * world_y) >> 5
;   j = (2 * world_y - a) >> 5
;
; Both divisions are exact powers of two, so a standing-on-tile query costs a
; handful of shifts instead of a divide.
;=============================================================================
.p816
.include "snes.inc"
.include "game.inc"
.include "ram.inc"
.include "macros.inc"

.export UpdateCamera, TileWalkable, TryMoveActor, IsoToWorld, TileHeight

.segment "CODE"

; Arithmetic (sign-preserving) shift right by one, 16-bit accumulator.
; CMP #$8000 sets carry exactly when bit 15 is set, and ROR feeds it back in.
.macro ASR16
    cmp #$8000
    ror a
.endmacro

.macro ASR16_5
    ASR16
    ASR16
    ASR16
    ASR16
    ASR16
.endmacro

;-----------------------------------------------------------------------------
; UpdateCamera -- centre the view on the player and clamp to the world.
; A8/I16 in and out.
;-----------------------------------------------------------------------------
.proc UpdateCamera
    .a8
    .i16
    lda playerIdx
    rep #$30
    .a16
    .i16
    and #$00FF
    asl a
    tax                         ; X = player word offset

    ;--- horizontal ---
    lda actX,x
    lsr a
    lsr a
    lsr a
    lsr a                       ; Q12.4 -> whole pixels
    sec
    sbc #(SCREEN_W / 2)
    bpl @cx_lo_ok
    lda #0
@cx_lo_ok:
    cmp #(CAM_MAX_X + 1)
    bcc @cx_done
    lda #CAM_MAX_X
@cx_done:
    sta camX
    ; shakeX is a signed byte, normally zero; the shatter drives it.
    sep #$20
    .a8
    lda shakeX
    rep #$20
    .a16
    and #$00FF
    cmp #$0080
    bcc :+
    ora #$FF00                  ; sign-extend
:   clc
    adc camX
    sta bgHOfs

    ;--- vertical ---
    lda actY,x
    lsr a
    lsr a
    lsr a
    lsr a
    sec
    sbc #(SCREEN_H / 2)
    bpl @cy_lo_ok
    lda #0
@cy_lo_ok:
    cmp #(CAM_MAX_Y + 1)
    bcc @cy_done
    lda #CAM_MAX_Y
@cy_done:
    sta camY
    ; BGnVOFS displays background line (value + 1), so bias by one.
    dec a
    and #$03FF
    sta bgVOfs

    sep #$20
    .a8
    rts
.endproc

;-----------------------------------------------------------------------------
; IsoToWorld -- convert isometric tile coordinates to a world pixel position.
; In:  tmp0 = i, tmp1 = j (16-bit)
; Out: tmp0 = world X, tmp1 = world Y (whole pixels)
; A16/I16.
;-----------------------------------------------------------------------------
.proc IsoToWorld
    .a16
    .i16
    lda tmp0
    sec
    sbc tmp1
    asl a
    asl a
    asl a
    asl a                       ; (i - j) * 16
    clc
    adc #ORIGIN_X
    sta tmp2                    ; stash: tmp0 is still needed

    lda tmp0
    clc
    adc tmp1
    asl a
    asl a
    asl a                       ; (i + j) * 8
    sta tmp1

    lda tmp2
    sta tmp0
    rts
.endproc

;-----------------------------------------------------------------------------
; TileIndex -- which map cell does this world pixel fall in?
; In:  tmp0 = world X, tmp1 = world Y (whole pixels, signed)
; Out: carry set and Y = j * MAP_W + i, or carry clear if off the map
; A16/I16.  Clobbers A, tmp2, tmp3, tmp4.
;-----------------------------------------------------------------------------
.proc TileIndex
    .a16
    .i16
    lda tmp1
    asl a
    sta tmp3                    ; t = 2 * world_y

    lda tmp0
    sec
    sbc #ORIGIN_X
    sta tmp2                    ; a = world_x - ORIGIN_X

    ; IsoToWorld puts a tile's *corner* at (i-j)*16 + ORIGIN_X, (i+j)*8, so a
    ; point at the middle of a diamond sits half a tile past that origin along
    ; both axes.  Bias by that half tile or every lookup lands on the diamond
    ; down and to the right of the one the actor is really standing in.
    clc
    adc tmp3
    sec
    sbc #16
    ASR16_5                     ; i = (a + t - 16) >> 5
    ; An out-of-range or negative i wraps to a large unsigned value, so one
    ; unsigned compare rejects both.
    cmp #MAP_W
    bcs @off
    sta tmp4

    lda tmp3
    sec
    sbc tmp2
    clc
    adc #16
    ASR16_5                     ; j = (t - a + 16) >> 5
    cmp #MAP_H
    bcs @off

    asl a
    asl a
    asl a
    asl a                       ; j * MAP_W (MAP_W is 16)
    clc
    adc tmp4
    tay
    sec
    rts

@off:
    clc
    rts
.endproc

;-----------------------------------------------------------------------------
; TileWalkable -- is this world pixel standing on walkable ground?
; In:  tmp0 = world X, tmp1 = world Y (whole pixels, signed)
; Out: carry set when walkable, and tmp2 = that tile's height
; A16/I16.  Clobbers A, tmp2, tmp3, tmp4, Y.
;-----------------------------------------------------------------------------
.proc TileWalkable
    .a16
    .i16
    jsr TileIndex
    bcc @blocked

    sep #$20
    .a8
    lda [collPtr],y
    rep #$20
    .a16
    and #$00FF
    beq @blocked

    sep #$20
    .a8
    lda [heightPtr],y
    rep #$20
    .a16
    and #$00FF
    sta tmp2
    sec
    rts

@blocked:
    stz tmp2
    clc
    rts
.endproc

;-----------------------------------------------------------------------------
; TileHeight -- ground height under a world pixel, in eight-pixel steps.
; In:  tmp0 = world X, tmp1 = world Y (whole pixels)
; Out: A = height (zero off the map)
; A16/I16.  Clobbers tmp2, tmp3, tmp4, Y.
;-----------------------------------------------------------------------------
.proc TileHeight
    .a16
    .i16
    jsr TileIndex
    bcc @flat
    sep #$20
    .a8
    lda [heightPtr],y
    rep #$20
    .a16
    and #$00FF
    rts
@flat:
    lda #$0000
    rts
.endproc

;-----------------------------------------------------------------------------
; StepOk -- can the actor being moved stand on this world pixel?
; In:  tmp0/tmp1 = candidate position (whole pixels), stepZ = current height
; Out: carry set when the tile is walkable and within one step of stepZ;
;      tmp2 = the tile's height
; A16/I16.  Clobbers A, tmp2, tmp3, tmp4, Y.
;-----------------------------------------------------------------------------
.proc StepOk
    .a16
    .i16
    jsr TileWalkable
    bcc @no
    lda tmp2
    sec
    sbc stepZ
    bpl :+
    eor #$FFFF
    inc a                       ; absolute difference
:   cmp #(MAX_STEP + 1)
    bcs @no
    sec
    rts
@no:
    clc
    rts
.endproc

;-----------------------------------------------------------------------------
; StoreZ -- record the height the actor just stepped onto.
; In: X = actor word offset, tmp2 = height.  A16/I16, X preserved.
;-----------------------------------------------------------------------------
.proc StoreZ
    .a16
    .i16
    phx
    txa
    lsr a
    tay
    sep #$20
    .a8
    lda tmp2
    sta actZ,y
    rep #$20
    .a16
    plx
    rts
.endproc

;-----------------------------------------------------------------------------
; TryMoveActor -- apply actVX/actVY with wall sliding.
; In:  X = actor word offset (index * 2)
; A16/I16.  Clobbers A, X, Y, tmp0-tmp6, stepZ.  X is restored on exit.
;-----------------------------------------------------------------------------
.proc TryMoveActor
    .a16
    .i16
    phx

    ; Where the actor is standing now, so a step up or down can be measured.
    txa
    lsr a
    tay
    sep #$20
    .a8
    lda actZ,y
    rep #$20
    .a16
    and #$00FF
    sta stepZ

    lda actX,x
    clc
    adc actVX,x
    sta tmp5                    ; candidate X, Q12.4
    lda actY,x
    clc
    adc actVY,x
    sta tmp6                    ; candidate Y, Q12.4

    ;--- both axes ---
    lda tmp5
    lsr a
    lsr a
    lsr a
    lsr a
    sta tmp0
    lda tmp6
    lsr a
    lsr a
    lsr a
    lsr a
    sta tmp1
    jsr StepOk
    bcc @slideX
    plx
    lda tmp5
    sta actX,x
    lda tmp6
    sta actY,x
    jsr StoreZ
    rts

    ;--- horizontal only ---
@slideX:
    plx
    phx
    lda tmp5
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
    jsr StepOk
    bcc @slideY
    plx
    lda tmp5
    sta actX,x
    jsr StoreZ
    rts

    ;--- vertical only ---
@slideY:
    plx
    phx
    lda actX,x
    lsr a
    lsr a
    lsr a
    lsr a
    sta tmp0
    lda tmp6
    lsr a
    lsr a
    lsr a
    lsr a
    sta tmp1
    jsr StepOk
    bcc @stuck
    plx
    lda tmp6
    sta actY,x
    jsr StoreZ
    rts

@stuck:
    plx
    rts
.endproc
