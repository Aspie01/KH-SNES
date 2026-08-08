;=============================================================================
; grid.s -- ground geometry, camera, and collision
;
; The view is three-quarters overhead, so screen space *is* world space: actors
; store world pixel coordinates in Q12.4 fixed point and drawing needs no
; projection at all.  The map only comes back into play when we ask "what tile
; am I standing on", and on a square lattice of 16x16 tiles that inverts
;
;   world_x = i * 16      world_y = j * 16
;
; into two shifts,
;
;   i = world_x >> 4      j = world_y >> 4
;
; so a standing-on-tile query is a lookup rather than a calculation.  Both
; shifts turn a negative coordinate into a large unsigned one, which the range
; check that follows rejects along with anything past the edge of the map.
;=============================================================================
.p816
.include "snes.inc"
.include "game.inc"
.include "ram.inc"
.include "macros.inc"

.export UpdateCamera, TileWalkable, TryMoveActor, TileToWorld, TileHeight

.segment "CODE"

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
    bmi @cx_lo                  ; a negative centre is below any bound
    cmp camLoX
    bcs @cx_hi
@cx_lo:
    lda camLoX
    bra @cx_done
@cx_hi:
    cmp camHiX
    bcc @cx_done
    lda camHiX
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
    bmi @cy_lo
    cmp camLoY
    bcs @cy_hi
@cy_lo:
    lda camLoY
    bra @cy_done
@cy_hi:
    cmp camHiY
    bcc @cy_done
    lda camHiY
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
; TileToWorld -- the centre of a map cell, in world pixels.
; In:  tmp0 = i, tmp1 = j (16-bit)
; Out: tmp0 = world X, tmp1 = world Y (whole pixels)
; A16/I16.
;-----------------------------------------------------------------------------
.proc TileToWorld
    .a16
    .i16
    lda tmp0
    asl a
    asl a
    asl a
    asl a                       ; i * TILE_PX
    clc
    adc #(TILE_PX / 2)
    sta tmp0

    lda tmp1
    asl a
    asl a
    asl a
    asl a
    clc
    adc #(TILE_PX / 2)
    sta tmp1
    rts
.endproc

;-----------------------------------------------------------------------------
; TileIndex -- which map cell does this world pixel fall in?
; In:  tmp0 = world X, tmp1 = world Y (whole pixels, signed)
; Out: carry set and Y = j * MAP_W + i, or carry clear if off the map
; A16/I16.  Clobbers A, tmp2.
;-----------------------------------------------------------------------------
.proc TileIndex
    .a16
    .i16
    lda tmp0
    lsr a
    lsr a
    lsr a
    lsr a                       ; i = world_x >> 4
    ; A negative coordinate shifts to a large unsigned value, so one unsigned
    ; compare rejects both "off the west edge" and "off the east edge".
    cmp #MAP_W
    bcs @off
    sta tmp2

    lda tmp1
    lsr a
    lsr a
    lsr a
    lsr a                       ; j = world_y >> 4
    cmp #MAP_H
    bcs @off

    asl a
    asl a
    asl a
    asl a
    asl a                       ; j * MAP_W (MAP_W is 32)
    clc
    adc tmp2
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
; A16/I16.  Clobbers A, tmp2, Y.
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
; A16/I16.  Clobbers tmp2, Y.
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
; A16/I16.  Clobbers A, tmp2, Y.
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
