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
; BG1 gets 512 characters: the stained-glass platform needs far more unique
; tiles than the island's terrain, which folds down to a few dozen.
VRAM_BG1_CHR = $0000            ; 512 tiles, 4bpp -> $0000-$1FFF
VRAM_BG3_CHR = $2000            ; 128 tiles, 2bpp -> $2000-$23FF
VRAM_BG1_MAP = $2400            ; 64x32 tilemap  -> $2400-$2BFF
VRAM_BG3_MAP = $2C00            ; 32x32 tilemap  -> $2C00-$2FFF
VRAM_OBJ_CHR = $4000            ; 256 tiles, 4bpp -> $4000-$4FFF

; Register values derived from the layout above.
BG1SC_VAL    = $25              ; base $2400/$400 = 9, size 1 (64x32)
BG3SC_VAL    = $2C              ; base $2C00/$400 = 11, size 0 (32x32)
BG12NBA_VAL  = $00              ; BG1 chars at $0000
BG34NBA_VAL  = $02              ; BG3 chars at $2000
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
TILE_PEDESTAL = $40             ; 32x32, Station of Awakening
TILE_SWORD   = $44              ; 32x32 Dream Sword
TILE_SHIELD  = $48              ; 32x32 Dream Shield
TILE_STAFF   = $4C              ; 32x32 Dream Rod
; Darkside is 64x64 -- four 32x32 blocks at $80/$84 over $C0/$C4, filling the
; lower-left quadrant of the sprite page.
TILE_DARKSIDE = $80
TILE_ORB     = $88              ; 16x16 dark orb, 2 frames
TILE_HEART0  = $C8              ; 16x16 Shadow Heartless, 4 frames
TILE_HEART1  = $CA
TILE_HEART2  = $CC
TILE_HEART3  = $CE
TILE_SHADOW  = $E8              ; 16x16 ground shadow
TILE_SLASH0  = $EA              ; 16x16 keyblade arc
TILE_SLASH1  = $EC
TILE_ROCK    = $EE              ; 16x16

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
PAL_OBJ_DIVE   = 5

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
ACT_PEDESTAL = 7                ; Station of Awakening dais
ACT_SWORD    = 8                ; the three dream weapons hovering above them
ACT_SHIELD   = 9
ACT_STAFF    = 10
ACT_DARKSIDE = 11               ; the boss that ends the Dive
ACT_ORB      = 12               ; dark orb fired from its chest

; Scenes
SCENE_DIVE   = 0                ; Station of Awakening
SCENE_DIVE2  = 1                ; the second Station of Awakening
SCENE_ISLAND = 2                ; Destiny Islands

; Which dream weapon was taken / surrendered
WEAPON_NONE  = 0
WEAPON_SWORD = 1
WEAPON_SHIELD = 2
WEAPON_STAFF = 3

; Dive script progression
DIVE_INTRO   = 0                ; the voice speaks
DIVE_PICK    = 1                ; choose a power to take
DIVE_DROP    = 2                ; choose a power to give up
DIVE_SHATTER = 3                ; the platform breaks up underfoot
DIVE_S2_INTRO = 4               ; landed on the second station
DIVE_S2_FIGHT = 5               ; the first Shadows
DIVE_BOSS    = 6                ; Darkside rises
DIVE_DONE    = 7

; Boss tuning
DS_MAX_HP    = 36
DS_SLAM_WIND = 44               ; frames of telegraph before a fist comes down
DS_SLAM_HOLD = 22               ; fist on the ground, vulnerable
DS_REST      = 56
DS_ORB_WIND  = 40               ; the chest gathers before it fires
DS_ORB_REST  = 30
ORB_LIFE     = 110              ; frames before an orb burns out
ORB_SPEED    = 40               ; Q12.4 along the eight facings

; Boss states
DSS_REST     = 0
DSS_SLAM_UP  = 1
DSS_SLAM_HIT = 2
DSS_ORB_UP   = 3
DSS_ORB_FIRE = 4
DS_HURT_X    = 480              ; Q12.4, generous: the body is 64 px wide
DS_HURT_Y    = 400
SHATTER_LEN  = 96               ; frames the platform takes to come apart

; actFlags bits
AF_LARGE     = $01              ; 32x32 sprite (otherwise 16x16)
AF_SHADOW    = $02              ; drop a ground shadow
AF_SOLID     = $04              ; blocks movement
AF_HFLIP     = $08              ; mirror horizontally when drawn
AF_TALK      = $10              ; can be interacted with by pressing A
AF_HUGE      = $20              ; 64x64, emitted as four sprites

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
