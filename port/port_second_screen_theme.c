#include "port_second_screen_theme.h"

#include "port_rom.h"
#include "region.h"
#include "structures.h"

#include <string.h>

/*
 * Decode map (all runtime, zero baked pixels — see header):
 *
 *   window chrome   gUnk_081092AC[0] border shapes + gUnk_081094CE color
 *                   LUT + gUnk_0810926C fill patterns — the exact pipeline
 *                   sub_0805F918 (src/text.c) runs to build the message
 *                   window's 7 tiles, replayed into private RGBA buffers.
 *                   Tile arrangement comes from DispMessageFrame
 *                   (src/message.c) — corner + edge tiles, flips mirroring.
 *   hearts/rupee/   gfx group 16 (the HUD tile load InitUI performs to BG0
 *   key icons       char base 3, src/ui.c) via Port_ResolveGfxGroupVram;
 *                   tile ids from DrawHearts/DrawRupees/DrawKeys.
 *   digit fonts     gUnk_085C4620 — the same blob RenderDigits/sub_0801C2F0
 *                   DMA into VRAM: +0x000 small ammo digits (tens/ones
 *                   tiles), +0x280 white 8x16 counters, +0x500 yellow
 *                   (maxed) variants.
 *   palette         palette group 12 (HUD bank 15 — MESSAGE_PALETTE), the
 *                   one palette the HUD/message tiles above are drawn with.
 *   A/B buttons     sprite 505 frames 0/1 (gUIElementDefinitions), pieces
 *                   from gFrameObjLists via sub_080AD8F0, tiles from the
 *                   sprite sheet like the HUD's own element DMA.
 *   equip cursor    pause-menu sprite 0x1FB (0x1FA on EU) frames 4/5 — the
 *                   blinking gold slot frame of the Items screen. Its
 *                   pieces address OBJ VRAM absolutely, so tiles resolve
 *                   through gfx group 90 (the Items screen's OBJ loads).
 */

/* src/common.c (appended accessors — ROM-const reads only). */
extern const u8* Port_GetRawPaletteGroupData(u32 group, u32* outNumColors);
extern const u8* Port_GetRawPaletteGroupEntryData(u32 group, u32 entryIdx, u32* outNumColors,
                                                  u32* outDestPaletteNum);
extern const u8* Port_ResolveGfxGroupVram(u32 group, u32 vramAddr, u32* outAvail);

/* src/affine.c — frame OBJ piece list for (sprite, frame); PC path is
 * bounds-checked and returns NULL when out of range. */
extern void* sub_080AD8F0(u32 sprite, u32 frame);

/* port/port_text_render.c — 64 packed bytes -> 128 pixel bytes (8x16). */
extern void UnpackTextNibbles(void* src_ptr, u8* dest);

/* ROM-const text/border tables, resolved from the loaded ROM at boot
 * (port_rom.c). Same externs src/text.c uses. */
extern void* gUnk_081092AC[]; /* border shape data per border_type */
extern u8 gUnk_081094CE[];    /* color LUTs, 0xC0 bytes per fill_type */
extern u32 gUnk_0810926C[];   /* window fill patterns, u32 each */

/* HUD digit gfx blob (data_const_stubs.c / ROM) — same symbol src/ui.c
 * reads at runtime for every region. */
extern const u8 gUnk_085C4620[];

#define HUD_GFX_GROUP 16u
#define HUD_PALETTE_GROUP 12u       /* -> BG bank 15, MESSAGE_PALETTE */
#define OBJ_PALETTE_GROUP 11u       /* -> OBJ banks 0..4 (gameplay set) */
#define PAUSE_PALETTE_GROUP 182u    /* -> BG 13/14 + OBJ banks 5..10 */
#define PAUSE_OBJ_GFX_GROUP 90u     /* Items screen OBJ tile loads */
#define HUD_BG_CHARBASE 0x0600C000u /* BG0 char base 3 (control 0x1f0c) */
#define OBJ_VRAM_BASE 0x06010000u

/* HUD tile ids (BG0 char base 3) — from DrawHearts / DrawRupees /
 * DrawKeys in src/ui.c and gWalletSizes in src/itemUtils.c. */
#define TILE_HEART_FULL 0x11 /* +1..+3 = quarter fills, 0x15 = empty */
#define TILE_HEART_EMPTY 0x15
#define TILE_KEY 0x1C       /* 2x2 */
#define TILE_RUPEE_W0 0x60  /* 2x2, +4 per wallet tier */

/* Sprite indices. */
#define SPRITE_HUD_BUTTONS 505u
#define SPRITE_PAUSE_MISC (REGION_IS_EU ? 0x1FAu : 0x1FBu)
#define CURSOR_FRAME_0 4u
#define CURSOR_FRAME_1 5u

static int sBuilt = 0;

/* All cached element pixels live in one arena so failure cleanup is just
 * "leave the slot NULL" — no per-element allocation bookkeeping. Sized
 * with headroom for the largest composites (~160 KB static, one-time). */
static uint32_t sArena[40000];
static int32_t sArenaUsed = 0;

static SecondScreenThemeSprite sSprites[SST_COUNT];
static uint32_t sColors[SSC_COUNT];

/* Window chrome: 6 border tiles in sub_0805F918's strip order plus the
 * solid fill tile — index meanings per src/message.c's tile enum. */
enum { CHROME_CORNER, CHROME_H_CORNER, CHROME_H_STRAIGHT, CHROME_V_CORNER, CHROME_V_STRAIGHT, CHROME_CURSOR,
       CHROME_FILL, CHROME_TILE_COUNT };
static uint32_t sChromeTiles[CHROME_TILE_COUNT][64];
static int sChromeOk = 0;

static uint32_t Rgb555ToRgba8888(uint16_t c) {
    uint8_t r = (uint8_t)((c & 0x1Fu) << 3);
    uint8_t g = (uint8_t)(((c >> 5) & 0x1Fu) << 3);
    uint8_t b = (uint8_t)(((c >> 10) & 0x1Fu) << 3);
    return 0xFF000000u | ((uint32_t)b << 16) | ((uint32_t)g << 8) | (uint32_t)r;
}

static uint32_t* ArenaAlloc(int32_t count) {
    uint32_t* p;
    if (count <= 0 || sArenaUsed + count > (int32_t)(sizeof(sArena) / sizeof(sArena[0]))) {
        return NULL;
    }
    p = &sArena[sArenaUsed];
    sArenaUsed += count;
    memset(p, 0, (size_t)count * 4u);
    return p;
}

/* One 4bpp GBA tile (32 bytes) -> 64 RGBA pixels; color 0 stays 0
 * (transparent), matching how these tiles sit over the backdrop. */
static void DecodeTile4bpp(const u8* tile, const uint16_t* pal16, u32 numColors, uint32_t* out64) {
    int32_t py, px;
    for (py = 0; py < 8; py++) {
        for (px = 0; px < 8; px++) {
            u8 packed = tile[py * 4 + px / 2];
            u8 idx = (px & 1) ? (u8)(packed >> 4) : (u8)(packed & 0x0Fu);
            out64[py * 8 + px] = (idx == 0 || idx >= numColors) ? 0u : Rgb555ToRgba8888(pal16[idx]);
        }
    }
}

static const u8* HudTileBytes(u32 tile, u32 tileCount) {
    u32 avail = 0;
    const u8* p = Port_ResolveGfxGroupVram(HUD_GFX_GROUP, HUD_BG_CHARBASE + tile * 32u, &avail);
    if (p == NULL || avail < tileCount * 32u) {
        return NULL;
    }
    return p;
}

/* Decodes `tileCount` consecutive tiles as a 2-tiles-wide icon block
 * (row1: t, t+1 / row2: t+2, t+3 — the exact cell order DrawRupees and
 * DrawKeys write into the tilemap). */
static const uint32_t* DecodeIcon2x2(const u8* tiles, const uint16_t* pal16, u32 numColors) {
    uint32_t* out = ArenaAlloc(16 * 16);
    uint32_t tmp[64];
    int32_t t, py, px;
    if (out == NULL) {
        return NULL;
    }
    for (t = 0; t < 4; t++) {
        int32_t ox = (t & 1) * 8;
        int32_t oy = (t >> 1) * 8;
        DecodeTile4bpp(tiles + t * 32, pal16, numColors, tmp);
        for (py = 0; py < 8; py++) {
            for (px = 0; px < 8; px++) {
                out[(oy + py) * 16 + ox + px] = tmp[py * 8 + px];
            }
        }
    }
    return out;
}

/* -------------------------------------------------------------------- */
/*  Window chrome (message-frame pipeline replay)                        */
/* -------------------------------------------------------------------- */

static void BuildChrome(const uint16_t* hudPal, u32 hudColors) {
    /* fill_type 0 — the plain in-game dialog window's color scheme. */
    const u8* lut = &gUnk_081094CE[0]; /* even-column half: plain indexes */
    u8 fillHead = gUnk_081094CE[0x0A]; /* logical offset 0xAA via the split head/tail tables */
    u32 fillIdx = gUnk_0810926C[fillHead & 0x3F] & 0xFu;
    u8 unpacked[128];
    int32_t border, block, py, px;

    sChromeOk = 0;

    /* border_type 0 is the standard dialog frame; if its data is missing
     * or decodes to nothing visible, try the next few types before giving
     * up — all of them are the game's own window frames. */
    for (border = 0; border < 4 && !sChromeOk; border++) {
        const u8* shapes = (const u8*)gUnk_081092AC[border];
        int32_t opaque = 0;
        if (shapes == NULL) {
            continue;
        }
        /* Three 0x40-byte blocks, each an 8x16 strip = two stacked tiles:
         * (corner, h-corner), (h-straight, v-corner), (v-straight, cursor). */
        for (block = 0; block < 3; block++) {
            UnpackTextNibbles((void*)(shapes + block * 0x40), unpacked);
            for (py = 0; py < 16; py++) {
                for (px = 0; px < 8; px++) {
                    u32 colorIdx = lut[unpacked[py * 8 + px] & 0x0Fu] & 0x0Fu;
                    uint32_t rgba = (colorIdx == 0 || colorIdx >= hudColors)
                                        ? 0u
                                        : Rgb555ToRgba8888(hudPal[colorIdx]);
                    sChromeTiles[block * 2 + (py >> 3)][(py & 7) * 8 + px] = rgba;
                    if (block == 0 && py < 8 && rgba != 0) {
                        opaque++; /* corner-tile visibility check */
                    }
                }
            }
        }
        sChromeOk = opaque >= 8;
    }

    /* Interior fill tile: the MemFill32 base sub_0805F918 leaves untouched
     * in the strip's 7th tile (MSG_BACKGROUND). */
    {
        uint32_t fill = (fillIdx != 0 && fillIdx < hudColors) ? Rgb555ToRgba8888(hudPal[fillIdx])
                                                              : sColors[SSC_WINDOW_FILL];
        for (py = 0; py < 64; py++) {
            sChromeTiles[CHROME_FILL][py] = fill;
        }
        sColors[SSC_WINDOW_FILL] = fill;
    }
}

/* -------------------------------------------------------------------- */
/*  Sprite-frame composites (buttons, equip cursor)                      */
/* -------------------------------------------------------------------- */

/* Standard GBA OBJ dimensions by (shape, size) — hardware constants. */
static const u8 kObjW[3][4] = { { 8, 16, 32, 64 }, { 16, 32, 32, 64 }, { 8, 8, 16, 32 } };
static const u8 kObjH[3][4] = { { 8, 16, 32, 64 }, { 8, 8, 16, 32 }, { 16, 32, 32, 64 } };

/* OBJ palette bank -> raw RGB555 colors, resolved through the palette
 * groups gameplay/pause keep loaded there (group 11: banks 0-4, group
 * 182's chained entries: banks 5+). */
static const uint16_t* ObjPalBank(u32 bank) {
    static const u32 kGroups[2] = { OBJ_PALETTE_GROUP, PAUSE_PALETTE_GROUP };
    u32 g, e;
    for (g = 0; g < 2; g++) {
        for (e = 0; e < 4; e++) {
            u32 numColors = 0, dest = 0;
            const u8* p = Port_GetRawPaletteGroupEntryData(kGroups[g], e, &numColors, &dest);
            if (p == NULL) {
                break;
            }
            if (dest >= 16) {
                u32 bankLo = dest - 16;
                u32 bankCount = numColors / 16;
                if (bank >= bankLo && bank < bankLo + bankCount) {
                    return (const uint16_t*)p + (bank - bankLo) * 16;
                }
            }
        }
    }
    return NULL;
}

/* Composites one sprite frame (all its OBJ pieces) into a bbox-cropped
 * RGBA buffer. Piece format is RenderSpritePieces' (port_draw.c): count
 * byte, then 5 bytes per piece {s8 x, s8 y, u8 shape/size/flip, u8 tile
 * low, u8 tile high (tile bits 8-9 + palette in the top nibble)}. Pieces
 * are drawn in reverse so the first (topmost OAM) piece wins overlaps.
 * objVramGroup == 0: tiles come from the sprite sheet (frames[] +
 * firstTileIndex, the UI-element DMA path); nonzero: tile indexes are
 * absolute OBJ VRAM tiles resolved through that gfx group's OBJ loads
 * (the DrawDirect path the pause menu uses). */
static int BuildSpriteComposite(u32 spriteIdx, u32 frameIdx, u32 objVramGroup, int outId) {
    const u8* frameData = (const u8*)sub_080AD8F0(spriteIdx, frameIdx);
    const SpritePtr* sp = NULL;
    const SpriteFrame* frame = NULL;
    u32 count;
    int32_t minX = 0x7FFF, minY = 0x7FFF, maxX = -0x7FFF, maxY = -0x7FFF;
    int32_t w, h, i;
    uint32_t* out;

    if (frameData == NULL) {
        return 0;
    }
    count = frameData[0];
    if (count == 0 || count > 16) {
        return 0;
    }

    if (objVramGroup == 0) {
        sp = Port_GetSpritePtr((u16)spriteIdx);
        if (sp == NULL || sp->frames == NULL || sp->ptr == NULL) {
            return 0;
        }
        frame = &sp->frames[frameIdx];
    }

    for (i = 0; i < (int32_t)count; i++) {
        const u8* p = frameData + 1 + i * 5;
        u32 shape = (p[2] >> 6) & 3;
        u32 size = (p[2] >> 4) & 3;
        int32_t px = (s8)p[0];
        int32_t py = (s8)p[1];
        if (shape == 3) {
            return 0;
        }
        if (px < minX) minX = px;
        if (py < minY) minY = py;
        if (px + kObjW[shape][size] > maxX) maxX = px + kObjW[shape][size];
        if (py + kObjH[shape][size] > maxY) maxY = py + kObjH[shape][size];
    }
    w = maxX - minX;
    h = maxY - minY;
    if (w <= 0 || h <= 0 || w > 96 || h > 96) {
        return 0;
    }
    out = ArenaAlloc(w * h);
    if (out == NULL) {
        return 0;
    }

    /* All-or-nothing: a composite missing pieces would read as broken
     * art, worse than the caller's clean fallback — so any unresolvable
     * piece rejects the whole sprite (the arena bump is a bounded
     * one-time cost; nothing partial is ever published). */
    for (i = (int32_t)count - 1; i >= 0; i--) {
        const u8* p = frameData + 1 + i * 5;
        u32 shape = (p[2] >> 6) & 3;
        u32 size = (p[2] >> 4) & 3;
        int hflip = (p[2] & 0x04) != 0;
        int vflip = (p[2] & 0x08) != 0;
        u32 tileIdx = (u32)p[3] | ((u32)(p[4] & 3) << 8);
        u32 palBank = (u32)p[4] >> 4;
        int32_t px = (int32_t)(s8)p[0] - minX;
        int32_t py = (int32_t)(s8)p[1] - minY;
        int32_t pw = kObjW[shape][size];
        int32_t ph = kObjH[shape][size];
        int32_t wTiles = pw / 8, hTiles = ph / 8;
        const uint16_t* pal = ObjPalBank(palBank);
        const u8* tiles;
        int32_t tx, ty, yy, xx;

        if (pal == NULL) {
            return 0;
        }
        if (objVramGroup != 0) {
            /* Absolute OBJ tiles: the piece can address any block the
             * menu keeps loaded — the screen's own OBJ load first, then
             * the always-loaded gameplay sets (LoadGfxGroups' 23/16). */
            const u32 kVramGroups[3] = { objVramGroup, 23u, HUD_GFX_GROUP };
            u32 avail = 0;
            u32 g;
            tiles = NULL;
            for (g = 0; g < 3 && tiles == NULL; g++) {
                tiles = Port_ResolveGfxGroupVram(kVramGroups[g], OBJ_VRAM_BASE + tileIdx * 32u, &avail);
                if (tiles != NULL && avail < (u32)(wTiles * hTiles) * 32u) {
                    tiles = NULL;
                }
            }
            if (tiles == NULL) {
                return 0;
            }
        } else {
            tiles = (const u8*)sp->ptr + ((u32)frame->firstTileIndex + tileIdx) * 32u;
        }

        /* 1D OBJ mapping (the game runs with DISPCNT bit 6 set): a
         * piece's tiles are consecutive, row-major across its width. */
        for (ty = 0; ty < hTiles; ty++) {
            for (tx = 0; tx < wTiles; tx++) {
                const u8* tile = tiles + (ty * wTiles + tx) * 32;
                for (yy = 0; yy < 8; yy++) {
                    for (xx = 0; xx < 8; xx++) {
                        u8 packed = tile[yy * 4 + xx / 2];
                        u8 idx = (xx & 1) ? (u8)(packed >> 4) : (u8)(packed & 0x0Fu);
                        int32_t dx, dy;
                        if (idx == 0) {
                            continue;
                        }
                        dx = tx * 8 + xx;
                        dy = ty * 8 + yy;
                        if (hflip) dx = pw - 1 - dx;
                        if (vflip) dy = ph - 1 - dy;
                        out[(py + dy) * w + px + dx] = Rgb555ToRgba8888(pal[idx]);
                    }
                }
            }
        }
    }

    sSprites[outId].px = out;
    sSprites[outId].w = w;
    sSprites[outId].h = h;
    return 1;
}

/* -------------------------------------------------------------------- */
/*  Derived colors                                                       */
/* -------------------------------------------------------------------- */

static u32 Luma(uint32_t c) {
    return 2 * (c & 0xFF) + 5 * ((c >> 8) & 0xFF) + ((c >> 16) & 0xFF);
}

static uint32_t BrightestPixel(const SecondScreenThemeSprite* s, uint32_t fallback) {
    uint32_t best = fallback;
    u32 bestLuma = 0;
    int32_t i;
    if (s == NULL || s->px == NULL) {
        return fallback;
    }
    for (i = 0; i < s->w * s->h; i++) {
        if (s->px[i] != 0 && Luma(s->px[i]) > bestLuma) {
            bestLuma = Luma(s->px[i]);
            best = s->px[i];
        }
    }
    return best;
}

static uint32_t CenterPixel(const SecondScreenThemeSprite* s, uint32_t fallback) {
    if (s == NULL || s->px == NULL) {
        return fallback;
    }
    {
        uint32_t c = s->px[(s->h / 2) * s->w + s->w / 2];
        return c != 0 ? c : fallback;
    }
}

static void DeriveColors(void) {
    int32_t i;
    /* Border light/dark from the corner tile's real colors — used by the
     * procedural bits (cell plates, fallback frames) so they share the
     * window's own palette instead of guessing hues. */
    if (sChromeOk) {
        uint32_t light = sColors[SSC_BORDER_LIGHT], dark = sColors[SSC_BORDER_DARK];
        u32 lightLuma = 0, darkLuma = 0xFFFFFFFFu;
        for (i = 0; i < 64; i++) {
            uint32_t c = sChromeTiles[CHROME_CORNER][i];
            if (c == 0) {
                continue;
            }
            if (Luma(c) > lightLuma) {
                lightLuma = Luma(c);
                light = c;
            }
            if (Luma(c) < darkLuma) {
                darkLuma = Luma(c);
                dark = c;
            }
        }
        sColors[SSC_BORDER_LIGHT] = light;
        sColors[SSC_BORDER_DARK] = dark;
    }
    sColors[SSC_GOLD] = BrightestPixel(&sSprites[SST_KEY], sColors[SSC_GOLD]);
    sColors[SSC_HEART_RED] = CenterPixel(&sSprites[SST_HEART_FULL], sColors[SSC_HEART_RED]);
    sColors[SSC_RUPEE_GREEN] = CenterPixel(&sSprites[SST_RUPEE_WALLET0], sColors[SSC_RUPEE_GREEN]);
    sColors[SSC_TEXT_LIGHT] = BrightestPixel(&sSprites[SST_DIGIT_WHITE_0], sColors[SSC_TEXT_LIGHT]);
}

/* -------------------------------------------------------------------- */
/*  Build                                                                */
/* -------------------------------------------------------------------- */

static void BuildAll(const uint16_t* hudPal, u32 hudColors) {
    int32_t i;

    BuildChrome(hudPal, hudColors);

    /* Hearts: full (0x11), quarter fills (0x12..0x14), empty (0x15). */
    for (i = 0; i < 5; i++) {
        static const int kIds[5] = { SST_HEART_FULL, SST_HEART_Q1, SST_HEART_Q2, SST_HEART_Q3, SST_HEART_EMPTY };
        static const u32 kTiles[5] = { TILE_HEART_FULL, TILE_HEART_FULL + 1, TILE_HEART_FULL + 2,
                                       TILE_HEART_FULL + 3, TILE_HEART_EMPTY };
        const u8* t = HudTileBytes(kTiles[i], 1);
        uint32_t* out = t ? ArenaAlloc(64) : NULL;
        if (out != NULL) {
            DecodeTile4bpp(t, hudPal, hudColors, out);
            sSprites[kIds[i]].px = out;
            sSprites[kIds[i]].w = 8;
            sSprites[kIds[i]].h = 8;
        }
    }

    /* Rupee icons per wallet tier + the small-key icon (2x2 tiles). */
    for (i = 0; i < 4; i++) {
        const u8* t = HudTileBytes(TILE_RUPEE_W0 + (u32)i * 4u, 4);
        const uint32_t* out = t ? DecodeIcon2x2(t, hudPal, hudColors) : NULL;
        if (out != NULL) {
            sSprites[SST_RUPEE_WALLET0 + i].px = out;
            sSprites[SST_RUPEE_WALLET0 + i].w = 16;
            sSprites[SST_RUPEE_WALLET0 + i].h = 16;
        }
    }
    {
        const u8* t = HudTileBytes(TILE_KEY, 4);
        const uint32_t* out = t ? DecodeIcon2x2(t, hudPal, hudColors) : NULL;
        if (out != NULL) {
            sSprites[SST_KEY].px = out;
            sSprites[SST_KEY].w = 16;
            sSprites[SST_KEY].h = 16;
        }
    }

    /* Counter digits, 8x16 (two stacked tiles per glyph), white + yellow. */
    for (i = 0; i < 20; i++) {
        const u8* glyph = gUnk_085C4620 + ((i < 10) ? 0x280u : 0x500u) + (u32)(i % 10) * 0x40u;
        uint32_t* out = ArenaAlloc(8 * 16);
        if (out != NULL) {
            DecodeTile4bpp(glyph, hudPal, hudColors, out);
            DecodeTile4bpp(glyph + 32, hudPal, hudColors, out + 64);
            sSprites[SST_DIGIT_WHITE_0 + i].px = out;
            sSprites[SST_DIGIT_WHITE_0 + i].w = 8;
            sSprites[SST_DIGIT_WHITE_0 + i].h = 16;
        }
    }

    /* Ammo-count digits, 8x8: tens glyphs [0..9], ones glyphs [10..19]. */
    for (i = 0; i < 20; i++) {
        uint32_t* out = ArenaAlloc(64);
        if (out != NULL) {
            DecodeTile4bpp(gUnk_085C4620 + (u32)i * 32u, hudPal, hudColors, out);
            sSprites[SST_SMALL_TENS_0 + i].px = out;
            sSprites[SST_SMALL_TENS_0 + i].w = 8;
            sSprites[SST_SMALL_TENS_0 + i].h = 8;
        }
    }

    /* HUD A/B button bubbles + the pause menu's blinking equip cursor. */
    BuildSpriteComposite(SPRITE_HUD_BUTTONS, 0, 0, SST_BUTTON_A);
    BuildSpriteComposite(SPRITE_HUD_BUTTONS, 1, 0, SST_BUTTON_B);
    BuildSpriteComposite(SPRITE_PAUSE_MISC, CURSOR_FRAME_0, PAUSE_OBJ_GFX_GROUP, SST_CURSOR_0);
    BuildSpriteComposite(SPRITE_PAUSE_MISC, CURSOR_FRAME_1, PAUSE_OBJ_GFX_GROUP, SST_CURSOR_1);

    DeriveColors();
}

int Port_SecondScreenTheme_Ready(void) {
    u32 hudColors = 0;
    const u8* hudPal;

    if (sBuilt) {
        return 1;
    }
    /* Neutral stand-ins until the ROM tables resolve; overwritten by
     * DeriveColors. These are design placeholders of ours, not game data. */
    sColors[SSC_WINDOW_FILL] = 0xFF282420u;
    sColors[SSC_BORDER_LIGHT] = 0xFF78B4C8u;
    sColors[SSC_BORDER_DARK] = 0xFF101820u;
    sColors[SSC_GOLD] = 0xFF40C8E8u;
    sColors[SSC_HEART_RED] = 0xFF3030E8u;
    sColors[SSC_RUPEE_GREEN] = 0xFF58C848u;
    sColors[SSC_TEXT_LIGHT] = 0xFFF0F0F0u;

    /* The one ingredient everything needs: the HUD/message palette. If
     * it (or the HUD tiles) aren't resolved yet the ROM isn't ready —
     * report not-ready and retry on a later frame. Callers only ask
     * during gameplay, so in practice this succeeds on the first call. */
    hudPal = Port_GetRawPaletteGroupData(HUD_PALETTE_GROUP, &hudColors);
    if (hudPal == NULL || hudColors < 16 || HudTileBytes(TILE_HEART_FULL, 1) == NULL) {
        return 0;
    }

    BuildAll((const uint16_t*)hudPal, hudColors > 16 ? 16 : hudColors);
    sBuilt = 1;
    return 1;
}

const SecondScreenThemeSprite* Port_SecondScreenTheme_Get(int id) {
    if (!sBuilt || id < 0 || id >= SST_COUNT || sSprites[id].px == NULL) {
        return NULL;
    }
    return &sSprites[id];
}

uint32_t Port_SecondScreenTheme_Color(int id) {
    if (id < 0 || id >= SSC_COUNT) {
        return 0xFF000000u;
    }
    return sColors[id];
}

/* -------------------------------------------------------------------- */
/*  Window drawing                                                       */
/* -------------------------------------------------------------------- */

static void FillRectTheme(uint32_t* px, int32_t bufW, int32_t bufH, int32_t stride, int32_t x0, int32_t y0,
                          int32_t x1, int32_t y1, uint32_t color) {
    int32_t x, y;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > bufW) x1 = bufW;
    if (y1 > bufH) y1 = bufH;
    for (y = y0; y < y1; y++) {
        uint32_t* row = px + (size_t)y * (size_t)stride;
        for (x = x0; x < x1; x++) {
            row[x] = color;
        }
    }
}

/* Nearest-neighbor integer-scale blit of one chrome tile with optional
 * mirroring, clipped to [cx0,cy0)x(cx1,cy1) so edge runs can end on a
 * partial tile without ever scaling the art non-integrally. */
static void BlitChromeTile(uint32_t* px, int32_t bufW, int32_t bufH, int32_t stride, int tile, int32_t x,
                           int32_t y, int32_t ts, int hflip, int vflip, int32_t cx0, int32_t cy0, int32_t cx1,
                           int32_t cy1) {
    const uint32_t* src = sChromeTiles[tile];
    int32_t sx, sy, ex, ey;
    if (cx0 < 0) cx0 = 0;
    if (cy0 < 0) cy0 = 0;
    if (cx1 > bufW) cx1 = bufW;
    if (cy1 > bufH) cy1 = bufH;
    for (sy = 0; sy < 8; sy++) {
        int32_t srcY = vflip ? 7 - sy : sy;
        for (sx = 0; sx < 8; sx++) {
            int32_t srcX = hflip ? 7 - sx : sx;
            uint32_t c = src[srcY * 8 + srcX];
            if (c == 0) {
                continue;
            }
            for (ey = 0; ey < ts; ey++) {
                int32_t dy = y + sy * ts + ey;
                if (dy < cy0 || dy >= cy1) {
                    continue;
                }
                for (ex = 0; ex < ts; ex++) {
                    int32_t dx = x + sx * ts + ex;
                    if (dx < cx0 || dx >= cx1) {
                        continue;
                    }
                    px[(size_t)dy * (size_t)stride + dx] = c;
                }
            }
        }
    }
}

void Port_SecondScreenTheme_DrawWindow(uint32_t* pixels, int32_t bufW, int32_t bufH, int32_t stride,
                                       int32_t x, int32_t y, int32_t w, int32_t h, int32_t tileScale) {
    int32_t t, cx;
    if (tileScale < 1) {
        tileScale = 1;
    }
    t = 8 * tileScale;

    if (!sChromeOk || w < 3 * t || h < 3 * t) {
        /* Frame fallback in the window's own (or neutral) colors. */
        uint32_t dark = sColors[SSC_BORDER_DARK], light = sColors[SSC_BORDER_LIGHT];
        FillRectTheme(pixels, bufW, bufH, stride, x, y, x + w, y + h, sColors[SSC_WINDOW_FILL]);
        FillRectTheme(pixels, bufW, bufH, stride, x, y, x + w, y + tileScale, light);
        FillRectTheme(pixels, bufW, bufH, stride, x, y + h - tileScale, x + w, y + h, light);
        FillRectTheme(pixels, bufW, bufH, stride, x, y, x + tileScale, y + h, light);
        FillRectTheme(pixels, bufW, bufH, stride, x + w - tileScale, y, x + w, y + h, light);
        FillRectTheme(pixels, bufW, bufH, stride, x + tileScale, y + tileScale, x + w - tileScale,
                      y + h - tileScale, sColors[SSC_WINDOW_FILL]);
        (void)dark;
        return;
    }

    /* Interior: extend the fill halfway under the border ring so the
     * border art's inner pixels always meet fill, never the backdrop
     * (the corner tiles' outer rounding stays in the untouched half). */
    FillRectTheme(pixels, bufW, bufH, stride, x + t / 2, y + t / 2, x + w - t / 2, y + h - t / 2,
                  sColors[SSC_WINDOW_FILL]);

    /* DispMessageFrame's arrangement: a corner tile plus its adjacent
     * edge tile at each end, straight tiles between (the last straight is
     * clipped, never rescaled), the far ends mirrored via flips exactly
     * like the tilemap's flip flags. */
    /* Top and bottom rows. */
    {
        int32_t xr = x + w - 2 * t; /* mirrored right-end pair starts here */
        BlitChromeTile(pixels, bufW, bufH, stride, CHROME_CORNER, x, y, tileScale, 0, 0, 0, 0, bufW, bufH);
        BlitChromeTile(pixels, bufW, bufH, stride, CHROME_CORNER, x + w - t, y, tileScale, 1, 0, 0, 0, bufW, bufH);
        BlitChromeTile(pixels, bufW, bufH, stride, CHROME_CORNER, x, y + h - t, tileScale, 0, 1, 0, 0, bufW, bufH);
        BlitChromeTile(pixels, bufW, bufH, stride, CHROME_CORNER, x + w - t, y + h - t, tileScale, 1, 1, 0, 0,
                       bufW, bufH);
        BlitChromeTile(pixels, bufW, bufH, stride, CHROME_H_CORNER, x + t, y, tileScale, 0, 0, 0, 0, bufW, bufH);
        BlitChromeTile(pixels, bufW, bufH, stride, CHROME_H_CORNER, xr, y, tileScale, 1, 0, 0, 0, bufW, bufH);
        BlitChromeTile(pixels, bufW, bufH, stride, CHROME_H_CORNER, x + t, y + h - t, tileScale, 0, 1, 0, 0, bufW,
                       bufH);
        BlitChromeTile(pixels, bufW, bufH, stride, CHROME_H_CORNER, xr, y + h - t, tileScale, 1, 1, 0, 0, bufW,
                       bufH);
        for (cx = x + 2 * t; cx < xr; cx += t) {
            BlitChromeTile(pixels, bufW, bufH, stride, CHROME_H_STRAIGHT, cx, y, tileScale, 0, 0, 0, 0, xr, bufH);
            BlitChromeTile(pixels, bufW, bufH, stride, CHROME_H_STRAIGHT, cx, y + h - t, tileScale, 0, 1, 0, 0,
                           xr, bufH);
        }
    }
    /* Left and right columns. */
    {
        int32_t yb = y + h - 2 * t; /* mirrored bottom-end pair starts here */
        int32_t cy;
        BlitChromeTile(pixels, bufW, bufH, stride, CHROME_V_CORNER, x, y + t, tileScale, 0, 0, 0, 0, bufW, bufH);
        BlitChromeTile(pixels, bufW, bufH, stride, CHROME_V_CORNER, x + w - t, y + t, tileScale, 1, 0, 0, 0, bufW,
                       bufH);
        BlitChromeTile(pixels, bufW, bufH, stride, CHROME_V_CORNER, x, yb, tileScale, 0, 1, 0, 0, bufW, bufH);
        BlitChromeTile(pixels, bufW, bufH, stride, CHROME_V_CORNER, x + w - t, yb, tileScale, 1, 1, 0, 0, bufW,
                       bufH);
        for (cy = y + 2 * t; cy < yb; cy += t) {
            BlitChromeTile(pixels, bufW, bufH, stride, CHROME_V_STRAIGHT, x, cy, tileScale, 0, 0, 0, 0, bufW, yb);
            BlitChromeTile(pixels, bufW, bufH, stride, CHROME_V_STRAIGHT, x + w - t, cy, tileScale, 1, 0, 0, 0,
                           bufW, yb);
        }
    }
}
