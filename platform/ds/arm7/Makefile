# The ARM7: the stock core, and why it is built rather than borrowed.
#
# devkitPro ships a prebuilt default ARM7 and most homebrew uses it.  This
# builds one, for a reason that is a sentence long: the port needs the
# touchscreen, arm7/source/main.c is the file that samples it, and a build whose
# ARM7 is a binary nobody in this tree can read is a build where "the pen does
# nothing" has no file to look in.  It is sixty lines of the standard template
# and it costs a second to compile.
#
# THE COMPILER IS A DIFFERENT ONE.  The ARM7 is an ARM7TDMI: ARMv4T, no ARMv5
# instructions, its own specs file and its own library.  Handing it the ARM9's
# flags produces an ELF that links and then executes an instruction the core
# does not have, which is a black screen with no diagnostic.

.SUFFIXES:

ifeq ($(strip $(DEVKITARM)),)
$(error DEVKITARM is not set; see platform/ds/Makefile)
endif

include $(DEVKITARM)/ds_rules

TARGET   := $(notdir $(CURDIR))
BUILD    := build
SOURCES  := source
INCLUDES := source

ARCH     := -mthumb -mthumb-interwork

CFLAGS   := -g -Wall -O2 \
            -mcpu=arm7tdmi -mtune=arm7tdmi \
            -fomit-frame-pointer -ffast-math \
            $(ARCH) $(INCLUDE) -DARM7
CXXFLAGS := $(CFLAGS) -fno-rtti -fno-exceptions
ASFLAGS  := -g $(ARCH)
LDFLAGS   = -specs=ds_arm7.specs -g $(ARCH) -Wl,-Map,$(notdir $*.map)

LIBS     := -lnds7
LIBDIRS  := $(LIBNDS)

#---------------------------------------------------------------------------
ifneq ($(BUILD),$(notdir $(CURDIR)))
#---------------------------------------------------------------------------

export OUTPUT  := $(CURDIR)/$(TARGET)
export VPATH   := $(foreach dir,$(SOURCES),$(CURDIR)/$(dir))
export DEPSDIR := $(CURDIR)/$(BUILD)

CFILES   := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
CPPFILES := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cpp)))
SFILES   := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.s)))

export LD := $(CC)

export OFILES := $(CPPFILES:.cpp=.o) $(CFILES:.c=.o) $(SFILES:.s=.o)

export INCLUDE  := $(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) \
                   $(foreach dir,$(LIBDIRS),-I$(dir)/include) \
                   -I$(CURDIR)/$(BUILD)
export LIBPATHS := $(foreach dir,$(LIBDIRS),-L$(dir)/lib)

.PHONY: $(BUILD) clean

$(BUILD):
	@[ -d $@ ] || mkdir -p $@
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

clean:
	@rm -fr $(BUILD) $(OUTPUT).elf $(OUTPUT).map

#---------------------------------------------------------------------------
else
#---------------------------------------------------------------------------

DEPENDS := $(OFILES:.o=.d)

$(OUTPUT).elf : $(OFILES)

-include $(DEPENDS)

#---------------------------------------------------------------------------
endif
#---------------------------------------------------------------------------
