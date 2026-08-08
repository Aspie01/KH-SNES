;=============================================================================
; ram.inc -- declarations for everything defined in ram.s
;=============================================================================
.ifndef RAM_INC
RAM_INC = 1

.include "game.inc"

;--- direct page scratch -----------------------------------------------------
.globalzp tmp0, tmp1, tmp2, tmp3, tmp4, tmp5, tmp6, tmp7, tmp8, tmp9
.globalzp ptr0, ptr1

;--- input -------------------------------------------------------------------
.globalzp padHeld, padPrev, padPressed

;--- frame / interrupt handshake ---------------------------------------------
.globalzp frameCount, vblankFlag, oamDirty

;--- camera ------------------------------------------------------------------
; camX/camY are the world pixel at the top-left of the screen.  bgHOfs/bgVOfs
; are the values the NMI pushes to the scroll registers -- precomputed on the
; main thread so the interrupt never has to touch shared scratch.
;
; camLo/camHi bound the scroll, and the scene sets them: the island wants the
; whole 512x256, while a Station of Awakening is smaller than the screen and
; wants the camera pinned so the void around it never comes into view.  Equal
; bounds mean a fixed camera.
.globalzp camX, camY, bgHOfs, bgVOfs
.globalzp camLoX, camHiX, camLoY, camHiY

;--- Sora sprite streaming request (consumed by the NMI handler) -------------
.globalzp streamPend, streamSrc, streamBank

;--- gameplay ----------------------------------------------------------------
.globalzp playerIdx, soraFrameCur, hitStopTimer, shakeTimer
.globalzp hudDirty, rngState, curActor
; Which sprite base a Shadow is drawn from.  The Dive has a palette to itself;
; the night shares OBJ palette 1 with Riku and Kairi and uses a second cut of
; the same art, so the base is per-scene rather than a constant.
.globalzp heartTile

;--- dialogue ----------------------------------------------------------------
.globalzp txtState, txtMode, txtPtr, txtCol, txtRow, txtDelay, txtDirty
.globalzp txtChoice, txtResult, txtHold

;--- scene / script ----------------------------------------------------------
.globalzp sceneId, diveStage, weaponTaken, weaponGiven, scriptWait
.globalzp pendActor, pendWeapon, collPtr, heightPtr, stepZ
.globalzp screenBright, mosaicAmt, shatterTimer, bossHP, shakeX
.globalzp cgwselVal, cgadsubVal
.globalzp fadeTimer, coldataAmt, deadFlag, fallTimer
.globalzp questState, questDay, dayTimer, pendTalk
.globalzp raceLeg, rikuWp, raceWon, raftName
.globalzp itemCount

;--- the night the island falls ----------------------------------------------
.globalzp nightStage, nightTimer, spawnTimer, flashTimer, flashWait
.globalzp keyGot, saidNoUse

;--- Traverse Town -----------------------------------------------------------
.globalzp townStage, townTimer, townKills, doorTimer, doorTo
.globalzp lastTileI, lastTileJ

;--- buffers (BSS) -----------------------------------------------------------
.global oamBuf, oamHigh, sortIdx, sortCount, hudRow, txtBuf

;--- actor arrays (structure-of-arrays; index with X) ------------------------
.global actType, actX, actY, actVX, actVY
.global actDir, actAnim, actAnimT, actState, actTimer
.global actHP, actFlags, actTile, actPal, actHitT, actZ

.endif ; RAM_INC
