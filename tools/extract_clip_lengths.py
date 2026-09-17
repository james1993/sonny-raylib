#!/usr/bin/env python3
"""How long each clip actually plays, as against how long it is exported.

The decompiler renders a nested clip by playing it and ignoring its stop(), so
a clip that holds another one comes out longer than it plays. BOOM_RED is a
25-frame holder around a 20-frame burst that ends in stop(): Flash holds the
burst on its blank last frame for the rest of the holder, and the export
instead shows it looping back to the beginning, so frames 21 to 24 are frames
1 to 4 over again. Played back, every one of those effects flares a second
time just as it should be going out.

Reading it off the pictures nearly works -- the repeat is exact, and identical
frames already share a file -- but it is a frame out whenever the holder puts
its child down on a frame other than its first, which the turn ring does. So
it is read from the SWF instead:

    playable = (the frame the child is placed on - 1) + how long the child runs

where a child runs to its own stop() if it has one and to its last frame if it
does not, and the whole thing is capped at the holder's own length.

    python3 tools/extract_clip_lengths.py SONNY1.swf --scripts assets/raw/as

Writes data/extracted/clip_lengths.json, which build_assets.py reads to cut
each clip back to what it plays. The script export is what carries the stop();
without it every child is taken to run to its last frame, which is right for
most of them and never cuts too much.
"""
import argparse
import glob
import json
import os
import re
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
from swfinfo import read_swf                                    # noqa: E402
from swf_doll import (header_end, walk_tags,                    # noqa: E402
                      TAG_DEFINE_SPRITE, TAG_PLACE_OBJECT2,
                      TAG_PLACE_OBJECT3, TAG_SHOW_FRAME)


def sprites(body):
    """{character: (frame count, tag start, tag length)}"""
    out = {}
    for tag, start, length in walk_tags(body, header_end(body)):
        if tag == TAG_DEFINE_SPRITE and length >= 4:
            cid, frames = struct.unpack_from('<HH', body, start)
            out[cid] = (frames, start, length)
    return out


def placements(body, start, length):
    """[(frame the placement is on, character)], frames counted from one."""
    out = []
    frame = 1
    for tag, s, l in walk_tags(body, start + 4, start + length):
        if tag == TAG_SHOW_FRAME:
            frame += 1
        elif tag in (TAG_PLACE_OBJECT2, TAG_PLACE_OBJECT3):
            flags = body[s]
            p = s + 1
            if tag == TAG_PLACE_OBJECT3:
                p += 1
            p += 2                                  # depth
            if flags & 2:                           # has a character
                out.append((frame, struct.unpack_from('<H', body, p)[0]))
    return out


def stop_frames(scripts):
    """{character: the frame its stop() sits on}"""
    out = {}
    if not scripts:
        return out
    for path in glob.glob(os.path.join(scripts, 'DefineSprite_*',
                                       'frame_*', 'DoAction.as')):
        cid = re.search(r'DefineSprite_(\d+)', path)
        frame = re.search(r'frame_(\d+)', path)
        if not cid or not frame:
            continue
        with open(path, encoding='utf-8', errors='replace') as fh:
            if 'stop()' not in fh.read():
                continue
        cid, frame = int(cid.group(1)), int(frame.group(1))
        if cid not in out or frame < out[cid]:
            out[cid] = frame
    return out


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('swf')
    ap.add_argument('--scripts',
                    help="the decompiler's script export, which is where the "
                         "stop() that ends a clip is found")
    ap.add_argument('--out', default=os.path.join(ROOT,
                                                  'data/extracted/clip_lengths.json'))
    args = ap.parse_args(argv)

    _, _, _, body = read_swf(args.swf)
    table = sprites(body)
    stops = stop_frames(args.scripts)

    out = {}
    for cid, (frames, start, length) in table.items():
        best = 0
        for frame, child in placements(body, start, length):
            if child not in table:
                continue
            kid_frames, _, _ = table[child]
            runs = min(stops.get(child, kid_frames), kid_frames)
            best = max(best, frame - 1 + runs)
        if best and best < frames:
            out[str(cid)] = best

    with open(args.out, 'w', encoding='utf-8') as fh:
        json.dump(out, fh, indent=1, sort_keys=True)
    print('%d clips of %d play shorter than they are exported -> %s'
          % (len(out), len(table), os.path.relpath(args.out, ROOT)))
    return 0


if __name__ == '__main__':
    sys.exit(main())
