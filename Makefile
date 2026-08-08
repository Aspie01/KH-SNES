# Kingdom Hearts SNES demake -- build
#
#   make            check, then build kh.sfc
#   make assets     regenerate art/map binaries only
#   make check      static checks only
#   make run        build and launch in mednafen
#   make clean      remove build output

AS      := ca65
LD      := ld65
PYTHON  := python3

TARGET  := kh.sfc
CFG     := kh.cfg
BUILD   := build

SRCS    := src/main.s src/nmi.s src/pad.s src/grid.s src/oam.s \
           src/world.s src/hud.s src/text.s src/dive.s src/island.s \
           src/night.s src/town.s src/ram.s src/header.s src/gfxdata.s
OBJS    := $(patsubst src/%.s,$(BUILD)/%.o,$(SRCS))

ASFLAGS := --cpu 65816 -I src -g

GEN     := assets/gen/bgchr.bin assets/gen/bg1map.bin assets/gen/collmap.bin \
           assets/gen/bgpal.bin assets/gen/objchr.bin assets/gen/objpal.bin \
           assets/gen/sorachr.bin assets/gen/hudchr.bin \
           assets/gen/hudpal.bin assets/gen/divechr.bin \
           assets/gen/divemap.bin assets/gen/divecoll.bin \
           assets/gen/divepal.bin assets/gen/dive2chr.bin \
           assets/gen/dive2map.bin assets/gen/dive2coll.bin \
           assets/gen/dive3chr.bin assets/gen/dive3map.bin \
           assets/gen/dive3coll.bin assets/gen/obj2chr.bin \
           assets/gen/heightmap.bin \
           assets/gen/islepal.bin assets/gen/nightpal.bin \
           assets/gen/nightobjpal.bin assets/gen/fragchr.bin \
           assets/gen/fragmap.bin assets/gen/fragcoll.bin \
           assets/gen/fragheight.bin assets/gen/townpal.bin \
           assets/gen/townobjpal.bin assets/gen/objtownchr.bin \
           assets/gen/town1chr.bin assets/gen/town1map.bin \
           assets/gen/town1coll.bin assets/gen/town1height.bin \
           assets/gen/town2chr.bin assets/gen/town2map.bin \
           assets/gen/town2coll.bin assets/gen/town2height.bin \
           assets/gen/town3chr.bin assets/gen/town3map.bin \
           assets/gen/town3coll.bin assets/gen/town3height.bin

ASSET_SRC := tools/build_assets.py tools/pixel.py assets/island.txt \
             assets/fragment.txt assets/town1.txt assets/town2.txt \
             assets/town3.txt

.PHONY: all assets check run clean

all: check $(TARGET)

# Both of these catch a class of mistake that is expensive to find by playing:
# a spawn point stranded by a map edit, and an immediate operand assembled at a
# register width the CPU does not have when it arrives.
check:
	$(PYTHON) tools/check_modes.py
	$(PYTHON) tools/check_map.py

assets: $(GEN)

$(GEN) &: $(ASSET_SRC)
	$(PYTHON) tools/build_assets.py

$(BUILD):
	mkdir -p $(BUILD)

# Every module includes the shared headers, so a constant change has to force a
# reassemble -- otherwise editing game.inc silently leaves stale objects linked.
INCS    := $(wildcard src/*.inc)

$(BUILD)/%.o: src/%.s $(INCS) | $(BUILD)
	$(AS) $(ASFLAGS) -o $@ $<

# The data module pulls in every generated binary, so it has to wait for them.
$(BUILD)/gfxdata.o: src/gfxdata.s $(GEN) $(INCS) | $(BUILD)
	$(AS) $(ASFLAGS) -o $@ $<

$(TARGET): $(OBJS) $(CFG)
	$(LD) -C $(CFG) -o $@ -m $(BUILD)/kh.map --dbgfile $(BUILD)/kh.dbg $(OBJS)
	$(PYTHON) tools/fixrom.py $@

run: $(TARGET)
	/usr/games/mednafen $(TARGET)

clean:
	rm -rf $(BUILD) $(TARGET) assets/gen
