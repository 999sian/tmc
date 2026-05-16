# buttons-xbox

Replaces the in-game **A**, **B**, and **R** button-prompt tiles with
Xbox controller equivalents. **Shape only — colours stay TMC's silver
prompt palette.** The palette-override approach that gave true Xbox
green/red caused collateral on dialog text (text glyphs share OBJ
palette 1 with the prompts), so the palette change has been backed out
in favour of leaving the text alone.

## How it works

`ButtonUIElement` in `src/ui.c` draws each prompt via
`DrawDirect(spriteIndex=505, frameIndex=…)`. The OAM piece data is:

| UI element            | sprite | frame | OAM piece                       |
|-----------------------|:------:|:-----:|---------------------------------|
| `UI_ELEMENT_BUTTON_A` | 505    | 0     | 16x16 piece, tile=0x0, palette 1 |
| `UI_ELEMENT_BUTTON_B` | 505    | 1     | 16x16 piece, tile=0x4, palette 1 |
| `UI_ELEMENT_BUTTON_R` | 505    | 2     | 8x16 piece tile=0x8 + 16x16 piece tile=0xa, palette 1 |

`port_draw.c` adds a per-element `extra` of `0x0100` to every OAM tile
index, so `tile=0x0` actually means **OBJ tile 256**. OBJ tile 256 is
`0x06012000`, where `gfx group 16` loads
`gfx/gfx_215e0_32x32_4bpp_uncompressed.bin` (448 bytes = 14 tiles).

Tile layout in that 14-tile blob:

```
tiles  0..3  = A button (16x16 in 2x2 1D mapping)
tiles  4..7  = B button
tiles  8..9  = R "R" letter column (8x16, 2 tiles vertical)
tiles 10..13 = R shoulder shape (16x16)
```

The mod replaces all 14 tiles with Xbox shapes, each quantised against
the **original** `gPalette_1.pal` so existing text/HUD sprites that
also use this palette remain visually unchanged.

## How to load

Drop this folder at `<exe>/mods/buttons-xbox/`. The mod loader auto-
discovers any subdirectory of `mods/` (or set `TMC_MODS=buttons-xbox`
explicitly).

Stderr log when active:

```
[MOD] active: buttons-xbox -> /…/mods/buttons-xbox
[MOD] 1 mod registered
[MOD] override gfx/gfx_215e0_32x32_4bpp_uncompressed.bin ← …
```

## Caveats and follow-ups

1. **Colours stay TMC's button-prompt palette, not Xbox green/red.**
   The 4bpp tiles index into a 16-colour palette that is **shared with
   the dialog text font and other HUD sprites**. A previous version of
   this mod (da18f890, reverted) overrode `palettes/gPalette_1.pal` to
   get true Xbox green/red, but the change tinted dialog text the same
   green — too disruptive. The shapes-only version preserves text
   colours.

   To revisit colour-accurate Xbox prompts cleanly, the only fully-safe
   path is per-context: ship a custom palette file mapped to only the
   palette-group entries that the button-prompt UI uses, while
   preserving the dialog-text-loading entries. This needs a small
   loader-side change because the current asset loader keys on
   `relative-path` only — there's no way to scope an override to "load
   N for this caller only".

2. **R prompt is RB-shaped.** The 8x16 + 16x16 piece layout was used to
   compose a 24x16 "RB" rounded rectangle from the user's
   `shoulder buttons grey.png`. If you'd prefer the GBA-style R prompt
   for those, delete this mod or revert tiles 8..13.

3. **Other UI tiles preserved.** Only the 448 bytes of `gfx_215e0` are
   replaced; the rest of the asset tree is untouched.

## Rebuild procedure

```sh
# 1. Pull TMC's existing palette so the quantiser uses the same 16
#    colours as the in-game render path. THE MOD MUST NOT SHIP A
#    PALETTE FILE — only the gfx tiles, quantised against this.
cp build/USA/assets/palettes/gPalette_1.pal /tmp/

# 2. Face buttons: 32x32 PNG laid out as 2x2 of 16x16 buttons.
#    Y(top-left)  X(top-right)
#    A(bot-left)  B(bot-right)
convert "tmc buttons/xbox style/face buttons.png" -crop 16x16+0+16  +repage /tmp/xbox_A.png
convert "tmc buttons/xbox style/face buttons.png" -crop 16x16+16+16 +repage /tmp/xbox_B.png
python3 tools/png_to_tmc4bpp.py /tmp/xbox_A.png /tmp/a.bin --palette /tmp/gPalette_1.pal
python3 tools/png_to_tmc4bpp.py /tmp/xbox_B.png /tmp/b.bin --palette /tmp/gPalette_1.pal

# 3. Shoulder R: crop RB (24x10), resize to 24x16, split 8x16 + 16x16.
convert "tmc buttons/xbox style/shoulder buttons grey.png" \
        -crop 24x10+0+12 +repage /tmp/rb_raw.png
convert /tmp/rb_raw.png -filter point -resize 24x16! /tmp/rb_24x16.png
convert /tmp/rb_24x16.png -crop  8x16+0+0 +repage /tmp/rb_left8.png
convert /tmp/rb_24x16.png -crop 16x16+8+0 +repage /tmp/rb_right16.png
python3 tools/png_to_tmc4bpp.py /tmp/rb_left8.png   /tmp/rl.bin --palette /tmp/gPalette_1.pal
python3 tools/png_to_tmc4bpp.py /tmp/rb_right16.png /tmp/rr.bin --palette /tmp/gPalette_1.pal

# 4. Splice all 14 tiles into the 448-byte blob (tiles 0..7 = A,B; 8..13 = R).
python3 -c "
import pathlib
orig = pathlib.Path('build/USA/assets/gfx/gfx_215e0_32x32_4bpp_uncompressed.bin').read_bytes()
a  = pathlib.Path('/tmp/a.bin').read_bytes()
b  = pathlib.Path('/tmp/b.bin').read_bytes()
rl = pathlib.Path('/tmp/rl.bin').read_bytes()
rr = pathlib.Path('/tmp/rr.bin').read_bytes()
mod = bytearray(orig)
mod[0:128]=a; mod[128:256]=b; mod[256:320]=rl; mod[320:448]=rr
pathlib.Path('mods/buttons-xbox/gfx/gfx_215e0_32x32_4bpp_uncompressed.bin').write_bytes(bytes(mod))
"
```
