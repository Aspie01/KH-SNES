;=============================================================================
; header.s -- SNES cartridge header and interrupt vectors
;
; Occupies $FFB0-$FFFF of bank $80 (file offset $7FB0-$7FFF).  The two
; checksum words are placeholders; tools/fixrom.py computes them after link.
;=============================================================================
.p816
.include "snes.inc"

.import Reset, NmiHandler, IrqHandler

.segment "SNESHEADER"

;--- $FFB0: extended header (unused; developer ID below is not $33) ----------
        .byte "  "              ; $FFB0 maker code
        .byte "KHSN"            ; $FFB2 game code
        .byte $00,$00,$00,$00,$00,$00,$00   ; $FFB6 reserved
        .byte $00               ; $FFBD expansion flash size
        .byte $00               ; $FFBE expansion RAM size
        .byte $00               ; $FFBF special version

;--- $FFC0: standard header --------------------------------------------------
        .byte "KINGDOM HEARTS SNES  "       ; $FFC0 title, exactly 21 bytes
        .byte $30               ; $FFD5 map mode: LoROM + FastROM
        .byte $00               ; $FFD6 cartridge type: ROM only
        .byte $09               ; $FFD7 ROM size: 2^9 KiB = 512 KiB
        .byte $00               ; $FFD8 RAM size: none
        .byte $01               ; $FFD9 country: USA / NTSC
        .byte $00               ; $FFDA developer ID
        .byte $00               ; $FFDB ROM version
        .word $FFFF             ; $FFDC checksum complement (patched by fixrom)
        .word $0000             ; $FFDE checksum             (patched by fixrom)

;--- $FFE0: native mode vectors ----------------------------------------------
        .word $0000, $0000      ; $FFE0 unused
        .word .loword(IrqHandler)        ; $FFE4 COP
        .word .loword(IrqHandler)        ; $FFE6 BRK
        .word .loword(IrqHandler)        ; $FFE8 ABORT
        .word .loword(NmiHandler)        ; $FFEA NMI
        .word $0000             ; $FFEC unused
        .word .loword(IrqHandler)        ; $FFEE IRQ

;--- $FFF0: emulation mode vectors -------------------------------------------
        .word $0000, $0000      ; $FFF0 unused
        .word .loword(IrqHandler)        ; $FFF4 COP
        .word $0000             ; $FFF6 unused
        .word .loword(IrqHandler)        ; $FFF8 ABORT
        .word .loword(NmiHandler)        ; $FFFA NMI
        .word .loword(Reset)             ; $FFFC RESET
        .word .loword(IrqHandler)        ; $FFFE IRQ/BRK
