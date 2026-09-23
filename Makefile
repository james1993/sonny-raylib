CC      ?= gcc
CFLAGS  ?= -std=c99 -Wall -Wextra -O2
RAYLIB_LIBS = -lraylib -lm -lpthread -ldl -lrt -lX11

CORE_SRC = src/core/formula.c src/core/rng.c src/core/unit.c \
           src/core/buffs.c src/core/battle.c src/core/character.c \
           src/core/campaign.c src/core/save.c src/core/stagefit.c \
           src/gen/gamedata.c
BUILD    = build

.PHONY: all game dist test simulate vectors data extract clean playtest

all: game test

$(BUILD):
	@mkdir -p $(BUILD)

game: $(BUILD)
	$(CC) $(CFLAGS) -o $(BUILD)/sonny src/platform/main.c \
	    src/platform/assets.c src/platform/audio.c src/platform/ui.c \
	    src/platform/screens.c src/platform/screen_battle.c \
	    src/platform/screen_menu.c src/platform/glow.c \
	    src/platform/render.c src/platform/options.c \
	    src/platform/loader.c src/platform/prefetch.c \
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
	python3 tools/check_content.py

# Regenerate the C data tables from the extracted JSON, with anything in
# data/content/ laid over it -- which is checked for references that land
# nowhere before anything is written.
data:
	python3 tools/check_content.py
	python3 tools/gen_c_data.py
	python3 tools/gen_asset_manifest.py

# Everything data/extracted/ and assets/art/ are made of, from the original,
# in the order each step needs the last. Needs the game's SONNY1.swf and JPEXS
# FFDec; the decompiler dumps land in assets/raw/, which is gitignored.
#   make extract SWF=path/to/SONNY1.swf FFDEC=path/to/ffdec-cli.jar
RAW = assets/raw
extract:
	@test -n "$(SWF)" -a -n "$(FFDEC)" || \
	    { echo "make extract SWF=SONNY1.swf FFDEC=ffdec-cli.jar"; exit 2; }
	tools/extract_assets.sh "$(SWF)" "$(FFDEC)" $(RAW)
	java -Xmx3g -jar "$(FFDEC)" -format script:as -export script $(RAW)/as "$(SWF)"
	python3 tools/swf_exports.py "$(SWF)" > data/extracted/exports.json
	python3 tools/extract_data.py $(RAW)/as/scripts -o data/extracted
	python3 tools/extract_stage.py "$(SWF)" $(RAW) > data/extracted/stage.json
	python3 tools/extract_cast.py "$(SWF)" > data/extracted/cast_frames.json
	python3 tools/swf_doll.py "$(SWF)" MODEL1 > data/extracted/doll_frames.json
	python3 tools/extract_clip_lengths.py "$(SWF)" --scripts $(RAW)/as
	python3 tools/extract_markers.py "$(SWF)" --raw $(RAW) \
	    --stripped $(RAW)/nomarkers.swf
	tools/extract_assets.sh $(RAW)/nomarkers.swf "$(FFDEC)" $(RAW)/nomarkers
	python3 tools/extract_glows.py --raw $(RAW)
	python3 tools/extract_streams.py "$(SWF)"
	python3 tools/build_assets.py --raw $(RAW) --zone-raw $(RAW)/nomarkers \
	    --out assets/art
	$(MAKE) data

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
