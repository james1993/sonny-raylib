CC      ?= gcc
CFLAGS  ?= -std=c99 -Wall -Wextra -O2
LDFLAGS ?=
RAYLIB_LIBS = -lraylib -lm -lpthread -ldl -lrt -lX11

CORE_SRC = src/core/combat.c src/core/rng.c
BUILD    = build

.PHONY: all game test clean

all: game test

$(BUILD):
	@mkdir -p $(BUILD)

game: $(BUILD)
	$(CC) $(CFLAGS) -o $(BUILD)/sonny src/platform/main.c $(CORE_SRC) $(LDFLAGS) $(RAYLIB_LIBS)

test: $(BUILD)
	$(CC) $(CFLAGS) -Werror -o $(BUILD)/test_core tests/test_core.c $(CORE_SRC) -lm
	$(BUILD)/test_core

clean:
	rm -rf $(BUILD)
