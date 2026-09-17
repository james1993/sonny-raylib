#!/usr/bin/env python3
"""The spinning markers on a zone's scene.

A marker is one clip -- character 1207, an 82-frame pinwheel drawn in grey --
placed on the zone scene with two things that turn it into what the player
sees: a colour transform that makes it red, cyan, gold or green, and a glow
filter that puts the coloured halo round it. Neither is in the clip, and the
decompiler's sprite export freezes nested timelines at frame one, so the
scene it hands back has a still marker painted into it.

This produces the two halves separately:

  * A copy of the SWF with the marker placements taken out of the zone clip,
    so re-exporting that clip gives the scene with no markers on it. The
    engine then draws them itself and they can turn.
  * Every frame of the marker, in each of the styles the scenes ask for, with
    the colour transform applied and the glow rendered behind it -- the glow
    is worked out from the clip's own alpha, which is what Flash does, so it
    turns with the wheel.

    python3 tools/extract_markers.py SONNY1.swf --raw assets/raw

Writes assets/art/marker/*.png, data/extracted/markers.json, and the stripped
SWF the caller then runs ffdec over.
"""
import argparse
import json
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rasterize                                                # noqa: E402
from swfinfo import read_swf, Bits                              # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

ZONE_CLIP = 1237
MARKER = 1207

TAG_END = 0
TAG_SHOW_FRAME = 1
TAG_REMOVE_OBJECT = 5
TAG_DEFINE_SPRITE = 39
TAG_PLACE_OBJECT2 = 26
TAG_PLACE_OBJECT3 = 70
TAG_REMOVE_OBJECT2 = 28
TAG_FRAME_LABEL = 43

TWIPS = 20.0
# Flash blurs with three box passes; three boxes of width w come out close to
# a Gaussian of sigma w/2, which is what PIL is asked for.
BLUR_TO_SIGMA = 0.5


def tags(body, pos, end):
    while pos + 2 <= end:
        code_and_len = struct.unpack_from('<H', body, pos)[0]
        pos += 2
        tag, length = code_and_len >> 6, code_and_len & 0x3F
        header = 2
        if length == 0x3F:
            length = struct.unpack_from('<I', body, pos)[0]
            pos += 4
            header = 6
        yield tag, pos, length, header
        pos += length
        if tag == TAG_END:
            break


def header_start(body):
    nbits = body[0] >> 3
    return (5 + nbits * 4 + 7) // 8 + 4


def read_matrix(b):
    scale_x = scale_y = 1.0
    if b.ub(1):
        n = b.ub(5)
        scale_x, scale_y = b.sb(n) / 65536.0, b.sb(n) / 65536.0
    if b.ub(1):
        n = b.ub(5)
        b.sb(n), b.sb(n)
    n = b.ub(5)
    x, y = b.sb(n) / TWIPS, b.sb(n) / TWIPS
    b.align()
    return scale_x, scale_y, x, y


def read_cxform(b):
    has_add, has_mult, n = b.ub(1), b.ub(1), b.ub(4)
    mult = [b.sb(n) for _ in range(4)] if has_mult else [256] * 4
    add = [b.sb(n) for _ in range(4)] if has_add else [0] * 4
    b.align()
    return mult, add


def read_filters(body, b):
    """Only what these placements use: a glow, which is all of them."""
    out = []
    count = body[b.pos]
    b.pos += 1
    for _ in range(count):
        which = body[b.pos]
        b.pos += 1
        if which != 2:                      # not a glow: stop reading
            return out
        red, green, blue, alpha = body[b.pos:b.pos + 4]
        b.pos += 4
        blur_x, blur_y = struct.unpack_from('<ii', body, b.pos)
        b.pos += 8
        strength = struct.unpack_from('<h', body, b.pos)[0] / 256.0
        b.pos += 2
        b.pos += 1                          # passes and the inner/knockout bits
        out.append({'color': [red, green, blue, alpha],
                    'blur_x': blur_x / 65536.0, 'blur_y': blur_y / 65536.0,
                    'strength': strength})
    return out


def parse_place(body, start, tag):
    """-> dict of what this placement sets, or None for a remove."""
    b = Bits(body, start)
    flags = b.ub(8)
    b.align()
    flags2 = 0
    if tag == TAG_PLACE_OBJECT3:
        flags2 = body[b.pos]
        b.pos += 1
    depth = struct.unpack_from('<H', body, b.pos)[0]
    b.pos += 2
    out = {'depth': depth, 'move': bool(flags & 0x01)}
    if tag == TAG_PLACE_OBJECT3 and ((flags2 & 0x08)
                                     or ((flags2 & 0x10) and (flags & 0x02))):
        b.pos = body.index(b'\0', b.pos) + 1
    if flags & 0x02:
        out['character'] = struct.unpack_from('<H', body, b.pos)[0]
        b.pos += 2
    if flags & 0x04:
        out['matrix'] = read_matrix(b)
    if flags & 0x08:
        out['cxform'] = read_cxform(b)
    if flags & 0x10:
        b.pos += 2
    if flags & 0x20:
        b.pos = body.index(b'\0', b.pos) + 1
    if flags & 0x40:
        b.pos += 2
    if tag == TAG_PLACE_OBJECT3 and (flags2 & 0x01):
        out['filters'] = read_filters(body, b)
    return out


def zone_clip(body):
    """(start, length, header size) of the zone scene's DefineSprite tag."""
    for tag, pos, length, header in tags(body, header_start(body), len(body)):
        if tag == TAG_DEFINE_SPRITE:
            if struct.unpack_from('<H', body, pos)[0] == ZONE_CLIP:
                return pos, length, header
    raise SystemExit('the zone clip (character %d) is not in this SWF'
                     % ZONE_CLIP)


def walk_zone(body):
    """Every marker on the zone clip's timeline, and which tags put it there.

    Returns (markers_by_frame, labels, doomed) where doomed is the byte range
    of each tag that exists only to place, move or take away a marker."""
    pos, length, _ = zone_clip(body)
    end = pos + length
    depths = {}
    markers = {}
    labels = {}
    doomed = []
    frame = 1
    for tag, at, size, header in tags(body, pos + 4, end):
        span = (at - header, at + size)
        if tag in (TAG_PLACE_OBJECT2, TAG_PLACE_OBJECT3):
            place = parse_place(body, at, tag)
            depth = place['depth']
            slot = depths.get(depth)
            if place.get('character') is not None and not place['move']:
                slot = {'character': place['character']}
                depths[depth] = slot
            if slot is None:
                continue
            for key in ('matrix', 'cxform', 'filters'):
                if key in place:
                    slot[key] = place[key]
            if slot['character'] == MARKER:
                doomed.append(span)
        elif tag in (TAG_REMOVE_OBJECT, TAG_REMOVE_OBJECT2):
            offset = 2 if tag == TAG_REMOVE_OBJECT else 0
            depth = struct.unpack_from('<H', body, at + offset)[0]
            gone = depths.pop(depth, None)
            if gone and gone['character'] == MARKER:
                doomed.append(span)
        elif tag == TAG_FRAME_LABEL:
            labels[body[at:body.index(b'\0', at)].decode('utf-8')] = frame
        elif tag == TAG_SHOW_FRAME:
            here = []
            for depth in sorted(depths):
                slot = depths[depth]
                if slot['character'] != MARKER or 'matrix' not in slot:
                    continue
                here.append({'depth': depth, 'matrix': slot['matrix'],
                             'cxform': slot.get('cxform', ([256] * 4, [0] * 4)),
                             'filters': slot.get('filters', [])})
            markers[frame] = here
            frame += 1
    return markers, labels, doomed


def strip_markers(path, out_path):
    """A copy of the SWF with nothing in the zone clip that places a marker."""
    _, version, _, body = read_swf(path)
    _, _, labels, = None, None, None
    markers, labels, doomed = walk_zone(body)
    pos, length, header = zone_clip(body)

    keep = bytearray()
    cut = sorted(doomed)
    at = pos + 4
    for start, stop in cut:
        keep += body[at:start]
        at = stop
    keep += body[at:pos + length]
    payload = body[pos:pos + 4] + bytes(keep)

    rebuilt = bytearray(body[:pos - header])
    rebuilt += struct.pack('<HI', (TAG_DEFINE_SPRITE << 6) | 0x3F,
                           len(payload))
    rebuilt += payload
    rebuilt += body[pos + length:]

    out = bytearray(b'FWS')
    out.append(version)
    out += struct.pack('<I', 8 + len(rebuilt))
    out += rebuilt
    with open(out_path, 'wb') as fh:
        fh.write(out)
    return markers, labels, len(cut)


def style_key(cxform, filters):
    mult, add = cxform
    glow = filters[0]['color'] if filters else [0, 0, 0, 0]
    return 'x%s_g%s' % ('.'.join(str(v) for v in mult + add),
                        '.'.join(str(v) for v in glow))


def style_name(index):
    return 'marker%d' % index


def render_style(frames, cxform, filters, out_dir, name, scale=1.0):
    """The marker's own frames in one style: the colour transform applied, and
    the glow the placement asks for rendered from the frame's own alpha.

    `scale` is how many pixels to the stage unit the art is wanted at, as
    everywhere else; the halo is worked out in those pixels too, so it comes
    out as fine as the wheel it surrounds. What is handed back is in units:
    the caller records where the marker's origin sits, and that is a stage
    coordinate whatever the art is rasterised at."""
    from PIL import Image, ImageFilter
    mult, add = cxform
    glow = filters[0] if filters else None
    pad = 0
    if glow:
        pad = int(max(glow['blur_x'], glow['blur_y']) * 1.5 * scale) + 1

    written = []
    for index, path in frames:
        art = rasterize.art(rasterize.svg_sibling(path), path, scale)
        if art is None:
            art = Image.open(path).convert('RGBA')
            scale = 1.0
            pad = (int(max(glow['blur_x'], glow['blur_y']) * 1.5) + 1
                   if glow else 0)
        red, green, blue, alpha = art.split()
        # CXFORMWITHALPHA: channel * mult / 256 + add, clamped.
        def transform(band, i):
            return band.point(lambda v: min(255, max(0, (v * mult[i]) // 256
                                                     + add[i])))
        art = Image.merge('RGBA', (transform(red, 0), transform(green, 1),
                                   transform(blue, 2), transform(alpha, 3)))

        canvas = Image.new('RGBA', (art.width + pad * 2, art.height + pad * 2),
                           (0, 0, 0, 0))
        if glow:
            spread = art.getchannel('A').copy()
            lit = Image.new('L', canvas.size, 0)
            lit.paste(spread, (pad, pad))
            lit = lit.filter(ImageFilter.GaussianBlur(
                (glow['blur_x'] + glow['blur_y']) / 2 * BLUR_TO_SIGMA
                * scale))
            strength = glow['strength'] * glow['color'][3] / 255.0
            lit = lit.point(lambda v: min(255, int(v * strength)))
            halo = Image.new('RGBA', canvas.size,
                             tuple(glow['color'][:3]) + (0,))
            halo.putalpha(lit)
            canvas = Image.alpha_composite(canvas, halo)
        canvas.alpha_composite(art, (pad, pad))
        out = os.path.join(out_dir, '%s_%d.png' % (name, index))
        canvas.save(out)
        written.append((index, os.path.relpath(out, ROOT)))
    return written, pad, scale


SVG_ROOT_TRANSFORM = __import__('re').compile(
    r'<g transform="matrix\(([-0-9.eE]+), *([-0-9.eE]+), *([-0-9.eE]+), *'
    r'([-0-9.eE]+), *([-0-9.eE]+), *([-0-9.eE]+)\)"')


def marker_origin(raw_dir):
    """Where the marker's own origin sits inside its exported frame -- the
    root transform of the SVG the decompiler writes beside every PNG."""
    svg = os.path.join(raw_dir, 'sprite_svg', 'DefineSprite_%d' % MARKER,
                       '1.svg')
    with open(svg, encoding='utf-8', errors='replace') as fh:
        match = SVG_ROOT_TRANSFORM.search(fh.read(4096))
    if not match:
        raise SystemExit('no root transform in %s' % svg)
    return float(match.group(5)), float(match.group(6))


def marker_frames(raw_dir):
    """The decompiler's render of the marker clip, frame by frame."""
    import glob
    import re
    here = os.path.join(raw_dir, 'sprite', 'DefineSprite_%d' % MARKER)
    out = []
    for path in glob.glob(os.path.join(here, '*.png')):
        match = re.match(r'(\d+)\.png$', os.path.basename(path))
        if match:
            out.append((int(match.group(1)), path))
    if not out:
        raise SystemExit('no frames for the marker clip in %s' % here)
    return sorted(out)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('swf')
    ap.add_argument('--raw', default='assets/raw')
    ap.add_argument('--scale', type=float, default=rasterize.DEFAULT_SCALE,
                    help='pixels of art per stage unit; see tools/rasterize.py')
    ap.add_argument('--out', default=os.path.join(ROOT, 'assets/art/marker'))
    ap.add_argument('--stripped',
                    help='where to write the marker-free SWF copy')
    ap.add_argument('--json',
                    default=os.path.join(ROOT, 'data/extracted/markers.json'))
    args = ap.parse_args(argv)

    stripped = args.stripped or os.path.join(
        os.path.dirname(args.json), 'zone_scene_nomarkers.swf')
    markers, labels, removed = strip_markers(args.swf, stripped)
    print('%s: %d placements taken out of the zone clip'
          % (os.path.relpath(stripped, ROOT), removed))

    os.makedirs(args.out, exist_ok=True)
    frames = marker_frames(args.raw)
    origin_x, origin_y = marker_origin(args.raw)

    styles = {}
    placed = {}
    for label, frame in sorted(labels.items(), key=lambda kv: kv[1]):
        here = []
        for marker in markers.get(frame, []):
            key = style_key(marker['cxform'], marker['filters'])
            if key not in styles:
                styles[key] = {'name': style_name(len(styles)),
                               'cxform': marker['cxform'],
                               'filters': marker['filters']}
            scale_x, scale_y, x, y = marker['matrix']
            here.append({'depth': marker['depth'], 'style': styles[key]['name'],
                         'x': round(x, 3), 'y': round(y, 3),
                         'scale_x': round(scale_x, 6),
                         'scale_y': round(scale_y, 6)})
        placed[label] = here

    out = {'clip': MARKER, 'frames': len(frames), 'styles': {}, 'zones': placed}
    for style in styles.values():
        written, pad, at = render_style(frames, style['cxform'],
                                        style['filters'], args.out,
                                        style['name'], args.scale)
        out['styles'][style['name']] = {
            'files': [path for _, path in written],
            # The glow grows the canvas, so the origin moves with it -- by
            # the halo's width in units, which is its pixels over the scale.
            'offset': [origin_x + pad / at, origin_y + pad / at],
            'scale': at,
            'glow': style['filters'][0]['color'] if style['filters'] else None,
        }
        print('%-9s %3d frames at %g px to the unit, %d px of glow round each'
              % (style['name'], len(written), at, pad))

    for label, here in placed.items():
        print('%-16s %s' % (label, ', '.join('%s at %g,%g' % (m['style'],
                                                              m['x'], m['y'])
                                             for m in here) or '(none)'))
    with open(args.json, 'w', encoding='utf-8') as fh:
        json.dump(out, fh, indent=1)


if __name__ == '__main__':
    main()
