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
.import SpawnActor, IsoToWorld, ClearActors, InitWorld

.export IslandInit, IslandUpdate

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
    jsr SpawnItems
    rts
.endproc

;-----------------------------------------------------------------------------
; SpawnItems -- walk a table of (type, isometric i, isometric j) triples until
; $FF.  In (A8/I16): tmp8 = the table's address in this bank.
;-----------------------------------------------------------------------------
.proc SpawnItems
    .a8
    .i16
    ldy #0
@loop:
    lda (tmp8),y
    cmp #$FF
    beq @done
    sta tmp6                    ; type
    iny
    lda (tmp8),y
    sta tmp4                    ; i
    iny
    lda (tmp8),y
    sta tmp5                    ; j
    iny
    sty tmp7                    ; SpawnActor clobbers Y, so park the cursor

    rep #$20
    .a16
    lda tmp4
    and #$00FF
    sta tmp0
    lda tmp5
    and #$00FF
    sta tmp1
    jsr IsoToWorld
    ; IsoToWorld lands on the diamond's top corner; step to its centre and
    ; convert to Q12.4.
    lda tmp0
    clc
    adc #16
    asl a
    asl a
    asl a
    asl a
    sta tmp0
    lda tmp1
    clc
    adc #8
    asl a
    asl a
    asl a
    asl a
    sta tmp1
    sep #$20
    .a8

    lda tmp6
    jsr SpawnActor
    ldy tmp7
    bra @loop
@done:
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
    cmp #Q_NIGHT
    bne :+
    jmp NightFade
:   cmp #Q_MORNING
    bne :+
    jmp MorningFade
:
    jsr TextBusy
    bcc @free
    rts                         ; somebody is talking

@free:
    ; Day one finished and its last line dismissed: turn in for the night.
    lda questState
    cmp #Q_DONE
    bne @play
    lda questDay
    cmp #1
    bne @play
    jmp BeginNight

@play:
    jsr CheckPickups
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
    bcc @out
    jsr TalkTo
@out:
    rts
.endproc

;=============================================================================
; The change of day
;=============================================================================

;-----------------------------------------------------------------------------
; BeginNight -- A8/I16.
;-----------------------------------------------------------------------------
.proc BeginNight
    .a8
    .i16
    lda #Q_NIGHT
    sta questState
    lda #DAY_FADE
    sta dayTimer
    rts
.endproc

;-----------------------------------------------------------------------------
; NightFade -- dim to black, then rebuild the island for the morning.  A8/I16.
;-----------------------------------------------------------------------------
.proc NightFade
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
    jsr SpawnItems

    lda #2
    sta questDay
    stz questState              ; Q_IDLE: Kairi has a new list
    jsr HudUpdate
    lda #Q_MORNING
    sta questState
    lda #DAY_FADE
    sta dayTimer
    stz screenBright
    rts
.endproc

;-----------------------------------------------------------------------------
; MorningFade -- and back up into day two.  A8/I16.
;-----------------------------------------------------------------------------
.proc MorningFade
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
; PlayerPos -- Sora's world position into tmp0/tmp1.  A8/I16.
;-----------------------------------------------------------------------------
.proc PlayerPos
    .a8
    .i16
    lda playerIdx
    rep #$20
    .a16
    and #$00FF
    asl a
    tax
    lda actX,x
    sta tmp0
    lda actY,x
    sta tmp1
    sep #$20
    .a8
    rts
.endproc

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
; NearPlayer -- is this actor within tmp2 by tmp3 of Sora?
; In (A8/I16): X = actor index, tmp0/tmp1 = Sora's position,
;              tmp2 = X range, tmp3 = Y range (both Q12.4)
; Out: carry set on a hit.  X is preserved.
;-----------------------------------------------------------------------------
.proc NearPlayer
    .a8
    .i16
    phx
    txa
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
:   lsr a                       ; the ground is squashed 2:1 across X
    cmp tmp2
    bcs @miss

    lda actY,x
    sec
    sbc tmp1
    bpl :+
    eor #$FFFF
    inc a
:   cmp tmp3
    bcs @miss

    sep #$20
    .a8
    plx
    sec
    rts

@miss:
    sep #$20
    .a8
    plx
    clc
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
    cmp #Q_DONE
    bne @asked
    ldx #0                      ; the rest-up line
    bra @say

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
; Scene data
;=============================================================================
.segment "RODATA"

; type, isometric i, isometric j -- terminated by $FF
day1Spawns:
    .byte ACT_LOG,     11, 11        ; the shore past the little bridge
    .byte ACT_LOG,     12,  2        ; the small island where Riku sits
    .byte ACT_CLOTH,    7,  6        ; inside the treehouse
    .byte ACT_ROPE,     3,  6        ; the high platform, beside Tidus
    .byte $FF

day2Spawns:
    .byte ACT_FISH,     4, 11        ; the shallows off the south beach
    .byte ACT_FISH,     3, 10
    .byte ACT_FISH,    12, 10        ; ...and off the east one
    .byte ACT_MUSH,     6, 11        ; the hollow behind the rock by Kairi
    .byte ACT_MUSH,     4,  5        ; the bushes at the foot of the tower
    .byte ACT_MUSH,     1,  7        ; inside the Secret Place
    .byte ACT_EGG,      7,  2        ; the nest atop the leaning tree
    .byte ACT_BOTTLE,   2,  7        ; under the waterfall
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
