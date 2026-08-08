;=============================================================================
; island.s -- Destiny Islands, day one
;
; Kairi wants the raft finished, and the raft wants two logs, a bolt of cloth
; and a length of rope.  This file owns the errand: what each islander says,
; and the tally that decides which line Kairi gives.
;
; Materials are taken by walking into them.  People need a button press, which
; keeps a stroll along the beach from tripping over five conversations.
;=============================================================================
.p816
.include "snes.inc"
.include "game.inc"
.include "ram.inc"
.include "macros.inc"
.include "text.inc"

.import TextOpen, TextBusy
.import HudUpdate

.export IslandInit, IslandUpdate

.segment "CODE"

;-----------------------------------------------------------------------------
; IslandInit -- start the day with an empty tally.  A8/I16.
;-----------------------------------------------------------------------------
.proc IslandInit
    .a8
    .i16
    stz questState
    stz itemLogs
    stz itemCloth
    stz itemRope
    stz pendTalk
    rts
.endproc

;-----------------------------------------------------------------------------
; IslandUpdate -- one frame of the errand.  A8/I16.
;-----------------------------------------------------------------------------
.proc IslandUpdate
    .a8
    .i16
    jsr TextBusy
    bcc @free
    rts                         ; somebody is talking

@free:
    jsr CheckPickups
    bcs @out                    ; something was just picked up

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
; CheckPickups -- walk into a raft material and it is yours.
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

    ; Which deck Sora is standing on: reaching up onto the treehouse from the
    ; grass below it should not count as picking anything up.
    lda playerIdx
    rep #$20
    .a16
    and #$00FF
    tax
    sep #$20
    .a8
    lda actZ,x
    sta tmp4

    ldx #0
@scan:
    lda actType,x
    cmp #ACT_LOG
    bcc @next
    cmp #(ACT_ROPE + 1)
    bcs @next
    lda actZ,x
    cmp tmp4
    bne @next
    jsr NearPlayer
    bcc @next

    ; Take it off the beach first, so nothing below can pick it up twice.
    lda actType,x
    stz actType,x
    ldx #0                      ; index into gotLines
    cmp #ACT_LOG
    bne :+
    inc itemLogs
    bra @got
:   cmp #ACT_CLOTH
    bne :+
    inc itemCloth
    ldx #2
    bra @got
:   inc itemRope
    ldx #4

@got:
    phx
    jsr HudUpdate               ; the tally moves on
    plx
    rep #$20
    .a16
    lda gotLines,x
    jsr Say
    sep #$20
    .a8
    sec
    rts

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
    lda islanderLines,x
    jmp Say
.endproc

;-----------------------------------------------------------------------------
; TalkKairi -- she is the one keeping the list.  A8/I16.
;-----------------------------------------------------------------------------
.proc TalkKairi
    .a8
    .i16
    lda questState
    cmp #Q_DONE
    bne @asked
    ldx #0                      ; scriptKairiRest
    bra @say

@asked:
    cmp #Q_ACTIVE
    beq @check

    ; First time asking: hand over the list, and the tally row with it.
    lda #Q_ACTIVE
    sta questState
    jsr HudUpdate
    ldx #2                      ; scriptKairiAsk
    bra @say

@check:
    jsr HaveAll
    bcc @short
    lda #Q_DONE
    sta questState
    ldx #4                      ; scriptKairiFinish
    bra @say
@short:
    ldx #6                      ; scriptKairiRemind

@say:
    rep #$20
    .a16
    lda kairiLines,x
    jmp Say
.endproc

;-----------------------------------------------------------------------------
; HaveAll -- carry set once every material on the list is in hand.  A8/I16.
;-----------------------------------------------------------------------------
.proc HaveAll
    .a8
    .i16
    lda itemLogs
    cmp #NEED_LOGS
    bcc @no
    lda itemCloth
    cmp #NEED_CLOTH
    bcc @no
    lda itemRope
    cmp #NEED_ROPE
    bcc @no
    sec
    rts
@no:
    clc
    rts
.endproc

;=============================================================================
; Scripts
;=============================================================================
.segment "RODATA"

kairiLines:
    .word .loword(scriptKairiRest), .loword(scriptKairiAsk)
    .word .loword(scriptKairiFinish), .loword(scriptKairiRemind)

islanderLines:
    .word .loword(scriptRiku), .loword(scriptTidus)
    .word .loword(scriptSelphie), .loword(scriptWakka)

gotLines:
    .word .loword(scriptGotLog), .loword(scriptGotCloth)
    .word .loword(scriptGotRope)

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

scriptGotLog:
    .byte "GOT A LOG.", SC_END

scriptGotCloth:
    .byte "GOT THE CLOTH.", SC_END

scriptGotRope:
    .byte "GOT THE ROPE.", SC_END
