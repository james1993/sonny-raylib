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
    tools/extract_streams.py     the cutscene narration, which is a stream
                                 sound on each comic's own timeline
    tools/extract_markers.py     the markers on a zone's scene, and a copy of
                                 the SWF with them taken out of it
    tools/check_assets.py        every name the engine can ask for, against
                                 what the art build shipped
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

`build/simulate` prints a battle move by move.

`tools/playtest.py` is how differences are found now, instead of by eye. One
script drives both games -- the real SWF under Ruffle and `build/sonny` --
through the same clicks at the same moments on the same 800x575 virtual
display, photographs both at the same points, and reports where they disagree,
worst first, with each difference located on a grid. A line can be addressed
to one side (`ref:` / `mine:`) where the two must be driven differently, which
is how the original's preloader is clicked through without the replica needing
one. `tests/play/zone1.txt` is the opening of the game end to end.

    python3 tools/playtest.py tests/play/zone1.txt \
        --ruffle path/to/ruffle --swf path/to/sonny1_dbg.swf

A mean difference under 3 is the text rasteriser; past that something is
missing. It found the victory screen's whole bottom half, the story's notes on
the hub, and the reticle's name sitting eighteen pixels right of its ring.

## Things that bite

- **ActionScript's undefined is not zero.** `FOCUSN >= undefined` is false, and
  that is load-bearing: it keeps the "None" placeholder out of the AI's move
  lists, which in turn decides whether the retreat check runs at all.
- **A SWF RECT is Xmin, Xmax, Ymin, Ymax** -- not Xmin, Ymin, Xmax, Ymax.
- **An exported image's top-left is not the art's origin.** Every export has an
  SWF sibling whose root transform records the offset; draw at `target - offset`.
- **A clip's scale carries its text with it.** The box and the type size
  shrink with the clip, not only the corner the box starts at. Scaling one and
  not the other is what pushed the reticle's name and level off the middle of
  its ring, by exactly the 18.3 pixels the gutter accounts for.
- **A name that is not there is silent.** attachSound on a missing name does
  nothing in Flash, and neither does asset_texture here. Three of the four
  battle tracks and all thirty-five lines of battle speech were missing for
  that reason, without anything saying so. tools/check_assets.py is the guard.
- **An exported image is a pixel bigger than its box.** The decompiler
  rasterises at one pixel to the unit and rounds the canvas up, so a 60-unit
  shape comes out 61 pixels with the last row and column empty. Drawing it
  into the box the SWF declares squeezes 61 pixels into 60 -- less than a
  pixel, and a pixel off the corner of every icon on the screen. Draw at the
  image's own size and let the padding fall outside.
- **The right-hand bar is mirrored twice if you are not careful.** The battle
  bar widget is mirrored for slots 2, 4 and 6 -- the container carries a
  negative horizontal scale, so the art has to be flipped about the bar's
  centre. Its *text* must not be: the original does not flip the fields with
  the widget, it lays them out a second time already mirrored, which is what
  the "right" rows of SONNY_BAR_FIELDS are (negative x, alignments swapped).
  Flipping those a second time undoes them, and the enemy's name and numbers
  land in the middle of the bar instead of at its two ends.

- **The streak behind a projectile is anchored, not carried.** KrinTrail is
  attached once, on the bolt's first frame, at the point the bolt has reached
  by then, and turned to face the way it is going. After that it never moves:
  the bolt runs on and the streak is stretched along its own x to follow it,
  by 8.3 per cent of its length for every unit the bolt covered that frame.
  Its art is ten units long and runs from clear at the tail to solid at the
  head, with the clear end sitting on the clip's origin, so stretching leaves
  the tail at the throw. Drawing it centred instead makes a comet that flies
  along with the bolt, which is not the effect at all.
- **KrinFilterBlur0 is empty, because of a typo in the original.** The line
  that should fill it reads `KrinFilterBlu0r.push(KFBlur0)`, so the blur it
  was built with is pushed onto a name nothing ever reads and the array stays
  empty. Two of BATTLEBLUR's eight steps therefore apply no blur at all. It
  is a quirk, so it is reproduced.

- **The model carries one slot that is not part of the body.** Depth 33 of
  MODEL1 holds whichever effect belongs to what is being done: the sweep
  through the magic swing (932), the orb a caster charges (936), the crackle
  of being held stunned (939). swf_doll.py keys it "@33" because it has no
  instance name, and it is the only slot whose character has to be read out of
  the display list rather than from the wearer's equipment.
- **attack1 runs straight into attack2.** There is no stop at the model's
  frame 92, so a swing is thirty frames, not fifteen: fifteen of attack1 and
  then fifteen of attack2, which is where the sweep lives. krinMeleeAttackCD
  and krinMeleeAttackEndCD are both 15 for exactly that reason. Holding the
  animation on attack1 means the sweep never shows at all.
- **setRGB beats the colour transform under it.** The cast orb is placed with
  a hard green cxform (rgb multiplied by nothing, 255 of green added), which
  is why it exports green -- but the layer's own onClipEvent(load) then calls
  setRGB, which replaces the colour outright. The placement's colour is dead;
  only its alpha matters. Each effect is a pair of layers and only one of them
  carries that handler, so the other keeps the colour it was drawn in.

- **The reticles come off the moment a move is committed.** When the turn
  clock reaches BattleTimeLimit the original sets _visible = false on all six
  KrinSelectors and on the loose selector, and puts them back only when it is
  the player's turn to pick again. They do not fade for that. Leaving them up
  draws a ring and a name over whoever is being hit all through the swing.

- **BATTLEBLUR never runs.** The motion blur over the battlefield is guarded
  by KrinSettings.SFX2, and the only place that sets SFX2 sets it false on
  both branches of its one `if`; nothing else in the file assigns it and there
  is no setting that turns it on. The twenty-frame clip and all four
  KrinFilterBlur arrays are dead code. It is not reproduced, because the game
  does not do it.
- **Writing alpha needs the separate blend factors.** raylib's ordinary blend
  runs the destination alpha through the source's alpha as well, so drawing
  something with alpha a onto a cleared render target stores a squared. main.c
  sets separate factors for exactly this reason, but only turns them on inside
  the pass that draws the stage -- so anything with a render target of its own
  has to ask for BLEND_CUSTOM_SEPARATE itself. A silhouette blurred twice
  without it comes out at the fourth power of itself, and an edge that should
  be half lit reads as a tenth.
- **A Flash filter's blurX is a width, not a reach.** BlurFilter(10, 10, 1)
  blurs over ten pixels, so the kernel runs five either side. Taking it as a
  radius makes every glow twice as wide and half as strong, which turns an
  edge into a smudge.
- **The hit flash is all filter, no colour.** BATTLEFLASH calls
  krinChangeColor(inner, "Hit") as well as setting the filters, but "Hit"
  sets every offset to zero -- it does nothing. The whole effect is the three
  glows: a white inner one a hundred pixels across at strength ten, which
  saturates and leaves a flat white cut-out, and the two ten-pixel orange
  edges inside and outside it.

- **Each model has a playhead of its own.** Two of the things it plays are
  started by something landing on it rather than by the move being made: the
  recoil, and dying. Clocking those off the move's counter runs them from
  wherever that counter had reached -- and after a melee attacker has walked
  sixty frames, the seventeen-frame recoil is already over before the blow
  lands, so nothing ever flinches. Each animation has to say which counter it
  is played from.
- **Only run and stand are loops.** The model's frame scripts say so: frame 45
  goes back to "stand", 77 is a bare stop, 128 and 276 run on into standing,
  243 goes back to "stun2", and 335 removes the model outright. So runback
  plays once and then the character stands there while it finishes sliding
  home; looping it plays the landing two or three times over.
- **A slot that does not act costs one frame, not twenty-five.** The pause
  between moves is AttackEndCounterLimit, and the original only ever sets
  AttackEndCounter when a move actually fires. A slot that passes, or whose
  caster is stunned or down, or whose target is already dead, is stepped over
  in the same frame. Charging the full pause for those stacks up: with three
  such slots ahead of the player, most of a second of nothing sits between
  choosing a move and anything moving.
- **A fight does not end on the killing blow.** The win is only checked once
  the whole round has run, and then blacker5 plays from its frame 31 to its
  85 over the battlefield -- fifty-four frames -- before the screen changes.
  That is the time the death animation plays in.
- **A unit can die without anything reporting it.** buffTicker plays "dead"
  itself when a tick takes someone down, as do the two damage branches. The
  reliable place to notice is the moment a unit stops being active, which
  catches all three.

- **The decompiler plays nested clips; it does not obey their stops.** When it
  renders a sprite frame by frame it steps the clips inside it too, ignoring
  any stop they carry -- so a holder longer than the clip inside it comes back
  with the inner clip wrapped round to its start. moveSelectBoomer is exactly
  that: 1610 is an empty twenty-five frame holder that places 1609 on its
  frame two, and 1609 is twenty-one frames with a stop on its last. The last
  three frames handed back are the ring setting off again just as it finishes.
  The clip's real last frame is the holder's twenty-second. A quick way to
  spot the others: an exported animation whose final frames are byte-identical
  to its opening ones.
- **A nested clip steps once per stage frame.** The file runs at thirty and so
  does the stage, so the model's own timeline is one frame per frame. Playing
  it off half the clock ran every idle at half speed.
- **The tooltip's gap belongs there.** The original leaves three pixels of
  screen between the title block and the body: the title field is eighteen
  pixels tall -- a sixteen-pixel line plus the gutter a Flash field keeps
  inside its border -- and the body is placed at twenty-one. Measuring the
  block as the bare line makes the gap five pixels instead of three, which is
  what reads as the two halves coming apart. autoSize does not pull the sides
  in once wordWrap is on, so both blocks stay the width they were created at.

- **A clip's scripts may be filed under its exported name.** The decompiler
  writes them to DefineSprite_<id>_<EXPORTNAME>, so looking for
  DefineSprite_801/ finds nothing while DefineSprite_801_KrinBuffShower/ has
  the handler in it. Searching by id alone will tell you a clip has no code
  when it does -- match the id with a trailing wildcard.

- **A unit is dead long before it falls over.** The original takes the health
  off and sets active false at the moment the blow lands. This side works the
  whole move out when it resolves and only shows it when the blow lands, so a
  unit killed by a melee attacker has been inactive since before the attacker
  set off -- a walk's length of frames early. Anything driven off `active`
  rather than off what has been shown will run early by that much: the bars
  hold their old value until the blow lands for the same reason, and the death
  animation has to wait on it too. Waiting is not enough on its own, either:
  the animation has to be chosen from its own clock rather than from `active`,
  or the model drops dead on the frame the move resolved regardless.

- **Only four places ask for music, and one place stops it.** The hub frame
  asks for a roaming track, the frame that loads a battle asks for a fight
  one, the opening cutscene asks, and the sound toggle asks. Winning goes to
  the hub frame, so the roaming track starts there and the results panel is
  drawn over it -- which is why there is music behind the rewards. Losing goes
  to resetHere, which is the whole game's reset: stopAllSounds, the counter
  back to zero, soundModeKrin back to zero. Grepping for addSound alone says
  the fight's track carries on into a loss, which is wrong -- the stop is not
  an addSound call.
- **The menus have no music.** Nothing between the first frame and the hub
  ever asks for a track, and resetHere stops whatever was playing on the way
  past, so the title and the menus sit in silence. "menumusic" being the first
  of the four tracks is misleading: it belongs to whoever calls for a roaming
  track first, which on a new game is the opening cutscene.

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
* A marker is a clip of its own: an 82-frame pinwheel drawn in grey, turned
  red, cyan, gold or green by the colour transform on its placement and given
  its halo by a glow filter there. Neither is in the clip, and the decompiler
  freezes a nested timeline at frame one, so the scene it exports has a still
  marker painted into it. The art build therefore runs twice: once over the
  SWF as it ships, and once over a copy with the marker placements taken out
  of the zone clip, which is where the scenes come from. The markers are
  rendered separately, style by style, with the colour transform applied and
  the glow worked out from the clip's own alpha so it turns with the wheel.

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
  string a field shows -- its instance name says nothing. More than one field
  can be bound to the same variable, and setting it fills all of them: that is
  how these screens get a drop shadow, a dark copy under a light one. Drawing
  only the first one found leaves the advice on the settings screen black.
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
  pressed. That art is recorded at the button's own size, so what the screen
  shrank the button to has to be carried with it -- the hub's row of icons is
  placed at 0.81 and the little marker beside the map at 0.30.
* The hub's icons are painted flat into the panel behind them; the border and
  the glass over each one are the button's art, and without it they read as
  flat colour.
* A cutscene is one long animation whose own frames carry its script: it sets
  a counter and, on the frames where the caption changes, shows the next line
  of CUTSUB or clears it. The intro is 1306 frames but only 166 pictures --
  most frames repeat -- so the asset build copies each distinct one once.
* Its narration is not a sound anything starts. Each comic carries a *stream*
  sound -- a SoundStreamHead on the sprite and a block on every frame -- which
  no script mentions and the decompiler's sound export does not contain. The
  header says MP3, mono, 22050 Hz, 735 samples a frame, which at the SWF's 30
  fps is one frame of audio per frame of animation: the track is the
  animation's clock, which is what Flash holds a timeline to. The comic runs
  on that playhead and keeps its own time only while the playhead is not
  moving -- Flash falls back to the frame rate when the sound cannot play,
  which is why the original stands still under Ruffle, which does not.
* The opening comic does not lead to the hub. PLAY sets gotoSceneKrin to
  "IntroSeq" before going to CS_INTRO, and SKIP and the comic's last frame
  both hand the root timeline to it: fifteen frames that end by setting
  BattlePick to 2 and progressFight and going to LOADBATTLESCENE. The story
  starts in a fight.
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
  original never shows. There is no clean edge to crop to -- the spread fades
  through the canvas -- so the screen keeps the picture inside the black
  backing behind it, which is the map's real extent and comes off the frame
  like everything else.

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

## The stage and the window

The stage is 800 by 575 -- the SWF's own header -- and every coordinate in the
game is in it. That is an authored size, not a window size: Flash scales the
stage to whatever the player is given and letterboxes to keep its shape, so
the window here is resizable and `stage_fit` puts the stage in it. Drawing and
the pointer both go through that one function, which is what keeps a click
landing where it looks like it landed; `tests/test_window.c` checks the round
trip at nine window shapes.

The stage's texture is the size the stage is shown at, not 800 by 575, and the
projection puts the stage's own coordinates over the whole of it
(`render_stage_projection`). That is what keeps the game as sharp as the
window it is in: a fixed 800 by 575 texture makes the stage's pixel the unit
of resolution and a window twice the size then smears each of them over four,
where the original is vector art rasterised at whatever size Flash is given.
Drawing code goes on working in stage units and needs to know nothing about
it. Three things do:

* **The text bakes.** A bake is a picture of the glyphs at a fixed size, so
  the size it is made at is the size it is sharp at. `face_size` makes the
  drawing bake at `render_scale()` times the stage size and lays each glyph
  down at a stage unit to its texel. The *measuring* bake is deliberately left
  off the render scale -- it only ever yields advances, in stage units, so
  keeping it fixed is what makes every line of text lay out identically at
  every window size.
* **The glow's targets** (`glow.c`), for the same reason, with the blur's
  kernel walking the target's texels rather than stage pixels.
* **The scissor.** `BeginScissorMode` is the one thing in raylib set in the
  framebuffer's own pixels rather than through the projection, so it has to be
  scaled by hand -- `render_scissor` does it, truncating each of the four
  numbers on its own, which is what the box came to when a stage unit was a
  pixel. Missing this leaves the battlefield drawn into a corner of the
  window.

`SONNY_WINDOW=WxH` opens at exactly that size, which is how a capture is taken
at anything other than the stage's own.

## Driving the game without a pointer

`SONNY_CLICKS="frame:x:y,..."` presses at a stage coordinate on the frame
given, so a whole run through the menus can be checked from a script:

    SONNY_CLICKS="5:393:355,12:392:341,20:270:208,28:399:274,36:250:470" \
    SONNY_SHOT=shot.png SONNY_STEPS=70 ./build/sonny

That path is title, New Game, slot 1, Destroyer, PLAY -- which lands on the
opening comic.

* A fight's money is a roll, not a number: `round(EnemyXPFinal * EuroConstant
  * ((85 + random(30)) / 100))`. The first fight pays 5, 6 or 7 and all three
  are right.
* An item says more about itself than a name and a line. Equipment gets a
  taller tooltip -- the frame's GO4 -- with what it takes to wear it ("Lvl. 1
  Headwear"), then one line for every attribute it adds, then its own text,
  and the name sits on a backing tinted by its rarity. A store puts the price
  in front of the name; a bag slot does not. The attribute lines come out
  backwards from the arrays, because that is the order ActionScript's for..in
  hands an array's indices back.
* What is on a unit in a fight hangs off its own bar, not off the unit: a
  KrinBuffShower for each, longest first, 110 out from the middle of the bar
  and 17 apart, going the way that team faces.
* The story stops the player on the hub at seven points to explain something
  (progressSpeech, keyed by how far the story has got). Proceed! on the
  victory screen is what offers one; winning re-arms the offer.
* The zone's progress bar is a track with a fill inside it, and only the fill
  is scaled -- the track stays the width it was.
* A comic's narration and a unit's speech are both sounds the engine can ask
  for by name, and both were missing from the art build. So is the balloon
  that pops over whoever is talking: it is part of the unit's own container
  (character 974, placed as "speech"), mirrored inside it, and played through
  once when that unit says something.

Known to differ: the reticle fades in and out here over six frames. The
original does fade, but nothing in its scripts does it and the clip is a
single frame with no tween, so the ramp is ours.

* A reticle's colour is not a decision the code makes: the original parks six
  of them, one per unit, and the colour is a transform on each placement. The
  player's carries none at all, so it keeps the art's own white; an ally's
  adds (-102, 0, -189), which comes out green; an enemy's adds (0, -138, -159),
  which comes out red.
* The music is one playlist of four walked by a single counter that the hub
  and every fight share, so which track a fight gets depends on what has
  played since. IntroSeq seeds the counter to 1 before the first fight, which
  is why that fight opens on the second track. The boss theme stands in for a
  boss marker or for battles 24, 30 and 36, and the counter moves either way.

* The turn indicator's ring is a clock, not a picture. `thingerClock`
  (character 1595 inside battleClocker) places 1594 under two glow filters and
  stops on one of two frames: "friend" with an inner glow of #0066FF (blur 8,
  strength 1) and an outer of #0066CC (blur 27, strength 1.7), "enemy" with
  #FFCC00 inner and #FF6600 outer, chosen by whether the player's own teamSide
  is TeamMoveNow. Inside 1594 the ring is one arc (character 1592, the right
  half) held twice, the second copy turned through 180 degrees, each masked to
  its own side and rotated every frame by clip actions reading
  `_root.BattleTimeNow` -- so the dial fills as the turn runs down. Turn-based
  play, which is how the game is played, pins BattleTimeNow at BattleTimeLimit
  whichever side is moving, so the ring is always whole and only its colour
  says anything. The decompiler renders 1594 as a one-pixel sliver, because at
  rest the two halves are masked away, and drops the filters besides; the art
  the engine draws is composed and lit by tools/extract_glows.py instead.
* A glow's blur is in box passes, and the number of them is the filter's
  quality: n passes of width w have a standard deviation of w*sqrt(n/12). The
  zone bar's glow is three passes and the turn ring's is one, which is why the
  ring's blur of 27 is tighter than the bar's of 19.
* Nothing draws a button unless a screen asks: a button is a list of state
  records, not a sprite, so it has no art to export. The battle screen's own
  two are the pass button in the middle of the dial and the quit button in the
  corner, whose dark glass plate over the red cross beneath it is what made
  that corner a flat bright square while it was missing.
* A move is delivered in one of three ways, and the way decides what is seen.
  Melee sends the attacker over: krinMelee walks it to where the two models
  meet (the target's x less half of each width), holds fifteen frames for the
  swing -- which is when the hit actually lands and the boom is attached --
  fifteen more for the follow through, and then walks it home. The pace is a
  sixtieth of the way there times ten a frame, scaled by 1.5 - 1.45 * how far
  along it is on the way out and 1.45 * that on the way back, capped at one:
  flat out for the first third, easing into the target, and the reverse going
  home. Missile throws a bolt and Shock lands on the spot.
* GridZoomer leans the battlefield in on whatever a move is aimed at. Ten
  steps in and ten back, each step proportional to how many are left, so it
  starts fast and settles; the numbers work out to a factor of 55, a gain of
  thirteen per cent of scale, and a finish that puts the target's middle at
  (400, 300) taken back by the delivery's own ratio -- 0.4 for melee and
  missile, 0.3 for shock. It holds at the top for thirty frames on a melee
  swing, five on a bolt and one on a shock. Only BATTLESCREEN moves, which is
  why the bars and the panels round it stay put; the reticles are on the root
  and are set to the unit's place plus BATTLESCREEN's, so they follow the pan
  but not the scale.
* A buff's widget is attached inside the unit's bar clip, not beside it, so
  the 110 out and 17 apart the code uses -- and the art itself -- come out at
  the bar's own scale of 1.085 by 1.161. Ignoring that put the icons nine
  pixels in from where the original has them and drew them a twelfth too
  small.
* A lit piece is the flat one with a margin round it for the halo, so its own
  origin sits that much further into the picture. Recording the margin as the
  origin instead drew the zone bar's fill 58 across and 65 down from the
  track -- off the bottom of the stage, which is why the bar looked empty
  however well the art came out.
* Hovering an empty square says "None / This slot is empty.": the original
  reads item zero rather than skipping the square, and it does that in the
  bag, in the store and on the victory screen alike.
* What a fight pays is EnemyXPFinal * EuroConstant * ((85 + random(30)) / 100),
  rounded, where EnemyXPFinal is the enemies' average level times 1 + 0.2 per
  enemy and EuroConstant is 5. The first fight's one level-one enemy makes that
  1.2 * 5 * 0.85..1.14, which rounds to five euros on seven of the thirty
  rolls, six on seventeen and seven on six -- so five and six are the same
  formula, not a difference in it.
* The original has no seed of its own: Flash seeds random() from the machine
  when the movie opens, so no two playthroughs roll the same. Standing on a
  constant made every playthrough identical down to the euro, which is what
  made that payout look wrong every time rather than sometimes. The clock
  stands in for Flash's entropy now; SONNY_SEED, or a seed on the command
  line, still pins it for the playtest and for headless captures.
* The exclamation mark at the turn indicator is character 1536, a thirty-pixel
  clip of forty-eight frames that pulses from a bright yellow to a dim orange
  and back. It sits at depth 18, under the whole bottom panel, and shows
  through the hole in the middle of the dial. Taking only its first frame --
  which is what the asset builder used to do for every clip of more than one
  -- is what left it dead.
* The orb at the indicator carries the two lines the original writes into it
  at load: "No ability selected!" and "You may click here to skip your turn."
  in turn-based play. They are literals in the clip, not entries in the
  language table. Once a move is chosen the orb says the ability's name and
  "You will use this ability on <name>. Click to cancel this ability."
* What answers the click there is krinToMove2, a button. It has no furniture
  row of its own -- only a button row -- so asking for its chrome gave
  nothing and the click never landed.
* moveSelectBoomer (character 1610) is a white ring that closes over the
  indicator when the player chooses. It rests on an empty first frame and is
  played through once.
* The space bar ends a line of speech: it sets nextSpeechKKK, which fades the
  box out, and stops my_sound1..3 so the voice over goes with it. Nothing in
  the original passes a turn on a key.
* An attribute point has to be rebuilt into the character's running totals.
  The screen reads stat_sets, which is a cache of what has been spent plus
  what is worn, so a point that only lands in spent[] never shows.
* An equipment row is read off whoever the menu is showing, which the party
  row can change: reading the player's rows while drawing an ally's gear put
  one character's words over another's. An empty row names what belongs in it
  (ITEMSS by the row's own number), not the empty item's own words.
* A node in the talent tree says four things: "(rank/tier) name", what the
  move does at the tier below the rank (the first tier while nothing is
  spent), what it costs, and "Next Tier (Lvl. N): ..." with what the next
  tier would do -- or "This ability is at its maximum tier." The level it
  wants is LEVELMIN + LEVELSCALE * rank. A passive node reads the same off
  the buff it grants, and says "Passive Combat Effect" for its third line.
* The box on the skills screen picks one of MENU[43..46] at random as it
  loads, unless there are points to spend -- then it walks through MENU[7]
  to MENU[10], one per click of "< Click Here >".
