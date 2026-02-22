/*
 * port_rom.c — Load baserom.gba and resolve ROM data symbols.
 *
 * GBA ROM at 0x08000000. Pointer tables (gGfxGroups, gPaletteGroups, etc.)
 * are translated to native pointers. ROM pages (4 KB) are extracted to
 * rom_data/ so the game can run without the full ROM after first boot.
 */

#include "port_rom.h"
#include "area.h"
#include "map.h"
#include "port_config.h"
#include "port_gba_mem.h"
#include "structures.h"
#include "tileMap.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <direct.h>
#include <io.h>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#endif

u8* gRomData = NULL;
u32 gRomSize = 0;
static SpritePtr sSpritePtrsStable[512];

/* ------------------------------------------------------------------ */
/*  ROM page extraction (4 KB pages)                                  */
/* ------------------------------------------------------------------ */
#define ROM_EXTRACT_DIR "rom_data"
#define ROM_PAGE_SHIFT 12
#define ROM_PAGE_SIZE (1u << ROM_PAGE_SHIFT)              /* 4096 */
#define ROM_EXPECTED_SIZE 0x1000000u                      /* 16 MB USA ROM */
#define ROM_MAX_PAGES (ROM_EXPECTED_SIZE / ROM_PAGE_SIZE) /* 4096 */

/* Bitfield: 1 = page already extracted this session */
static u8 sExtractedPages[ROM_MAX_PAGES / 8];

static void MarkPageExtracted(u32 page) {
    sExtractedPages[page / 8] |= (u8)(1 << (page % 8));
}
static int IsPageExtracted(u32 page) {
    return (sExtractedPages[page / 8] >> (page % 8)) & 1;
}

static void EnsureExtractDir(void) {
#ifdef _WIN32
    _mkdir(ROM_EXTRACT_DIR);
#else
    mkdir(ROM_EXTRACT_DIR, 0755);
#endif
}

/* Extract a single 4 KB page to rom_data/XXXXXXXX.bin */
static void ExtractPage(u32 page) {
    if (page >= ROM_MAX_PAGES || !gRomData)
        return;
    if (IsPageExtracted(page))
        return;
    MarkPageExtracted(page);

    u32 offset = page << ROM_PAGE_SHIFT;
    u32 size = ROM_PAGE_SIZE;
    if (offset + size > gRomSize)
        size = gRomSize - offset;
    if (size == 0)
        return;

    EnsureExtractDir();

    char path[256];
    snprintf(path, sizeof(path), ROM_EXTRACT_DIR "/%08X.bin", offset);

    /* Don't overwrite if file already exists with correct size */
    FILE* chk = fopen(path, "rb");
    if (chk) {
        fseek(chk, 0, SEEK_END);
        long existing = ftell(chk);
        fclose(chk);
        if ((u32)existing == size)
            return;
    }

    FILE* f = fopen(path, "wb");
    if (f) {
        fwrite(&gRomData[offset], 1, size, f);
        fclose(f);
    }
}

/* Extract all pages covering [rom_offset .. rom_offset+size) */
static void ExtractRegion(u32 rom_offset, u32 size) {
    if (!gRomData || size == 0)
        return;
    u32 first_page = rom_offset >> ROM_PAGE_SHIFT;
    u32 last_page = (rom_offset + size - 1) >> ROM_PAGE_SHIFT;
    for (u32 p = first_page; p <= last_page && p < ROM_MAX_PAGES; p++)
        ExtractPage(p);
}

/* Load rom_data/*.bin files from a specific directory into gRomData.
 * Returns the number of pages loaded. */
static int LoadExtractedPagesFrom(const char* dir) {
    int loaded = 0;

    /* Allocate gRomData if not yet done */
    if (!gRomData) {
        gRomSize = ROM_EXPECTED_SIZE;
        gRomData = (u8*)calloc(1, gRomSize);
        if (!gRomData) {
            fprintf(stderr, "ERROR: Failed to allocate %u bytes for ROM buffer\n", gRomSize);
            return 0;
        }
    }

#ifdef _WIN32
    char pattern[256];
    snprintf(pattern, sizeof(pattern), "%s\\*.bin", dir);
    WIN32_FIND_DATAA fd;
    HANDLE hFind = FindFirstFileA(pattern, &fd);
    if (hFind == INVALID_HANDLE_VALUE)
        return 0;
    do {
        u32 offset = 0;
        if (sscanf(fd.cFileName, "%08X.bin", &offset) != 1)
            continue;
        if (offset >= gRomSize)
            continue;

        char path[256];
        snprintf(path, sizeof(path), "%s\\%s", dir, fd.cFileName);
        FILE* f = fopen(path, "rb");
        if (!f)
            continue;
        fseek(f, 0, SEEK_END);
        u32 fsize = (u32)ftell(f);
        fseek(f, 0, SEEK_SET);
        if (offset + fsize > gRomSize)
            fsize = gRomSize - offset;
        fread(&gRomData[offset], 1, fsize, f);
        fclose(f);

        /* Mark pages as extracted so we don't re-write them */
        u32 first_page = offset >> ROM_PAGE_SHIFT;
        u32 last_page = (offset + fsize - 1) >> ROM_PAGE_SHIFT;
        for (u32 p = first_page; p <= last_page; p++)
            MarkPageExtracted(p);
        loaded++;
    } while (FindNextFileA(hFind, &fd));
    FindClose(hFind);
#else
    DIR* dirp = opendir(dir);
    if (!dirp)
        return 0;
    struct dirent* ent;
    while ((ent = readdir(dirp)) != NULL) {
        u32 offset = 0;
        if (sscanf(ent->d_name, "%08X.bin", &offset) != 1)
            continue;
        if (offset >= gRomSize)
            continue;

        char path[256];
        snprintf(path, sizeof(path), "%s/%s", dir, ent->d_name);
        FILE* f = fopen(path, "rb");
        if (!f)
            continue;
        fseek(f, 0, SEEK_END);
        u32 fsize = (u32)ftell(f);
        fseek(f, 0, SEEK_SET);
        if (offset + fsize > gRomSize)
            fsize = gRomSize - offset;
        fread(&gRomData[offset], 1, fsize, f);
        fclose(f);

        u32 first_page = offset >> ROM_PAGE_SHIFT;
        u32 last_page = (offset + fsize - 1) >> ROM_PAGE_SHIFT;
        for (u32 p = first_page; p <= last_page; p++)
            MarkPageExtracted(p);
        loaded++;
    }
    closedir(dirp);
#endif

    return loaded;
}

/* Try multiple rom_data directories and return total pages loaded */
static int LoadExtractedPages(void) {
    const char* dirs[] = {
        ROM_EXTRACT_DIR,  /* rom_data/ (cwd) */
        "../../rom_data", /* project root from build/pc/ */
    };
    int total = 0;
    for (int i = 0; i < (int)(sizeof(dirs) / sizeof(dirs[0])); i++) {
        int n = LoadExtractedPagesFrom(dirs[i]);
        if (n > 0) {
            fprintf(stderr, "  rom_data: %d pages from %s\n", n, dirs[i]);
            total += n;
        }
    }
    return total;
}

/* ------------------------------------------------------------------ */
/*  ROM access logging — now also extracts the touched page           */
/* ------------------------------------------------------------------ */
void Port_LogRomAccess(u32 gba_addr, const char* caller) {
    if (gba_addr < 0x08000000u)
        return;
    u32 offset = gba_addr - 0x08000000u;
    u32 page = offset >> ROM_PAGE_SHIFT;
    if (page < ROM_MAX_PAGES && !IsPageExtracted(page)) {
        fprintf(stderr, "[ROM] New page %08X accessed from %s\n", page << ROM_PAGE_SHIFT, caller ? caller : "?");
        fflush(stderr);
    }
    ExtractPage(page);
}

void Port_PrintRomAccessSummary(void) {
    int count = 0;
    for (u32 p = 0; p < ROM_MAX_PAGES; p++) {
        if (IsPageExtracted(p))
            count++;
    }
    fprintf(stderr, "\n[ROM] Summary: %d pages (4 KB each, %d KB total) extracted to " ROM_EXTRACT_DIR "/\n", count,
            count * 4);
    fflush(stderr);
}

/* ------------------------------------------------------------------ */
/*  ROM region detection & offset tables (USA / EU)                   */
/* ------------------------------------------------------------------ */
RomRegion gRomRegion = ROM_REGION_UNKNOWN;
const RomOffsets* gRomOffsets = NULL;

/* USA offsets (from build/USA/tmc.map) */
const RomOffsets kRomOffsets_USA = {
    .gfxAndPalettes = 0x5A2E80,
    .gfxGroups = 0x100AA8,
    .paletteGroups = 0x0FF850,
    .objPalettes = 0x133368,
    .frameObjLists = 0x2F3D74,
    .fixedTypeGfx = 0x132B30,
    .spritePtrs = 0x0029B4,
    .translations = 0x109214,
    .text09230 = 0x109230,
    .text09244 = 0x109244,
    .text09248 = 0x109248,
    .text0926C = 0x10926C,
    .text092AC = 0x1092AC,
    .text092D4 = 0x1092D4,
    .text0942E = 0x10942E,
    .text094CE = 0x1094CE,
    .uiData = 0x0C9044,
    .fadeData = 0x000F54,
    .overlaySizeTable = 0x0B2BE8,
    .mapDataBase = 0x324AE4,
    .areaRoomHeaders = 0x11E214,
    .areaTileSets = 0x10246C,
    .areaTileSetsCount = 0x90,
    .areaRoomMaps = 0x107988,
    .areaTable = 0x0D50FC,
    .areaTiles = 0x10309C,
    .exitLists = 0x13A7F0,
    .bgAnimTable = 0x0B755C,
    .localFlagBanks = 0x11E454,
    .gfxGroupsCount = 133,
    .paletteGroupsCount = 208,
    .objPalettesCount = 360,
    .frameObjListsSize = 200045,
    .fixedTypeGfxCount = 527,
    .spritePtrsCount = 329,
    .expectedRomSize = 0x1000000,
    .gameCode = "BZME",
};

/* EU offsets (from build/EU/tmc_eu.map) */
const RomOffsets kRomOffsets_EU = {
    .gfxAndPalettes = 0x5A23D0,
    .gfxGroups = 0x100204,
    .paletteGroups = 0x0FED88,
    .objPalettes = 0x1329B4,
    .frameObjLists = 0x2F3460,
    .fixedTypeGfx = 0x132180,
    .spritePtrs = 0x002A5C,
    .translations = 0x108968,
    .text09230 = 0x108984,
    .text09244 = 0x108998,
    .text09248 = 0x10899C,
    .text0926C = 0x1089C0,
    .text092AC = 0x108A00,
    .text092D4 = 0x108A28,
    .text0942E = 0x108B82,
    .text094CE = 0x108C22,
    .uiData = 0x0C876C,
    .fadeData = 0x000F9C,
    .overlaySizeTable = 0x0B25E8, /* EU overlay size table (shifted) */
    .mapDataBase = 0x323FEC,
    .areaRoomHeaders = 0x11D95C,
    .areaTileSets = 0x101BC8,
    .areaTileSetsCount = 0x90,
    .areaRoomMaps = 0x1070E4,
    .areaTable = 0x0D4828,
    .areaTiles = 0x1027F8,
    .exitLists = 0x139EDC,
    .bgAnimTable = 0x0B6C84,
    .localFlagBanks = 0x11DB9C,
    .gfxGroupsCount = 133,
    .paletteGroupsCount = 208,
    .objPalettesCount = 360,
    .frameObjListsSize = 200045,
    .fixedTypeGfxCount = 527,
    .spritePtrsCount = 329,
    .expectedRomSize = 0x1000000,
    .gameCode = "BZMP",
};

RomRegion Port_DetectRomRegion(const u8* romData, u32 romSize) {
    if (!romData || romSize < 0xB0)
        return ROM_REGION_UNKNOWN;

    if (memcmp(&romData[0xAC], "BZME", 4) == 0) {
        gRomRegion = ROM_REGION_USA;
        gRomOffsets = &kRomOffsets_USA;
        fprintf(stderr, "ROM region detected: USA (BZME)\n");
    } else if (memcmp(&romData[0xAC], "BZMP", 4) == 0) {
        gRomRegion = ROM_REGION_EU;
        gRomOffsets = &kRomOffsets_EU;
        fprintf(stderr, "ROM region detected: EU (BZMP)\n");
    } else {
        fprintf(stderr, "WARNING: Unknown ROM game code '%.4s'. Defaulting to USA offsets.\n", &romData[0xAC]);
        gRomRegion = ROM_REGION_USA;
        gRomOffsets = &kRomOffsets_USA;
    }
    return gRomRegion;
}

/* Max table sizes (for static arrays) */
#define GFX_GROUPS_COUNT_MAX 133
#define PALETTE_GROUPS_COUNT_MAX 208
#define AREA_COUNT 0x90 /* 144 areas (0x00..0x8F) */
#define MORE_SPRITE_PTRS_COUNT 16
#define SPRITE_ANIM_322_COUNT 128

extern u32 gFrameObjLists[];
/* gUnk_08133368 now const — provided by src/data/objPalettes.c */
extern SpritePtr gSpritePtrs[];
extern u32 gFixedTypeGfxData[];
extern u16* gMoreSpritePtrs[MORE_SPRITE_PTRS_COUNT];
extern Frame* gSpriteAnimations_322[SPRITE_ANIM_322_COUNT];
extern void Port_LoadOverlayData(const u8* romData, u32 romSize, u32 overlayOffset);

/* ---- Compile-time ROM tables (port_rom_tables.c) ---- */
extern const u8 kFrameObjListsData[];
extern const u8 kFixedTypeGfxInitData[];
extern const u8 kOverlaySizeData[];
extern const u8 kFontText09244Data[];
extern const u8 kFontText0926CData[];
extern const u8 kFontText092D4Data[];
extern const u8 kFontText0942EData[];
extern const u8 kFontText094CEData[];
extern const u8 kUiInitData[];
extern const u32 kGfxGroupOffsets[];
extern const u32 kPaletteGroupOffsets[];
extern const u32 kSpritePtrEntries[][4];
extern const u32 kAreaRoomHeaderOffsets[];
extern const u32 kAreaTileSetOffsets[];
extern const u32 kAreaRoomMapOffsets[];
extern const u32 kAreaTableOffsets[];
extern const u32 kAreaTilesOffsets[];
extern const u32 kTranslationOffsets[];
extern const u32 kUnk09230Offsets[];
extern const u32 kUnk09248Offsets[];
extern const u32 kUnk092ACOffsets[];

/* Helper: resolve an offset from a compile-time offset table.
 * 0xFFFFFFFF means NULL. */
static inline void* ResolveTableOffset(u32 offset) {
    if (offset == 0xFFFFFFFF || !gRomData)
        return NULL;
    if (offset < gRomSize)
        return &gRomData[offset];
    return NULL;
}

/* Area / room data tables (port_linked_stubs.c) */
extern RoomHeader* gAreaRoomHeaders[];
extern void* gAreaRoomMaps[];
extern void* gAreaTable[];
extern void* gAreaTileSets[];
extern void* gAreaTiles[];
/* gExitLists now const — provided by src/data/transitions.c */

/*
 * Shadow arrays for second-level ROM pointer resolution.
 * On GBA, sub-arrays inside gAreaTileSets[area], gAreaRoomMaps[area],
 * gAreaTable[area], and gExitLists[area] contain packed 32-bit GBA ROM
 * pointers. On 64-bit PC, sizeof(void*)==8, so we can't dereference them
 * directly. These shadow arrays hold pre-resolved native pointers.
 */
static void* sTileSetsResolved[AREA_COUNT][MAX_ROOMS];
static void* sRoomMapsResolved[AREA_COUNT][MAX_ROOMS];
static void* sAreaTableResolved[AREA_COUNT][MAX_ROOMS];
/* sExitListsResolved removed — gExitLists now compile-time const from transitions.c */

/* Forward declaration */
static inline void* ResolveRomPtr(u32 gba_addr);

/* Resolve a ROM sub-table of 32-bit GBA pointers into a native pointer array. */
static void ResolveSubTable(void* romBase, void** dest, u32 count) {
    u8* base = (u8*)romBase;
    for (u32 j = 0; j < count; j++) {
        u32 ptr;
        memcpy(&ptr, base + j * 4, 4);
        dest[j] = ResolveRomPtr(ptr);
    }
}

/* Font data tables (data_stubs_autogen.c) */
extern void* gUnk_08109230[];
extern u8 gUnk_08109244[];
extern void* gTranslations[];
extern void* gUnk_08109248[];
extern u8 gUnk_0810926C[];
extern void* gUnk_081092AC[];
extern u8 gUnk_081092D4[];
extern u8 gUnk_0810942E[];
extern u8 gUnk_081094CE[];

/* Resolved pointer tables */
const u8* gGlobalGfxAndPalettes = NULL;
const void* gGfxGroups[GFX_GROUPS_COUNT_MAX];
const void* gPaletteGroups[PALETTE_GROUPS_COUNT_MAX];

/* Helper: resolve a 32-bit GBA ROM pointer to native */
static inline void* ResolveRomPtr(u32 gba_addr) {
    if (gba_addr == 0)
        return NULL;
    if (gba_addr >= 0x08000000u && gba_addr < 0x08000000u + gRomSize)
        return &gRomData[gba_addr - 0x08000000u];
    fprintf(stderr, "ResolveRomPtr: address 0x%08X is outside ROM\n", gba_addr);
    return NULL;
}

/* Read a packed 32-bit GBA ROM pointer at base + index*4.
 * Returns resolved native pointer for data, NULL for function pointers. */
void* Port_ReadPackedRomPtr(const void* base, u32 index) {
    if (!base) {
        fprintf(stderr, "Port_ReadPackedRomPtr: base is NULL (index=%u)\n", index);
        return NULL;
    }
    /* Safety: check that base points inside gRomData */
    if (gRomData && ((const u8*)base < gRomData || (const u8*)base >= gRomData + gRomSize)) {
        fprintf(stderr, "Port_ReadPackedRomPtr: base %p is outside gRomData [%p..%p] (index=%u)\n", base,
                (void*)gRomData, (void*)(gRomData + gRomSize), index);
        return NULL;
    }
    u32 raw;
    memcpy(&raw, (const u8*)base + index * 4, 4);
    if (raw == 0)
        return NULL;
    /* GBA Thumb function pointers have bit 0 set (Thumb indicator).
     * These point to GBA code and can't be called on PC. */
    if (raw & 1)
        return NULL;
    return ResolveRomPtr(raw);
}

/*
 * Port_ResolveEwramPtr — resolve a GBA EWRAM address to a native PC pointer.
 *
 * On GBA, MapLayer (gMapBottom/gMapTop) starts with a 4-byte pointer bgSettings,
 * so mapData is at offset +4.  On 64-bit PC, bgSettings is 8 bytes, shifting all
 * subsequent fields by +4.  We compensate by adding that delta when the GBA
 * address falls within a MapLayer.
 *
 * Known EWRAM globals (same addresses for USA and EU):
 *   0x02025EB0  gMapBottom      (MapLayer, ~0xC010 bytes)
 *   0x0200B650  gMapTop         (MapLayer, ~0xC010 bytes)
 *   0x02019EE0  gMapDataBottomSpecial
 *   0x02002F00  gMapDataTopSpecial
 */
void* Port_ResolveEwramPtr(u32 gba_addr) {
    /* --- gMapBottom (MapLayer at GBA 0x02025EB0) --- */
    {
        const u32 GBA_BASE = 0x02025EB0u;
        const u32 GBA_SIZE = 0xC010u; /* sizeof(MapLayer) on GBA: 4 + 0xC00C = 0xC010 */
        if (gba_addr >= GBA_BASE && gba_addr < GBA_BASE + GBA_SIZE) {
            u32 gba_off = gba_addr - GBA_BASE;
            /*
             * GBA layout:  bgSettings(4)  mapData(0x2000)  collisionData(0x1000) ...
             * PC  layout:  bgSettings(8)  mapData(0x2000)  collisionData(0x1000) ...
             * All fields after bgSettings are shifted by +4 on PC.
             */
            u32 pc_off = (gba_off < 4) ? gba_off : gba_off + 4;
            return (u8*)&gMapBottom + pc_off;
        }
    }
    /* --- gMapTop (MapLayer at GBA 0x0200B650) --- */
    {
        const u32 GBA_BASE = 0x0200B650u;
        const u32 GBA_SIZE = 0xC010u;
        if (gba_addr >= GBA_BASE && gba_addr < GBA_BASE + GBA_SIZE) {
            u32 gba_off = gba_addr - GBA_BASE;
            u32 pc_off = (gba_off < 4) ? gba_off : gba_off + 4;
            return (u8*)&gMapTop + pc_off;
        }
    }
    /* --- gMapDataBottomSpecial at GBA 0x02019EE0 --- */
    {
        const u32 GBA_BASE = 0x02019EE0u;
        if (gba_addr >= GBA_BASE && gba_addr < GBA_BASE + sizeof(gMapDataBottomSpecial)) {
            return (u8*)&gMapDataBottomSpecial + (gba_addr - GBA_BASE);
        }
    }
    /* --- gMapDataTopSpecial at GBA 0x02002F00 --- */
    {
        const u32 GBA_BASE = 0x02002F00u;
        if (gba_addr >= GBA_BASE && gba_addr < GBA_BASE + sizeof(gMapDataTopSpecial)) {
            return (u8*)&gMapDataTopSpecial + (gba_addr - GBA_BASE);
        }
    }
    /* Fallback to generic gba_TryMemPtr for other EWRAM or non-EWRAM addresses */
    return gba_TryMemPtr(gba_addr);
}

const SpritePtr* Port_GetSpritePtr(u16 sprite_idx) {
    if (!gRomOffsets)
        return NULL;
    if (sprite_idx >= gRomOffsets->spritePtrsCount)
        return NULL;
    return &sSpritePtrsStable[sprite_idx];
}

static FILE* TryOpenRom(const char** paths, int count, char* foundPath, int foundPathLen) {
    for (int i = 0; i < count; i++) {
        FILE* f = fopen(paths[i], "rb");
        if (f) {
            if (foundPath)
                snprintf(foundPath, foundPathLen, "%s", paths[i]);
            return f;
        }
    }
    return NULL;
}

/*
 * LoadRomGaps — load rom_gaps.bin to fill assembled data regions
 * (pointer tables, GfxItem arrays, etc.) that are NOT in asset files.
 *
 * File format: "GAPD" magic, u32 chunk_count,
 *   then for each chunk: u32 offset, u32 size, u8[size] data.
 *
 * Generated by tools/generate_rom_gaps.py from baserom.gba.
 */
static int LoadRomGaps(void) {
    const char* candidates[] = {
        "rom_gaps.bin",       "build/USA/rom_gaps.bin",       "build/pc/rom_gaps.bin",
        "../../rom_gaps.bin", "../../build/USA/rom_gaps.bin", "../rom_gaps.bin",
    };
    FILE* f = NULL;
    const char* usedPath = NULL;
    for (int i = 0; i < (int)(sizeof(candidates) / sizeof(candidates[0])); i++) {
        f = fopen(candidates[i], "rb");
        if (f) {
            usedPath = candidates[i];
            break;
        }
    }
    if (!f)
        return 0;

    /* Allocate ROM buffer if not already done */
    if (!gRomData) {
        gRomSize = ROM_EXPECTED_SIZE;
        gRomData = (u8*)calloc(1, gRomSize);
        if (!gRomData) {
            fclose(f);
            return 0;
        }
    }

    /* Read and verify header */
    char magic[4];
    u32 chunkCount;
    if (fread(magic, 1, 4, f) != 4 || memcmp(magic, "GAPD", 4) != 0) {
        fprintf(stderr, "WARNING: %s has invalid magic\n", usedPath);
        fclose(f);
        return 0;
    }
    if (fread(&chunkCount, 4, 1, f) != 1) {
        fclose(f);
        return 0;
    }

    /* Read chunks and patch gRomData */
    u32 loaded = 0;
    u32 totalBytes = 0;
    for (u32 i = 0; i < chunkCount; i++) {
        u32 offset, size;
        if (fread(&offset, 4, 1, f) != 1 || fread(&size, 4, 1, f) != 1)
            break;
        if (offset + size > gRomSize) {
            fseek(f, (long)size, SEEK_CUR);
            continue;
        }
        if (fread(&gRomData[offset], 1, size, f) != size)
            break;
        loaded++;
        totalBytes += size;
    }

    fclose(f);
    fprintf(stderr, "Gap data loaded: %u chunks (%u KB) from %s\n", loaded, totalBytes / 1024, usedPath);
    return (int)loaded;
}

void Port_LoadRom(const char* path) {
    memset(sExtractedPages, 0, sizeof(sExtractedPages));

    /* ---- Step 1: try loading from rom_data/ extracted pages ---- */
    int pagesLoaded = LoadExtractedPages();
    if (pagesLoaded > 0) {
        fprintf(stderr, "ROM data: loaded %d extracted pages from " ROM_EXTRACT_DIR "/\n", pagesLoaded);
    }

    /* ---- Step 2: try ROM files (USA first, then EU) ---- */
    /*
     * Load a ROM file BEFORE assets so that ALL data regions are filled,
     * including assembled pointer tables (GfxGroups, PaletteGroups, area
     * tables, etc.) that are NOT covered by .incbin asset files.
     * Assets are loaded afterwards and safely overwrite the .incbin regions
     * with identical data.
     */
    int romLoaded = 0;
    {
        const char* romCandidates[] = {
            path,                    /* user-provided path */
            "baserom.gba",           /* USA default */
            "baserom_eu.gba",        /* EU default */
            "synthetic_baserom.gba", /* generated from extracted assets */
            "build/pc/baserom.gba",  /* copied to build dir */
            "build/pc/baserom_eu.gba",
            "tmc.gba", /* common names */
            "tmc_eu.gba",
            /* When running from build/pc/, look up to project root */
            "../../baserom.gba",
            "../../baserom_eu.gba",
            "../../tmc.gba",
            "../../tmc_eu.gba",
        };
        int numCandidates = sizeof(romCandidates) / sizeof(romCandidates[0]);
        char usedPath[256] = { 0 };

        FILE* f = TryOpenRom(romCandidates, numCandidates, usedPath, sizeof(usedPath));
        if (f) {
            fseek(f, 0, SEEK_END);
            u32 fileSize = (u32)ftell(f);
            fseek(f, 0, SEEK_SET);

            if (!gRomData) {
                gRomSize = fileSize;
                gRomData = (u8*)malloc(gRomSize);
                if (!gRomData) {
                    fprintf(stderr, "ERROR: Failed to allocate %u bytes for ROM\n", gRomSize);
                    abort();
                }
            }
            if (fileSize <= gRomSize) {
                fread(gRomData, 1, fileSize, f);
                gRomSize = fileSize;
            }
            fclose(f);
            romLoaded = 1;
            fprintf(stderr, "ROM loaded: %u bytes (0x%X) from %s\n", gRomSize, gRomSize, usedPath);
        }
    }

    /* ---- Step 3: overlay assets on top of ROM data ---- */
    /*
     * Assets cover .incbin binary blobs. If a ROM was loaded, the .incbin
     * regions already have correct data and this is a harmless overwrite.
     * If no ROM was loaded, assets fill those regions from build output.
     * NOTE: assembled pointer tables (gGfxGroups, gPaletteGroups, area
     * tables, etc.) are NOT in assets — they require a ROM file.
     */
    extern int Port_LoadFromAssets(const char* assetsDir);
    const char* assetDirs[] = {
        "build/USA/assets",      "build/EU/assets",
        "../build/USA/assets",   "../../build/USA/assets", /* project root from build/pc/ */
        "../../build/EU/assets",
    };
    int assetsLoaded = 0;
    for (int i = 0; i < (int)(sizeof(assetDirs) / sizeof(assetDirs[0])); i++) {
        if (Port_LoadFromAssets(assetDirs[i])) {
            assetsLoaded = 1;
            break;
        }
    }
    if (assetsLoaded) {
        fprintf(stderr, "Asset data overlaid on ROM buffer.\n");
    }

    /* ---- Step 4: load gap data (assembled tables not in assets) ---- */
    /*
     * rom_gaps.bin contains ROM data from regions NOT covered by .incbin
     * asset files: pointer tables, GfxItem arrays, PaletteGroup structs,
     * area sub-tables, etc. Generated once from baserom.gba by
     * tools/generate_rom_gaps.py.
     */
    int gapsLoaded = 0;
    if (!romLoaded) {
        gapsLoaded = LoadRomGaps();
    }

    /* ---- Check that we have some data ---- */
    if (!romLoaded && !assetsLoaded && !gapsLoaded && pagesLoaded == 0) {
        fprintf(stderr, "ERROR: Cannot open any ROM file.\n");
        fprintf(stderr, "  Place baserom.gba (USA) or baserom_eu.gba (EU) in the working directory,\n");
        fprintf(stderr, "  or provide extracted pages in " ROM_EXTRACT_DIR "/\n");
        fprintf(stderr, "  or provide asset files in build/USA/assets/\n");
        abort();
    }
    if (!romLoaded && assetsLoaded && !gapsLoaded) {
        fprintf(stderr, "WARNING: No ROM file or rom_gaps.bin found. Assembled data tables\n");
        fprintf(stderr, "  (pointer tables, GfxGroups, PaletteGroups, area tables) may be missing.\n");
        fprintf(stderr, "  Run: python tools/generate_rom_gaps.py\n");
    }

    if (!gRomData || gRomSize == 0) {
        fprintf(stderr, "ERROR: No ROM data available.\n");
        abort();
    }

    /* ---- Step 3: auto-detect ROM region ---- */
    Port_DetectRomRegion(gRomData, gRomSize);
    const RomOffsets* R = gRomOffsets;

    fprintf(stderr, "Using offsets for %s (game code: %.4s)\n", gRomRegion == ROM_REGION_EU ? "EU" : "USA",
            R->gameCode);

    /* ---- Step 4: resolve ROM symbols using compile-time tables + gRomData ---- */

    /* gGlobalGfxAndPalettes — huge palette/gfx blob (still points into gRomData) */
    gGlobalGfxAndPalettes = &gRomData[R->gfxAndPalettes];

    /* gGfxGroups — resolved from compile-time offset table */
    for (u32 i = 0; i < R->gfxGroupsCount; i++) {
        ((const void**)gGfxGroups)[i] = ResolveTableOffset(kGfxGroupOffsets[i]);
    }

    /* gPaletteGroups — resolved from compile-time offset table */
    for (u32 i = 0; i < R->paletteGroupsCount; i++) {
        ((const void**)gPaletteGroups)[i] = ResolveTableOffset(kPaletteGroupOffsets[i]);
    }

    /* gFrameObjLists — from compile-time const data (no ROM read needed) */
    memcpy(gFrameObjLists, kFrameObjListsData, R->frameObjListsSize);
    fprintf(stderr, "gFrameObjLists loaded (%u bytes from compile-time table).\n", R->frameObjListsSize);

    /* gExtraFrameOffsets — self-relative offset table for multi-part sprite positioning */
    {
        extern const u8 kExtraFrameOffsetsData[4352];
        extern u8 gExtraFrameOffsets[4352];
        memcpy(gExtraFrameOffsets, kExtraFrameOffsetsData, 4352);
        fprintf(stderr, "gExtraFrameOffsets loaded (4352 bytes from compile-time table).\n");
    }

    /* OBJ palette offset table — now compile-time const from src/data/objPalettes.c */

    /* gFixedTypeGfxData — from compile-time const data */
    memcpy(gFixedTypeGfxData, kFixedTypeGfxInitData, R->fixedTypeGfxCount * 4);
    fprintf(stderr, "gFixedTypeGfxData loaded (%u entries from compile-time table).\n", R->fixedTypeGfxCount);

    /* gSpritePtrs — resolved from compile-time offset table */
    {
        memset(sSpritePtrsStable, 0, sizeof(sSpritePtrsStable));
        for (u32 i = 0; i < R->spritePtrsCount; i++) {
            gSpritePtrs[i].animations = ResolveTableOffset(kSpritePtrEntries[i][0]);
            gSpritePtrs[i].frames = (SpriteFrame*)ResolveTableOffset(kSpritePtrEntries[i][1]);
            gSpritePtrs[i].ptr = ResolveTableOffset(kSpritePtrEntries[i][2]);
            gSpritePtrs[i].pad = kSpritePtrEntries[i][3];
            sSpritePtrsStable[i] = gSpritePtrs[i];
        }
        fprintf(stderr, "gSpritePtrs loaded (%u entries from compile-time offset table, pointers resolved).\n",
                R->spritePtrsCount);
    }

    /* gMoreSpritePtrs / gSpriteAnimations_322
     * On GBA, these are packed 32-bit pointer tables. On PC/64-bit we materialize
     * native pointers to avoid invalid indexing with sizeof(void*) == 8. */
    {
        memset(gMoreSpritePtrs, 0, sizeof(gMoreSpritePtrs));
        memset(gSpriteAnimations_322, 0, sizeof(gSpriteAnimations_322));

        if (R->spritePtrsCount > 322) {
            const SpritePtr* sp322 = &gSpritePtrs[322];
            gMoreSpritePtrs[0] = (u16*)sp322->animations;
            gMoreSpritePtrs[1] = (u16*)sp322->frames;
            gMoreSpritePtrs[2] = (u16*)sp322->ptr;

            if (sp322->animations != NULL) {
                const u8* animTable = (const u8*)sp322->animations;
                u32 resolvedCount = 0;
                for (u32 i = 0; i < SPRITE_ANIM_322_COUNT; i++) {
                    u32 gbaPtr;
                    memcpy(&gbaPtr, animTable + i * 4, 4);
                    if (gbaPtr == 0) {
                        break;
                    }
                    gSpriteAnimations_322[i] = (Frame*)ResolveRomPtr(gbaPtr);
                    if (gSpriteAnimations_322[i] == NULL) {
                        break;
                    }
                    resolvedCount++;
                }
                fprintf(stderr, "gSpriteAnimations_322 resolved (%u entries via SpritePtr[322]).\n", resolvedCount);
            }
        }
    }

    /* Font/text data tables — from compile-time const data */
    memcpy(gUnk_08109244, kFontText09244Data, 4);
    memcpy(gUnk_0810926C, kFontText0926CData, 64);
    memcpy(gUnk_081092D4, kFontText092D4Data, 346);
    memcpy(gUnk_0810942E, kFontText0942EData, 160);
    memcpy(gUnk_081094CE, kFontText094CEData, 1378);

    /* UI data — from compile-time const data */
    {
        extern u8 gUnk_080C9044[];
        memcpy(gUnk_080C9044, kUiInitData, 8);
    }

    /* UI element definitions (native function pointers) */
    {
        extern void Port_InitUIElementDefinitions(void);
        Port_InitUIElementDefinitions();
    }

    /* gTranslations — resolved from compile-time offset table */
    for (int i = 0; i < 7; i++) {
        gTranslations[i] = ResolveTableOffset(kTranslationOffsets[i]);
    }
    fprintf(stderr, "gTranslations loaded (7 entries from compile-time offsets).\n");

    /* gUnk_08109230 — resolved from compile-time offset table */
    for (int i = 0; i < 5; i++) {
        gUnk_08109230[i] = ResolveTableOffset(kUnk09230Offsets[i]);
    }

    /* gUnk_08109248 — resolved from compile-time offset table */
    for (int i = 0; i < 9; i++) {
        gUnk_08109248[i] = ResolveTableOffset(kUnk09248Offsets[i]);
    }
    fprintf(stderr, "gUnk_08109248 font tables loaded (9 entries from compile-time offsets).\n");

    /* gUnk_081092AC — resolved from compile-time offset table */
    for (int i = 0; i < 10; i++) {
        gUnk_081092AC[i] = ResolveTableOffset(kUnk092ACOffsets[i]);
    }
    fprintf(stderr, "gUnk_081092AC border tables loaded (10 entries from compile-time offsets).\n");

    /* Load overlay data from compile-time const (no ROM read needed) */
    {
        extern void Port_LoadOverlayDataFromConst(const u8* data, u32 size);
        Port_LoadOverlayDataFromConst(kOverlaySizeData, 240);
    }

    /* gMapData — copy map data blob from ROM into the PC buffer.
     * On GBA, gMapData is a ROM label; on PC it's a large u8 array.
     * Source files compute &gMapData + offset, so we fill the buffer. */
    {
        extern u8 gMapData[];
        u32 mapDataSize = gRomSize - R->mapDataBase;
        if (mapDataSize > 0xE00000u)
            mapDataSize = 0xE00000u;
        memcpy(gMapData, &gRomData[R->mapDataBase], mapDataSize);
        fprintf(stderr, "gMapData loaded (%u bytes from ROM offset 0x%X).\n", mapDataSize, R->mapDataBase);
    }

    /* ---- Area / room data tables (0x90 entries each) ---- */
    {
        /* gAreaRoomHeaders — pointer to RoomHeader array per area.
         * RoomHeader contains only u16 fields (no pointers), so we can
         * point directly into gRomData. */
        /* gAreaRoomHeaders — resolved from compile-time offset table */
        for (u32 i = 0; i < AREA_COUNT; i++) {
            gAreaRoomHeaders[i] = (RoomHeader*)ResolveTableOffset(kAreaRoomHeaderOffsets[i]);
        }
        fprintf(stderr, "gAreaRoomHeaders loaded (0x%X entries from compile-time offsets).\n", AREA_COUNT);

        /* First pass: resolve first-level pointers from compile-time offset tables.
         * NOTE: gAreaTileSets now uses the full R->areaTileSetsCount (0x90) entries,
         *       same as the other tables (AREA_COUNT = 0x90). */
        u32 tsCount = R->areaTileSetsCount < AREA_COUNT ? R->areaTileSetsCount : AREA_COUNT;
        for (u32 i = 0; i < AREA_COUNT; i++) {
            if (i < tsCount) {
                gAreaTileSets[i] = ResolveTableOffset(kAreaTileSetOffsets[i]);
            }
            gAreaRoomMaps[i] = ResolveTableOffset(kAreaRoomMapOffsets[i]);
            gAreaTable[i] = ResolveTableOffset(kAreaTableOffsets[i]);
            gAreaTiles[i] = ResolveTableOffset(kAreaTilesOffsets[i]);
            /* gExitLists — now compile-time const from src/data/transitions.c, no ROM loading needed */
        }

        /* Second pass: resolve sub-arrays of 32-bit GBA pointers.
         * On GBA, sizeof(void*)==4 and these arrays are read directly.
         * On 64-bit PC, sizeof(void*)==8, so we must pre-resolve into
         * shadow arrays with native-width pointers.
         *
         * Instead of relying on room counts (which can reference garbage
         * tileSet_id values like 0xFFF3), we scan each sub-array to
         * determine its actual length: stop at the first entry that is
         * neither NULL nor a valid ROM pointer. */
        for (u32 i = 0; i < AREA_COUNT; i++) {
/* Helper: scan sub-array of packed 32-bit values at 'base',
 * counting entries that are 0 or valid ROM pointers.
 * Stops at first entry that is neither. Caps at MAX_ROOMS. */
#define SCAN_SUB_ARRAY(base, out_count)                                        \
    do {                                                                       \
        u8* _b = (u8*)(base);                                                  \
        (out_count) = 0;                                                       \
        for (u32 _j = 0; _j < MAX_ROOMS; _j++) {                               \
            u32 _v;                                                            \
            memcpy(&_v, _b + _j * 4, 4);                                       \
            if (_v == 0 || (_v >= 0x08000000u && _v < 0x08000000u + gRomSize)) \
                (out_count) = _j + 1;                                          \
            else                                                               \
                break;                                                         \
        }                                                                      \
    } while (0)

            u32 subCount;

            if (gAreaTileSets[i]) {
                SCAN_SUB_ARRAY(gAreaTileSets[i], subCount);
                if (subCount > 0) {
                    ResolveSubTable(gAreaTileSets[i], sTileSetsResolved[i], subCount);
                    gAreaTileSets[i] = (void*)sTileSetsResolved[i];
                }
            }

            if (gAreaRoomMaps[i]) {
                SCAN_SUB_ARRAY(gAreaRoomMaps[i], subCount);
                if (subCount > 0) {
                    ResolveSubTable(gAreaRoomMaps[i], sRoomMapsResolved[i], subCount);
                    gAreaRoomMaps[i] = (void*)sRoomMapsResolved[i];
                }
            }

            if (gAreaTable[i]) {
                SCAN_SUB_ARRAY(gAreaTable[i], subCount);
                if (subCount > 0) {
                    ResolveSubTable(gAreaTable[i], sAreaTableResolved[i], subCount);
                    gAreaTable[i] = (void*)sAreaTableResolved[i];
                }
            }

            /* gExitLists — compile-time const, no resolution needed */

#undef SCAN_SUB_ARRAY
        }

        fprintf(stderr, "Area data tables loaded (0x%X areas, 2-level pointers resolved).\n", AREA_COUNT);
    }

    fprintf(stderr, "ROM symbols resolved (%s: gGlobalGfxAndPalettes, gGfxGroups, gPaletteGroups, gFrameObjLists).\n",
            gRomRegion == ROM_REGION_EU ? "EU" : "USA");

    /* Initialize data stubs with ROM datas */
    {
        extern void Port_InitDataStubs(void);
        Port_InitDataStubs();
    }

    /* ---- Extract all known ROM regions to rom_data/ ---- */
    EnsureExtractDir();

    /* ROM header (for game code verification) */
    ExtractRegion(0, 0x200);

    /* Brightness/fade tables */
    ExtractRegion(R->fadeData, 0x1200 - R->fadeData);

    /* Pointer tables themselves */
    ExtractRegion(R->gfxGroups, R->gfxGroupsCount * 4);
    ExtractRegion(R->paletteGroups, R->paletteGroupsCount * 4);
    ExtractRegion(R->objPalettes, R->objPalettesCount * 4);
    ExtractRegion(R->frameObjLists, R->frameObjListsSize);

    /* Sprite pointer table + fixed type gfx data */
    ExtractRegion(R->spritePtrs, R->spritePtrsCount * 16);
    ExtractRegion(R->fixedTypeGfx, R->fixedTypeGfxCount * 4);

    /* Gfx+palette blob */
    if (gRomSize > R->gfxAndPalettes)
        ExtractRegion(R->gfxAndPalettes, gRomSize - R->gfxAndPalettes);

    /* Overlay size table */
    ExtractRegion(R->overlaySizeTable, 240);

    /* Area data pointer tables */
    ExtractRegion(R->areaRoomHeaders, AREA_COUNT * 4);
    ExtractRegion(R->areaTileSets, R->areaTileSetsCount * 4);
    ExtractRegion(R->areaRoomMaps, AREA_COUNT * 4);
    ExtractRegion(R->areaTable, AREA_COUNT * 4);
    ExtractRegion(R->areaTiles, AREA_COUNT * 4);
    ExtractRegion(R->exitLists, AREA_COUNT * 4);

    /* Font/text data region */
    {
        u32 textStart = R->translations;
        u32 textEnd = R->text094CE + 1378 + 0x100; /* conservative region */
        ExtractRegion(textStart, textEnd - textStart);
    }

    /* Extract gGfxGroups / gPaletteGroups referenced data */
    for (u32 i = 0; i < R->gfxGroupsCount; i++) {
        u32 ptr;
        memcpy(&ptr, &gRomData[R->gfxGroups + i * 4], 4);
        if (ptr >= 0x08000000u && ptr < 0x08000000u + gRomSize) {
            ExtractRegion(ptr - 0x08000000u, ROM_PAGE_SIZE);
        }
    }
    for (u32 i = 0; i < R->paletteGroupsCount; i++) {
        u32 ptr;
        memcpy(&ptr, &gRomData[R->paletteGroups + i * 4], 4);
        if (ptr >= 0x08000000u && ptr < 0x08000000u + gRomSize) {
            ExtractRegion(ptr - 0x08000000u, ROM_PAGE_SIZE);
        }
    }

    /* Extract area sub-table data (referenced by pointer tables) */
    for (u32 i = 0; i < AREA_COUNT; i++) {
        u32 tables[] = { R->areaRoomHeaders, R->areaTileSets, R->areaRoomMaps, R->areaTable, R->areaTiles };
        for (u32 t = 0; t < 5; t++) {
            if (t == 1 && i >= R->areaTileSetsCount)
                continue;
            u32 ptr;
            memcpy(&ptr, &gRomData[tables[t] + i * 4], 4);
            if (ptr >= 0x08000000u && ptr < 0x08000000u + gRomSize) {
                ExtractRegion(ptr - 0x08000000u, ROM_PAGE_SIZE);
            }
        }
    }

    /* Extract sprite pointer referenced data */
    {
        const u8* src = &gRomData[R->spritePtrs];
        for (u32 i = 0; i < R->spritePtrsCount; i++) {
            u32 ptrs[3];
            memcpy(&ptrs[0], src + i * 16 + 0, 4); /* animations */
            memcpy(&ptrs[1], src + i * 16 + 4, 4); /* frames */
            memcpy(&ptrs[2], src + i * 16 + 8, 4); /* ptr */
            for (int p = 0; p < 3; p++) {
                if (ptrs[p] >= 0x08000000u && ptrs[p] < 0x08000000u + gRomSize) {
                    ExtractRegion(ptrs[p] - 0x08000000u, ROM_PAGE_SIZE);
                }
            }
        }
    }

    Port_PrintRomAccessSummary();
}
