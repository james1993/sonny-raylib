#!/usr/bin/env python3
"""Extract the character model's per-frame part transforms.

The game builds a character by attaching art into named instances inside the
model sprite -- inner[head], inner[chest], inner[weapon1] and so on -- so the
model's own timeline is what positions every piece. Reproducing the doll means
knowing each named instance's matrix on each frame, which is only in the
display list.

This walks the model's timeline the way a player would: PlaceObject2/3 creates
or moves the object at a depth, RemoveObject2 clears it, and objects persist
across frames until changed. Each ShowFrame snapshots the state.

    python3 tools/swf_doll.py SONNY1.swf MODEL1 > data/extracted/doll_frames.json
"""
import json
import struct
import sys

sys.path.insert(0, __file__.rsplit('/', 1)[0])
from swfinfo import read_swf              # noqa: E402
from swf_exports import (walk_tags, read_string, TAG_SHOW_FRAME,   # noqa: E402
                         TAG_DEFINE_SPRITE, TAG_EXPORT_ASSETS,
                         TAG_SYMBOL_CLASS)

TAG_PLACE_OBJECT2 = 26
TAG_PLACE_OBJECT3 = 70
TAG_REMOVE_OBJECT = 5
TAG_REMOVE_OBJECT2 = 28
TWIPS = 20.0


class Bits:
    def __init__(self, data, pos):
        self.data, self.pos, self.bit = data, pos, 0

    def ub(self, n):
        v = 0
        for _ in range(n):
            byte = self.data[self.pos]
            v = (v << 1) | ((byte >> (7 - self.bit)) & 1)
            self.bit += 1
            if self.bit == 8:
                self.bit, self.pos = 0, self.pos + 1
        return v

    def sb(self, n):
        v = self.ub(n)
        if n and (v >> (n - 1)) & 1:
            v -= 1 << n
        return v

    def fb(self, n):
        """Fixed 16.16 signed."""
        return self.sb(n) / 65536.0

    def align(self):
        if self.bit:
            self.bit, self.pos = 0, self.pos + 1
        return self.pos


def read_matrix(body, pos):
    """MATRIX -> (a, b, c, d, tx, ty) with translation in pixels."""
    b = Bits(body, pos)
    a = d = 1.0
    rot0 = rot1 = 0.0
    if b.ub(1):
        n = b.ub(5)
        a = b.fb(n)
        d = b.fb(n)
    if b.ub(1):
        n = b.ub(5)
        rot0 = b.fb(n)
        rot1 = b.fb(n)
    n = b.ub(5)
    tx = b.sb(n) / TWIPS
    ty = b.sb(n) / TWIPS
    return (a, rot0, rot1, d, tx, ty), b.align()


def parse_place(body, pos, length, tag):
    """-> (depth, character, name, matrix, is_move, clip_depth)

    clip_depth is set when the placement is a mask: the object is not drawn,
    it clips everything placed above it up to that depth. The battle screen
    uses one to keep the backdrop inside the battlefield frame."""
    flags = body[pos]
    p = pos + 1
    if tag == TAG_PLACE_OBJECT3:
        p += 1          # extended flags
    depth = struct.unpack_from('<H', body, p)[0]
    p += 2

    is_move = bool(flags & 1)
    character = None
    matrix = None
    name = None
    clip_depth = None

    if flags & 2:
        character = struct.unpack_from('<H', body, p)[0]
        p += 2
    if flags & 4:
        matrix, p = read_matrix(body, p)
    if flags & 8:
        # CXFORMWITHALPHA: skip it by parsing its bit layout.
        b = Bits(body, p)
        has_add = b.ub(1)
        has_mult = b.ub(1)
        nbits = b.ub(4)
        if has_mult:
            for _ in range(4):
                b.sb(nbits)
        if has_add:
            for _ in range(4):
                b.sb(nbits)
        p = b.align()
    if flags & 16:
        p += 2          # ratio
    if flags & 32:
        name, p = read_string(body, p)
    if flags & 64:
        clip_depth = struct.unpack_from('<H', body, p)[0]
        p += 2
    return depth, character, name, matrix, is_move, clip_depth


def timeline_frames(body, pos, end):
    """Walk a tag stream as a player would, snapshotting named instances."""
    depths = {}
    frames = []
    for t2, s2, l2 in walk_tags(body, pos, end):
        if t2 in (TAG_PLACE_OBJECT2, TAG_PLACE_OBJECT3):
            depth, character, name, matrix, is_move, clip_depth = parse_place(
                body, s2, l2, t2)
            slot = depths.setdefault(depth, {})
            if character is not None and not is_move:
                slot.clear()
                slot['character'] = character
            if name:
                slot['name'] = name
            if matrix:
                slot['matrix'] = matrix
            if clip_depth is not None:
                slot['clip_depth'] = clip_depth
        elif t2 in (TAG_REMOVE_OBJECT, TAG_REMOVE_OBJECT2):
            depth = struct.unpack_from(
                '<H', body, s2 + (2 if t2 == TAG_REMOVE_OBJECT else 0))[0]
            depths.pop(depth, None)
        elif t2 == TAG_SHOW_FRAME:
            snapshot = {}
            for depth in sorted(depths):
                slot = depths[depth]
                if 'matrix' not in slot:
                    continue
                # Named instances key by name; everything else by depth, so a
                # backdrop placed without a name is still locatable.
                key = slot.get('name') or ('@%d' % depth)
                snapshot[key] = {
                    'depth': depth,
                    'matrix': [round(v, 6) for v in slot['matrix']],
                    'character': slot.get('character'),
                }
                if slot.get('clip_depth') is not None:
                    snapshot[key]['clip_depth'] = slot['clip_depth']
            frames.append(snapshot)
    return frames


def model_frames(body, sprite_id):
    """[{name: [a, b, c, d, tx, ty]}] per frame, for named instances."""
    if sprite_id == 0:      # the root timeline
        return timeline_frames(body, header_end(body), len(body))
    for tag, start, length in walk_tags(body, header_end(body)):
        if tag != TAG_DEFINE_SPRITE or length < 4:
            continue
        if struct.unpack_from('<H', body, start)[0] != sprite_id:
            continue

        return timeline_frames(body, start + 4, start + length)
    return []


def header_end(body):
    nbits = body[0] >> 3
    return ((5 + 4 * nbits) + 7) // 8 + 4


def exports_table(body):
    names = {}
    for tag, start, length in walk_tags(body, header_end(body)):
        if tag in (TAG_EXPORT_ASSETS, TAG_SYMBOL_CLASS):
            count = struct.unpack_from('<H', body, start)[0]
            p = start + 2
            for _ in range(count):
                cid = struct.unpack_from('<H', body, p)[0]
                p += 2
                name, p = read_string(body, p)
                names[name] = cid
    return names


def main(path, export_name):
    _, _, _, body = read_swf(path)
    names = exports_table(body)
    if export_name.isdigit():
        character = int(export_name)
    elif export_name in names:
        character = names[export_name]
    else:
        raise SystemExit('no export named %s' % export_name)

    frames = model_frames(body, character)
    instances = sorted({name for frame in frames for name in frame})
    json.dump({'model': export_name, 'character': character,
               'frame_count': len(frames), 'instances': instances,
               'frames': frames}, sys.stdout, indent=1)
    print()


if __name__ == '__main__':
    if len(sys.argv) != 3:
        raise SystemExit(__doc__)
    main(sys.argv[1], sys.argv[2])
