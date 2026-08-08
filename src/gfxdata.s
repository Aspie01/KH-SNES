;=============================================================================
; gfxdata.s -- links the generated art and map data into the ROM
;
; Everything here is produced by tools/build_assets.py.  Sora's sheet gets a
; bank to itself so the streaming pointer (a 16-bit offset plus a fixed bank)
; can address any frame without a carry into the bank byte.
;=============================================================================
.p816
.include "game.inc"

.export bgChr, bgChrEnd, hudChr, hudChrEnd
.export objChr, objChrEnd, obj2Chr, obj2ChrEnd
.export bgPal, objPal, hudPal, islePal, nightPal, nightObjPal
.export townPal, townObjPal, objTownChr, objTownChrEnd
.export bg1Map, collMap, heightMap, flatHeights
.export soraChr
.export diveChr, diveChrEnd, diveMap, diveColl, divePal
.export dive2Chr, dive2ChrEnd, dive2Map, dive2Coll
.export dive3Chr, dive3ChrEnd, dive3Map, dive3Coll
.export fragChr, fragChrEnd, fragMap, fragColl, fragHeight
.export town1Chr, town1ChrEnd, town1Map, town1Coll, town1Height
.export town2Chr, town2ChrEnd, town2Map, town2Coll, town2Height
.export town3Chr, town3ChrEnd, town3Map, town3Coll, town3Height

.segment "GFXBG"
bgChr:      .incbin "assets/gen/bgchr.bin"
bgChrEnd:
hudChr:     .incbin "assets/gen/hudchr.bin"
hudChrEnd:

.segment "GFXOBJ"
objChr:     .incbin "assets/gen/objchr.bin"
objChrEnd:
obj2Chr:    .incbin "assets/gen/obj2chr.bin"
obj2ChrEnd:
; The same VRAM page, with Traverse Town's cast on it instead.
objTownChr: .incbin "assets/gen/objtownchr.bin"
objTownChrEnd:

.segment "MAPDATA"
bg1Map:     .incbin "assets/gen/bg1map.bin"
collMap:    .incbin "assets/gen/collmap.bin"
heightMap:  .incbin "assets/gen/heightmap.bin"
; Every other scene is one flat plane, so they all share this.
flatHeights: .res MAP_W * MAP_H, $00
bgPal:      .incbin "assets/gen/bgpal.bin"
objPal:     .incbin "assets/gen/objpal.bin"
hudPal:     .incbin "assets/gen/hudpal.bin"
islePal:    .incbin "assets/gen/islepal.bin"
; The island after dark is the same characters and the same tilemap with one
; different palette, so the night costs a hundred and sixty bytes of CGRAM
; data and nothing else.
nightPal:   .incbin "assets/gen/nightpal.bin"
nightObjPal: .incbin "assets/gen/nightobjpal.bin"
townPal:    .incbin "assets/gen/townpal.bin"
townObjPal: .incbin "assets/gen/townobjpal.bin"

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

.segment "GFXFRAG"
fragChr:    .incbin "assets/gen/fragchr.bin"
fragChrEnd:
fragMap:    .incbin "assets/gen/fragmap.bin"
fragColl:   .incbin "assets/gen/fragcoll.bin"
fragHeight: .incbin "assets/gen/fragheight.bin"

.segment "GFXTOWN"
town1Chr:   .incbin "assets/gen/town1chr.bin"
town1ChrEnd:
town1Map:   .incbin "assets/gen/town1map.bin"
town1Coll:  .incbin "assets/gen/town1coll.bin"
town1Height: .incbin "assets/gen/town1height.bin"
town2Chr:   .incbin "assets/gen/town2chr.bin"
town2ChrEnd:
town2Map:   .incbin "assets/gen/town2map.bin"
town2Coll:  .incbin "assets/gen/town2coll.bin"
town2Height: .incbin "assets/gen/town2height.bin"

.segment "GFXTOWN2"
town3Chr:   .incbin "assets/gen/town3chr.bin"
town3ChrEnd:
town3Map:   .incbin "assets/gen/town3map.bin"
town3Coll:  .incbin "assets/gen/town3coll.bin"
town3Height: .incbin "assets/gen/town3height.bin"
