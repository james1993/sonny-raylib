#!/usr/bin/env python3
"""Turn the extracted JSON tables into C source the engine links against.

Generating C rather than parsing JSON at runtime keeps the game data in one
authoritative place (the original's own tables) with no parser to get wrong,
and makes every value visible in a diff when the extractor changes.

    python3 tools/gen_c_data.py        # reads data/extracted, writes src/gen
"""
import argparse
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import content                                                # noqa: E402

ELEMENTS = ["Physical", "Magic", "Ice", "Fire", "Lightning", "Earth",
            "Shadow", "Poison"]


def c_float(value):
    """A C float literal that round-trips."""
    return '%.6ff' % float(value)


def c_string(value):
    if value is None:
        return '""'
    text = str(value)
    out = []
    for ch in text:
        if ch == '\\':
            out.append('\\\\')
        elif ch == '"':
            out.append('\\"')
        elif ch == '\n':
            out.append('\\n')
        elif ch == '\t':
            out.append('\\t')
        elif ord(ch) < 32:
            out.append('\\%03o' % ord(ch))
        elif ord(ch) > 126:
            out.extend('\\%03o' % b for b in ch.encode('utf-8'))
        else:
            out.append(ch)
    return '"%s"' % ''.join(out)


def num(value, default=0):
    if isinstance(value, bool):
        return int(value)
    if isinstance(value, (int, float)):
        return value
    return default


def element_index(name):
    return ELEMENTS.index(name) if name in ELEMENTS else 0


def fmt(value):
    """Emit a double that round-trips."""
    v = float(value)
    if v == int(v) and abs(v) < 1e15:
        return '%d' % int(v)
    return repr(v)


def gen_abilities(abilities):
    lines = []
    ids = [a['id'] for a in abilities]
    lines.append('const AbilityDef SONNY_ABILITIES[] = {')
    for a in abilities:
        c = a['combat']
        kind = a.get('damage_kind') or ''
        delivery = a.get('delivery') or ''
        buff = c.get('buff')
        buff_key = buff if isinstance(buff, str) else ''
        lines.append('    { /* %d %s */' % (a['id'], (a.get('name') or a.get('icon') or '?')))
        lines.append('        .id = %d, .name = %s, .icon = %s,'
                     % (a['id'], c_string(a.get('name') or ''), c_string(a.get('icon') or '')))
        lines.append('        .target_self = %d, .target_enemy = %d, .target_ally = %d,'
                     % (num(a.get('target_self')), num(a.get('target_enemy')),
                        num(a.get('target_ally'))))
        lines.append('        .focus_cost = %d, .health_cost = %d, .cooldown = %d,'
                     % (num(a.get('focus_cost')), num(a.get('health_cost')),
                        num(a.get('cooldown'))))
        lines.append('        .bar_copies = %d,'
                     % int(num(a.get('bar_copies')) or 0))
        lines.append('        .health_cost_pct = %s, .anim_speed = %s,'
                     % (fmt(num(a.get('health_cost_pct'))),
                        fmt(num(a.get('anim_speed')))))
        # addNewMove("None") supplies no cost arguments, leaving them
        # undefined. The original's affordability test then compares against
        # undefined, which is false in ActionScript, so such a move can never
        # be chosen. Flag it rather than defaulting the costs to zero.
        costs_defined = 0 if (a.get('focus_cost') is None
                              or a.get('health_cost') is None
                              or a.get('health_cost_pct') is None) else 1
        lines.append('        .costs_defined = %d,' % costs_defined)
        lines.append('        .kind = %s, .delivery = %s,'
                     % ('KIND_FULL_DAMAGE' if kind == 'Full Damage' else
                        'KIND_HEAL' if kind == 'Heal' else
                        'KIND_FOCUS' if kind == 'Focus' else 'KIND_NONE',
                        'DELIVER_MELEE' if delivery == 'Melee' else
                        'DELIVER_MISSILE' if delivery == 'Missile' else
                        'DELIVER_SHOCK' if delivery == 'Shock' else
                        'DELIVER_NONE'))
        # Slot 12. The melee path leaves it unused, but a missile throws it:
        # it names the projectile clip the bolt is made from.
        lines.append('        .projectile = %s,'
                     % c_string((a.get('anim') or '')
                                if a.get('delivery') == 'Missile' else ''))
        # Slot 11: the colour the move paints its own effects with.
        colour = a.get('color') or '0xFFFFFF'
        colour = (int(str(colour), 16) if str(colour).lower().startswith('0x')
                  else int(colour))
        lines.append('        .colour = 0x%06X,' % (colour & 0xFFFFFF))
        lines.append('        .buff = %s, .sound = %s, .model = %s,'
                     % (c_string(buff_key), c_string(a.get('sound') or ''),
                        c_string(a.get('model') or '')))
        lines.append('        .tooltip = %s,' % c_string(c.get('tooltip') or ''))
        lines.append('        .cost_text = %s,'
                     % c_string(c.get('cost_text') or ''))
        lines.append('        .coefs = { .element = %d, .strength_add = %s, '
                     '.strength_coef = %s,'
                     % (element_index(c.get('element')),
                        fmt(num(c.get('strength_add'))),
                        fmt(num(c.get('strength_coef')))))
        lines.append('            .magic_add = %s, .magic_coef = %s, '
                     '.speed_add = %s, .speed_coef = %s,'
                     % (fmt(num(c.get('magic_add'))), fmt(num(c.get('magic_coef'))),
                        fmt(num(c.get('speed_add'))), fmt(num(c.get('speed_coef')))))
        lines.append('            .hit_add = %s, .hit_coef = %s, .flat_damage = %s,'
                     % (fmt(num(c.get('hit_add'))), fmt(num(c.get('hit_coef'))),
                        fmt(num(c.get('flat_damage')))))
        lines.append('            .damage_coef = %s, .focus_coef = %s },'
                     % (fmt(num(c.get('damage_coef'))), fmt(num(c.get('focus_coef')))))
        mask = 0
        for name in (c.get('dispel_elements') or []):
            if name in ELEMENTS:
                mask |= 1 << ELEMENTS.index(name)
        lines.append('        .dispel_count = %d, .dispel_elements = 0x%02X, '
                     '.dispel_nature = %d, .hits_team = %d,'
                     % (int(num(c.get('dispel_count'))), mask,
                        int(num(c.get('dispel_nature'), 1)),
                        1 if c.get('hits_team') else 0))
        lines.append('    },')
    lines.append('};')
    lines.append('const int SONNY_ABILITY_COUNT = '
                 '(int)(sizeof(SONNY_ABILITIES) / sizeof(SONNY_ABILITIES[0]));')
    lines.append('const int SONNY_ABILITY_MAX_ID = %d;' % max(ids))
    return '\n'.join(lines)


def gen_buffs(buffs):
    lines = ['const BuffDef SONNY_BUFFS[] = {']
    changes = ['strength_add', 'strength_pct', 'magic_add', 'magic_pct',
               'speed_add', 'speed_pct', 'life_add', 'life_pct',
               'damage_add', 'damage_pct', 'damage_taken_add',
               'damage_taken_pct']
    for b in buffs:
        f = b['fields']

        def g(name):
            return num(f.get(name))

        lines.append('    { /* %s */' % b['key'])
        lines.append('        .key = %s, .name = %s, .element = %d,'
                     % (c_string(b['key']), c_string(b.get('name') or ''),
                        element_index(b.get('element'))))
        lines.append('        .change = { %s },'
                     % ', '.join(fmt(g(name)) for name in changes))
        lines.append('        .dot_flat = %s, .focus_drain = %s, .duration = %d,'
                     % (fmt(g('dot_flat')), fmt(g('focus_drain')),
                        int(num(g('duration')))))
        lines.append('        .stun = %d, .reflect = %d, .shield = %d,'
                     % (int(num(g('stun'))), int(num(g('reflect'))),
                        int(num(g('shield')))))
        lines.append('        .per_flat = %s, .def_flat = %s, .per_pct = %s, '
                     '.def_pct = %s,'
                     % (fmt(g('per_flat')), fmt(g('def_flat')),
                        fmt(g('per_pct')), fmt(g('def_pct'))))
        lines.append('        .sswitch = %d, .dot_strength = %s, .dot_magic = %s,'
                     % (int(num(g('sswitch'))), fmt(g('dot_strength')),
                        fmt(g('dot_magic'))))
        lines.append('        .dot_speed = %s, .filter = %d,'
                     % (fmt(g('dot_speed')), int(num(g('filter')))))
        lines.append('        .nature = %d, .unique = %d,'
                     % (int(num(g('nature'))), 1 if g('unique') else 0))
        # The line the interface shows for the buff: on the widget over a
        # unit's bar, and on a passive node in the talent tree.
        say = f.get('tooltip')
        lines.append('        .tooltip = %s,'
                     % c_string(say if isinstance(say, str) else ''))
        lines.append('    },')
    lines.append('};')
    lines.append('const int SONNY_BUFF_COUNT = '
                 '(int)(sizeof(SONNY_BUFFS) / sizeof(SONNY_BUFFS[0]));')
    return '\n'.join(lines)


def gen_units(units):
    lines = ['const UnitTemplate SONNY_UNITS[] = {']
    for u in units:
        per = u.get('PER') or [25] * 8
        deff = u.get('DEF') or [25] * 8
        ag = u.get('agressionArray') or [50, 90, 60, 40]
        moves_a = [m for m in (u.get('movesA') or []) if isinstance(m, int)]
        moves_d = [m for m in (u.get('movesD') or []) if isinstance(m, int)]
        lines.append('    { /* %d %s */' % (u['id'], u.get('name') or '?'))
        lines.append('        .id = %d, .name = %s,' % (u['id'], c_string(u.get('name'))))
        lines.append('        .life = %s, .life_growth = %s,'
                     % (fmt(num(u.get('vitality'))), fmt(num(u.get('vitality_growth')))))
        lines.append('        .strength = %s, .strength_growth = %s,'
                     % (fmt(num(u.get('strength'))), fmt(num(u.get('strength_growth')))))
        lines.append('        .magic = %s, .magic_growth = %s,'
                     % (fmt(num(u.get('magic'))), fmt(num(u.get('magic_growth')))))
        lines.append('        .speed = %s, .speed_growth = %s,'
                     % (fmt(num(u.get('speed'))), fmt(num(u.get('speed_growth')))))
        lines.append('        .focus = %s,' % fmt(num(u.get('focus'), 100)))
        lines.append('        .per = { %s },' % ', '.join(fmt(num(x, 25)) for x in per))
        lines.append('        .def = { %s },' % ', '.join(fmt(num(x, 25)) for x in deff))
        lines.append('        .aggression = %d, .life_boundary1 = %d, '
                     '.life_boundary2 = %d, .focus_aggression = %d,'
                     % (int(num(ag[0], 50)), int(num(ag[1], 90)),
                        int(num(ag[2], 60)), int(num(ag[3], 40))))
        lines.append('        .moves_a = { %s }, .moves_a_count = %d,'
                     % (', '.join(str(m) for m in moves_a) or '0', len(moves_a)))
        lines.append('        .moves_d = { %s }, .moves_d_count = %d,'
                     % (', '.join(str(m) for m in moves_d) or '0', len(moves_d)))
        # Appearance: the model array is [base, skin, hair, gender] and the
        # equipment array holds item ids whose `looks` dress each slot. A
        # skinSetter overrides the first five slots.
        model = u.get('model') or []
        model = [m if isinstance(m, str) else '' for m in (list(model) + [''] * 4)]
        equipment = [e if isinstance(e, int) else 0
                     for e in ((u.get('equipment') or []) + [0] * 7)]
        skin_setter = u.get('skinSetter')
        lines.append('        .model_skin = %s, .model_hair = %s, '
                     '.model_gender = %s,'
                     % (c_string(model[1]), c_string(model[2]),
                        c_string(model[3] or 'M')))
        lines.append('        .equipment = { %s },'
                     % ', '.join(str(e) for e in equipment[:7]))
        lines.append('        .skin_setter = %s,'
                     % c_string(skin_setter if isinstance(skin_setter, str)
                                else ''))
        voices = [v for v in (u.get('voiceHit') or []) if isinstance(v, str)]
        voices = (voices + [''] * 3)[:3]
        lines.append('        .voice_hit = { %s },'
                     % ', '.join(c_string(v) for v in voices))
        lines.append('        .voice_die = %s,'
                     % c_string(u.get('voiceDie') if isinstance(
                         u.get('voiceDie'), str) else ''))
        lines.append('    },')
    lines.append('};')
    lines.append('const int SONNY_UNIT_COUNT = '
                 '(int)(sizeof(SONNY_UNITS) / sizeof(SONNY_UNITS[0]));')
    return '\n'.join(lines)


# Keep in step with the #defines in HEADER; the generator refuses to emit data
# that would overflow them rather than truncating it.
MAX_DROPS, MAX_RARE, MAX_TRAINING = 8, 24, 16


def gen_lang(langs, language='ENGLISH'):
    """The display text, as the game's own named arrays."""
    table = langs.get(language) or {}
    # Most of the text is in numbered arrays, but BUFFSAY is an object the
    # tree looks up by name, so the two are kept apart rather than one being
    # forced into the other's shape.
    arrays = [a for a in sorted(table) if not isinstance(table[a], dict)]
    keyed = [a for a in sorted(table) if isinstance(table[a], dict)]
    lines = []
    for array in arrays:
        values = table[array]
        lines.append('static const char *const LANG_%s[] = { %s };'
                     % (array, ', '.join(c_string(v if v is not None else '')
                                         for v in values)))
    lines.append('')
    lines.append('const LangArray SONNY_LANG[] = {')
    for array in arrays:
        lines.append('    { %s, LANG_%s, %d },'
                     % (c_string(array), array, len(table[array])))
    lines.append('};')
    lines.append('const int SONNY_LANG_COUNT = '
                 '(int)(sizeof(SONNY_LANG) / sizeof(SONNY_LANG[0]));')
    lines.append('')
    lines.append('const LangEntry SONNY_LANG_KEYED[] = {')
    for name in keyed:
        for key in sorted(table[name]):
            lines.append('    { %s, %s, %s },'
                         % (c_string(name), c_string(key),
                            c_string(table[name][key] or '')))
    lines.append('};')
    lines.append('const int SONNY_LANG_KEYED_COUNT = '
                 '(int)(sizeof(SONNY_LANG_KEYED) / '
                 'sizeof(SONNY_LANG_KEYED[0]));')
    return '\n'.join(lines)


def gen_battles(battles):
    lines = []
    # Battle dialogue. A line shows when the turn counter reaches its
    # turnTime and the within-turn counter its turnTime2, provided the
    # speaker is still alive.
    for b in battles:
        speeches = [s for s in (b.get('speeches') or []) if isinstance(s, dict)]
        if not speeches:
            continue
        lines.append('static const Speech SPEECHES_%d[] = {' % b['id'])
        for sp in speeches:
            lines.append('    { %d, %d, %d, %s, %s, %s },'
                         % (int(num(sp.get('player'))),
                            int(num(sp.get('turnTime'))),
                            int(num(sp.get('turnTime2'))),
                            c_float(num(sp.get('timeToSay'), 4)),
                            c_string(sp.get('say') or ''),
                            c_string(sp.get('voiceOver') or '')))
        lines.append('};')
    lines.append('')
    lines.append('const BattleDef SONNY_BATTLES[] = {')
    for b in battles:
        players = (b.get('players') or [0] * 5) + [0] * 5
        levels_raw = (b.get('playersLevels') or [0] * 5) + [0] * 5
        # A level of "X" means "same as the player"; stored as -1.
        levels = []
        for lv in levels_raw[:5]:
            if isinstance(lv, str):
                levels.append(-1)
            else:
                levels.append(int(num(lv)))
        drops = [d for d in (b.get('itemDrops') or []) if isinstance(d, dict)]
        rare = [r for r in (b.get('itemRare') or []) if isinstance(r, int)]
        if len(drops) > MAX_DROPS or len(rare) > MAX_RARE:
            raise SystemExit('battle %d has %d drops and %d rare entries; '
                             'raise SONNY_MAX_DROPS/SONNY_MAX_RARE'
                             % (b['id'], len(drops), len(rare)))

        lines.append('    { /* battle %d */' % b['id'])
        lines.append('        .id = %d,' % b['id'])
        lines.append('        .players = { %s },'
                     % ', '.join(str(int(num(p))) for p in players[:5]))
        lines.append('        .levels = { %s },'
                     % ', '.join(str(lv) for lv in levels))
        lines.append('        .absolute_start = %d, .win_date = %d,'
                     % (int(num(b.get('absoluteStart'))),
                        int(num(b.get('winDate'), -1))))
        lines.append('        .win_date_condition = %d, .time_lock = %d,'
                     % (int(num(b.get('winDateCondition'))),
                        1 if b.get('timeLock') else 0))
        lines.append('        .zone_bg = %s, .sky_bg = %s,'
                     % (c_string(b.get('ZoneBG') or ''),
                        c_string(b.get('SkyBG') or '')))
        lines.append('        .drops = { %s }, .drop_count = %d,'
                     % (', '.join('{ %d, %d }' % (int(num(d.get('ID'))),
                                                  int(num(d.get('CHANCE'))))
                                  for d in drops) or '{ 0, 0 }', len(drops)))
        lines.append('        .rare = { %s }, .rare_count = %d, '
                     '.rare_dropper = %d,'
                     % (', '.join(str(r) for r in rare) or '0', len(rare),
                        int(num(b.get('itemRareDropper')))))
        speeches = [sp for sp in (b.get('speeches') or [])
                    if isinstance(sp, dict)]
        lines.append('        .speeches = %s, .speech_count = %d,'
                     % (('SPEECHES_%d' % b['id']) if speeches else 'NULL',
                        len(speeches)))
        lines.append('    },')
    lines.append('};')
    lines.append('const int SONNY_BATTLE_COUNT = '
                 '(int)(sizeof(SONNY_BATTLES) / sizeof(SONNY_BATTLES[0]));')
    return '\n'.join(lines)


SHOP_SLOTS = 15


def gen_zones(zones):
    lines = ['const ZoneDef SONNY_ZONES[] = {']
    for z in zones:
        training = [t for t in (z.get('training') or []) if isinstance(t, int)]
        if len(training) > MAX_TRAINING:
            raise SystemExit('zone %d has %d training battles; raise '
                             'SONNY_MAX_TRAINING' % (z['zone'], len(training)))
        lines.append('    { /* zone %d %s */' % (z['zone'], z.get('name') or '?'))
        lines.append('        .zone = %d, .name = %s, .subtitle = %s,'
                     % (z['zone'], c_string(z.get('name') or ''),
                        c_string(z.get('subtitle') or '')))
        lines.append('        .first_battle = %d, .last_battle = %d,'
                     % (int(num(z.get('first_battle'))),
                        int(num(z.get('last_battle')))))
        lines.append('        .training = { %s }, .training_count = %d,'
                     % (', '.join(str(t) for t in training) or '0',
                        len(training)))
        lines.append('    },')
    lines.append('};')
    lines.append('const int SONNY_ZONE_COUNT = '
                 '(int)(sizeof(SONNY_ZONES) / sizeof(SONNY_ZONES[0]));')
    return '\n'.join(lines)


def gen_shops(shops):
    """The stores, exactly as krinSetShop stocks them, and which button on a
    zone's scene opens which one."""
    stock = shops.get('stock') or {}
    buttons = shops.get('buttons') or {}
    lines = ['const ShopDef SONNY_SHOPS[] = {']
    for key in sorted(stock, key=int):
        items = list(stock[key])[:SHOP_SLOTS]
        items += [0] * (SHOP_SLOTS - len(items))
        lines.append('    { .id = %s, .item = { %s } },'
                     % (key, ', '.join(str(i) for i in items)))
    lines.append('};')
    lines.append('const int SONNY_SHOP_COUNT = '
                 '(int)(sizeof(SONNY_SHOPS) / sizeof(SONNY_SHOPS[0]));')
    return '\n'.join(lines)


PARTY_SIZE = 6


def gen_party(party):
    """Sonny and the five who can fight beside him, with who has joined when
    the story starts and who it hands over later."""
    def arr(m, key, n):
        v = m.get(key) or []
        v = [int(num(x)) for x in v][:n]
        v += [0] * (n - len(v))
        return ', '.join(str(x) for x in v)

    lines = ['const PartyMember SONNY_PARTY[] = {']
    for m in party['members']:
        lines.append('    { /* %s */' % (m.get('NameSets') or '?'))
        lines.append('        .index = %d, .name = %s, .class_id = %d,'
                     % (m['index'], c_string(str(m.get('NameSets') or '')),
                        int(num(m.get('ClassStats') or 0))))
        lines.append('        .level = %d, .gender = %d, .skin = %d, '
                     '.hair = %d,'
                     % (int(num(m.get('LevelStats') or 0)),
                        int(num(m.get('GSet') or 0)),
                        int(num(m.get('SkinSet') or 0)),
                        int(num(m.get('HairSet') or 0))))
        lines.append('        .equip = { %s },' % arr(m, 'equipArray', 7))
        lines.append('        .stat = { %s },' % arr(m, 'StatSets', 5))
        lines.append('        .per = { %s },' % arr(m, 'PerSets', 8))
        lines.append('        .def_ = { %s },' % arr(m, 'DefSets', 8))
        lines.append('        .aggression = { %s },' % arr(m, 'agArray', 4))
        lines.append('    },')
    lines.append('};')
    lines.append('const int SONNY_PARTY_COUNT = '
                 '(int)(sizeof(SONNY_PARTY) / sizeof(SONNY_PARTY[0]));')
    lines.append('')
    starting = [int(num(v)) for v in (party.get('starting') or [])]
    starting += [-1] * (PARTY_SIZE - len(starting))
    lines.append('const int32_t SONNY_PARTY_START[SONNY_PARTY_SIZE] = { %s };'
                 % ', '.join(str(v) for v in starting[:PARTY_SIZE]))
    team = [int(num(v)) for v in (party.get('team') or [])]
    team += [0] * (2 - len(team))
    lines.append('const int32_t SONNY_PARTY_TEAM[2] = { %s };'
                 % ', '.join(str(v) for v in team[:2]))
    lines.append('')
    lines.append('const PartyJoin SONNY_PARTY_JOINS[] = {')
    for j in (party.get('joins') or []):
        friends = [int(num(v)) for v in j['friends']]
        friends += [-1] * (PARTY_SIZE - len(friends))
        lines.append('    { %d, %d, { %s } },'
                     % (j['at'], 1 if j['exact'] else 0,
                        ', '.join(str(v) for v in friends[:PARTY_SIZE])))
    lines.append('};')
    lines.append('const int SONNY_PARTY_JOIN_COUNT = '
                 '(int)(sizeof(SONNY_PARTY_JOINS) / '
                 'sizeof(SONNY_PARTY_JOINS[0]));')
    return '\n'.join(lines)


def gen_cutscenes(cutscenes):
    """Where each cutscene changes its caption."""
    lines = ['const CutsceneCue SONNY_CUTSCENE_CUES[] = {']
    for cid in sorted(cutscenes, key=int):
        for cue in cutscenes[cid]['cues']:
            lines.append('    { %s, %d, %d },'
                         % (cid, cue['frame'], 1 if cue['clear'] else 0))
    lines.append('};')
    lines.append('const int SONNY_CUTSCENE_CUE_COUNT = '
                 '(int)(sizeof(SONNY_CUTSCENE_CUES) / '
                 'sizeof(SONNY_CUTSCENE_CUES[0]));')
    lines.append('')
    lines.append('const CutsceneDef SONNY_CUTSCENES[] = {')
    for cid in sorted(cutscenes, key=int):
        lines.append('    { %s, %d },' % (cid, cutscenes[cid]['start']))
    lines.append('};')
    lines.append('const int SONNY_CUTSCENE_COUNT = '
                 '(int)(sizeof(SONNY_CUTSCENES) / sizeof(SONNY_CUTSCENES[0]));')
    return '\n'.join(lines)


MARKER_KINDS = {'progress': 'MARKER_PROGRESS', 'training': 'MARKER_TRAINING',
                'shop': 'MARKER_SHOP', 'scenery': 'MARKER_SCENERY'}


def gen_story(story):
    lines = ['const HubNote SONNY_HUB_NOTES[] = {']
    for note in story.get('hub_notes') or []:
        lines.append('    { %d, %d },' % (note['at'], note['say']))
    lines.append('};')
    lines.append('const int SONNY_HUB_NOTE_COUNT = '
                 '(int)(sizeof(SONNY_HUB_NOTES) / sizeof(SONNY_HUB_NOTES[0]));')
    music = story.get('boss_music') or []
    lines.append('const int32_t SONNY_BOSS_MUSIC[] = { %s };'
                 % ', '.join(str(at) for at in music))
    lines.append('const int SONNY_BOSS_MUSIC_COUNT = %d;' % len(music))
    lines.append('const BossComic SONNY_BOSS_COMICS[] = {')
    for comic in story.get('boss_comics') or []:
        lines.append('    { %d, %s },' % (comic['at'], c_string(comic['comic'])))
    lines.append('};')
    lines.append('const int SONNY_BOSS_COMIC_COUNT = '
                 '(int)(sizeof(SONNY_BOSS_COMICS) / sizeof(SONNY_BOSS_COMICS[0]));')
    lines.append('const int32_t SONNY_OPENING_BATTLE = %d;'
                 % int(story.get('opening_battle') or 1))
    return '\n'.join(lines)


def gen_zone_buttons(buttons):
    """What each marker on a zone's scene does, by its button's own id."""
    lines = ['const ZoneButton SONNY_ZONE_BUTTONS[] = {']
    for key in sorted(buttons, key=int):
        b = buttons[key]
        lines.append('    { %s, %s, %d, %d, %d, %d },'
                     % (key, MARKER_KINDS[b['kind']], b.get('shop', -1),
                        b.get('choices', 0), b.get('say', 0),
                        b.get('element', -1)))
    lines.append('};')
    lines.append('const int SONNY_ZONE_BUTTON_COUNT = '
                 '(int)(sizeof(SONNY_ZONE_BUTTONS) / '
                 'sizeof(SONNY_ZONE_BUTTONS[0]));')
    return '\n'.join(lines)


def gen_elements(elements):
    lines = ['const ElementDef SONNY_ELEMENT_DEFS[] = {']
    for e in elements:
        lines.append('    { %s, 0x%06Xu },' % (c_string(e['name']),
                                               e['colour']))
    lines.append('};')
    lines.append('const int SONNY_ELEMENT_DEF_COUNT = '
                 '(int)(sizeof(SONNY_ELEMENT_DEFS) / sizeof(SONNY_ELEMENT_DEFS[0]));')
    return '\n'.join(lines)


def gen_talents(talents):
    nodes = talents['nodes']
    lines = ['const TalentDef SONNY_TALENTS[] = {']
    for n in nodes:
        pre = [p for p in (n.get('PRESKILL') or []) if isinstance(p, int)]
        # PRESKILL [-1] means "no prerequisite": the original indexes
        # talentMainArray[-1], gets undefined, and `undefined == 0` is false in
        # ActionScript, so the check passes. Kept as -1 and skipped explicitly.
        lines.append('    { /* %d -> ability %s */' % (n['index'], n.get('ID')))
        lines.append('        .index = %d, .ability_id = %d, .level_min = %d,'
                     % (n['index'], int(num(n.get('ID'))),
                        int(num(n.get('LEVELMIN')))))
        lines.append('        .level_scale = %d, .max_rank = %d, .passive = %d,'
                     % (int(num(n.get('LEVELSCALE'))), int(num(n.get('TIER'))),
                        int(num(n.get('CLASSIFY')))))
        buffname = n.get('BUFFNAME')
        lines.append('        .buff_name = %s,'
                     % c_string(buffname if isinstance(buffname, str) else ''))
        lines.append('        .prereq = { %s }, .prereq_count = %d,'
                     % (', '.join(str(p) for p in pre) or '-1', len(pre)))
        lines.append('    },')
    lines.append('};')
    lines.append('const int SONNY_TALENT_COUNT = '
                 '(int)(sizeof(SONNY_TALENTS) / sizeof(SONNY_TALENTS[0]));')
    lines.append('const int32_t SONNY_START_SKILL1 = %d;'
                 % int(num(talents.get('startSkill1'))))
    lines.append('const int32_t SONNY_START_SKILL2 = %d;'
                 % int(num(talents.get('startSkill2'))))
    return '\n'.join(lines)


def gen_items(items):
    lines = ['const ItemDef SONNY_ITEMS[] = {']
    for it in items:
        lines.append('    { /* %d %s */' % (it['id'], it.get('name') or '?'))
        lines.append('        .id = %d, .name = %s, .icon = %s, .looks = %s,'
                     % (it['id'], c_string(it.get('name') or ''),
                        c_string(it.get('icon') or ''),
                        c_string(it.get('looks') or '')))
        lines.append('        .slot = %d, .rarity = %s, .class_req = %d,'
                     % (int(num(it.get('slot'))), c_string(it.get('rarity') or ''),
                        int(num(it.get('class_req')))))
        lines.append('        .level_req = %d, .price = %d,'
                     % (int(num(it.get('level_req'))), int(num(it.get('price')))))
        lines.append('        .stat = { %s },'
                     % ', '.join(fmt(num(x)) for x in it['statUpdater']))
        lines.append('        .per = { %s },'
                     % ', '.join(fmt(num(x)) for x in it['statUpdaterP']))
        lines.append('        .def = { %s },'
                     % ', '.join(fmt(num(x)) for x in it['statUpdaterD']))
        lines.append('        .tooltip = %s,' % c_string(it.get('tooltip') or ''))
        lines.append('    },')
    lines.append('};')
    lines.append('const int SONNY_ITEM_COUNT = '
                 '(int)(sizeof(SONNY_ITEMS) / sizeof(SONNY_ITEMS[0]));')
    return '\n'.join(lines)


HEADER = '''/* GENERATED by tools/gen_c_data.py from the extracted JSON tables -- do not edit.
 *
 * These are the original game's own tables, read out of SONNY1.swf.
 */
#ifndef SONNY_GAMEDATA_H
#define SONNY_GAMEDATA_H

#include "../core/buffs.h"
#include "../core/formula.h"

#define SONNY_MAX_TEMPLATE_MOVES 12
#define SONNY_TOOLTIP_LEN 512

typedef enum {
    KIND_NONE = 0,
    KIND_FULL_DAMAGE,
    KIND_HEAL,
    KIND_FOCUS
} MoveKind;

typedef enum {
    DELIVER_NONE = 0,
    DELIVER_MELEE,
    DELIVER_MISSILE,
    DELIVER_SHOCK
} Delivery;

typedef struct {
    int32_t      id;
    const char  *name;
    const char  *icon;
    int32_t      target_self;
    int32_t      target_enemy;
    int32_t      target_ally;
    int32_t      focus_cost;
    int32_t      health_cost;
    int32_t      cooldown;
    /* How many places on the action bar this move may take at once. The
       original checks it before it lets a move be dropped on a slot. */
    int32_t      bar_copies;
    /* 0 when the original left the cost fields undefined (only the "None"
       placeholder). Comparing a number against undefined is false in
       ActionScript, so such a move fails every affordability test -- which is
       what keeps the AI from ever selecting it. */
    int32_t      costs_defined;
    double       health_cost_pct;
    double       anim_speed;
    MoveKind     kind;
    Delivery     delivery;
    const char  *buff;       /* "" when the move applies none */
    const char  *sound;      /* effect sound, played on impact */
    const char  *model;      /* the impact graphic (BOOM_*) */
    /* What a missile throws. The original keeps it in the slot the melee
       path leaves unused, so it is empty for everything else. */
    const char  *projectile;
    /* Slot 11, the move's own colour, as 0xRRGGBB. It tints the streak a
       projectile leaves behind it, and the cast effect on the caster. */
    int32_t      colour;
    const char  *tooltip;
    /* Slot 18: what the move costs, in the words addNewMove() builds as it
       registers it -- "Costs 15 Focus. (CD: 6)". The tree shows it as the
       third line of a move's tip. */
    const char  *cost_text;
    AbilityCoefs coefs;
    /* The dispel the move runs as it lands, before anything else it does:
       up to dispel_count of the target's buffs whose element is one of the
       dispel_elements bits (1 << Element) and whose nature is dispel_nature
       -- 1 to take helpful buffs off, -1 to take harmful ones. */
    int32_t      dispel_count;
    uint32_t     dispel_elements;
    int32_t      dispel_nature;
    /* Set: the move lands on every living member of the other team. No move
       in the game sets it, but the engine honours it as the original does. */
    int32_t      hits_team;
} AbilityDef;

/* One of the game's named text arrays (SYSTEM, MENU, VICTORY, ZONES, ...).
   Looked up by name and index so call sites read like the original's. */
typedef struct {
    const char        *name;
    const char *const *values;
    int32_t            count;
} LangArray;

/* One line of a text table the game looks up by name rather than by index.
   BUFFSAY is the only one: the ability tree reads BUFFSAY[name] for what a
   passive is called and BUFFSAY[name + rank] for what that rank of it does. */
typedef struct {
    const char *table;
    const char *key;
    const char *value;
} LangEntry;

#define SONNY_MAX_PREREQ 4
#define SONNY_BATTLE_SLOTS 5     /* players[0..4] fill slots 2..6 */
#define SONNY_MAX_DROPS 8
#define SONNY_MAX_RARE 24
#define SONNY_MAX_TRAINING 16

typedef struct {
    int32_t item_id;
    int32_t chance;          /* drops when CHANCE > random(100) */
} ItemDrop;

/* A line of battle dialogue. It shows when the battle's turn counter reaches
   `turn` and the within-turn counter reaches `sequence`, and holds for
   `seconds` at the original's 30 fps. */
typedef struct {
    int32_t     speaker;     /* slot */
    int32_t     turn;
    int32_t     sequence;
    float       seconds;
    const char *say;
    const char *voice_over;
} Speech;

/* One battle's roster (a KBR object).
 *
 * players[i] fills slot i + 2: a positive value is an enemy unit template, a
 * negative one selects a party ally (index players[i] + 2), and 0 leaves the
 * slot empty. A level of -1 stands for the original's "X", meaning "match the
 * player's level". Slots are parity teams, so allies land in 3 and 5.
 *
 * Drops are rolled when the battle STARTS, not when it is won -- which is
 * what the original does in frame 196. */
typedef struct {
    int32_t     id;
    int32_t     players[SONNY_BATTLE_SLOTS];
    int32_t     levels[SONNY_BATTLE_SLOTS];
    int32_t     absolute_start;   /* forces a team to move first */
    int32_t     win_date;         /* turn count for a timed objective, -1 none */
    int32_t     win_date_condition;
    int32_t     time_lock;
    const char *zone_bg;
    const char *sky_bg;
    ItemDrop    drops[SONNY_MAX_DROPS];
    int32_t     drop_count;
    int32_t     rare[SONNY_MAX_RARE];
    int32_t     rare_count;
    int32_t      rare_dropper;    /* how many rare picks to make */
    const Speech *speeches;
    int32_t      speech_count;
} BattleDef;

typedef struct {
    int32_t     zone;
    const char *name;
    const char *subtitle;
    int32_t     first_battle;
    int32_t     last_battle;
    int32_t     training[SONNY_MAX_TRAINING];
    int32_t     training_count;
} ZoneDef;

/* The eight elements, in the order everything else indexes them by, with the
   colour the interface gives each -- the tint on a chosen ability's orb, and
   on the number that floats off a hit. */
typedef struct {
    const char *name;
    uint32_t    colour;
} ElementDef;

/* One node of the talent tree (Krin.abilityXer). A node's rank N uses ability
 * id `ability_id + N - 1`, which is why the ability table holds each move five
 * times in a row with rising coefficients. A passive node instead contributes
 * the buff `buff_name` with the rank number appended -- "REGENERATION" + 2
 * really is the key "REGENERATION2". */
typedef struct {
    int32_t     index;
    int32_t     ability_id;
    int32_t     level_min;
    int32_t     level_scale;
    int32_t     max_rank;        /* TIER */
    int32_t     passive;         /* CLASSIFY */
    const char *buff_name;
    int32_t     prereq[SONNY_MAX_PREREQ];
    int32_t     prereq_count;
} TalentDef;

/* An equippable item. statUpdater is [Health, Strength, Magic, Speed, Focus];
   per/def are the per-element piercing and defense bonuses. */
typedef struct {
    int32_t     id;
    const char *name;
    const char *icon;
    const char *looks;
    int32_t     slot;        /* 0 = not equipment; otherwise ITEMSS[slot-2] */
    const char *rarity;
    int32_t     class_req;
    int32_t     level_req;
    int32_t     price;
    double      stat[5];
    double      per[SONNY_ELEMENTS];
    double      def[SONNY_ELEMENTS];
    const char *tooltip;
} ItemDef;

typedef struct {
    int32_t     id;
    const char *name;
    double      life, life_growth;
    double      strength, strength_growth;
    double      magic, magic_growth;
    double      speed, speed_growth;
    double      focus;
    double      per[SONNY_ELEMENTS];
    double      def[SONNY_ELEMENTS];
    int32_t     aggression;      /* agressionArray[0] */
    int32_t     life_boundary1;  /* [1] */
    int32_t     life_boundary2;  /* [2] */
    int32_t     focus_aggression;/* [3] */
    int32_t     moves_a[SONNY_MAX_TEMPLATE_MOVES];
    int32_t     moves_a_count;
    int32_t     moves_d[SONNY_MAX_TEMPLATE_MOVES];
    int32_t     moves_d_count;
    /* Appearance. The model array is [base, skin, hair, gender]; equipment
       holds item ids whose `looks` string dresses each slot; a non-empty
       skin_setter overrides the first five slots' looks. */
    const char *model_skin;
    const char *model_hair;
    const char *model_gender;
    int32_t     equipment[7];
    const char *skin_setter;
    /* Hit grunts (the game picks one of three at random) and the death cry. */
    const char *voice_hit[3];
    const char *voice_die;
} UnitTemplate;

extern const AbilityDef SONNY_ABILITIES[];
extern const int SONNY_ABILITY_COUNT;
extern const int SONNY_ABILITY_MAX_ID;
extern const BuffDef SONNY_BUFFS[];
extern const int SONNY_BUFF_COUNT;
extern const UnitTemplate SONNY_UNITS[];
extern const int SONNY_UNIT_COUNT;
extern const ItemDef SONNY_ITEMS[];
extern const int SONNY_ITEM_COUNT;
extern const TalentDef SONNY_TALENTS[];
extern const int SONNY_TALENT_COUNT;
extern const int32_t SONNY_START_SKILL1;
extern const int32_t SONNY_START_SKILL2;
extern const LangArray SONNY_LANG[];
extern const int SONNY_LANG_COUNT;
extern const LangEntry SONNY_LANG_KEYED[];
extern const int SONNY_LANG_KEYED_COUNT;
/* Text out of a table keyed by name; "" when absent, never NULL. */
const char *lang_say(const char *table, const char *key);
/* Text by array name and index; "" when absent, never NULL. */
const char *lang_text(const char *array, int32_t index);

extern const BattleDef SONNY_BATTLES[];
extern const int SONNY_BATTLE_COUNT;
extern const ElementDef SONNY_ELEMENT_DEFS[];
extern const int SONNY_ELEMENT_DEF_COUNT;

extern const ZoneDef SONNY_ZONES[];
extern const int SONNY_ZONE_COUNT;

/* A store's stock. Every store offers a fixed list of item ids, which is what
   krinSetShop sets Krin.dropArray to; a zero is an empty slot, and the screen
   hides it. */
#define SONNY_SHOP_SLOTS 15
typedef struct {
    int32_t id;
    int32_t item[SONNY_SHOP_SLOTS];
} ShopDef;

/* What a marker on a zone's scene does. A marker is a button and nothing else
   says what it is for: one starts the zone's next story fight, one rolls a
   practice fight out of the zone's training list, one opens a store, and the
   rest are scenery, which say a line about themselves and are what the
   gameplay tally counts as found. */
typedef enum {
    MARKER_PROGRESS = 0,
    MARKER_TRAINING,
    MARKER_SHOP,
    MARKER_SCENERY
} MarkerKind;

typedef struct {
    int32_t    button;    /* the button's own character id */
    MarkerKind kind;
    int32_t    shop;      /* MARKER_SHOP: which store; -1 otherwise */
    int32_t    choices;   /* MARKER_TRAINING: how many the marker rolls
                             against, which is its own number and not the
                             training list's length */
    /* MARKER_SCENERY: the line out of NAVTITLE/NAVTEXT the marker says, and
       which of the eight bgElementsInteracted it flags. -1 for the rest. */
    int32_t    say;
    int32_t    element;
} ZoneButton;

/* One of the six the story can put in the fighting line. Their stats are kept
   the way the original keeps them: parallel arrays of a class, a level and
   the bonuses that stand in for equipment and spent points. */
#define SONNY_PARTY_SIZE 6

typedef struct {
    int32_t     index;
    const char *name;
    int32_t     class_id;      /* Krin.ClassStats */
    int32_t     level;
    int32_t     gender;        /* Krin.GSet: 0 male, 1 female */
    int32_t     skin, hair;
    int32_t     equip[7];      /* Krin.equipArrayN */
    double      stat[5];       /* Krin.StatSetsN */
    double      per[SONNY_ELEMENTS];
    double      def_[SONNY_ELEMENTS];
    int32_t     aggression[4]; /* Krin.agArrayN */
} PartyMember;

/* A point in the story that hands someone over: when progress reaches `at`
   -- exactly, or having passed it -- friendArray becomes `friends`, where -1
   means that place is still empty. */
typedef struct {
    int32_t at;
    int32_t exact;
    int32_t friends[SONNY_PARTY_SIZE];
} PartyJoin;

extern const PartyMember SONNY_PARTY[];
extern const int SONNY_PARTY_COUNT;
/* Krin.friendArray as the story starts, and Krin.friendArrayX -- which two of
   them stand in the line. */
extern const int32_t SONNY_PARTY_START[SONNY_PARTY_SIZE];
extern const int32_t SONNY_PARTY_TEAM[2];
extern const PartyJoin SONNY_PARTY_JOINS[];
extern const int SONNY_PARTY_JOIN_COUNT;

/* A cutscene is one long animation whose own frames carry its script: it
   starts a counter and, on the frames where the caption changes, shows the
   next line of CUTSUB or clears it. */
typedef struct {
    int32_t clip;         /* the animation's character id */
    int32_t start;        /* the line it starts counting from */
} CutsceneDef;

typedef struct {
    int32_t clip;
    int32_t frame;
    int32_t clear;        /* clears the caption rather than advancing it */
} CutsceneCue;

/* The points in the story the engine reacts to, from data/extracted/story.json.

   The note the hub puts up once progress reaches `at`: NAVTITLE2/NAVTEXT2
   entry `say` (Krin.progressSpeech). */
typedef struct {
    int32_t at;
    int32_t say;
} HubNote;

extern const HubNote SONNY_HUB_NOTES[];
extern const int SONNY_HUB_NOTE_COUNT;
/* The fights the boss theme plays for whatever marker started them. */
extern const int32_t SONNY_BOSS_MUSIC[];
extern const int SONNY_BOSS_MUSIC_COUNT;
/* The comic Proceed! plays after the zone's last fight, by progress; `comic`
   is its frame label on the root (CS_BRIDGE, CS_OUTRO). */
typedef struct {
    int32_t     at;
    const char *comic;
} BossComic;

extern const BossComic SONNY_BOSS_COMICS[];
extern const int SONNY_BOSS_COMIC_COUNT;
/* The fight the opening comic hands over to (Krin.BattlePick in IntroSeq). */
extern const int32_t SONNY_OPENING_BATTLE;

extern const CutsceneDef SONNY_CUTSCENES[];
extern const int SONNY_CUTSCENE_COUNT;
extern const CutsceneCue SONNY_CUTSCENE_CUES[];
extern const int SONNY_CUTSCENE_CUE_COUNT;
const CutsceneDef *cutscene_by_clip(int32_t clip);

extern const ShopDef SONNY_SHOPS[];
extern const int SONNY_SHOP_COUNT;
extern const ZoneButton SONNY_ZONE_BUTTONS[];
extern const int SONNY_ZONE_BUTTON_COUNT;
/* The store with this id, what a marker's button does, and the store it
   opens (NULL when it is not a store marker). */
const ShopDef *shop_by_id(int32_t id);
const ZoneButton *zone_button(int32_t button);
const ShopDef *shop_for_button(int32_t button);

/* Lookups by the original's own ids/keys. NULL when absent.
 *
 * Prefer ids: two template names repeat ("Templar" is both the player class
 * and the ally, "Galiant the Paladin" appears twice), and the original selects
 * a class by id -- Krin.ClassStats[0] = Krin.Class + 1, so classes are
 * templates 1..4. unit_template_by_name returns the first match. */
const AbilityDef *ability_by_id(int32_t id);
const UnitTemplate *unit_template_by_id(int32_t id);
const UnitTemplate *unit_template_by_name(const char *name);
const ItemDef *item_by_id(int32_t id);
const BattleDef *battle_def_by_id(int32_t id);
/* The zone whose battle range contains this id, or NULL. */
const ZoneDef *zone_of_battle(int32_t battle_id);

#endif
'''

LOOKUPS = '''
const CutsceneDef *cutscene_by_clip(int32_t clip)
{
    for (int i = 0; i < SONNY_CUTSCENE_COUNT; i++)
        if (SONNY_CUTSCENES[i].clip == clip)
            return &SONNY_CUTSCENES[i];
    return NULL;
}

const ShopDef *shop_by_id(int32_t id)
{
    for (int i = 0; i < SONNY_SHOP_COUNT; i++)
        if (SONNY_SHOPS[i].id == id)
            return &SONNY_SHOPS[i];
    return NULL;
}

const ZoneButton *zone_button(int32_t button)
{
    for (int i = 0; i < SONNY_ZONE_BUTTON_COUNT; i++)
        if (SONNY_ZONE_BUTTONS[i].button == button)
            return &SONNY_ZONE_BUTTONS[i];
    return NULL;
}

const ShopDef *shop_for_button(int32_t button)
{
    const ZoneButton *b = zone_button(button);
    return (b && b->kind == MARKER_SHOP) ? shop_by_id(b->shop) : NULL;
}

const AbilityDef *ability_by_id(int32_t id)
{
    int lo = 0, hi = SONNY_ABILITY_COUNT - 1;
    while (lo <= hi) {
        int mid = lo + (hi - lo) / 2;
        if (SONNY_ABILITIES[mid].id == id)
            return &SONNY_ABILITIES[mid];
        if (SONNY_ABILITIES[mid].id < id)
            lo = mid + 1;
        else
            hi = mid - 1;
    }
    return NULL;
}

const UnitTemplate *unit_template_by_id(int32_t id)
{
    int lo = 0, hi = SONNY_UNIT_COUNT - 1;
    while (lo <= hi) {
        int mid = lo + (hi - lo) / 2;
        if (SONNY_UNITS[mid].id == id)
            return &SONNY_UNITS[mid];
        if (SONNY_UNITS[mid].id < id)
            lo = mid + 1;
        else
            hi = mid - 1;
    }
    return NULL;
}

const UnitTemplate *unit_template_by_name(const char *name)
{
    if (!name)
        return NULL;
    for (int i = 0; i < SONNY_UNIT_COUNT; i++)
        if (strcmp(SONNY_UNITS[i].name, name) == 0)
            return &SONNY_UNITS[i];
    return NULL;
}

const ItemDef *item_by_id(int32_t id)
{
    int lo = 0, hi = SONNY_ITEM_COUNT - 1;
    while (lo <= hi) {
        int mid = lo + (hi - lo) / 2;
        if (SONNY_ITEMS[mid].id == id)
            return &SONNY_ITEMS[mid];
        if (SONNY_ITEMS[mid].id < id)
            lo = mid + 1;
        else
            hi = mid - 1;
    }
    return NULL;
}

const BattleDef *battle_def_by_id(int32_t id)
{
    int lo = 0, hi = SONNY_BATTLE_COUNT - 1;
    while (lo <= hi) {
        int mid = lo + (hi - lo) / 2;
        if (SONNY_BATTLES[mid].id == id)
            return &SONNY_BATTLES[mid];
        if (SONNY_BATTLES[mid].id < id)
            lo = mid + 1;
        else
            hi = mid - 1;
    }
    return NULL;
}

const char *lang_text(const char *array, int32_t index)
{
    if (!array)
        return "";
    for (int i = 0; i < SONNY_LANG_COUNT; i++) {
        if (strcmp(SONNY_LANG[i].name, array) != 0)
            continue;
        if (index < 0 || index >= SONNY_LANG[i].count)
            return "";
        return SONNY_LANG[i].values[index] ? SONNY_LANG[i].values[index] : "";
    }
    return "";
}

const char *lang_say(const char *table, const char *key)
{
    if (!table || !key)
        return "";
    for (int i = 0; i < SONNY_LANG_KEYED_COUNT; i++)
        if (strcmp(SONNY_LANG_KEYED[i].table, table) == 0
            && strcmp(SONNY_LANG_KEYED[i].key, key) == 0)
            return SONNY_LANG_KEYED[i].value ? SONNY_LANG_KEYED[i].value : "";
    return "";
}

const ZoneDef *zone_of_battle(int32_t battle_id)
{
    for (int i = 0; i < SONNY_ZONE_COUNT; i++)
        if (battle_id >= SONNY_ZONES[i].first_battle
            && battle_id <= SONNY_ZONES[i].last_battle)
            return &SONNY_ZONES[i];
    return NULL;
}
'''


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('-o', '--out', default='src/gen')
    args = ap.parse_args()

    # The extracted tables with data/content/ laid over them; see content.py.
    def load(name):
        table = content.load(name)
        if isinstance(table, list) and table and isinstance(table[0], dict) \
                and 'id' in table[0]:
            ids = [r['id'] for r in table]
            if len(ids) != len(set(ids)):
                raise SystemExit('%s: an id is used twice' % name)
            # The engine finds a record by binary search on its id.
            table = sorted(table, key=lambda r: r['id'])
        return table

    abilities, buffs, units = load('abilities'), load('buffs'), load('units')

    def load_items():
        return load('items')

    os.makedirs(args.out, exist_ok=True)

    with open(os.path.join(args.out, 'gamedata.h'), 'w', encoding='utf-8') as fh:
        fh.write(HEADER)
    with open(os.path.join(args.out, 'gamedata.c'), 'w', encoding='utf-8') as fh:
        fh.write('/* GENERATED by tools/gen_c_data.py -- do not edit. */\n')
        fh.write('#include <string.h>\n#include "gamedata.h"\n\n')
        fh.write(gen_abilities(abilities) + '\n\n')
        fh.write(gen_buffs(buffs) + '\n\n')
        fh.write(gen_units(units) + '\n\n')
        fh.write(gen_items(load_items()) + '\n\n')
        fh.write(gen_talents(load('talents')) + '\n\n')
        fh.write(gen_battles(load('battles')) + '\n\n')
        fh.write(gen_zones(load('zones')) + '\n\n')
        fh.write(gen_shops(load('shops')) + '\n\n')
        fh.write(gen_zone_buttons(load('zone_buttons')) + '\n\n')
        fh.write(gen_party(load('party')) + '\n\n')
        fh.write(gen_cutscenes(load('cutscenes')) + '\n\n')
        fh.write(gen_elements(load('elements')) + '\n\n')
        fh.write(gen_story(load('story')) + '\n\n')
        fh.write(gen_lang(load('lang')) + '\n')
        fh.write(LOOKUPS)

    print('abilities %d, buffs %d, units %d, items %d, talents %d, '
          'battles %d, zones %d -> %s'
          % (len(abilities), len(buffs), len(units), len(load_items()),
             len(load('talents')['nodes']), len(load('battles')),
             len(load('zones')), args.out))


if __name__ == '__main__':
    main()
