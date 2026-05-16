# buttons-xbox

Replaces the in-game **A**, **B**, and **R** button-prompt tiles with
Xbox controller equivalents, **in their Xbox colours** (green A, red B,
grey RB). Ships both an updated `gfx/gfx_215e0_*.bin` (the actual button-
prompt 4bpp tiles) and an updated `palettes/gPalette_1.pal` so the
button-prompt OBJ palette renders with Xbox green/red instead of TMC's
silver/blue.

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

## Palette layout

The 4bpp tiles index into `palettes/gPalette_1.pal` (OBJ palette 1).
That palette is loaded into the same OBJ slot by 8 different palette
groups (9, 10, 11, 181, 203, 204, 205, 207), so any HUD sprite that
also uses palette 1 sees the same colours.

The mod **only changes slots 10..14**; slots 0..9 and 15 stay at their
original values to minimise collateral damage to other HUD sprites:

| Slot | Original (#hex) | Modded (#hex) | Purpose                |
|:----:|:---------------:|:-------------:|------------------------|
|  0..9 | (preserved)    | (preserved)   | Used by other HUD sprites; left untouched |
|  10  | `#52626a`       | **`#506068`** | RB outline (matches PNG exactly) |
|  11  | `#738b94`       | **`#b4b4bd`** | RB letter / light grey |
|  12  | `#a4b4c5`       | **`#43a847`** | Xbox A bright green    |
|  13  | `#d5deee`       | **`#cc3838`** | Xbox B bright red      |
|  14  | `#ffffff`       | **`#1e5621`** | Dark green (A shadow, currently unused — quantiser picked black instead) |
|  15  | `#000000`       | `#000000`     | Universal black outline — KEPT |

The quantiser then maps the source PNG pixels to the nearest of those
16 slots: Xbox A pixels land on slot 12 (green), B pixels on slot 13
(red), RB outline on slot 10 (exact match), RB highlight on slot 5
(original light cyan — close to off-white).

## Known side-effects

Any other OBJ sprite that uses palette 1 slots 10..14 will also pick up
the new colours:

- Slot 12 was a **light blue-grey** (used for highlights of metallic
  HUD sprites). Now bright green. Affected sprites will look greener.
- Slot 13 was **very light blue** (highlight). Now bright red.
- Slot 14 was **pure white** (brightest highlight). Now dark green —
  sprites that used white highlights lose them, becoming dark green.

These side-effects only show on HUD/UI sprites loaded with palette 1
into OBJ palette slot 1. Other gameplay sprites (entities, enemies,
NPCs) use different palette groups and are unaffected.

## How to load

Drop this folder at `<exe>/mods/buttons-xbox/`. The mod loader auto-
discovers any subdirectory of `mods/` (or set `TMC_MODS=buttons-xbox`
explicitly).

Stderr log when active:

```
[MOD] active: buttons-xbox -> /…/mods/buttons-xbox
[MOD] 1 mod registered
[MOD] override gfx/gfx_215e0_32x32_4bpp_uncompressed.bin ← …
[MOD] override palettes/gPalette_1.pal ← …
```

The gfx override fires at the title-screen demo loop (gfx group 16 is
always loaded). The palette override fires later, when a save is
loaded and the HUD/UI palette gets requested by palette groups 9/10/11
or similar.

## Rebuild procedure

```sh
# 1. Pull TMC's original gPalette_1.pal — we keep slots 0..9 and 15.
cp build/USA/assets/palettes/gPalette_1.pal /tmp/orig_pal1.pal

# 2. Build the modded palette (Python one-liner; see commit message for
#    the slot allocation rationale).
python3 - <<PY
import struct, pathlib
def rgb555(r,g,b): return ((r>>3)&0x1f) | (((g>>3)&0x1f)<<5) | (((b>>3)&0x1f)<<10)
new = bytearray(pathlib.Path('/tmp/orig_pal1.pal').read_bytes())
for idx,(r,g,b) in {10:(0x50,0x60,0x68), 11:(0xb4,0xb4,0xbd),
                    12:(0x43,0xa8,0x47), 13:(0xcc,0x38,0x38),
                    14:(0x1e,0x56,0x21)}.items():
    new[idx*2:idx*2+2] = struct.pack('<H', rgb555(r,g,b))
pathlib.Path('mods/buttons-xbox/palettes/gPalette_1.pal').write_bytes(bytes(new))
pathlib.Path('/tmp/xbox_pal1.pal').write_bytes(bytes(new))
PY

# 3. Crop A, B, RB and convert each against the new palette
convert "tmc buttons/xbox style/face buttons.png" -crop 16x16+0+16 +repage /tmp/xbox_A.png
convert "tmc buttons/xbox style/face buttons.png" -crop 16x16+16+16 +repage /tmp/xbox_B.png
convert "tmc buttons/xbox style/shoulder buttons grey.png" -crop 24x10+0+12 +repage /tmp/rb_raw.png
convert /tmp/rb_raw.png -filter point -resize 24x16! /tmp/rb_24x16.png
convert /tmp/rb_24x16.png -crop 8x16+0+0  +repage /tmp/rb_left8.png
convert /tmp/rb_24x16.png -crop 16x16+8+0 +repage /tmp/rb_right16.png

python3 tools/png_to_tmc4bpp.py /tmp/xbox_A.png    /tmp/a.bin  --palette /tmp/xbox_pal1.pal
python3 tools/png_to_tmc4bpp.py /tmp/xbox_B.png    /tmp/b.bin  --palette /tmp/xbox_pal1.pal
python3 tools/png_to_tmc4bpp.py /tmp/rb_left8.png  /tmp/rl.bin --palette /tmp/xbox_pal1.pal
python3 tools/png_to_tmc4bpp.py /tmp/rb_right16.png /tmp/rr.bin --palette /tmp/xbox_pal1.pal

# 4. Splice into the 448-byte blob
python3 - <<PY
import pathlib
orig = pathlib.Path('build/USA/assets/gfx/gfx_215e0_32x32_4bpp_uncompressed.bin').read_bytes()
a  = pathlib.Path('/tmp/a.bin').read_bytes()
b  = pathlib.Path('/tmp/b.bin').read_bytes()
rl = pathlib.Path('/tmp/rl.bin').read_bytes()
rr = pathlib.Path('/tmp/rr.bin').read_bytes()
mod = bytearray(orig)
mod[0:128]=a; mod[128:256]=b; mod[256:320]=rl; mod[320:448]=rr
pathlib.Path('mods/buttons-xbox/gfx/gfx_215e0_32x32_4bpp_uncompressed.bin').write_bytes(bytes(mod))
PY
```
