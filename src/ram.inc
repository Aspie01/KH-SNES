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
.globalzp camX, camY, bgHOfs, bgVOfs

;--- Sora sprite streaming request (consumed by the NMI handler) -------------
.globalzp streamPend, streamSrc, streamBank

;--- gameplay ----------------------------------------------------------------
.globalzp playerIdx, soraFrameCur, hitStopTimer, shakeTimer
.globalzp hudDirty, rngState, curActor

;--- dialogue ----------------------------------------------------------------
.globalzp txtState, txtMode, txtPtr, txtCol, txtRow, txtDelay, txtDirty
.globalzp txtChoice, txtResult, txtHold

;--- scene / script ----------------------------------------------------------
.globalzp sceneId, diveStage, weaponTaken, weaponGiven, scriptWait
.globalzp pendActor, pendWeapon, collPtr
.globalzp screenBright, mosaicAmt, shatterTimer, bossHP, shakeX
.globalzp fadeTimer, coldataAmt, deadFlag, fallTimer

;--- buffers (BSS) -----------------------------------------------------------
.global oamBuf, oamHigh, sortIdx, sortCount, hudRow, txtBuf

;--- actor arrays (structure-of-arrays; index with X) ------------------------
.global actType, actX, actY, actVX, actVY
.global actDir, actAnim, actAnimT, actState, actTimer
.global actHP, actFlags, actTile, actPal, actHitT

.endif ; RAM_INC
