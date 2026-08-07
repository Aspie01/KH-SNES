# Kingdom Hearts SNES demake -- build
#
#   make            build kh.sfc
#   make assets     regenerate art/map binaries only
#   make run        build and launch in mednafen
#   make clean      remove build output

AS      := ca65
LD      := ld65
PYTHON  := python3

TARGET  := kh.sfc
CFG     := kh.cfg
BUILD   := build

SRCS    := src/main.s src/nmi.s src/pad.s src/iso.s src/oam.s \
           src/world.s src/hud.s src/text.s src/dive.s \
           src/ram.s src/header.s src/gfxdata.s
OBJS    := $(patsubst src/%.s,$(BUILD)/%.o,$(SRCS))

ASFLAGS := --cpu 65816 -I src -g

GEN     := assets/gen/bgchr.bin assets/gen/bg1map.bin assets/gen/collmap.bin \
           assets/gen/bgpal.bin assets/gen/objchr.bin assets/gen/objpal.bin \
           assets/gen/sorachr.bin assets/gen/hudchr.bin \
           assets/gen/hudpal.bin assets/gen/divechr.bin \
           assets/gen/divemap.bin assets/gen/divecoll.bin \
           assets/gen/divepal.bin

ASSET_SRC := tools/build_assets.py tools/pixel.py assets/island.txt

.PHONY: all assets run clean

all: $(TARGET)

assets: $(GEN)

$(GEN) &: $(ASSET_SRC)
	$(PYTHON) tools/build_assets.py

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/%.o: src/%.s | $(BUILD)
	$(AS) $(ASFLAGS) -o $@ $<

# The data module pulls in every generated binary, so it has to wait for them.
$(BUILD)/gfxdata.o: src/gfxdata.s $(GEN) | $(BUILD)
	$(AS) $(ASFLAGS) -o $@ $<

$(TARGET): $(OBJS) $(CFG)
	$(LD) -C $(CFG) -o $@ -m $(BUILD)/kh.map --dbgfile $(BUILD)/kh.dbg $(OBJS)
	$(PYTHON) tools/fixrom.py $@

run: $(TARGET)
	/usr/games/mednafen $(TARGET)

clean:
	rm -rf $(BUILD) $(TARGET) assets/gen
