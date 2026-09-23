"""The game's tables as the engine is built from them: what was extracted
from the SWF, with anything hand-written laid over it.

data/extracted/ is regenerated wholesale from the original whenever the
extractors change, so nothing written by hand can live there. data/content/
is where it goes instead: a file of the same name as an extracted table, in
the same shape, holding only what is new or different.

  * A table that is a list of records -- abilities, items, units, battles,
    zones, buffs -- is matched up by `id` (buffs by `key`). A record whose id
    is already there is merged into it field by field, so an overlay can
    change one number and leave the rest; a new id is added, and the table is
    kept in id order.
  * A mapping is merged key by key, all the way down.
  * A list inside a record is replaced whole, except that a mapping whose keys
    are indices ({"12": "..."}) sets those entries of it -- which is how a
    line of the language table is changed or added without restating the
    rest.
  * Setting a field to null removes it.

    from content import load
    abilities = load('abilities')
"""
import json
import os

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
EXTRACTED = os.path.join(ROOT, 'data', 'extracted')
CONTENT = os.path.join(ROOT, 'data', 'content')

# What identifies a record, for the tables that are lists of them.
IDENTITY = {'buffs': 'key'}


def _identity(table):
    return IDENTITY.get(table, 'id')


def _merge(base, over, table=None):
    if over is None:
        return None
    if isinstance(base, dict) and isinstance(over, dict):
        out = dict(base)
        for key, value in over.items():
            merged = _merge(base.get(key), value)
            if merged is None:
                out.pop(key, None)
            else:
                out[key] = merged
        return out
    if isinstance(base, list) and isinstance(over, dict) \
            and all(k.lstrip('-').isdigit() for k in over):
        out = list(base)
        for key, value in sorted(over.items(), key=lambda kv: int(kv[0])):
            index = int(key)
            while len(out) <= index:
                out.append(None)
            out[index] = _merge(out[index], value)
        return out
    if table and isinstance(base, list) and isinstance(over, list):
        ident = _identity(table)
        if all(isinstance(r, dict) and ident in r for r in base + over):
            order = [r[ident] for r in base]
            records = {r[ident]: r for r in base}
            for record in over:
                key = record[ident]
                if key in records:
                    records[key] = _merge(records[key], record)
                else:
                    records[key] = record
                    order.append(key)
            out = [records[k] for k in order if records[k] is not None]
            if ident == 'id':
                out.sort(key=lambda r: r['id'])
            return out
    return over


def overlay_path(name):
    return os.path.join(CONTENT, name + '.json')


def load(name):
    """One table, extracted and overlaid."""
    with open(os.path.join(EXTRACTED, name + '.json'), encoding='utf-8') as fh:
        table = json.load(fh)
    path = overlay_path(name)
    if os.path.exists(path):
        with open(path, encoding='utf-8') as fh:
            table = _merge(table, json.load(fh), name)
    return table


def overlays():
    """The tables something has been written over, by name."""
    if not os.path.isdir(CONTENT):
        return []
    return sorted(f[:-5] for f in os.listdir(CONTENT) if f.endswith('.json'))
