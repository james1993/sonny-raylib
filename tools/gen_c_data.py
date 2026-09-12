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

ELEMENTS = ["Physical", "Magic", "Ice", "Fire", "Lightning", "Earth",
            "Shadow", "Poison"]


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
        lines.append('        .buff = %s,' % c_string(buff_key))
        lines.append('        .tooltip = %s,' % c_string(c.get('tooltip') or ''))
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
        lines.append('    },')
    lines.append('};')
    lines.append('const int SONNY_ABILITY_COUNT = '
                 '(int)(sizeof(SONNY_ABILITIES) / sizeof(SONNY_ABILITIES[0]));')
    lines.append('const int SONNY_ABILITY_MAX_ID = %d;' % max(ids))
    return '\n'.join(lines)


def gen_buffs(buffs):
    lines = ['const BuffDef SONNY_BUFFS[] = {']
    for b in buffs:
        f = b['fields']

        def g(i):
            return num(f.get(str(i)))

        lines.append('    { /* %s */' % b['key'])
        lines.append('        .key = %s, .name = %s, .element = %d,'
                     % (c_string(b['key']), c_string(b.get('name') or ''),
                        element_index(b.get('element'))))
        lines.append('        .change = { %s },'
                     % ', '.join(fmt(g(i)) for i in range(2, 14)))
        lines.append('        .dot_flat = %s, .focus_drain = %s, .duration = %d,'
                     % (fmt(g(14)), fmt(g(15)), int(num(g(16)))))
        lines.append('        .stun = %d, .reflect = %d, .shield = %d,'
                     % (int(num(g(17))), int(num(g(18))), int(num(g(19)))))
        lines.append('        .per_flat = %s, .def_flat = %s, .per_pct = %s, '
                     '.def_pct = %s,'
                     % (fmt(g(21)), fmt(g(22)), fmt(g(23)), fmt(g(24))))
        lines.append('        .sswitch = %d, .dot_strength = %s, .dot_magic = %s,'
                     % (int(num(g(26))), fmt(g(28)), fmt(g(29))))
        lines.append('        .dot_speed = %s, .filter = %d,'
                     % (fmt(g(30)), int(num(g(31)))))
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
        lines.append('    },')
    lines.append('};')
    lines.append('const int SONNY_UNIT_COUNT = '
                 '(int)(sizeof(SONNY_UNITS) / sizeof(SONNY_UNITS[0]));')
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
    const char  *tooltip;
    AbilityCoefs coefs;
} AbilityDef;

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

#endif
'''

LOOKUPS = '''
const AbilityDef *ability_by_id(int32_t id)
{
    for (int i = 0; i < SONNY_ABILITY_COUNT; i++)
        if (SONNY_ABILITIES[i].id == id)
            return &SONNY_ABILITIES[i];
    return NULL;
}

const UnitTemplate *unit_template_by_id(int32_t id)
{
    for (int i = 0; i < SONNY_UNIT_COUNT; i++)
        if (SONNY_UNITS[i].id == id)
            return &SONNY_UNITS[i];
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
    for (int i = 0; i < SONNY_ITEM_COUNT; i++)
        if (SONNY_ITEMS[i].id == id)
            return &SONNY_ITEMS[i];
    return NULL;
}
'''


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('-i', '--input', default='data/extracted')
    ap.add_argument('-o', '--out', default='src/gen')
    args = ap.parse_args()

    def load(name):
        with open(os.path.join(args.input, name + '.json'), encoding='utf-8') as fh:
            return json.load(fh)

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
        fh.write(gen_items(load_items()) + '\n')
        fh.write(LOOKUPS)

    print('abilities %d, buffs %d, units %d, items %d -> %s'
          % (len(abilities), len(buffs), len(units), len(load_items()),
             args.out))


if __name__ == '__main__':
    main()
