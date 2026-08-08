;=============================================================================
; night.s -- the night the island falls
;
; The last scene of the opening, and the one everything else has been building
; toward.  It runs on the same map, the same tileset and the same tilemap as
; the two days do: what makes it night is one palette upload.  The only new
; ground in the game is the fragment at the end, which is a second small map.
;
; The sequence, in order, is what nightStage holds:
;
;   N_INTRO   the storm; the raft is already gone
;   N_SEEK    Shadows are arriving.  Sora has a wooden sword and it goes
;             straight through them, so the only thing to do is find Riku
;   N_RIKU    he has said his piece, and the dark is taking him
;   N_KEY     the Keyblade arrives, and the Shadows become killable
;   N_KAIRI   the Secret Place, and who is standing in it
;   N_DOOR    the door comes off the rock and she goes with it
;   N_TEAR    the island comes apart
;   N_BOSS    Darkside, on the last piece of it
;   N_END     beaten; the dark takes the rest
;   N_OVER    the card
;
; Two things are deliberately not faithful.  The Shadows cannot hurt Sora
; before the Keyblade -- in the source they can, but here there would be no
; way to answer them, and a death loop in a corridor with one exit is not
; tension.  And he keeps the drawn Keyblade in his hand throughout: a second
; thirty-cel sheet of him holding a wooden sword is not worth 15 KiB.
;=============================================================================
.p816
.include "snes.inc"
.include "game.inc"
.include "ram.inc"
.include "macros.inc"
.include "text.inc"

.import TextOpen, TextBusy
.import HudUpdate
.import ClearActors, SpawnActor, SpawnTable, PlayerPos, NearPlayer, CountType
.import LoadScene

.export NightBegin, NightUpdate, NightRestart, NightStageLabel

.segment "CODE"

;=============================================================================
; Starting, and starting again
;=============================================================================

;-----------------------------------------------------------------------------
; NightBegin -- the island, after dark.  Called with the screen already dimmed
; to nothing by the last evening's fade.  A8/I16.
;-----------------------------------------------------------------------------
.proc NightBegin
    .a8
    .i16
    lda #$8F
    sta screenBright
    sta INIDISP                 ; forced blank: LoadScene writes VRAM

    lda #SCENE_NIGHT
    jsr LoadScene
    jsr ClearActors
    rep #$20
    .a16
    lda #.loword(nightSpawns)
    sta tmp8
    sep #$20
    .a8
    jsr SpawnTable

    stz keyGot                  ; a wooden sword, until Riku is found
    stz saidNoUse
    stz nightStage              ; N_INTRO
    stz nightTimer
    stz bossHP
    lda #SHADOW_GAP
    sta spawnTimer
    jsr ArmLightning
    jsr HudUpdate

    ; No fade back in: the storm arrives with the first flash of lightning.
    lda #$0F
    sta screenBright
    lda #FLASH_LEN
    sta flashTimer

    rep #$20
    .a16
    lda #.loword(scriptStorm)
    jmp Say
.endproc

;-----------------------------------------------------------------------------
; NightRestart -- put the current beat back on its feet after a death.
; The scene is already loaded and the actors already cleared.  A8/I16.
;-----------------------------------------------------------------------------
.proc NightRestart
    .a8
    .i16
    lda #SHADOW_GAP
    sta spawnTimer
    stz flashTimer
    jsr ArmLightning
    stz bossHP
    ; A death can land in the middle of the island coming apart, so put the
    ; screen-wide effects back before anything else.
    stz mosaicAmt
    stz shakeX
    jsr ShadowMath

    lda sceneId
    cmp #SCENE_FRAGMENT
    beq @onFragment

    ; Back on the island: the cast goes back where it was, and the stage is
    ; wound back to whichever of the two searches was in progress.
    rep #$20
    .a16
    lda #.loword(nightSpawns)
    sta tmp8
    sep #$20
    .a8
    jsr SpawnTable
    lda nightStage
    cmp #N_KAIRI
    bcc @seek
    lda #N_KAIRI                ; the Keyblade is not taken back
    sta nightStage
    jsr OpenTheDoor
    bra @done
@seek:
    stz keyGot                  ; LoadScene hands it back; he has not earned it
    lda #N_SEEK
    sta nightStage
    bra @done

@onFragment:
    rep #$20
    .a16
    lda #.loword(fragSpawns)
    sta tmp8
    sep #$20
    .a8
    jsr SpawnTable
    lda #1
    sta keyGot
    lda #N_BOSS
    sta nightStage
    jsr RaiseDarkside

@done:
    jsr HudUpdate
    rts
.endproc

;=============================================================================
; The frame
;=============================================================================

;-----------------------------------------------------------------------------
; NightUpdate -- A8/I16.
;-----------------------------------------------------------------------------
.proc NightUpdate
    .a8
    .i16
    ; The storm runs through whatever else is happening -- but not past the
    ; end of it: from N_END on, the colour-math unit belongs to the fade, and
    ; a flash resetting it back to translucent shadows would undo the fade
    ; every time one landed.
    lda nightStage
    cmp #N_END
    bcs :+
    jsr Lightning
:
    ; The two effects stages own the screen and ignore the dialogue box.
    lda nightStage
    cmp #N_TEAR
    bne :+
    jmp Tear
:   cmp #N_END
    bne :+
    jmp EndFade
:
    jsr TextBusy
    bcc @free
    rts                         ; somebody is talking

@free:
    lda nightStage
    cmp #N_INTRO
    bne :+
    jmp AfterStorm
:   cmp #N_SEEK
    bne :+
    jmp SeekRiku
:   cmp #N_RIKU
    bne :+
    jmp TakeRiku
:   cmp #N_KEY
    bne :+
    jmp GiveKeyblade
:   cmp #N_KAIRI
    bne :+
    jmp SeekKairi
:   cmp #N_DOOR
    bne :+
    jmp LoseKairi
:   cmp #N_BOSS
    bne :+
    jmp WatchBoss
:   rts                         ; N_OVER: the card stays up
.endproc

;=============================================================================
; Lightning
;
; Four frames of additive white through the colour-math unit -- the same path
; the whiteout at the end of the Dive takes, with the registers shadowed in RAM
; so the switch lands in vblank instead of tearing a seam across the frame.
;=============================================================================

;-----------------------------------------------------------------------------
; ArmLightning -- set the wait to the next flash.  A8/I16.
;-----------------------------------------------------------------------------
.proc ArmLightning
    .a8
    .i16
    jsr Rand
    and #FLASH_GAP_VAR
    rep #$20
    .a16
    and #$00FF
    clc
    adc #FLASH_GAP_MIN
    sta flashWait
    sep #$20
    .a8
    rts
.endproc

;-----------------------------------------------------------------------------
; Lightning -- one frame of the storm.  A8/I16.
;-----------------------------------------------------------------------------
.proc Lightning
    .a8
    .i16
    lda flashTimer
    beq @waiting

    dec flashTimer
    ; Two strikes in the one flash: bright, out, brighter, out.  The ramp is
    ; read off the timer so it costs no extra state.
    ldx #0
    lda flashTimer
    cmp #(FLASH_LEN - 2)
    bcs @lit                    ; frames 8-7: the first stab
    cmp #(FLASH_LEN - 4)
    bcs @dim                    ; frames 6-5: dark again
    cmp #2
    bcc @dim                    ; frames 1-0: falling away
    ldx #26                     ; frames 4-2: the one that lights the island
    bra @put
@lit:
    ldx #16
    bra @put
@dim:
    ldx #0
@put:
    stx tmp0
    lda tmp0
    sta coldataAmt
    beq @off
    stz cgwselVal               ; the fixed colour is the second operand
    lda #$3F                    ; add, no halving, every layer and the backdrop
    sta cgadsubVal
    rts
@off:
    jsr ShadowMath
    rts

@waiting:
    rep #$20
    .a16
    lda flashWait
    beq @strike
    dec a
    sta flashWait
    sep #$20
    .a8
    rts
@strike:
    sep #$20
    .a8
    lda #FLASH_LEN
    sta flashTimer
    jmp ArmLightning
.endproc

;-----------------------------------------------------------------------------
; ShadowMath -- put the colour-math unit back to translucent shadows.  A8/I16.
;-----------------------------------------------------------------------------
.proc ShadowMath
    .a8
    .i16
    stz coldataAmt
    lda #CGWSEL_VAL
    sta cgwselVal
    lda #CGADSUB_VAL
    sta cgadsubVal
    rts
.endproc

;-----------------------------------------------------------------------------
; Rand -- advance rngState, return A = its low byte.  A maximal 16-bit Galois
; LFSR: one shift and a conditional xor.  A8/I16.
;-----------------------------------------------------------------------------
.proc Rand
    .a8
    .i16
    rep #$20
    .a16
    lda rngState
    asl a
    bcc :+
    eor #$002D
:   sta rngState
    sep #$20
    .a8
    rts
.endproc

;=============================================================================
; The Shadows
;=============================================================================

;-----------------------------------------------------------------------------
; SpawnShadows -- keep the island populated, up to SHADOW_MAX.  A8/I16.
;-----------------------------------------------------------------------------
.proc SpawnShadows
    .a8
    .i16
    lda spawnTimer
    beq @due
    dec spawnTimer
    rts

@due:
    lda #SHADOW_GAP
    sta spawnTimer
    lda #ACT_SHADOW
    jsr CountType
    cmp #SHADOW_MAX
    bcs @out

    ; Somewhere on the island, but not on top of the player: one arriving in
    ; his face reads as a bug rather than as a Heartless.
    jsr Rand
    rep #$20
    .a16
    and #$00FF
@wrap:
    cmp #NIGHT_SPOTS
    bcc :+
    sec
    sbc #NIGHT_SPOTS
    bra @wrap
:   asl a                       ; two bytes a spot
    tax
    sep #$20
    .a8
    lda nightSpots,x
    sta tmp4
    lda nightSpots+1,x
    sta tmp5
    jsr SpotToWorld
    jsr PlayerPos               ; tmp0/tmp1 -> Sora; the spot is in tmp6/tmp9
    rep #$20
    .a16
    lda tmp6
    sec
    sbc tmp0
    bpl :+
    eor #$FFFF
    inc a
:   cmp #1024                   ; 64 px
    bcs @far
    lda tmp9
    sec
    sbc tmp1
    bpl :+
    eor #$FFFF
    inc a
:   cmp #1024
    bcc @tooClose

@far:
    lda tmp6
    sta tmp0
    lda tmp9
    sta tmp1
    sep #$20
    .a8
    lda #ACT_SHADOW
    jsr SpawnActor
@out:
    rts

@tooClose:
    sep #$20
    .a8
    lda #12                     ; try again shortly
    sta spawnTimer
    rts
.endproc

;-----------------------------------------------------------------------------
; SpotToWorld -- tile tmp4/tmp5 to the middle of that cell, Q12.4.
; Out: tmp6 = X, tmp9 = Y.  A8/I16.
;-----------------------------------------------------------------------------
.proc SpotToWorld
    .a8
    .i16
    rep #$20
    .a16
    lda tmp4
    and #$00FF
    asl a
    asl a
    asl a
    asl a                       ; i * TILE_PX
    clc
    adc #(TILE_PX / 2)
    asl a
    asl a
    asl a
    asl a                       ; ...and into Q12.4
    sta tmp6
    lda tmp5
    and #$00FF
    asl a
    asl a
    asl a
    asl a
    clc
    adc #(TILE_PX / 2)
    asl a
    asl a
    asl a
    asl a
    sta tmp9
    sep #$20
    .a8
    rts
.endproc

;=============================================================================
; The beats
;=============================================================================

;-----------------------------------------------------------------------------
; AfterStorm -- the opening line has been dismissed.  A8/I16.
;-----------------------------------------------------------------------------
.proc AfterStorm
    .a8
    .i16
    lda #N_SEEK
    sta nightStage
    jsr HudUpdate
    rts
.endproc

;-----------------------------------------------------------------------------
; SeekRiku -- Shadows arriving, and one thing worth doing.  A8/I16.
;-----------------------------------------------------------------------------
.proc SeekRiku
    .a8
    .i16
    jsr SpawnShadows

    rep #$20
    .a16
    lda padPressed
    and #PAD_A
    sep #$20
    .a8
    beq @notA
    lda #ACT_RIKU
    jsr TalkTarget
    bcc @notA
    lda #N_RIKU
    sta nightStage
    lda #DARK_HOLD
    sta nightTimer
    jsr HudUpdate
    rep #$20
    .a16
    lda #.loword(scriptRiku)
    jmp Say

@notA:
    ; Swinging at them is worth exactly one line, the first time.
    lda saidNoUse
    bne @out
    rep #$20
    .a16
    lda padPressed
    and #PAD_B
    sep #$20
    .a8
    beq @out
    lda #ACT_SHADOW
    jsr TalkTarget              ; one of them within arm's reach
    bcc @out
    lda #1
    sta saidNoUse
    rep #$20
    .a16
    lda #.loword(scriptNoUse)
    jmp Say
@out:
    rts
.endproc

;-----------------------------------------------------------------------------
; TakeRiku -- his lines are done; the dark comes up round him.  A8/I16.
;-----------------------------------------------------------------------------
.proc TakeRiku
    .a8
    .i16
    lda nightTimer
    cmp #DARK_HOLD
    bne @wait

    ; First pass: swap him for the column of darkness, in his place.
    dec nightTimer
    ldx #0
@find:
    lda actType,x
    cmp #ACT_RIKU
    beq @got
    inx
    cpx #MAX_ACTORS
    bcc @find
    rts
@got:
    stx tmp7
    rep #$20
    .a16
    lda tmp7
    asl a
    tax
    lda actX,x
    sta tmp0
    lda actY,x
    sta tmp1
    sep #$20
    .a8
    ldx tmp7
    stz actType,x               ; Riku is not there any more
    lda #ACT_DARK
    jsr SpawnActor
    rts

@wait:
    lda nightTimer
    beq @gone
    dec nightTimer
    ; The column flickers between its two cels.
    lda frameCount
    and #$04
    beq :+
    lda #(TILE_DARK + 4)
    bra :++
:   lda #TILE_DARK
:   sta tmp4
    ldx #0
@cel:
    lda actType,x
    cmp #ACT_DARK
    bne :+
    lda tmp4
    sta actTile,x
:   inx
    cpx #MAX_ACTORS
    bcc @cel
    rts

@gone:
    lda #N_KEY
    sta nightStage
    rep #$20
    .a16
    lda #.loword(scriptRikuGone)
    jmp Say
.endproc

;-----------------------------------------------------------------------------
; GiveKeyblade -- the dark thins out, and there is something in his hand.
; A8/I16.
;-----------------------------------------------------------------------------
.proc GiveKeyblade
    .a8
    .i16
    ; Clear the column away.
    ldx #0
@clear:
    lda actType,x
    cmp #ACT_DARK
    bne :+
    stz actType,x
:   inx
    cpx #MAX_ACTORS
    bcc @clear

    lda #1
    sta keyGot
    lda #N_KAIRI
    sta nightStage
    lda #FLASH_LEN
    sta flashTimer              ; it arrives on a flash of its own
    jsr HudUpdate
    rep #$20
    .a16
    lda #.loword(scriptKey)
    jmp Say
.endproc

;-----------------------------------------------------------------------------
; SeekKairi -- the Shadows can be answered now; she is in the Secret Place.
; A8/I16.
;-----------------------------------------------------------------------------
.proc SeekKairi
    .a8
    .i16
    jsr SpawnShadows

    rep #$20
    .a16
    lda padPressed
    and #PAD_A
    sep #$20
    .a8
    beq @out
    lda #ACT_KAIRI
    jsr TalkTarget
    bcc @out
    lda #N_DOOR
    sta nightStage
    lda #DARK_HOLD
    sta nightTimer
    jsr OpenTheDoor
    jsr HudUpdate
    rep #$20
    .a16
    lda #.loword(scriptKairi)
    jmp Say
@out:
    rts
.endproc

;-----------------------------------------------------------------------------
; OpenTheDoor -- swap the door on the wall for what is behind it.  A8/I16.
;-----------------------------------------------------------------------------
.proc OpenTheDoor
    .a8
    .i16
    ldx #0
@scan:
    lda actType,x
    cmp #ACT_DOOR
    bne @next
    lda #ACT_DOOROPEN
    sta actType,x
    lda #TILE_DOOROPEN
    sta actTile,x
@next:
    inx
    cpx #MAX_ACTORS
    bcc @scan
    rts
.endproc

;-----------------------------------------------------------------------------
; LoseKairi -- she goes through him, and then the island goes.  A8/I16.
;-----------------------------------------------------------------------------
.proc LoseKairi
    .a8
    .i16
    lda nightTimer
    cmp #DARK_HOLD
    bne @wait

    ; First pass: she is gone, and a column of dark is standing where she was.
    dec nightTimer
    ldx #0
@find:
    lda actType,x
    cmp #ACT_KAIRI
    beq @got
    inx
    cpx #MAX_ACTORS
    bcc @find
    rts
@got:
    stx tmp7
    rep #$20
    .a16
    lda tmp7
    asl a
    tax
    lda actX,x
    sta tmp0
    lda actY,x
    sta tmp1
    sep #$20
    .a8
    ldx tmp7
    stz actType,x
    lda #ACT_DARK
    jsr SpawnActor
    lda #FLASH_LEN
    sta flashTimer
    rts

@wait:
    lda nightTimer
    beq @gone
    dec nightTimer
    rts

@gone:
    lda #N_TEAR
    sta nightStage
    lda #TEAR_LEN
    sta nightTimer
    jsr ShadowMath              ; the flash must not be left switched on
    rep #$20
    .a16
    lda #.loword(scriptTorn)
    jmp Say
.endproc

;-----------------------------------------------------------------------------
; Tear -- the island comes apart, the same way the platform did: MOSAIC
; coarsening BG1 while the brightness falls and the screen shakes.  This stage
; runs through the dialogue box rather than waiting for it, so the line about
; it is on screen while it happens.  A8/I16.
;-----------------------------------------------------------------------------
.proc Tear
    .a8
    .i16
    lda nightTimer
    beq @land
    dec nightTimer

    ; elapsed, scaled into the 0-15 the registers take
    lda #TEAR_LEN
    sec
    sbc nightTimer
    lsr a
    lsr a
    lsr a                       ; /8 -> 0..15
    cmp #16
    bcc :+
    lda #15
:   sta tmp2

    asl a
    asl a
    asl a
    asl a
    ora #$01                    ; mosaic size in the high nibble, BG1 enabled
    sta mosaicAmt

    lda #15
    sec
    sbc tmp2
    sta screenBright

    lda frameCount
    and #$02
    beq :+
    lda #$FD                    ; -3
    bra :++
:   lda #$03
:   sta shakeX
    rts

@land:
    stz shakeX
    stz mosaicAmt
    lda #$8F
    sta screenBright
    sta INIDISP

    lda #SCENE_FRAGMENT
    jsr LoadScene
    jsr ClearActors
    rep #$20
    .a16
    lda #.loword(fragSpawns)
    sta tmp8
    sep #$20
    .a8
    jsr SpawnTable
    lda #1
    sta keyGot
    jsr RaiseDarkside

    lda #$0F
    sta screenBright
    lda #N_BOSS
    sta nightStage
    jsr HudUpdate
    rep #$20
    .a16
    lda #.loword(scriptFragment)
    jmp Say
.endproc

;-----------------------------------------------------------------------------
; RaiseDarkside -- the boss, on the fragment.  A8/I16.
;-----------------------------------------------------------------------------
.proc RaiseDarkside
    .a8
    .i16
    lda #15                     ; tile (15,7): as high up the scrap as a 64 px
    sta tmp4                    ; sprite can stand and keep its head clear of
    lda #7                      ; the HUD
    sta tmp5
    jsr SpotToWorld
    rep #$20
    .a16
    lda tmp6
    sta tmp0
    lda tmp9
    sta tmp1
    sep #$20
    .a8
    lda #ACT_DARKSIDE
    jsr SpawnActor
    bcc @out
    lda #DS_MAX_HP
    sta bossHP
    lda #DS_REST
    sta actTimer,x
@out:
    rts
.endproc

;-----------------------------------------------------------------------------
; WatchBoss -- A8/I16.
;-----------------------------------------------------------------------------
.proc WatchBoss
    .a8
    .i16
    lda #ACT_DARKSIDE
    jsr CountType
    bne @out
    stz bossHP

    ; Whatever it left behind goes with it.
    ldx #0
@sweep:
    lda actType,x
    cmp #ACT_SHADOW
    bne :+
    stz actType,x
:   inx
    cpx #MAX_ACTORS
    bcc @sweep

    lda #N_END
    sta nightStage
    lda #END_FADE
    sta nightTimer
    ; Subtract the fixed colour from BG1, the sprites and the backdrop, but
    ; not from BG3 -- so the world goes black while the box stays readable.
    stz coldataAmt
    stz cgwselVal
    lda #$B1
    sta cgadsubVal
    jsr HudUpdate
@out:
    rts
.endproc

;-----------------------------------------------------------------------------
; EndFade -- the dark takes the rest.
;
; Subtractive colour math on BG1, the sprites and the backdrop, ramped to full.
; BG3 is left out so the closing line stays readable -- and the SNES only
; applies OBJ colour math to palettes 4-7, so Sora, on palette 0, does not
; darken with the world.  That was not the plan, but it is the right picture:
; everything goes, and he is left standing in it holding the Keyblade.
; A8/I16.
;-----------------------------------------------------------------------------
.proc EndFade
    .a8
    .i16
    lda nightTimer
    beq @card
    dec nightTimer

    ; 0 -> 31 as the timer runs out.  END_FADE is 62, so halving the elapsed
    ; count lands exactly on the five bits COLDATA takes.
    lda #END_FADE
    sec
    sbc nightTimer
    lsr a
    cmp #32
    bcc :+
    lda #31
:   sta coldataAmt
    rts

@card:
    lda #$FF
    sta coldataAmt              ; clamped to five bits by the register
    lda #N_OVER
    sta nightStage
    rep #$20
    .a16
    lda #.loword(scriptCard)
    jmp Say
.endproc

;=============================================================================
; Helpers
;=============================================================================

;-----------------------------------------------------------------------------
; TalkTarget -- is an actor of this type within talking range?
; In (A8/I16): A = actor type.  Out: carry set when one is.  Clobbers tmp0-tmp4.
;-----------------------------------------------------------------------------
.proc TalkTarget
    .a8
    .i16
    sta tmp4
    jsr PlayerPos
    rep #$20
    .a16
    lda #TALK_X
    sta tmp2
    lda #TALK_Y
    sta tmp3
    sep #$20
    .a8

    ldx #0
@scan:
    lda actType,x
    cmp tmp4
    bne @next
    jsr NearPlayer
    bcs @hit
@next:
    inx
    cpx #MAX_ACTORS
    bcc @scan
    clc
    rts
@hit:
    sec
    rts
.endproc

;-----------------------------------------------------------------------------
; Say -- open a box on the script whose address is in A.  A16/I16 in, A8 out.
;-----------------------------------------------------------------------------
.proc Say
    .a16
    .i16
    sta txtPtr
    sep #$20
    .a8
    lda #^scriptStorm           ; every line here shares one bank
    sta txtPtr+2
    lda #TM_MESSAGE
    jsr TextOpen
    rts
.endproc

;-----------------------------------------------------------------------------
; NightStageLabel -- which objective line the HUD should show, as an index
; into the table hud.s holds.  Out (A8/I16): A = index, or $FF for none.
;-----------------------------------------------------------------------------
.proc NightStageLabel
    .a8
    .i16
    lda nightStage
    cmp #N_SEEK
    bne :+
    lda #0                      ; FIND RIKU
    rts
:   cmp #N_KAIRI
    bne :+
    lda #1                      ; THE SECRET PLACE
    rts
:   lda #$FF
    rts
.endproc

;=============================================================================
; Scene data
;=============================================================================
.segment "RODATA"

; type, tile i, tile j -- terminated by $FF.
;
; The islanders who went home are not here: only Riku, out past the bridge
; where he always sits, and Kairi in the Secret Place.  The chalk is still on
; the wall, which is the point of it having been there for two days.
nightSpawns:
    .byte ACT_SORA,    12, 12        ; where she was standing this afternoon
    .byte ACT_RIKU,    27,  8        ; the small island, past the raised bridge
    .byte ACT_KAIRI,    2,  7        ; the Secret Place, with her back to it
    .byte ACT_PALM,    15,  4
    .byte ACT_PALM,     6,  7
    .byte ACT_PALM,    16, 10
    .byte ACT_PALM,    27,  7        ; the paopu tree
    .byte ACT_PALM,     9, 10        ; the two by the water, picked clean
    .byte ACT_PALM,    13, 10
    .byte ACT_ROCKBIG, 15,  9
    .byte ACT_ROCK,     8, 12
    .byte ACT_FACES,    1,  6
    .byte ACT_DOOR,     2,  6
    .byte ACT_SCRIBBLE, 3,  6
    .byte $FF

; The last piece of it: Sora, and the one tree still standing on it.
fragSpawns:
    .byte ACT_SORA,    15, 12
    .byte ACT_PALM,    18,  6
    .byte $FF

; Where the Shadows come up.  Spread over the island so they arrive from
; wherever Sora is not, and every one is walkable ground by construction --
; tools/check_map.py keeps them that way.
nightSpots:
    .byte  7,  9
    .byte 17,  9
    .byte 10, 10
    .byte  5, 10
    .byte 14, 11
    .byte 19, 11
    .byte 12,  8
    .byte 26,  8
    .byte  9,  6
    .byte 15,  7
nightSpotsEnd:
.assert ((nightSpotsEnd - nightSpots) / 2) = NIGHT_SPOTS, error, "NIGHT_SPOTS"

;--- scripts.  SC_NL breaks a line, SC_PAGE waits and clears, SC_END ends. ---
scriptStorm:
    .byte "THE STORM CAME UP OUT", SC_NL
    .byte "OF NOTHING AT ALL.", SC_PAGE
    .byte "THE RAFT IS GONE.", SC_NL
    .byte SC_NL
    .byte "SO ARE RIKU AND KAIRI.", SC_END

scriptNoUse:
    .byte "THE SWORD GOES", SC_NL
    .byte "STRAIGHT THROUGH IT.", SC_PAGE
    .byte "FIND RIKU FIRST.", SC_END

scriptRiku:
    .byte "THE DOOR HAS OPENED,", SC_NL
    .byte "SORA.", SC_PAGE
    .byte "NOW WE CAN GO.", SC_NL
    .byte "ANYWHERE AT ALL.", SC_PAGE
    .byte "ARE YOU COMING?", SC_END

scriptRikuGone:
    .byte "RIKU!", SC_PAGE
    .byte "THE DARK COMES UP", SC_NL
    .byte "ROUND HIM LIKE WATER.", SC_PAGE
    .byte "HE DOES NOT LOOK", SC_NL
    .byte "AFRAID OF IT.", SC_END

scriptKey:
    .byte "THERE IS SOMETHING", SC_NL
    .byte "IN HIS HAND.", SC_PAGE
    .byte "IT WAS NOT THERE", SC_NL
    .byte "A MOMENT AGO.", SC_PAGE
    .byte "THEY CAN BE CUT", SC_NL
    .byte "NOW.", SC_PAGE
    .byte "KAIRI. THE CAVE.", SC_END

scriptKairi:
    .byte "KAIRI!", SC_PAGE
    .byte "SHE TURNS ROUND VERY", SC_NL
    .byte "SLOWLY, AS IF THE AIR", SC_NL
    .byte "WERE HEAVY.", SC_PAGE
    .byte "...SORA?", SC_END

scriptTorn:
    .byte "THE DOOR COMES OFF", SC_NL
    .byte "THE ROCK.", SC_PAGE
    .byte "SHE GOES STRAIGHT", SC_NL
    .byte "THROUGH HIM AND THERE", SC_NL
    .byte "IS NOTHING TO HOLD.", SC_END

scriptFragment:
    .byte "THE LAST OF THE", SC_NL
    .byte "ISLAND IS UNDER HIS", SC_NL
    .byte "FEET.", SC_PAGE
    .byte "AND HIS OWN SHADOW IS", SC_NL
    .byte "STANDING UP OUT OF", SC_NL
    .byte "THE WATER.", SC_END

scriptCard:
    .byte "THE ISLANDS ARE GONE.", SC_PAGE
    .byte "SO IS EVERYONE HE", SC_NL
    .byte "WOKE UP WITH.", SC_PAGE
    .byte "HE STILL HAS THE KEY.", SC_END
