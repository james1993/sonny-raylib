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
    m = LANG_VAR_RE.match(tok)
    if m:
        # krinABC1/2/3 alias SKILLTIP / SKILLTIP2 / SKILLTIP3
        array = {'1': 'SKILLTIP', '2': 'SKILLTIP2', '3': 'SKILLTIP3'}[m.group(1)]
        return {"$lang": [array, int(m.group(2))]}
    if '+' in tok:  # concatenation of literals and lang lookups
        return {"$concat": [parse_value(p) for p in tok.split('+')]}
    return {"$expr": tok}


# ------------------------------------------------------------------ lang tables

LANG_ASSIGN = re.compile(
    r'^KrinLang\.([A-Z]+)\.([A-Z0-9_]+)\[(\d+)\]\s*=\s*(.+);$')


def extract_lang(text):
    langs = {}
    for line in text.splitlines():
        m = LANG_ASSIGN.match(line.strip())
        if not m:
            continue
        lang, array, idx, value = m.group(1), m.group(2), int(m.group(3)), m.group(4)
        langs.setdefault(lang, {}).setdefault(array, {})[idx] = parse_value(value)
    # dict-of-index -> dense list
    for lang, arrays in langs.items():
        for name, entries in arrays.items():
            arrays[name] = [entries.get(i) for i in range(max(entries) + 1)]
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
        return value
    return value


# ------------------------------------------------------- registrar replay

# addNewMove(a, c, d, e, f, g, h, i, j, k, l, m, n, o, p, q, r, s) -- 18 args
# that land in the A-table at indices 0, 2..16, then the name id and sound.
# Names below are the ones confirmed by how the engine reads each slot
# (frame_62 executeMove/AImoveAdder and the frame_212 combat driver);
# unconfirmed slots keep their raw index so nothing is mislabelled.
MOVE_PARAMS = ['icon', 'a2', 'a3', 'a4', 'focus_cost', 'health_cost',
               'cooldown', 'a8', 'anim_speed', 'delivery', 'color', 'anim',
               'model', 'damage_kind', 'a15', 'health_cost_pct', 'name_id',
               'sound']

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
PATCH_RE = re.compile(r'^_root\.(hackMove|hackMove2)\[(\d+)\]\s*=\s*(.+);$')
UNIT_PATCH_RE = re.compile(r'^jesivie\.([A-Za-z0-9_]+)\s*=\s*(.+);$')
MOVECOUNT_RE = re.compile(r'^MoveCount\s*=\s*(\d+);$')


def extract_tables(text):
    moves, units, items, buffs = {}, {}, [], {}
    move_count = 0
    unit_count = 0
    cur = None          # ('move'|'buff', key) for hackMove/hackMove2 patches
    cur_unit = None

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
                items.append({'id': len(items), 'args': args})
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

    return moves, units, items, buffs


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('scripts_dir', help='ffdec script export dir (…/scripts)')
    ap.add_argument('-o', '--out', default='data/extracted')
    ap.add_argument('--lang', default='ENGLISH')
    args = ap.parse_args()

    data_as = os.path.join(args.scripts_dir, 'frame_62', 'DoAction.as')
    lang_as = os.path.join(args.scripts_dir, 'frame_61', 'DoAction.as')
    for p in (data_as, lang_as):
        if not os.path.exists(p):
            sys.exit('missing %s -- is this an ffdec script export of SONNY1.swf?' % p)

    langs = extract_lang(open(lang_as, encoding='utf-8', errors='replace').read())
    lang = langs.get(args.lang, {})
    text = open(data_as, encoding='utf-8', errors='replace').read()
    moves, units, items, buffs = extract_tables(text)

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

    item_list = [{'id': it['id'], 'args': [resolve(a, lang) for a in it['args']]}
                 for it in items]

    os.makedirs(args.out, exist_ok=True)
    written = []
    for name, payload in (('lang', langs), ('abilities', abilities),
                          ('units', unit_list), ('items', item_list),
                          ('buffs', buff_list)):
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
    for name, path in written:
        print('  wrote %-10s %s' % (name, path))


if __name__ == '__main__':
    main()
