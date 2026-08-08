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
.import DiveInit, DiveUpdate, GameOverUpdate
.import IslandUpdate
.import NightUpdate
.import TownUpdate
.import NightBegin

.import bgChr, bgChrEnd
.import objChr, objChrEnd, obj2Chr, obj2ChrEnd
.import hudChr, hudChrEnd
.import bgPal, objPal, hudPal, islePal, nightPal, nightObjPal
.import townPal, townObjPal, objTownChr, objTownChrEnd
.import fragChr, fragChrEnd, fragMap, fragColl, fragHeight
.import town1Chr, town1ChrEnd, town1Map, town1Coll, town1Height
.import town2Chr, town2ChrEnd, town2Map, town2Coll, town2Height
.import town3Chr, town3ChrEnd, town3Map, town3Coll, town3Height
.import bg1Map, collMap, heightMap, flatHeights
.import diveChr, diveChrEnd, diveMap, diveColl, divePal
.import dive2Chr, dive2ChrEnd, dive2Map, dive2Coll
.import dive3Chr, dive3ChrEnd, dive3Map, dive3Coll

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
    DMA_VRAM VRAM_OBJ2_CHR, obj2Chr, (obj2ChrEnd - obj2Chr)
    DMA_CGRAM 128, objPal, 256                  ; OBJ palettes 0-7

    ; Only the island has raised ground; the platforms are one flat plane.
    lda #<flatHeights
    sta heightPtr
    lda #>flatHeights
    sta heightPtr+1
    lda #^flatHeights
    sta heightPtr+2

    ; The Shadows have their own palette everywhere except the night, so this
    ; is the default and the night branch overrides it.
    lda #TILE_HEART0
    sta heartTile
    ; Sora is armed in every scene but the one that takes the Keyblade away
    ; from him, and that scene says so itself.
    lda #1
    sta keyGot

    ; A Station of Awakening is narrower than the screen, so the camera is
    ; pinned on it and the void around it never scrolls into view.  The island
    ; is the default: the whole 512x256 is in play.
    rep #$20
    .a16
    lda #DIVE_CAM_X
    sta camLoX
    sta camHiX
    lda #DIVE_CAM_Y
    sta camLoY
    sta camHiY
    sep #$20
    .a8

    ; Every branch is longer than a short branch can clear, so the dispatch
    ; hops through jmps.
    lda sceneId
    cmp #SCENE_DIVE2
    beq @toDive2
    cmp #SCENE_DIVE3
    beq @toDive3
    cmp #SCENE_ISLAND
    beq @toIsland
    cmp #SCENE_NIGHT
    beq @toNight
    cmp #SCENE_FRAGMENT
    beq @toFrag
    cmp #SCENE_TOWN1
    beq @toTown1
    cmp #SCENE_TOWN2
    beq @toTown2
    cmp #SCENE_TOWN3
    beq @toTown3
    jmp @dive1
@toDive2:
    jmp @dive2
@toDive3:
    jmp @dive3
@toIsland:
    jmp @island
@toNight:
    jmp @night
@toFrag:
    jmp @fragment
@toTown1:
    jmp @town1
@toTown2:
    jmp @town2
@toTown3:
    jmp @town3

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

    ;--- Station of Awakening, third platform ---
@dive3:
    DMA_VRAM VRAM_BG1_CHR, dive3Chr, (dive3ChrEnd - dive3Chr)
    DMA_VRAM VRAM_BG1_MAP, dive3Map, 4096
    DMA_CGRAM 0, divePal, 256
    lda #<dive3Coll
    sta collPtr
    lda #>dive3Coll
    sta collPtr+1
    lda #^dive3Coll
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
    lda #<heightMap
    sta heightPtr
    lda #>heightMap
    sta heightPtr+1
    lda #^heightMap
    sta heightPtr+2
    rep #$20
    .a16
    stz camLoX
    stz camLoY
    lda #CAM_MAX_X
    sta camHiX
    lda #CAM_MAX_Y
    sta camHiY
    sep #$20
    .a8
    ; The islanders take over OBJ palette 1 for the day.
    DMA_CGRAM (128 + 16), islePal, 32
    jmp @common

    ;--- the same island, the night it falls -------------------------------
    ; Same characters, same tilemap, same collision: only the palettes change,
    ; which is the whole reason the night costs almost nothing.
@night:
    DMA_VRAM VRAM_BG1_CHR, bgChr,  (bgChrEnd - bgChr)
    DMA_VRAM VRAM_BG1_MAP, bg1Map, 4096
    DMA_CGRAM 0, nightPal, 256
    lda #<collMap
    sta collPtr
    lda #>collMap
    sta collPtr+1
    lda #^collMap
    sta collPtr+2
    lda #<heightMap
    sta heightPtr
    lda #>heightMap
    sta heightPtr+1
    lda #^heightMap
    sta heightPtr+2
    rep #$20
    .a16
    stz camLoX
    stz camLoY
    lda #CAM_MAX_X
    sta camHiX
    lda #CAM_MAX_Y
    sta camHiY
    sep #$20
    .a8
    ; Riku and Kairi keep OBJ palette 1; the Shadows are cut from the three
    ; colours the islanders who are not out here were using.  Palette 2 -- the
    ; palms and the rocks -- follows it, dimmed, so the scenery is not the
    ; brightest thing on a night screen.
    DMA_CGRAM (128 + 16), nightObjPal, 64
    lda #TILE_HEART_NIGHT
    sta heartTile
    jmp @common

    ;--- what is left of it ------------------------------------------------
@fragment:
    DMA_VRAM VRAM_BG1_CHR, fragChr, (fragChrEnd - fragChr)
    DMA_VRAM VRAM_BG1_MAP, fragMap, 4096
    DMA_CGRAM 0, nightPal, 256
    lda #<fragColl
    sta collPtr
    lda #>fragColl
    sta collPtr+1
    lda #^fragColl
    sta collPtr+2
    lda #<fragHeight
    sta heightPtr
    lda #>fragHeight
    sta heightPtr+1
    lda #^fragHeight
    sta heightPtr+2
    ; Nobody is left out here but Sora, the thing he is fighting and one tree,
    ; so OBJ palette 1 goes back to the Heartless and Darkside is drawn as it
    ; was in the Dive.  The tree keeps the night's dimmed scenery colours.
    DMA_CGRAM (128 + 32), nightObjPal + 32, 32
    rep #$20
    .a16
    lda #FRAG_CAM_X
    sta camLoX
    sta camHiX
    lda #FRAG_CAM_Y
    sta camLoY
    sta camHiY
    sep #$20
    .a8
    jmp @common

    ;--- Traverse Town -----------------------------------------------------
    ; Three districts of one screen each, differing only in which four
    ; binaries they pull.  The town's cast replaces the islanders on the
    ; second sprite page, and its two OBJ palettes land over the two the
    ; island was using -- palette 1 carries the Heartless colours in the same
    ; three slots the night put them in, so TILE_HEART_NIGHT is the right cut
    ; here too and costs nothing.
@town1:
    DMA_VRAM VRAM_BG1_CHR, town1Chr, (town1ChrEnd - town1Chr)
    DMA_VRAM VRAM_BG1_MAP, town1Map, 4096
    lda #<town1Coll
    sta collPtr
    lda #>town1Coll
    sta collPtr+1
    lda #^town1Coll
    sta collPtr+2
    lda #<town1Height
    sta heightPtr
    lda #>town1Height
    sta heightPtr+1
    lda #^town1Height
    sta heightPtr+2
    jmp @townCommon

@town2:
    DMA_VRAM VRAM_BG1_CHR, town2Chr, (town2ChrEnd - town2Chr)
    DMA_VRAM VRAM_BG1_MAP, town2Map, 4096
    lda #<town2Coll
    sta collPtr
    lda #>town2Coll
    sta collPtr+1
    lda #^town2Coll
    sta collPtr+2
    lda #<town2Height
    sta heightPtr
    lda #>town2Height
    sta heightPtr+1
    lda #^town2Height
    sta heightPtr+2
    jmp @townCommon

@town3:
    DMA_VRAM VRAM_BG1_CHR, town3Chr, (town3ChrEnd - town3Chr)
    DMA_VRAM VRAM_BG1_MAP, town3Map, 4096
    lda #<town3Coll
    sta collPtr
    lda #>town3Coll
    sta collPtr+1
    lda #^town3Coll
    sta collPtr+2
    lda #<town3Height
    sta heightPtr
    lda #>town3Height
    sta heightPtr+1
    lda #^town3Height
    sta heightPtr+2

@townCommon:
    DMA_CGRAM 0, townPal, 256
    DMA_VRAM VRAM_OBJ2_CHR, objTownChr, (objTownChrEnd - objTownChr)
    DMA_CGRAM (128 + 16), townObjPal, 64
    lda #TILE_HEART_NIGHT
    sta heartTile
    rep #$20
    .a16
    stz camLoX
    stz camLoY
    lda #CAM_MAX_X
    sta camHiX
    lda #CAM_MAX_Y
    sta camHiY
    sep #$20
    .a8

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
    sta cgwselVal
    sta CGWSEL
    lda #CGADSUB_VAL
    sta cgadsubVal
    sta CGADSUB
    rts
.endproc

;-----------------------------------------------------------------------------
; SceneUpdate -- run the script that owns the current scene.  A8/I16.
;-----------------------------------------------------------------------------
.proc SceneUpdate
    .a8
    .i16
    ; Being out of HP takes priority over whatever the scene was doing.
    lda deadFlag
    beq @alive
    jsr GameOverUpdate
    rts

    ; The opening script owns the transition onto Destiny Islands too, so it
    ; keeps running after the scene changes -- the island only takes over once
    ; the dive has reached its terminal state.
@alive:
    lda sceneId
    cmp #SCENE_TOWN1
    bcc :+
    jsr TownUpdate              ; the three districts
    rts
:   cmp #SCENE_NIGHT
    bcc :+
    jsr NightUpdate             ; SCENE_NIGHT and SCENE_FRAGMENT
    rts
:   cmp #SCENE_ISLAND
    bne @dive
    lda diveStage
    cmp #DIVE_ARRIVED
    bne @dive
    jsr IslandUpdate
    rts
@dive:
    jsr DiveUpdate
    rts
.endproc

.segment "RODATA"
zeroWord:   .word $0000
