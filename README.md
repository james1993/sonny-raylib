# Sonny (2008) — reimplementation in C + raylib

A from-scratch reimplementation of the combat/RPG systems of *Sonny*, the first
game in the Sonny Legacy Collection. The goal is behavioural exactness: the same
stats, formulas, ability effects, enemy AI and turn resolution, so a given fight
plays out the way it does in the original.

## Status

Engine shell only. It builds and runs, and the combat core is under test, but
every number in it is a placeholder until real game data is extracted.

- `make test` — headless tests of the combat core (no raylib needed)
- `make game` — the raylib front end
- `SONNY_SHOT=out.png ./build/sonny` — render one frame and exit (works under
  `xvfb-run`, so visuals can be diffed against reference screenshots in CI)

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

- [x] Data extraction: abilities, units, items, buffs, all text → JSON
- [x] Damage and hit resolution (`executeMove` "Full Damage", `perScript`),
      verified against an independent transcription on 400 generated cases
- [x] The RNG's battle-scoped ring buffer
- [ ] Buffs: `applyBuffKrin` / `buffTicker` / `applyChangesKrin`
- [ ] Turn order and the move queue (`krinAddMove`, `MoveArrayFINAL`)
- [ ] Enemy AI (`AImoveAdder`, aggression thresholds)
- [ ] Non-damage move kinds (`Heal`, `Focus`, and the rest of `executeMove`)
- [ ] Level-ups, ability trees, equipment, shops, zones, save data
- [ ] Art, audio and UI layout
