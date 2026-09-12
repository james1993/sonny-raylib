#!/usr/bin/env python3
"""Read a SWF's ExportAssets table: the character id -> name mapping.

This is the bridge between the engine and the art. The game refers to graphics
by export name -- attachMovie("BOOM_SLASHORANGE"), gotoAndStop("WHITE
NOVEMBER"), the equipment "looks" strings -- while the decompiler exports files
named by character id. Without this table there is no way to know which of the
800 exported shapes is the one an ability names.

    python3 tools/swf_exports.py SONNY1.swf > data/extracted/exports.json
"""
import json
import struct
import sys

sys.path.insert(0, __file__.rsplit('/', 1)[0])
from swfinfo import read_swf   # noqa: E402

TAG_END = 0
TAG_SHOW_FRAME = 1
TAG_PLACE_OBJECT2 = 26
TAG_PLACE_OBJECT3 = 70
TAG_DEFINE_SHAPES = (2, 22, 32, 83)
TAG_DEFINE_SOUND = 14
TAG_DEFINE_SPRITE = 39
TAG_FRAME_LABEL = 43
TAG_EXPORT_ASSETS = 56
TAG_SYMBOL_CLASS = 76


def walk_tags(body, pos, end=None):
    """Yield (tag, start, length) over a tag stream."""
    if end is None:
        end = len(body)
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


def read_rect(body, pos):
    """RECT -> (xmin, xmax, ymin, ymax) in pixels -- the SWF field order."""
    nbits = body[pos] >> 3
    total = 5 + 4 * nbits
    bitpos = pos * 8 + 5
    values = []
    for _ in range(4):
        v = 0
        for _ in range(nbits):
            byte = body[bitpos >> 3]
            v = (v << 1) | ((byte >> (7 - (bitpos & 7))) & 1)
            bitpos += 1
        if nbits and (v >> (nbits - 1)) & 1:
            v -= 1 << nbits
        values.append(v / 20.0)
    return values, pos + (total + 7) // 8


def read_matrix_offset(body, pos):
    """MATRIX -> ((a, b, c, d, tx, ty), next position), pixels for tx/ty."""
    nbits_pos = pos * 8
    def ub(n):
        nonlocal nbits_pos
        v = 0
        for _ in range(n):
            byte = body[nbits_pos >> 3]
            v = (v << 1) | ((byte >> (7 - (nbits_pos & 7))) & 1)
            nbits_pos += 1
        return v

    def sb(n):
        v = ub(n)
        if n and (v >> (n - 1)) & 1:
            v -= 1 << n
        return v

    a = d = 1.0
    b = c = 0.0
    if ub(1):
        n = ub(5)
        a = sb(n) / 65536.0
        d = sb(n) / 65536.0
    if ub(1):
        n = ub(5)
        b = sb(n) / 65536.0
        c = sb(n) / 65536.0
    n = ub(5)
    tx = sb(n) / 20.0
    ty = sb(n) / 20.0
    return (a, b, c, d, tx, ty), (nbits_pos + 7) // 8


def placed_character(body, pos, tag):
    """(character id, matrix) for a PlaceObject2/3 that places a character.

    The matrix matters as much as the id: a backdrop's art is positioned by
    the frame that places it, so without it the picture cannot be put where
    the game puts it."""
    flags = body[pos]
    p = pos + 1
    if tag == TAG_PLACE_OBJECT3:
        p += 1
    p += 2                      # depth
    if not (flags & 2):         # no character: this is a move, not a place
        return None, None
    cid = struct.unpack_from('<H', body, p)[0]
    p += 2
    matrix = None
    if flags & 4:
        matrix, _ = read_matrix_offset(body, p)
    return cid, matrix


def sprite_frame_labels(body, pos, length):
    """Label -> frame number inside one DefineSprite.

    The game selects art by frame label -- gotoAndStop("Blood Focus"),
    equip[i] = "CROWBAR", gotoAndStop(ZoneBG) -- so these labels are how an
    engine name maps to a picture. A FrameLabel applies to the frame the next
    ShowFrame completes, and frames are numbered from 1 to match how the
    decompiler names exported files.
    """
    labels = {}
    places = {}
    frame = 1
    pending = []
    frame_places = []
    for tag, start, tag_len in walk_tags(body, pos + 4, pos + length):
        if tag == TAG_FRAME_LABEL:
            label, _ = read_string(body, start)
            pending.append(label)
        elif tag in (TAG_PLACE_OBJECT2, TAG_PLACE_OBJECT3):
            cid, matrix = placed_character(body, start, tag)
            if cid is not None:
                frame_places.append({'character': cid,
                                     'matrix': ([round(v, 6) for v in matrix]
                                                if matrix else None)})
        elif tag == TAG_SHOW_FRAME:
            for label in pending:
                labels[label] = frame
            if frame_places:
                places[frame] = frame_places
            pending = []
            frame_places = []
            frame += 1
    return labels, places


def read_string(body, pos):
    end = body.index(b'\x00', pos)
    return body[pos:end].decode('utf-8', 'replace'), end + 1


def main(path):
    _, _, _, body = read_swf(path)

    # Skip the header rect to reach the tag stream.
    nbits = body[0] >> 3
    total_bits = 5 + 4 * nbits
    pos = (total_bits + 7) // 8 + 4

    exports = {}
    root_labels = {}
    shape_bounds = {}
    sprites, sounds, shapes = [], [], []
    sprite_labels = {}
    sprite_places = {}
    root_frame = 1
    pending = []

    for tag, start, length in walk_tags(body, pos):
        if tag in (TAG_EXPORT_ASSETS, TAG_SYMBOL_CLASS):
            count = struct.unpack_from('<H', body, start)[0]
            p = start + 2
            for _ in range(count):
                if p + 2 > start + length:
                    break
                cid = struct.unpack_from('<H', body, p)[0]
                p += 2
                name, p = read_string(body, p)
                exports[name] = cid
        elif tag == TAG_DEFINE_SPRITE and length >= 4:
            sprite_id = struct.unpack_from('<H', body, start)[0]
            sprites.append(sprite_id)
            labels, places = sprite_frame_labels(body, start, length)
            if labels:
                sprite_labels[sprite_id] = labels
                sprite_places[sprite_id] = places
        elif tag == TAG_DEFINE_SOUND and length >= 2:
            sounds.append(struct.unpack_from('<H', body, start)[0])
        elif tag in TAG_DEFINE_SHAPES and length >= 2:
            shape_id = struct.unpack_from('<H', body, start)[0]
            shapes.append(shape_id)
            # A shape's bounds say where its art sits relative to its own
            # origin. The decompiler exports PNGs trimmed to these bounds, so
            # without them a shape cannot be placed where the game places it.
            try:
                (xmin, xmax, ymin, ymax), _ = read_rect(body, start + 2)
                # Stored as (xmin, ymin, xmax, ymax), which is the order
                # everything downstream wants.
                shape_bounds[shape_id] = [round(xmin, 3), round(ymin, 3),
                                          round(xmax, 3), round(ymax, 3)]
            except (IndexError, struct.error):
                pass
        elif tag == TAG_FRAME_LABEL and length >= 1:
            label, _ = read_string(body, start)
            pending.append(label)
        elif tag == TAG_SHOW_FRAME:
            for label in pending:
                root_labels[label] = root_frame
            pending = []
            root_frame += 1

    # A flat label -> [(sprite id, frame, [characters placed on that frame])]
    # index. The same label (an equipment look, an ability icon) appears in
    # several sprites, and the characters a labelled frame places matter: a
    # backdrop frame places one big shape, and that shape on its own is the
    # art -- rendering the containing frame instead also picks up whatever
    # persists from earlier frames, which for the battle screen means
    # design-time unit placeholders the game overwrites at runtime.
    by_label = {}
    for sprite_id, labels in sprite_labels.items():
        for label, frame in labels.items():
            placed = (sprite_places.get(sprite_id) or {}).get(frame, [])
            by_label.setdefault(label, []).append([sprite_id, frame, placed])

    out = {
        'exports': dict(sorted(exports.items())),
        'sprite_ids': sprites,
        'sound_ids': sounds,
        'shape_ids': shapes,
        'shape_bounds': {str(k): v for k, v in sorted(shape_bounds.items())},
        'root_frame_labels': root_labels,
        'sprite_frame_labels': {str(k): v for k, v in sorted(sprite_labels.items())},
        'label_index': dict(sorted(by_label.items())),
    }
    json.dump(out, sys.stdout, indent=1, ensure_ascii=False)
    print()


if __name__ == '__main__':
    if len(sys.argv) != 2:
        raise SystemExit(__doc__)
    main(sys.argv[1])
