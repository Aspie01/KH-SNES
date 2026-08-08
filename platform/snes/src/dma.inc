;=============================================================================
; dma.inc -- DMA helper macros
;
; All of these assume A8/I16 and clobber A and X.  DMA advances the A-bus
; source address as it runs, which the sprite streamer relies on to walk
; through consecutive tile rows without recomputing the pointer.
;=============================================================================
.ifndef DMA_INC
DMA_INC = 1

.include "snes.inc"

; Copy `bytelen` bytes from ROM label `src` to VRAM word address `vaddr`.
.macro DMA_VRAM vaddr, src, bytelen
    lda #$80                    ; increment after writing VMDATAH, +1 word
    sta VMAIN
    ldx #vaddr
    stx VMADDL
    lda #$01                    ; mode 1: 2 bytes -> $2118/$2119
    sta DMAP0
    lda #<VMDATAL
    sta BBAD0
    ldx #.loword(src)
    stx A1T0L
    lda #^src
    sta A1B0
    ldx #bytelen
    stx DAS0L
    lda #$01
    sta MDMAEN
.endmacro

; Copy `bytelen` bytes from ROM label `src` into CGRAM starting at colour
; index `cgaddr` (0-255; each colour is two bytes).
.macro DMA_CGRAM cgaddr, src, bytelen
    lda #cgaddr
    sta CGADD
    lda #$00                    ; mode 0: 1 byte -> $2122
    sta DMAP0
    lda #<CGDATA
    sta BBAD0
    ldx #.loword(src)
    stx A1T0L
    lda #^src
    sta A1B0
    ldx #bytelen
    stx DAS0L
    lda #$01
    sta MDMAEN
.endmacro

; Fill a region by DMAing from a fixed source byte/word.
.macro DMA_VRAM_FILL vaddr, src, bytelen
    lda #$80
    sta VMAIN
    ldx #vaddr
    stx VMADDL
    lda #$09                    ; fixed source + mode 1
    sta DMAP0
    lda #<VMDATAL
    sta BBAD0
    ldx #.loword(src)
    stx A1T0L
    lda #^src
    sta A1B0
    ldx #bytelen
    stx DAS0L
    lda #$01
    sta MDMAEN
.endmacro

.endif ; DMA_INC
