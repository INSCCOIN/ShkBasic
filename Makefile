# shkbasic - MMBasic for the SharkDeck
#
#   make                 build for this machine
#   make deck            cross-compile for the SharkDeck (aarch64)
#   make test            run the test suite
#   make install         install to /usr/local/bin
#
# Cross-compiling needs gcc-aarch64-linux-gnu:
#   sudo apt install gcc-aarch64-linux-gnu

CC      ?= cc
CFLAGS  ?= -std=c99 -O2 -Wall -Wextra -D_DEFAULT_SOURCE
LDFLAGS ?=
LDLIBS  ?= -lm

SRC  := $(wildcard src/*.c)
OBJ  := $(SRC:src/%.c=build/%.o)
BIN  := shkbasic

# Cortex-A53 (Allwinner H618) with a static build so a half-broken
# dpkg state on the SD card cannot strand you.
DECK_CC     ?= aarch64-linux-gnu-gcc
DECK_CFLAGS := -std=c99 -O2 -mcpu=cortex-a53 -Wall -Wextra -D_DEFAULT_SOURCE
DECK_LDLIBS := -lm -static

.PHONY: all deck test clean install font

all: $(BIN)

$(BIN): $(OBJ)
	$(CC) $(LDFLAGS) -o $@ $(OBJ) $(LDLIBS)

build/%.o: src/%.c src/shk.h | build
	$(CC) $(CFLAGS) -c $< -o $@

build:
	@mkdir -p build

deck: $(SRC) src/shk.h
	@mkdir -p build-deck
	$(DECK_CC) $(DECK_CFLAGS) -o shkbasic-aarch64 $(SRC) $(DECK_LDLIBS)
	@echo "built shkbasic-aarch64 - copy it to the deck with scp"

# regenerate the bitmap font header from its source art
font:
	python3 tools/mkfont.py src/font6x8.h >/dev/null

test: $(BIN)
	@sh tests/run.sh

install: $(BIN)
	install -m 755 $(BIN) /usr/local/bin/shkbasic

clean:
	rm -rf build build-deck $(BIN) shkbasic-aarch64 tests/out
