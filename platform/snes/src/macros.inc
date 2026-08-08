;=============================================================================
; macros.inc -- register-width helpers and small utilities
;
; The 65816 tracks accumulator/index width in the processor status register,
; and ca65 must be told about it too (.a8/.a16/.i8/.i16) so it picks the right
; operand size. These macros keep the CPU and the assembler in agreement.
;=============================================================================
.ifndef MACROS_INC
MACROS_INC = 1

; --- accumulator width ---
.macro A8
    sep #$20
    .a8
.endmacro

.macro A16
    rep #$20
    .a16
.endmacro

; --- index width ---
.macro I8
    sep #$10
    .i8
.endmacro

.macro I16
    rep #$10
    .i16
.endmacro

; --- both ---
.macro AI8
    sep #$30
    .a8
    .i8
.endmacro

.macro AI16
    rep #$30
    .a16
    .i16
.endmacro

; Arithmetic (sign-preserving) shift right by one, 16-bit accumulator.
; The 65816 has no ASR, but CMP #$8000 sets carry exactly when bit 15 is set,
; and ROR then feeds that back into the top.
.macro ASR1
    cmp #$8000
    ror a
.endmacro

; Set the data bank register to the bank of `addr`.
; Clobbers A (8-bit on exit is the caller's problem -- call in A8 mode).
.macro SETDBR addr
    lda #^addr
    pha
    plb
.endmacro

; Push the current data bank, set a new one.
.macro PUSHDBR addr
    phb
    lda #^addr
    pha
    plb
.endmacro

; Set the 16-bit VRAM *word* address for subsequent VMDATA writes.
; Call in A16 mode. Clobbers A.
.macro VRAM_ADDR waddr
    lda #waddr
    sta VMADDL
.endmacro

.endif ; MACROS_INC
