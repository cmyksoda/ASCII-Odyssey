DEVKITPRO ?= /opt/devkitpro
DEVKITPPC ?= $(DEVKITPRO)/devkitPPC
export DEVKITPRO DEVKITPPC
PC_GOALS := pc test render
ifeq ($(filter $(PC_GOALS),$(MAKECMDGOALS)),)
include $(DEVKITPPC)/wii_rules
endif
.DEFAULT_GOAL := all

APP := asciiodyssey
VERSION := 0.2.0
CC := $(DEVKITPPC)/bin/powerpc-eabi-gcc
# fp-contract off keeps every float op IEEE-rounded, so seeds match the PC tool exactly.
COMMON_CFLAGS := -O2 -std=gnu11 -Wall -Wextra -Werror -ffp-contract=off -fno-strict-aliasing -MMD -MP
CFLAGS := -g $(COMMON_CFLAGS) $(MACHDEP) -I$(LIBOGC_INC) -Ibuild -DAPP_VERSION='"$(VERSION)"'
LIBS := -lfat -lwiikeyboard -lwiiuse -lbte -lasnd -logc -lm

CORE := $(wildcard source/core/*.c)
GENERATED := build/gen/font_faces.c build/gen/figlet_fonts.c
FLF := $(wildcard vendor/figlet/*.flf)
WII_OBJECTS := $(patsubst source/%.c,build/wii/%.o,$(CORE) $(wildcard source/wii/*.c)) \
	build/wii/gen/font_faces.o build/wii/gen/figlet_fonts.o

.PHONY: all pc test render clean package
all: dist/apps/$(APP)/boot.dol

build/gen/font_faces.c: tools/gen_font.py $(wildcard vendor/fonts/terminus/*.bdf)
	@mkdir -p $(@D)
	python3 tools/gen_font.py $@

build/gen/figlet_fonts.c: tools/embed.py $(FLF)
	@mkdir -p $(@D)
	python3 tools/embed.py $@ $(foreach f,$(FLF),flf_$(basename $(notdir $(f)))=$(f))

build/wii/%.o: source/%.c
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -c $< -o $@

build/wii/gen/%.o: build/gen/%.c
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -Isource/core -c $< -o $@

build/wii/$(APP).elf: $(WII_OBJECTS)
	$(CC) $^ $(MACHDEP) -L$(LIBOGC_LIB) $(LIBS) -Wl,-Map,build/wii/$(APP).map -o $@

dist/apps/$(APP)/boot.dol: build/wii/$(APP).elf hbc/meta.xml hbc/icon.png
	@mkdir -p $(@D)
	$(DEVKITPRO)/tools/bin/elf2dol $< $@
	sed 's/@VERSION@/$(VERSION)/' hbc/meta.xml > dist/apps/$(APP)/meta.xml
	cp hbc/icon.png dist/apps/$(APP)/icon.png

# The PC side: the same core, built natively, for looking at frames and rendering pieces.
HOST_CC ?= cc
HOST_CFLAGS := -g $(COMMON_CFLAGS) -Ibuild
PC_OBJECTS := $(patsubst source/%.c,build/pc/%.o,$(CORE) $(wildcard source/pc/*.c)) \
	build/pc/gen/font_faces.o build/pc/gen/figlet_fonts.o

build/pc/%.o: source/%.c
	@mkdir -p $(@D)
	$(HOST_CC) $(HOST_CFLAGS) -c $< -o $@

build/pc/gen/%.o: build/gen/%.c
	@mkdir -p $(@D)
	$(HOST_CC) $(HOST_CFLAGS) -Isource/core -c $< -o $@

build/odyssey-render: $(PC_OBJECTS)
	$(HOST_CC) $^ -lz -lm -o $@

pc render: build/odyssey-render

TEST_SOURCES := $(wildcard tests/*_test.c)
TEST_CORE := $(filter-out build/pc/pc/%,$(PC_OBJECTS))
build/tests/%: tests/%.c $(TEST_CORE)
	@mkdir -p $(@D)
	$(HOST_CC) $(HOST_CFLAGS) -Isource $^ -lz -lm -o $@

test: $(patsubst tests/%.c,build/tests/%,$(TEST_SOURCES))
	@for t in $^; do echo "== $$t"; ./$$t || exit 1; done

package: all
	python3 tools/package.py $(APP) $(VERSION)

clean:
	rm -rf build dist

-include $(WII_OBJECTS:.o=.d) $(PC_OBJECTS:.o=.d)
