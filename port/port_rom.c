/*
 * port_rom.c — Load baserom.gba and resolve ROM data symbols.
 *
 * GBA ROM at 0x08000000. Pointer tables (gGfxGroups, gPaletteGroups, etc.)
 * are translated to native pointers. ROM pages (4 KB) are extracted to
 * rom_data/ so the game can run without the full ROM after first boot.
 */

#include "port_rom.h"
#include "port_config.h"
#include "structures.h"
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

/* Load all rom_data/*.bin files back into gRomData.
 * Returns the number of pages loaded. */
static int LoadExtractedPages(void) {
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
    WIN32_FIND_DATAA fd;
    HANDLE hFind = FindFirstFileA(ROM_EXTRACT_DIR "\\*.bin", &fd);
    if (hFind == INVALID_HANDLE_VALUE)
        return 0;
    do {
        u32 offset = 0;
        if (sscanf(fd.cFileName, "%08X.bin", &offset) != 1)
            continue;
        if (offset >= gRomSize)
            continue;

        char path[256];
        snprintf(path, sizeof(path), ROM_EXTRACT_DIR "\\%s", fd.cFileName);
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
    DIR* dir = opendir(ROM_EXTRACT_DIR);
    if (!dir)
        return 0;
    struct dirent* ent;
    while ((ent = readdir(dir)) != NULL) {
        u32 offset = 0;
        if (sscanf(ent->d_name, "%08X.bin", &offset) != 1)
            continue;
        if (offset >= gRomSize)
            continue;

        char path[256];
        snprintf(path, sizeof(path), ROM_EXTRACT_DIR "/%s", ent->d_name);
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
    closedir(dir);
#endif

    return loaded;
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

extern u32 gFrameObjLists[];
extern u32 gUnk_08133368[];
extern SpritePtr gSpritePtrs[];
extern u32 gFixedTypeGfxData[];
extern void Port_LoadOverlayData(const u8* romData, u32 romSize, u32 overlayOffset);

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

/* ---- Helper: try to open a ROM from a list of candidate paths ---- */
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

void Port_LoadRom(const char* path) {
    memset(sExtractedPages, 0, sizeof(sExtractedPages));

    /* ---- Step 1: try loading from rom_data/ extracted pages ---- */
    int pagesLoaded = LoadExtractedPages();
    if (pagesLoaded > 0) {
        fprintf(stderr, "ROM data: loaded %d extracted pages from " ROM_EXTRACT_DIR "/\n", pagesLoaded);
    }

    /* ---- Step 2: try ROM files (USA first, then EU) ---- */
    const char* romCandidates[] = {
        path,                   /* user-provided path */
        "baserom.gba",          /* USA default */
        "baserom_eu.gba",       /* EU default */
        "build/pc/baserom.gba", /* copied to build dir */
        "build/pc/baserom_eu.gba",
        "tmc.gba", /* common names */
        "tmc_eu.gba",
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
        fprintf(stderr, "ROM loaded: %u bytes (0x%X) from %s\n", gRomSize, gRomSize, usedPath);
    } else if (pagesLoaded > 0) {
        fprintf(stderr, "No ROM file found, using %d extracted pages.\n", pagesLoaded);
    } else {
        fprintf(stderr, "ERROR: Cannot open any ROM file.\n");
        fprintf(stderr, "  Place baserom.gba (USA) or baserom_eu.gba (EU) in the working directory,\n");
        fprintf(stderr, "  or provide extracted pages in " ROM_EXTRACT_DIR "/\n");
        abort();
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

    /* ---- Step 4: resolve ROM symbols using detected offsets ---- */

    /* gGlobalGfxAndPalettes — huge palette/gfx blob */
    gGlobalGfxAndPalettes = &gRomData[R->gfxAndPalettes];

    /* gGfxGroups — array of ROM pointers → native pointers to GfxItem arrays */
    for (u32 i = 0; i < R->gfxGroupsCount; i++) {
        u32 ptr;
        memcpy(&ptr, &gRomData[R->gfxGroups + i * 4], 4);
        ((const void**)gGfxGroups)[i] = ResolveRomPtr(ptr);
    }

    /* gPaletteGroups — array of ROM pointers → native PaletteGroup arrays */
    for (u32 i = 0; i < R->paletteGroupsCount; i++) {
        u32 ptr;
        memcpy(&ptr, &gRomData[R->paletteGroups + i * 4], 4);
        ((const void**)gPaletteGroups)[i] = ResolveRomPtr(ptr);
        if (i < 10) {
            fprintf(stderr, "  PaletteGroup[%d]: ROM ptr = 0x%08X -> native %p\n", i, ptr, gPaletteGroups[i]);
        }
    }

    /* gFrameObjLists — sprite frame data (self-relative offset table + data) */
    memcpy(gFrameObjLists, &gRomData[R->frameObjLists], R->frameObjListsSize);
    fprintf(stderr, "gFrameObjLists loaded (%u bytes from ROM offset 0x%X).\n", R->frameObjListsSize, R->frameObjLists);

    /* OBJ palette offset table (used by LoadObjPalette) */
    memcpy(gUnk_08133368, &gRomData[R->objPalettes], R->objPalettesCount * 4);
    fprintf(stderr, "OBJ palettes loaded (%u entries from ROM offset 0x%X).\n", R->objPalettesCount, R->objPalettes);

    /* gFixedTypeGfxData — offset+size table for fixed-type gfx */
    memcpy(gFixedTypeGfxData, &gRomData[R->fixedTypeGfx], R->fixedTypeGfxCount * 4);
    fprintf(stderr, "gFixedTypeGfxData loaded (%u entries from ROM offset 0x%X).\n", R->fixedTypeGfxCount,
            R->fixedTypeGfx);

    /* gSpritePtrs — array of SpritePtr {animations*, frames*, ptr*, pad}
     * Each pointer field is a GBA ROM address that needs resolution. */
    {
        const u8* src = &gRomData[R->spritePtrs];
        for (u32 i = 0; i < R->spritePtrsCount; i++) {
            u32 anim, frames, ptr, pad;
            memcpy(&anim, src + i * 16 + 0, 4);
            memcpy(&frames, src + i * 16 + 4, 4);
            memcpy(&ptr, src + i * 16 + 8, 4);
            memcpy(&pad, src + i * 16 + 12, 4);
            gSpritePtrs[i].animations = ResolveRomPtr(anim);
            gSpritePtrs[i].frames = (SpriteFrame*)ResolveRomPtr(frames);
            gSpritePtrs[i].ptr = ResolveRomPtr(ptr);
            gSpritePtrs[i].pad = pad;
        }
        fprintf(stderr, "gSpritePtrs loaded (%u entries from ROM offset 0x%X, pointers resolved).\n",
                R->spritePtrsCount, R->spritePtrs);
    }

    /* Font/text data tables */
    memcpy(gUnk_08109244, &gRomData[R->text09244], 4);
    memcpy(gUnk_0810926C, &gRomData[R->text0926C], 64);
    memcpy(gUnk_081092D4, &gRomData[R->text092D4], 346);
    memcpy(gUnk_0810942E, &gRomData[R->text0942E], 160);
    memcpy(gUnk_081094CE, &gRomData[R->text094CE], 1378);

    /* UI data */
    {
        extern u8 gUnk_080C9044[];
        memcpy(gUnk_080C9044, &gRomData[R->uiData], 8);
    }

    /* UI element definitions (native function pointers) */
    {
        extern void Port_InitUIElementDefinitions(void);
        Port_InitUIElementDefinitions();
    }

    /* gTranslations — 7 ROM pointers */
    for (int i = 0; i < 7; i++) {
        u32 ptr;
        memcpy(&ptr, &gRomData[R->translations + i * 4], 4);
        gTranslations[i] = ResolveRomPtr(ptr);
    }
    fprintf(stderr, "gTranslations loaded (7 entries, pointers resolved).\n");

    /* gUnk_08109230 — 5 ROM pointers */
    for (int i = 0; i < 5; i++) {
        u32 ptr;
        memcpy(&ptr, &gRomData[R->text09230 + i * 4], 4);
        gUnk_08109230[i] = ResolveRomPtr(ptr);
    }

    /* gUnk_08109248 — 9 ROM pointers (font glyph data) */
    for (int i = 0; i < 9; i++) {
        u32 ptr;
        memcpy(&ptr, &gRomData[R->text09248 + i * 4], 4);
        gUnk_08109248[i] = ResolveRomPtr(ptr);
    }
    fprintf(stderr, "gUnk_08109248 font tables loaded (9 entries, pointers resolved).\n");

    /* gUnk_081092AC — 10 ROM pointers (border glyphs) */
    for (int i = 0; i < 10; i++) {
        u32 ptr;
        memcpy(&ptr, &gRomData[R->text092AC + i * 4], 4);
        gUnk_081092AC[i] = ResolveRomPtr(ptr);
    }
    fprintf(stderr, "gUnk_081092AC border tables loaded (10 entries, pointers resolved).\n");

    /* Load overlay data tables (size/clipping table etc.) */
    Port_LoadOverlayData(gRomData, gRomSize, R->overlaySizeTable);

    fprintf(stderr, "ROM symbols resolved (%s: gGlobalGfxAndPalettes, gGfxGroups, gPaletteGroups, gFrameObjLists).\n",
            gRomRegion == ROM_REGION_EU ? "EU" : "USA");

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

    /* Gfx+palette blob */
    if (gRomSize > R->gfxAndPalettes)
        ExtractRegion(R->gfxAndPalettes, gRomSize - R->gfxAndPalettes);

    /* Overlay size table */
    ExtractRegion(R->overlaySizeTable, 240);

    /* Font/text data region */
    {
        u32 textEnd = R->translations + 0x800; /* conservative region */
        ExtractRegion(R->translations, textEnd - R->translations);
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

    Port_PrintRomAccessSummary();
}
