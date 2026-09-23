# Hand-made art and sound

`assets/art/` is rebuilt from the original by `tools/build_assets.py`, so art
made by hand lives here instead, with a `manifest.json` in the same form as
`assets/art/manifest.json`:

    {"assets": {
        "My Sword": {"category": "item",
                     "frames": ["assets/extra/my_sword.png"],
                     "scale": 2, "offsets": [[17, 17]]}
    }}

- The name is what the engine asks for: an item's `name`, an ability's
  `model`, a buff's `key`, a `#character` for a piece of the interface.
- `frames` are paths from the top of the repository, one per frame.
- `scale` is pixels of art per stage unit; the extracted art is drawn at 2.
- `offsets` is where the art's own origin sits inside each frame, in stage
  units -- what the engine lines up with the point it draws at.

An entry here is added to the extracted ones, or stands in for one of the
same name. `make data` picks it up; `tools/check_assets.py` checks the files
are there and that nothing is left on the disk that no manifest names.
