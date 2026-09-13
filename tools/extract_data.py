#!/usr/bin/env python3
"""Extract Sonny's game data tables out of decompiled ActionScript into JSON.

The original defines all of its content by calling four registrar functions in
frame 62 (`addNewMove`, `createNewUnitKrin`, `createNewItemKrin`,
`addNewBuffKrin`), each followed by lines that patch individual array slots
(`_root.hackMove[2] = 1.6`). This reproduces that interpretation exactly:
replay the calls in order, apply the patches, and write out the resulting
tables. No value is inferred or rounded -- what the game ships is what lands
in the JSON.

Usage:
    ffdec-cli -format script:as -export script /tmp/sonny1 SONNY1.swf
    python3 tools/extract_data.py /tmp/sonny1/scripts -o data/extracted

Output stays local and gitignored; it is game content, not source.
"""
import argparse
import json
import os
import re
import sys

# ---------------------------------------------------------------- value parsing

NUM_RE = re.compile(r'^-?(?:\d+\.?\d*(?:[eE][-+]?\d+)?|\.\d+)$')
LANG_RE = re.compile(r'^_root\.KrinLang\[KLangChoosen\]\.([A-Z0-9_]+)\[(\d+)\]$')
LANG_VAR_RE = re.compile(r'^krinABC(\d)\[(\d+)\]$')
# Locals that alias a language array: sayBayK = KrinLang[...].BATTLESPEECH
LANG_ALIAS = {'krinABC1': 'SKILLTIP', 'krinABC2': 'SKILLTIP2',
              'krinABC3': 'SKILLTIP3', 'sayBayK': 'BATTLESPEECH'}
LANG_ALIAS_RE = re.compile(r'^([A-Za-z_][A-Za-z0-9_]*)\[(\d+)\]$')


def split_args(s):
    """Split a call's argument list on top-level commas."""
    out, depth, cur, i, quote = [], 0, [], 0, None
    while i < len(s):
        ch = s[i]
        if quote:
            cur.append(ch)
            if ch == '\\' and i + 1 < len(s):
                cur.append(s[i + 1])
                i += 2
                continue
            if ch == quote:
                quote = None
        elif ch in '"\'':
            quote = ch
            cur.append(ch)
        elif ch in '([{':
            depth += 1
            cur.append(ch)
        elif ch in ')]}':
            depth -= 1
            cur.append(ch)
        elif ch == ',' and depth == 0:
            out.append(''.join(cur).strip())
            cur = []
        else:
            cur.append(ch)
        i += 1
    if ''.join(cur).strip():
        out.append(''.join(cur).strip())
    return out


def unescape(s):
    return (s.replace("\\'", "'").replace('\\"', '"').replace('\\n', '\n')
             .replace('\\t', '\t').replace('\\\\', '\\'))


def parse_value(tok):
    """AS literal -> Python. Lang lookups become {"$lang": [array, index]}."""
    tok = tok.strip()
    if not tok or tok in ('undefined', 'null'):
        return None
    if tok == 'true':
        return True
    if tok == 'false':
        return False
    if NUM_RE.match(tok):
        return float(tok) if ('.' in tok or 'e' in tok or 'E' in tok) else int(tok)
    if len(tok) >= 2 and tok[0] == tok[-1] and tok[0] in '"\'':
        return unescape(tok[1:-1])
    if tok.startswith('[') and tok.endswith(']'):
        inner = tok[1:-1].strip()
        return [parse_value(v) for v in split_args(inner)] if inner else []
    if tok in ('new Array()', 'new Object()'):
        return []
    m = LANG_RE.match(tok)
    if m:
        return {"$lang": [m.group(1), int(m.group(2))]}
    m = LANG_ALIAS_RE.match(tok)
    if m and m.group(1) in LANG_ALIAS:
        return {"$lang": [LANG_ALIAS[m.group(1)], int(m.group(2))]}
    if '+' in tok:  # concatenation of literals and lang lookups
        return {"$concat": [parse_value(p) for p in tok.split('+')]}
    return {"$expr": tok}


# ------------------------------------------------------------------ lang tables

LANG_ASSIGN = re.compile(
    r'^KrinLang\.([A-Z]+)\.([A-Z0-9_]+)\[(\d+)\]\s*=\s*(.+);$')
# Not every piece of text is in an array: the menu headers and a handful of
# other labels are plain properties.
LANG_SCALAR = re.compile(
    r'^KrinLang\.([A-Z]+)\.([A-Z0-9_]+)\s*=\s*(\"(?:[^\"\\\\]|\\\\.)*\");$')


def extract_lang(text):
    langs = {}
    scalars = {}
    for line in text.splitlines():
        line = line.strip()
        m = LANG_ASSIGN.match(line)
        if m:
            lang, array, idx, value = (m.group(1), m.group(2),
                                       int(m.group(3)), m.group(4))
            langs.setdefault(lang, {}).setdefault(array,
                                                  {})[idx] = parse_value(value)
            continue
        m = LANG_SCALAR.match(line)
        if m:
            scalars.setdefault(m.group(1), {})[m.group(2)] = parse_value(
                m.group(3))
    # dict-of-index -> dense list
    for lang, arrays in langs.items():
        for name, entries in arrays.items():
            arrays[name] = [entries.get(i) for i in range(max(entries) + 1)]
    # A plain label is kept as a one-entry array, so every lookup reads the
    # same way whether the original wrote it as a property or an array.
    for lang, entries in scalars.items():
        for name, value in entries.items():
            langs.setdefault(lang, {}).setdefault(name, [value])
    return langs


def resolve(value, lang):
    """Replace {"$lang": ...} / {"$concat": ...} with real text."""
    if isinstance(value, list):
        return [resolve(v, lang) for v in value]
    if isinstance(value, dict):
        if "$lang" in value:
            array, idx = value["$lang"]
            table = lang.get(array) or []
            return table[idx] if idx < len(table) else None
        if "$concat" in value:
            parts = [resolve(p, lang) for p in value["$concat"]]
            return ''.join('' if p is None else str(p) for p in parts)
        if "$expr" in value:
            return value
        # A plain object literal (a speech, an item drop): resolve its values.
        return {k: resolve(v, lang) for k, v in value.items()}
    return value


# ------------------------------------------------------- registrar replay

# addNewMove(a, c, d, e, f, g, h, i, j, k, l, m, n, o, p, q, r, s) -- 18 args
# that land in the A-table at indices 0, 2..16, then the name id and sound.
# Names below are the ones confirmed by how the engine reads each slot
# (frame_62 executeMove/AImoveAdder and the frame_212 combat driver);
# unconfirmed slots keep their raw index so nothing is mislabelled.
MOVE_PARAMS = ['icon', 'target_self', 'target_enemy', 'target_ally',
               'focus_cost', 'health_cost', 'cooldown', 'a8', 'anim_speed',
               'delivery', 'color', 'anim', 'model', 'damage_kind', 'a15',
               'health_cost_pct', 'name_id', 'sound']

# Verified against executeMove()/perScript() in frame_62.
MOVEB_FIELDS = {
    0: 'element', 1: 'strength_add', 2: 'strength_coef', 3: 'magic_add',
    4: 'magic_coef', 5: 'speed_add', 6: 'speed_coef', 7: 'hit_add',
    8: 'hit_coef', 9: 'flat_damage', 10: 'damage_coef', 11: 'focus_coef',
    13: 'buff', 15: 'element_list', 17: 'tooltip', 18: 'cost_text',
}

# createNewUnitKrin(a..j): name then (base, per-level) pairs per stat.
UNIT_PARAMS = ['name', 'vitality', 'vitality_growth', 'strength',
               'strength_growth', 'magic', 'magic_growth', 'speed',
               'speed_growth', 'focus']

CALL_RE = re.compile(r'^(addNewMove|createNewUnitKrin|createNewItemKrin|'
                     r'addNewBuffKrin)\((.*)\);$')
# Krin.HealthUP = 5;  Krin.PIU[0] = 12;  Krin.DIU[3] = 12;
ITEM_STAT_RE = re.compile(r'^Krin\.(HealthUP|StrengthUP|MagicUP|SpeedUP|FocusUP)'
                          r'\s*=\s*(.+);$')
ITEM_ELEM_RE = re.compile(r'^Krin\.(PIU|DIU)\[(\d+)\]\s*=\s*(.+);$')
ITEM_LOOKS_RE = re.compile(r'^gghhjjuu\.(looks|req)\s*=\s*(.+);$')
CLEAR_RE = re.compile(r'^allClearKrinItem\(\);$')

# createNewItemKrin(a, b, c, d, e, f)
ITEM_PARAMS = ['icon', 'slot', 'rarity', 'class_req', 'level_req', 'price']
# statUpdater is [Health, Strength, Magic, Speed, Focus] in that order.
ITEM_STAT_ORDER = ['HealthUP', 'StrengthUP', 'MagicUP', 'SpeedUP', 'FocusUP']
PATCH_RE = re.compile(r'^_root\.(hackMove|hackMove2)\[(\d+)\]\s*=\s*(.+);$')
UNIT_PATCH_RE = re.compile(r'^jesivie\.([A-Za-z0-9_]+)\s*=\s*(.+);$')
MOVECOUNT_RE = re.compile(r'^MoveCount\s*=\s*(\d+);$')
# Krin.abilityXer[0] = {ID:1,LEVELMIN:2,LEVELSCALE:1,TIER:5,PRESKILL:[-1],
#                       CLASSIFY:0,BUFFNAME:0};
TALENT_RE = re.compile(r'^Krin\.abilityXer\[(\d+)\]\s*=\s*\{(.*)\};$')
START_SKILL_RE = re.compile(r'^Krin\.startSkill([12])\s*=\s*(\d+);$')
# The eight elements, and the colour the interface gives each of them.
ELEMENT_RE = re.compile(r'^element(Main|Color)Array\s*=\s*\[(.*)\];$')


def extract_elements(text):
    """[{name, colour}] in the order everything else indexes them by."""
    found = {}
    for line in text.splitlines():
        m = ELEMENT_RE.match(line.strip())
        if m:
            found[m.group(1)] = [parse_value(tok.strip())
                                 for tok in split_args(m.group(2))]
    names = found.get('Main') or []
    colours = found.get('Color') or []
    out = []
    for index, name in enumerate(names):
        colour = colours[index] if index < len(colours) else '0xFFFFFF'
        out.append({'name': name,
                    'colour': int(str(colour), 16)
                    if str(colour).lower().startswith('0x')
                    else int(colour)})
    return out


def parse_object(body):
    """Parse an AS object literal's `key:value` pairs."""
    out = {}
    for part in split_args(body):
        if ':' not in part:
            continue
        key, _, value = part.partition(':')
        out[key.strip()] = parse_value(value.strip())
    return out


# createNewBattle(); then a run of `rengi.<field> = ...` / `.push(...)` lines.
BATTLE_NEW_RE = re.compile(r'^createNewBattle\(\);$')
BATTLE_SET_RE = re.compile(r'^rengi\.([A-Za-z0-9_]+)\s*=\s*(.+);$')
BATTLE_PUSH_RE = re.compile(r'^rengi\.([A-Za-z0-9_]+)\.push\((.+)\);$')
BATTLE_ID_RE = re.compile(r'^battleCreationID\s*=\s*(\d+);$')
ZONE_RANGE_RE = re.compile(r'^Krin\.progressArray\[(\d+)\]\s*=\s*(.+);$')
ZONE_TRAIN_RE = re.compile(r'^Krin\.trainingArray\[(\d+)\]\s*=\s*(.+);$')


def extract_battles(text):
    """The battle rosters (KBR objects). players[0..4] fill slots 2..6:
    positive values are enemy templates, negatives reference allies, 0 is an
    empty slot. A level of "X" means "match the player's level"."""
    battles, order = {}, []
    battle_id = 0
    cur = None

    for raw in text.splitlines():
        line = raw.strip()

        m = BATTLE_ID_RE.match(line)
        if m:
            battle_id = int(m.group(1))
            continue

        if BATTLE_NEW_RE.match(line):
            battle_id += 1
            battles[battle_id] = {
                'id': battle_id,
                'players': [0, 1, 0, 2, 0],
                'playersLevels': [1, 1, 1, 1, 1],
                'speeches': [], 'itemDrops': [], 'itemRare': [],
                'itemRareDropper': 0, 'winDate': -1, 'timeLock': False,
                'winDateCondition': 0, 'absoluteStart': 0,
            }
            order.append(battle_id)
            cur = battle_id
            continue

        if cur is None:
            continue

        m = BATTLE_PUSH_RE.match(line)
        if m:
            field, value = m.group(1), m.group(2).strip()
            if value.startswith('{') and value.endswith('}'):
                entry = parse_object(value[1:-1])
            else:
                entry = parse_value(value)
            battles[cur].setdefault(field, []).append(entry)
            continue

        m = BATTLE_SET_RE.match(line)
        if m:
            field, value = m.group(1), m.group(2).strip()
            if value in ('new Array()', 'new Object()'):
                battles[cur][field] = []
                continue
            if field == 'speeches' and value.startswith('sortOn'):
                continue
            battles[cur][field] = parse_value(value)
            continue

    return [battles[i] for i in order]


def extract_zones(text):
    ranges, training = {}, {}
    for raw in text.splitlines():
        line = raw.strip()
        m = ZONE_RANGE_RE.match(line)
        if m:
            ranges[int(m.group(1))] = parse_value(m.group(2))
            continue
        m = ZONE_TRAIN_RE.match(line)
        if m:
            training[int(m.group(1))] = parse_value(m.group(2))
    return [{'zone': z, 'first_battle': ranges[z][0],
             'last_battle': ranges[z][1],
             'training': training.get(z, [])}
            for z in sorted(ranges)]


DOLL_RE = re.compile(r'^dollParts(Array|Cores|Cores2)\s*=\s*(\[.*\]);$')


def extract_doll(text):
    """How a character is assembled from art.

    The battle screen builds each character by attaching, for every doll part,
    the export named `<gender>_<partCore2>_<look>` -- so "M_WEAPON_CROWBAR".
    dollPartsArray names the 15 parts, dollPartsCores maps each to an
    equipment slot, and dollPartsCores2 gives the name used in the export.
    """
    out = {}
    for raw in text.splitlines():
        m = DOLL_RE.match(raw.strip())
        if m:
            out['dollParts' + m.group(1)] = parse_value(m.group(2))
    return out


def extract_talents(text):
    """The talent tree: loadTalents() in frame 61 builds one 28-node tree,
    ignoring its class parameter in this build."""
    nodes, start = {}, {}
    for raw in text.splitlines():
        line = raw.strip()
        m = TALENT_RE.match(line)
        if m:
            node = parse_object(m.group(2))
            node['index'] = int(m.group(1))
            nodes[node['index']] = node
            continue
        m = START_SKILL_RE.match(line)
        if m:
            start['startSkill%s' % m.group(1)] = int(m.group(2))
    return [nodes[i] for i in sorted(nodes)], start


def new_item_stats():
    return {'stats': {k: 0 for k in ITEM_STAT_ORDER},
            'PIU': [0] * 8, 'DIU': [0] * 8}


def extract_tables(text):
    moves, units, items, buffs = {}, {}, [], {}
    move_count = 0
    unit_count = 0
    item_count = 0
    cur = None          # ('move'|'buff', key) for hackMove/hackMove2 patches
    cur_unit = None
    cur_item = None
    item_stats = new_item_stats()

    for raw in text.splitlines():
        line = raw.strip()

        m = MOVECOUNT_RE.match(line)
        if m:
            move_count = int(m.group(1))
            continue

        m = CALL_RE.match(line)
        if m:
            fn, args = m.group(1), [parse_value(a) for a in split_args(m.group(2))]
            if fn == 'addNewMove':
                entry = {'id': move_count, 'args': dict(
                    zip(MOVE_PARAMS, args + [None] * (len(MOVE_PARAMS) - len(args))))}
                # Defaults applied by addNewMove to the B-table.
                entry['b'] = {0: 'Physical', 1: 0, 2: 0, 3: 0, 4: 0, 5: 0, 6: 0,
                              7: 0, 8: 1, 9: 0, 10: 1, 11: 0, 12: 0, 13: 0,
                              14: 0, 15: [0], 16: 0,
                              17: 'No Tooltip assigned', 18: 'Costs ', 19: 1,
                              20: 0}
                moves[move_count] = entry
                cur = ('move', move_count)
                move_count += 1
            elif fn == 'createNewUnitKrin':
                unit_count += 1
                units[unit_count] = {'id': unit_count, 'args': dict(
                    zip(UNIT_PARAMS, args + [None] * (len(UNIT_PARAMS) - len(args))))}
                cur_unit = unit_count
            elif fn == 'createNewItemKrin':
                # itemKrinIDnow starts at -1 and is pre-incremented, so the
                # first item is id 0 -- which is also how the language tables
                # are indexed.
                items.append({
                    'id': item_count,
                    'args': dict(zip(ITEM_PARAMS,
                                     args + [None] * (len(ITEM_PARAMS) - len(args)))),
                    # Snapshot of the stat block, exactly as the registrar
                    # copies Krin.*UP / Krin.PIU / Krin.DIU into the item.
                    'statUpdater': [item_stats['stats'][k] for k in ITEM_STAT_ORDER],
                    'statUpdaterP': list(item_stats['PIU']),
                    'statUpdaterD': list(item_stats['DIU']),
                })
                item_count += 1
                cur_item = len(items) - 1
            elif fn == 'addNewBuffKrin':
                key = args[0]
                buffs[key] = {'key': key, 'name': args[1],
                              'element': args[2] if len(args) > 2 else None,
                              'fields': {i: 0 for i in range(2, 32)}}
                cur = ('buff', key)
            continue

        m = PATCH_RE.match(line)
        if m:
            which, idx, value = m.group(1), int(m.group(2)), parse_value(m.group(3))
            if which == 'hackMove' and cur and cur[0] == 'move':
                moves[cur[1]]['b'][idx] = value
            elif which == 'hackMove2' and cur and cur[0] == 'buff':
                buffs[cur[1]]['fields'][idx] = value
            continue

        m = UNIT_PATCH_RE.match(line)
        if m and cur_unit is not None:
            units[cur_unit][m.group(1)] = parse_value(m.group(2))
            continue

        m = ITEM_STAT_RE.match(line)
        if m:
            item_stats['stats'][m.group(1)] = parse_value(m.group(2))
            continue

        m = ITEM_ELEM_RE.match(line)
        if m:
            which, idx = m.group(1), int(m.group(2))
            if idx < 8:
                item_stats[which][idx] = parse_value(m.group(3))
            continue

        m = ITEM_LOOKS_RE.match(line)
        if m and cur_item is not None:
            items[cur_item][m.group(1)] = parse_value(m.group(2))
            continue

        if CLEAR_RE.match(line):
            item_stats = new_item_stats()
            continue

    return moves, units, items, buffs


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('scripts_dir', help='ffdec script export dir (…/scripts)')
    ap.add_argument('-o', '--out', default='data/extracted')
    ap.add_argument('--lang', default='ENGLISH')
    args = ap.parse_args()

    data_as = os.path.join(args.scripts_dir, 'frame_62', 'DoAction.as')
    lang_as = os.path.join(args.scripts_dir, 'frame_61', 'DoAction.as')
    doll_as = os.path.join(args.scripts_dir, 'DefineSprite_1503', 'frame_1',
                           'DoAction.as')
    for p in (data_as, lang_as):
        if not os.path.exists(p):
            sys.exit('missing %s -- is this an ffdec script export of SONNY1.swf?' % p)

    lang_text = open(lang_as, encoding='utf-8', errors='replace').read()
    langs = extract_lang(lang_text)
    lang = langs.get(args.lang, {})
    talents, start_skills = extract_talents(lang_text)
    zones = extract_zones(lang_text)
    elements = extract_elements(lang_text)
    doll = {}
    if os.path.exists(doll_as):
        doll = extract_doll(open(doll_as, encoding='utf-8',
                                 errors='replace').read())
    text = open(data_as, encoding='utf-8', errors='replace').read()
    moves, units, items, buffs = extract_tables(text)
    battles = extract_battles(text)

    # Resolve text references and give the verified B-table fields real names.
    abilities = []
    for mid in sorted(moves):
        m = moves[mid]
        b = m['b']
        out = {'id': mid}
        out.update({k: resolve(v, lang) for k, v in m['args'].items()})
        named = {}
        for idx, value in sorted(b.items()):
            key = MOVEB_FIELDS.get(idx, 'b%d' % idx)
            named[key] = resolve(value, lang)
        out['combat'] = named
        name_id = out.get('name_id')
        skillname = lang.get('SKILLNAME') or []
        out['name'] = (skillname[name_id] if isinstance(name_id, int)
                       and name_id < len(skillname) else None)
        abilities.append(out)

    unit_list = []
    for uid in sorted(units):
        u = units[uid]
        out = {k: resolve(v, lang) for k, v in u['args'].items()}
        out['id'] = uid
        for k, v in u.items():
            if k not in ('args', 'id'):
                out[k] = resolve(v, lang)
        unit_list.append(out)

    buff_list = []
    for key in buffs:
        b = buffs[key]
        buff_list.append({
            'key': key,
            'name': resolve(b['name'], lang),
            'element': b['element'],
            'fields': {str(i): resolve(v, lang) for i, v in sorted(b['fields'].items())},
        })

    itemname = lang.get('ITEMNAME') or []
    itemsay = lang.get('ITEMSAY') or []
    item_list = []
    for it in items:
        out = {'id': it['id']}
        out.update({k: resolve(v, lang) for k, v in it['args'].items()})
        # The registrar takes the display name and tooltip from the language
        # tables by item id, not from its arguments.
        out['name'] = itemname[it['id']] if it['id'] < len(itemname) else None
        out['tooltip'] = itemsay[it['id']] if it['id'] < len(itemsay) else None
        out['statUpdater'] = it['statUpdater']
        out['statUpdaterP'] = it['statUpdaterP']
        out['statUpdaterD'] = it['statUpdaterD']
        for extra in ('looks', 'req'):
            if extra in it:
                out[extra] = resolve(it[extra], lang)
        item_list.append(out)

    os.makedirs(args.out, exist_ok=True)
    written = []
    battle_list = []
    for b in battles:
        out = {k: resolve(v, lang) for k, v in b.items()}
        battle_list.append(out)

    zone_list = []
    zone_names = lang.get('ZONES') or []
    zone_subtitles = lang.get('ZONES2') or []
    for z in zones:
        out = dict(z)
        out['name'] = (zone_names[z['zone']]
                       if z['zone'] < len(zone_names) else None)
        out['subtitle'] = (zone_subtitles[z['zone']]
                           if z['zone'] < len(zone_subtitles) else None)
        zone_list.append(out)

    talent_payload = {'startSkill1': start_skills.get('startSkill1'),
                      'startSkill2': start_skills.get('startSkill2'),
                      'nodes': talents}

    for name, payload in (('lang', langs), ('abilities', abilities),
                          ('units', unit_list), ('items', item_list),
                          ('buffs', buff_list), ('talents', talent_payload),
                          ('battles', battle_list), ('zones', zone_list),
                          ('elements', elements), ('doll', doll)):
        path = os.path.join(args.out, name + '.json')
        with open(path, 'w', encoding='utf-8') as fh:
            json.dump(payload, fh, indent=1, ensure_ascii=False)
        written.append((name, path))

    print('languages : %s' % ', '.join(sorted(langs)))
    print('abilities : %d (ids %d..%d)' % (len(abilities), abilities[0]['id'],
                                           abilities[-1]['id']))
    print('units     : %d' % len(unit_list))
    print('items     : %d' % len(item_list))
    print('buffs     : %d' % len(buff_list))
    print('talents   : %d nodes, start skills %s/%s'
          % (len(talents), start_skills.get('startSkill1'),
             start_skills.get('startSkill2')))
    print('battles   : %d (ids %s..%s)'
          % (len(battle_list), battle_list[0]['id'] if battle_list else '-',
             battle_list[-1]['id'] if battle_list else '-'))
    print('zones     : %d' % len(zone_list))
    print('doll      : %d part lists' % len(doll))
    for name, path in written:
        print('  wrote %-10s %s' % (name, path))


if __name__ == '__main__':
    main()
