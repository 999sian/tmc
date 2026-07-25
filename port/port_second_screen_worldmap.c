#include "port_second_screen_worldmap.h"

#include "port_asset_loader.h" /* Port_IsRoomHeaderPtrReadable */
#include "port_rom.h"          /* gRomData/gRomSize, Port_ReadU16/U32 */
#include "area.h"              /* AreaHeader/gAreaMetadata, RoomHeader/gAreaRoomHeaders, Transition */

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

/*
 * Hyrule world-map image for the second screen, decoded at runtime from the
 * same ROM data the game's own map screen loads — never shipped as baked
 * pixels, same policy as port_second_screen_render.c's item icons.
 *
 * What "the game's own map screen" concretely is: subtaskMapHint.c and the
 * pause menu's Map tab both run pause-menu screen 4, whose row in
 * gUnk_08128A38 selects gUnk_08128AD8[2] = { paletteGroup 185, gfxGroup 94,
 * dispcnt bits, bg1Control 0x1C03, bg2Control 0x1D0B } (src/data/
 * figurineMenuData.c, applied by sub_080A4DB8 in src/menu/figurineMenu.c).
 * Reading those control words plus the setup path gives the full recipe:
 *
 *   BG1 (map artwork):  4bpp tiles at charBase 0 (0x06000000, gfx group 94's
 *                       16 KB entry) + the 32x20 tilemap group 94 copies into
 *                       gBG1Buffer (0x02021F30). BGVOFS -4 (sub_080A6290).
 *   BG2 (frame/chrome): tiles at charBase 2 (0x06008000, from gfx group 86 —
 *                       sub_080A4D34's LoadGfxGroup(0x56 + health state); the
 *                       three variants differ only in the BG3 map below, we
 *                       use the full-health group) + group 94's gBG2Buffer
 *                       tilemap (0x020344B0). BGVOFS -4.
 *   BG3 (backdrop):     same charBase 2 tiles + the tilemap group 86 copies
 *                       into gBG3Buffer (0x02001A40). No scroll.
 *   Palettes:           the game reaches this screen through LoadGfxGroups()
 *                       (palette groups 11, 12), sub_080A4D34 (group 0xB5 =
 *                       181) and sub_080A4DB8 (group 185); later loads win,
 *                       exactly like the shared gPaletteBuffer. Backdrop
 *                       color = entry 81, per Subtask_MapHint_0's
 *                       SetColor(0, gPaletteBuffer[81]).
 *
 * Layers are composed back-to-front (BG3, BG2, BG1 — all priority 3, lower
 * BG number wins on the GBA) into one 240x160 RGBA8888 replica of the map
 * screen, so the game's own marker math lands on this image with no extra
 * transform.
 *
 * Threading: built lazily, ONCE, by whoever calls GetImage first — by design
 * that is only the second-screen render thread. The image is written into a
 * private static buffer and only then published through a single aligned
 * pointer-sized store (sPublishedImage); it is immutable afterwards. Today
 * builder and consumer are the same thread, so no cross-thread ordering is
 * exercised at all; if another reader thread is ever added it observes
 * either NULL or a pointer to a buffer whose writes precede the store in
 * program order — add a release/acquire pair then. ROM readiness is probed
 * per call and every failure path returns "not ready" instead of crashing,
 * so calling early (before Port_LoadRom resolved gGfxGroups/gPaletteGroups)
 * just keeps the caller on its schematic fallback for another frame.
 *
 * This module never reads live engine state (gSave/gArea/gRoomControls/
 * gPaletteBuffer/VRAM) — only ROM-constant data via the port's resolved
 * tables; anything save- or room-dependent arrives as caller parameters.
 */

/* Resolved ROM pointer tables (port/port_rom.c). Declared with the defining
 * types: entries point at raw little-endian GfxItem (12-byte) / PaletteGroup
 * (4-byte) records inside gRomData. */
extern const void* gGfxGroups[];
extern const void* gPaletteGroups[];
extern const u8* gGlobalGfxAndPalettes;

/* Windcrest warp targets — the compiled table the fast-travel screen indexes
 * by windcrest bit (src/menu/kinstoneMenu.c; used by sub_080A6EE0 in
 * src/subtask/subtaskFastTravel.c). */
extern const Transition gUnk_08128024[];

/* port/port_bios.c — BIOS LZ77 into a caller-owned buffer (the engine's
 * LZ77UnCompWram/Vram always resolve their destination into live GBA
 * regions, which an off-screen decode must not touch). */
extern void Port_LZ77DecompressToBuffer(const void* src, void* dst, size_t dstCap);

#define WORLDMAP_IMAGE_W 240 /* GBA LCD; menu screens always render the native canvas */
#define WORLDMAP_IMAGE_H 160

/* gfx groups (see file comment for the derivation) */
#define WORLDMAP_GFX_GROUP 94u /* map tiles + BG1/BG2 tilemaps (pause screen 4) */
#define CHROME_GFX_GROUP 86u   /* menu chrome tiles + BG3 tilemap (full-health variant) */

/* GfxItem destinations that identify the entries we need within those
 * groups. VRAM addresses are fixed by the BG control words on every region;
 * the EWRAM ones are the USA link addresses of the tilemap staging buffers
 * (include/vram.h) — EU/JP retail links may place those buffers elsewhere,
 * so EWRAM entries are ALSO matched positionally (nth EWRAM-destined record
 * of the group) as a region-agnostic fallback. */
#define DEST_MAP_TILES 0x06000000u    /* BG charBase 0 (bg1Control 0x1C03) */
#define DEST_CHROME_TILES 0x06008000u /* BG charBase 2 (bg2Control 0x1D0B, bg3 0x1E0B) */
#define DEST_BG1_TILEMAP 0x02021F30u  /* gBG1Buffer — group 94's 1st EWRAM entry */
#define DEST_BG2_TILEMAP 0x020344B0u  /* gBG2Buffer — group 94's 2nd EWRAM entry */
#define DEST_BG3_TILEMAP 0x02001A40u  /* gBG3Buffer — group 86's 1st EWRAM entry */

/* Palette groups applied on the way into the map screen, in load order. */
static const u8 sWorldMapPaletteGroups[] = { 11, 12, 181, 185 };

/* sub_080A6290 sets bg1/bg2 BGVOFS to -4: the artwork sits 4 px lower on
 * screen than in its tilemap. BG3 is not scrolled. */
#define MAP_LAYER_YSHIFT 4

#define GFX_GROUP_WALK_MAX 32 /* sanity cap, real groups have <8 records */
#define PALETTE_GROUP_WALK_MAX 16
#define LZ77_DECODED_CAP 0x20000u /* 128 KB — far above any BG tile/map blob */

static uint32_t sImagePixels[WORLDMAP_IMAGE_W * WORLDMAP_IMAGE_H];
static const uint32_t* volatile sPublishedImage = NULL;

typedef struct {
    int32_t x, y;
} WorldMapPin;
static WorldMapPin sWindcrestPins[8];
static int sWindcrestPinsReady = 0;

/* One fetched gfx-group entry: `data` points either into gRomData or at an
 * owned decompression buffer (owned != NULL, free after composing). */
typedef struct {
    const u8* data;
    u32 len;
    u8* owned;
} GfxBlob;

static uint32_t Rgb555ToRgba8888(uint16_t c) {
    uint8_t r = (uint8_t)((c & 0x1Fu) << 3);
    uint8_t g = (uint8_t)(((c >> 5) & 0x1Fu) << 3);
    uint8_t b = (uint8_t)(((c >> 10) & 0x1Fu) << 3);
    return 0xFF000000u | ((uint32_t)b << 16) | ((uint32_t)g << 8) | (uint32_t)r;
}

static int RangeInRom(const u8* p, u32 len) {
    if (gRomData == NULL || p == NULL) {
        return 0;
    }
    return p >= gRomData && len <= gRomSize && p + len <= gRomData + gRomSize;
}

/* Hand back one gfx-group record's payload. Mirrors LoadGfxGroup's walk
 * (src/common.c:513): 12-byte records { u32 offset|ctrl<<24, u32 dest,
 * s32 size }, chained while bit 31 of the first word is set; ctrl 0xD ends
 * the group, ctrl 0x7 means always-load, other values are language-
 * conditional. Records are selected by exact dest address, or — when
 * ewramOrdinal >= 0 — as the nth record targeting EWRAM (0x02xxxxxx),
 * whatever its exact address; wantDest is ignored then. size < 0 marks BIOS
 * LZ77 data (decoded into an owned buffer); size >= 0 is returned as a
 * direct pointer into ROM. Returns 0 when not found/ready. */
static int FetchGfxGroupEntry(u32 group, u32 wantDest, int ewramOrdinal, int allowAnyCtrl, GfxBlob* out) {
    const u8* rec;
    int i;
    int ewramSeen = 0;

    out->data = NULL;
    out->len = 0;
    out->owned = NULL;

    if (group >= 133u) { /* GFX_GROUPS_COUNT_MAX (port_rom.c) */
        return 0;
    }
    rec = (const u8*)gGfxGroups[group];
    if (rec == NULL) {
        return 0;
    }

    for (i = 0; i < GFX_GROUP_WALK_MAX; i++, rec += 12) {
        u32 raw0, ctrl, dest;
        int match;
        if (!RangeInRom(rec, 12)) {
            return 0;
        }
        raw0 = Port_ReadU32(rec);
        ctrl = (raw0 >> 24) & 0xF;
        if (ctrl == 0xD) {
            return 0; /* explicit end-of-group record */
        }
        if (ctrl == 0x7 || allowAnyCtrl) {
            dest = Port_ReadU32(rec + 4);
            if (ewramOrdinal >= 0) {
                match = (dest >> 24) == 0x02u && ewramSeen++ == ewramOrdinal;
            } else {
                match = dest == wantDest;
            }
            if (match) {
                const u8* src = gGlobalGfxAndPalettes + (raw0 & 0xFFFFFF);
                s32 size = (s32)Port_ReadU32(rec + 8);
                if (size >= 0) {
                    if (!RangeInRom(src, (u32)size)) {
                        return 0;
                    }
                    out->data = src;
                    out->len = (u32)size;
                    return 1;
                }
                /* LZ77: real payload length lives in the stream's own header
                 * (byte 0 = type 0x10, bytes 1-3 = decompressed size). */
                {
                    u32 header, decodedLen;
                    if (!RangeInRom(src, 4)) {
                        return 0;
                    }
                    header = Port_ReadU32(src);
                    decodedLen = header >> 8;
                    if ((header & 0xF0u) != 0x10u || decodedLen == 0 || decodedLen > LZ77_DECODED_CAP) {
                        return 0;
                    }
                    out->owned = (u8*)calloc(1, decodedLen);
                    if (out->owned == NULL) {
                        return 0;
                    }
                    Port_LZ77DecompressToBuffer(src, out->owned, decodedLen);
                    out->data = out->owned;
                    out->len = decodedLen;
                    return 1;
                }
            }
        }
        if (((raw0 >> 24) & 0x80) == 0) {
            return 0; /* last record, dest never matched */
        }
    }
    return 0;
}

/* Selection ladder per blob: unconditional (ctrl 7) record at the exact USA
 * dest first; then the first language variant at that dest (this module
 * can't read the live save language, and the map-screen groups are ctrl 7
 * on every known region anyway); then — for the EWRAM tilemap copies whose
 * absolute staging address is a per-region link detail — the nth
 * EWRAM-destined record of the group. */
static int FetchGfxGroupEntryRobust(u32 group, u32 wantDest, int ewramOrdinal, GfxBlob* out) {
    if (FetchGfxGroupEntry(group, wantDest, -1, 0, out) || FetchGfxGroupEntry(group, wantDest, -1, 1, out)) {
        return 1;
    }
    if (ewramOrdinal >= 0) {
        return FetchGfxGroupEntry(group, 0, ewramOrdinal, 0, out) ||
               FetchGfxGroupEntry(group, 0, ewramOrdinal, 1, out);
    }
    return 0;
}

static void FreeGfxBlob(GfxBlob* blob) {
    free(blob->owned);
    blob->owned = NULL;
    blob->data = NULL;
    blob->len = 0;
}

/* Apply one palette group to a private BG palette (banks 0-15, 16 colors
 * each, RGB555). Mirrors LoadPaletteGroup's chain walk (src/common.c:361):
 * 4-byte records { u16 paletteId, u8 destBank, u8 count|more<<7 }, count 0
 * meaning 16; palette data at gGlobalGfxAndPalettes[paletteId * 32]. OBJ
 * banks (destBank >= 16) are skipped — this composite has no sprites. */
static void ApplyPaletteGroup(u32 group, uint16_t* bgPal) {
    const u8* rec;
    int i;

    if (group >= 208u) { /* PALETTE_GROUPS_COUNT_MAX (port_rom.c) */
        return;
    }
    rec = (const u8*)gPaletteGroups[group];
    if (rec == NULL) {
        return;
    }

    for (i = 0; i < PALETTE_GROUP_WALK_MAX; i++, rec += 4) {
        u32 pg, paletteId, destBank, numPalettes, p;
        if (!RangeInRom(rec, 4)) {
            return;
        }
        pg = Port_ReadU32(rec);
        paletteId = pg & 0xFFFF;
        destBank = (pg >> 16) & 0xFF;
        numPalettes = (pg >> 24) & 0xF;
        if (numPalettes == 0) {
            numPalettes = 16;
        }
        for (p = 0; p < numPalettes; p++) {
            const u8* src = gGlobalGfxAndPalettes + (paletteId + p) * 32u;
            u32 bank = destBank + p;
            u32 c;
            if (bank >= 16 || !RangeInRom(src, 32)) {
                continue;
            }
            for (c = 0; c < 16; c++) {
                bgPal[bank * 16u + c] = Port_ReadU16(src + c * 2u);
            }
        }
        if (((pg >> 24) & 0x80) == 0) {
            return;
        }
    }
}

/* Paint one text-mode BG layer over the image. yShift: screen row r shows
 * tilemap pixel row (r - yShift) & 255 — the GBA adds BGVOFS, and the menu
 * parks it at -4 for BG1/BG2. Tilemap entries are the standard text-BG
 * format (tile 0-9, hflip 10, vflip 11, palette 12-15) over a 32x32 map;
 * rows past the 32x20 staging copy read as entry 0, exactly what the
 * MemClear'd gBGxBuffer holds on console. Color 0 is transparent, tile ids
 * beyond the loaded tile blob are skipped (console would show stale VRAM
 * there; real map tilemaps never reference unloaded tiles). */
static void DrawBgLayer(uint32_t* dst, const u8* tilemap, u32 tilemapLen, const u8* tiles, u32 tilesLen,
                        const uint16_t* bgPal, int32_t yShift) {
    u32 tileCount = tilesLen / 32u;
    int32_t x, y;

    for (y = 0; y < WORLDMAP_IMAGE_H; y++) {
        int32_t mapY = (y - yShift) & 0xFF;
        u32 tileRow = ((u32)mapY >> 3) & 31u;
        int32_t rowInTile = mapY & 7;
        for (x = 0; x < WORLDMAP_IMAGE_W; x++) {
            u32 tileCol = ((u32)x >> 3) & 31u;
            u32 entryOff = (tileRow * 32u + tileCol) * 2u;
            u16 entry = (entryOff + 2u <= tilemapLen) ? Port_ReadU16(tilemap + entryOff) : 0;
            u32 tileId = entry & 0x3FFu;
            int32_t inX = x & 7;
            int32_t inY = rowInTile;
            u8 packed, colorIndex;

            if (tileId >= tileCount) {
                continue;
            }
            if (entry & 0x400u) {
                inX = 7 - inX;
            }
            if (entry & 0x800u) {
                inY = 7 - inY;
            }
            packed = tiles[tileId * 32u + (u32)inY * 4u + ((u32)inX >> 1)];
            colorIndex = (inX & 1) ? (u8)(packed >> 4) : (u8)(packed & 0xFu);
            if (colorIndex == 0) {
                continue;
            }
            dst[(size_t)y * WORLDMAP_IMAGE_W + (size_t)x] =
                Rgb555ToRgba8888(bgPal[(((u32)entry >> 12) & 0xFu) * 16u + colorIndex]);
        }
    }
}

static int BuildWorldMapImage(void) {
    uint16_t bgPal[16 * 16];
    GfxBlob mapTiles = { 0 }, chromeTiles = { 0 }, bg1Map = { 0 }, bg2Map = { 0 }, bg3Map = { 0 };
    uint32_t backdrop;
    size_t i;
    int ok = 0;

    /* ROM tables come up on the game thread during Port_LoadRom; until then
     * (or during a mid-session ROM reload) entries read as NULL and we
     * simply report not-ready. */
    if (gRomData == NULL || gRomSize == 0 || gGlobalGfxAndPalettes == NULL) {
        return 0;
    }
    for (i = 0; i < sizeof(sWorldMapPaletteGroups); i++) {
        if (gPaletteGroups[sWorldMapPaletteGroups[i]] == NULL) {
            return 0;
        }
    }

    if (FetchGfxGroupEntryRobust(WORLDMAP_GFX_GROUP, DEST_MAP_TILES, -1, &mapTiles) &&
        FetchGfxGroupEntryRobust(WORLDMAP_GFX_GROUP, DEST_BG1_TILEMAP, 0, &bg1Map) &&
        FetchGfxGroupEntryRobust(WORLDMAP_GFX_GROUP, DEST_BG2_TILEMAP, 1, &bg2Map) &&
        FetchGfxGroupEntryRobust(CHROME_GFX_GROUP, DEST_CHROME_TILES, -1, &chromeTiles) &&
        FetchGfxGroupEntryRobust(CHROME_GFX_GROUP, DEST_BG3_TILEMAP, 0, &bg3Map)) {

        memset(bgPal, 0, sizeof(bgPal));
        for (i = 0; i < sizeof(sWorldMapPaletteGroups); i++) {
            ApplyPaletteGroup(sWorldMapPaletteGroups[i], bgPal);
        }

        /* Subtask_MapHint_0: SetColor(0, gPaletteBuffer[81]) — the screen's
         * backdrop is bank 5 color 1 of the freshly loaded group 185. */
        backdrop = Rgb555ToRgba8888(bgPal[81]);
        for (i = 0; i < (size_t)WORLDMAP_IMAGE_W * WORLDMAP_IMAGE_H; i++) {
            sImagePixels[i] = backdrop;
        }

        DrawBgLayer(sImagePixels, bg3Map.data, bg3Map.len, chromeTiles.data, chromeTiles.len, bgPal, 0);
        DrawBgLayer(sImagePixels, bg2Map.data, bg2Map.len, chromeTiles.data, chromeTiles.len, bgPal,
                    MAP_LAYER_YSHIFT);
        DrawBgLayer(sImagePixels, bg1Map.data, bg1Map.len, mapTiles.data, mapTiles.len, bgPal,
                    MAP_LAYER_YSHIFT);

        sPublishedImage = sImagePixels; /* publish only the finished image */
        ok = 1;
    }

    FreeGfxBlob(&mapTiles);
    FreeGfxBlob(&chromeTiles);
    FreeGfxBlob(&bg1Map);
    FreeGfxBlob(&bg2Map);
    FreeGfxBlob(&bg3Map);
    return ok;
}

const uint32_t* Port_SecondScreenWorldMap_GetImage(int32_t* outW, int32_t* outH) {
    const uint32_t* image = sPublishedImage;

    if (image == NULL && BuildWorldMapImage()) {
        image = sPublishedImage;
    }
    if (outW) {
        *outW = image ? WORLDMAP_IMAGE_W : 0;
    }
    if (outH) {
        *outH = image ? WORLDMAP_IMAGE_H : 0;
    }
    return image;
}

/* The game's own world→map-screen scaling, ported verbatim per marker type
 * (the two screens use slightly different Y math and we keep that):
 *   player dot, pause map (src/menu/pauseMenu.c sub_080A6378):
 *       x = worldX * 160 / 0xF90 + 0x28;   y = worldY * 128 / 0xC60 + 0xC
 *   windcrest flags, fast travel (src/subtask/subtaskFastTravel.c
 *   sub_080A6EE0):
 *       x = worldX * 160 / 0xF90 + 0x28;   y = worldY * 160 / 0xF90 + 0xC
 * 0xF90/0xC60 are the overworld's pixel extents (the ~249x198 16px-tile
 * grid of gOverworldLocations), 0x28/0xC the artwork margin on the 240x160
 * screen. Both screens draw over the same BG1 map (gfx groups 93 and 94
 * share their map-tile and tilemap entries), so both land on our image. */
static int32_t WorldToMapX(int32_t worldX) {
    return worldX * 160 / 0xF90 + 0x28;
}

static int32_t ClampMapX(int32_t x) {
    return x < 0 ? 0 : (x >= WORLDMAP_IMAGE_W ? WORLDMAP_IMAGE_W - 1 : x);
}

static int32_t ClampMapY(int32_t y) {
    return y < 0 ? 0 : (y >= WORLDMAP_IMAGE_H ? WORLDMAP_IMAGE_H - 1 : y);
}

int Port_SecondScreenWorldMap_LocatePlayer(uint8_t area, int32_t areaX, int32_t areaY, int32_t* outMapX,
                                           int32_t* outMapY) {
    /* Overworld test = CheckAreaOverworld (src/gameUtils.c): the compiled
     * gAreaMetadata row (src/data/areaMetadata.c, 153 rows) must carry
     * exactly AR_IS_OVERWORLD|AR_ALLOWS_WARP. Interiors/dungeons fail here
     * and the caller keeps its last outdoor fix — the same reason the
     * game's own marker freezes at the doorway (UpdatePlayerMapCoords only
     * stores overworld_map_x/y while AreaIsOverworld()). On the overworld,
     * entity/area coordinates ARE overworld-map coordinates: that function
     * copies gPlayerEntity.base.x.HALF_U.HI straight into overworld_map_x,
     * no per-area offset. */
    if (area >= 153 || gAreaMetadata[area].flags != (AR_IS_OVERWORLD | AR_ALLOWS_WARP)) {
        return 0;
    }
    if (outMapX) {
        *outMapX = ClampMapX(WorldToMapX(areaX));
    }
    if (outMapY) {
        *outMapY = ClampMapY(areaY * 128 / 0xC60 + 0xC);
    }
    return 1;
}

/* Windcrest pin positions are ROM constants: gUnk_08128024[bit] gives the
 * warp's area/room/room-local landing point, the area's RoomHeader adds the
 * room's world origin (sub_080A6EE0's exact recipe). Computed once, lazily,
 * because gAreaRoomHeaders is resolved from the loaded ROM — until it's
 * readable we report not-ready and the caller just retries. */
static int EnsureWindcrestPins(void) {
    int i;

    if (sWindcrestPinsReady) {
        return 1;
    }
    if (gRomData == NULL || gRomSize == 0) {
        return 0;
    }

    for (i = 0; i < 8; i++) {
        const Transition* warp = &gUnk_08128024[i];
        const RoomHeader* table;
        int32_t worldX, worldY;

        if (warp->area >= 0x90 || warp->room >= MAX_ROOMS) { /* AREA_COUNT (port_rom.c) */
            return 0;
        }
        table = gAreaRoomHeaders[warp->area];
        if (!Port_IsRoomHeaderPtrReadable(table)) {
            return 0;
        }
        worldX = (int32_t)warp->endX + table[warp->room].map_x;
        worldY = (int32_t)warp->endY + table[warp->room].map_y;
        sWindcrestPins[i].x = ClampMapX(WorldToMapX(worldX));
        sWindcrestPins[i].y = ClampMapY(worldY * 160 / 0xF90 + 0xC);
    }
    sWindcrestPinsReady = 1;
    return 1;
}

int Port_SecondScreenWorldMap_GetWindcrestPin(int32_t windcrestId, int32_t* outMapX, int32_t* outMapY) {
    /* windcrestId is the bit index within gSave.windcrests' upper byte:
     * 0 = WINDCREST_MT_CRENEL (bit 24) ... 7 = WINDCREST_MINISH_WOODS
     * (bit 31), the same index the fast-travel cursor uses. */
    if (windcrestId < 0 || windcrestId >= 8 || !EnsureWindcrestPins()) {
        return 0;
    }
    if (outMapX) {
        *outMapX = sWindcrestPins[windcrestId].x;
    }
    if (outMapY) {
        *outMapY = sWindcrestPins[windcrestId].y;
    }
    return 1;
}
