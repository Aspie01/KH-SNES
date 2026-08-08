;=============================================================================
; town.s -- Traverse Town
;
; Where the game puts you once it has taken everything away.  Three districts,
; one screen each, joined by doors:
;
;   T_ARRIVE  face down on wet stone in the First District
;   T_LOOK    somebody in this town is still up; find them
;   T_SECOND  ...and the door they pointed at
;   T_THIRD   the Second District, once the Heartless in it are finished
;   T_MEET    two people come down out of the sky
;   T_BOSS    ...and so does the Guard Armor
;   T_WON     the three of them are still standing
;   T_OVER    the card
;
; A district change is a tile.  Step onto a door and the screen fades, the
; scene is swapped underneath it and Sora is put down on the far side; the
; doors are a table rather than three special cases, so a fourth district
; would cost three lines of data and no code.  Which stage a door needs is in
; the same row, which is how the town is gated without a lock flag anywhere.
;
; townStage is progress, not location.  The player can walk back through any
; door they have already opened and the town remembers where it had got to.
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
.import AimAtPlayer, DamageSora, SetActorZ
.import LoadScene
.import TileToWorld, TryMoveActor
.import ShadowMath, Rand

.export TownBegin, TownUpdate, TownRestart, TownStageLabel, UpdateArmor

; One row of doorTable.  Seven bytes: which map it is in, where in that map,
; where it leads, where to stand on the far side, and the stage it wants.
DOOR_STRIDE = 7

.segment "CODE"

;=============================================================================
; Arriving, and arriving again
;=============================================================================

;-----------------------------------------------------------------------------
; TownBegin -- the First District, out of the dark the islands went into.
; Called with the screen blacked out by the night's closing fade.  A8/I16.
;-----------------------------------------------------------------------------
.proc TownBegin
    .a8
    .i16
    jsr ShadowMath              ; the night's subtractive fade is done with
    stz townStage               ; T_ARRIVE
    stz townTimer
    stz townKills
    stz doorTimer
    stz bossHP
    stz mosaicAmt
    stz shakeX

    ; Seed the LFSR here rather than trusting whatever ran before: a Galois
    ; shift register whose state is zero stays zero, and every Heartless in
    ; the Second District would then come up on the same paving stone.
    rep #$20
    .a16
    lda #$1D57
    sta rngState
    sep #$20
    .a8

    lda #SCENE_TOWN1
    jsr LoadDistrict
    lda #$0F
    sta screenBright
    jsr HudUpdate

    rep #$20
    .a16
    lda #.loword(scriptWake)
    jmp Say
.endproc

;-----------------------------------------------------------------------------
; TownRestart -- put the current district back on its feet after a death.
; The scene is already loaded and the actors already cleared.  A8/I16.
;-----------------------------------------------------------------------------
.proc TownRestart
    .a8
    .i16
    stz doorTimer
    stz mosaicAmt
    stz shakeX
    stz bossHP
    jsr ShadowMath
    jsr SpawnDistrict

    ; The Second District's wave starts over: half a wave of survivors left
    ; standing while the counter says the district is nearly clear would be a
    ; retry that is easier than the attempt.
    stz townKills
    lda #TOWN_GAP
    sta spawnTimer

    ; The Guard Armor comes down again, from the top.
    stz townTimer
    lda townStage
    cmp #T_BOSS
    bne :+
    lda #1
    sta townTimer
:   jsr HudUpdate
    rts
.endproc

;-----------------------------------------------------------------------------
; LoadDistrict -- swap the ground and the cast under a fade.
; In (A8/I16): A = scene id.  Leaves the screen in forced blank; the caller
; owns the brightness, because a door and an arrival want different things.
;-----------------------------------------------------------------------------
.proc LoadDistrict
    .a8
    .i16
    pha
    lda #$8F
    sta screenBright
    sta INIDISP                 ; forced blank: LoadScene writes VRAM
    pla
    jsr LoadScene
    jsr ClearActors
    jsr SpawnDistrict

    ; Nowhere, so whichever tile he lands on counts as a step onto it.
    lda #$FF
    sta lastTileI
    sta lastTileJ
    lda #TOWN_GAP
    sta spawnTimer
    rts
.endproc

;-----------------------------------------------------------------------------
; SpawnDistrict -- lay out whichever district sceneId names.  A8/I16.
;-----------------------------------------------------------------------------
.proc SpawnDistrict
    .a8
    .i16
    lda sceneId
    cmp #SCENE_TOWN2
    beq @two
    cmp #SCENE_TOWN3
    beq @three
    rep #$20
    .a16
    lda #.loword(town1Spawns)
    bra @go
@two:
    rep #$20
    .a16
    lda #.loword(town2Spawns)
    bra @go
@three:
    rep #$20
    .a16
    lda #.loword(town3Spawns)
@go:
    sta tmp8
    sep #$20
    .a8
    jsr SpawnTable

    ; The two who fall on him stay standing there afterwards, so a retry in
    ; the Third District has to put them back on their feet as well.
    lda sceneId
    cmp #SCENE_TOWN3
    bne @out
    lda townStage
    cmp #T_BOSS
    bcc @out
    rep #$20
    .a16
    lda #.loword(pairSpawns)
    sta tmp8
    sep #$20
    .a8
    jsr SpawnTable
@out:
    rts
.endproc

;=============================================================================
; The frame
;=============================================================================

;-----------------------------------------------------------------------------
; TownUpdate -- A8/I16.
;-----------------------------------------------------------------------------
.proc TownUpdate
    .a8
    .i16
    ; A door owns the screen for as long as it is running.
    lda doorTimer
    beq :+
    jmp DoorStep
:
    jsr TextBusy
    bcc @free
    rts                         ; somebody is talking

@free:
    lda townStage
    cmp #T_ARRIVE
    bne :+
    jmp Woke
:   cmp #T_MEET
    bne :+
    jmp Meet
:   cmp #T_BOSS
    bne :+
    jmp WatchArmor
:   cmp #T_WON
    bne :+
    jmp AfterArmor
:   cmp #T_OVER
    bne :+
    rts                         ; the card stays up
:
    ; T_LOOK, T_SECOND, T_THIRD: the town is walkable and the doors are live.
    lda sceneId
    cmp #SCENE_TOWN2
    bne :+
    jsr TownShadows
:
    jsr TalkTown
    jsr TextBusy
    bcs @out                    ; a conversation just started; the door waits
    jsr CheckDoors
@out:
    rts
.endproc

;-----------------------------------------------------------------------------
; Woke -- the opening line has been dismissed.  A8/I16.
;-----------------------------------------------------------------------------
.proc Woke
    .a8
    .i16
    lda #T_LOOK
    sta townStage
    jsr HudUpdate
    rts
.endproc

;=============================================================================
; Doors
;=============================================================================

;-----------------------------------------------------------------------------
; CheckDoors -- has the player just stepped onto a way out?  A8/I16.
;-----------------------------------------------------------------------------
.proc CheckDoors
    .a8
    .i16
    jsr PlayerPos               ; tmp0 / tmp1, Q12.4
    rep #$20
    .a16
    ; Q12.4 to pixels is four shifts and pixels to tiles is four more, so a
    ; tile coordinate is the top byte of the fixed-point one.
    lda tmp0
    xba
    and #$00FF
    sta tmp2
    lda tmp1
    xba
    and #$00FF
    sta tmp3
    sep #$20
    .a8

    ; Only the frame he arrives on counts.  A bolted door has a line, and it
    ; would be said on every frame he stood in front of it otherwise.
    lda tmp2
    cmp lastTileI
    bne @moved
    lda tmp3
    cmp lastTileJ
    beq @out
@moved:
    lda tmp2
    sta lastTileI
    lda tmp3
    sta lastTileJ

    ldx #0
@scan:
    lda doorTable,x
    cmp #$FF
    beq @out
    cmp sceneId
    bne @next
    lda doorTable+1,x
    cmp tmp2
    bne @next
    lda doorTable+2,x
    cmp tmp3
    beq @found
@next:
    rep #$20
    .a16
    txa
    clc
    adc #DOOR_STRIDE
    tax
    sep #$20
    .a8
    bra @scan

@found:
    lda doorTable+6,x           ; the stage this one wants
    cmp townStage
    beq @open
    bcs @shut
@open:
    txa
    sta doorTo
    lda #(DOOR_FADE * 2)
    sta doorTimer
    rts

@shut:
    cmp #T_SECOND
    bne :+
    rep #$20
    .a16
    lda #.loword(scriptShut1)
    jmp Say
:   rep #$20
    .a16
    lda #.loword(scriptShut2)
    jmp Say

@out:
    rts
.endproc

;-----------------------------------------------------------------------------
; DoorStep -- one frame of a district change: out, swap, back in.  A8/I16.
;-----------------------------------------------------------------------------
.proc DoorStep
    .a8
    .i16
    lda doorTimer
    dec a
    sta doorTimer
    beq @up
    cmp #DOOR_FADE
    bcc @in
    beq @swap

    ; still going out
    sec
    sbc #DOOR_FADE
    lsr a
    sta screenBright
    rts

@swap:
    jsr DoorRow                 ; X = the row being walked through
    lda doorTable+3,x           ; where it leads
    jsr LoadDistrict
    jsr DoorRow                 ; LoadDistrict has had the registers
    lda doorTable+4,x
    sta tmp4
    lda doorTable+5,x
    sta tmp5
    jsr PlaceSora
    jsr ArriveAt
    stz screenBright
    rts

@in:
    lda #DOOR_FADE
    sec
    sbc doorTimer
    lsr a
    sta screenBright
    rts

@up:
    lda #$0F
    sta screenBright
    rts
.endproc

;-----------------------------------------------------------------------------
; DoorRow -- X = the byte offset of the door being walked through.  A8/I16.
;-----------------------------------------------------------------------------
.proc DoorRow
    .a8
    .i16
    lda doorTo
    rep #$20
    .a16
    and #$00FF
    tax
    sep #$20
    .a8
    rts
.endproc

;-----------------------------------------------------------------------------
; ArriveAt -- whatever the district does about being walked into.  A8/I16.
;-----------------------------------------------------------------------------
.proc ArriveAt
    .a8
    .i16
    lda sceneId
    cmp #SCENE_TOWN3
    bne @out
    lda townStage
    cmp #T_THIRD
    bne @out                    ; been here before
    lda #T_MEET
    sta townStage
    lda #FALL_WAIT
    sta townTimer
    jsr HudUpdate
@out:
    rts
.endproc

;-----------------------------------------------------------------------------
; PlaceSora -- stand the player in the middle of tile tmp4 / tmp5.  A8/I16.
;-----------------------------------------------------------------------------
.proc PlaceSora
    .a8
    .i16
    jsr SpotWorld               ; tmp8 / tmp9
    lda playerIdx
    rep #$20
    .a16
    and #$00FF
    asl a
    tax
    lda tmp8
    sta actX,x
    lda tmp9
    sta actY,x
    stz actVX,x
    stz actVY,x
    sep #$20
    .a8

    lda playerIdx
    rep #$20
    .a16
    and #$00FF
    tax
    sep #$20
    .a8
    lda #DIR_S
    sta actDir,x
    stz actState,x
    stz actTimer,x
    jsr SetActorZ
    rts
.endproc

;=============================================================================
; The people who live here
;=============================================================================

;-----------------------------------------------------------------------------
; TalkTown -- press A next to somebody.  A8/I16.
;
; Anything carrying AF_TALK answers, so the set of people who have something
; to say is decided by the type table rather than by a list kept here.
;-----------------------------------------------------------------------------
.proc TalkTown
    .a8
    .i16
    rep #$20
    .a16
    lda padPressed
    and #PAD_A
    sep #$20
    .a8
    beq @out

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
    beq @next
    lda actFlags,x
    and #AF_TALK
    beq @next
    jsr NearPlayer
    bcs @hit
@next:
    inx
    cpx #MAX_ACTORS
    bcc @scan
@out:
    rts

@hit:
    lda actType,x
    cmp #ACT_TOWNMAN
    beq @man
    cmp #ACT_TOWNWOMAN
    beq @woman
    cmp #ACT_DONALD
    beq @donald
    cmp #ACT_GOOFY
    beq @goofy

    ; Cid is the one who matters: he is the reason the door opens.  He is the
    ; fall-through rather than a branch target because the bookkeeping has to
    ; run in the same eight-bit stretch as the dispatch -- every other arm
    ; here opens with a rep, so it does not care what width it arrives at.
    lda townStage
    cmp #T_LOOK
    bne @cidAgain
    lda #T_SECOND
    sta townStage
    jsr HudUpdate
    rep #$20
    .a16
    lda #.loword(scriptCid)
    jmp Say
@cidAgain:
    rep #$20
    .a16
    lda #.loword(scriptCid2)
    jmp Say
@man:
    rep #$20
    .a16
    lda #.loword(scriptMan)
    jmp Say
@woman:
    rep #$20
    .a16
    lda #.loword(scriptWoman)
    jmp Say
@donald:
    rep #$20
    .a16
    lda #.loword(scriptDonald)
    jmp Say
@goofy:
    rep #$20
    .a16
    lda #.loword(scriptGoofy)
    jmp Say
.endproc

;=============================================================================
; The Second District
;=============================================================================

;-----------------------------------------------------------------------------
; TownShadows -- keep the square populated until the wave is spent, then let
; the player through.  A8/I16.
;-----------------------------------------------------------------------------
.proc TownShadows
    .a8
    .i16
    lda townKills
    cmp #TOWN_WAVE
    bcs @spent

    lda spawnTimer
    beq @due
    dec spawnTimer
    rts

@due:
    lda #TOWN_GAP
    sta spawnTimer
    lda #ACT_SHADOW
    jsr CountType
    cmp #TOWN_SHADOWS
    bcs @out

    ; Somewhere in the square, but not on top of the player.
    jsr Rand
    rep #$20
    .a16
    and #$00FF
@wrap:
    cmp #TOWN_SPOTS
    bcc :+
    sec
    sbc #TOWN_SPOTS
    bra @wrap
:   asl a                       ; two bytes a spot
    tax
    sep #$20
    .a8
    lda townSpots,x
    sta tmp4
    lda townSpots+1,x
    sta tmp5
    jsr SpotWorld               ; tmp8 / tmp9; PlayerPos only touches tmp0/tmp1
    jsr PlayerPos
    rep #$20
    .a16
    lda tmp8
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
    lda tmp8
    sta tmp0
    lda tmp9
    sta tmp1
    sep #$20
    .a8
    lda #ACT_SHADOW
    jsr SpawnActor
    bcc @out
    inc townKills
@out:
    rts

@tooClose:
    sep #$20
    .a8
    lda #12                     ; try again shortly
    sta spawnTimer
    rts

    ; The whole wave has been out and none of it is left: the way on opens.
    ;
    ; This is the tail of the routine rather than the head of it because the
    ; sixteen-bit stretch below ends in a jmp, and a `.a16` left in force over
    ; the eight-bit code that used to follow it is how this scene shipped a BRK
    ; in the middle of the Second District.
@spent:
    lda #ACT_SHADOW
    jsr CountType
    bne @quiet
    lda townStage
    cmp #T_SECOND
    beq @clear
@quiet:
    rts

@clear:
    lda #T_THIRD
    sta townStage
    jsr HudUpdate
    rep #$20
    .a16
    lda #.loword(scriptClear)
    jmp Say
.endproc

;=============================================================================
; The Third District
;=============================================================================

;-----------------------------------------------------------------------------
; Meet -- the wait, and then two people landing in the square.  A8/I16.
;-----------------------------------------------------------------------------
.proc Meet
    .a8
    .i16
    lda #ACT_DONALD
    jsr CountType
    bne @falling

    lda townTimer
    beq @drop
    dec townTimer
    rts

@drop:
    jsr DropPair
    lda #FALL_DROP
    sta townTimer
    rts

@falling:
    lda townTimer
    beq @land
    dec townTimer
    jsr LowerPair
    rts

@land:
    stz shakeX
    lda #6
    sta hitStopTimer
    lda #T_BOSS
    sta townStage
    lda #1
    sta townTimer               ; ...and the armour has not come down yet
    jsr HudUpdate
    rep #$20
    .a16
    lda #.loword(scriptMeet)
    jmp Say
.endproc

;-----------------------------------------------------------------------------
; DropPair -- put them in the square, well above it.  A8/I16.
;-----------------------------------------------------------------------------
.proc DropPair
    .a8
    .i16
    rep #$20
    .a16
    lda #.loword(pairSpawns)
    sta tmp8
    sep #$20
    .a8
    jsr SpawnTable

    ldx #0
@scan:
    lda actType,x
    cmp #ACT_DONALD
    beq @up
    cmp #ACT_GOOFY
    bne @next
@up:
    lda #FALL_Z
    sta actZ,x
@next:
    inx
    cpx #MAX_ACTORS
    bcc @scan
    rts
.endproc

;-----------------------------------------------------------------------------
; LowerPair -- bring them down with the timer.  FALL_DROP is twice FALL_Z, so
; the descent is one shift.  A8/I16.
;-----------------------------------------------------------------------------
.proc LowerPair
    .a8
    .i16
    lda townTimer
    lsr a
    sta tmp4
    ldx #0
@scan:
    lda actType,x
    cmp #ACT_DONALD
    beq @set
    cmp #ACT_GOOFY
    bne @next
@set:
    lda tmp4
    sta actZ,x
@next:
    inx
    cpx #MAX_ACTORS
    bcc @scan

    ; the square feels it coming
    lda frameCount
    and #$02
    beq :+
    lda #$FE                    ; -2
    bra :++
:   lda #$02
:   sta shakeX
    rts
.endproc

;=============================================================================
; The Guard Armor
;=============================================================================

;-----------------------------------------------------------------------------
; WatchArmor -- raise it once, then wait for it to stop.  A8/I16.
;-----------------------------------------------------------------------------
.proc WatchArmor
    .a8
    .i16
    lda townTimer
    beq @fight
    stz townTimer
    jmp RaiseArmor

@fight:
    lda #ACT_ARMOR
    jsr CountType
    bne @out
    stz bossHP
    stz shakeX

    ; Its hands go with it.
    ldx #0
@sweep:
    lda actType,x
    cmp #ACT_GAUNTLET
    bne :+
    stz actType,x
:   inx
    cpx #MAX_ACTORS
    bcc @sweep

    lda #T_WON
    sta townStage
    jsr HudUpdate
    rep #$20
    .a16
    lda #.loword(scriptWon)
    jmp Say
@out:
    rts
.endproc

;-----------------------------------------------------------------------------
; AfterArmor -- the closing card.  A8/I16.
;-----------------------------------------------------------------------------
.proc AfterArmor
    .a8
    .i16
    lda #T_OVER
    sta townStage
    jsr HudUpdate
    rep #$20
    .a16
    lda #.loword(scriptCard)
    jmp Say
.endproc

;-----------------------------------------------------------------------------
; RaiseArmor -- it comes down in the middle of the square, with its hands.
; A8/I16.
;-----------------------------------------------------------------------------
.proc RaiseArmor
    .a8
    .i16
    ; Stand the player back in the middle of the square first.  Sixty-four
    ; pixels of armour are drawn above its feet, so whoever is within two tiles
    ; of where it lands is inside it -- and the way in is exactly two tiles from
    ; where it lands.  Putting him here is the staging: it comes down between
    ; him and the door he came through.
    lda #16
    sta tmp4
    lda #10
    sta tmp5
    jsr PlaceSora

    lda #16                     ; tile (16,7): far enough up the square for a
    sta tmp4                    ; 64 px sprite to keep its head out of the HUD
    lda #7
    sta tmp5
    jsr SpotWorld

    rep #$20
    .a16
    lda tmp8
    sta tmp0
    lda tmp9
    sta tmp1
    sep #$20
    .a8
    lda #ACT_ARMOR
    jsr SpawnActor
    bcc @out
    lda #GA_DROP_Z
    sta actZ,x                  ; actState is GAS_DROP by virtue of being zero
    lda #GA_DROP
    sta actTimer,x
    lda #GA_MAX_HP
    sta bossHP
    jsr SpawnHands
    jsr HudUpdate
@out:
    rts
.endproc

;-----------------------------------------------------------------------------
; SpawnHands -- the two gauntlets, in the body's place until it places them.
; In: tmp8 / tmp9 = where the armour is.  A8/I16.
;-----------------------------------------------------------------------------
.proc SpawnHands
    .a8
    .i16
    stz tmp7                    ; the counter lives here, not in Y: SpawnActor
@one:                           ; indexes the type tables with Y and keeps it
    rep #$20
    .a16
    lda tmp8
    sta tmp0
    lda tmp9
    sta tmp1
    sep #$20
    .a8
    lda #ACT_GAUNTLET
    jsr SpawnActor
    bcc @out
    lda tmp7
    sta actAnim,x               ; 0 is the left hand, 1 the right
    inc tmp7
    lda tmp7
    cmp #2
    bcc @one
@out:
    rts
.endproc

;-----------------------------------------------------------------------------
; UpdateArmor -- In (A8/I16): X = actor index.  Called from UpdateWorld.
;
; It walks at you and then puts a fist where you are standing.  Everything it
; does is dodged by moving, and the wind-up is long enough that moving is
; always an answer -- what makes it harder than Darkside is that the thing
; follows you between attacks instead of waiting.
;-----------------------------------------------------------------------------
.proc UpdateArmor
    .a8
    .i16
    stx curActor

    lda actHitT,x
    beq :+
    dec a
    sta actHitT,x
:
    lda actState,x
    cmp #GAS_WALK
    beq @walk
    cmp #GAS_WIND
    beq @wind
    cmp #GAS_SLAM
    beq @slam
    cmp #GAS_REST
    beq @rest

    ;--- still coming down ---
    lda actTimer,x
    beq @landed
    dec a
    sta actTimer,x
    lsr a
    lsr a
    sta actZ,x                  ; GA_DROP_Z is GA_DROP shifted twice
    lda frameCount
    and #$02
    beq :+
    lda #$FD                    ; -3
    bra :++
:   lda #$03
:   sta shakeX
    rts
@landed:
    stz shakeX
    stz actZ,x
    lda #8
    sta hitStopTimer
    bra @toWalk

    ;--- closing on the player ---
@walk:
    lda actTimer,x
    beq @toWind
    dec a
    sta actTimer,x
    jsr StepArmor
    jmp PlaceHands
@toWind:
    jsr AimAtPlayer             ; actVX / actVY mark where the fist will land
    ldx curActor
    lda #GAS_WIND
    sta actState,x
    lda #GA_SLAM_WIND
    sta actTimer,x
    jmp PlaceHands

    ;--- a fist up over the mark ---
@wind:
    lda actTimer,x
    beq @toSlam
    dec a
    sta actTimer,x
    jmp PlaceHands
@toSlam:
    lda #GAS_SLAM
    sta actState,x
    lda #GA_SLAM_HOLD
    sta actTimer,x
    jsr PlaceHands
    jmp ArmorSlam

    ;--- and down ---
@slam:
    lda actTimer,x
    beq @toRest
    dec a
    sta actTimer,x
    jmp PlaceHands
@toRest:
    lda #GAS_REST
    sta actState,x
    lda #GA_REST
    sta actTimer,x
    jmp PlaceHands

@rest:
    lda actTimer,x
    beq @toWalk
    dec a
    sta actTimer,x
    jmp PlaceHands
@toWalk:
    ldx curActor
    lda #GAS_WALK
    sta actState,x
    lda #GA_WALK_LEN
    sta actTimer,x
    rep #$20
    .a16
    txa
    asl a
    tax
    stz actVX,x                 ; the mark is spent; these are velocity again
    stz actVY,x
    sep #$20
    .a8
    jmp PlaceHands
.endproc

;-----------------------------------------------------------------------------
; StepArmor -- one frame of it tracking the player.  A8/I16, X = its index.
;
; Sideways only, and that is a decision rather than an omission.  A sprite this
; tall is anchored by its feet, so sixty-four pixels of armour are drawn
; *above* wherever it is standing: let it walk north to meet somebody and its
; whole body goes over the top of them, and the fight stops reading as a fight.
; Holding its station and following the player left and right keeps it a wall
; at the head of the square, which is what it should look like anyway -- the
; vertical work belongs to the hands, and they land wherever you are.
;-----------------------------------------------------------------------------
.proc StepArmor
    .a8
    .i16
    jsr PlayerPos               ; tmp0 / tmp1
    lda curActor
    rep #$20
    .a16
    and #$00FF
    asl a
    tax

    lda tmp0
    sec
    sbc actX,x
    bpl :+
    eor #$FFFF
    inc a
    sta tmp4                    ; distance
    lda #.loword(-GA_WALK)
    bra :++
:   sta tmp4
    lda #GA_WALK
:   sta tmp5
    lda tmp4
    cmp #GA_STOP
    bcs :+
    stz tmp5                    ; near enough; do not jitter on the spot
:   lda tmp5
    sta actVX,x
    stz actVY,x

    jsr TryMoveActor            ; A16/I16, and it preserves X
    sep #$20
    .a8
    rts
.endproc

;-----------------------------------------------------------------------------
; PlaceHands -- put the two gauntlets where the body's state says they are.
;
; They are ordinary actors, so the Y-sort draws them in front of the torso and
; a shadow-free 32x32 costs nothing else -- but nothing drives them.  The body
; does, from here, every frame.  A8/I16.
;-----------------------------------------------------------------------------
.proc PlaceHands
    .a8
    .i16
    lda curActor
    rep #$20
    .a16
    and #$00FF
    asl a
    tax
    lda actX,x
    sta tmp0
    lda actY,x
    sta tmp1
    lda actVX,x
    sta tmp2                    ; where a slam is aimed, while one is coming
    lda actVY,x
    sta tmp3
    sep #$20
    .a8

    ldy curActor
    lda actState,y
    sta tmp4
    lda actZ,y
    sta tmp5

    ldx #0
@scan:
    lda actType,x
    cmp #ACT_GAUNTLET
    bne @next
    jsr OneHand
@next:
    inx
    cpx #MAX_ACTORS
    bcc @scan
    rts
.endproc

;-----------------------------------------------------------------------------
; OneHand -- In (A8/I16): X = gauntlet index; tmp0-tmp5 from PlaceHands.
;-----------------------------------------------------------------------------
.proc OneHand
    .a8
    .i16
    lda tmp4
    cmp #GAS_WIND
    beq @striking
    cmp #GAS_SLAM
    beq @striking

    ;--- station-keeping: one either side of the torso ---
@idle:
    lda actAnim,x
    beq @left
    rep #$20
    .a16
    lda tmp0
    clc
    adc #GA_HAND_R
    bra @sides
@left:
    rep #$20
    .a16
    lda tmp0
    sec
    sbc #GA_HAND_R
@sides:
    sta tmp7
    lda tmp1
    sec
    sbc #GA_HAND_UP
    sta tmp8
    sep #$20
    .a8
    ldy #0                      ; the open hand
    lda tmp5
    sta tmp9
    bra @write

    ;--- the right hand is the one that does the work ---
@striking:
    lda actAnim,x
    beq @idle
    rep #$20
    .a16
    lda tmp2
    sta tmp7
    lda tmp3
    sta tmp8
    sep #$20
    .a8
    ldy #4                      ; the closed fist
    lda tmp4
    cmp #GAS_SLAM
    beq @onGround
    lda #GA_HAND_HIGH           ; wound up, well clear of the ground
    sta tmp9
    bra @write
@onGround:
    stz tmp9

@write:
    phx
    txa
    rep #$20
    .a16
    and #$00FF
    asl a
    tax
    lda tmp7
    sta actX,x
    lda tmp8
    sta actY,x
    sep #$20
    .a8
    plx
    lda tmp9
    sta actZ,x
    tya
    clc
    adc #TILE_GAUNTLET
    sta actTile,x
    rts
.endproc

;-----------------------------------------------------------------------------
; ArmorSlam -- the fist reaches the mark.  A8/I16.
;-----------------------------------------------------------------------------
.proc ArmorSlam
    .a8
    .i16
    lda #6
    sta hitStopTimer

    lda curActor
    rep #$20
    .a16
    and #$00FF
    asl a
    tax
    lda actVX,x
    sta tmp0
    lda actVY,x
    sta tmp1

    ; Anyone still standing there takes it.
    sep #$20
    .a8
    lda playerIdx
    rep #$20
    .a16
    and #$00FF
    asl a
    tax
    lda actX,x
    sec
    sbc tmp0
    bpl :+
    eor #$FFFF
    inc a
:   cmp #GA_HAND_X
    bcs @clear
    lda actY,x
    sec
    sbc tmp1
    bpl :+
    eor #$FFFF
    inc a
:   cmp #GA_HAND_Y
    bcs @clear
    sep #$20
    .a8
    ldx curActor
    jsr DamageSora
    rts
@clear:
    sep #$20
    .a8
    rts
.endproc

;=============================================================================
; Helpers
;=============================================================================

;-----------------------------------------------------------------------------
; SpotWorld -- the middle of tile tmp4 / tmp5 in world Q12.4, into tmp8/tmp9.
;
; The high slots, because SpawnActor spends tmp0-tmp6 and the callers here
; need the position to survive it.  A8/I16.
;-----------------------------------------------------------------------------
.proc SpotWorld
    .a8
    .i16
    rep #$20
    .a16
    lda tmp4
    and #$00FF
    sta tmp0
    lda tmp5
    and #$00FF
    sta tmp1
    jsr TileToWorld             ; whole pixels
    lda tmp0
    asl a
    asl a
    asl a
    asl a
    sta tmp8
    lda tmp1
    asl a
    asl a
    asl a
    asl a
    sta tmp9
    sep #$20
    .a8
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
    lda #^scriptWake            ; every line here shares one bank
    sta txtPtr+2
    lda #TM_MESSAGE
    jsr TextOpen
    rts
.endproc

;-----------------------------------------------------------------------------
; TownStageLabel -- which objective line the HUD should show, as an index into
; the table hud.s holds.  Out (A8/I16): A = index, or $FF for none.
;-----------------------------------------------------------------------------
.proc TownStageLabel
    .a8
    .i16
    lda townStage
    cmp #T_LOOK
    bne :+
    lda #0                      ; FIND SOMEBODY AWAKE
    rts
:   cmp #T_SECOND
    bne :+
    lda #1                      ; THE SECOND DISTRICT
    rts
:   cmp #T_THIRD
    bne :+
    lda #2                      ; THE THIRD DISTRICT
    rts
:   lda #$FF
    rts
.endproc

;=============================================================================
; Scene data
;=============================================================================
.segment "RODATA"

; Where the districts join.  Seven bytes a row:
;
;   which map, the door's tile, where it leads, the tile to stand on over
;   there, and the stage the town has to have reached for it to open.
;
; Every door is in row DOOR_ROW -- the bottom row of a building block, which
; is the only row of one that shows a face -- and every landing is the tile
; directly south of the door on the far side, so a player who walks straight
; through comes out facing the square.
doorTable:
    .byte SCENE_TOWN1, 25, DOOR_ROW, SCENE_TOWN2, 26, 5, T_SECOND
    .byte SCENE_TOWN2, 26, DOOR_ROW, SCENE_TOWN1, 25, 5, T_ARRIVE
    .byte SCENE_TOWN2,  5, DOOR_ROW, SCENE_TOWN3, 16, 5, T_THIRD
    .byte SCENE_TOWN3, 16, DOOR_ROW, SCENE_TOWN2,  5, 5, T_ARRIVE
    .byte $FF
doorTableEnd:
.assert ((doorTableEnd - doorTable - 1) .mod DOOR_STRIDE) = 0, error, "doorTable"

; type, tile i, tile j -- terminated by $FF.
;
; The lamp posts are sprites standing on the map's own blocked lamp tiles, so
; the light pool is painted into the ground and the post that casts it can be
; walked behind.
town1Spawns:
    .byte ACT_SORA,      14, 12       ; face down in the middle of the square
    .byte ACT_CID,       23,  6       ; the one shop front with a light in it
    .byte ACT_TOWNMAN,    8,  9
    .byte ACT_TOWNWOMAN, 18, 13
    .byte ACT_LAMP,       3,  7
    .byte ACT_LAMP,      27,  7
    .byte $FF
town1SpawnsEnd:

; Nobody is out in the Second District.  That is the point of it.
town2Spawns:
    .byte ACT_SORA,      26,  5
    .byte ACT_LAMP,       3,  7
    .byte ACT_LAMP,      28,  7
    .byte $FF
town2SpawnsEnd:

town3Spawns:
    .byte ACT_SORA,      16,  5
    .byte ACT_LAMP,       7,  7
    .byte ACT_LAMP,      24,  7
    .byte $FF
town3SpawnsEnd:

; The two who come down out of the sky, and stay standing there afterwards.
pairSpawns:
    .byte ACT_DONALD,    14,  9
    .byte ACT_GOOFY,     18,  9
    .byte $FF
pairSpawnsEnd:

TOWN1_CAST = (town1SpawnsEnd - town1Spawns - 1) / 3
TOWN2_CAST = (town2SpawnsEnd - town2Spawns - 1) / 3
TOWN3_CAST = (town3SpawnsEnd - town3Spawns - 1) / 3
PAIR_CAST  = (pairSpawnsEnd - pairSpawns - 1) / 3
.assert (TOWN1_CAST + TRANSIENT_ACTORS) <= MAX_ACTORS, error, "the First District does not fit in MAX_ACTORS"
.assert (TOWN2_CAST + TOWN_SHADOWS + TRANSIENT_ACTORS) <= MAX_ACTORS, error, "the Second District does not fit in MAX_ACTORS"
; the armour and its two hands, on top of everyone standing there
.assert (TOWN3_CAST + PAIR_CAST + 3 + TRANSIENT_ACTORS) <= MAX_ACTORS, error, "the Third District does not fit in MAX_ACTORS"

; Where the Heartless come up in the Second District.  Clear of the fountain,
; the crates and the lamps by construction -- tools/check_map.py keeps them so.
townSpots:
    .byte  6,  6
    .byte 20,  6
    .byte  9,  9
    .byte 22,  9
    .byte  6, 12
    .byte 24, 12
    .byte 16, 13
    .byte 11, 11
townSpotsEnd:
.assert ((townSpotsEnd - townSpots) / 2) = TOWN_SPOTS, error, "TOWN_SPOTS"

;--- scripts.  SC_NL breaks a line, SC_PAGE waits and clears, SC_END ends. ---
scriptWake:
    .byte "HE COMES ROUND FACE", SC_NL
    .byte "DOWN ON WET STONE.", SC_PAGE
    .byte "LAMPS. SHUTTERS. A SKY", SC_NL
    .byte "WITH THE WRONG STARS", SC_NL
    .byte "IN IT.", SC_PAGE
    .byte "THIS IS NOT THE ISLAND.", SC_END

scriptCid:
    .byte "YOU'RE NEW.", SC_PAGE
    .byte "EVERYBODY HERE IS, THE", SC_NL
    .byte "FIRST NIGHT.", SC_PAGE
    .byte "YOUR WORLD WENT, DIDN'T", SC_NL
    .byte "IT. THIS IS WHERE THE", SC_NL
    .byte "PIECES WASH UP.", SC_PAGE
    .byte "THE SECOND DISTRICT IS", SC_NL
    .byte "THROUGH THE DOOR AT THE", SC_NL
    .byte "END. MIND YOURSELF.", SC_END

scriptCid2:
    .byte "THE DOOR AT THE END OF", SC_NL
    .byte "THE ROW. IT'S OPEN NOW.", SC_END

scriptMan:
    .byte "DON'T GO OUT PAST THE", SC_NL
    .byte "FIRST DISTRICT.", SC_PAGE
    .byte "THINGS COME UP OUT OF", SC_NL
    .byte "THE GROUND OUT THERE.", SC_END

scriptWoman:
    .byte "IS THAT A KEY?", SC_PAGE
    .byte "THEN YOU'RE THE ONE", SC_NL
    .byte "THEY'RE LOOKING FOR.", SC_END

scriptShut1:
    .byte "THE DOOR WILL NOT", SC_NL
    .byte "SHIFT.", SC_PAGE
    .byte "SOMEBODY IN THIS TOWN", SC_NL
    .byte "IS STILL AWAKE. FIND", SC_NL
    .byte "THEM FIRST.", SC_END

scriptShut2:
    .byte "IT IS HELD SHUT FROM", SC_NL
    .byte "THE FAR SIDE.", SC_PAGE
    .byte "NOTHING IN THIS TOWN", SC_NL
    .byte "OPENS WHILE THE SQUARE", SC_NL
    .byte "BEHIND YOU IS MOVING.", SC_END

scriptClear:
    .byte "THE LAST OF THEM GOES", SC_NL
    .byte "OUT LIKE A LAMP.", SC_PAGE
    .byte "SOMETHING UNBOLTS", SC_NL
    .byte "ITSELF ON THE FAR SIDE", SC_NL
    .byte "OF THE SQUARE.", SC_END

scriptMeet:
    .byte "TWO OF THEM COME DOWN", SC_NL
    .byte "OUT OF THE SKY.", SC_PAGE
    .byte "THE SHORT ONE IS", SC_NL
    .byte "ALREADY SHOUTING.", SC_PAGE
    .byte "THE KEY! HE'S GOT THE", SC_NL
    .byte "KEY!", SC_PAGE
    .byte "GAWRSH. SO HE HAS.", SC_END

scriptDonald:
    .byte "WE'VE BEEN LOOKING FOR", SC_NL
    .byte "YOU. OR FOR THAT.", SC_PAGE
    .byte "SAME THING, THE KING", SC_NL
    .byte "SAID.", SC_END

scriptGoofy:
    .byte "AW, DON'T MIND HIM.", SC_PAGE
    .byte "HE SHOUTS AT EVERYTHING", SC_NL
    .byte "THAT ISN'T A DUCK.", SC_END

scriptWon:
    .byte "THE ARMOUR COMES APART", SC_NL
    .byte "IN THE AIR.", SC_PAGE
    .byte "A HEART GOES UP OUT OF", SC_NL
    .byte "IT AND KEEPS GOING.", SC_PAGE
    .byte "THE THREE OF THEM ARE", SC_NL
    .byte "STILL STANDING.", SC_END

scriptCard:
    .byte "SO. YOU'RE COMING WITH", SC_NL
    .byte "US, THEN.", SC_PAGE
    .byte "THERE IS A SHIP. THERE", SC_NL
    .byte "ARE OTHER WORLDS.", SC_PAGE
    .byte "AND SOMEWHERE OUT", SC_NL
    .byte "THERE ARE TWO PEOPLE", SC_NL
    .byte "HE HAS TO FIND.", SC_END
