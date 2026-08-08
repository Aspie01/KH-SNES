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
VRAM_OBJ2_CHR = $5000           ; second 256-tile page -> $5000-$5FFF

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
TILE_STREAK  = $8C              ; 16x16 light mote, 2 frames
TILE_HEART0  = $C8              ; 16x16 Shadow Heartless, 4 frames
TILE_HEART1  = $CA
TILE_HEART2  = $CC
TILE_HEART3  = $CE
TILE_SHADOW  = $E8              ; 16x16 ground shadow
TILE_SLASH0  = $EA              ; 16x16 keyblade arc
TILE_SLASH1  = $EC
TILE_ROCK    = $EE              ; 16x16

; Second sprite page, reached through the OAM name bit (AF_PAGE1).  The first
; page is full, and the islanders would not fit beside the Dive's cast.
TILE_KAIRI   = $00              ; 32x32
TILE_RIKU    = $04
TILE_TIDUS   = $08
TILE_SELPHIE = $0C
TILE_WAKKA   = $40
TILE_DOOR    = $44              ; 32x32, the door in the Secret Place
TILE_LOG     = $48              ; 16x16 raft materials
TILE_CLOTH   = $4A
TILE_ROPE    = $4C
TILE_MUSH    = $4E              ; ...and what day two is after
TILE_FISH    = $68              ; two frames
TILE_COCONUT = $6C
TILE_EGG     = $6E
TILE_BOTTLE  = $80
TILE_FACES   = $84              ; 32x32 chalk drawings on the cave wall
TILE_SCRIBBLE = $88

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
; The islanders have to live below palette 4 or colour math would half-add
; them into the sea.  Nothing on Destiny Islands is a Heartless, so the island
; loads their colours over slot 1 and takes it for the day.
PAL_OBJ_ISLE   = 1

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
ACT_MOTE     = 13               ; rising light during the fall
ACT_ORB      = 12               ; dark orb fired from its chest
; Destiny Islands.  The five islanders are consecutive so one range check
; covers "is this somebody I can talk to".
ACT_KAIRI    = 14
ACT_RIKU     = 15
ACT_TIDUS    = 16
ACT_SELPHIE  = 17
ACT_WAKKA    = 18
; Everything that is taken by walking into it, in one range.  Their order
; also indexes itemCount, so a pickup needs no lookup table to tally itself.
ACT_LOG      = 19
ACT_CLOTH    = 20
ACT_ROPE     = 21
ACT_MUSH     = 22
ACT_COCONUT  = 23
ACT_EGG      = 24
ACT_BOTTLE   = 25
; ...and the two that answer to the keyblade instead.
ACT_FISH     = 26
ACT_PALMC    = 27               ; a palm still carrying its coconuts
; What is on the wall of the Secret Place, in one range so a single scan
; covers "is this something Sora can look at".
ACT_DOOR     = 28
ACT_FACES    = 29               ; the two of them, drawn facing each other
ACT_SCRIBBLE = 30               ; years of smaller drawings

; itemCount slots, which are (actor type - ACT_LOG)
IT_LOG       = 0
IT_CLOTH     = 1
IT_ROPE      = 2
IT_MUSH      = 3
IT_NUT       = 4
IT_EGG       = 5
IT_WATER     = 6
IT_FISH      = 7

; Scenes
SCENE_DIVE   = 0                ; Station of Awakening
SCENE_DIVE2  = 1                ; the second Station of Awakening
SCENE_DIVE3  = 2                ; the third, where Darkside rises
SCENE_ISLAND = 3                ; Destiny Islands

; Which dream weapon was taken / surrendered
WEAPON_NONE  = 0
WEAPON_SWORD = 1
WEAPON_SHIELD = 2
WEAPON_STAFF = 3

; Dive script progression.  Three Stations of Awakening: the weapon choice,
; the first Shadows, then the boss.
DIVE_INTRO    = 0               ; the voice speaks
DIVE_PICK     = 1               ; choose a power to take
DIVE_DROP     = 2               ; choose a power to give up
DIVE_SHATTER  = 3               ; station one comes apart
DIVE_S2_INTRO = 4
DIVE_S2_FIGHT = 5               ; the first Shadows
DIVE_SHATTER2 = 6               ; station two comes apart
DIVE_S3_INTRO = 7
DIVE_BOSS     = 8               ; Darkside
DIVE_DONE     = 9               ; beaten; victory line showing
DIVE_FALL     = 10              ; falling away from the platform
DIVE_FADE     = 11              ; the light resolves into Destiny Islands
DIVE_ARRIVED  = 12

FADE_LEN     = 64               ; frames; the scene swaps at the halfway point
FALL_LEN     = 170              ; frames of descent before the light takes over
MOTE_LIFE    = 40               ; frames one speck of light lasts
MOTE_RISE    = 96               ; Q12.4 per frame, upward -- 6 px a frame

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
DSS_SWEEP_UP = 5                ; close-range arm sweep
DSS_SWEEP_HIT = 6

DS_SWEEP_WIND = 26              ; shorter telegraph than the fist: it is the
DS_SWEEP_HOLD = 16              ; punish for standing underneath
SWEEP_X      = 700              ; Q12.4, compared after un-squashing X
SWEEP_Y      = 420
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
AF_PAGE1     = $40              ; tile lives on the second sprite page
; Something hung on a wall rather than standing on the ground: it keeps a
; height of zero wherever it is placed, so it lands on the face of the block
; behind it instead of on top.
AF_FLAT      = $80

; actState values for Sora
ST_IDLE      = 0
ST_WALK      = 1
ST_ATTACK    = 2
ST_HURT      = 3
ST_DEAD      = 4                ; out of HP; no control until the retry
ST_FALL      = 5                ; carried by the scene, not the player

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

;-----------------------------------------------------------------------------
; Destiny Islands, day one: Kairi's list of raft materials
;-----------------------------------------------------------------------------
Q_IDLE       = 0                ; Kairi has not asked yet
Q_ACTIVE     = 1                ; the list is open
Q_DONE       = 2                ; everything handed in
Q_NIGHT      = 3                ; the day is fading out
Q_MORNING    = 4                ; ...and the next one is fading in
; Day two ends with the race that decides who names the raft.
Q_RACE_SET   = 5                ; on your marks, and Kairi counting down
Q_RACE_RUN   = 6
Q_RACE_OVER  = 7                ; somebody has crossed the line
Q_NAMING     = 8                ; picking a name
Q_NAMED      = 9

COUNT_LEN    = 192              ; frames of countdown, 64 to a number
; Riku runs the course at a fixed pace.  He is a little slower than a clean
; line, so the race is winnable, and quick enough to punish wandering.
RIKU_VX      = 30               ; Q12.4 per frame
RIKU_VY      = 15
RIKU_NEAR    = 40               ; Q12.4; how close counts as reaching a marker
TAG_X        = 560              ; touching the paopu tree, Q12.4
TAG_Y        = 360
LINE_X       = 560              ; ...and the finish line back at Kairi
LINE_Y       = 360

RACE_WPS     = 18               ; markers in the course

; The start line by Kairi, the paopu tree that has to be tagged, and the
; finish -- all Q12.4 world pixels.
START_SORA_X = (16 * (8 - 11) + ORIGIN_X + 16) * 16
START_SORA_Y = (8 * (8 + 11) + 8) * 16
START_RIKU_X = (16 * (9 - 11) + ORIGIN_X + 16) * 16
START_RIKU_Y = (8 * (9 + 11) + 8) * 16
PAOPU_X      = (16 * (13 - 1) + ORIGIN_X + 16) * 16
PAOPU_Y      = (8 * (13 + 1) + 8) * 16
FINISH_X     = (16 * (8 - 10) + ORIGIN_X + 16) * 16
FINISH_Y     = (8 * (8 + 10) + 8) * 16

RAFT_HIGHWIND  = 1
RAFT_EXCALIBUR = 2
RAFT_RAGNAROK  = 3

NEED_LOGS    = 2
NEED_CLOTH   = 1
NEED_ROPE    = 1
NEED_FISH    = 3
NEED_MUSH    = 3
NEED_NUT     = 2
NEED_EGG     = 1
NEED_WATER   = 1

DAY_FADE     = 30               ; frames each half of the day change takes
FISH_SWIM    = 16               ; Q12.4 per frame; fish drift, they do not race

TALK_X       = 320              ; interaction range, Q12.4 (un-squashed X)
TALK_Y       = 260
PICK_X       = 260              ; a pickup is taken by walking into it
PICK_Y       = 190
; The keyblade reaches further than an arm: fish are out in the shallows and
; coconuts are at the top of a palm.
SWING_X      = 480
SWING_Y      = 320
; Looking at something on the cave wall is more forgiving than talking: the
; chamber is small and the walls are behind you as often as not.
PROP_X       = 400
PROP_Y       = 300

MAX_STEP     = 1                ; ground-height difference a step may cross

ATTACK_FRAMES = 18              ; length of a swing
HURT_FRAMES   = 20
DEATH_FRAMES  = 50              ; the screen dims over these before GAME OVER
SORA_MAX_HP   = 20
HEART_MAX_HP  = 3

.endif ; GAME_INC
