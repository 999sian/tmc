# buttons-xbox

Replaces the in-game **A** and **B** button-prompt tiles (the small
round icons shown in dialog boxes and HUD) with the Xbox controller's
A and B button shapes, using TMC's existing button-prompt palette so
the colours blend into the surrounding UI.

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

Layout inside that 14-tile blob:

```
tiles  0..3  = A button (16x16 in 2x2 1D mapping)
tiles  4..7  = B button
tiles  8..9  = R "R" letter (8x16, 2 tiles vertical)
tiles 10..13 = R shoulder shape (16x16)
```

This mod ships a 448-byte replacement that overrides only tiles 0..7
(A + B). Tiles 8..13 (the R prompt) are copied byte-for-byte from the
USA extraction, so R continues to render as the GBA original.

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

The override fires during the title-screen demo loop (gfx group 16
loads at title screen), so you'll see Xbox-shaped buttons in the next
dialog or HUD prompt with no save load required.

## Caveats and follow-ups

1. **Colours are TMC's button-prompt palette, not Xbox green/red.**
   The PNG → 4bpp conversion quantises against `palettes/gPalette_1.pal`
   (shared OBJ palette 1, used by 8 palette groups for HUD sprites), so
   the Xbox SHAPES are preserved but the COLOURS stay TMC's silver/blue
   prompt palette. For Xbox-accurate green/red, the mod would also need
   to ship a custom `palettes/gPalette_1.pal` — but that palette is
   shared with many other OBJ sprites, so a naive override would also
   recolour other HUD pieces.
2. **R prompt untouched.** xbox face-buttons.png only contains Y/X/A/B.
   The R prompt would come from `shoulder buttons grey.png` (53x23,
   non-tile-aligned, needs different cropping into the 8x16 + 16x16 R
   layout).
3. **Other UI tiles preserved.** Only the first 256 of the 448 bytes
   change; tiles 8..13 are bit-identical to the original.

## Rebuild procedure

```sh
# Pull TMC's button-prompt palette so the quantiser uses the same
# 16 colours as the in-game render path.
cp build/USA/assets/palettes/gPalette_1.pal /tmp/

# Crop A (bottom-left of the 2x2 face-buttons grid) and B (bottom-right)
convert "tmc buttons/xbox style/face buttons.png" \
        -crop 16x16+0+16 +repage xbox_A.png
convert "tmc buttons/xbox style/face buttons.png" \
        -crop 16x16+16+16 +repage xbox_B.png

# PNG → 4bpp, quantised against the existing palette
python3 tools/png_to_tmc4bpp.py xbox_A.png a.bin --palette /tmp/gPalette_1.pal
python3 tools/png_to_tmc4bpp.py xbox_B.png b.bin --palette /tmp/gPalette_1.pal

# Splice the new A/B into the first 8 tiles; keep tiles 8..13 (R prompt)
python3 -c "
import pathlib
orig = pathlib.Path('build/USA/assets/gfx/gfx_215e0_32x32_4bpp_uncompressed.bin').read_bytes()
a = pathlib.Path('a.bin').read_bytes()
b = pathlib.Path('b.bin').read_bytes()
mod = bytearray(orig); mod[0:128]=a; mod[128:256]=b
pathlib.Path('mods/buttons-xbox/gfx/gfx_215e0_32x32_4bpp_uncompressed.bin').write_bytes(bytes(mod))
"
```
