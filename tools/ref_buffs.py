#!/usr/bin/env python3
"""Reference transcription of the buff system (applyBuffKrin,
applyChangesKrin, buffTicker) from the decompiled frame 62, used to generate
test vectors for the C port.

Literal on purpose, including the quirks:
  - `if (iftbc)` is true on removal too, so a shield is re-added on expiry.
  - Buff durations tick down after the turn's damage is counted.
  - A shield smaller than the tick damage is destroyed without reducing it.

    python3 tools/ref_buffs.py > tests/vectors_buffs.txt
"""
import math
import random
import sys

ELEMENTS = 8
MAX_BUFFS = 40
FILTERS = 9

CASTER = {'STRENGTHU': 100.0, 'MAGICU': 50.0, 'SPEEDU': 30.0}


def new_unit(plevel, life, strength, magic, speed, per, deff, focus):
    return {
        'plevel': plevel, 'active': 1,
        'LIFE': life, 'STRENGTH': strength, 'MAGIC': magic, 'SPEED': speed,
        'PER': [per] * ELEMENTS, 'DEF': [deff] * ELEMENTS,
        'LIFEU': round(life), 'LIFEN': round(life),
        'FOCUSU': focus, 'FOCUSN': focus,
        'STRENGTHU': strength, 'MAGICU': magic, 'SPEEDU': speed,
        'PERU': [per] * ELEMENTS, 'DEFU': [deff] * ELEMENTS,
        'SHIELD': 0, 'SHIELDCOUNTER': 0, 'STUN': 0, 'STUNP': 0,
        'REFLECT': 0, 'SSWITCH': 0,
        'DMG': 0, 'DMG2': 0, 'IDMG': 0, 'IDMG2': 0,
        'changeArray': [0.0] * 21,
        'changeArrayEP': [0.0] * ELEMENTS, 'changeArrayEP2': [0.0] * ELEMENTS,
        'changeArrayED': [0.0] * ELEMENTS, 'changeArrayED2': [0.0] * ELEMENTS,
        'DOTTICKERARRAY': [0.0] * 10,
        'FILTERSBUFFARRAY': [0] * FILTERS,
        'BUFFARRAYK': [{'buffId': 'None', 'CD': 0, 'buffValue': 0.0}
                       for _ in range(MAX_BUFFS)],
    }


def apply_buff(t, b, iftbc, caster, debuff_value):
    t['STUN'] += iftbc * b['stun']
    t['REFLECT'] += iftbc * b['reflect']
    t['SHIELDCOUNTER'] += iftbc * b['shield']

    if iftbc:
        t['SHIELD'] += b['shield']
    elif b['shield'] == 1000000:
        t['SHIELD'] -= b['shield']
        if t['SHIELD'] < 0:
            t['SHIELD'] = 0
    if t['SHIELDCOUNTER'] == 0:
        t['SHIELD'] = 0

    if 0 <= b['filter'] < FILTERS:
        t['FILTERSBUFFARRAY'][b['filter']] += iftbc

    slot = None
    if iftbc == 1:
        for i in range(MAX_BUFFS):
            if slot is None and t['BUFFARRAYK'][i]['CD'] == 0:
                t['BUFFARRAYK'][i]['CD'] = b['duration']
                t['BUFFARRAYK'][i]['buffId'] = b['key']
                slot = i

    t['SSWITCH'] += iftbc * b['sswitch']
    for s in range(12):
        t['changeArray'][s] += iftbc * b['change'][s]
    t['DOTTICKERARRAY'][9] += iftbc * b['focus_drain']

    for i in range(ELEMENTS):
        if i != b['element']:
            continue
        t['changeArrayEP'][i] += iftbc * b['per_flat']
        t['changeArrayEP2'][i] += iftbc * b['per_pct']
        t['changeArrayED'][i] += iftbc * b['def_flat']
        t['changeArrayED2'][i] += iftbc * b['def_pct']

        ghecker = (b['dot_flat'] + b['dot_strength'] + b['dot_magic']
                   + b['dot_speed'])
        if iftbc == 1:
            amount = math.ceil(b['dot_flat']
                               + b['dot_strength'] * caster['STRENGTHU']
                               + b['dot_magic'] * caster['MAGICU']
                               + iftbc * b['dot_speed'] * caster['SPEEDU'])
            if slot is not None:
                t['BUFFARRAYK'][slot]['buffValue'] = 0
                t['BUFFARRAYK'][slot]['buffValue'] += amount
        else:
            amount = -debuff_value

        if ghecker < 0:
            t['DOTTICKERARRAY'][8] += amount
        else:
            t['DOTTICKERARRAY'][i] += amount


def apply_changes(u):
    epy = u['changeArray']
    u['STUNP'] = u['STUN']
    u['STRENGTHU'] = math.ceil((u['STRENGTH'] + epy[0]) * (1 + epy[1]))
    u['MAGICU'] = math.ceil((u['MAGIC'] + epy[2]) * (1 + epy[3]))
    u['SPEEDU'] = math.ceil((u['SPEED'] + epy[4]) * (1 + epy[5]))

    lifer = math.ceil((u['LIFE'] + epy[6]) * (1 + epy[7]) - u['LIFEU'])
    was_alive = u['LIFEN'] > 0
    factor = u['LIFEN'] / u['LIFEU'] if u['LIFEU'] else 0
    u['LIFEU'] += lifer
    if lifer > 0:
        u['LIFEN'] += lifer
    else:
        u['LIFEN'] = math.floor(factor * u['LIFEU'] + 0.5)   # AS Math.round
    if u['LIFEN'] <= 0 and was_alive:
        u['LIFEN'] = 1
    if u['LIFEN'] > u['LIFEU']:
        u['LIFEN'] = u['LIFEU']

    u['DMG'], u['DMG2'] = epy[8], epy[9]
    u['IDMG'], u['IDMG2'] = epy[10], epy[11]

    for i in range(ELEMENTS):
        u['PERU'][i] = u['PER'][i] + u['changeArrayEP'][i]
        u['PERU'][i] *= u['changeArrayEP2'][i] + 1
    for i in range(ELEMENTS):
        u['DEFU'][i] = u['DEF'][i] + u['changeArrayED'][i]
        u['DEFU'][i] *= u['changeArrayED2'][i] + 1


def buff_tick(u, lib):
    total_focus = u['DOTTICKERARRAY'][9]
    total_dmg = u['DOTTICKERARRAY'][8]
    for e in range(ELEMENTS):
        total_dmg += math.ceil((1 + u['IDMG2'])
                               * (u['DOTTICKERARRAY'][e]
                                  * ((25 + u['plevel'] * 5) / u['DEFU'][e])))

    expired = []
    for b in range(MAX_BUFFS):
        if u['BUFFARRAYK'][b]['CD'] > 0:
            u['BUFFARRAYK'][b]['CD'] -= 1
            if u['BUFFARRAYK'][b]['CD'] == 0:
                expired.append(b)
    for b in expired:
        apply_buff(u, lib[u['BUFFARRAYK'][b]['buffId']], -1, None,
                   u['BUFFARRAYK'][b]['buffValue'])

    if u['SSWITCH'] > 0:
        total_dmg *= -1

    diff = (u['SHIELD'] - total_dmg) if (u['SHIELD'] > 0 and total_dmg > 0) else 0
    if diff > 0:
        u['SHIELD'] -= int(total_dmg)
    else:
        if total_dmg > 0:
            u['SHIELD'] = 0
        u['LIFEN'] -= int(total_dmg)

    u['FOCUSN'] -= int(total_focus)
    if u['FOCUSN'] > u['FOCUSU']:
        u['FOCUSN'] = u['FOCUSU']
    if u['FOCUSN'] < 0:
        u['FOCUSN'] = 0

    died = 0
    if u['LIFEN'] <= 0:
        u['LIFEN'] = 0
        u['FOCUSN'] = 0
        u['active'] = 0
        died = 1

    apply_changes(u)
    return total_dmg, total_focus, died


def make_buff(key, **kw):
    b = {'key': key, 'element': 0, 'change': [0.0] * 12, 'dot_flat': 0.0,
         'focus_drain': 0.0, 'duration': 3, 'stun': 0, 'reflect': 0,
         'shield': 0, 'per_flat': 0.0, 'def_flat': 0.0, 'per_pct': 0.0,
         'def_pct': 0.0, 'sswitch': 0, 'dot_strength': 0.0, 'dot_magic': 0.0,
         'dot_speed': 0.0, 'filter': 0}
    b.update(kw)
    return b


# A synthetic library shaped like the real buffs: stat swings, bleeds that
# scale off the caster, regen, shields, stuns, and a damage-to-healing flip.
LIBRARY = [
    make_buff('STATDOWN', change=[0, -0.15, 0, -0.15, 0, -0.15, 0, 0, 0, 0, 0, 0],
              duration=5, filter=0),
    make_buff('BLEED', element=0, dot_flat=4, dot_strength=0.12, duration=4,
              filter=1),
    make_buff('REGEN', element=6, dot_flat=-1000, duration=9, filter=0),
    make_buff('SHIELDUP', element=1, shield=500, duration=2, filter=0),
    make_buff('STUNNED', element=4, stun=1, duration=1, filter=0),
    make_buff('FLIP', element=6, sswitch=1, duration=3, filter=0),
    make_buff('TOUGH', change=[0, 0, 0, 0, 0, 0, 200, 0.25, 0, 0, 0, 0],
              element=2, def_flat=20, def_pct=0.3, duration=6, filter=0),
    make_buff('DRAIN', element=7, focus_drain=12, dot_magic=0.2, duration=3,
              filter=1),
    make_buff('MEGASHIELD', element=1, shield=1000000, duration=2, filter=0),
    make_buff('FRAIL', change=[0, 0, 0, 0, 0, 0, -300, 0, 0, 0, 40, 0.5],
              element=3, per_flat=-5, per_pct=-0.2, duration=4, filter=2),
]
BY_KEY = {b['key']: b for b in LIBRARY}

BUFF_ORDER = ['element'] + ['change%d' % i for i in range(12)] + [
    'dot_flat', 'focus_drain', 'duration', 'stun', 'reflect', 'shield',
    'per_flat', 'def_flat', 'per_pct', 'def_pct', 'sswitch', 'dot_strength',
    'dot_magic', 'dot_speed', 'filter']


def buff_tokens(b):
    out = [b['element']]
    out += b['change']
    out += [b['dot_flat'], b['focus_drain'], b['duration'], b['stun'],
            b['reflect'], b['shield'], b['per_flat'], b['def_flat'],
            b['per_pct'], b['def_pct'], b['sswitch'], b['dot_strength'],
            b['dot_magic'], b['dot_speed'], b['filter']]
    return out


def main():
    rnd = random.Random(4242)
    print('# generated by tools/ref_buffs.py -- do not edit')
    print('# BUFF key ' + ' '.join(BUFF_ORDER))
    for b in LIBRARY:
        print('BUFF %s %s' % (b['key'],
                              ' '.join(str(x) for x in buff_tokens(b))))
    print('# CASE plevel LIFE STRENGTH MAGIC SPEED PER DEF FOCUSU nbuffs '
          '<keys...> ticks EXPECT LIFEN LIFEU FOCUSN STRENGTHU MAGICU SPEEDU '
          'PERU0 DEFU0 SHIELD SHIELDCOUNTER STUN SSWITCH active last_damage')

    for _ in range(300):
        plevel = rnd.randint(1, 30)
        life = rnd.randint(50, 3000)
        strength = rnd.randint(1, 300)
        magic = rnd.randint(0, 300)
        speed = rnd.randint(1, 200)
        per = rnd.randint(5, 120)
        deff = rnd.randint(5, 200)
        focus = rnd.randint(20, 300)
        keys = [rnd.choice(LIBRARY)['key'] for _ in range(rnd.randint(1, 4))]
        ticks = rnd.randint(1, 12)

        u = new_unit(plevel, life, strength, magic, speed, per, deff, focus)
        apply_changes(u)
        for k in keys:
            apply_buff(u, BY_KEY[k], 1, CASTER, 0)
        apply_changes(u)
        last = (0, 0, 0)
        for _ in range(ticks):
            last = buff_tick(u, BY_KEY)

        print('CASE %d %d %d %d %d %d %d %d %d %s %d EXPECT %d %d %d %g %g %g '
              '%.10g %.10g %d %d %d %d %d %g' % (
                  plevel, life, strength, magic, speed, per, deff, focus,
                  len(keys), ' '.join(keys), ticks,
                  u['LIFEN'], u['LIFEU'], u['FOCUSN'], u['STRENGTHU'],
                  u['MAGICU'], u['SPEEDU'], u['PERU'][0], u['DEFU'][0],
                  u['SHIELD'], u['SHIELDCOUNTER'], u['STUN'], u['SSWITCH'],
                  u['active'], last[0]))


if __name__ == '__main__':
    sys.exit(main())
