#!/usr/bin/env python3
"""Select the art the engine actually references out of the raw decompiler
dump, into a compact committed set plus a manifest.

The raw sprite export is every frame of every sprite -- about 200 MB, most of
it never referenced. The engine names its art three ways, and this resolves all
three against the tables in data/extracted:

  * by export name        -- effects (BOOM_*), backgrounds, UI, and the
                             composite doll parts "M_<PART>_<LOOK>"
  * by frame label        -- ability icons, zone backdrops, buff icons, which
                             the game selects with gotoAndStop(name)
  * by doll assembly      -- an item's `looks` combined with the part table to
                             form the export names above

    python3 tools/build_assets.py --raw assets/raw --out assets/art
"""
import argparse
import glob
import hashlib
import json
import os
import re
import shutil
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rasterize                                              # noqa: E402
from rasterize import svg_sibling                             # noqa: E402

# The root <g> of an exported SVG carries the offset from the sprite's own
# origin to the top-left of the exported canvas. Without it a sprite can only
# be centred by guesswork, and effects land beside their target.
SVG_ROOT_TRANSFORM = re.compile(
    r'<g transform="matrix\(([-0-9.eE]+), *([-0-9.eE]+), *([-0-9.eE]+), *'
    r'([-0-9.eE]+), *([-0-9.eE]+), *([-0-9.eE]+)\)"')

# The ring's own copy of an ability icon, which is a different shape to the
# one the menus use under the same label.
ORB_PREFIX = 'ORB '
# A chrome asset is asked for by character id, optionally with the one frame
# of it that is wanted: "#1444" or "#1444@8".
CHROME_ID = re.compile(r'^[0-9]+(@[0-9]+)?$')

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)

# The balloon a unit pops while it speaks, and where its container puts it.
SPEECH_BALLOON = 974
SPEECH_BALLOON_AT = (-10.65, -64.3)
# The prompt beside the speech box.
SPEECH_SKIP = 1639
# The exclamation mark that pulses at the turn indicator (@18 on the battle
# frame), which is what the original shows while no ability is chosen.
TURN_MARK = 1536
# moveSelectBoomer: the ring that closes over the indicator on a choice.
MOVE_BOOMER = 1610


def load(name, data_dir):
    with open(os.path.join(data_dir, name + '.json'), encoding='utf-8') as fh:
        return json.load(fh)


def frame_offset(png_path, raw):
    """(tx, ty): where the art's own origin sits inside its exported canvas.

    Both sprite frames and shapes get exported alongside an SVG whose root
    transform records exactly this, which is the only reliable way to line the
    trimmed and padded PNG back up with the coordinate the game draws at.

    It is in stage units and stays that way whatever scale the art itself is
    rasterised at."""
    svg = svg_sibling(png_path)
    if not svg:
        return None
    try:
        with open(svg, encoding='utf-8', errors='replace') as fh:
            head = fh.read(4096)
    except OSError:
        return None
    m = SVG_ROOT_TRANSFORM.search(head)
    if not m:
        return None
    return [round(float(m.group(5)), 3), round(float(m.group(6)), 3)]


def shape_png(raw, shape_id):
    path = os.path.join(raw, 'shape_png', '%d.png' % shape_id)
    return path if os.path.exists(path) else None


def placed_origin(exports, shape_id, matrix):
    """Where a placed shape belongs relative to its parent's origin, as
    [xmin, ymin, xmax, ymax] with the placement matrix applied.

    The exported PNG is trimmed to the shape's bounds and carries a little
    padding, so the reliable way to place it is to centre it on this box
    rather than to line up its corner."""
    bounds = (exports.get('shape_bounds') or {}).get(str(shape_id))
    if not bounds:
        return None
    xmin, ymin, xmax, ymax = bounds
    if matrix:
        a, b, c, d, tx, ty = matrix
        corners = [(a * x + c * y + tx, b * x + d * y + ty)
                   for x, y in ((xmin, ymin), (xmax, ymin),
                                (xmin, ymax), (xmax, ymax))]
        xs = [p[0] for p in corners]
        ys = [p[1] for p in corners]
        xmin, xmax = min(xs), max(xs)
        ymin, ymax = min(ys), max(ys)
    return [round(xmin, 3), round(ymin, 3), round(xmax, 3), round(ymax, 3)]


def shape_origin(exports, shape_id):
    """A shape's bounds in its own coordinate space."""
    return placed_origin(exports, shape_id, None)


def sound_file(raw, exports, name):
    """The decompiler names sound files "<id>_<export name>", so the export
    table is what connects a sound the engine asks for to a file."""
    cid = (exports.get('exports') or {}).get(name)
    if cid is None:
        return None
    for ext in ('mp3', 'wav'):
        path = os.path.join(raw, 'sound', '%d_%s.%s' % (cid, name, ext))
        if os.path.exists(path):
            return path
    return None


def sprite_dir(raw, sprite_id):
    matches = glob.glob(os.path.join(raw, 'sprite', 'DefineSprite_%d' % sprite_id))
    matches += glob.glob(os.path.join(raw, 'sprite',
                                      'DefineSprite_%d_*' % sprite_id))
    return matches[0] if matches else None


def frame_files(raw, sprite_id):
    d = sprite_dir(raw, sprite_id)
    if not d:
        return []
    files = []
    for path in glob.glob(os.path.join(d, '*.png')):
        base = os.path.splitext(os.path.basename(path))[0]
        if base.isdigit():
            files.append((int(base), path))
    return sorted(files)


def collect_names(data_dir):
    """Every art name the engine can ask for, by category."""
    abilities = load('abilities', data_dir)
    units = load('units', data_dir)
    items = load('items', data_dir)
    battles = load('battles', data_dir)
    buffs = load('buffs', data_dir)
    doll = load('doll', data_dir)
    talents = load('talents', data_dir)
    try:
        shops = load('shops', data_dir)
    except (OSError, ValueError):
        shops = {}

    want = {'icon': set(), 'effect': set(), 'background': set(),
            'doll': set(), 'buff': set(), 'ui': set(), 'sound': set(),
            'chrome': set(), 'orb': set(), 'portrait': set(),
            'cutscene': set(),
            'zone': set(), 'item': set()}
    speculative = set()
    # Expected to have no art: enemy ability icons (never on the player's
    # bar), permanent passive-talent buffs, and the doll cross-product below,
    # most combinations of which do not exist.

    for a in abilities:
        if a.get('icon'):
            want['icon'].add(a['icon'])
            # The battle screen does not show a bare icon: it shows the whole
            # orb, which is one clip whose frames are labelled by icon name.
            # Its copy of the icon is a different shape to the one the menus
            # use, so it is asked for under its own name. The placeholder move
            # is the one with no picture at all.
            want['orb'].add(ORB_PREFIX + a['icon'])
            if a['icon'] == 'None':
                speculative.add(ORB_PREFIX + a['icon'])
            if a['id'] >= 500:
                speculative.add(a['icon'])
                speculative.add(ORB_PREFIX + a['icon'])
        # `model` names the projectile or impact graphic. `anim` ("Attack",
        # "Heal") is not art at all: it selects which animation the caster
        # plays, and the model's own frame labels (attack1, cast, ...) are
        # what get played. See the animations block in the manifest.
        if a.get('model'):
            want['effect'].add(a['model'])
        # A missile's own projectile, which is the clip named in the slot the
        # melee path leaves unused. Without it a bolt move had nothing to
        # throw and the hit simply happened.
        if a.get('delivery') == 'Missile' and a.get('anim'):
            want['effect'].add(a['anim'])
        if a.get('sound'):
            want['sound'].add(a['sound'])
    for b in buffs:
        want['buff'].add(b['key'])
        # Permanent buffs (duration -1) are the passive talents. The icon
        # sprite has no frame for them, so the original's
        # gotoAndStop("REGENERATION1") silently leaves the icon on whatever
        # frame it was already showing. There is no art to take.
        if str(b['fields'].get('16')) == '-1':
            speculative.add(b['key'])
    for b in battles:
        for key in ('ZoneBG', 'SkyBG'):
            if b.get(key):
                want['background'].add(b[key])

    # Doll parts: gender + part + look, for every look any item or unit uses.
    cores2 = doll.get('dollPartsCores2') or []
    looks = set()
    for it in items:
        if it.get('looks'):
            looks.add(it['looks'])
    for u in units:
        model = u.get('model')
        if isinstance(model, list):
            for entry in model:
                if isinstance(entry, str) and entry:
                    looks.add(entry)
        if isinstance(u.get('skinSetter'), str) and u['skinSetter']:
            looks.add(u['skinSetter'])
        # Each unit's own hit grunts and death cry.
        for voice in (u.get('voiceHit') or []):
            if isinstance(voice, str) and voice:
                want['sound'].add(voice)
        if isinstance(u.get('voiceDie'), str) and u['voiceDie']:
            want['sound'].add(u['voiceDie'])

    # The zone's progress bar, in two pieces: the game scales only the fill
    # (the child named "bar"), leaving the track it runs in alone, so the two
    # have to be drawn apart.
    want['chrome'].update(['#1259', '#1261'])
    # The turn indicator's plain ring. The clip it sits in (#1596) also holds
    # the clock that fills over it, which the decompiler renders as a pair of
    # one-pixel slivers, so the ring is taken on its own and the lit clock
    # comes from tools/extract_glows.py.
    want['chrome'].update(['#1591'])
    # The widget that shows one buff on a unit's bar: its coloured backing,
    # its frame, and the backing behind the turns left on it.
    want['chrome'].update(['#780', '#781', '#800'])
    # The experience bar's fill on the victory screen, which has a second
    # frame it switches to on a level.
    want['chrome'].update(['#1344@1', '#1344@2'])
    # The balloon over a speaker's head. Each unit's container carries one
    # (character 974, placed as "speech") and the speech driver plays it
    # through when that unit says something.
    want['effect'].add('#%d' % SPEECH_BALLOON)
    # And the "< Press SPACEBAR to skip >" that sits beside the box while
    # someone is talking, which pulses through its own frames.
    want['effect'].add('#%d' % SPEECH_SKIP)
    # The exclamation mark under the turn indicator, which pulses from a
    # bright yellow to a dim orange and back over its forty-eight frames. It
    # is thirty pixels square, so keeping all of them costs almost nothing.
    want['effect'].add('#%d' % TURN_MARK)
    # The layers of the three effect clips the model plays over itself: the
    # sweep through the magic swing (929 over 930), the orb a caster charges
    # (934 under 935), and the crackle of being held stunned (938). The clips
    # that hold them are kept apart because the game recolours one layer of
    # each pair and not the other; see tools/extract_cast.py.
    want['effect'].update(['#929', '#930', '#934', '#935', '#938'])
    # KrinTrail's streak: one small shape, clear at the tail and solid at
    # the head, which the bolt code drops behind a projectile and stretches.
    # The clip around it (146) holds nothing else, so the shape is enough.
    want['effect'].add('#144')
    # The ring that closes over the turn indicator once a move is chosen,
    # which the original plays through once (moveSelectBoomer.play()).
    want['effect'].add('#%d' % MOVE_BOOMER)
    # The battle screen's own effect sounds, played by name.
    want['sound'].update(['Swing', 'MagicCast', 'Forcefield', 'Click2putdown',
                          'Click3pickup'])
    # Music. The original walks a list of four during battle and plays the
    # first again on the hub; the boss theme stands in for a boss fight.
    want['sound'].update(['menumusic', 'Gamemusic002', 'BossBattleloopable',
                          'BattleMusic1loopable', 'BattleMusic2loopable'])
    # The lines a fight speaks: a battle's speech list names the take.
    for b in battles:
        for speech in (b.get('speeches') or []):
            if isinstance(speech.get('voiceOver'), str) and speech['voiceOver']:
                want['sound'].add(speech['voiceOver'])
    # Each doll part draws two layers: the skin underneath
    # (<gender>_S<part>_<skin>) and the equipped item over it
    # (<gender>_<part>_<look>). Weapons always use the M_ form. Hair goes on
    # the head when nothing is equipped there.
    skins = set()
    hairs = set()
    for u in units:
        model = u.get('model')
        if isinstance(model, list):
            if len(model) > 1 and isinstance(model[1], str) and model[1]:
                skins.add(model[1])
            if len(model) > 2 and isinstance(model[2], str) and model[2]:
                hairs.add(model[2])
    # The character screen's own skin and hair sets.
    skins.update(['ONE', 'TWO', 'THREE', 'FOUR', 'FIVE', 'SIX'])
    hairs.update(['ONE', 'TWO', 'THREE', 'FOUR', 'FIVE', 'SIX', 'BART'])

    for gender in ('M', 'F'):
        for part in set(cores2):
            for look in looks:
                name = '%s_%s_%s' % (gender, part, look)
                want['doll'].add(name)
                speculative.add(name)
            for skin in skins:
                name = '%s_S%s_%s' % (gender, part, skin)
                want['doll'].add(name)
                speculative.add(name)
    for hair in hairs:
        want['doll'].add('HAIR_%s' % hair)
        speculative.add('HAIR_%s' % hair)

    # UI pieces the battle screen attaches by name.
    want['ui'].update(['KrinBuffShower', 'MODEL1'])
    # The orb the action bar shows for a slot with nothing assigned to it:
    # the skills screen does gotoAndStop("Empty") on any such slot.
    want['orb'].add(ORB_PREFIX + 'Empty')
    # A passive node of the tree shows its buff's icon rather than the
    # ability's: the tree does gotoAndStop(abilityXer[k].BUFFNAME).
    for node in (talents.get('nodes') or []):
        if node.get('CLASSIFY') == 1 and node.get('BUFFNAME'):
            want['orb'].add(ORB_PREFIX + node['BUFFNAME'])

    # The battle screen's furniture. Most of it is never exported under a
    # name -- the root timeline just places the character -- so it is asked
    # for by id, as "#1531". extract_stage.py recorded the display list; this
    # takes the art for every entry in it that has any.
    try:
        stage = load('stage', data_dir)
    except (OSError, ValueError):
        stage = {}
    # Two entries are containers the engine fills itself rather than pictures
    # to draw: the sky and the ground, each of which the game retargets with
    # gotoAndStop to the zone's own art. Their design-time frames are not the
    # battle's backdrop and copying all 54 of them would cost 13 MB.
    skip_ids = {(stage.get('backdrop') or {}).get('character')}
    skip_ids.update(layer['character']
                    for layer in (stage.get('layers') or {}).values())
    for entry in (stage.get('chrome') or []):
        if (entry.get('character') and 'width' in entry
                and entry['character'] not in skip_ids):
            want['chrome'].add('#%d' % entry['character'])
    # The bar widget is drawn from its pieces rather than as a whole, because
    # its own frame has the design-time name and numbers baked into it.
    # The speech box is drawn from its pieces for the same reason the bar is:
    # its own frame has the design-time copy baked into it. The portrait is
    # one of them, and the game points it at a frame named for the speaker.
    for part in (stage.get('clip_parts') or []):
        if part.get('character') and part.get('width') and not part.get('frames'):
            want['chrome'].add('#%d' % part['character'])
    # An item's picture, which is a frame of the clip every slot shows its
    # contents through, labelled with the item's name.
    for it in items:
        if it.get('name'):
            want['item'].add(it['name'])
            speculative.add(it['name'])

    # The hub's scene, one labelled frame per zone.
    want['zone'].update(((stage.get('zone_screen') or {}).get('labels')) or {})

    # A menu's slots are clips the engine fills, but their empty square is
    # still drawn, so their resting frame is wanted too.
    for menu in (stage.get('menus') or {}).values():
        # A menu screen's own furniture, which is never exported under a name.
        for part in (menu.get('parts') or []):
            if part.get('width') and not part.get('frames'):
                want['chrome'].add('#%d' % part['character'])
        for slot in menu['slots'].values():
            if slot.get('character') and slot.get('width'):
                want['chrome'].add('#%d' % slot['character'])
                # A slot the screen points at a frame of by name wants that
                # frame too, not just the resting one.
                for frame in (slot.get('labels') or {}).values():
                    want['chrome'].add('#%d@%d' % (slot['character'], frame))
    for part in ((stage.get('bar') or {}).get('graphics') or []):
        if part.get('character') and part.get('width'):
            want['chrome'].add('#%d' % part['character'])
    # A button's own resting art, which appears nowhere else in the file.
    for pieces in (stage.get('button_art') or {}).values():
        for piece in pieces:
            if piece.get('width'):
                want['chrome'].add('#%d' % piece['character'])
    # The ability pool's row: a bar with the ability's orb sitting on it.
    for part in ((stage.get('talent_row') or {}).get('parts') or []):
        if part.get('width') and not part.get('frames'):
            want['chrome'].add('#%d' % part['character'])
    # The row of party portraits on the character screen, which the original
    # points at a frame by number rather than by name.
    for frame in range(1, 7):
        want['chrome'].add('#1312@%d' % frame)
    want['chrome'].update(('#1314', '#1317'))
    # The three cutscenes, each one long animation the screen plays through.
    for scr in ('CS_INTRO', 'CS_BRIDGE', 'CS_OUTRO'):
        for entry in (stage.get('chrome') or []):
            if entry.get('screen') == scr and entry.get('width') \
                    and entry['character'] not in skip_ids:
                want['cutscene'].add('#%d' % entry['character'])
    # The world map and the marker that stands on it per zone.
    world = stage.get('map_screen') or {}
    for part in (world.get('parts') or []):
        if part.get('width') and not part.get('frames'):
            want['chrome'].add('#%d' % part['character'])
    for marker in (world.get('markers') or []):
        want['chrome'].add('#%d' % marker['character'])
    # The store's picture. Its clip has a frame per store and the screen
    # points it at shopId + 1, so every store's frame is wanted.
    picture = next((p for p in ((stage.get('menus') or {}).get('shop') or {})
                    .get('parts', []) if p.get('frames')), None)
    if picture:
        for shop in (shops.get('stock') or {}):
            want['chrome'].add('#%d@%d' % (picture['character'],
                                           int(shop) + 1))
    return want, speculative


def copy_frames(entries, dest_dir, safe, scale):
    """Put one asset's frames in place. -> (paths, whether they are at
    `scale`).

    An animation is all one picture to the eye, so its frames have to agree on
    how finely they are drawn: the first frame that will not rasterise
    faithfully -- because the decompiler wrote its colour transform or its
    glow as an SVG filter, and no other rasteriser reproduces those the way
    the decompiler's own renderer does -- gives up on the whole asset, which
    is then copied as the decompiler exported it.

    A frame that is byte for byte one already written points at that copy
    instead of making another: a cutscene of thirteen hundred frames is a
    hundred and sixty pictures.
    """
    files = []
    written = {}
    for index, (_, src) in enumerate(entries, start=1):
        ext = os.path.splitext(src)[1] or '.png'
        with open(src, 'rb') as fh:
            digest = hashlib.md5(fh.read()).hexdigest()
        if digest in written:
            files.append(written[digest])
            continue
        dest = os.path.join(dest_dir,
                            '%s_%d%s' % (safe, index, ext) if len(entries) > 1
                            else '%s%s' % (safe, ext))
        if scale > 1:
            large = rasterize.art(svg_sibling(src), src, scale)
            if large is None:
                return src, False
            large.save(dest)
        else:
            shutil.copyfile(src, dest)
        written[digest] = os.path.relpath(dest, ROOT)
        files.append(written[digest])
    return files, True


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--raw', default='assets/raw')
    ap.add_argument('--zone-raw',
                    help="a second decompiler dump, of the copy of the SWF "
                         "tools/extract_markers.py strips the markers out of. "
                         "The zone scenes come from there so the engine can "
                         "draw the markers itself and let them turn.")
    ap.add_argument('--out', default='assets/art')
    ap.add_argument('--data', default='data/extracted')
    ap.add_argument('--max-frames', type=int, default=64,
                    help='cap on frames copied per animation')
    ap.add_argument('--chrome-frames', type=int, default=1,
                    help='cap on frames copied per piece of furniture')
    ap.add_argument('--art-scale', type=float,
                    default=rasterize.DEFAULT_SCALE,
                    help="pixels of art per stage unit. The decompiler only "
                         "exports at 1, so anything above it is rasterised "
                         "from the SVG beside each PNG and checked against "
                         "that PNG; see tools/rasterize.py. 1 turns it off.")
    args = ap.parse_args()

    exports = load('exports', args.data)
    by_name = exports['exports']
    label_index = exports['label_index']
    want, speculative = collect_names(args.data)

    # The ring's orb, whose frames are labelled by ability icon name.
    try:
        stage = load('stage', args.data)
    except (OSError, ValueError):
        stage = {}
    selector = stage.get('selector') or {}
    orb_icons = selector.get('icons') or {}
    item_clip = stage.get('item_icons') or {}
    item_character = item_clip.get('character')
    item_labels = item_clip.get('labels') or {}
    zone_clip = stage.get('zone_screen') or {}
    zone_character = zone_clip.get('character')
    zone_labels = zone_clip.get('labels') or {}
    # Which sprite a label should be taken from when several carry it. The
    # battle's two backdrop layers are the containers the game retargets on
    # load, so their frames are the zone's real art.
    preferred = {'background': {layer['character'] for layer
                                in (stage.get('layers') or {}).values()}}
    # The speech portrait, whose frames are labelled with who is speaking.
    portrait = next((p for p in (stage.get('clip_parts') or [])
                     if p.get('owner') == 'combatScript' and p.get('frames')),
                    None)
    portrait_character = portrait['character'] if portrait else None
    portrait_labels = (exports['sprite_frame_labels']
                       .get(str(portrait_character)) or {}) if portrait else {}
    want['portrait'].update(portrait_labels)
    # The orb's shared pieces: the ball, the mask that cuts the icon to it,
    # the glass over it and the black disc shown while a move is unusable.
    for part in (selector.get('parts') or {}).values():
        if part.get('character'):
            want['chrome'].add('#%d' % part['character'])

    os.makedirs(args.out, exist_ok=True)
    manifest = {}
    stats = {'copied': 0, 'bytes': 0, 'missing': [], 'flat': [],
             'categories': {}}

    for category, names in sorted(want.items()):
        found = 0
        for name in sorted(names):
            entries = []
            origin = None

            if category == 'sound':
                path = sound_file(args.raw, exports, name)
                if path:
                    entries = [(1, path)]
            elif category == 'item':
                frame = item_labels.get(name)
                if frame:
                    for index, path in frame_files(args.raw, item_character):
                        if index == frame:
                            entries = [(1, path)]
                            break
            elif category == 'zone':
                frame = zone_labels.get(name)
                if frame:
                    for index, path in frame_files(args.zone_raw or args.raw,
                                                   zone_character):
                        if index == frame:
                            entries = [(1, path)]
                            break
            elif category == 'portrait':
                frame = portrait_labels.get(name)
                if frame:
                    for index, path in frame_files(args.raw,
                                                   portrait_character):
                        if index == frame:
                            entries = [(1, path)]
                            break
            elif category == 'orb':
                # The icon alone, as the ring's orb clip places it. The orb is
                # put back together from its shared pieces at draw time.
                cid = orb_icons.get(name[len(ORB_PREFIX):])
                png = shape_png(args.raw, cid) if cid else None
                if png:
                    entries = [(1, png)]
                    origin = shape_origin(exports, cid)
                elif cid:
                    frames = frame_files(args.raw, cid)[:args.max_frames]
                    entries = list(frames)
            elif name.startswith('#') and CHROME_ID.match(name[1:]):
                # Asked for by character id: a sprite keeps its frames, a
                # shape is one picture carrying its own bounds. "#1444@8" asks
                # for one particular frame, for a clip the screen points at a
                # frame of by name.
                spec, _, pick = name[1:].partition('@')
                cid = int(spec)
                # Furniture is drawn on its resting frame. Several of these
                # clips are animations of hundreds of frames -- the turn
                # indicator's pulse, the fade between screens -- and copying
                # all of them costs far more than it buys.
                if pick:
                    frames = [(1, path) for frame, path
                              in frame_files(args.raw, cid)
                              if frame == int(pick)]
                elif category in ('cutscene', 'effect'):
                    # Something meant to be played through keeps every frame
                    # it has: a comic, and the balloon that pops over a
                    # character's head while they talk.
                    frames = frame_files(args.raw, cid)
                else:
                    frames = frame_files(args.raw, cid)[:args.chrome_frames]
                if frames:
                    entries = list(frames)
                else:
                    png = shape_png(args.raw, cid)
                    if png:
                        entries = [(1, png)]
                        origin = shape_origin(exports, cid)
            elif name in by_name:
                # A whole exported character: a sprite keeps its frames as an
                # animation, a shape is a single picture.
                cid = by_name[name]
                frames = frame_files(args.raw, cid)[:args.max_frames]
                if frames:
                    entries = [(f, p) for f, p in frames]
                else:
                    png = shape_png(args.raw, cid)
                    if png:
                        entries = [(1, png)]
                        origin = shape_origin(exports, cid)
            elif name in label_index:
                # A labelled frame inside a sprite. The same label often
                # appears in several of them -- a backdrop is labelled both in
                # the battle's own sky container and in a menu preview -- so
                # the container the engine actually points at comes first.
                entries_for_label = label_index[name]
                if preferred.get(category):
                    entries_for_label = (
                        [e for e in entries_for_label
                         if e[0] in preferred[category]]
                        + [e for e in entries_for_label
                           if e[0] not in preferred[category]])
                # Prefer the art the frame actually places: a backdrop or icon frame places one shape,
                # and that shape alone is the picture. Rendering the enclosing
                # frame instead also catches whatever persisted from earlier
                # frames -- on the battle screen, the design-time unit
                # placeholders the game overwrites at runtime.
                for entry in entries_for_label:
                    sprite_id, frame = entry[0], entry[1]
                    placed = entry[2] if len(entry) > 2 else []
                    if len(placed) == 1:
                        one = placed[0]
                        cid = one['character'] if isinstance(one, dict) else one
                        matrix = one.get('matrix') if isinstance(one, dict) else None
                        png = shape_png(args.raw, cid)
                        if png:
                            entries = [(1, png)]
                            origin = placed_origin(exports, cid, matrix)
                            break
                        frames = frame_files(args.raw, one)[:args.max_frames]
                        if frames:
                            entries = list(frames)
                            break
                    for f, p in frame_files(args.raw, sprite_id):
                        if f == frame:
                            entries = [(1, p)]
                            break
                    if entries:
                        break

            if not entries:
                if name not in speculative:
                    stats['missing'].append('%s/%s' % (category, name))
                continue

            safe = name.replace('/', '_').replace(' ', '_')
            dest_dir = os.path.join(args.out, category)
            os.makedirs(dest_dir, exist_ok=True)
            scale = args.art_scale if category != 'sound' else 1.0
            files, sharp = copy_frames(entries, dest_dir, safe, scale)
            if not sharp:
                stats['flat'].append('%-28s %s' % ('%s/%s' % (category, name),
                                                   files))
                scale = 1.0
                files, _ = copy_frames(entries, dest_dir, safe, scale)
            for path in set(files):
                stats['copied'] += 1
                stats['bytes'] += os.path.getsize(os.path.join(ROOT, path))
            manifest[name] = {'category': category, 'frames': files}
            if scale > 1:
                manifest[name]['scale'] = scale
            if origin:
                manifest[name]['bounds'] = origin
            if category != 'sound':
                # A zone scene taken from the marker-free export has no SVG
                # sibling of its own; the art is the same size either way, so
                # the original dump's transform still lines it up.
                offsets = [frame_offset(src, args.raw)
                           or frame_offset(src.replace(args.zone_raw or '\0',
                                                       args.raw), args.raw)
                           for _, src in entries]
                if any(o is not None for o in offsets):
                    manifest[name]['offsets'] = [o or [0, 0] for o in offsets]
            found += 1
        stats['categories'][category] = {'wanted': len(names), 'found': found}

    # The cutscene narration, which tools/extract_streams.py writes straight
    # out of the SWF because it is a stream sound on the animation's timeline
    # rather than anything the decompiler's sound export sees. The files are
    # already in place; this only puts them in the manifest so the engine can
    # ask for them by name like any other sound.
    streams_path = os.path.join(args.data, 'streams.json')
    if os.path.exists(streams_path):
        with open(streams_path, encoding='utf-8') as fh:
            for name, info in json.load(fh).items():
                if os.path.exists(os.path.join(ROOT, info['file'])):
                    manifest[name] = {'category': 'sound',
                                      'frames': [info['file']]}

    # Pieces the game draws under a glow filter, which tools/extract_glows.py
    # renders with the glow applied -- the filter is on the placement, so the
    # art on its own comes out unlit. The lit picture is the flat one with a
    # margin round it for the halo, so the piece's own origin sits that much
    # further into it than it did before.
    glows_path = os.path.join(args.data, 'glows.json')
    if os.path.exists(glows_path):
        with open(glows_path, encoding='utf-8') as fh:
            for name, info in json.load(fh).items():
                if not os.path.exists(os.path.join(ROOT, info['file'])):
                    continue
                flat = manifest.get('#%d' % info['character']) or {}
                origin = info.get('origin') \
                    or (flat.get('offsets') or [[0.0, 0.0]])[0]
                manifest[name] = {'category': 'glow',
                                  'frames': [info['file']],
                                  'offsets': [[origin[0] + info['pad'],
                                               origin[1] + info['pad']]]}
                if info.get('scale', 1) > 1:
                    manifest[name]['scale'] = info['scale']

    # The markers on a zone's scene, which tools/extract_markers.py renders
    # style by style with their colour transform and glow already applied.
    # They are a normal multi-frame asset from here on.
    markers_path = os.path.join(args.data, 'markers.json')
    if os.path.exists(markers_path):
        with open(markers_path, encoding='utf-8') as fh:
            markers = json.load(fh)
        for name, style in markers.get('styles', {}).items():
            files = [f for f in style['files']
                     if os.path.exists(os.path.join(ROOT, f))]
            if not files:
                continue
            manifest[name] = {'category': 'marker', 'frames': files,
                              'offsets': [style['offset']] * len(files)}
            if style.get('scale', 1) > 1:
                manifest[name]['scale'] = style['scale']

    # The character model's frame labels are its animation states, so record
    # each one's start frame and how long it runs. The engine plays these by
    # name: stand, run, attack1, attack2, cast, stun, hit, dead.
    animations = {}
    model_id = by_name.get('MODEL1')
    if model_id is not None:
        labels = exports['sprite_frame_labels'].get(str(model_id)) or {}
        frames = frame_files(args.raw, model_id)
        last_frame = frames[-1][0] if frames else 0
        starts = sorted(labels.items(), key=lambda kv: kv[1])
        for index, (label, start) in enumerate(starts):
            end = (starts[index + 1][1] - 1 if index + 1 < len(starts)
                   else last_frame)
            animations[label] = {'start': start, 'end': end,
                                 'length': max(0, end - start + 1)}

    with open(os.path.join(args.out, 'manifest.json'), 'w',
              encoding='utf-8') as fh:
        json.dump({'assets': manifest, 'animations': animations,
                   'model': 'MODEL1'}, fh, indent=1, ensure_ascii=False)

    for category, c in sorted(stats['categories'].items()):
        print('%-12s %4d resolved of %d asked for' % (category, c['found'],
                                                      c['wanted']))
    print('%d files, %.1f MB' % (stats['copied'], stats['bytes'] / 1e6))
    if stats['flat']:
        print('%d assets kept at 1 pixel to the unit, with the frame that '
              'would not rasterise faithfully:' % len(stats['flat']))
        for name in stats['flat'][:30]:
            print('  ' + name)
        if len(stats['flat']) > 30:
            print('  ... and %d more' % (len(stats['flat']) - 30))
    if animations:
        print('model animations: %s'
              % ', '.join('%s(%d)' % (k, v['length'])
                          for k, v in sorted(animations.items(),
                                             key=lambda kv: kv[1]['start'])))
    if stats['missing']:
        print()
        print('%d names the engine uses but no art was found for:'
              % len(stats['missing']))
        for name in stats['missing'][:20]:
            print('  ' + name)
    else:
        print('every non-speculative name resolved')


if __name__ == '__main__':
    main()
