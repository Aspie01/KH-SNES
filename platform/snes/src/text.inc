;=============================================================================
; text.inc -- BG3 character numbers and dialogue script control codes
;
; The numbering here mirrors the order tools/build_assets.py emits into the
; 2bpp font page.  Every glyph carries an opaque background so text can sit
; inside the dialogue window; CH_CLEAR is the one genuinely transparent cell.
;=============================================================================
.ifndef TEXT_INC
TEXT_INC = 1

CH_BLANK     = 0                ; space, opaque box background
CH_A         = 1                ; A..Z run from here
CH_0         = 27               ; 0..9 run from here
CH_DOT       = 37
CH_COMMA     = 38
CH_BANG      = 39
CH_QUERY     = 40
CH_APOS      = 41
CH_DASH      = 42
CH_COLON     = 43
CH_SLASH     = 44
CH_CURSOR    = 45               ; menu selection arrow
CH_ADVANCE   = 46               ; "press a button" indicator

CH_BAR_L     = 48               ; HP gauge pieces
CH_BAR_FULL  = 49
CH_BAR_HALF  = 50
CH_BAR_EMPTY = 51
CH_BAR_R     = 52

CH_WIN_TL    = 56               ; dialogue window nine-patch
CH_WIN_T     = 57
CH_WIN_TR    = 58
CH_WIN_L     = 59
CH_WIN_C     = 60
CH_WIN_R     = 61
CH_WIN_BL    = 62
CH_WIN_B     = 63
CH_WIN_BR    = 64

CH_CLEAR     = 127              ; fully transparent

; BG3 tilemap attribute: priority bit plus palette 4, which for a 2bpp layer
; is CGRAM colours 16-19.
TXT_ATTR     = $3000

;--- dialogue box geometry, in BG3 tilemap cells -----------------------------
BOX_ROW      = 20               ; first tilemap row the box occupies
BOX_ROWS     = 7
TEXT_COL     = 2                ; first column of the text area
TEXT_ROW     = 1                ; first box-relative row of the text area
TEXT_W       = 28               ; characters per line
TEXT_H       = 5                ; lines before the box has to page
PROMPT_ROW   = 3                ; yes/no sits below a three-line question
MENU_ROW     = 2                ; a three-way menu needs the line above too
OPT_W        = 11               ; cells an option's text may occupy

;--- script control codes (anything below 32 is a code, not a character) -----
SC_END       = $00              ; end of message
SC_NL        = $01              ; newline
SC_PAGE      = $02              ; wait, clear the text area, keep going

;--- txtState ----------------------------------------------------------------
TS_CLOSED    = 0
TS_REVEAL    = 1
TS_WAIT      = 2                ; message finished, waiting for a button
TS_PROMPT    = 3                ; yes/no selector is up

;--- txtMode -----------------------------------------------------------------
; Anything above TM_MESSAGE is a prompt; the mode doubles as the menu id, so
; TM_PROMPT shows yes/no and TM_RAFT the three names for the raft.
TM_MESSAGE   = 0
TM_PROMPT    = 1
TM_RAFT      = 2

;--- assembling a run of font tiles from a plain string ----------------------
; Only the characters the HUD and the menus actually use are mapped; the run
; is terminated by $FF.
.macro TXTSTR str
    .repeat .strlen(str), i
    .if .strat(str, i) = 32
        .byte CH_BLANK
    .elseif .strat(str, i) = 47
        .byte CH_SLASH
    .elseif .strat(str, i) < 65
        .byte CH_0 + .strat(str, i) - 48
    .else
        .byte CH_A + .strat(str, i) - 65
    .endif
    .endrepeat
    .byte $FF
.endmacro

.endif ; TEXT_INC
