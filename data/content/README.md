# Hand-written content

`data/extracted/` is the original game's own tables, taken out of the SWF, and
is rewritten wholesale whenever the extractors run. Anything written by hand
goes here instead, in a file named for the table it changes -- `items.json`,
`abilities.json`, `battles.json`, `buffs.json`, `units.json`, `zones.json`,
`talents.json`, `shops.json`, `party.json`, `story.json`, `lang.json` -- in
the same shape as the extracted one, holding only what is new or different.
`tools/content.py` lays it over the extracted table; `make data` rebuilds the
C tables from the result.

- **Records** (abilities, items, units, battles, zones by `id`; buffs by
  `key`). A record whose id is already there is merged field by field, so this
  is enough to change one number:

      [{"id": 5, "price": 12}]

  A new id is a new record and needs every field its table's records have.
  The table is kept in id order.
- **Mappings** merge key by key, all the way down.
- **Lists** are replaced whole -- except that a mapping of indices sets just
  those entries, which is how a line of text is changed or added:

      {"ENGLISH": {"ITEMNAME": {"126": "A New Sword"}}}

- `null` removes a field.

Before anything is written, `tools/check_content.py` follows every reference
one table makes to another -- a battle's units and drops, a move's buff, a
talent's moves at every rank, a shop's stock -- and refuses to go on if one
lands nowhere. `make test` runs it too, and `tools/check_assets.py` checks
that every picture and sound the tables name has been shipped.

What the fields mean is in the header the generator writes,
`src/gen/gamedata.h`, and in the field maps at the top of
`tools/extract_data.py` (`MOVE_PARAMS`, `MOVEB_FIELDS`, `BUFF_FIELDS`).
