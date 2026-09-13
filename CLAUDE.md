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

* Every screen that is not the fight is one clip with a frame each --
  inventory, win, shop, skills, data, options -- and the same slot name means
  different places on different frames. Laying the victory screen out from the
  wrong frame is an easy mistake: `win` is frame 9, not frame 16.
* A menu's slots are clips the engine fills. Their empty square is still drawn
  for the bag; a drop square is only there while something is in it.
* raylib bakes printable ASCII and nothing else, so the euro sign the game
  prints against every price has to be asked for by codepoint.

* Buttons are not part of the art -- a DefineButton2 never renders -- so what
  a screen responds to only exists in its button records. Their hit boxes are
  extracted from the SWF; a button's character id is what says what pressing it
  does. Two things bite: a DefineButton2's header is five bytes, not six, and
  its records always carry a colour transform whether or not a flag says so.
* The hub is a scene with a frame per zone and markers on it -- one starts the
  next fight, one opens the store -- not a list of buttons.

* An item's picture is not in the icon set the abilities use. Every slot shows
  its contents through one clip whose frames are labelled with item names, and
  the game points it at whatever is in the slot.
* The character sheet's two bands of element bars are eight pieces each, one
  per element, coloured and sized by the character's own numbers.
* Sonny starts the story already wearing something: Krin.equipArray0 is
  [0,0,0,4,8,5,0].

* A button does carry art, in its own state records, and for a screen whose
  furniture is a row of buttons -- the plus signs beside the attributes -- that
  is the only place the art exists.
* The ability tree's branches are drawn at run time, not exported: a six-wide
  black line with a two-wide one over it, gold (0xFFCC00) once the prerequisite
  is learned and 0x2B2B2B while it is not.
* A tree node shows its move's icon, but a passive node shows its *buff's*
  instead -- the orb clip has a frame for each -- and every node wears the
  "cannot use this" disc until a point goes into it. The disc's alpha is not
  one number: 80% on the tree, 85% round a unit in a fight.
* The rank over a tree node is there but invisible: the tree hides it unless
  the space bar is held.
* An orb's icon is drawn larger than its ball and cut to it by a circular
  mask. raylib has no masking, so each icon is combined with the mask once and
  the cut copy is what the orbs draw.
* `for..in` over an array in ActionScript 2 hands back its indices last to
  first, which is the order the ability pool fills its rows in.
* A store's stock is a fixed list of item ids per store (krinSetShop), and
  which store a marker opens is only in the marker's button: the shopId its
  handler sets. A zone can carry more than one -- the fourth has three -- so
  it cannot be taken from the zone.
* The store's picture is a clip with a frame per store, pointed at shopId + 1
  by number rather than by name.
* A field says which variable it is bound to, and a frame fills its screen by
  setting those variables. That binding is the only reliable way to tell which
  string a field shows -- its instance name says nothing.
* A field with no variable is never set at run time: what it was authored with
  is what it shows, as the HTML it was authored in.
* The front end is a chain of root frames -- mainMenu, subMenu, dataMenu,
  classMenu, optionsMenu -- and nameMenu sits between them without ever being
  reached, so the game never asks for a name.
* The tooltip is built at run time, not laid out: KrinToolTipper creates its
  two text fields on the spot, 170 wide, _sans 12 with the title bold on a
  light backing and the body white on a dark one, and each backing is
  stretched to whatever the text came out as. It rides the pointer, flips to
  its left past x 570 and rides up past y 500.
* A save slot is named for who is in it -- "Lvl N Class" -- not for the
  player, and the game never asks for a name.
* A button carries art for its resting state and its over state, and swapping
  between them is the only thing most of these buttons do to show they can be
  pressed.
* A cutscene is one long animation whose own frames carry its script: it sets
  a counter and, on the frames where the caption changes, shows the next line
  of CUTSUB or clears it. The intro is 1306 frames but only 166 pictures --
  most frames repeat -- so the asset build copies each distinct one once.
* Winning decides where the game goes next, not the hub: the save is written
  first when autosave is on, a zone just finished goes out to the map (by way
  of a comic after battle 9 and after 38), and otherwise it is back to the hub
  or on to the ability screen when the fight was a level.

* Nothing in this game is dragged. The pointer carries one thing at a time --
  Krin.mouseItem for an item, UITmouseHold for an ability -- and every slot
  swaps what it holds with what is carried. An equipment row takes only its
  own kind of item (its index plus two), for this class or any, at or below
  the character's level. The recycler pays a quarter of an item's price,
  rounded up. An ability can only take as many places on the action bar as
  its own number allows.
* The fifth button on the hub's row gives every point back, and the scenery on
  a zone's scene is clickable: the settings screen counts how many pieces of
  it have been found.

* Losing stops on loseCombat for a moment and then goes to gameOverMenu,
  where the only way on is to load the slot the run was in.

* Every marker on a zone's scene is a button and nothing else says what it is
  for: one starts the next story fight, one rolls a practice fight out of the
  zone's training list, one opens a store, and the rest are scenery. The
  practice marker rolls against its own number, not the list's length -- the
  second zone lists nine and rolls eight.
* A fight only carries progress when the story marker started it
  (Krin.progressFight). Replaying a zone's last fight is a boss fight that
  pays out and leaves progress where it is.
* The party is six parallel arrays, and who has joined is friendArray, which
  the story rewrites at exactly two points: Veradux at battle 15, everyone at
  38. friendArrayX names which two stand in the line.

* The world map is a picture with a marker per zone, shown once progress has
  reached the battle before that zone's first, with the route drawn between
  the ones showing at run time. There is no way off it but to pick somewhere.
* The map picture's export carries a margin its filter spread into, which the
  original never shows; the black backing under it is its real extent.

* The attribute swatches go grey the moment the last point is spent, and are
  coloured again the next time the screen opens -- the menu clip goes back to
  its first frame, so the grey never survives a visit.

Deliberately left out: the preloader. The original waits on a screen that
says "< CLICK TO PLAY >" while the SWF streams in. Nothing streams here, so
the screen would only be a gate to click through; the frame it is on has no
play button of its own, only the sponsor's link.

Known to differ: text is rasterised by stb_truetype without hinting, so stems
land between pixels where Flash's device-font rendering snaps them onto one.
Size, spacing, wrap and colour match; the weight reads lighter. Bold is only
ever asked for in one place -- the tooltip's title, my_fmt2.bold -- and that
is drawn in the bold weight of the same face.

Known to differ, and not ours: the reference is Ruffle, which substitutes its
own face for Flash's device fonts. Its euro sign is half again as wide as
Arial's, so the purse reads wider there than here. Everything measured around
it -- the cap heights, the baselines, the field boxes -- matches.

Known to differ: an orb under the "cannot use this" disc comes out darker here
than the reference renders it -- 43/255 against 52/255 on a white icon at the
80% the original asks for. The disc is drawn at exactly that alpha, and the
same draw at 0% and 100% lands on the right values, so the gap is somewhere in
how the blend between them resolves; it has not been run to ground yet.

* The bonus a character carries (Krin.StatSetsN, PerSetsN, DefSetsN) is a
  running total, not something worked out from what is worn: an item only
  enters it by passing through an equipment slot. The gear the story starts
  Sonny in is worn without ever having done so, so it contributes nothing
  until he takes it off and puts it back on -- which is why a new Sonny has
  22 strength and not 25. The respec is the only thing that rebuilds the
  total from what is worn.
* It never goes stale where it would matter: LOADBATTLESCENE works the
  fighting numbers out from the totals before every fight.

## Driving the game without a pointer

`SONNY_CLICKS="frame:x:y,..."` presses at a stage coordinate on the frame
given, so a whole run through the menus can be checked from a script:

    SONNY_CLICKS="5:393:355,12:392:341,20:270:208,28:399:274,36:250:470" \
    SONNY_SHOT=shot.png SONNY_STEPS=70 ./build/sonny

That path is title, New Game, slot 1, Destroyer, PLAY -- which lands on the
opening comic.
