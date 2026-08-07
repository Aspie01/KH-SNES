;=============================================================================
; nmi.s -- vertical blank interrupt
;
; Everything that touches VRAM, CGRAM or OAM happens here, inside the ~2.4 ms
; of NTSC vblank.  The main loop only ever prepares buffers and sets flags, so
; the interrupt never needs shared scratch space and can preempt safely.
;
; Budget used per frame:
;   OAM             544 bytes   (every frame)
;   Sora sprite     512 bytes   (only when the animation frame changes)
;   HUD row          64 bytes   (only when HP/MP changes)
;=============================================================================
.p816
.include "snes.inc"
.include "game.inc"
.include "ram.inc"
.include "macros.inc"
.include "text.inc"

.export NmiHandler

.segment "CODE"

.proc NmiHandler
    rep #$38                    ; A16, I16 so the pushes below are 16-bit
    .a16
    .i16
    pha
    phx
    phy
    phd
    phb

    lda #$0000
    tcd                         ; direct page = $0000
    sep #$20
    .a8
    phk
    plb                         ; data bank = program bank

    lda RDNMI                   ; acknowledge the interrupt

    ; Screen-wide effects. INIDISP and MOSAIC are not VRAM, but writing them
    ; here keeps the change atomic with the frame it belongs to.
    lda screenBright
    sta INIDISP
    lda mosaicAmt
    sta MOSAIC

    ;--- sprite table -------------------------------------------------------
    lda oamDirty
    beq @scroll
    stz oamDirty

    stz OAMADDL
    stz OAMADDH
    stz DMAP0                   ; mode 0: 1 byte -> $2104
    lda #<OAMDATA
    sta BBAD0
    ldx #.loword(oamBuf)
    stx A1T0L
    lda #^oamBuf
    sta A1B0
    ldx #544                    ; 512-byte low table + 32-byte high table
    stx DAS0L
    lda #$01
    sta MDMAEN

    ;--- background scroll --------------------------------------------------
@scroll:
    lda bgHOfs
    sta BG1HOFS
    lda bgHOfs+1
    sta BG1HOFS
    lda bgVOfs
    sta BG1VOFS
    lda bgVOfs+1
    sta BG1VOFS

    ;--- Sora's current animation frame ------------------------------------
    ; A 32x32 sprite occupies a 4x4 block of a 16-wide tile page, so the frame
    ; arrives as four 128-byte rows spaced $100 words apart.  DMA advances the
    ; source pointer itself, so each row just needs a new destination.
    lda streamPend
    beq @hud
    stz streamPend

    lda #$80
    sta VMAIN
    lda #$01                    ; mode 1: 2 bytes -> $2118/$2119
    sta DMAP0
    lda #<VMDATAL
    sta BBAD0
    ldx streamSrc
    stx A1T0L
    lda streamBank
    sta A1B0

    ldx #(VRAM_OBJ_CHR + $000)
    stx VMADDL
    ldx #SORA_ROW_BYTES
    stx DAS0L
    lda #$01
    sta MDMAEN

    ldx #(VRAM_OBJ_CHR + $100)
    stx VMADDL
    ldx #SORA_ROW_BYTES
    stx DAS0L
    lda #$01
    sta MDMAEN

    ldx #(VRAM_OBJ_CHR + $200)
    stx VMADDL
    ldx #SORA_ROW_BYTES
    stx DAS0L
    lda #$01
    sta MDMAEN

    ldx #(VRAM_OBJ_CHR + $300)
    stx VMADDL
    ldx #SORA_ROW_BYTES
    stx DAS0L
    lda #$01
    sta MDMAEN

    ;--- HUD gauge row ------------------------------------------------------
@hud:
    lda hudDirty
    beq @text
    stz hudDirty

    lda #$80
    sta VMAIN
    ldx #(VRAM_BG3_MAP + 32)    ; second row of the BG3 tilemap
    stx VMADDL
    lda #$01
    sta DMAP0
    lda #<VMDATAL
    sta BBAD0
    ldx #.loword(hudRow)
    stx A1T0L
    lda #^hudRow
    sta A1B0
    ldx #64
    stx DAS0L
    lda #$01
    sta MDMAEN

    ;--- dialogue window ----------------------------------------------------
@text:
    lda txtDirty
    beq @done
    stz txtDirty

    lda #$80
    sta VMAIN
    ldx #(VRAM_BG3_MAP + BOX_ROW * 32)
    stx VMADDL
    lda #$01
    sta DMAP0
    lda #<VMDATAL
    sta BBAD0
    ldx #.loword(txtBuf)
    stx A1T0L
    lda #^txtBuf
    sta A1B0
    ldx #(BOX_ROWS * 32 * 2)
    stx DAS0L
    lda #$01
    sta MDMAEN

@done:
    rep #$20
    .a16
    inc frameCount
    sep #$20
    .a8
    lda #$01
    sta vblankFlag

    rep #$38
    .a16
    .i16
    plb
    pld
    ply
    plx
    pla
    rti
.endproc
