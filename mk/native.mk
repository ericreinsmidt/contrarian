# Native dev build (macOS/Linux host): make -f mk/native.mk
# Point it at a staging tree with CTR_ROOT / CTR_CARD.
BUILD := build-native
SRC := $(filter-out src/btpair.c,$(wildcard src/*.c))

PKGS := sdl2 SDL2_image
CFLAGS := -O1 -g -Wall -Wextra -Wno-unused-parameter -std=gnu11 -D_GNU_SOURCE \
          $(shell pkg-config --cflags $(PKGS))
LDLIBS := $(shell pkg-config --libs $(PKGS)) -lm -lz

$(BUILD)/contrarian: $(SRC) $(wildcard src/*.h)
	mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -o $@ $(SRC) $(LDLIBS)
