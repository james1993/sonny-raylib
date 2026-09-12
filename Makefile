CC      ?= gcc
CFLAGS  ?= -std=c99 -Wall -Wextra -O2
RAYLIB_LIBS = -lraylib -lm -lpthread -ldl -lrt -lX11

CORE_SRC = src/core/formula.c src/core/rng.c src/core/unit.c \
           src/core/buffs.c
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
	$(CC) $(CFLAGS) -Werror -o $(BUILD)/test_buffs tests/test_buffs.c $(CORE_SRC) -lm
	$(BUILD)/test_formula tests/vectors_formula.txt
	$(BUILD)/test_rng
	$(BUILD)/test_buffs tests/vectors_buffs.txt

# Regenerate the differential vectors from the reference transcription.
vectors:
	python3 tools/ref_formula.py > tests/vectors_formula.txt
	python3 tools/ref_buffs.py > tests/vectors_buffs.txt

clean:
	rm -rf $(BUILD)
