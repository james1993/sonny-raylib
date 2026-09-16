#!/usr/bin/env python3
"""Extract the effect clips the character model plays over itself.

The model's timeline keeps one unnamed slot at depth 33 for an effect that
belongs to the move rather than to the body: a sweep through the magic swing
(attack2), the orb a caster charges (cast), and the crackle of being held
stunned (stun2). swf_doll.py records the slot and which character sits in it,
but not what is inside that character, and the inside is what matters: each of
these clips is a pair of layers, one of which the game recolours to the move's
own colour and one of which it leaves alone.

    onClipEvent(load){ my_color = new Color(this);
                       my_color.setRGB(_parent._parent.colortobe); }

`colortobe` is set on the model before it is told to play, so the layer that
carries that handler comes out in the colour of whatever was cast. Which layer
that is has to be recorded here, because nothing about the art says so.

    python3 tools/extract_cast.py SONNY1.swf > data/extracted/cast_frames.json
"""
import json
import struct
import sys

sys.path.insert(0, __file__.rsplit('/', 1)[0])
from swfinfo import read_swf                                  # noqa: E402
from swf_doll import (Bits, header_end, parse_place, read_matrix,  # noqa: E402
                      walk_tags,
                      TAG_PLACE_OBJECT2, TAG_PLACE_OBJECT3,
                      TAG_REMOVE_OBJECT, TAG_REMOVE_OBJECT2,
                      TAG_SHOW_FRAME, TAG_DEFINE_SPRITE)

# The three the model plays, by the animation that plays them, with whether
# the clip runs round again when it reaches the end. 932 carries a stop() on
# its last frame, which is blank, so the sweep plays once and is gone; 936 is
# a single frame; 939 has no stop, so the crackle keeps going for as long as
# the model is held stunned. None of that is visible in the display list, so
# it is recorded here from the clip's own actions.
EFFECTS = {'attack2': (932, False), 'cast': (936, False),
           'stun2': (939, True)}

# The layers carrying the setRGB(colortobe) handler, as the clip that holds
# them sees them. 929 wraps 928, which is where attack2's handler actually
# sits; the wrapper is what the effect clip places, so the wrapper is what is
# named here. stun2 has no such layer -- it is never recoloured.
TINTED = {932: 929, 936: 934}


def place_alpha(body, pos, tag):
    """The alpha multiplier on a placement, or None when it carries no colour
    transform. attack2 animates entirely through these -- its two layers never
    move -- so dropping them leaves the sweep on full for all twelve frames.
    swf_doll's parse_place steps over the transform without reading it, so the
    layout is walked again here for the one field that matters."""
    flags = body[pos]
    p = pos + 1
    if tag == TAG_PLACE_OBJECT3:
        p += 1
    p += 2                              # depth
    if flags & 2:
        p += 2                          # character
    if flags & 4:
        _, p = read_matrix(body, p)
    if not flags & 8:
        return None
    b = Bits(body, p)
    has_add = b.ub(1)
    has_mult = b.ub(1)
    nbits = b.ub(4)
    mult = [b.sb(nbits) for _ in range(4)] if has_mult else [256] * 4
    add = [b.sb(nbits) for _ in range(4)] if has_add else [0] * 4
    return round(mult[3] / 256.0 + add[3] / 255.0, 6)


def clip_frames(body, sprite_id):
    """[[{depth, character, matrix, tinted}]] -- one list per frame."""
    for tag, start, length in walk_tags(body, header_end(body)):
        if tag != TAG_DEFINE_SPRITE or length < 4:
            continue
        if struct.unpack_from('<H', body, start)[0] != sprite_id:
            continue
        depths, frames = {}, []
        for t2, s2, l2 in walk_tags(body, start + 4, start + length):
            if t2 in (TAG_PLACE_OBJECT2, TAG_PLACE_OBJECT3):
                depth, character, _, matrix, is_move, _ = parse_place(
                    body, s2, l2, t2)
                slot = depths.setdefault(depth, {})
                if character is not None and not is_move:
                    slot.clear()
                    slot['character'] = character
                if matrix:
                    slot['matrix'] = matrix
                alpha = place_alpha(body, s2, t2)
                if alpha is not None:
                    slot['alpha'] = alpha
            elif t2 in (TAG_REMOVE_OBJECT, TAG_REMOVE_OBJECT2):
                depth = struct.unpack_from(
                    '<H', body,
                    s2 + (2 if t2 == TAG_REMOVE_OBJECT else 0))[0]
                depths.pop(depth, None)
            elif t2 == TAG_SHOW_FRAME:
                frames.append([
                    {'depth': depth,
                     'character': depths[depth].get('character'),
                     'matrix': [round(v, 6)
                                for v in depths[depth]['matrix']],
                     'alpha': depths[depth].get('alpha', 1.0),
                     'tinted': int(depths[depth].get('character')
                                   == TINTED.get(sprite_id))}
                    for depth in sorted(depths)
                    if 'matrix' in depths[depth]
                    and depths[depth].get('character') is not None])
        return frames
    return []


def main(path):
    _, _, _, body = read_swf(path)
    out = {}
    for animation, (cid, loops) in EFFECTS.items():
        frames = clip_frames(body, cid)
        out[animation] = {'character': cid, 'loops': int(loops),
                          'frame_count': len(frames), 'frames': frames}
    json.dump(out, sys.stdout, indent=1)
    print()


if __name__ == '__main__':
    if len(sys.argv) != 2:
        raise SystemExit(__doc__)
    main(sys.argv[1])
