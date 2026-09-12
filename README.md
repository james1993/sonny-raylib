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

    src/core/       pure C, no raylib: combat state machine, RNG, stats, buffs
    src/platform/   raylib: window, fixed-stage render target, input, drawing
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

### A note on Sonny 1's art

Almost none of it is bitmaps: the SWF carries 3 lossless images totalling 27 KB
against 356 `DefineShape` and 519 `DefineSprite` tags, so the characters, UI and
effects are vector art. Audio is the reverse -- 88 sounds, 8.7 MB, most of the
file. Extraction therefore has a real choice to make: rasterize the shapes to
PNG atlases (simple, plain raylib textures, loses crisp scaling), or export SVG
and rasterize at load (faithful to the original's scaling, needs a vector
rasterizer in the build). First pass takes the atlas route at 2x stage size.

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
- [ ] Zones, stages, shops, the world map and save data
- [ ] Art, audio and pixel-accurate UI layout
