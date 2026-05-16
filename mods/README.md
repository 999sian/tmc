# mods/

Sample mods for the tmc_pc Tier-1 mod loader. Each subdirectory is a
self-contained mod that overrides one or more runtime asset files.

## Available button-prompt mods

| Mod | What it replaces | Style |
|---|---|---|
| `buttons-xbox` | A/B/R in-game prompts (gfx_215e0), L/R pause-menu tabs (gfx_35cb00) | Xbox face-buttons-white + shoulder grey |
| `buttons-gba` | Same as above | GBA-style face buttons + GBA-style L/R shoulder |
| `buttons-ps-grey` | Same as above | PS face buttons greyscale (X/O for A/B) + PS L1/R1 shoulders |

All three mods override the **same two files** (`gfx/gfx_215e0_*.bin`
and `gfx/gfx_35cb00_*.bin`), so **only one button-style mod can be
active at a time**.

## How to switch styles

The mod loader auto-discovers every subdirectory of `mods/` and loads
them in alphabetical order. With more than one button-style mod
present that means the first one wins on every collision (currently
`buttons-gba` would win alphabetically). To pick a specific style use
the `TMC_MODS` env var — comma-separated, leftmost wins:

```sh
# Use Xbox style:
TMC_MODS=buttons-xbox ./build/pc/tmc_pc

# Use GBA style:
TMC_MODS=buttons-gba ./build/pc/tmc_pc

# Use PS-grey style:
TMC_MODS=buttons-ps-grey ./build/pc/tmc_pc

# Disable all mods (no TMC_MODS, all mods/ folders renamed/removed):
./build/pc/tmc_pc
```

When `TMC_MODS` is unset the loader falls back to auto-discovery
(alphabetical), so leaving multiple mods in `mods/` and not setting
the env var is non-deterministic — better to set it explicitly.

## Stderr log when a button-style mod is active

```
[MOD] active: buttons-xbox -> /…/mods/buttons-xbox
[MOD] 1 mod registered
[MOD] override gfx/gfx_215e0_32x32_4bpp_uncompressed.bin ← …   (in-game prompts, at title-screen load)
[MOD] override gfx/gfx_35cb00_64x56_4bpp_uncompressed.bin ← … (pause-menu L/R, when pause opens)
```

## How each mod was built

See the individual `README.md` inside each subdirectory for the exact
PNG cropping + 4bpp quantisation + tile splicing pipeline. All three
follow the same recipe with different source PNGs.

## Adding your own button style

1. Copy any `buttons-*/` directory as a starting point.
2. Replace the PNGs in `assets-src/` with your style.
3. Re-run the rebuild commands in that mod's `README.md` to regenerate
   the two `.bin` files.
4. Test with `TMC_MODS=your-mod-name ./build/pc/tmc_pc`.

## DMCA / copyright

The `.bin` files contain raw 4bpp tile data derived from the user's
own art (the PNGs in `assets-src/`). The mods do **not** include any
Nintendo-copyrighted graphics. The asset loader reads these files at
runtime from the user's mods/ folder; the binary ships zero modded
pixel data.
