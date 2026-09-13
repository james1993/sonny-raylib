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

## Checking against the original

The original runs here. `tools/refcap.py` starts Ruffle on a virtual X display,
drives the game with synthetic clicks and keys, and saves PNGs, so any screen
can be put side by side with the replica's own `SONNY_SHOT` capture.

Two things are needed to get past the title:

* The Legacy Collection's SWF is a client of the Adobe AIR shell around it.
  Root frame 65 runs `if (!isDebugMode) stop();` and waits for the shell to
  tell it to go on, which never comes when the SWF is run on its own. Frame 2
  sets `isDebugMode = false` outright, so a flashvar cannot override it; the
  capture script patches that one boolean in the bytecode and writes an
  uncompressed copy to run.
* There is no audio device, so the intro cutscene -- which advances on the
  narration's playhead -- never moves. Click SKIP.

What the original settled that guesswork had got wrong:

* The battle screen's furniture is the root timeline's own display list on the
  KRINBATTLESCENE frame, including a clip-depth mask that is why the backdrop
  stops at the battlefield frame instead of filling the stage.
* Its two backdrop layers are containers the game retargets on load, the sky
  with `gotoAndStop(Krin.SkyBG)` and the ground with `gotoAndStop(Krin.ZoneBG)`.
* The health bar does not tint its fill. It points a hundred-frame flat-colour
  clip at `round(percent * 100)`, red through yellow to green.
* The bars' text is set in `_sans`, one of Flash's device fonts, so the player
  draws it in a system face -- Arial on Windows -- not in the Tahoma the SWF
  embeds. Alignment, leading and colour are all in the DefineEditText tags.
* Flash sizes text by the em square; raylib bakes a font so its ascent plus
  descent comes to the size asked for. Text is a fifth too small until the
  request is scaled by the face's own ratio between the two.
* raylib rounds every glyph advance down to a whole pixel, which at nine
  points loses about half a pixel a letter -- a line comes out the better part
  of a fifth too narrow and wraps in the wrong place. Text is measured against
  a much larger bake of the same face and laid out a glyph at a time.
* The player picks a target first. Hovering a unit parks a ring of eight orbs
  around it, one per slot of the loadout, and clicking an orb uses that move on
  that target; the panels along the bottom stay empty until something happens.
* The right-hand team's containers carry a negative horizontal scale, so a
  mirrored quad winds the other way and OpenGL's backface culling eats it.
* The same frame label appears in several sprites. A backdrop has to be taken
  from the container the game retargets, not from whichever sprite comes first.

Known to differ: text is rasterised by stb_truetype without hinting, so stems
land between pixels where Flash's device-font rendering snaps them onto one.
Size, spacing, wrap and colour match; the weight reads lighter.
