#!/usr/bin/env python3
"""Pull the cutscene narration out of the SWF.

Each of the three comics carries its voice track as a *stream* sound on the
animation's own timeline -- a SoundStreamHead on the sprite and a
SoundStreamBlock on every frame -- not as a DefineSound something starts. That
is why no script mentions it and why the decompiler's sound export does not
contain it, and it is also why the original's cutscene runs at the narration's
pace: a Flash stream sound is the clock, and the timeline is held to it.

The header says MP3, mono, 22050 Hz, 735 samples a frame, which at the SWF's
30 fps is exactly one frame of audio per frame of animation. Each block is a
four-byte header (samples in the block, then the seek offset into the first
MP3 frame) followed by raw MP3 frames, so concatenating the payloads gives the
track back unaltered.

    python3 tools/extract_streams.py SONNY1.swf

Writes assets/art/sound/<name>.mp3 and data/extracted/streams.json, which
build_assets.py reads to put them in the manifest.
"""
import argparse
import json
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from swfinfo import read_swf

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# The three comics, and the name the engine asks for each narration by.
NARRATION = {1695: 'CutsceneVoiceIntro',
             1710: 'CutsceneVoiceBridge',
             1719: 'CutsceneVoiceOutro'}

TAG_END = 0
TAG_SHOW_FRAME = 1
TAG_DEFINE_SPRITE = 39
TAG_SOUND_STREAM_HEAD = 18
TAG_SOUND_STREAM_HEAD2 = 45
TAG_SOUND_STREAM_BLOCK = 19

FORMAT_MP3 = 2
RATES = (5512, 11025, 22050, 44100)


def tags(body, pos, end):
    while pos + 2 <= end:
        code_and_len = struct.unpack_from('<H', body, pos)[0]
        pos += 2
        tag, length = code_and_len >> 6, code_and_len & 0x3F
        if length == 0x3F:
            length = struct.unpack_from('<I', body, pos)[0]
            pos += 4
        yield tag, pos, length
        pos += length
        if tag == TAG_END:
            break


def header_start(body):
    """Past the stage rect, the frame rate and the frame count."""
    nbits = body[0] >> 3
    return (5 + nbits * 4 + 7) // 8 + 4


def stream_head(payload):
    info = payload[1]
    return {'format': (info >> 4) & 0xF,
            'rate': RATES[(info >> 2) & 3],
            'sample_size': 16 if (info >> 1) & 1 else 8,
            'stereo': bool(info & 1),
            'samples_per_frame': struct.unpack_from('<H', payload, 2)[0]}


def sprite_stream(body, pos, length):
    """The stream on one sprite: its head, and the payload of every block in
    frame order."""
    head, blocks, frame = None, [], 1
    for tag, at, size in tags(body, pos + 4, pos + length):
        if tag == TAG_SHOW_FRAME:
            frame += 1
        elif tag in (TAG_SOUND_STREAM_HEAD, TAG_SOUND_STREAM_HEAD2):
            head = stream_head(body[at:at + size])
        elif tag == TAG_SOUND_STREAM_BLOCK and size > 4:
            # SampleCount UI16, SeekSamples SI16, then the MP3 frames.
            blocks.append(body[at + 4:at + size])
    return head, blocks, frame - 1


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('swf')
    ap.add_argument('--out', default=os.path.join(ROOT, 'assets/art/sound'))
    ap.add_argument('--json',
                    default=os.path.join(ROOT, 'data/extracted/streams.json'))
    args = ap.parse_args(argv)

    _, _, _, body = read_swf(args.swf)
    os.makedirs(args.out, exist_ok=True)

    found = {}
    for tag, pos, length in tags(body, header_start(body), len(body)):
        if tag != TAG_DEFINE_SPRITE:
            continue
        character = struct.unpack_from('<H', body, pos)[0]
        if character not in NARRATION:
            continue
        head, blocks, frames = sprite_stream(body, pos, length)
        if not head or not blocks:
            continue
        if head['format'] != FORMAT_MP3:
            raise SystemExit('sprite %d: stream is format %d, not MP3'
                             % (character, head['format']))
        name = NARRATION[character]
        path = os.path.join(args.out, name + '.mp3')
        with open(path, 'wb') as fh:
            for payload in blocks:
                fh.write(payload)
        found[name] = {'clip': character, 'frames': frames,
                       'rate': head['rate'],
                       'samples_per_frame': head['samples_per_frame'],
                       'file': os.path.relpath(path, ROOT)}
        print('%-22s %4d frames, %s, %.1f kB'
              % (name, frames, '%d Hz %s' % (head['rate'],
                                             'stereo' if head['stereo']
                                             else 'mono'),
                 os.path.getsize(path) / 1e3))

    missing = set(NARRATION.values()) - set(found)
    if missing:
        raise SystemExit('no stream found for: %s' % ', '.join(sorted(missing)))

    with open(args.json, 'w', encoding='utf-8') as fh:
        json.dump(found, fh, indent=1)


if __name__ == '__main__':
    main()
