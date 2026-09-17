#!/usr/bin/env python3
"""Pieces the game draws under a glow filter, rendered with the glow applied.

A filter lives on the PlaceObject, not in the clip, so the decompiler exports
the art without it -- the zone's progress bar comes out a flat yellow block
where the original has a lit one. This renders the named pieces the way Flash
composites them: the glow worked out from the art's own alpha, coloured,
multiplied by the filter's strength, and laid under the art.

    python3 tools/extract_glows.py --raw assets/raw

Writes assets/art/glow/*.png and data/extracted/glows.json, which
build_assets.py reads to put them in the manifest.
"""
import argparse
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
import rasterize                                                # noqa: E402

def blur_sigma(glow):
    """The Gaussian PIL is asked for, from the filter's own numbers.

    Flash blurs with box passes, and how many it makes is the filter's
    quality. One box pass of width w has a standard deviation of w/sqrt(12),
    and n of them stack to sqrt(n) times that -- so the bar's three passes
    come out at w/2 and the turn ring's single pass at about 0.29w, which is
    why the two glows look so different for the blur each asks for."""
    return glow['blur'] * (glow.get('passes', 3) / 12.0) ** 0.5

# What to render, and the filter the SWF puts on its placement. Each entry is
# the name the engine asks for, the character to take the art from, and the
# glow: colour, blur, strength.
# What to render, and the filters the SWF puts on its placement. Each entry is
# the name the engine asks for, the character to take the art from, and the
# list of glows in the order Flash applies them: colour, blur, strength, and
# whether the glow lights the inside of the shape rather than the air round it.
PIECES = {
    # The fill inside krinXbarPro, the zone's progress bar.
    'ZoneBarFill': {'character': 1261,
                    'filters': [{'color': [255, 204, 51], 'blur': 19.0,
                                 'strength': 1.5, 'passes': 3}]},
    # The turn indicator's ring, at the middle of the bottom panel. The clip
    # under the filters is a clock wipe: two copies of the same half ring,
    # each masked to its own side, each rotated by the battle timer so the
    # ring fills as the turn runs down. Turn-based play -- which is how the
    # game is played -- pins the timer at its limit, so the ring is always
    # whole, and only its colour says whose turn it is.
    'TurnRingFriend': {'character': 1592, 'compose': 'ring',
                       'origin': 'middle',
                       'filters': [{'color': [0, 102, 255], 'blur': 8.0,
                                    'strength': 1.0, 'passes': 1,
                                    'inner': True},
                                   {'color': [0, 102, 204], 'blur': 27.0,
                                    'strength': 1.69921875, 'passes': 1}]},
    'TurnRingEnemy': {'character': 1592, 'compose': 'ring',
                      'origin': 'middle',
                      'filters': [{'color': [255, 204, 0], 'blur': 8.0,
                                   'strength': 1.0, 'passes': 1,
                                   'inner': True},
                                  {'color': [255, 102, 0], 'blur': 27.0,
                                   'strength': 1.69921875, 'passes': 1}]},
}


def shape_png(raw, character):
    path = os.path.join(raw, 'shape_png', '%d.png' % character)
    return path if os.path.exists(path) else None


def sprite_png(raw, character, frame=1):
    import glob
    for d in glob.glob(os.path.join(raw, 'sprite',
                                    'DefineSprite_%d' % character)) \
            + glob.glob(os.path.join(raw, 'sprite',
                                     'DefineSprite_%d_*' % character)):
        path = os.path.join(d, '%d.png' % frame)
        if os.path.exists(path):
            return path
    return None


def compose_ring(art):
    """The turn ring, put back together from the one half the SWF stores.

    The clip holds that half twice, the second copy turned through 180
    degrees, each masked to its own side of the dial. Rotating about the
    shape's own origin -- its straight edge, which is the left of the
    exported art -- is what butts the two halves together."""
    from PIL import Image
    ring = Image.new('RGBA', (art.width * 2, art.height), (0, 0, 0, 0))
    ring.paste(art.rotate(180), (0, 0))
    ring.paste(art, (art.width, 0))
    return ring


COMPOSERS = {'ring': compose_ring}


def outer_glow(canvas, alpha, glow, scale=1.0):
    """The air round the shape lit up, laid under it."""
    from PIL import Image, ImageFilter
    lit = alpha.filter(ImageFilter.GaussianBlur(blur_sigma(glow) * scale))
    lit = lit.point(lambda v: min(255, int(v * glow['strength'])))
    halo = Image.new('RGBA', canvas.size, tuple(glow['color']) + (0,))
    halo.putalpha(lit)
    return Image.alpha_composite(canvas, halo)


def inner_glow(art, glow, scale=1.0):
    """The inside of the shape lit up along its edges.

    Flash lights an inner glow from the *outside*: it blurs everything the
    shape is not, so the light is brightest where the shape meets the air and
    dies away towards the middle. Clipping that back to the shape's own alpha
    keeps it inside the art."""
    from PIL import Image, ImageFilter, ImageChops
    alpha = art.getchannel('A')
    outside = alpha.point(lambda v: 255 - v)
    lit = outside.filter(ImageFilter.GaussianBlur(blur_sigma(glow) * scale))
    lit = lit.point(lambda v: min(255, int(v * glow['strength'])))
    layer = Image.new('RGBA', art.size, tuple(glow['color']) + (0,))
    layer.putalpha(ImageChops.multiply(lit, alpha))
    return Image.alpha_composite(art, layer)


def render(source, spec, out_path, scale=1.0):
    """The art with its filters applied. -> (width, height, pad, scale), the
    first three in stage units: the caller records where the piece's own
    origin sits, which is a stage coordinate whatever the art is drawn at."""
    from PIL import Image
    art = rasterize.art(rasterize.svg_sibling(source), source, scale)
    if art is None:
        art = Image.open(source).convert('RGBA')
        scale = 1.0
    compose = COMPOSERS.get(spec.get('compose'))
    if compose:
        art = compose(art)

    glows = spec.get('filters') or [spec['glow']]
    outers = [g for g in glows if not g.get('inner')]
    inners = [g for g in glows if g.get('inner')]

    # Room for the widest halo. An inner glow stays inside the art, so only
    # the outer ones decide how far the picture has to reach.
    pad = max([int(blur_sigma(g) * scale * 3) + 1 for g in outers] or [0])
    canvas = Image.new('RGBA', (art.width + pad * 2, art.height + pad * 2),
                       (0, 0, 0, 0))

    alpha = Image.new('L', canvas.size, 0)
    alpha.paste(art.getchannel('A'), (pad, pad))
    for glow in outers:
        canvas = outer_glow(canvas, alpha, glow, scale)
    for glow in inners:
        art = inner_glow(art, glow, scale)
    canvas.alpha_composite(art, (pad, pad))
    canvas.save(out_path)
    return art.width / scale, art.height / scale, pad / scale, scale


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--raw', default='assets/raw')
    ap.add_argument('--scale', type=float, default=rasterize.DEFAULT_SCALE,
                    help='pixels of art per stage unit; see tools/rasterize.py')
    ap.add_argument('--out', default=os.path.join(ROOT, 'assets/art/glow'))
    ap.add_argument('--json',
                    default=os.path.join(ROOT, 'data/extracted/glows.json'))
    args = ap.parse_args(argv)

    os.makedirs(args.out, exist_ok=True)
    out = {}
    for name, spec in PIECES.items():
        source = (shape_png(args.raw, spec['character'])
                  or sprite_png(args.raw, spec['character']))
        if not source:
            raise SystemExit('no art for character %d' % spec['character'])
        path = os.path.join(args.out, name + '.png')
        width, height, pad, at = render(source, spec, path, args.scale)
        out[name] = {'file': os.path.relpath(path, ROOT),
                     'character': spec['character'],
                     'width': width, 'height': height, 'pad': pad,
                     'scale': at}
        # Where the piece's own origin sits in the art. Left unsaid, it is
        # the flat art's, which build_assets.py already knows; a composed
        # piece has to say, because the flat art is only half of it.
        if spec.get('origin') == 'middle':
            out[name]['origin'] = [width / 2.0, height / 2.0]
        print('%-14s %gx%g art at %g px to the unit, %g units of glow '
              'round it' % (name, width, height, at, pad))
    with open(args.json, 'w', encoding='utf-8') as fh:
        json.dump(out, fh, indent=1)


if __name__ == '__main__':
    main()
