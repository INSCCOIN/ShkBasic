# shkbasic - MMBasic for the SharkDeck
#
#   make                 build the interpreter and the IDE
#   make deck            cross-compile both, static, for aarch64
#   make test            run the test suite
#   make install         install to /usr/local/bin
#
# Cross-compiling needs gcc-aarch64-linux-gnu:
#   sudo apt install gcc-aarch64-linux-gnu

CC      ?= cc
CFLAGS  ?= -std=c99 -O2 -Wall -Wextra -D_DEFAULT_SOURCE
LDFLAGS ?=
LDLIBS  ?= -lm

SRC     := $(wildcard src/*.c)
OBJ     := $(SRC:src/%.c=build/%.o)
BIN     := shkbasic

IDE_SRC := $(wildcard ide/*.c)
IDE_OBJ := $(IDE_SRC:ide/%.c=build/ide_%.o)
IDE_BIN := shkide

# Cortex-A53 (Allwinner H618), static so a half-broken dpkg state on
# the SD card cannot strand you.
DECK_CC     ?= aarch64-linux-gnu-gcc
DECK_CFLAGS := -std=c99 -O2 -mcpu=cortex-a53 -Wall -Wextra -D_DEFAULT_SOURCE

.PHONY: all ide deck test test-ide test-all clean install font

all: $(BIN) $(IDE_BIN)

ide: $(IDE_BIN)

$(BIN): $(OBJ)
	$(CC) $(LDFLAGS) -o $@ $(OBJ) $(LDLIBS)

$(IDE_BIN): $(IDE_OBJ)
	$(CC) $(LDFLAGS) -o $@ $(IDE_OBJ)

build/%.o: src/%.c src/shk.h src/builtins.h | build
	$(CC) $(CFLAGS) -c $< -o $@

build/ide_%.o: ide/%.c ide/term.h ide/edit.h ide/browser.h ide/syntax.h | build
	$(CC) $(CFLAGS) -c $< -o $@

build:
	@mkdir -p build

deck: $(SRC) $(IDE_SRC)
	$(DECK_CC) $(DECK_CFLAGS) -o shkbasic-aarch64 $(SRC) -lm -static
	$(DECK_CC) $(DECK_CFLAGS) -o shkide-aarch64 $(IDE_SRC) -static
	@echo "built shkbasic-aarch64 and shkide-aarch64 - scp them to the deck"

# regenerate the bitmap font header from its source art
font:
	python3 tools/mkfont.py src/font6x8.h >/dev/null

test: $(BIN)
	@sh tests/run.sh

# the IDE tests drive the real binary inside a pty (needs python3 + pyte:
#   pip install pyte)
test-ide: $(IDE_BIN) $(BIN)
	@python3 tests/ide_test.py

test-all: test test-ide

install: $(BIN) $(IDE_BIN)
	install -m 755 $(BIN) /usr/local/bin/shkbasic
	install -m 755 $(IDE_BIN) /usr/local/bin/shkide

clean:
	rm -rf build $(BIN) $(IDE_BIN) shkbasic-aarch64 shkide-aarch64 tests/out
