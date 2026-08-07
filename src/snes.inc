;=============================================================================
; snes.inc -- SNES hardware register definitions
;=============================================================================
.ifndef SNES_INC
SNES_INC = 1

;--- PPU registers (write) ---------------------------------------------------
INIDISP     = $2100      ; screen brightness / forced blank
OBSEL       = $2101      ; object size & character base
OAMADDL     = $2102
OAMADDH     = $2103
OAMDATA     = $2104
BGMODE      = $2105      ; BG mode + per-BG tile size
MOSAIC      = $2106
BG1SC       = $2107      ; tilemap base + size
BG2SC       = $2108
BG3SC       = $2109
BG4SC       = $210A
BG12NBA     = $210B      ; BG1/BG2 character base
BG34NBA     = $210C      ; BG3/BG4 character base
BG1HOFS     = $210D      ; scroll (write twice: low, high)
BG1VOFS     = $210E
BG2HOFS     = $210F
BG2VOFS     = $2110
BG3HOFS     = $2111
BG3VOFS     = $2112
BG4HOFS     = $2113
BG4VOFS     = $2114
VMAIN       = $2115      ; VRAM address increment mode
VMADDL      = $2116      ; VRAM word address
VMADDH      = $2117
VMDATAL     = $2118
VMDATAH     = $2119
M7SEL       = $211A
M7A         = $211B
M7B         = $211C
M7C         = $211D
M7D         = $211E
M7X         = $211F
M7Y         = $2120
CGADD       = $2121      ; CGRAM (palette) address
CGDATA      = $2122
W12SEL      = $2123
W34SEL      = $2124
WOBJSEL     = $2125
WH0         = $2126
WH1         = $2127
WH2         = $2128
WH3         = $2129
WBGLOG      = $212A
WOBJLOG     = $212B
TM          = $212C      ; main screen layer enable
TS          = $212D      ; sub screen layer enable
TMW         = $212E
TSW         = $212F
CGWSEL      = $2130
CGADSUB     = $2131
COLDATA     = $2132
SETINI      = $2133

;--- PPU registers (read) ----------------------------------------------------
MPYL        = $2134
MPYM        = $2135
MPYH        = $2136
SLHV        = $2137
OAMDATAREAD = $2138
VMDATALREAD = $2139
VMDATAHREAD = $213A
CGDATAREAD  = $213B
OPHCT       = $213C
OPVCT       = $213D
STAT77      = $213E
STAT78      = $213F

;--- APU communication ports -------------------------------------------------
APUIO0      = $2140
APUIO1      = $2141
APUIO2      = $2142
APUIO3      = $2143

;--- CPU / DMA registers -----------------------------------------------------
WMDATA      = $2180
WMADDL      = $2181
WMADDM      = $2182
WMADDH      = $2183

NMITIMEN    = $4200      ; NMI / IRQ / auto-joypad enable
WRIO        = $4201
WRMPYA      = $4202      ; 8x8 unsigned multiply
WRMPYB      = $4203
WRDIVL      = $4204      ; 16/8 unsigned divide
WRDIVH      = $4205
WRDIVB      = $4206
HTIME       = $4207
HTIMEH      = $4208
VTIME       = $4209
VTIMEH      = $420A
MDMAEN      = $420B      ; general purpose DMA enable
HDMAEN      = $420C
MEMSEL      = $420D      ; 1 = FastROM

RDNMI       = $4210      ; read to acknowledge NMI
TIMEUP      = $4211
HVBJOY      = $4212
RDIO        = $4213
RDDIVL      = $4214
RDDIVH      = $4215
RDMPYL      = $4216
RDMPYH      = $4217
JOY1L       = $4218      ; auto-read joypad 1
JOY1H       = $4219
JOY2L       = $421A
JOY2H       = $421B

;--- DMA channel 0 registers (channel n is at $43n0) -------------------------
DMAP0       = $4300      ; transfer mode / direction
BBAD0       = $4301      ; B-bus destination ($21xx low byte)
A1T0L       = $4302      ; A-bus source address
A1T0H       = $4303
A1B0        = $4304      ; A-bus source bank
DAS0L       = $4305      ; transfer size
DAS0H       = $4306
DAS0B       = $4307

;--- Joypad bit masks (16-bit value from JOY1L/JOY1H) ------------------------
PAD_B       = $8000
PAD_Y       = $4000
PAD_SELECT  = $2000
PAD_START   = $1000
PAD_UP      = $0800
PAD_DOWN    = $0400
PAD_LEFT    = $0200
PAD_RIGHT   = $0100
PAD_A       = $0080
PAD_X       = $0040
PAD_L       = $0020
PAD_R       = $0010

.endif ; SNES_INC
