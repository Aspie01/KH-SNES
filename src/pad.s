;=============================================================================
; pad.s -- controller input
;=============================================================================
.p816
.include "snes.inc"
.include "ram.inc"
.include "macros.inc"

.export ReadPad

.segment "CODE"

;-----------------------------------------------------------------------------
; ReadPad -- latch this frame's buttons and derive the newly-pressed set.
; A8/I16 in, A8/I16 out.
;-----------------------------------------------------------------------------
.proc ReadPad
    .a8
    .i16
    ; The auto-reader starts at the top of vblank and needs a few scanlines;
    ; bit 0 of HVBJOY stays set while it is running.
@wait:
    lda HVBJOY
    and #$01
    bne @wait

    rep #$20
    .a16
    lda padHeld
    sta padPrev
    lda JOY1L
    sta padHeld
    eor padPrev
    and padHeld                 ; held now, but not held last frame
    sta padPressed
    sep #$20
    .a8
    rts
.endproc
