;=============================================================================
; game.inc -- engine-wide constants
;=============================================================================
.ifndef GAME_INC
GAME_INC = 1

;-----------------------------------------------------------------------------
; Isometric world geometry
;
; The ground plane is built from 32x16 pixel diamonds.  That size is the whole
; reason this works on a SNES: a 32x16 diamond lattice steps by 16 pixels
; horizontally and 8 vertically, and both are multiples of 8 -- so every
; isometric tile lands exactly on the PPU's 8x8 character grid and the ground
; can live in an ordinary BG tilemap instead of costing sprites.
;
;   world_x = (i - j) * 16 + ORIGIN_X
;   world_y = (i + j) * 8
;
; A 16x16 grid spans 512x256 pixels, which is one 64x32 tilemap exactly.
;-----------------------------------------------------------------------------
MAP_W        = 16               ; isometric tiles along +i
MAP_H        = 16               ; isometric tiles along +j
ORIGIN_X     = 240              ; (MAP_H-1)*16, shifts world_x to 0..511

WORLD_W      = 512
WORLD_H      = 256

SCREEN_W     = 256
SCREEN_H     = 224

CAM_MAX_X    = WORLD_W - SCREEN_W       ; 256
CAM_MAX_Y    = WORLD_H - SCREEN_H       ; 32

;-----------------------------------------------------------------------------
; VRAM map (word addresses)
;-----------------------------------------------------------------------------
VRAM_BG1_CHR = $0000            ; 256 tiles, 4bpp -> $0000-$0FFF
VRAM_BG3_CHR = $1000            ; 128 tiles, 2bpp -> $1000-$13FF
VRAM_BG1_MAP = $1400            ; 64x32 tilemap  -> $1400-$1BFF
VRAM_BG3_MAP = $1C00            ; 32x32 tilemap  -> $1C00-$1FFF
VRAM_OBJ_CHR = $4000            ; 256 tiles, 4bpp -> $4000-$4FFF

; Register values derived from the layout above.
BG1SC_VAL    = $15              ; base $1400/$400 = 5, size 1 (64x32)
BG3SC_VAL    = $1C              ; base $1C00/$400 = 7, size 0 (32x32)
BG12NBA_VAL  = $00              ; BG1 chars at $0000
BG34NBA_VAL  = $01              ; BG3 chars at $1000
OBSEL_VAL    = $62              ; size mode 3 (16x16 / 32x32), base $4000
BGMODE_VAL   = $09              ; mode 1, BG3 priority high (HUD over everything)
TM_VAL       = $15              ; main screen: BG1 + BG3 + OBJ
TS_VAL       = $01              ; sub screen: BG1, as the colour-math operand
CGWSEL_VAL   = $02              ; take the second operand from the sub screen
CGADSUB_VAL  = $50              ; half-add, OBJ only -> 50% shadows

;-----------------------------------------------------------------------------
; Sprite tile allocation inside the OBJ page.
; The page is a 16x16 grid of tiles, so a 32x32 sprite occupies a 4x4 block and
; its tile index advances by 16 per row.
;-----------------------------------------------------------------------------
TILE_SORA    = $00              ; 32x32, streamed one frame at a time
TILE_PALM    = $04              ; 32x32, resident
TILE_ROCKBIG = $08              ; 32x32, resident
TILE_SHADOWBIG = $0C            ; 32x32 ground shadow
TILE_HEART0  = $40              ; 16x16 Shadow Heartless, 4 frames
TILE_HEART1  = $42
TILE_HEART2  = $44
TILE_HEART3  = $46
TILE_SHADOW  = $48              ; 16x16 ground shadow
TILE_SLASH0  = $4A              ; 16x16 keyblade arc
TILE_SLASH1  = $4C
TILE_ROCK    = $4E              ; 16x16

; Sora's streaming window: 4 tile rows of 4 tiles at VRAM_OBJ_CHR.
SORA_FRAME_BYTES = 512          ; 32x32 4bpp
SORA_ROW_BYTES   = 128          ; 4 tiles
SORA_ROW_STRIDE  = $100         ; 16 tiles = 256 words

;-----------------------------------------------------------------------------
; Palettes.  CGRAM 0-127 is BG, 128-255 is OBJ (8 palettes of 16 each).
;-----------------------------------------------------------------------------
PAL_OBJ_SORA   = 0              ; OBJ palette 0 -> CGRAM $80
PAL_OBJ_HEART  = 1
PAL_OBJ_SCENE  = 2
PAL_OBJ_FX     = 3
; Colour math on the OBJ layer only affects sprites using palettes 4-7, so
; the ground shadow lives up here to get real translucency.
PAL_OBJ_SHADOW = 4

;-----------------------------------------------------------------------------
; Actors
;-----------------------------------------------------------------------------
MAX_ACTORS   = 24

ACT_NONE     = 0
ACT_SORA     = 1
ACT_SHADOW   = 2                ; Shadow Heartless
ACT_PALM     = 3
ACT_ROCKBIG  = 4
ACT_ROCK     = 5
ACT_SLASH    = 6                ; transient keyblade arc

; actFlags bits
AF_LARGE     = $01              ; 32x32 sprite (otherwise 16x16)
AF_SHADOW    = $02              ; drop a ground shadow
AF_SOLID     = $04              ; blocks movement
AF_HFLIP     = $08              ; mirror horizontally when drawn

; actState values for Sora
ST_IDLE      = 0
ST_WALK      = 1
ST_ATTACK    = 2
ST_HURT      = 3

; Facing directions.  Only N, NE, E, SE, S are drawn; W, SW, NW reuse the
; eastern art mirrored, which is how the sprite budget stays sane.
DIR_S        = 0
DIR_SE       = 1
DIR_E        = 2
DIR_NE       = 3
DIR_N        = 4
DIR_NW       = 5
DIR_W        = 6
DIR_SW       = 7

; Movement speed, Q12.4 pixels per frame.  The ground plane is squashed 2:1,
; so horizontal steps are twice the vertical ones for a uniform apparent speed.
WALK_SPEED   = 24               ; 1.5 px/frame vertically

ATTACK_FRAMES = 18              ; length of a swing
HURT_FRAMES   = 20
SORA_MAX_HP   = 20
HEART_MAX_HP  = 3

.endif ; GAME_INC
