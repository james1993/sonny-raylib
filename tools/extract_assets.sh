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

# The same shapes as PNG. These are what the SVG renderings are checked
# against, and what is shipped for the handful of pieces that will not
# rasterise faithfully. A few are drawn under a glow filter that lives on the
# placement rather than in the clip, and tools/extract_glows.py renders those
# itself from the flat art.
java -Xmx3g -jar "$FFDEC" -format shape:png -export shape "$OUT/shape_png" "$SWF"

# Bitmaps (few, but needed).
java -Xmx3g -jar "$FFDEC" -format image:png -export image "$OUT/image" "$SWF"

# Every sprite as SVG as well, and this is what most of the art is actually
# built from. The decompiler will not export a PNG at more than one pixel to
# the stage unit, which is as sharp as the game could ever be; the SVG is the
# same vector art, so tools/rasterize.py renders it at two and checks what
# comes back against the PNG beside it. The root transform in the SVG is also
# where the art's own origin sits inside the trimmed PNG, and without that
# every sprite frame lands beside its mark.
java -Xmx3g -jar "$FFDEC" -format sprite:svg -export sprite "$OUT/sprite_svg" "$SWF"

# Sprites rendered frame by frame. This is the big one: every frame of every
# sprite, so expect ~200 MB. build_assets.py keeps only the frames named by
# the engine.
java -Xmx3g -jar "$FFDEC" -format sprite:png -export sprite "$OUT/sprite" "$SWF"

# The zone scenes again, without their markers. A marker is a clip nested in
# the scene and the decompiler freezes nested timelines at frame one, so the
# scene it hands back has a still marker painted into it and the engine can
# never turn it. tools/extract_markers.py writes a copy of the SWF with those
# placements taken out, and renders the marker's own frames separately; this
# exports the scenes from that copy.
python3 tools/extract_markers.py "$SWF" --raw "$OUT" \
    --stripped "$OUT/nomarkers.swf"
java -Xmx3g -jar "$FFDEC" -format sprite:png -export sprite \
    "$OUT/nomarkers/sprite" "$OUT/nomarkers.swf"
java -Xmx3g -jar "$FFDEC" -format sprite:svg -export sprite \
    "$OUT/nomarkers/sprite_svg" "$OUT/nomarkers.swf"

# Every script, which is where a clip says where it stops. Small, and it is
# what tools/extract_clip_lengths.py needs to tell how long a clip actually
# plays as against how long the decompiler exports it -- see that tool.
java -Xmx3g -jar "$FFDEC" -format script:as -export script "$OUT/as" "$SWF"
python3 tools/extract_clip_lengths.py "$SWF" --scripts "$OUT/as/scripts"

# The cutscene narration, which is a stream sound on each comic's timeline and
# so is not in the sound export above.
python3 tools/extract_streams.py "$SWF"

echo
echo "Raw export in $OUT:"
du -sh "$OUT"/* 2>/dev/null || true
echo
echo "Next: python3 tools/swf_exports.py \"$SWF\" > data/extracted/exports.json"
echo "      python3 tools/build_assets.py --raw $OUT \\"
echo "              --zone-raw $OUT/nomarkers"
