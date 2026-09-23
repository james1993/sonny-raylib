#!/usr/bin/env python3
"""Every name the engine can ask for, and whether the art build shipped it.

A missing name is silent at run time -- attachSound on a name that is not
there does nothing in Flash, and so does asset_texture here -- which is how
three of the four battle tracks and every line of battle speech came to be
missing without anything saying so. This walks the data tables for every name
the engine can reach and checks the manifest has it.

    python3 tools/check_assets.py

Exits non-zero if anything the engine can ask for is not there.
"""
import glob
import json
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import content                                                # noqa: E402

MANIFESTS = ('assets/art/manifest.json', 'assets/extra/manifest.json')


def load(name):
    # The tables as the engine is built from them, overlays and all.
    return content.load(name)


def manifest_assets():
    assets = {}
    for path in MANIFESTS:
        full = os.path.join(ROOT, path)
        if os.path.exists(full):
            with open(full, encoding='utf-8') as fh:
                assets.update(json.load(fh)['assets'])
    return assets


def rows(table):
    return table if isinstance(table, list) else list(table.values())


# The names the engine plays by hand rather than out of a table.
BY_HAND = {
    'sound': ['Swing', 'MagicCast', 'Forcefield', 'Click2putdown',
              'Click3pickup',
              # battle_music() walks these four and swaps in the boss theme
              'menumusic', 'BattleMusic2loopable', 'Gamemusic002',
              'BattleMusic1loopable', 'BossBattleloopable'],
    'art': ['CutsceneVoiceIntro', 'CutsceneVoiceBridge', 'CutsceneVoiceOutro'],
}


def wanted():
    """-> {name: [what asks for it]}"""
    want = {}

    def ask(name, who):
        if isinstance(name, str) and name:
            want.setdefault(name, []).append(who)

    for a in rows(load('abilities')):
        ask(a.get('sound'), 'an ability\'s sound')
        ask(a.get('model'), 'an ability\'s effect')
    for u in rows(load('units')):
        ask(u.get('voiceDie'), 'a unit\'s death cry')
        for voice in (u.get('voiceHit') or []):
            ask(voice, 'a unit\'s hit grunt')
    for b in rows(load('battles')):
        for speech in (b.get('speeches') or []):
            ask(speech.get('voiceOver'), 'a line of battle speech')
        for key in ('ZoneBG', 'SkyBG'):
            ask(b.get(key), 'a battle backdrop')
    for i in rows(load('items')):
        ask(i.get('name'), 'an item\'s picture')
    for names in BY_HAND.values():
        for name in names:
            ask(name, 'the engine by name')
    for name, where in source_names():
        ask(name, where)
    return want


# Art the engine names in its own source rather than through a table: a piece
# of furniture by character id ("#1259", or one frame of it, "#1444@8"), and a
# sound it plays by hand. The grey stat swatch was asked for this way and never
# shipped, and nothing noticed until this looked.
SOURCE_ART = re.compile(r'"(#[0-9]+(?:@[0-9]+)?)"')
SOURCE_SOUND = re.compile(r'audio_(?:play|music|narration)\(\s*"([^"]+)"')


def source_names():
    for path in sorted(glob.glob(os.path.join(ROOT, 'src/platform/*.c'))):
        with open(path, encoding='utf-8') as fh:
            text = fh.read()
        where = os.path.relpath(path, ROOT)
        for pattern in (SOURCE_ART, SOURCE_SOUND):
            for m in pattern.finditer(text):
                yield m.group(1), where


def main():
    have = set(manifest_assets())

    want = wanted()
    missing = sorted(n for n in want if n not in have)
    print('%d names the engine can ask for, %d shipped'
          % (len(want), len(want) - len(missing)))
    failed = 0
    if missing:
        print('\nnot shipped:')
        for name in missing:
            print('  %-28s %s' % (name, ', '.join(sorted(set(want[name])))))
        failed = 1

    # And the other way: every file the manifest names is on the disk, and
    # nothing is on the disk that the manifest does not name -- a picture left
    # behind by an older build is weight in every clone and nothing on screen.
    # Everything any manifest names, including what the extra manifest
    # stands in for -- an extracted picture replaced by a hand-made one is
    # still the extracted build's to keep.
    named = set()
    for path in MANIFESTS:
        full = os.path.join(ROOT, path)
        if os.path.exists(full):
            with open(full, encoding='utf-8') as fh:
                named |= {os.path.normpath(f)
                          for entry in json.load(fh)['assets'].values()
                          for f in entry['frames']}
    gone = sorted(f for f in named if not os.path.exists(os.path.join(ROOT, f)))
    on_disk = {os.path.normpath(os.path.relpath(os.path.join(d, f), ROOT))
               for top in ('assets/art', 'assets/extra')
               for d, _, files in os.walk(os.path.join(ROOT, top))
               for f in files if not f.endswith(('.json', '.txt', '.md'))}
    stray = sorted(on_disk - named)
    if gone:
        print('\nnamed by the manifest but not on the disk:')
        for f in gone:
            print('  ' + f)
        failed = 1
    if stray:
        print('\non the disk but named by nothing:')
        for f in stray[:20]:
            print('  ' + f)
        if len(stray) > 20:
            print('  ... and %d more' % (len(stray) - 20))
        failed = 1
    return failed


if __name__ == '__main__':
    sys.exit(main())
