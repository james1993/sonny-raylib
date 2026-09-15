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
import json
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def load(name):
    with open(os.path.join(ROOT, 'data/extracted/%s.json' % name),
              encoding='utf-8') as fh:
        return json.load(fh)


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
    return want


def main():
    with open(os.path.join(ROOT, 'assets/art/manifest.json'),
              encoding='utf-8') as fh:
        have = set(json.load(fh)['assets'])

    want = wanted()
    missing = sorted(n for n in want if n not in have)
    print('%d names the engine can ask for, %d shipped'
          % (len(want), len(want) - len(missing)))
    if not missing:
        return 0
    print('\nnot shipped:')
    for name in missing:
        print('  %-28s %s' % (name, ', '.join(sorted(set(want[name])))))
    return 1


if __name__ == '__main__':
    sys.exit(main())
