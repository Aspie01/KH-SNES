;=============================================================================
; main.s -- reset, hardware bring-up, and the frame loop
;
; Convention used throughout the engine: routines are entered and left with
; an 8-bit accumulator and 16-bit index registers (A8/I16).  Anything that
; needs 16-bit arithmetic switches locally and switches back.
;=============================================================================
.p816
.include "snes.inc"
.include "game.inc"
.include "ram.inc"
.include "macros.inc"
.include "dma.inc"

.import InitWorld, UpdateWorld
.import BuildOam, ClearOamBuffer
.import UpdateCamera
.import ReadPad
.import HudInit
.import TextInit, TextUpdate
.import DiveInit, DiveUpdate

.import bgChr, bgChrEnd
.import objChr, objChrEnd
.import hudChr, hudChrEnd
.import bgPal, objPal, hudPal
.import bg1Map, collMap
.import diveChr, diveChrEnd, diveMap, diveColl, divePal
.import dive2Chr, dive2ChrEnd, dive2Map, dive2Coll

.export Reset, IrqHandler, WaitVBlank, LoadScene

;-----------------------------------------------------------------------------
; CLEAR_WRAM -- zero all 128 KiB of work RAM through the WMDATA port.
;
; This has to be a macro, not a subroutine.  WRAM $7E0000-$7E1FFF is the same
; memory the CPU sees at $0000-$1FFF, and the stack lives at $1800-$1FFF -- so
; the transfer overwrites any return address parked there, and an RTS after it
; would jump into nothing.  The stack *pointer* is a CPU register and survives
; fine; only the bytes are lost.
;
; Assumes A8/I16.  Clobbers A and X.
;-----------------------------------------------------------------------------
.macro CLEAR_WRAM
    stz WMADDL
    stz WMADDM
    stz WMADDH                  ; WRAM port -> $7E0000

    lda #$08                    ; fixed source, mode 0 (1 byte -> 1 register)
    sta DMAP0
    lda #<WMDATA
    sta BBAD0
    ldx #.loword(zeroWord)
    stx A1T0L
    lda #^zeroWord
    sta A1B0

    ldx #$0000                  ; a length of 0 means 65536 bytes
    stx DAS0L
    lda #$01
    sta MDMAEN

    ldx #$0000                  ; second bank ($7F0000)
    stx DAS0L
    lda #$01
    sta MDMAEN
.endmacro

.segment "CODE"

;-----------------------------------------------------------------------------
; Reset -- entered in 6502 emulation mode with interrupts off.
;-----------------------------------------------------------------------------
.proc Reset
    sei
    clc
    xce                         ; leave emulation mode
    rep #$38                    ; A16, I16, decimal off
    .a16
    .i16
    ldx #$1FFF
    txs                         ; stack lives just under the BSS ceiling
    lda #$0000
    tcd                         ; direct page = $0000
    phk
    plb                         ; data bank = program bank ($80)

    sep #$20
    .a8
    lda #$8F
    sta INIDISP                 ; forced blank while we touch VRAM
    stz NMITIMEN
    stz HDMAEN
    stz MDMAEN
    lda #$01
    sta MEMSEL                  ; FastROM: 3.58 MHz in banks $80+

    jsr ClearPpuRegs
    CLEAR_WRAM                  ; inline: it erases its own return address
    jsr ClearVram
    jsr ClearCgram
    lda #SCENE_DIVE
    jsr LoadScene
    jsr SetupPpu

    jsr ClearOamBuffer
    jsr HudInit
    jsr TextInit
    jsr DiveInit
    jsr UpdateCamera
    jsr BuildOam

    ; Bring the screen up and arm the vblank interrupt.  The NMI drives
    ; INIDISP from screenBright from here on, so set it there too.
    lda #$0F
    sta screenBright
    sta INIDISP
    stz mosaicAmt
    stz shakeX
    lda #$81
    sta NMITIMEN                ; NMI enable + auto joypad read
    cli

MainLoop:
    jsr WaitVBlank              ; the NMI just uploaded the previous frame
    jsr ReadPad
    jsr TextUpdate
    jsr SceneUpdate
    jsr UpdateWorld
    jsr UpdateCamera
    jsr BuildOam
    lda #$01
    sta oamDirty                ; tell the NMI there is a frame to upload
    bra MainLoop
.endproc

;-----------------------------------------------------------------------------
; WaitVBlank -- block until the NMI handler has run once.  A8/I16.
;-----------------------------------------------------------------------------
.proc WaitVBlank
    .a8
    .i16
    stz vblankFlag
@wait:
    lda vblankFlag
    beq @wait
    rts
.endproc

;-----------------------------------------------------------------------------
; IrqHandler -- nothing uses IRQ, COP, BRK or ABORT yet.
;-----------------------------------------------------------------------------
.proc IrqHandler
    rti
.endproc

;-----------------------------------------------------------------------------
; ClearPpuRegs -- put every PPU register into a known, quiet state.  A8/I16.
;-----------------------------------------------------------------------------
.proc ClearPpuRegs
    .a8
    .i16
    stz OBSEL
    stz OAMADDL
    stz OAMADDH
    stz BGMODE
    stz MOSAIC
    stz BG1SC
    stz BG2SC
    stz BG3SC
    stz BG4SC
    stz BG12NBA
    stz BG34NBA

    ; Scroll registers are write-twice; clear both halves.
    ldx #$0000
    stz BG1HOFS
    stz BG1HOFS
    stz BG1VOFS
    stz BG1VOFS
    stz BG2HOFS
    stz BG2HOFS
    stz BG2VOFS
    stz BG2VOFS
    stz BG3HOFS
    stz BG3HOFS
    stz BG3VOFS
    stz BG3VOFS
    stz BG4HOFS
    stz BG4HOFS
    stz BG4VOFS
    stz BG4VOFS

    stz M7SEL
    stz W12SEL
    stz W34SEL
    stz WOBJSEL
    stz WH0
    stz WH1
    stz WH2
    stz WH3
    stz WBGLOG
    stz WOBJLOG
    stz TM
    stz TS
    stz TMW
    stz TSW
    stz CGWSEL
    stz CGADSUB
    lda #$E0                    ; COLDATA: clear the fixed colour to black
    sta COLDATA
    stz SETINI
    rts
.endproc

;-----------------------------------------------------------------------------
; ClearVram / ClearCgram.  A8/I16.
;-----------------------------------------------------------------------------
.proc ClearVram
    .a8
    .i16
    DMA_VRAM_FILL $0000, zeroWord, $0000    ; 65536 bytes = all of VRAM
    rts
.endproc

.proc ClearCgram
    .a8
    .i16
    stz CGADD
    lda #$08                    ; fixed source, mode 0
    sta DMAP0
    lda #<CGDATA
    sta BBAD0
    ldx #.loword(zeroWord)
    stx A1T0L
    lda #^zeroWord
    sta A1B0
    ldx #512
    stx DAS0L
    lda #$01
    sta MDMAEN
    rts
.endproc

;-----------------------------------------------------------------------------
; LoadScene -- upload the tiles, tilemap, palette and collision map for one
; scene.  Must run during forced blank.  In (A8/I16): A = scene id.
;-----------------------------------------------------------------------------
.proc LoadScene
    .a8
    .i16
    sta sceneId
    lda #$8F
    sta INIDISP                 ; forced blank: VRAM is only writable now

    ; Sprites and font are the same in every scene.
    DMA_VRAM VRAM_BG3_CHR, hudChr, (hudChrEnd - hudChr)
    DMA_VRAM VRAM_OBJ_CHR, objChr, (objChrEnd - objChr)
    DMA_CGRAM 128, objPal, 256                  ; OBJ palettes 0-7

    ; Every branch is longer than a short branch can clear, so the dispatch
    ; hops through jmps.
    lda sceneId
    cmp #SCENE_DIVE2
    beq @toDive2
    cmp #SCENE_ISLAND
    beq @toIsland
    jmp @dive1
@toDive2:
    jmp @dive2
@toIsland:
    jmp @island

    ;--- Station of Awakening, first platform ---
@dive1:
    DMA_VRAM VRAM_BG1_CHR, diveChr, (diveChrEnd - diveChr)
    DMA_VRAM VRAM_BG1_MAP, diveMap, 4096
    DMA_CGRAM 0, divePal, 256
    lda #<diveColl
    sta collPtr
    lda #>diveColl
    sta collPtr+1
    lda #^diveColl
    sta collPtr+2
    jmp @common

    ;--- Station of Awakening, second platform (shares the glass palette) ---
@dive2:
    DMA_VRAM VRAM_BG1_CHR, dive2Chr, (dive2ChrEnd - dive2Chr)
    DMA_VRAM VRAM_BG1_MAP, dive2Map, 4096
    DMA_CGRAM 0, divePal, 256
    lda #<dive2Coll
    sta collPtr
    lda #>dive2Coll
    sta collPtr+1
    lda #^dive2Coll
    sta collPtr+2
    jmp @common

    ;--- Destiny Islands ---
@island:
    DMA_VRAM VRAM_BG1_CHR, bgChr,  (bgChrEnd - bgChr)
    DMA_VRAM VRAM_BG1_MAP, bg1Map, 4096         ; 64x32 entries, 2 bytes each
    DMA_CGRAM 0, bgPal, 256
    lda #<collMap
    sta collPtr
    lda #>collMap
    sta collPtr+1
    lda #^collMap
    sta collPtr+2

@common:
    ; The scene's BG palette covers CGRAM 0-127, so the HUD's four colours
    ; have to land after it.  BG3 is 2bpp, so its palette 4 is colours 16-19 --
    ; clear of the sixteen the ground occupies in BG palette 0.
    DMA_CGRAM 16, hudPal, 8
    rts
.endproc

;-----------------------------------------------------------------------------
; SetupPpu -- select the video mode and enable layers.  A8/I16.
;-----------------------------------------------------------------------------
.proc SetupPpu
    .a8
    .i16
    lda #BGMODE_VAL
    sta BGMODE
    lda #BG1SC_VAL
    sta BG1SC
    lda #BG3SC_VAL
    sta BG3SC
    lda #BG12NBA_VAL
    sta BG12NBA
    lda #BG34NBA_VAL
    sta BG34NBA
    lda #OBSEL_VAL
    sta OBSEL
    lda #TM_VAL
    sta TM
    ; BG1 is repeated on the sub screen purely as the colour-math operand;
    ; half-adding a black shadow sprite against it darkens the ground by 50%
    ; instead of stamping an opaque blob on it.
    lda #TS_VAL
    sta TS
    lda #CGWSEL_VAL
    sta CGWSEL
    lda #CGADSUB_VAL
    sta CGADSUB
    rts
.endproc

;-----------------------------------------------------------------------------
; SceneUpdate -- run the script that owns the current scene.  A8/I16.
;-----------------------------------------------------------------------------
.proc SceneUpdate
    .a8
    .i16
    ; Both Stations of Awakening run the same script; only the island opts out.
    lda sceneId
    cmp #SCENE_ISLAND
    beq @island
    jsr DiveUpdate
@island:
    rts
.endproc

.segment "RODATA"
zeroWord:   .word $0000
