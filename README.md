# Sonny (2008) — reimplementation in C + raylib

A from-scratch reimplementation of the combat/RPG systems of *Sonny*, the first
game in the Sonny Legacy Collection. The goal is behavioural exactness: the same
stats, formulas, ability effects, enemy AI and turn resolution, so a given fight
plays out the way it does in the original.

## Status

Combat works end to end on the original's own tables: 142 abilities, 87 buffs,
43 unit templates, the real damage and healing math, the real turn order, and
the real enemy AI. Art, audio and progression are not in yet.

- `make test` — headless tests, no raylib needed
- `make game` — the raylib front end (raylib 6.0)
- `make simulate` — `build/simulate [seed] [player_level] [enemy_level]`
  prints a move-by-move battle log
- `make data` — regenerate the C tables from `data/extracted/`
- `make vectors` — regenerate the differential test vectors
- `SONNY_SHOT=out.png SONNY_STEPS=60 ./build/sonny` — render a frame and exit
  (works under `xvfb-run`, so layout can be diffed against reference
  screenshots)

### How the port is checked

Each pure function is ported statement for statement from the decompiled
source, then diffed against an independent transcription of that same source
in `tools/ref_*.py` over generated cases: 400 damage vectors, 300 heal/focus
vectors, 300 multi-buff tick scenarios. The turn driver is a state machine
rather than a pure function, so it is checked by properties instead —
termination, legal unit state, one surviving side, replay determinism from a
seed, and outcomes that shift with enemy level.

`build/simulate` exists for the check that matters most and is not automated:
setting up the same fight in the real game and comparing the log move by move.

## Layout

    src/core/       pure C, no raylib: combat, character, campaign, saves
    src/platform/   raylib: window, fixed-stage render target, screens, audio
    data/extracted/ ability/unit/item/buff tables and all text, as JSON
    tools/          extraction and inspection utilities
    tests/          headless tests of core behaviour

`src/core` deliberately links nothing but libc so fights can be simulated
headlessly and replayed deterministically: `Rng` is seeded splitmix64, so a
(seed, action list) pair reproduces a fight exactly. That is the mechanism for
verifying the replica against recordings of the original.

## How exactness is achieved

The original is a Flash game; the Legacy Collection ships it as an Adobe AIR
captive-runtime app, so the game itself is a `.swf` sitting next to the launcher
`.exe`. Everything the replica needs is in there:

1. `tools/swfinfo.py <game.swf>` reports stage size, frame rate and tag
   inventory, and says whether the code is AS2 (`DoAction`) or AS3 (`DoABC`).
2. The ActionScript holds the ability definitions, damage formulas, level-up
   curves and enemy AI scripts. Those get transcribed into `data/*.json` and
   implemented in `src/core`.
3. Art and audio are extracted from the same `.swf` into `assets/`.

This repository is **private**, so the extracted content lives here alongside
the code and a clean clone builds and runs. It is content from a game you own,
kept for your own use -- not redistributed. If this repo is ever made public,
that content has to come out of the history first (`git filter-repo`), not just
out of the tree.

### How the art is named, and how it gets here

Almost none of it is bitmaps: 3 lossless images totalling 27 KB against 356
`DefineShape` and 519 `DefineSprite` tags. Audio is the reverse -- 88 sounds,
8.5 MB, most of the file.

The engine asks for art three different ways, and all three have to be
resolved before a single sprite can be drawn:

1. **By export name.** `attachMovie("BOOM_SLASHORANGE")`, and the character
   doll, whose part names are built at runtime as `<gender>_<part>_<look>` --
   so an item with `looks = "CROWBAR"` becomes the export `M_WEAPON_CROWBAR`.
   `tools/swf_exports.py` reads the SWF's ExportAssets table for these (418
   names).
2. **By frame label inside a sprite.** `gotoAndStop("Blood Focus")` for an
   ability icon, `gotoAndStop(ZoneBG)` for a backdrop. These labels are not in
   ExportAssets; the same tool walks each `DefineSprite` body pairing
   `FrameLabel` with `ShowFrame` to recover them (332 labels across 26
   sprites).
3. **By animation state.** The model's own frame labels -- `stand`, `run`,
   `attack1`, `attack2`, `cast`, `stun`, `hit`, `dead` -- are what an ability's
   `anim` field selects. Their frame ranges land in the asset manifest.

### Placing it where the game places it

Knowing *which* picture to draw is only half of it. Three more things come out
of the SWF's display list, because nothing else knows them:

- **The doll's per-frame transforms.** The model positions 15 named part
  instances -- `head`, `chest`, `arm1`, `weapon1` and so on -- and each carries
  its own matrix on each of 335 frames. `tools/swf_doll.py` walks the timeline
  the way a player would (place, move, remove, persist across frames) and
  snapshots every frame. Parts are drawn under those matrices directly, so
  skew and rotation survive rather than being approximated.
- **Where the interface goes.** `tools/extract_stage.py` combines the battle
  screen's placement on the root timeline with the six `player` containers
  inside it, and reads the six `p1BAR`..`p6BAR` health bars the same way. An
  element's own origin is rarely its corner -- the bar's is its centre -- so
  the exported SVG's root transform is read for that too. Position follows the *slot*, not the speed order -- and the
  right-hand team's containers carry a negative horizontal scale, which is how
  the original mirrors them to face left.
- **Where any piece of art sits inside its exported image.** Exports are
  trimmed and padded, so the image's top-left is not the art's origin. Every
  export has an SVG sibling whose root transform records exactly that offset,
  and the asset builder stores it per frame. Drawing an image with its
  top-left at `target - offset` is what puts a backdrop, an impact graphic or
  a doll part exactly where the game puts it. (The `DefineShape` bounds say
  the same thing for shapes -- and that RECT is Xmin/Xmax/Ymin/Ymax, not
  Xmin/Ymin/Xmax/Ymax, which cost an afternoon.)

`tools/extract_assets.sh` runs the decompiler over your SWF for the full dump
(~200 MB, gitignored), then `tools/build_assets.py` resolves every name the
engine can ask for and keeps just those frames: 786 files, 8 MB, with a
manifest. Three groups are expected to resolve to nothing, and the tool says so
rather than reporting a failure: enemy ability icons (they never reach the
player's bar, and the original has no art for them), the permanent
passive-talent buffs (the original's own `gotoAndStop("REGENERATION1")` finds
no such frame and silently leaves the icon as it was), and the deliberate
gender x part x look cross-product for the doll, of which 197 combinations
actually exist.

## What has been verified from the original

Decompiled from `SONNY1.swf` (AS2, stage 800×575, 30 fps, 330 frames). The
game's own structure, with the author's function names intact:

| System | Where it lives in the original |
| --- | --- |
| Ability table (142 entries) | `addNewMove` + `_root.hackMove[...]` patches, frame 62 |
| Unit templates (43) | `createNewUnitKrin` + `jesivie.*` patches |
| Items (126) | `createNewItemKrin` |
| Buffs/debuffs (87) | `addNewBuffKrin` + `_root.hackMove2[...]` patches |
| Damage + hit resolution | `executeMove`, `perScript` |
| Buff ticking / application | `buffTicker`, `applyBuffKrin`, `applyChangesKrin` |
| Enemy AI | `AImoveAdder` + each unit's `agressionArray` |
| XP curve | `expWorkOut` |
| Battle setup | `createNewBattle`, `krinAddNewUnit` |
| Talent tree (28 nodes) | `loadTalents`, frame 61 |
| Battle rosters (50) | `createNewBattle` + `rengi.*`, frame 62 |
| Zones and training fights | `Krin.progressArray` / `trainingArray` |
| Item drops | frame 196, rolled at battle START |
| All display text (EN + DE) | `KrinLang`, frame 61 |

Two things the port keeps deliberately separate, because the original does:
the **player** derives stats as `StatSets[i] + ceil(classBase + growth * level)`
over a piercing/defense baseline of `25 + 5 * level`, while an **enemy** uses
`base + level * growth` with no rounding and `template + 5 * level` for
piercing and defense. Using one formula for both is wrong in both directions --
and badly wrong at high levels, where enemy defense scaling dominates.

Stats are Vitality, Strength, Magic, Speed, Focus, Piercing, Defense, Health;
the eight damage elements are Physical, Magic, Ice, Fire, Lightning, Earth,
Shadow and Poison, and piercing/defense are tracked per element. Player classes
are Dreadnaught, Templar, Phantom, Phaser and Enigma.

Two details worth knowing, both reproduced rather than cleaned up:

- **Rolls come from a table, not a fresh draw.** At the start of each battle the
  game fills `KRS[0..99]` with `random(100)` and then walks it cyclically, so
  within one battle the roll sequence repeats every 100 rolls.
- **A shield exactly equal to the incoming hit does not absorb it.** The
  original's condition is `SHIELD - damage > 0`, so the equal case falls through
  to the damage branch (for zero net damage). Ported as-is.

### Porting status

- [x] Data extraction: abilities, units, items, buffs, all text → JSON → C
- [x] Damage and hit resolution (`executeMove` "Full Damage", `perScript`)
- [x] The RNG's battle-scoped ring buffer
- [x] Buffs: `applyBuffKrin` / `buffTicker` / `applyChangesKrin`
- [x] Turn order and the move queue (`TeamSelect`, `krinAddMove`,
      `MoveArrayFINAL`)
- [x] Enemy AI (`AImoveAdder`, aggression thresholds, target selection)
- [x] `Heal` and `Focus` move kinds
- [ ] Shields/reflect on the remaining executeMove paths (`REFLECT` is tracked
      but nothing reads it yet)
- [x] Items and equipment (126 items, with their stat and per-element bonuses)
- [x] The character stat pipeline: class growth, spent points, equipment
- [x] XP (`expWorkOut`), the enemy rating, level-ups and euro rewards
- [x] The ability tree (28 nodes, ranks, level gates, prerequisites, passives)
- [x] Campaign data: 50 battle rosters, 4 zones, item drops, battle dialogue
- [x] Progression: battle setup from a roster, rewards, advancement
- [x] The zone hub, with the original's own menu
- [x] Ability tree, inventory and shop screens; the victory and rewards screen
- [x] Save data (a text file rather than the original's Flash shared object)
- [x] The world map, with the original's zone-unlock rule
- [x] Battle dialogue, timed to the turn counter as the original times it
- [x] The game's own font (Tahoma, embedded in the SWF)
- [x] Unit bars, ability bar and speech box placed from the display list
- [ ] The remaining layout, against reference screenshots of the original
- [x] Asset pipeline: names resolved, art and audio extracted, manifest
- [x] Real backdrops and ability icons on screen
- [x] The character doll: per-part transforms out of the model's display list,
      with the original's own stage layout
- [x] Audio: ability sounds, per-unit hit grunts and death cries, battle music
- [x] Impact graphics played at the target
- [ ] Pixel-accurate UI layout against reference screenshots
