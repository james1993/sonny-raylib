CC      ?= gcc
CFLAGS  ?= -std=c99 -Wall -Wextra -O2
RAYLIB_LIBS = -lraylib -lm -lpthread -ldl -lrt -lX11

CORE_SRC = src/core/formula.c src/core/rng.c src/core/unit.c \
           src/core/buffs.c src/core/battle.c src/core/character.c \
           src/core/campaign.c src/core/save.c src/core/stagefit.c \
           src/gen/gamedata.c
BUILD    = build

.PHONY: all game dist test simulate vectors data clean playtest

all: game test

$(BUILD):
	@mkdir -p $(BUILD)

game: $(BUILD)
	$(CC) $(CFLAGS) -o $(BUILD)/sonny src/platform/main.c \
	    src/platform/assets.c src/platform/audio.c src/platform/ui.c \
	    src/platform/screens.c src/platform/screen_battle.c \
	    src/platform/screen_menu.c src/platform/glow.c \
	    src/platform/render.c \
	    src/gen/assets_gen.c \
	    $(CORE_SRC) $(RAYLIB_LIBS)

# The binary the repository ships, for people who would rather not build it.
# It is committed, so it goes stale the moment the game or the art changes --
# and stale is not a thing a player can see: art rasterised finer than a build
# expects draws at a multiple of its size, quietly and everywhere. Rebuild
# this with any change that reaches either.
dist: game
	strip -o dist/sonny-linux-x86_64 $(BUILD)/sonny
	@ls -l dist/sonny-linux-x86_64

simulate: $(BUILD)
	$(CC) $(CFLAGS) -o $(BUILD)/simulate tools/simulate.c $(CORE_SRC) -lm

test: $(BUILD)
	$(CC) $(CFLAGS) -Werror -o $(BUILD)/test_formula tests/test_formula.c $(CORE_SRC) -lm
	$(CC) $(CFLAGS) -Werror -o $(BUILD)/test_rng tests/test_rng.c src/core/rng.c -lm
	$(CC) $(CFLAGS) -Werror -o $(BUILD)/test_buffs tests/test_buffs.c $(CORE_SRC) -lm
	$(CC) $(CFLAGS) -Werror -o $(BUILD)/test_heal tests/test_heal.c $(CORE_SRC) -lm
	$(CC) $(CFLAGS) -Werror -o $(BUILD)/test_battle tests/test_battle.c $(CORE_SRC) -lm
	$(CC) $(CFLAGS) -Werror -o $(BUILD)/test_character tests/test_character.c $(CORE_SRC) -lm
	$(CC) $(CFLAGS) -Werror -o $(BUILD)/test_talents tests/test_talents.c $(CORE_SRC) -lm
	$(CC) $(CFLAGS) -Werror -o $(BUILD)/test_campaign tests/test_campaign.c $(CORE_SRC) -lm
	$(CC) $(CFLAGS) -Werror -o $(BUILD)/test_party tests/test_party.c $(CORE_SRC) -lm
	$(CC) $(CFLAGS) -Werror -o $(BUILD)/test_window tests/test_window.c $(CORE_SRC) -lm
	$(CC) $(CFLAGS) -Werror -o $(BUILD)/test_save tests/test_save.c $(CORE_SRC) -lm
	$(BUILD)/test_formula tests/vectors_formula.txt
	$(BUILD)/test_rng
	$(BUILD)/test_buffs tests/vectors_buffs.txt
	$(BUILD)/test_heal tests/vectors_heal.txt
	$(BUILD)/test_battle
	$(BUILD)/test_character tests/vectors_character.txt
	$(BUILD)/test_talents
	$(BUILD)/test_campaign
	$(BUILD)/test_party
	$(BUILD)/test_window
	$(BUILD)/test_save
	python3 tools/check_assets.py

# Regenerate the C data tables from the extracted JSON.
data:
	python3 tools/gen_c_data.py
	python3 tools/gen_asset_manifest.py

# Play both games through the same script and report where they differ.
# Needs Ruffle and a debug-patched copy of the SWF; see tools/refcap.py.
#   make playtest RUFFLE=path/to/ruffle SWF=path/to/sonny1_dbg.swf
playtest: $(BUILD)/sonny
	python3 tools/playtest.py tests/play/zone1.txt \
	    --ruffle "$(RUFFLE)" --swf "$(SWF)"

# Regenerate the differential vectors from the reference transcription.
vectors:
	python3 tools/ref_formula.py > tests/vectors_formula.txt
	python3 tools/ref_buffs.py > tests/vectors_buffs.txt
	python3 tools/ref_formula.py --heal > tests/vectors_heal.txt
	python3 tools/ref_character.py > tests/vectors_character.txt

clean:
	rm -rf $(BUILD)
