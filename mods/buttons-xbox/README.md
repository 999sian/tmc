# buttons-xbox

Sample mod for the tmc_pc Tier-1 mod loader. Demonstrates an asset
override: the 32×32 graphic at `gfx/gfx_215e0_32x32_4bpp_uncompressed.bin`
is replaced with the Xbox face-buttons icon (A/B/X/Y) converted from
`assets-src/face buttons.png`.

## How to load

1. Make sure this directory sits at `<exe>/mods/buttons-xbox/` relative
   to your `tmc_pc` binary (or `dist/USA/tmc_pc`).
2. Either set `TMC_MODS=buttons-xbox` or just let auto-discovery find
   it (any directory under `<exe>/mods/` is loaded alphabetically when
   `TMC_MODS` is unset).
3. Run `tmc_pc`. You should see in the stderr log:

   ```
   [MOD] active: buttons-xbox -> /path/to/mods/buttons-xbox
   [MOD] 1 mod registered
   [MOD] override gfx/gfx_215e0_32x32_4bpp_uncompressed.bin ← .../buttons-xbox/gfx/...
   ```

## How to rebuild the .bin from the source PNG

```sh
python3 tools/png_to_tmc4bpp.py \
    mods/buttons-xbox/assets-src/face\ buttons.png \
    mods/buttons-xbox/gfx/gfx_215e0_32x32_4bpp_uncompressed.bin
```

The converter writes a sibling `*.pal.bin` containing the 16-colour
palette it picked. TMC ignores this file (palettes are loaded from
separate palette-group assets) — it's only there if you want to verify
the colour quantisation looked sane.

## Adding more overrides

Place files at the same relative path the runtime asset loader uses.
The path conventions match the `assets/` tree the extractor produces:

```
mods/<modname>/
  gfx/gfx_*.bin
  palettes/gPalette_*.pal
  tilemaps/tilemap_*.bin
  animations/<symbol>.bin
  sprites/<symbol>/*.4bpp
  room_properties/<symbol>.bin
  area_*.json
  texts.json
```

Any file present in the mod takes priority over the equivalent pak
entry or loose file. First mod (in `TMC_MODS` order) wins on collision.
