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
import json
import os
import shutil

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)


def load(name, data_dir):
    with open(os.path.join(data_dir, name + '.json'), encoding='utf-8') as fh:
        return json.load(fh)


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

    want = {'icon': set(), 'effect': set(), 'background': set(),
            'doll': set(), 'buff': set(), 'ui': set()}
    speculative = set()
    # Expected to have no art: enemy ability icons (never on the player's
    # bar), permanent passive-talent buffs, and the doll cross-product below,
    # most combinations of which do not exist.

    for a in abilities:
        if a.get('icon'):
            want['icon'].add(a['icon'])
            if a['id'] >= 500:
                speculative.add(a['icon'])
        # `model` names the projectile or impact graphic. `anim` ("Attack",
        # "Heal") is not art at all: it selects which animation the caster
        # plays, and the model's own frame labels (attack1, cast, ...) are
        # what get played. See the animations block in the manifest.
        if a.get('model'):
            want['effect'].add(a['model'])
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
    return want, speculative


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--raw', default='assets/raw')
    ap.add_argument('--out', default='assets/art')
    ap.add_argument('--data', default='data/extracted')
    ap.add_argument('--max-frames', type=int, default=64,
                    help='cap on frames copied per animation')
    args = ap.parse_args()

    exports = load('exports', args.data)
    by_name = exports['exports']
    label_index = exports['label_index']
    want, speculative = collect_names(args.data)

    os.makedirs(args.out, exist_ok=True)
    manifest = {}
    stats = {'copied': 0, 'bytes': 0, 'missing': [], 'categories': {}}

    for category, names in sorted(want.items()):
        found = 0
        for name in sorted(names):
            entries = []
            origin = None

            if name in by_name:
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
                # A labelled frame inside a sprite. Prefer the art the frame
                # actually places: a backdrop or icon frame places one shape,
                # and that shape alone is the picture. Rendering the enclosing
                # frame instead also catches whatever persisted from earlier
                # frames -- on the battle screen, the design-time unit
                # placeholders the game overwrites at runtime.
                for entry in label_index[name]:
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
            files = []
            for index, (_, src) in enumerate(entries, start=1):
                dest = os.path.join(dest_dir, '%s_%d.png' % (safe, index)
                                    if len(entries) > 1 else '%s.png' % safe)
                shutil.copyfile(src, dest)
                files.append(os.path.relpath(dest, ROOT))
                stats['copied'] += 1
                stats['bytes'] += os.path.getsize(dest)
            manifest[name] = {'category': category, 'frames': files}
            if origin:
                manifest[name]['bounds'] = origin
            found += 1
        stats['categories'][category] = {'wanted': len(names), 'found': found}

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
