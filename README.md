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
    data/           ability/enemy/zone tables (JSON) — the source of truth
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
3. Art and audio are extracted from the same `.swf` **locally** into `assets/`,
   which is gitignored. No original content is committed to this repository.

Game mechanics and numbers are reimplemented; original art, audio and text are
not redistributed here. Point the build at your own copy's assets.
