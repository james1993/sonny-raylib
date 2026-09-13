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
import struct
import sys

# The root <g> of an exported SVG says where the art's own origin sits inside
# its canvas -- for the bar, its centre rather than a corner.
SVG_ROOT_TRANSFORM = re.compile(
    r'<g transform="matrix\(([-0-9.eE]+), *([-0-9.eE]+), *([-0-9.eE]+), *'
    r'([-0-9.eE]+), *([-0-9.eE]+), *([-0-9.eE]+)\)"')


def sprite_geometry(raw_dir, character):
    """(width, height, origin x, origin y) of an exported sprite frame.

    Falls back to the shape export, because the battle screen's furniture is a
    mix of the two and both carry the same root transform."""
    if not raw_dir:
        return None
    import glob
    matches = glob.glob(os.path.join(raw_dir, 'sprite_svg',
                                     'DefineSprite_%d' % character, '1.svg'))
    matches += glob.glob(os.path.join(raw_dir, 'sprite_svg',
                                      'DefineSprite_%d_*' % character, '1.svg'))
    matches += glob.glob(os.path.join(raw_dir, 'shape', '%d.svg' % character))
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
from swf_doll import model_frames, header_end      # noqa: E402
from swf_exports import (walk_tags, read_string,   # noqa: E402
                         TAG_SHOW_FRAME, TAG_FRAME_LABEL,
                         TAG_DEFINE_SPRITE, TAG_DEFINE_SHAPES)
from swf_doll import (Bits, parse_place, read_matrix,  # noqa: E402
                      TAG_PLACE_OBJECT2, TAG_PLACE_OBJECT3,
                      TAG_REMOVE_OBJECT, TAG_REMOVE_OBJECT2)

TAG_DEFINE_EDIT_TEXT = 37
TAG_DEFINE_FONT2 = 48
TAG_DEFINE_FONT3 = 75
TAG_DEFINE_BUTTON2 = 34

BATTLE_SCREEN = 'BATTLESCREEN'
# The root frame the game stops on during a fight.
BATTLE_FRAME_LABEL = 'KRINBATTLESCENE'
# The root frames worth recording: each is a screen the game stops on, and
# its display list is that screen's layout.
SCREEN_FRAMES = ('mainMenu', 'KRINBATTLESCENE', 'Navigation', 'winCombat',
                 'loseCombat', 'overMap')
# A sky the battle table names, so the sky container can be told apart from
# the rest of the furniture by what its timeline is labelled with.
SKY_FRAME = 'NIGHT'
# The health/focus bar widget, and the flat-colour clip the bar points at
# round(percent * 100) to colour its fill.
BAR_CHARACTER = 1585
RAMP_CHARACTER = 91
# The ability ring the game parks on the hovered unit.
SELECTOR_INSTANCE = 'selector'
# The speech box, which the game slides to the speaker's side of the screen.
SPEECH_INSTANCE = 'combatScript'
# The clip that carries every screen that is not the fight, one frame each.
MENU_INSTANCE = 'KRINMENU'
# The zone scene, and the clip repeated on it as the fight markers.
ZONE_INSTANCE = 'KrinScreen'
# How far inside a screen's clips to look for buttons.
BUTTON_DEPTH = 4
MARKER_CHARACTER = 1207
# How far to look inside a clip for its text fields.
TEXT_FIELD_DEPTH = 3
# Inside the orb, the icon is whatever the frame places in this depth range.
ICON_DEPTH = (4, 6)
# The bar's name field is much wider than its number fields.
NAME_FIELD_WIDTH = 60


def sprite_labels(body, sprite_id):
    """The frame labels on one sprite's own timeline."""
    for tag, start, length in walk_tags(body, header_end(body)):
        if tag != TAG_DEFINE_SPRITE or length < 4:
            continue
        if struct.unpack_from('<H', body, start)[0] != sprite_id:
            continue
        out = []
        for t2, s2, l2 in walk_tags(body, start + 4, start + length):
            if t2 == TAG_FRAME_LABEL and l2 >= 1:
                out.append(read_string(body, s2)[0])
        return out
    return []


def root_label_frame(body, label):
    """The 1-based root frame a label names."""
    frame = 1
    pending = []
    for tag, start, length in walk_tags(body, header_end(body)):
        if tag == TAG_FRAME_LABEL and length >= 1:
            pending.append(read_string(body, start)[0])
        elif tag == TAG_SHOW_FRAME:
            if label in pending:
                return frame
            pending = []
            frame += 1
    raise SystemExit('no root frame labelled %s' % label)


def font_table(body):
    """id -> (name, glyph count) for every font in the file.

    A font with no glyphs is one of Flash's device fonts -- "_sans" and the
    like -- which the player renders with a system face rather than with
    anything in the SWF. Knowing which fields use one is the difference
    between drawing the game's own Tahoma and drawing what the original
    actually showed."""
    out = {}
    for tag, start, length in walk_tags(body, header_end(body)):
        if tag not in (TAG_DEFINE_FONT2, TAG_DEFINE_FONT3) or length < 6:
            continue
        cid = struct.unpack_from('<H', body, start)[0]
        name_length = body[start + 4]
        name = body[start + 5:start + 5 + name_length].decode('latin1')
        glyphs = struct.unpack_from('<H', body, start + 5 + name_length)[0]
        out[cid] = (name.rstrip('\x00'), glyphs)
    return out


def edit_text_boxes(body):
    """Every DefineEditText's box and colour, by character id.

    The bar's numbers and names are text fields, so where each one sits and
    what colour it is only exists in these tags."""
    out = {}
    for tag, start, length in walk_tags(body, header_end(body)):
        if tag != TAG_DEFINE_EDIT_TEXT or length < 3:
            continue
        cid = struct.unpack_from('<H', body, start)[0]
        bits = Bits(body, start + 2)
        n = bits.ub(5)
        # A SWF RECT is Xmin, Xmax, Ymin, Ymax -- not the corner pairs the
        # order reads like.
        xmin, xmax, ymin, ymax = (bits.sb(n) / 20.0 for _ in range(4))
        p = bits.align()
        f1, f2 = body[p], body[p + 1]
        p += 2
        height = None
        color = None
        align = 0
        font = None
        if f2 & 0x80:                    # HasFontClass
            _, p = read_string(body, p)
            height = struct.unpack_from('<H', body, p)[0] / 20.0
            p += 2
        elif f1 & 0x01:                  # HasFont
            font = struct.unpack_from('<H', body, p)[0]
            height = struct.unpack_from('<H', body, p + 2)[0] / 20.0
            p += 4
        if f1 & 0x04:                    # HasTextColor
            color = list(body[p:p + 4])
            p += 4
        if f1 & 0x02:                    # HasMaxLength
            p += 2
        leading = 0.0
        if f2 & 0x20:                    # HasLayout: 0 left, 1 right, 2 centre
            align = body[p]
            # align, left and right margin, indent, then the leading, which
            # is the gap the player puts above the first line as well as
            # between lines.
            leading = struct.unpack_from('<h', body, p + 7)[0] / 20.0
            p += 9
        _, p = read_string(body, p)      # variable name
        initial = ''
        if f1 & 0x80:                    # HasText: a label the game never sets
            initial = read_string(body, p)[0]
        out[cid] = {'xmin': round(xmin, 3), 'ymin': round(ymin, 3),
                    'xmax': round(xmax, 3), 'ymax': round(ymax, 3),
                    'height': height, 'color': color, 'align': align,
                    'leading': leading, 'font': font, 'text': initial}
    return out


def bar_widget(body, raw_dir, texts, fonts):
    """The health/focus bar taken apart.

    One sprite holds two children: inner2, the graphics -- a black panel, the
    two fills, and a translucent gloss over them -- and inner, the text, whose
    frame 1 is the left-hand team's layout and frame 2 ("second") the
    right-hand team's mirror of it. The right-hand bars set
    inner2._xscale = -100 and inner.gotoAndStop("second"), so one widget
    serves both sides."""
    frames = model_frames(body, BAR_CHARACTER)
    if not frames:
        return None
    top = frames[0]
    widget = {'character': BAR_CHARACTER, 'graphics': [], 'text': {}}
    for key, info in top.items():
        name = info.get('name') or key
        a, _, _, d, x, y = info['matrix']
        if name == 'inner2':
            for part, pinfo in sorted(model_frames(body, info['character'])[0].items(),
                                      key=lambda kv: kv[1]['depth']):
                pa, _, _, pd, px, py = pinfo['matrix']
                entry = {'name': part, 'character': pinfo['character'],
                         'depth': pinfo['depth'],
                         # A mask rather than a picture: it stops the fill
                         # overflowing into the maximum's box.
                         'clip_depth': pinfo.get('clip_depth') or 0,
                         'x': round(x + a * px, 3), 'y': round(y + d * py, 3),
                         'scale_x': round(a * pa, 6), 'scale_y': round(d * pd, 6)}
                geom = sprite_geometry(raw_dir, pinfo['character'])
                if geom:
                    (entry['width'], entry['height'],
                     entry['origin_x'], entry['origin_y']) = geom
                widget['graphics'].append(entry)
        elif name == 'inner':
            inner = model_frames(body, info['character'])
            for index, label in ((0, 'left'), (1, 'right')):
                fields = []
                # The two number rows, by where the widget places them:
                # life above, focus below.
                rows = sorted({round(f['matrix'][5], 2)
                               for f in inner[index].values()
                               if f['character'] in texts
                               and texts[f['character']]['xmax']
                               - texts[f['character']]['xmin']
                               <= NAME_FIELD_WIDTH})
                for _, finfo in sorted(inner[index].items(),
                                       key=lambda kv: kv[1]['depth']):
                    box = texts.get(finfo['character'])
                    if not box:
                        continue
                    fa, _, _, fd, fx, fy = finfo['matrix']
                    # What each field is, from the widget itself rather than
                    # from a table: the wide one is the name, and of the
                    # narrow ones the grey are the maxima and the white the
                    # current values, on the upper row for life and the lower
                    # for focus.
                    if box['xmax'] - box['xmin'] > NAME_FIELD_WIDTH:
                        role = 'name'
                    else:
                        stat = ('life' if round(fy, 2) == rows[0]
                                else 'focus')
                        role = stat + ('Max' if box['color'][:3] != [255, 255, 255]
                                       else 'Now')
                    fields.append({'role': role,
                                   'character': finfo['character'],
                                   'depth': finfo['depth'],
                                   'x': round(x + a * (fx + fa * box['xmin']), 3),
                                   'y': round(y + d * (fy + fd * box['ymin']), 3),
                                   'width': round(a * fa * (box['xmax'] - box['xmin']), 3),
                                   'height': round(d * fd * (box['ymax'] - box['ymin']), 3),
                                   'size': box['height'],
                                   'align': box['align'],
                                   'leading': round(d * fd * box['leading'], 3),
                                   'text': box['text'],
                                   'font': (fonts.get(box['font']) or ('', 0))[0],
                                   'device': (fonts.get(box['font'])
                                              or ('', 0))[1] == 0,
                                   'color': box['color']})
                widget['text'][label] = fields
    return widget


def selector_ring(body, raw_dir, chrome):
    """The ability ring.

    The player does not choose a move from a bar along the bottom. Hovering a
    unit brings up a ring of eight orbs around it -- one per slot of the
    player's own ability loadout -- and clicking one uses that ability on that
    target. The ring is a single clip the game moves onto whichever unit is
    under the pointer, and its eight orbs sit at fixed offsets inside it."""
    placed = next((c for c in chrome if c['name'] == SELECTOR_INSTANCE), None)
    if not placed:
        return None
    slots = []
    orb = None
    for name, info in model_frames(body, placed['character'])[0].items():
        if not name.startswith('thing') or not name[5:].isdigit():
            continue
        a, _, _, d, x, y = info['matrix']
        orb = info['character']
        slots.append({'slot': int(name[5:]),
                      'x': round(x, 3), 'y': round(y, 3),
                      'scale_x': round(a, 6), 'scale_y': round(d, 6)})
    if not slots:
        return None
    geom = sprite_geometry(raw_dir, orb)
    ring = {'character': placed['character'], 'orb': orb,
            'scale_x': placed['scale_x'], 'scale_y': placed['scale_y'],
            'slots': sorted(slots, key=lambda s: s['slot'])}
    if geom:
        ring['width'], ring['height'], ring['origin_x'], ring['origin_y'] = geom
    ring.update(orb_parts(body, raw_dir, orb))
    return ring


def orb_parts(body, raw_dir, orb):
    """The orb taken apart.

    One clip serves every ability: its frames are labelled by icon name, and a
    frame only swaps the icon. Everything else is shared -- a ball whose colour
    the game replaces once a move is chosen, a circular mask that cuts the icon
    to the ball, a glass highlight over it, and a black disc shown while the
    ability cannot be used. Exporting a labelled frame whole is no use, because
    that black disc is in front of all of it, so the pieces are taken
    separately and the screen puts them back together.

    The display list has to be walked rather than read off one frame: the icon
    lives at either of two depths depending on the ability, and the ball is
    swapped out again further along the timeline."""
    live = {}
    icons = {}
    shared = {}
    frame = 1
    pending = []
    for tag, start, length in walk_tags(body, header_end(body)):
        if tag != TAG_DEFINE_SPRITE or length < 4:
            continue
        if struct.unpack_from('<H', body, start)[0] != orb:
            continue
        for t2, s2, l2 in walk_tags(body, start + 4, start + length):
            if t2 in (TAG_PLACE_OBJECT2, TAG_PLACE_OBJECT3):
                depth, character, name, matrix, is_move, clip = parse_place(
                    body, s2, l2, t2)
                slot = live.setdefault(depth, {})
                if character is not None:
                    # A place with a character replaces whatever was at the
                    # depth, whether or not it is flagged as a move; the orb
                    # swaps its icon that way.
                    if not is_move:
                        slot.clear()
                    slot['character'] = character
                if name:
                    slot['name'] = name
                if matrix:
                    slot['matrix'] = matrix
                slot['clip'] = clip
            elif t2 in (TAG_REMOVE_OBJECT, TAG_REMOVE_OBJECT2):
                depth = struct.unpack_from(
                    '<H', body, s2 + (2 if t2 == TAG_REMOVE_OBJECT else 0))[0]
                live.pop(depth, None)
            elif t2 == TAG_FRAME_LABEL and l2 >= 1:
                pending.append(read_string(body, s2)[0])
            elif t2 == TAG_SHOW_FRAME:
                icon = max((d for d in live
                            if ICON_DEPTH[0] <= d <= ICON_DEPTH[1]),
                           default=None)
                for label in pending:
                    if icon is not None:
                        icons[label] = live[icon]['character']
                for depth, info in live.items():
                    if ICON_DEPTH[0] <= depth <= ICON_DEPTH[1]:
                        continue
                    role = ('mask' if info.get('clip') else
                            'ball' if info.get('name') == 'moveColor' else
                            'filter' if info.get('name') == 'bfilter' else
                            'glass' if depth > ICON_DEPTH[1] else None)
                    if role and role not in shared:
                        shared[role] = (depth, dict(info))
                pending = []
                frame += 1
        break

    parts = {}
    for role, (depth, info) in sorted(shared.items()):
        a, _, _, d, x, y = info.get('matrix') or (1, 0, 0, 1, 0, 0)
        entry = {'character': info['character'], 'depth': depth,
                 'x': round(x, 3), 'y': round(y, 3),
                 'scale_x': round(a, 6), 'scale_y': round(d, 6)}
        geom = sprite_geometry(raw_dir, info['character'])
        if geom:
            (entry['width'], entry['height'],
             entry['origin_x'], entry['origin_y']) = geom
        parts[role] = entry
    return {'parts': parts, 'icons': icons}


def text_fields(body, character, texts, fonts, at=(1.0, 1.0, 0.0, 0.0),
                depth=0, frame=0):
    """Every text field under a clip, with its box in that clip's own
    coordinates. Fields are often a container or two down, so this composes
    the transforms on the way. `frame` picks which of the clip's own frames to
    read, for a clip that is a screen per frame."""
    if depth > TEXT_FIELD_DEPTH:
        return []
    sx, sy, tx, ty = at
    found = []
    frames = model_frames(body, character)
    if frame >= len(frames):
        return []
    for name, info in sorted(frames[frame].items(),
                             key=lambda kv: kv[1]['depth']):
        a, _, _, d, x, y = info['matrix']
        here = (sx * a, sy * d, tx + sx * x, ty + sy * y)
        box = texts.get(info['character'])
        if box:
            found.append({'name': name, 'depth': info['depth'],
                          'x': round(here[2] + here[0] * box['xmin'], 3),
                          'y': round(here[3] + here[1] * box['ymin'], 3),
                          'width': round(here[0] * (box['xmax'] - box['xmin']), 3),
                          'height': round(here[1] * (box['ymax'] - box['ymin']), 3),
                          'size': box['height'], 'align': box['align'],
                          'leading': box['leading'],
                          'text': box['text'],
                          'font': (fonts.get(box['font']) or ('', 0))[0],
                          'device': (fonts.get(box['font']) or ('', 0))[1] == 0,
                          'color': box['color']})
            continue
        found.extend(text_fields(body, info['character'], texts, fonts, here,
                                 depth + 1))
    return found


def graphic_parts(body, raw_dir, character, texts, at=(1.0, 1.0, 0.0, 0.0),
                  depth=0, frame=0, skip=()):
    """Every drawable piece under a clip, with its transform composed.

    A clip cannot be exported whole when it carries text fields, because the
    decompiler bakes their design-time contents into the picture. Taking the
    graphics separately and drawing the text over them is what keeps the
    placeholder copy out."""
    if depth > TEXT_FIELD_DEPTH:
        return []
    sx, sy, tx, ty = at
    found = []
    frames = model_frames(body, character)
    if frame >= len(frames):
        return []
    for name, info in sorted(frames[frame].items(),
                             key=lambda kv: kv[1]['depth']):
        if info['character'] in texts:
            continue
        # A named slot the engine fills itself -- a bag square, a drop square.
        # The original hides an empty one, so its art is not the screen's.
        if name in skip:
            continue
        a, _, _, d, x, y = info['matrix']
        here = (sx * a, sy * d, tx + sx * x, ty + sy * y)
        # A clip with more than one frame is one the game points at a frame
        # of, by name -- the speaker's portrait, say -- so it stays one piece
        # rather than being flattened into whatever its first frame holds.
        child = model_frames(body, info['character'])
        if len(child) <= 1:
            inner = graphic_parts(body, raw_dir, info['character'], texts,
                                  here, depth + 1, 0, skip)
            if inner:
                found.extend(inner)
                continue
        geom = sprite_geometry(raw_dir, info['character'])
        entry = {'name': name, 'depth': info['depth'],
                 'character': info['character'],
                 'x': round(here[2], 3), 'y': round(here[3], 3),
                 'scale_x': round(here[0], 6), 'scale_y': round(here[1], 6)}
        if geom:
            (entry['width'], entry['height'],
             entry['origin_x'], entry['origin_y']) = geom
        if len(child) > 1:
            entry['frames'] = len(child)
        found.append(entry)
    return found


def menu_frames(body, raw_dir, chrome, texts, fonts):
    """The menu, taken apart a screen at a time.

    One clip carries every screen that is not the fight: the inventory, the
    victory tally, the item store, the ability tree, the save slots and the
    settings, one labelled frame each. Its placement on the hub gives them all
    stage coordinates, and each frame's own graphics, text fields and named
    slots are what that screen is made of."""
    placed = next((c for c in chrome if c['name'] == MENU_INSTANCE), None)
    if not placed:
        return {}
    at = (placed['scale_x'], placed['scale_y'], placed['x'], placed['y'])
    frames = model_frames(body, placed['character'])
    out = {}
    for label, frame in sprite_frame_labels(body, placed['character']).items():
        index = frame - 1
        if index >= len(frames):
            continue
        slots = {}
        for name, info in frames[index].items():
            if name.startswith('@'):
                continue
            a, _, _, d, x, y = info['matrix']
            entry = {'x': round(at[2] + at[0] * x, 3),
                     'y': round(at[3] + at[1] * y, 3),
                     'scale': round(at[0] * a, 6),
                     'character': info['character']}
            geom = sprite_geometry(raw_dir, info['character'])
            if geom:
                (entry['width'], entry['height'],
                 entry['origin_x'], entry['origin_y']) = geom
            slots[name] = entry
        out[label] = {
            'frame': frame,
            'slots': slots,
            'parts': graphic_parts(body, raw_dir, placed['character'], texts,
                                   at, 0, index, set(slots)),
            'fields': text_fields(body, placed['character'], texts, fonts,
                                  at, 0, index),
        }
    return out


def sprite_frame_labels(body, sprite_id):
    """label -> 1-based frame, for one sprite's own timeline."""
    out = {}
    for tag, start, length in walk_tags(body, header_end(body)):
        if tag != TAG_DEFINE_SPRITE or length < 4:
            continue
        if struct.unpack_from('<H', body, start)[0] != sprite_id:
            continue
        frame = 1
        pending = []
        for t2, s2, l2 in walk_tags(body, start + 4, start + length):
            if t2 == TAG_FRAME_LABEL and l2 >= 1:
                pending.append(read_string(body, s2)[0])
            elif t2 == TAG_SHOW_FRAME:
                for label in pending:
                    out[label] = frame
                pending = []
                frame += 1
        break
    return out


def buttons_in(body, character, boxes, at=(1.0, 1.0, 0.0, 0.0), depth=0,
               frame=0):
    """Every button under a clip, with the box it responds in, composed into
    stage coordinates. Buttons sit wherever the screen puts them -- on the
    scene, inside a panel, a container or two down -- so this walks in."""
    if depth > BUTTON_DEPTH:
        return []
    sx, sy, tx, ty = at
    found = []
    frames = model_frames(body, character)
    if frame >= len(frames):
        return []
    for name, info in sorted(frames[frame].items(),
                             key=lambda kv: kv[1]['depth']):
        a, _, _, d, x, y = info['matrix']
        here = (sx * a, sy * d, tx + sx * x, ty + sy * y)
        box = boxes.get(info['character'])
        if box:
            found.append({
                'name': name, 'character': info['character'],
                'x': round(here[2] + here[0] * box[0], 3),
                'y': round(here[3] + here[1] * box[1], 3),
                'width': round(abs(here[0]) * (box[2] - box[0]), 3),
                'height': round(abs(here[1]) * (box[3] - box[1]), 3)})
            continue
        found.extend(buttons_in(body, info['character'], boxes, here,
                                depth + 1))
    return found


def shape_bounds(body):
    """id -> [xmin, ymin, xmax, ymax] for every shape in the file."""
    out = {}
    for tag, start, length in walk_tags(body, header_end(body)):
        if tag not in TAG_DEFINE_SHAPES or length < 2:
            continue
        cid = struct.unpack_from('<H', body, start)[0]
        bits = Bits(body, start + 2)
        n = bits.ub(5)
        xmin, xmax, ymin, ymax = (bits.sb(n) / 20.0 for _ in range(4))
        out[str(cid)] = [round(xmin, 3), round(ymin, 3),
                         round(xmax, 3), round(ymax, 3)]
    return out


def skip_filter_list(body, pos):
    """Step over a button record's filter list.

    Every filter is a type byte and a fixed payload, except the two gradient
    ones, which carry a colour ramp first."""
    count = body[pos]
    pos += 1
    sizes = {0: 23, 1: 9, 2: 15, 3: 27, 6: 80}
    for _ in range(count):
        kind = body[pos]
        pos += 1
        if kind in sizes:
            pos += sizes[kind]
        elif kind in (4, 7):                 # gradient glow, gradient bevel
            colours = body[pos]
            pos += 1 + colours * 5 + 23
        elif kind == 5:                      # convolution
            mx, my = body[pos], body[pos + 1]
            pos += 2 + 4 + 4 + 4 * mx * my + 4 + 1
        else:
            raise ValueError('unknown filter %d' % kind)
    return pos


def button_boxes(body, shape_bounds):
    """id -> the box a button responds in, in its own coordinates.

    A DefineButton2 is a list of state records, each placing a character in
    some of the up/over/down/hit states. The hit state is the one that decides
    where the pointer counts as being on the button; when a button gives none,
    the states it does give stand in for it."""
    out = {}
    for tag, start, length in walk_tags(body, header_end(body)):
        if tag != TAG_DEFINE_BUTTON2 or length < 6:
            continue
        button = struct.unpack_from('<H', body, start)[0]
        # id (2), the flags byte, then the action offset (2).
        pos = start + 5
        hit = []
        any_state = []
        try:
            while pos < start + length:
                flags = body[pos]
                if flags == 0:               # end of the record list
                    break
                pos += 1
                character = struct.unpack_from('<H', body, pos)[0]
                pos += 4                     # character, depth
                matrix, pos = read_matrix(body, pos)
                # DefineButton2 always carries a colour transform.
                bits = Bits(body, pos)
                has_add, has_mult = bits.ub(1), bits.ub(1)
                nbits = bits.ub(4)
                for _ in range(4 * (1 if has_mult else 0)
                               + 4 * (1 if has_add else 0)):
                    bits.sb(nbits)
                pos = bits.align()
                if flags & 0x10:             # has a filter list
                    pos = skip_filter_list(body, pos)
                if flags & 0x20:             # has a blend mode
                    pos += 1
                box = placed_box(shape_bounds.get(str(character)), matrix)
                if box:
                    any_state.append(box)
                    if flags & 0x08:         # the hit state
                        hit.append(box)
        except (IndexError, ValueError, struct.error):
            continue
        boxes = hit or any_state
        if boxes:
            out[button] = [round(min(b[0] for b in boxes), 3),
                           round(min(b[1] for b in boxes), 3),
                           round(max(b[2] for b in boxes), 3),
                           round(max(b[3] for b in boxes), 3)]
    return out


def placed_box(bounds, matrix):
    """A shape's bounds with a placement matrix applied."""
    if not bounds:
        return None
    xmin, ymin, xmax, ymax = bounds
    a, b, c, d, tx, ty = matrix
    corners = [(a * x + c * y + tx, b * x + d * y + ty)
               for x, y in ((xmin, ymin), (xmax, ymin),
                            (xmin, ymax), (xmax, ymax))]
    xs = [p[0] for p in corners]
    ys = [p[1] for p in corners]
    return (min(xs), min(ys), max(xs), max(ys))


def zone_screen(body, raw_dir, chrome):
    """The scene the hub is built around.

    One clip holds a frame per zone -- the deck of the research ship, the
    plains, and so on -- and on each are the markers the player clicks to pick
    a fight. The frames are labelled with the zone, and the markers are the
    repeated instances of one clip, so both come off the timeline."""
    placed = next((c for c in chrome if c['name'] == ZONE_INSTANCE), None)
    if not placed:
        return None
    labels = sprite_frame_labels(body, placed['character'])
    frames = model_frames(body, placed['character'])
    markers = {}
    counts = {}
    for label, frame in sorted(labels.items(), key=lambda kv: kv[1]):
        index = frame - 1
        if index >= len(frames):
            continue
        for name, info in frames[index].items():
            counts.setdefault(info['character'], []).append(1)
        found = []
        for name, info in sorted(frames[index].items(),
                                 key=lambda kv: kv[1]['depth']):
            a, _, _, d, x, y = info['matrix']
            found.append({'name': name, 'character': info['character'],
                          'x': round(placed['x'] + placed['scale_x'] * x, 3),
                          'y': round(placed['y'] + placed['scale_y'] * y, 3)})
        markers[label] = [f for f in found
                          if f['character'] == MARKER_CHARACTER]
    return {'x': placed['x'], 'y': placed['y'],
            'scale_x': placed['scale_x'], 'scale_y': placed['scale_y'],
            'character': placed['character'],
            'labels': labels, 'markers': markers}


def speech_box(body, raw_dir, chrome, texts, fonts):
    """The box that carries what a character is saying.

    The original places it once, on the right-hand side, and slides it over to
    whichever side the speaker is on. Its portrait, name and line are text
    fields inside the clip, so where each goes comes from the same tags the
    bars' numbers do."""
    placed = next((c for c in chrome if c['name'] == SPEECH_INSTANCE), None)
    if not placed:
        return None
    out = dict(placed)
    out['fields'] = text_fields(body, placed['character'], texts, fonts)
    out['parts'] = graphic_parts(body, raw_dir, placed['character'], texts)
    return out


def colour_ramp(body, raw_dir):
    """The hundred colours the health bar runs through.

    The bar does not tint its fill: it points a hundred-frame clip at
    round(percent * 100), and each of those frames is one flat colour. Reading
    them off is the only way to get the original's red-to-green curve."""
    import glob
    frames = sorted(glob.glob(os.path.join(raw_dir or '', 'sprite',
                                           'DefineSprite_%d' % RAMP_CHARACTER,
                                           '*.png')),
                    key=lambda f: int(os.path.basename(f)[:-4]))
    if not frames:
        return []
    try:
        from PIL import Image
    except ImportError:
        return []
    out = []
    for path in frames:
        image = Image.open(path).convert('RGBA')
        w, h = image.size
        out.append(list(image.getpixel((w // 2, h // 2))[:3]))
    return out


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

    # The battlefield has two backdrop layers, and each is a container the
    # game retargets by name on load: the sky sits behind everything as its
    # own root-level clip doing gotoAndStop(Krin.SkyBG), and the ground is
    # BATTLESCREEN itself doing gotoAndStop(Krin.ZoneBG) before it places the
    # six units over it. Recording both placements is what lets a zone's own
    # art land where the original puts it.
    layers = {'zone': {'x': sx, 'y': sy, 'character': screen['character'],
                       'scale_x': round(sa, 6), 'scale_y': round(sd, 6)}}

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

    # The ability tree. Its nodes live two containers deep -- the menu holds
    # the tree sprite, which holds st0..stN -- so their stage positions are
    # the three placements composed.
    talents = {}
    menu = last_seen(root, 'KRINMENU')
    if menu:
        ma, _, _, md, mx, my = menu['matrix']
        for frame in model_frames(body, menu['character']):
            tree = frame.get('talenttreefull')
            if not tree:
                continue
            ta, _, _, td, tx, ty = tree['matrix']
            for node in model_frames(body, tree['character']):
                for name, info in node.items():
                    if not name.startswith('st') or not name[2:].isdigit():
                        continue
                    na, _, _, nd, nx, ny = info['matrix']
                    talents[int(name[2:])] = {
                        'x': round(mx + ma * (tx + ta * nx), 3),
                        'y': round(my + md * (ty + td * ny), 3),
                        'scale': round(ma * ta * na, 6),
                    }
                if talents:
                    break
            if talents:
                break

    # The menu screens. Frame 1 is the character and inventory screen -- 36
    # bag slots, 7 equipment slots, a live doll preview and the per-element
    # bars -- and frame 16 adds the 15 drop slots the victory screen uses.
    # Positions are relative to the menu, so they are composed with its own
    # placement to give stage coordinates.
    menu_screens = {}
    if menu:
        ma, _, _, md, mx, my = menu['matrix']
        frames = model_frames(body, menu['character'])
        for index, label in ((0, 'character'), (15, 'victory'),
                             (24, 'talents')):
            if index >= len(frames):
                continue
            entries = {}
            for name, info in frames[index].items():
                if name.startswith('@'):
                    continue
                a, _, _, d, x, y = info['matrix']
                entries[name] = {'x': round(mx + ma * x, 3),
                                 'y': round(my + md * y, 3),
                                 'scale': round(ma * a, 6)}
            menu_screens[label] = entries

    # The battle screen's own furniture. The root timeline places the whole
    # thing on the KRINBATTLESCENE frame: the top stats panel, the black
    # battlefield backing, the three panels along the bottom, the turn
    # indicator and so on, each at its own depth. Recording the display list
    # verbatim is what lets the battle screen be laid out by the original
    # rather than by eye; the engine picks the pieces it draws out of this by
    # depth, and everything carries its exported canvas geometry so the
    # trimmed PNG lines back up with the coordinate the game draws at.
    def frame_chrome(label):
        entries = []
        index = root_label_frame(body, label) - 1
        if index >= len(root):
            return entries
        for name, info in sorted(root[index].items(),
                                 key=lambda kv: kv[1]['depth']):
            a, _, _, d, x, y = info['matrix']
            entry = {'screen': label, 'name': name, 'depth': info['depth'],
                     'character': info['character'],
                     'x': round(x, 3), 'y': round(y, 3),
                     'scale_x': round(a, 6), 'scale_y': round(d, 6)}
            if info.get('clip_depth') is not None:
                # A mask, not a picture: it clips everything above it up to
                # this depth. The battlefield's is what keeps the backdrop
                # inside its frame.
                entry['clip_depth'] = info['clip_depth']
            geom = sprite_geometry(raw_dir, info['character'])
            if geom:
                entry['width'], entry['height'] = geom[0], geom[1]
                entry['origin_x'], entry['origin_y'] = geom[2], geom[3]
            entries.append(entry)
        return entries

    screens = {label: frame_chrome(label) for label in SCREEN_FRAMES}

    # Every button a screen places, with the box it responds in. A button is
    # not a sprite, so it never appears in the art; its id is what says what
    # pressing it does.
    boxes = button_boxes(body, shape_bounds(body))
    buttons = []
    for label, entries in screens.items():
        for entry in entries:
            at = (entry['scale_x'], entry['scale_y'], entry['x'], entry['y'])
            box = boxes.get(entry['character'])
            if box:
                found = [{'name': entry['name'],
                          'character': entry['character'],
                          'x': round(entry['x'] + entry['scale_x'] * box[0], 3),
                          'y': round(entry['y'] + entry['scale_y'] * box[1], 3),
                          'width': round(entry['scale_x'] * (box[2] - box[0]), 3),
                          'height': round(entry['scale_y'] * (box[3] - box[1]), 3)}]
            else:
                found = buttons_in(body, entry['character'], boxes, at)
            for button in found:
                button['screen'] = label
                button['owner'] = entry['name']
                buttons.append(button)
    chrome = screens[BATTLE_FRAME_LABEL]

    # Which root-level clip is the sky is not something to guess at: it is
    # the one whose own timeline is labelled with the sky names, the frames
    # gotoAndStop(Krin.SkyBG) selects between.
    for entry in chrome:
        if entry['character'] in (screen['character'], None):
            continue
        labels = sprite_labels(body, entry['character'])
        if labels and SKY_FRAME in labels:
            layers['sky'] = {'x': entry['x'], 'y': entry['y'],
                             'character': entry['character'],
                             'scale_x': entry['scale_x'],
                             'scale_y': entry['scale_y']}
            break

    texts = edit_text_boxes(body)
    fonts = font_table(body)
    widget = bar_widget(body, raw_dir, texts, fonts)

    # Any text the battle screen's furniture carries. Some entries are a text
    # field in their own right -- the frame-rate readout is two of them --
    # and some are clips with fields inside.
    # Any clip that carries text cannot be exported as one picture: the
    # decompiler bakes the fields' design-time copy into it. Its graphics are
    # recorded separately so the screen can put it back together with real
    # text over them.
    clip_parts = []
    chrome_fields = []
    for entry in [e for group in screens.values() for e in group]:
        box = texts.get(entry['character'])
        if box:
            chrome_fields.append({
                'screen': entry['screen'],
                'owner': entry['name'], 'name': entry['name'],
                'x': round(entry['x'] + entry['scale_x'] * box['xmin'], 3),
                'y': round(entry['y'] + entry['scale_y'] * box['ymin'], 3),
                'width': round(entry['scale_x'] * (box['xmax'] - box['xmin']), 3),
                'height': round(entry['scale_y'] * (box['ymax'] - box['ymin']), 3),
                'size': box['height'], 'align': box['align'],
                'leading': box['leading'], 'text': box['text'],
                'font': (fonts.get(box['font']) or ('', 0))[0],
                'device': (fonts.get(box['font']) or ('', 0))[1] == 0,
                'color': box['color']})
            continue
        fields = text_fields(body, entry['character'], texts, fonts)
        if not fields:
            continue
        for field in fields:
            field['screen'] = entry['screen']
            field['owner'] = entry['name']
            field['x'] = round(entry['x'] + entry['scale_x'] * field['x'], 3)
            field['y'] = round(entry['y'] + entry['scale_y'] * field['y'], 3)
            chrome_fields.append(field)
        for part in graphic_parts(body, raw_dir, entry['character'], texts):
            part['screen'] = entry['screen']
            part['owner'] = entry['name']
            # In stage coordinates, like the fields, so a clip the game moves
            # carries its pieces and its text by the same offset.
            part['x'] = round(entry['x'] + entry['scale_x'] * part['x'], 3)
            part['y'] = round(entry['y'] + entry['scale_y'] * part['y'], 3)
            part['scale_x'] = round(entry['scale_x'] * part['scale_x'], 6)
            part['scale_y'] = round(entry['scale_y'] * part['scale_y'], 6)
            clip_parts.append(part)

    json.dump({'screen': {'x': sx, 'y': sy, 'character': screen['character']},
               'slots': slots, 'backdrop': backdrop, 'bars': bars,
               'layers': layers, 'chrome': [e for group in screens.values()
                                            for e in group],
               'bar': widget, 'life_colours': colour_ramp(body, raw_dir),
               'selector': selector_ring(body, raw_dir, chrome),
               'chrome_text': chrome_fields,
               'speech': speech_box(body, raw_dir, chrome, texts, fonts),
               'clip_parts': clip_parts,
               'buttons': buttons,
               'zone_screen': zone_screen(body, raw_dir,
                                          [e for group in screens.values()
                                           for e in group]),
               'menus': menu_frames(body, raw_dir,
                                     [e for group in screens.values()
                                      for e in group], texts, fonts),
               'talents': talents, 'menu': menu_screens}, sys.stdout, indent=1)
    print()


if __name__ == '__main__':
    if len(sys.argv) not in (2, 3):
        raise SystemExit(__doc__)
    main(sys.argv[1], sys.argv[2] if len(sys.argv) > 2 else None)
