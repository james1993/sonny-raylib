#!/usr/bin/env python3
"""Work out where the battle screen puts each of the six units.

The original places a BATTLESCREEN instance on the root timeline and, inside
it, six named player containers. Combining the two gives the stage coordinate
of every slot -- and the containers for the right-hand team carry a negative
horizontal scale, which is how the game mirrors them to face left.

    python3 tools/extract_stage.py SONNY1.swf > data/extracted/stage.json
"""
import json
import os
import re
import sys

# The root <g> of an exported SVG says where the art's own origin sits inside
# its canvas -- for the bar, its centre rather than a corner.
SVG_ROOT_TRANSFORM = re.compile(
    r'<g transform="matrix\(([-0-9.eE]+), *([-0-9.eE]+), *([-0-9.eE]+), *'
    r'([-0-9.eE]+), *([-0-9.eE]+), *([-0-9.eE]+)\)"')


def sprite_geometry(raw_dir, character):
    """(width, height, origin x, origin y) of an exported sprite frame."""
    if not raw_dir:
        return None
    import glob
    matches = glob.glob(os.path.join(raw_dir, 'sprite_svg',
                                     'DefineSprite_%d' % character, '1.svg'))
    matches += glob.glob(os.path.join(raw_dir, 'sprite_svg',
                                      'DefineSprite_%d_*' % character, '1.svg'))
    if not matches:
        return None
    with open(matches[0], encoding='utf-8', errors='replace') as fh:
        head = fh.read(4096)
    size = re.search(r'height="([0-9.]+)px" width="([0-9.]+)px"', head)
    origin = SVG_ROOT_TRANSFORM.search(head)
    if not size or not origin:
        return None
    return (float(size.group(2)), float(size.group(1)),
            float(origin.group(5)), float(origin.group(6)))

sys.path.insert(0, __file__.rsplit('/', 1)[0])
from swfinfo import read_swf                       # noqa: E402
from swf_doll import model_frames, exports_table   # noqa: E402

BATTLE_SCREEN = 'BATTLESCREEN'


def last_seen(frames, name):
    """The most recent placement of a named instance across a timeline."""
    found = None
    for frame in frames:
        if name in frame:
            found = frame[name]
    return found


def main(path, raw_dir=None):
    _, _, _, body = read_swf(path)

    root = model_frames(body, 0)
    screen = last_seen(root, BATTLE_SCREEN)
    if not screen:
        raise SystemExit('no %s on the root timeline' % BATTLE_SCREEN)

    sa, _, _, sd, sx, sy = screen['matrix']
    inner = model_frames(body, screen['character'])

    slots = {}
    for slot in range(1, 7):
        placement = last_seen(inner, 'player%d' % slot)
        if not placement:
            continue
        a, _, _, d, x, y = placement['matrix']
        slots[slot] = {
            'x': round(sx + sa * x, 3),
            'y': round(sy + sd * y, 3),
            'scale_x': round(sa * a, 6),
            'scale_y': round(sd * d, 6),
            # A negative horizontal scale is the original mirroring the
            # right-hand team so it faces the other way.
            'flip': a < 0,
        }

    # The backdrop: the zone frames each place one unnamed child, and its
    # matrix is where the artwork actually sits on the stage.
    backdrop = None
    for frame in inner:
        for key, info in frame.items():
            if key.startswith('@') and info.get('character'):
                a, _, _, d, x, y = info['matrix']
                candidate = {'character': info['character'],
                             'x': round(sx + sa * x, 3),
                             'y': round(sy + sd * y, 3),
                             'scale_x': round(sa * a, 6),
                             'scale_y': round(sd * d, 6)}
                # Keep the lowest depth: the backdrop sits behind everything.
                if backdrop is None or info['depth'] < backdrop['depth']:
                    candidate['depth'] = info['depth']
                    backdrop = candidate
        if backdrop:
            break

    # The health/focus bars, which the root timeline places by name as
    # p1BAR..p6BAR. Their size comes from the bar sprite itself.
    bars = {}
    for slot in range(1, 7):
        placement = last_seen(root, 'p%dBAR' % slot)
        if not placement:
            continue
        a, _, _, d, x, y = placement['matrix']
        entry = {'x': round(x, 3), 'y': round(y, 3),
                 'scale_x': round(a, 6), 'scale_y': round(d, 6),
                 'character': placement['character']}
        geom = sprite_geometry(raw_dir, placement['character'])
        if geom:
            entry['width'], entry['height'] = geom[0], geom[1]
            entry['origin_x'], entry['origin_y'] = geom[2], geom[3]
        bars[slot] = entry

    json.dump({'screen': {'x': sx, 'y': sy, 'character': screen['character']},
               'slots': slots, 'backdrop': backdrop, 'bars': bars},
              sys.stdout, indent=1)
    print()


if __name__ == '__main__':
    if len(sys.argv) not in (2, 3):
        raise SystemExit(__doc__)
    main(sys.argv[1], sys.argv[2] if len(sys.argv) > 2 else None)
