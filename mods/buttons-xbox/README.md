# buttons-xbox

Replaces the in-game **A** and **B** button-prompt tiles with the Xbox
controller's green-A and red-B icons.

## How it works

The button prompts that the game shows in dialog boxes ("Press A to
talk", "B to cancel", "R for action") are rendered by `ButtonUIElement`
in `src/ui.c`. Each uses `DrawDirect(spriteIndex=505, frameIndex=…)`:

| UI element       | sprite | frame | OAM piece                          |
|------------------|:------:|:-----:|------------------------------------|
| `UI_ELEMENT_BUTTON_A` | 505 | 0     | 16x16 piece at VRAM tile `0x0`, pal 1 |
| `UI_ELEMENT_BUTTON_B` | 505 | 1     | 16x16 piece at VRAM tile `0x4`, pal 1 |
| `UI_ELEMENT_BUTTON_R` | 505 | 2     | 8x16 + 16x16 pieces at tiles `0x8`/`0xa`, pal 1 |

Those tiles live in OBJ VRAM starting at tile 0, and OBJ VRAM tile 0 is
populated by `gfx group 89 → gfx_1d7e0_128x128_4bpp_uncompressed.bin`
(dest 0x06010000, 8192 bytes). The first 14 tiles (A, B, R) sit at the
start of this 256-tile blob; the remaining 242 tiles are other UI/HUD
graphics.

This mod ships a modified copy of that 8192-byte blob with **only tiles
0..7 replaced** (Xbox A and B icons, 16x16 each). Tiles 8..255 are
copied byte-for-byte from the original ROM extraction.

## How to load

1. Make sure this directory sits at `<exe>/mods/buttons-xbox/`.
2. Either set `TMC_MODS=buttons-xbox` or just leave it under `mods/` —
   auto-discovery loads all subdirectories of `mods/` alphabetically.
3. Run the game and load a save. When you next see an A or B prompt
   (interacting with an NPC, dialog box), it should appear as the
   Xbox-style icon. The stderr log will show:

   ```
   [MOD] override gfx/gfx_1d7e0_128x128_4bpp_uncompressed.bin ←
         .../buttons-xbox/gfx/gfx_1d7e0_128x128_4bpp_uncompressed.bin (8192 bytes)
   ```

## Known limitations

- **Palette unchanged.** TMC's OBJ palette 1 controls these tiles' colours;
  the converter picked a 16-colour palette from the source PNG but TMC
  applies its own palette at render time. The button **shapes** will be
  Xbox-style; the **colours** will be TMC's original A/B-prompt palette
  (green/yellow/dark). For colour-accurate Xbox icons we'd also need to
  override the matching `palettes/gPalette_NN.pal` file.
- **R button left alone.** The xbox face-buttons PNG only has A/B/X/Y;
  TMC's R prompt is a shoulder-button shape we'd source from
  `xbox style/shoulder buttons grey.png` separately.
- **Other UI tiles preserved.** Only tiles 0..7 of `gfx_1d7e0` are
  rewritten; tiles 8..255 (R prompt and other HUD pieces) are unchanged.

## How the .bin was built

```sh
# 1. crop A and B (16x16 each) out of the 32x32 face-buttons.png
convert "tmc buttons/xbox style/face buttons.png" \
        -crop 16x16+0+16 +repage xbox_A.png
convert "tmc buttons/xbox style/face buttons.png" \
        -crop 16x16+16+16 +repage xbox_B.png

# 2. PNG → 4bpp tile blobs (128 bytes each = 4 tiles)
python3 tools/png_to_tmc4bpp.py xbox_A.png xbox_A_tiles.bin
python3 tools/png_to_tmc4bpp.py xbox_B.png xbox_B_tiles.bin

# 3. splice: replace first 256 bytes of the original 8192-byte blob
#    with [xbox_A_tiles, xbox_B_tiles].  Keep tiles 8..255 intact.
python3 -c "
import pathlib
orig = pathlib.Path('original_gfx_1d7e0.bin').read_bytes()
a = pathlib.Path('xbox_A_tiles.bin').read_bytes()
b = pathlib.Path('xbox_B_tiles.bin').read_bytes()
mod = bytearray(orig); mod[0:128]=a; mod[128:256]=b
pathlib.Path('gfx_1d7e0_128x128_4bpp_uncompressed.bin').write_bytes(bytes(mod))
"
```
