CC      ?= gcc
CFLAGS  ?= -std=c99 -Wall -Wextra -O2
RAYLIB_LIBS = -lraylib -lm -lpthread -ldl -lrt -lX11

CORE_SRC = src/core/formula.c src/core/rng.c src/core/unit.c
BUILD    = build

.PHONY: all game test vectors clean

all: game test

$(BUILD):
	@mkdir -p $(BUILD)

game: $(BUILD)
	$(CC) $(CFLAGS) -o $(BUILD)/sonny src/platform/main.c $(CORE_SRC) $(RAYLIB_LIBS)

test: $(BUILD)
	$(CC) $(CFLAGS) -Werror -o $(BUILD)/test_formula tests/test_formula.c $(CORE_SRC) -lm
	$(CC) $(CFLAGS) -Werror -o $(BUILD)/test_rng tests/test_rng.c src/core/rng.c -lm
	$(BUILD)/test_formula tests/vectors_formula.txt
	$(BUILD)/test_rng

# Regenerate the differential vectors from the reference transcription.
vectors:
	python3 tools/ref_formula.py > tests/vectors_formula.txt

clean:
	rm -rf $(BUILD)
