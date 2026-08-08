;=============================================================================
; island.s -- Destiny Islands
;
; Two days of Kairi's errands.  Day one is the raft itself -- two logs, cloth
; and rope.  Day two is what to put on it: fish, mushrooms, coconuts, a
; seagull egg and a bottle of water.
;
; Three kinds of interaction, and which one a thing wants is decided by its
; actor type alone:
;
;   walk into it   the materials, the mushrooms, a fallen coconut, the egg,
;                  the bottle -- everything in ACT_LOG..ACT_BOTTLE
;   swing at it    fish out in the shallows, and palms still carrying coconuts
;   press A        the islanders, so a stroll along the beach does not trip
;                  over five conversations
;
; itemCount is indexed by (actor type - ACT_LOG), so a pickup tallies itself.
;=============================================================================
.p816
.include "snes.inc"
.include "game.inc"
.include "ram.inc"
.include "macros.inc"
.include "text.inc"

.import TextOpen, TextBusy
.import HudUpdate
.import SpawnActor, TileToWorld, ClearActors, InitWorld
.import SpawnTable, PlayerPos, NearPlayer
.import NightBegin
.import SetActorZ

.export IslandInit, IslandUpdate, UpdateRiku

.segment "CODE"

;-----------------------------------------------------------------------------
; IslandInit -- day one, with an empty tally.  A8/I16.
;-----------------------------------------------------------------------------
.proc IslandInit
    .a8
    .i16
    stz questState
    stz pendTalk
    lda #1
    sta questDay
    ldx #0
:   stz itemCount,x
    inx
    cpx #8
    bcc :-

    rep #$20
    .a16
    lda #.loword(day1Spawns)
    sta tmp8
    sep #$20
    .a8
    jsr SpawnTable
    rts
.endproc


;-----------------------------------------------------------------------------
; IslandUpdate -- one frame of the errand.  A8/I16.
;-----------------------------------------------------------------------------
.proc IslandUpdate
    .a8
    .i16
    ; The day change runs on its own and ignores everything else.
    lda questState
    cmp #Q_DAYOUT
    bne :+
    jmp DayOut
:   cmp #Q_DAYIN
    bne :+
    jmp DayIn
:
    jsr TextBusy
    bcc @free
    rts                         ; somebody is talking

@free:
    ; The race owns the scene from the countdown until the raft has a name.
    lda questState
    cmp #Q_RACE_SET
    bne :+
    jmp Countdown
:   cmp #Q_RACE_RUN
    bne :+
    jmp RaceRun
:   cmp #Q_RACE_OVER
    bne :+
    jmp AfterRace
:   cmp #Q_NAMING
    bne :+
    jmp TakeName
:   cmp #Q_DUSK
    bne :+
    jmp Dusk
:
    ; Day one finished and its last line dismissed: turn in for the night.
    lda questState
    cmp #Q_DONE
    bne @play
    lda questDay
    cmp #1
    bne @play
    jmp EndOfDay

@play:
    ; Once the raft has a name there is nothing left to gather, and during the
    ; race Sora has better things to do than talk.
    lda questState
    cmp #Q_RACE_SET
    bcc :+
    cmp #Q_NAMED
    bne @out
:   jsr CheckPickups
    bcs @out                    ; something was just picked up

    rep #$20
    .a16
    lda padPressed
    and #PAD_B
    sep #$20
    .a8
    beq @notB
    jsr SwingAt
    bcs @out
@notB:
    rep #$20
    .a16
    lda padPressed
    and #PAD_A
    sep #$20
    .a8
    beq @out
    jsr FindTalker
    bcc :+
    jsr TalkTo
    rts
:   jsr FindProp
    bcc @out
    jsr LookAt
@out:
    rts
.endproc

;=============================================================================
; The change of day
;=============================================================================

;-----------------------------------------------------------------------------
; Dusk -- the last evening.  Everything is on the raft, so the light simply
; goes, and what comes up is not the morning.  A8/I16.
;-----------------------------------------------------------------------------
.proc Dusk
    .a8
    .i16
    lda dayTimer
    beq @gone
    dec dayTimer
    lsr a
    sta screenBright            ; DAY_FADE/2 down to 0
    rts
@gone:
    jmp NightBegin
.endproc

;-----------------------------------------------------------------------------
; EndOfDay -- A8/I16.
;-----------------------------------------------------------------------------
.proc EndOfDay
    .a8
    .i16
    lda #Q_DAYOUT
    sta questState
    lda #DAY_FADE
    sta dayTimer
    rts
.endproc

;-----------------------------------------------------------------------------
; DayOut -- dim to black, then rebuild the island for the morning.  A8/I16.
;-----------------------------------------------------------------------------
.proc DayOut
    .a8
    .i16
    lda dayTimer
    beq @over
    dec dayTimer
    lsr a
    sta screenBright            ; DAY_FADE/2 down to 0
    rts

@over:
    lda #$8F
    sta screenBright
    sta INIDISP                 ; forced blank while the cast is rebuilt

    jsr ClearActors
    jsr InitWorld               ; the islanders and the scenery, back in place
    rep #$20
    .a16
    lda #.loword(day2Spawns)
    sta tmp8
    sep #$20
    .a8
    jsr SpawnTable

    lda #2
    sta questDay
    stz questState              ; Q_IDLE: Kairi has a new list
    jsr HudUpdate
    lda #Q_DAYIN
    sta questState
    lda #DAY_FADE
    sta dayTimer
    stz screenBright
    rts
.endproc

;-----------------------------------------------------------------------------
; DayIn -- and back up into day two.  A8/I16.
;-----------------------------------------------------------------------------
.proc DayIn
    .a8
    .i16
    lda dayTimer
    beq @up
    dec dayTimer
    lda #DAY_FADE
    sec
    sbc dayTimer
    lsr a
    sta screenBright
    rts

@up:
    lda #$0F
    sta screenBright
    stz questState              ; Q_IDLE
    rep #$20
    .a16
    lda #.loword(scriptNextDay)
    jsr Say
    rts
.endproc

;=============================================================================
; Finding things near Sora
;=============================================================================


;-----------------------------------------------------------------------------
; PlayerZ -- the deck Sora is standing on, into tmp4.  A8/I16.
;-----------------------------------------------------------------------------
.proc PlayerZ
    .a8
    .i16
    lda playerIdx
    rep #$20
    .a16
    and #$00FF
    tax
    sep #$20
    .a8
    lda actZ,x
    sta tmp4
    rts
.endproc


;-----------------------------------------------------------------------------
; CheckPickups -- walk into a collectable and it is yours.
; Out: carry set if something was taken.  A8/I16.
;-----------------------------------------------------------------------------
.proc CheckPickups
    .a8
    .i16
    jsr PlayerPos
    rep #$20
    .a16
    lda #PICK_X
    sta tmp2
    lda #PICK_Y
    sta tmp3
    sep #$20
    .a8
    ; Reaching up onto the treehouse from the grass below it should not count.
    jsr PlayerZ

    ldx #0
@scan:
    lda actType,x
    cmp #ACT_LOG
    bcc @next
    cmp #(ACT_BOTTLE + 1)
    bcs @next
    lda actZ,x
    cmp tmp4
    bne @next
    jsr NearPlayer
    bcc @next

    ; Take it off the beach first, so nothing below can pick it up twice.
    lda actType,x
    stz actType,x
    sec
    sbc #ACT_LOG                ; ...which is also its tally slot
    jmp Collect

@next:
    inx
    cpx #MAX_ACTORS
    bcc @scan
    clc
    rts
.endproc

;-----------------------------------------------------------------------------
; Collect -- tally one item and say so.
; In (A8/I16): A = itemCount slot.  Out: carry set.
;-----------------------------------------------------------------------------
.proc Collect
    .a8
    .i16
    rep #$20
    .a16
    and #$00FF
    tax
    sep #$20
    .a8
    inc itemCount,x
    phx
    jsr HudUpdate
    plx
    rep #$20
    .a16
    txa
    asl a
    tax
    lda gotLines,x
    jsr Say
    sep #$20
    .a8
    sec
    rts
.endproc

;-----------------------------------------------------------------------------
; SwingAt -- what the keyblade is good for when nothing is attacking: fish in
; the shallows, and coconuts that will not come down on their own.
; Out: carry set if the swing landed on something.  A8/I16.
;-----------------------------------------------------------------------------
.proc SwingAt
    .a8
    .i16
    jsr PlayerPos
    rep #$20
    .a16
    lda #SWING_X
    sta tmp2
    lda #SWING_Y
    sta tmp3
    sep #$20
    .a8

    ldx #0
@scan:
    lda actType,x
    cmp #ACT_FISH
    beq @try
    cmp #ACT_PALMC
    bne @next
@try:
    jsr NearPlayer
    bcc @next

    lda actType,x
    cmp #ACT_FISH
    bne @palm
    stz actType,x               ; caught
    lda #IT_FISH
    jmp Collect

@palm:
    ; The palm keeps standing; it just has nothing left to give.
    lda #ACT_PALM
    sta actType,x
    lda #IT_NUT
    jmp Collect

@next:
    inx
    cpx #MAX_ACTORS
    bcc @scan
    clc
    rts
.endproc

;-----------------------------------------------------------------------------
; FindTalker -- the nearest islander within arm's reach.
; Out: carry set and pendTalk = their actor type.  A8/I16.
;-----------------------------------------------------------------------------
.proc FindTalker
    .a8
    .i16
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
    cmp #ACT_KAIRI
    bcc @next
    cmp #(ACT_WAKKA + 1)
    bcs @next
    jsr NearPlayer
    bcc @next
    lda actType,x
    sta pendTalk
    sec
    rts
@next:
    inx
    cpx #MAX_ACTORS
    bcc @scan
    clc
    rts
.endproc

;=============================================================================
; Talking
;=============================================================================

;-----------------------------------------------------------------------------
; FindProp -- something on the cave wall within looking distance.
; Out: carry set and pendTalk = what it is.  A8/I16.
;-----------------------------------------------------------------------------
.proc FindProp
    .a8
    .i16
    jsr PlayerPos
    rep #$20
    .a16
    lda #PROP_X
    sta tmp2
    lda #PROP_Y
    sta tmp3
    lda #$7FFF
    sta tmp6                    ; best distance so far
    sep #$20
    .a8
    stz tmp7                    ; ...and what scored it

    ldx #0
@scan:
    lda actType,x
    cmp #ACT_DOOR
    bcc @next
    cmp #(ACT_SCRIBBLE + 1)
    bcs @next
    jsr NearPlayer
    bcc @next

    ; Three things hang within arm's reach of each other, so being in range
    ; is not enough -- take whichever is closest.
    stx tmp5
    lda actType,x
    sta tmp8
    rep #$20
    .a16
    lda tmp5
    asl a
    tax
    lda actX,x
    sec
    sbc tmp0
    bpl :+
    eor #$FFFF
    inc a
:
    sta tmp4
    lda actY,x
    sec
    sbc tmp1
    bpl :+
    eor #$FFFF
    inc a
:   ; Both axes are the same scale, so a plain |dx| + |dy| separates the three
    ; drawings: they sit side by side along the wall, one tile apart.
    clc
    adc tmp4
    cmp tmp6
    bcs @keep
    sta tmp6
    sep #$20
    .a8
    lda tmp8
    sta tmp7
    rep #$20
    .a16
@keep:
    sep #$20
    .a8
    ldx tmp5

@next:
    inx
    cpx #MAX_ACTORS
    bcc @scan

    lda tmp7
    beq @none
    sta pendTalk
    sec
    rts
@none:
    clc
    rts
.endproc

;-----------------------------------------------------------------------------
; LookAt -- what Sora makes of the thing in pendTalk.  A8/I16.
;
; The door is the one that changes: before the raft has a name it is just an
; oddity, and after it Sora has started to wonder about it.
;-----------------------------------------------------------------------------
.proc LookAt
    .a8
    .i16
    ldx #0                      ; index into propLines
    lda pendTalk
    cmp #ACT_DOOR
    bne @drawing
    lda questState
    cmp #Q_NAMED
    bne @say
    ldx #2                      ; he has started to wonder about it
    bra @say

@drawing:
    cmp #ACT_FACES
    bne :+
    ldx #4
    bra @say
:   ldx #6

@say:
    rep #$20
    .a16
    lda propLines,x
    jmp Say
.endproc

;-----------------------------------------------------------------------------
; Say -- open a message box on a script in this bank.
; In (A16/I16): the script's address.  Returns in A8.
;-----------------------------------------------------------------------------
.proc Say
    .a16
    .i16
    sta txtPtr                  ; a 16-bit store fills both pointer bytes
    sep #$20
    .a8
    lda #^scriptKairiAsk        ; every line on the island shares one bank
    sta txtPtr+2
    lda #TM_MESSAGE
    jsr TextOpen
    rts
.endproc

;-----------------------------------------------------------------------------
; TalkTo -- whatever the islander in pendTalk has to say.  A8/I16.
;-----------------------------------------------------------------------------
.proc TalkTo
    .a8
    .i16
    lda pendTalk
    cmp #ACT_KAIRI
    bne @other
    jmp TalkKairi

@other:
    sec
    sbc #ACT_RIKU
    rep #$20
    .a16
    and #$00FF
    asl a
    tax
    sep #$20
    .a8
    lda questDay
    cmp #2
    beq @day2
    rep #$20
    .a16
    lda islanderLines,x
    jmp Say
@day2:
    rep #$20
    .a16
    lda islander2Lines,x
    jmp Say
.endproc

;-----------------------------------------------------------------------------
.proc TalkKairi
    .a8
    .i16
    lda questState
    cmp #Q_NAMED
    beq @allset
    cmp #Q_DONE
    bne @asked
    ldx #0                      ; the rest-up line
    jmp @say

@allset:
    ; The raft has a name and everything is on it.  There is nothing left to
    ; do on the island, so this line is the last of the day -- dismissing it
    ; puts the light out.
    lda #Q_DUSK
    sta questState
    lda #DAY_FADE
    sta dayTimer
    rep #$20
    .a16
    lda #.loword(scriptAllSet)
    jmp Say

    .a8
@asked:
    cmp #Q_ACTIVE
    beq @check

    ; First time asking: hand over the list, and the tally row with it.
    lda #Q_ACTIVE
    sta questState
    jsr HudUpdate
    ldx #2                      ; the list
    bra @say

@check:
    jsr HaveAll
    bcc @short
    lda #Q_DONE
    sta questState
    ldx #4                      ; that's everything
    bra @say
@short:
    ldx #6                      ; still something missing

@say:
    ; Day two's list handed in and acknowledged: Riku wants his race.
    lda questDay
    cmp #2
    bne @lines
    lda questState
    cmp #Q_DONE
    bne @lines
    cpx #0                      ; the rest-up line is where the offer lands
    bne @lines
    jmp OfferRace

@lines:
    lda questDay
    cmp #2
    beq @day2
    rep #$20
    .a16
    lda kairiLines,x
    jmp Say
@day2:
    rep #$20
    .a16
    lda kairi2Lines,x
    jmp Say
.endproc

;-----------------------------------------------------------------------------
; HaveAll -- carry set once the whole of today's list is in hand.  A8/I16.
;-----------------------------------------------------------------------------
.proc HaveAll
    .a8
    .i16
    lda questDay
    cmp #2
    beq @day2

    lda itemCount + IT_LOG
    cmp #NEED_LOGS
    bcc @no
    lda itemCount + IT_CLOTH
    cmp #NEED_CLOTH
    bcc @no
    lda itemCount + IT_ROPE
    cmp #NEED_ROPE
    bcc @no
    sec
    rts

@day2:
    lda itemCount + IT_FISH
    cmp #NEED_FISH
    bcc @no
    lda itemCount + IT_MUSH
    cmp #NEED_MUSH
    bcc @no
    lda itemCount + IT_NUT
    cmp #NEED_NUT
    bcc @no
    lda itemCount + IT_EGG
    cmp #NEED_EGG
    bcc @no
    lda itemCount + IT_WATER
    cmp #NEED_WATER
    bcc @no
    sec
    rts

@no:
    clc
    rts
.endproc


;=============================================================================
; The race, and what it decides
;=============================================================================

;-----------------------------------------------------------------------------
; OfferRace -- line the two of them up and start Kairi counting.  A8/I16.
;-----------------------------------------------------------------------------
.proc OfferRace
    .a8
    .i16
    lda #Q_RACE_SET
    sta questState
    lda #COUNT_LEN
    sta dayTimer
    stz raceLeg
    stz rikuWp
    stz raceWon
    jsr PlaceRacers
    jsr HudUpdate
    rep #$20
    .a16
    lda #.loword(scriptChallenge)
    jmp Say
.endproc

;-----------------------------------------------------------------------------
; PlaceRacers -- both of them onto the start line by Kairi.  A8/I16.
;-----------------------------------------------------------------------------
.proc PlaceRacers
    .a8
    .i16
    rep #$20
    .a16
    lda #START_SORA_X
    sta tmp0
    lda #START_SORA_Y
    sta tmp1
    sep #$20
    .a8
    lda playerIdx
    jsr PutActor

    rep #$20
    .a16
    lda #START_RIKU_X
    sta tmp0
    lda #START_RIKU_Y
    sta tmp1
    sep #$20
    .a8
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
    txa
    jmp PutActor
.endproc

;-----------------------------------------------------------------------------
; PutActor -- drop an actor at tmp0/tmp1 (Q12.4).  In (A8/I16): A = index.
;-----------------------------------------------------------------------------
.proc PutActor
    .a8
    .i16
    pha
    rep #$20
    .a16
    and #$00FF
    asl a
    tax
    lda tmp0
    sta actX,x
    lda tmp1
    sta actY,x
    stz actVX,x
    stz actVY,x
    sep #$20
    .a8
    pla
    rep #$20
    .a16
    and #$00FF
    tax
    sep #$20
    .a8
    jsr SetActorZ
    rts
.endproc

;-----------------------------------------------------------------------------
; Countdown -- three, two, one.  A8/I16.
;-----------------------------------------------------------------------------
.proc Countdown
    .a8
    .i16
    lda dayTimer
    beq @go
    dec dayTimer
    jsr HudUpdate               ; the number on the race row
    rts
@go:
    lda #Q_RACE_RUN
    sta questState
    jsr HudUpdate
    rts
.endproc

;-----------------------------------------------------------------------------
; RaceRun -- watch for either of them reaching the end of the course.  A8/I16.
;-----------------------------------------------------------------------------
.proc RaceRun
    .a8
    .i16
    lda rikuWp
    cmp #RACE_WPS
    bcc @sora
    lda #2                      ; Riku is home
    sta raceWon
    jmp RaceOver

@sora:
    rep #$20
    .a16
    lda #TAG_X
    sta tmp2
    lda #TAG_Y
    sta tmp3
    sep #$20
    .a8
    lda raceLeg
    bne @home

    rep #$20
    .a16
    lda #PAOPU_X
    sta tmp4
    lda #PAOPU_Y
    sta tmp5
    sep #$20
    .a8
    jsr SoraNear
    bcc @out
    lda #1
    sta raceLeg                 ; tagged; now get back
    jsr HudUpdate
    rts

@home:
    rep #$20
    .a16
    lda #FINISH_X
    sta tmp4
    lda #FINISH_Y
    sta tmp5
    sep #$20
    .a8
    jsr SoraNear
    bcc @out
    lda #1                      ; Sora is home
    sta raceWon
    jmp RaceOver
@out:
    rts
.endproc

;-----------------------------------------------------------------------------
; SoraNear -- is Sora within tmp2 by tmp3 of the point in tmp4/tmp5?
; Out: carry set on a hit.  A8/I16.
;-----------------------------------------------------------------------------
.proc SoraNear
    .a8
    .i16
    lda playerIdx
    rep #$20
    .a16
    and #$00FF
    asl a
    tax
    lda tmp4
    sec
    sbc actX,x
    bpl :+
    eor #$FFFF
    inc a
:
    cmp tmp2
    bcs @no
    lda tmp5
    sec
    sbc actY,x
    bpl :+
    eor #$FFFF
    inc a
:   cmp tmp3
    bcs @no
    sep #$20
    .a8
    sec
    rts
@no:
    sep #$20
    .a8
    clc
    rts
.endproc

;-----------------------------------------------------------------------------
; RaceOver -- A8/I16.
;-----------------------------------------------------------------------------
.proc RaceOver
    .a8
    .i16
    lda #Q_RACE_OVER
    sta questState
    jsr HudUpdate
    lda raceWon
    cmp #1
    bne @lost
    rep #$20
    .a16
    lda #.loword(scriptSoraWins)
    jmp Say
@lost:
    rep #$20
    .a16
    lda #.loword(scriptRikuWins)
    jmp Say
.endproc

;-----------------------------------------------------------------------------
; AfterRace -- the result line has been dismissed.  A8/I16.
;-----------------------------------------------------------------------------
.proc AfterRace
    .a8
    .i16
    lda raceWon
    cmp #1
    bne @riku

    lda #Q_NAMING
    sta questState
    rep #$20
    .a16
    lda #.loword(scriptWhatName)
    sta txtPtr
    sep #$20
    .a8
    lda #^scriptWhatName
    sta txtPtr+2
    lda #TM_RAFT                ; the three names, not yes/no
    jsr TextOpen
    rts

@riku:
    ; He won, so he names it, and he was never going to pick anything else.
    lda #RAFT_EXCALIBUR
    sta raftName
    lda #Q_NAMED
    sta questState
    rep #$20
    .a16
    lda #.loword(scriptRikuNames)
    jmp Say
.endproc

;-----------------------------------------------------------------------------
; TakeName -- the prompt has closed; txtResult is the name Sora chose.  A8/I16.
;-----------------------------------------------------------------------------
.proc TakeName
    .a8
    .i16
    lda txtResult
    beq @out                    ; still choosing
    sta raftName
    stz txtResult
    lda #Q_NAMED
    sta questState
    lda raftName                ; ...which the state constant above clobbered
    dec a                       ; RAFT_HIGHWIND is the first line
    rep #$20
    .a16
    and #$00FF
    asl a
    tax
    lda namedLines,x
    jmp Say
@out:
    rts
.endproc

;-----------------------------------------------------------------------------
; UpdateRiku -- In (A8/I16): X = actor index.
;
; Riku only moves during the race, and then only between the markers the
; course is made of.  He ignores the ground: every marker sits on a walkable
; tile by construction, so steering him round the boulder would cost more than
; it is worth.  rikuWp is the marker he is running for; RACE_WPS means home.
;-----------------------------------------------------------------------------
.proc UpdateRiku
    .a8
    .i16
    lda questState
    cmp #Q_RACE_RUN
    beq :+
    rts
:   lda rikuWp
    cmp #RACE_WPS
    bcc :+
    rts
:   stx curActor

    ; where he is running for
    rep #$20
    .a16
    lda rikuWp
    and #$00FF
    asl a
    asl a                       ; four bytes a marker
    tax
    lda raceWp,x
    sta tmp0
    lda raceWp+2,x
    sta tmp1

    lda curActor
    asl a
    tax                         ; X = his word offset from here on

    ;--- which way is he leaning ---
    lda tmp0
    sec
    sbc actX,x
    sta tmp2
    bmi @west
    sep #$20
    .a8
    ldy curActor
    lda actFlags,y
    and #<(~AF_HFLIP)
    sta actFlags,y
    rep #$20
    .a16
    bra @movex
@west:
    sep #$20
    .a8
    ldy curActor
    lda actFlags,y
    ora #AF_HFLIP
    sta actFlags,y
    rep #$20
    .a16
@movex:

    ;--- close the gap, by at most one step ---
    lda tmp2
    bpl @east
    cmp #.loword(-RIKU_VX)
    bcs @xgo
    lda #.loword(-RIKU_VX)
    bra @xgo
@east:
    cmp #RIKU_VX
    bcc @xgo
    lda #RIKU_VX
@xgo:
    clc
    adc actX,x
    sta actX,x

    lda tmp1
    sec
    sbc actY,x
    bpl @south
    cmp #.loword(-RIKU_VY)
    bcs @ygo
    lda #.loword(-RIKU_VY)
    bra @ygo
@south:
    cmp #RIKU_VY
    bcc @ygo
    lda #RIKU_VY
@ygo:
    clc
    adc actY,x
    sta actY,x

    ;--- near enough to take the next marker? ---
    lda tmp0
    sec
    sbc actX,x
    bpl :+
    eor #$FFFF
    inc a
:   cmp #RIKU_NEAR
    bcs @done
    lda tmp1
    sec
    sbc actY,x
    bpl :+
    eor #$FFFF
    inc a
:   cmp #RIKU_NEAR
    bcs @done
    sep #$20
    .a8
    inc rikuWp
    rep #$20
    .a16
@done:
    sep #$20
    .a8
    ldx curActor
    rts
.endproc

;=============================================================================
; Scene data
;=============================================================================

; A course marker, from tile coordinates to the middle of that cell in Q12.4
; world pixels.
.macro WP i, j
    .word CELL_X(i)
    .word CELL_Y(j)
.endmacro

.segment "RODATA"

; East along the beach, over the footbridge, up the spit, across the big
; bridge, round the paopu tree and back.  Riku runs these in order; Sora may
; take any line he likes.
raceWp:
    WP 16, 12
    WP 18, 12
    WP 20, 12
    WP 20, 10
    WP 20,  8
    WP 22,  8
    WP 25,  8
    WP 28,  8
    WP 28,  6
    WP 26,  6
    WP 25,  8
    WP 22,  8
    WP 20,  8
    WP 20, 10
    WP 20, 12
    WP 18, 12
    WP 16, 12
    WP 14, 12
    WP 13, 12
    WP 12, 12
raceWpEnd:
.assert ((raceWpEnd - raceWp) / 4) = RACE_WPS, error, "RACE_WPS"

propLines:
    .word .loword(scriptDoor), .loword(scriptDoor2)
    .word .loword(scriptFaces), .loword(scriptScribble)

namedLines:
    .word .loword(scriptNamedHighwind)
    .word .loword(scriptNamedExcalibur)
    .word .loword(scriptNamedRagnarok)

; type, tile i, tile j -- terminated by $FF
day1Spawns:
    .byte ACT_LOG,     20, 12        ; the shore past the little footbridge
    .byte ACT_LOG,     28,  9        ; the small island where Riku sits
    .byte ACT_CLOTH,   12,  4        ; inside the treehouse
    .byte ACT_ROPE,     8,  4        ; the lookout platform, beside Tidus
    .byte $FF

day2Spawns:
    .byte ACT_FISH,     4, 12        ; the shallows off the west beach
    .byte ACT_FISH,     6, 13        ; ...the south one
    .byte ACT_FISH,    17, 13        ; ...and the inlet under the footbridge
    .byte ACT_MUSH,     9, 12        ; the hollow behind the rock by Kairi
    .byte ACT_MUSH,     6,  4        ; the bushes at the foot of the tower
    .byte ACT_MUSH,     2,  7        ; inside the Secret Place
    .byte ACT_EGG,     21,  6        ; the nest atop the leaning tree
    .byte ACT_BOTTLE,   4,  7        ; under the waterfall
    .byte $FF

; Kairi has four lines a day: rest, the list, that's everything, and a nudge.
kairiLines:
    .word .loword(scriptKairiRest), .loword(scriptKairiAsk)
    .word .loword(scriptKairiFinish), .loword(scriptKairiRemind)
kairi2Lines:
    .word .loword(scriptKairiRest2), .loword(scriptKairiAsk2)
    .word .loword(scriptKairiFinish2), .loword(scriptKairiRemind2)

islanderLines:
    .word .loword(scriptRiku), .loword(scriptTidus)
    .word .loword(scriptSelphie), .loword(scriptWakka)
islander2Lines:
    .word .loword(scriptRiku2), .loword(scriptTidus2)
    .word .loword(scriptSelphie2), .loword(scriptWakka2)

; Indexed by itemCount slot.
gotLines:
    .word .loword(scriptGotLog), .loword(scriptGotCloth)
    .word .loword(scriptGotRope), .loword(scriptGotMush)
    .word .loword(scriptGotNut), .loword(scriptGotEgg)
    .word .loword(scriptGotWater), .loword(scriptGotFish)

;--- day one -----------------------------------------------------------------
scriptKairiAsk:
    .byte "SORA! THERE YOU ARE.", SC_PAGE
    .byte "THE RAFT STILL NEEDS", SC_NL
    .byte "TWO LOGS, SOME CLOTH", SC_NL
    .byte "AND A LENGTH OF ROPE.", SC_PAGE
    .byte "ONE LOG IS PAST THE", SC_NL
    .byte "LITTLE BRIDGE. THE OTHER", SC_NL
    .byte "IS OUT WHERE RIKU SITS.", SC_PAGE
    .byte "THE CLOTH IS UP IN THE", SC_NL
    .byte "TREEHOUSE, AND THE ROPE", SC_NL
    .byte "IS ON TIDUS' PLATFORM.", SC_END

scriptKairiRemind:
    .byte "STILL SOMETHING", SC_NL
    .byte "MISSING, SORA.", SC_PAGE
    .byte "TWO LOGS, THE CLOTH", SC_NL
    .byte "FROM THE TREEHOUSE,", SC_NL
    .byte "AND TIDUS' ROPE.", SC_END

scriptKairiFinish:
    .byte "THAT'S EVERYTHING!", SC_PAGE
    .byte "THE RAFT IS ALMOST DONE.", SC_NL
    .byte SC_NL
    .byte "TOMORROW WE'LL NEED", SC_NL
    .byte "FOOD AND WATER.", SC_PAGE
    .byte "GET SOME REST, SORA.", SC_END

scriptKairiRest:
    .byte "GO ON, GET SOME REST.", SC_NL
    .byte SC_NL
    .byte "TOMORROW IS A BIG DAY.", SC_END

scriptRiku:
    .byte "YOU'RE SLOW, SORA.", SC_PAGE
    .byte "THERE'S A LOG BEHIND ME.", SC_NL
    .byte SC_NL
    .byte "GO ON. TAKE IT.", SC_END

scriptTidus:
    .byte "UP FOR A MATCH?", SC_PAGE
    .byte "...NO? FINE.", SC_NL
    .byte SC_NL
    .byte "THE ROPE UP HERE", SC_NL
    .byte "IS ALL YOURS.", SC_END

scriptSelphie:
    .byte "THE OCEAN IS HUGE,", SC_NL
    .byte "ISN'T IT?", SC_PAGE
    .byte "KAIRI HAS BEEN TALKING", SC_NL
    .byte "ABOUT THAT RAFT", SC_NL
    .byte "ALL DAY LONG.", SC_END

scriptWakka:
    .byte "HEY, SORA!", SC_PAGE
    .byte "WATCH THE BRIDGE, YA?", SC_NL
    .byte SC_NL
    .byte "THOSE PLANKS ARE SLIPPERY", SC_NL
    .byte "THIS TIME OF DAY.", SC_END

;--- the morning after --------------------------------------------------------
scriptNextDay:
    .byte "THE NEXT MORNING.", SC_END

;--- day two ------------------------------------------------------------------
scriptKairiAsk2:
    .byte "MORNING, SORA!", SC_PAGE
    .byte "A RAFT IS NO GOOD", SC_NL
    .byte "WITHOUT PROVISIONS.", SC_PAGE
    .byte "THREE FISH FROM THE", SC_NL
    .byte "SHALLOWS, AND THREE", SC_NL
    .byte "MUSHROOMS.", SC_PAGE
    .byte "ONE IS BEHIND THE ROCK", SC_NL
    .byte "RIGHT THERE. ONE IS IN", SC_NL
    .byte "THE BUSHES BY THE TOWER.", SC_PAGE
    .byte "THE LAST ONE IS IN THE", SC_NL
    .byte "SECRET PLACE, BEHIND", SC_NL
    .byte "THE WATERFALL.", SC_PAGE
    .byte "TWO COCONUTS -- HIT THE", SC_NL
    .byte "PALMS BY ME UNTIL THE", SC_NL
    .byte "GOLD ONES COME DOWN.", SC_PAGE
    .byte "THEN A SEAGULL EGG FROM", SC_NL
    .byte "THE TREE BY THE BRIDGE,", SC_NL
    .byte "AND WATER FROM THE FALL.", SC_END

scriptKairiRemind2:
    .byte "NOT QUITE, SORA.", SC_PAGE
    .byte "FISH, MUSHROOMS,", SC_NL
    .byte "COCONUTS, THE EGG,", SC_NL
    .byte "AND THE WATER.", SC_END

scriptKairiFinish2:
    .byte "THAT'S ALL OF IT!", SC_PAGE
    .byte "THE RAFT IS READY.", SC_NL
    .byte SC_NL
    .byte "TOMORROW WE FINALLY", SC_NL
    .byte "SEE WHAT'S OUT THERE.", SC_END

scriptKairiRest2:
    .byte "IT'S ALL LOADED.", SC_NL
    .byte SC_NL
    .byte "GET SOME SLEEP, SORA.", SC_END

scriptRiku2:
    .byte "SO KAIRI HAS YOU", SC_NL
    .byte "RUNNING ERRANDS AGAIN.", SC_PAGE
    .byte "THE GULLS NEST UP THE", SC_NL
    .byte "LEANING TREE BY THE", SC_NL
    .byte "BRIDGE. CLIMB IT.", SC_END

scriptTidus2:
    .byte "THE FISH ARE BITING", SC_NL
    .byte "DOWN BY THE SHORE.", SC_PAGE
    .byte "SWING AT ONE. YOU'LL", SC_NL
    .byte "SURPRISE YOURSELF.", SC_END

scriptSelphie2:
    .byte "BEHIND THE WATERFALL", SC_NL
    .byte "THERE'S A CAVE.", SC_PAGE
    .byte "RIKU CALLS IT THE", SC_NL
    .byte "SECRET PLACE.", SC_END

scriptWakka2:
    .byte "COCONUTS, YA?", SC_PAGE
    .byte "GIVE THE PALMS BY KAIRI", SC_NL
    .byte "A GOOD WHACK.", SC_END

;--- pickups ------------------------------------------------------------------
scriptGotLog:
    .byte "GOT A LOG.", SC_END
scriptGotCloth:
    .byte "GOT THE CLOTH.", SC_END
scriptGotRope:
    .byte "GOT THE ROPE.", SC_END
scriptGotMush:
    .byte "GOT A MUSHROOM.", SC_END
scriptGotNut:
    .byte "A COCONUT COMES DOWN.", SC_END
scriptGotEgg:
    .byte "GOT THE SEAGULL EGG.", SC_END
scriptGotWater:
    .byte "FILLED THE BOTTLE.", SC_END
scriptGotFish:
    .byte "CAUGHT A FISH.", SC_END

;--- the race -----------------------------------------------------------------
scriptChallenge:
    .byte "ONE MORE THING, SORA.", SC_PAGE
    .byte "RIKU! WE'RE READY!", SC_PAGE
    .byte "SO. WHOEVER WINS GETS", SC_NL
    .byte "TO NAME THE RAFT.", SC_PAGE
    .byte "OUT ALONG THE SHORE,", SC_NL
    .byte "OVER THE BRIDGE, TAG THE", SC_NL
    .byte "PAOPU TREE, AND BACK.", SC_PAGE
    .byte "I'LL COUNT YOU DOWN!", SC_END

scriptSoraWins:
    .byte "SORA WINS!", SC_PAGE
    .byte "...FINE. YOU WERE", SC_NL
    .byte "FASTER. THIS TIME.", SC_END

scriptRikuWins:
    .byte "RIKU WINS.", SC_PAGE
    .byte "TOLD YOU. YOU'RE SLOW.", SC_END

scriptWhatName:
    .byte "SO WHAT DO WE CALL IT?", SC_END

scriptRikuNames:
    .byte "THEN IT'S THE EXCALIBUR.", SC_PAGE
    .byte "IT'S A GOOD NAME, SORA.", SC_NL
    .byte SC_NL
    .byte "YOU'LL GET USED TO IT.", SC_END

scriptNamedHighwind:
    .byte "THE HIGHWIND IT IS.", SC_PAGE
    .byte "TOMORROW, THEN.", SC_END

scriptNamedExcalibur:
    .byte "THE EXCALIBUR IT IS.", SC_PAGE
    .byte "TOMORROW, THEN.", SC_END

scriptNamedRagnarok:
    .byte "THE RAGNAROK IT IS.", SC_PAGE
    .byte "TOMORROW, THEN.", SC_END

scriptAllSet:
    .byte "EVERYTHING IS ABOARD.", SC_PAGE
    .byte "WE LEAVE AT DAWN, SORA.", SC_END

;--- the Secret Place ---------------------------------------------------------
scriptDoor:
    .byte "A DOOR, SET INTO", SC_NL
    .byte "THE ROCK.", SC_PAGE
    .byte "NO HANDLE. NO KEYHOLE.", SC_NL
    .byte SC_NL
    .byte "IT HAS ALWAYS BEEN HERE.", SC_END

scriptDoor2:
    .byte "THE DOOR IS STILL SHUT.", SC_PAGE
    .byte "...WHERE HAVE I HEARD", SC_NL
    .byte "THAT BEFORE?", SC_END

scriptFaces:
    .byte "TWO FACES, DRAWN IN", SC_NL
    .byte "CHALK, LOOKING AT", SC_NL
    .byte "EACH OTHER.", SC_PAGE
    .byte "SORA AND KAIRI.", SC_NL
    .byte SC_NL
    .byte "SHE DREW HIS SIDE.", SC_END

scriptScribble:
    .byte "A BOAT. A STAR.", SC_NL
    .byte "A FISH WITH TOO MANY", SC_NL
    .byte "FINS.", SC_PAGE
    .byte "EVERY SUMMER SOMEBODY", SC_NL
    .byte "ADDS ANOTHER.", SC_END
