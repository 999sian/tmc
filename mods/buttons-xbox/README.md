# buttons-xbox

Replaces the in-game **A**, **B**, and **R** button-prompt tiles (the
small icons shown in dialog boxes and HUD) with the Xbox controller's
A, B, and RB button shapes, using TMC's existing button-prompt palette
so the colours blend into the surrounding UI.

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

This mod replaces **all 14 tiles**:

- A button (tiles 0..3) ← cropped from `face buttons.png` bottom-left (Xbox A).
- B button (tiles 4..7) ← cropped from `face buttons.png` bottom-right (Xbox B).
- R "R" column (tiles 8..9) ← left 8 px of `shoulder buttons grey.png`'s
  RB button, resized to 8x16.
- R shoulder (tiles 10..13) ← right 16 px of the same RB crop, resized to 16x16.

The two R pieces compose into a single 24x16 "RB" rounded button at the
piece offsets defined in `gFrameObjLists[505][2]`.

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
loads at startup), so you'll see Xbox-shaped buttons in the next
dialog or HUD prompt with no save load required.

## Known caveats

1. **Colours are TMC's button-prompt palette, not Xbox green/red.**
   The PNG → 4bpp conversion quantises against `palettes/gPalette_1.pal`
   (shared OBJ palette 1, used by 8 palette groups for HUD sprites), so
   the Xbox SHAPES are preserved but the COLOURS stay TMC's silver/blue
   prompt palette. For Xbox-accurate green/red, the mod would also need
   to ship a custom `palettes/gPalette_1.pal` — but that palette is
   shared with many other OBJ sprites, so a naive override would
   recolour them too.
2. **Other UI tiles preserved.** Only the 448 bytes of `gfx_215e0` are
   replaced. The 8-KB `gfx_1d7e0` blob that lives at the lower OBJ tile
   range (which holds many other HUD pieces) is untouched.

## Rebuild procedure

```sh
# 1. Pull TMC's button-prompt palette so the quantiser uses the same
#    16 colours as the in-game render path.
cp build/USA/assets/palettes/gPalette_1.pal /tmp/

# 2. Face buttons (A, B). 32x32 PNG laid out as 2x2 of 16x16 buttons:
#    Y(top-left)  X(top-right)
#    A(bot-left)  B(bot-right)
convert "tmc buttons/xbox style/face buttons.png" \
        -crop 16x16+0+16 +repage xbox_A.png
convert "tmc buttons/xbox style/face buttons.png" \
        -crop 16x16+16+16 +repage xbox_B.png
python3 tools/png_to_tmc4bpp.py xbox_A.png a.bin --palette /tmp/gPalette_1.pal
python3 tools/png_to_tmc4bpp.py xbox_B.png b.bin --palette /tmp/gPalette_1.pal

# 3. Shoulder R/L. The 53x23 PNG holds LB top-left, RB bottom-left,
#    RT and LT on the right. We crop RB (a 24x10 region), resize to
#    24x16, then split into the 8x16 left column and 16x16 right
#    block that TMC's R prompt OAM pieces expect.
convert "tmc buttons/xbox style/shoulder buttons grey.png" \
        -crop 24x10+0+12 +repage rb_raw.png
convert rb_raw.png -filter point -resize 24x16! rb_24x16.png
convert rb_24x16.png -crop  8x16+0+0  +repage rb_left8.png
convert rb_24x16.png -crop 16x16+8+0  +repage rb_right16.png
python3 tools/png_to_tmc4bpp.py rb_left8.png   rb_left.bin  --palette /tmp/gPalette_1.pal
python3 tools/png_to_tmc4bpp.py rb_right16.png rb_right.bin --palette /tmp/gPalette_1.pal

# 4. Splice all 14 tiles into the 448-byte blob:
python3 -c "
import pathlib
orig = pathlib.Path('build/USA/assets/gfx/gfx_215e0_32x32_4bpp_uncompressed.bin').read_bytes()
a  = pathlib.Path('a.bin').read_bytes()        # 128 B (4 tiles, A button)
b  = pathlib.Path('b.bin').read_bytes()        # 128 B (4 tiles, B button)
rl = pathlib.Path('rb_left.bin').read_bytes()  #  64 B (2 tiles, R col)
rr = pathlib.Path('rb_right.bin').read_bytes() # 128 B (4 tiles, R pad)
mod = bytearray(orig)
mod[0:128]   = a    # tiles 0..3
mod[128:256] = b    # tiles 4..7
mod[256:320] = rl   # tiles 8..9
mod[320:448] = rr   # tiles 10..13
pathlib.Path('mods/buttons-xbox/gfx/gfx_215e0_32x32_4bpp_uncompressed.bin').write_bytes(bytes(mod))
"
```
