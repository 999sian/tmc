/**
 * port_text_render.c — C implementations of ARM assembly text rendering functions
 *
 * These were originally in asm/src/code_08001A7C.s (Thumb).
 * They handle unpacking 4bpp font data and rendering glyph pixels into tile buffers.
 */

#include "global.h"

/**
 * UnpackTextNibbles — Expand 4bpp packed font data into per-pixel bytes.
 *
 * Reads 16 rows × 4 bytes = 64 bytes of packed 4bpp data (8 pixels wide, 16 rows).
 * Outputs 16 rows × 8 bytes = 128 bytes, one nibble per byte.
 *
 * Original: 0x08002724
 */
void UnpackTextNibbles(const u8* src, u8* dest) {
    for (int i = 0; i < 16; i++) {
        u8 b0 = src[0];
        u8 b1 = src[1];
        u8 b2 = src[2];
        u8 b3 = src[3];
        dest[0] = b0 & 0xF;
        dest[1] = b0 >> 4;
        dest[2] = b1 & 0xF;
        dest[3] = b1 >> 4;
        dest[4] = b2 & 0xF;
        dest[5] = b2 >> 4;
        dest[6] = b3 & 0xF;
        dest[7] = b3 >> 4;
        src += 4;
        dest += 8;
    }
}

/**
 * sub_080026C4 — Render one pixel column of a glyph into a 4bpp tile buffer.
 *
 * Writes 16 rows of a single pixel column. Uses a color LUT to map pixel
 * indices to 4bpp nibble values.
 *
 * @param src       Unpacked pixel data (128 bytes, 8 per row). Points to the
 *                  specific column within the unpacked data.
 * @param dest      Output tile buffer (e.g., gTextGfxBuffer). Organized as
 *                  tile pairs (64 bytes each = top 8x8 + bottom 8x8 tile).
 * @param colorLUT  32-byte color lookup table. Entries 0-15 produce low-nibble
 *                  colors; entries 16-31 produce high-nibble colors.
 * @param col       Pixel column index in the output buffer. Determines:
 *                  - Tile pair: col / 8 (each pair = 64 bytes)
 *                  - Byte within row: (col / 2) & 3
 *                  - Nibble: even col → low nibble, odd col → high nibble
 *
 * Original: 0x080026C4
 */
void sub_080026C4(const u8* src, u8* dest, const u8* colorLUT, u32 col) {
    /* Select tile pair (64 bytes each) */
    u32 tileOffset = (col >> 3) << 6;
    dest += tileOffset;

    /* Default: even pixel → low nibble, preserve high nibble */
    u8 mask = 0xF0;
    u32 byteOffset = (col >> 1) & 3;

    /* Odd pixel → high nibble, use second half of LUT */
    if (col & 1) {
        mask = 0x0F;
        colorLUT += 0x10;
    }

    dest += byteOffset;

    /* Write 16 rows (each row = 4 bytes in 4bpp tile, so stride = 4) */
    for (int i = 0; i < 16; i++) {
        u8 pixel = src[0];
        u8 color = colorLUT[pixel];
        u8 existing = dest[0];
        existing &= mask;
        existing |= color;
        dest[0] = existing;
        dest += 4;  /* Next row in tile (4 bytes per row in 4bpp) */
        src += 8;   /* Next row in unpacked data (8 bytes per row) */
    }
}

/**
 * sub_080026F2 — Same as sub_080026C4 but with transparency.
 *
 * Skips writing if the color-mapped value is 0 (transparent).
 *
 * Original: 0x080026F2
 */
void sub_080026F2(const u8* src, u8* dest, const u8* colorLUT, u32 col) {
    u32 tileOffset = (col >> 3) << 6;
    dest += tileOffset;

    u8 mask = 0xF0;
    u32 byteOffset = (col >> 1) & 3;

    if (col & 1) {
        mask = 0x0F;
        colorLUT += 0x10;
    }

    dest += byteOffset;

    for (int i = 0; i < 16; i++) {
        u8 pixel = src[0];
        u8 color = colorLUT[pixel];
        if (color != 0) {
            u8 existing = dest[0];
            existing &= mask;
            existing |= color;
            dest[0] = existing;
        }
        dest += 4;
        src += 8;
    }
}
