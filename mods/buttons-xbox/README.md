# buttons-xbox

Replaces the in-game **A**, **B**, and **R** button-prompt tiles with
Xbox-style letterforms. **No palette override** — the tiles quantise
against TMC's existing `gPalette_1.pal`, so dialog text and other HUD
sprites that share OBJ palette 1 remain visually unchanged.

The mod uses:

- `face buttons white.png` for the A and B letterforms (bare letters,
  no surrounding circle — clean minimal look).
- `shoulder buttons grey.png` for the RB rounded-rectangle prompt.

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
tiles  0..3  = A letterform   (16x16 in 2x2 1D mapping)
tiles  4..7  = B letterform
tiles  8..9  = R "R" letter column   (8x16, 2 tiles vertical)
tiles 10..13 = R shoulder shape       (16x16)
```

## Colour mapping (quantised against original `gPalette_1`)

The source PNGs' bright Xbox colours don't have exact matches in TMC's
silver/blue prompt palette, so the converter picks the nearest slot:

| Source PNG colour                | Maps to (slot) | Visible result          |
|----------------------------------|:--------------:|-------------------------|
| White outline of A/B/RB          | 14 (`#ffffff`) | White outline           |
| Green A letter (`#3cdb4e`)       | 11 (`#738b94`) | Gray-blue letter strokes |
| Red B letter (`#c03a3a`)         | 7  (`#ac6220`) | Orange-brown letter strokes |
| RB outline dark blue-grey        | 10 (`#52626a`) | Same dark blue-grey      |
| RB fill (light blue-grey)        | 12 (`#a4b4c5`) | Light gray-blue           |
| Transparent background           | 0              | Transparent              |

The result: white-outlined A/B/RB silhouettes with each letter drawn
in its closest TMC palette colour. Visually distinct from the original
GBA prompts (which were filled circles), and importantly does **not**
disturb the dialog text font that uses the same palette.

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
loads at startup), so the new buttons are visible as soon as any
prompt is drawn.

## Caveats

1. **Colours are not Xbox green/red.** TMC's `gPalette_1` has no
   bright green or red — closest matches are gray-blue and orange.
   Getting true Xbox colours requires overriding `gPalette_1.pal`
   itself, which **also** retints dialog text and other HUD sprites
   that share the palette. That trade-off was tried in commit
   `da18f890` and reverted in `a54cc7c8` because the text recolour was
   too disruptive. A clean Xbox-colour version would need a per-caller
   palette-scope feature in the asset loader.

2. **R prompt is RB-shaped.** Composed from the user's
   `shoulder buttons grey.png` RB region, resized 24x10 → 24x16, then
   split into 8x16 + 16x16 to match TMC's two-piece OAM layout.

3. **Other UI tiles preserved.** Only the 448 bytes of `gfx_215e0`
   change; the rest of the asset tree is untouched.

## Rebuild procedure

```sh
# 1. Pull TMC's original palette so the quantiser uses the same
#    16 colours as the in-game render path. THE MOD MUST NOT SHIP A
#    PALETTE FILE — only the gfx tiles, quantised against this.
cp build/USA/assets/palettes/gPalette_1.pal /tmp/

# 2. White-style face buttons (A green-on-white, B red-on-white).
#    32x32 PNG laid out as 2x2 of 16x16 buttons:
#      Y(top-left)  X(top-right)
#      A(bot-left)  B(bot-right)
convert "tmc buttons/xbox style/face buttons white.png" \
        -crop 16x16+0+16  +repage /tmp/xbox_Aw.png
convert "tmc buttons/xbox style/face buttons white.png" \
        -crop 16x16+16+16 +repage /tmp/xbox_Bw.png
python3 tools/png_to_tmc4bpp.py /tmp/xbox_Aw.png /tmp/Aw.bin --palette /tmp/gPalette_1.pal
python3 tools/png_to_tmc4bpp.py /tmp/xbox_Bw.png /tmp/Bw.bin --palette /tmp/gPalette_1.pal

# 3. Shoulder R/L. 53x23 PNG: LB top-left, RB bottom-left, RT/LT right.
#    Crop RB (a 24x10 region), resize to 24x16, split into 8x16 + 16x16.
convert "tmc buttons/xbox style/shoulder buttons grey.png" \
        -crop 24x10+0+12 +repage /tmp/rb_raw.png
convert /tmp/rb_raw.png -filter point -resize 24x16! /tmp/rb_24x16.png
convert /tmp/rb_24x16.png -crop  8x16+0+0 +repage /tmp/rb_left8.png
convert /tmp/rb_24x16.png -crop 16x16+8+0 +repage /tmp/rb_right16.png
python3 tools/png_to_tmc4bpp.py /tmp/rb_left8.png   /tmp/rl.bin --palette /tmp/gPalette_1.pal
python3 tools/png_to_tmc4bpp.py /tmp/rb_right16.png /tmp/rr.bin --palette /tmp/gPalette_1.pal

# 4. Splice all 14 tiles into the 448-byte blob:
python3 -c "
import pathlib
orig = pathlib.Path('build/USA/assets/gfx/gfx_215e0_32x32_4bpp_uncompressed.bin').read_bytes()
a  = pathlib.Path('/tmp/Aw.bin').read_bytes()
b  = pathlib.Path('/tmp/Bw.bin').read_bytes()
rl = pathlib.Path('/tmp/rl.bin').read_bytes()
rr = pathlib.Path('/tmp/rr.bin').read_bytes()
mod = bytearray(orig)
mod[0:128]=a; mod[128:256]=b; mod[256:320]=rl; mod[320:448]=rr
pathlib.Path('mods/buttons-xbox/gfx/gfx_215e0_32x32_4bpp_uncompressed.bin').write_bytes(bytes(mod))
"
```
