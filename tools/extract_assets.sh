#!/bin/sh
# Extract Sonny 1's art and audio from your own copy of the game.
#
# Needs JPEXS FFDec (ffdec-cli.jar) and the game's SONNY1.swf, which lives in
# the Legacy Collection install directory next to the Adobe AIR launcher.
#
#   tools/extract_assets.sh /path/to/SONNY1.swf /path/to/ffdec-cli.jar [outdir]
#
# Output (default assets/raw) is the full decompiler dump -- a couple of
# hundred megabytes, and gitignored. tools/build_assets.py then selects the
# pieces the engine actually references into the committed set.
set -e

SWF="$1"
FFDEC="$2"
OUT="${3:-assets/raw}"

if [ -z "$SWF" ] || [ -z "$FFDEC" ]; then
    sed -n '2,12p' "$0"
    exit 2
fi

mkdir -p "$OUT"

# Audio: 88 sounds, about 8.5 MB as MP3.
java -Xmx3g -jar "$FFDEC" -format sound:mp3 -export sound "$OUT/sound" "$SWF"

# Vector art, kept as SVG because that is what it is in the original -- the
# SWF has 356 DefineShape tags against 3 bitmaps.
java -Xmx3g -jar "$FFDEC" -format shape:svg -export shape "$OUT/shape" "$SWF"

# Bitmaps (few, but needed).
java -Xmx3g -jar "$FFDEC" -format image:png -export image "$OUT/image" "$SWF"

# Sprites rendered frame by frame. This is the big one: every frame of every
# sprite, so expect ~200 MB. build_assets.py keeps only the frames named by
# the engine.
java -Xmx3g -jar "$FFDEC" -format sprite:png -export sprite "$OUT/sprite" "$SWF"

echo
echo "Raw export in $OUT:"
du -sh "$OUT"/* 2>/dev/null || true
echo
echo "Next: python3 tools/swf_exports.py \"$SWF\" > data/extracted/exports.json"
echo "      python3 tools/build_assets.py --raw $OUT"
