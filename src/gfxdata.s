;=============================================================================
; gfxdata.s -- links the generated art and map data into the ROM
;
; Everything here is produced by tools/build_assets.py.  Sora's sheet gets a
; bank to itself so the streaming pointer (a 16-bit offset plus a fixed bank)
; can address any frame without a carry into the bank byte.
;=============================================================================
.p816

.export bgChr, bgChrEnd, hudChr, hudChrEnd
.export objChr, objChrEnd
.export bgPal, objPal, hudPal, bg1Map, collMap, soraChr
.export diveChr, diveChrEnd, diveMap, diveColl, divePal
.export dive2Chr, dive2ChrEnd, dive2Map, dive2Coll
.export dive3Chr, dive3ChrEnd, dive3Map, dive3Coll

.segment "GFXBG"
bgChr:      .incbin "assets/gen/bgchr.bin"
bgChrEnd:
hudChr:     .incbin "assets/gen/hudchr.bin"
hudChrEnd:

.segment "GFXOBJ"
objChr:     .incbin "assets/gen/objchr.bin"
objChrEnd:

.segment "MAPDATA"
bg1Map:     .incbin "assets/gen/bg1map.bin"
collMap:    .incbin "assets/gen/collmap.bin"
bgPal:      .incbin "assets/gen/bgpal.bin"
objPal:     .incbin "assets/gen/objpal.bin"
hudPal:     .incbin "assets/gen/hudpal.bin"

.segment "GFXSORA"
soraChr:    .incbin "assets/gen/sorachr.bin"

.segment "GFXDIVE"
diveChr:    .incbin "assets/gen/divechr.bin"
diveChrEnd:
diveMap:    .incbin "assets/gen/divemap.bin"
diveColl:   .incbin "assets/gen/divecoll.bin"
divePal:    .incbin "assets/gen/divepal.bin"

.segment "GFXDIVE2"
dive2Chr:   .incbin "assets/gen/dive2chr.bin"
dive2ChrEnd:
dive2Map:   .incbin "assets/gen/dive2map.bin"
dive2Coll:  .incbin "assets/gen/dive2coll.bin"

.segment "GFXDIVE3"
dive3Chr:   .incbin "assets/gen/dive3chr.bin"
dive3ChrEnd:
dive3Map:   .incbin "assets/gen/dive3map.bin"
dive3Coll:  .incbin "assets/gen/dive3coll.bin"
