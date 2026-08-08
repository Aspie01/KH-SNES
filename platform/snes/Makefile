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

# Map data, dialogue and the asset pipeline are shared with the other platform
# target, so they live at the repository root rather than in here.
ROOT    := ../..
TOOLS   := $(ROOT)/tools
ASSETS  := $(ROOT)/assets

TARGET  := kh.sfc
CFG     := kh.cfg
BUILD   := build

SRCS    := src/main.s src/nmi.s src/pad.s src/grid.s src/oam.s \
           src/world.s src/hud.s src/text.s src/dive.s src/island.s \
           src/night.s src/town.s src/ram.s src/header.s src/gfxdata.s
OBJS    := $(patsubst src/%.s,$(BUILD)/%.o,$(SRCS))

# --bin-include-dir, not -I: ca65 searches a separate path for .incbin, which is
# what lets gfxdata.s keep saying `assets/gen/...` while the shared asset tree
# lives at the repository root rather than in here.
ASFLAGS := --cpu 65816 -I src --bin-include-dir $(ROOT) -g

GEN     := $(ASSETS)/gen/bgchr.bin $(ASSETS)/gen/bg1map.bin $(ASSETS)/gen/collmap.bin \
           $(ASSETS)/gen/bgpal.bin $(ASSETS)/gen/objchr.bin $(ASSETS)/gen/objpal.bin \
           $(ASSETS)/gen/sorachr.bin $(ASSETS)/gen/hudchr.bin \
           $(ASSETS)/gen/hudpal.bin $(ASSETS)/gen/divechr.bin \
           $(ASSETS)/gen/divemap.bin $(ASSETS)/gen/divecoll.bin \
           $(ASSETS)/gen/divepal.bin $(ASSETS)/gen/dive2chr.bin \
           $(ASSETS)/gen/dive2map.bin $(ASSETS)/gen/dive2coll.bin \
           $(ASSETS)/gen/dive3chr.bin $(ASSETS)/gen/dive3map.bin \
           $(ASSETS)/gen/dive3coll.bin $(ASSETS)/gen/obj2chr.bin \
           $(ASSETS)/gen/heightmap.bin \
           $(ASSETS)/gen/islepal.bin $(ASSETS)/gen/nightpal.bin \
           $(ASSETS)/gen/nightobjpal.bin $(ASSETS)/gen/fragchr.bin \
           $(ASSETS)/gen/fragmap.bin $(ASSETS)/gen/fragcoll.bin \
           $(ASSETS)/gen/fragheight.bin $(ASSETS)/gen/townpal.bin \
           $(ASSETS)/gen/townobjpal.bin $(ASSETS)/gen/objtownchr.bin \
           $(ASSETS)/gen/town1chr.bin $(ASSETS)/gen/town1map.bin \
           $(ASSETS)/gen/town1coll.bin $(ASSETS)/gen/town1height.bin \
           $(ASSETS)/gen/town2chr.bin $(ASSETS)/gen/town2map.bin \
           $(ASSETS)/gen/town2coll.bin $(ASSETS)/gen/town2height.bin \
           $(ASSETS)/gen/town3chr.bin $(ASSETS)/gen/town3map.bin \
           $(ASSETS)/gen/town3coll.bin $(ASSETS)/gen/town3height.bin

ASSET_SRC := $(TOOLS)/build_assets.py $(TOOLS)/pixel.py $(ASSETS)/island.txt \
             $(ASSETS)/fragment.txt $(ASSETS)/town1.txt $(ASSETS)/town2.txt \
             $(ASSETS)/town3.txt

.PHONY: all assets check run clean

all: check $(TARGET)

# Both of these catch a class of mistake that is expensive to find by playing:
# a spawn point stranded by a map edit, and an immediate operand assembled at a
# register width the CPU does not have when it arrives.
check:
	$(PYTHON) $(TOOLS)/check_modes.py
	$(PYTHON) $(TOOLS)/check_map.py

assets: $(GEN)

$(GEN) &: $(ASSET_SRC)
	$(PYTHON) $(TOOLS)/build_assets.py

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
	$(PYTHON) $(TOOLS)/fixrom.py $@

run: $(TARGET)
	/usr/games/mednafen $(TARGET)

clean:
	rm -rf $(BUILD) $(TARGET) $(ASSETS)/gen
