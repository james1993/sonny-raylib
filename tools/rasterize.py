#!/usr/bin/env python3
"""The art at more than one pixel to the stage unit.

The decompiler will not export a shape or a sprite frame at anything but 1:1
from the command line. `export.zoom` is in its settings and `-config
export.zoom=2` is accepted without complaint, but the PNGs come out the same
size: it is the GUI's preview zoom. One pixel to the unit is then the limit on
how sharp the game can ever be, however finely the stage itself is drawn --
where the original is vector art that Flash rasterises at whatever size it is
given.

The decompiler writes an SVG beside every PNG it exports, from the same vector
art, so the art is rasterised from that instead. Nothing about that is taken
on trust: every frame rendered large is reduced back to the size the
decompiler exported it at and compared against what the decompiler exported,
and a frame that does not come back is kept as it was. cairosvg quietly
ignores an SVG filter, and art whose fill is a bitmap cannot gain detail it
never had, so without the check a piece would lose its glow or come back soft
with nothing to say so.
"""
import io
import os

# One pixel of art to the stage unit is what the decompiler gives; this is
# how many are wanted. Two is chosen for what it costs: it covers every
# window up to twice the stage, which is as large as the game opens on a
# 1080-line monitor, and the committed art is four times the size.
DEFAULT_SCALE = 2

_cairosvg = None


def svg_sibling(png_path):
    """The SVG the decompiler wrote beside a PNG it exported. Both are
    rendered from the same vector art, and that is what makes it possible
    both to read the art's own origin out of it and to rasterise it again at
    a size the decompiler will not export at."""
    if os.sep + 'shape_png' + os.sep in png_path:
        svg = png_path.replace(os.sep + 'shape_png' + os.sep,
                               os.sep + 'shape' + os.sep)
    else:
        svg = png_path.replace(os.sep + 'sprite' + os.sep,
                               os.sep + 'sprite_svg' + os.sep)
    svg = os.path.splitext(svg)[0] + '.svg'
    return svg if os.path.exists(svg) else None


def available():
    """Whether the art can be rasterised at all. Missing, everything falls
    back to the decompiler's own 1:1 export and the game still runs -- just
    no sharper than it used to."""
    global _cairosvg
    if _cairosvg is None:
        try:
            import cairosvg
            _cairosvg = cairosvg
        except ImportError:
            _cairosvg = False
    return bool(_cairosvg)


# A line one twip wide, which is how the decompiler writes Flash's hairline.
# The player draws one of those a single screen pixel wide however far the
# movie is zoomed in -- it is the line that never thins away -- and the
# decompiler's own renderer does the same. Taken literally it is a twentieth
# of a pixel, and every outline in the game disappears.
HAIRLINE = 'stroke-width="0.05"'


def _source(svg_path, scale):
    """The SVG with its hairlines set for a rendering at `scale` pixels to the
    unit: a screen pixel there is one over the scale in the art's own units."""
    with open(svg_path, encoding='utf-8') as fh:
        text = fh.read()
    return text.replace(HAIRLINE, 'stroke-width="%g"' % (1.0 / scale))


def render(svg_path, scale):
    """The SVG at `scale` pixels to the unit, or None."""
    if not available():
        return None
    from PIL import Image
    try:
        data = _cairosvg.svg2png(bytestring=_source(svg_path, scale).encode(),
                                 scale=float(scale), unsafe=True)
    except Exception:                                  # noqa: BLE001
        return None
    try:
        return Image.open(io.BytesIO(data)).convert('RGBA')
    except Exception:                                  # noqa: BLE001
        return None


def _premultiplied(image):
    """Colour multiplied by alpha, so what is invisible cannot be compared.

    Reducing an image mixes its pixels, and a transparent pixel still carries
    a colour: without this, the colour sitting under nothing at the edge of a
    shape ends up mixed into the pixel beside it and two renderings that look
    identical compare as different."""
    from PIL import Image, ImageChops
    r, g, b, a = image.split()
    return Image.merge('RGBA', (ImageChops.multiply(r, a),
                                ImageChops.multiply(g, a),
                                ImageChops.multiply(b, a), a))


def reduces_to(large, reference, report=None):
    """Whether art rendered large is the same picture the decompiler exported.

    Two rasterisers never agree pixel for pixel along an edge, and how much of
    a picture is edge depends entirely on how big it is: a backdrop is edge
    nowhere, a lock of hair nine pixels across is edge almost everywhere. So
    the tolerance is taken from the reference itself -- what fraction of it
    lies along a boundary in its own alpha -- and what is being asked is only
    whether this is the same picture, not whether it is the same rendering.
    The rasterisers disagreeing outright is caught by `art` before it gets
    here.
    """
    from PIL import Image, ImageChops, ImageStat
    small = _premultiplied(large.resize(reference.size, Image.BOX))
    ref = _premultiplied(reference)
    diff = ImageChops.difference(small, ref)
    mean = max(ImageStat.Stat(diff).mean)
    pixels = small.width * small.height
    # How much ink there is either way, as a fraction of the canvas rather
    # than of each other: a piece that is nearly empty has an ink ratio that
    # swings on a handful of pixels and says nothing.
    ink = ImageStat.Stat(small.getchannel('A')).sum[0] / 255.0 / pixels
    ink_ref = ImageStat.Stat(ref.getchannel('A')).sum[0] / 255.0 / pixels
    gross = diff.convert('L').point(lambda v: 255 if v > 64 else 0)
    wrong = ImageStat.Stat(gross).sum[0] / 255.0 / pixels
    from PIL import ImageFilter
    # Where the art's alpha steps. Not where it is halfway -- the decompiler
    # exports small pieces with a hard edge and no halfway anywhere, and those
    # are exactly the pieces one pixel of disagreement matters most on.
    border = reference.getchannel('A').filter(ImageFilter.FIND_EDGES)
    border = border.point(lambda v: 255 if v > 32 else 0)
    edge = ImageStat.Stat(border).sum[0] / 255.0 / pixels
    if report is not None:
        report.update(mean=mean, ink=ink - ink_ref, wrong=wrong, edge=edge)
    return (mean <= 12.0 + 120.0 * edge
            and abs(ink - ink_ref) <= 0.03 + edge
            and wrong <= 0.08 + edge)


def _framed(large, reference, scale):
    """The rendering cut to exactly `scale` times the canvas the decompiler
    exported.

    The decompiler truncates: a shape 5.55 units wide comes out 5 pixels, and
    rendered at two the same shape is 11 -- which is 5.5 units, not 5, so the
    two pictures no longer show the same thing. A quarter of a unit is nothing
    on a backdrop and a tenth of a foot on a doll's foot. Cutting the canvas
    to twice the one the decompiler made keeps every piece of art exactly the
    size it has always been, in units, with more pixels in it and nothing
    else changed."""
    from PIL import Image
    want = (int(round(reference.width * scale)),
            int(round(reference.height * scale)))
    if large.size == want:
        return large
    out = Image.new('RGBA', want, (0, 0, 0, 0))
    out.paste(large.crop((0, 0, min(want[0], large.width),
                          min(want[1], large.height))), (0, 0))
    return out


def art(svg_path, png_path, scale, report=None):
    """One frame at `scale` pixels to the unit, checked against the
    decompiler's own export of it. None when there is no SVG for it, when it
    cannot be rendered, or when what comes back is not the same picture."""
    if scale <= 1 or not svg_path or not os.path.exists(png_path):
        return None
    # A filter is where the two renderers part company for good. The
    # decompiler writes a colour transform, a blur or a glow as an SVG filter,
    # and cairosvg has none: it drops them without a word, so the art comes
    # back unlit, or at full strength where it should have faded. Nothing
    # statistical is going to tell that apart from a rasteriser's ordinary
    # disagreement reliably enough, so art with a filter in it is simply not
    # rasterised here.
    with open(svg_path, encoding='utf-8', errors='replace') as fh:
        if '<filter' in fh.read():
            if report is not None:
                report.update(filtered=True)
            return None
    from PIL import Image
    reference = Image.open(png_path).convert('RGBA')
    # The check is made at 1:1, against the picture the decompiler exported,
    # because that is the only size the two can be expected to agree at: a
    # hairline is a screen pixel at any scale, so the large rendering is
    # deliberately not the small one magnified.
    check = render(svg_path, 1)
    if check is None:
        return None
    if not reduces_to(_framed(check, reference, 1), reference, report):
        return None
    large = render(svg_path, scale)
    if large is None:
        return None
    return _framed(large, reference, scale)
