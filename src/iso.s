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

.import collMap

.export UpdateCamera, TileWalkable, TryMoveActor, IsoToWorld

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
; TileWalkable -- is this world pixel standing on walkable ground?
; In:  tmp0 = world X, tmp1 = world Y (whole pixels, signed)
; Out: carry set when walkable
; A16/I16.  Clobbers A, X, tmp2, tmp3, tmp4.
;-----------------------------------------------------------------------------
.proc TileWalkable
    .a16
    .i16
    lda tmp1
    asl a
    sta tmp3                    ; t = 2 * world_y

    lda tmp0
    sec
    sbc #ORIGIN_X
    sta tmp2                    ; a = world_x - ORIGIN_X

    clc
    adc tmp3
    ASR16_5                     ; i = (a + t) >> 5
    ; An out-of-range or negative i wraps to a large unsigned value, so one
    ; unsigned compare rejects both.
    cmp #MAP_W
    bcs @blocked
    sta tmp4

    lda tmp3
    sec
    sbc tmp2
    ASR16_5                     ; j = (t - a) >> 5
    cmp #MAP_H
    bcs @blocked

    asl a
    asl a
    asl a
    asl a                       ; j * MAP_W (MAP_W is 16)
    clc
    adc tmp4
    tax

    sep #$20
    .a8
    lda f:collMap,x
    rep #$20
    .a16
    and #$00FF
    beq @blocked
    sec
    rts

@blocked:
    clc
    rts
.endproc

;-----------------------------------------------------------------------------
; TryMoveActor -- apply actVX/actVY with wall sliding.
; In:  X = actor word offset (index * 2)
; A16/I16.  Clobbers A, X, Y, tmp0-tmp6.  X is restored on exit.
;-----------------------------------------------------------------------------
.proc TryMoveActor
    .a16
    .i16
    phx

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
    jsr TileWalkable
    bcc @slideX
    plx
    lda tmp5
    sta actX,x
    lda tmp6
    sta actY,x
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
    jsr TileWalkable
    bcc @slideY
    plx
    lda tmp5
    sta actX,x
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
    jsr TileWalkable
    bcc @stuck
    plx
    lda tmp6
    sta actY,x
    rts

@stuck:
    plx
    rts
.endproc
