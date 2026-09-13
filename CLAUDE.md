# Working on this port

A from-scratch reimplementation of *Sonny* (2008) in C with raylib, ported from
the original game's own logic and art. The rule this was built under: when a
question comes up, consult the original. If the game has it, include it; if it
doesn't, leave it out.

## Where everything comes from

Nothing here is guessed. The game ships as `SONNY1.swf` inside the Legacy
Collection, and every number, string and pixel is extracted from it:

    tools/extract_assets.sh      the decompiler dump (~200 MB, gitignored)
    tools/extract_data.py        abilities, units, items, buffs, talents,
                                 battles, zones, dialogue, all display text
    tools/swf_exports.py         export names, frame labels, shape bounds
    tools/swf_doll.py            timeline walker: per-frame part transforms
    tools/extract_stage.py       where the interface goes, from the display list
    tools/build_assets.py        selects the art the engine names
    tools/gen_c_data.py          data tables -> src/gen/gamedata.c
    tools/gen_asset_manifest.py  assets, doll frames, layout -> src/gen

`make data` regenerates the C tables; `make vectors` regenerates the test
vectors. Both need `data/extracted/`, which comes from the SWF.

## How the port is checked

Pure functions are ported statement for statement, then diffed against an
independent transcription of the same source (`tools/ref_*.py`) over generated
cases. The turn driver is a state machine rather than a pure function, so it is
checked by properties instead. `make test` runs all of it.

`build/simulate` prints a battle move by move. That exists for the one check
that is not automated: setting up the same fight in the real game and comparing.

## Things that bite

- **ActionScript's undefined is not zero.** `FOCUSN >= undefined` is false, and
  that is load-bearing: it keeps the "None" placeholder out of the AI's move
  lists, which in turn decides whether the retreat check runs at all.
- **A SWF RECT is Xmin, Xmax, Ymin, Ymax** -- not Xmin, Ymin, Xmax, Ymax.
- **An exported image's top-left is not the art's origin.** Every export has an
  SWF sibling whose root transform records the offset; draw at `target - offset`.
- **Enemies and the player use different stat formulas.** `krinAddNewUnit`
  scales linearly with no rounding; the player's path rounds up over a
  level-scaled baseline. One formula for both is wrong in both directions.
- **Position follows the slot, not the speed order.** teamAdder decides who acts
  when, nothing else.
- **Quirks are reproduced, not fixed.** Rolls come from a 100-entry table that
  repeats within a battle; XP discards its overflow; a shield equal to the
  incoming hit does not absorb it. The balance sits on top of these.

## Conventions

- `src/core/` is pure C and links nothing but libc, so battles can be simulated
  headlessly and replayed from a seed.
- `src/platform/` is raylib: screens, assets, audio.
- `src/gen/` is generated. Do not edit it; change the generator.
- Text is drawn with `ui_text`, never raylib's `DrawText`, so it uses the
  game's own font.
