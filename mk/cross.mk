# Runs inside the tg5040 toolchain container (CC/SYSROOT come from its env).
BUILD := build
SRC := $(wildcard src/*.c)

CFLAGS := -O2 -mcpu=cortex-a53 -Wall -Wextra -Wno-unused-parameter -std=gnu11 \
          -I$(SYSROOT)/usr/include/SDL2 -D_GNU_SOURCE
LDLIBS := -lSDL2 -lSDL2_image -lm -ldl -lz

$(BUILD)/contrarian.elf: $(SRC) $(wildcard src/*.h)
	mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -o $@ $(SRC) $(LDLIBS)

$(BUILD)/setbright: tools/setbright.c
	mkdir -p $(BUILD)
	$(CC) -O2 -mcpu=cortex-a53 -Wall -std=gnu11 -o $@ $<

.PHONY: tools
tools: $(BUILD)/setbright
