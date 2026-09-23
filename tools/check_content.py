#!/usr/bin/env python3
"""Every reference one table makes to another, and whether it lands.

The tables name each other by id and by key -- a battle names the units in it
and what it drops, a move names the buff it puts on, a talent names the move
it teaches and every rank above it -- and a name that points at nothing is as
silent in the engine as a missing picture is: the fight is a slot short, the
move lands without its buff. This walks the tables as the engine is built
from them, overlays and all (see content.py), and says which references do
not resolve.

    python3 tools/check_content.py

Exits non-zero if any do.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from content import load                                      # noqa: E402

ELEMENTS = ['Physical', 'Magic', 'Ice', 'Fire', 'Lightning', 'Earth',
            'Shadow', 'Poison']
# Item 0 is "nothing here", which every slot uses.
NOTHING = 0
# The comics' frame labels on the root, which a story beat may name.
COMICS = {'CS_INTRO', 'CS_BRIDGE', 'CS_OUTRO'}


def main():
    abilities = {a['id']: a for a in load('abilities')}
    buffs = {b['key']: b for b in load('buffs')}
    units = {u['id']: u for u in load('units')}
    items = {i['id']: i for i in load('items')}
    battles = {b['id']: b for b in load('battles')}
    zones = load('zones')
    talents = load('talents')
    shops = load('shops')
    party = load('party')
    story = load('story')
    lang = load('lang').get('ENGLISH', {})

    problems = []

    def need(ok, where, what):
        if not ok:
            problems.append('%s: %s' % (where, what))

    for aid, a in sorted(abilities.items()):
        where = 'ability %d %s' % (aid, a.get('name') or '')
        c = a.get('combat') or {}
        buff = c.get('buff')
        if isinstance(buff, str) and buff:
            need(buff in buffs, where, 'puts on buff %r, which is not there'
                 % buff)
        need(c.get('element', 'Physical') in ELEMENTS, where,
             'element %r is not one of the eight' % c.get('element'))
        for element in c.get('dispel_elements') or []:
            if element != 0:
                need(element in ELEMENTS, where,
                     'dispels element %r, which is not one of the eight'
                     % element)
        need(a.get('delivery') in (None, 'Melee', 'Missile', 'Shock'), where,
             'delivery %r is not Melee, Missile or Shock' % a.get('delivery'))

    for key, b in sorted(buffs.items()):
        need(b.get('element') in ELEMENTS, 'buff %s' % key,
             'element %r is not one of the eight' % b.get('element'))

    for uid, u in sorted(units.items()):
        where = 'unit %d %s' % (uid, u.get('name'))
        for move in (u.get('movesA') or []) + (u.get('movesD') or []):
            need(move in abilities, where, 'knows move %r, which is not there'
                 % move)

    for iid, i in sorted(items.items()):
        need(0 <= (i.get('slot') or 0) <= 8, 'item %d' % iid,
             'slot %r is out of range' % i.get('slot'))

    for bid, b in sorted(battles.items()):
        where = 'battle %d' % bid
        players = b.get('players') or []
        levels = b.get('playersLevels') or []
        need(len(players) == len(levels), where,
             '%d players but %d levels' % (len(players), len(levels)))
        for entry in players:
            if entry > 0:
                need(entry in units, where, 'fields unit %d, which is not there'
                     % entry)
            elif entry < 0:
                need(entry in (-1, -2), where,
                     'ally place %d is not one of the two' % entry)
        for drop in b.get('itemDrops') or []:
            need(drop.get('ID') in items, where,
                 'drops item %r, which is not there' % drop.get('ID'))
        for rare in b.get('itemRare') or []:
            item = rare.get('ID') if isinstance(rare, dict) else rare
            need(item in items, where,
                 'rare drop %r is not there' % item)

    for z in zones:
        where = 'zone %d %s' % (z['zone'], z.get('name'))
        for bid in range(z['first_battle'], z['last_battle']):
            need(bid in battles, where, 'story fight %d is not there' % bid)
        for bid in z.get('training') or []:
            need(bid in battles, where, 'practice fight %d is not there' % bid)

    for node in talents['nodes']:
        where = 'talent node %d' % node['index']
        if node.get('CLASSIFY'):
            name = node.get('BUFFNAME')
            for rank in range(1, node['TIER'] + 1):
                need('%s%d' % (name, rank) in buffs, where,
                     'rank %d grants buff %s%d, which is not there'
                     % (rank, name, rank))
        else:
            for rank in range(node['TIER']):
                need(node['ID'] + rank in abilities, where,
                     'rank %d teaches move %d, which is not there'
                     % (rank + 1, node['ID'] + rank))
        for pre in node.get('PRESKILL') or []:
            need(pre == -1 or 0 <= pre < len(talents['nodes']), where,
                 'needs node %r first, which is not there' % pre)
    for start in ('startSkill1', 'startSkill2'):
        if talents.get(start) is not None:
            need(talents[start] in abilities, 'talents',
                 '%s is move %r, which is not there'
                 % (start, talents[start]))

    for shop, stock in sorted((shops.get('stock') or {}).items()):
        for item in stock:
            if item != NOTHING:
                need(item in items, 'shop %s' % shop,
                     'stocks item %d, which is not there' % item)

    for m in party.get('members') or []:
        where = 'party member %d %s' % (m['index'], m.get('NameSets'))
        for item in m.get('equipArray') or []:
            if item != NOTHING:
                need(item in items, where, 'wears item %d, which is not there'
                     % item)
        need(m.get('ClassStats') in units, where,
             'fights as unit %r, which is not there' % m.get('ClassStats'))

    notes = len(lang.get('NAVTITLE2') or [])
    for note in story.get('hub_notes') or []:
        need(note['say'] < notes, 'story',
             'hub note %d has no NAVTITLE2 line' % note['say'])
    for comic in story.get('boss_comics') or []:
        need(comic['comic'] in COMICS, 'story',
             'plays comic %r, which is not one of %s'
             % (comic['comic'], ', '.join(sorted(COMICS))))
    need(story.get('opening_battle') in battles, 'story',
         'opens on fight %r, which is not there' % story.get('opening_battle'))

    print('content: %d abilities, %d buffs, %d units, %d items, %d battles '
          'cross-checked, %d problems'
          % (len(abilities), len(buffs), len(units), len(items), len(battles),
             len(problems)))
    for p in problems:
        print('  ' + p)
    return 1 if problems else 0


if __name__ == '__main__':
    sys.exit(main())
