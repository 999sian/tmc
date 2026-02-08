/*
 * port_rom.c — Load baserom.gba and resolve ROM-backed data symbols.
 *
 * GBA ROM lives at 0x08000000. All ROM data pointers in the original
 * binary are GBA addresses.  We load the ROM into gRomData[] and
 * translate pointer tables (gGfxGroups, gPaletteGroups) into native
 * pointers so that the decompiled C code can use them directly.
 *
 * ROM page extraction:
 *   At runtime, every ROM access goes through Port_LogRomAccess() which
 *   extracts the touched 4 KB page to rom_data/XXXXXXXX.bin.  On startup
 *   Port_LoadRom() first tries to reconstruct gRomData entirely from the
 *   extracted pages; if that fails it falls back to baserom.gba and then
 *   extracts the known data blocks immediately.
 */

#include "port_rom.h"
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

/* ---- USA ROM symbol offsets (addr - 0x08000000) ---- */
#define ROM_OFF_GFX_AND_PALETTES 0x5A2E80
#define ROM_OFF_GFX_GROUPS 0x100AA8     /* pointer table, 133 entries */
#define ROM_OFF_PALETTE_GROUPS 0x0FF850 /* pointer table, 208 entries */

#define GFX_GROUPS_COUNT 133
#define PALETTE_GROUPS_COUNT 208

#define ROM_OFF_OBJ_PALETTES 0x133368 /* gUnk_08133368, OBJ palette offset table */
#define OBJ_PALETTES_COUNT 360

#define ROM_OFF_FRAME_OBJ_LISTS 0x2F3D74 /* gFrameObjLists, 200045 bytes */
#define FRAME_OBJ_LISTS_SIZE 200045

#define ROM_OFF_FIXED_TYPE_GFX 0x132B30 /* gFixedTypeGfxData, 527 entries × 4 bytes */
#define FIXED_TYPE_GFX_COUNT 527

#define ROM_OFF_SPRITE_PTRS 0x0029B4 /* gSpritePtrs, 329 entries × 16 bytes */
#define SPRITE_PTRS_COUNT 329

extern u32 gFrameObjLists[];
extern u32 gUnk_08133368[];
extern SpritePtr gSpritePtrs[];
extern u32 gFixedTypeGfxData[];
extern void Port_LoadOverlayData(const u8* romData, u32 romSize);

/* Resolved pointer tables — defined as arrays to match extern declarations
 * in common.c:  extern const GfxItem* gGfxGroups[];
 *               extern const PaletteGroup* gPaletteGroups[];
 * Using void* element type since PaletteGroup/GfxItem are file-local typedefs. */
const u8* gGlobalGfxAndPalettes = NULL;
const void* gGfxGroups[GFX_GROUPS_COUNT];
const void* gPaletteGroups[PALETTE_GROUPS_COUNT];

/* Helper: resolve a 32-bit GBA ROM pointer to native */
static inline void* ResolveRomPtr(u32 gba_addr) {
    if (gba_addr == 0)
        return NULL;
    if (gba_addr >= 0x08000000u && gba_addr < 0x08000000u + gRomSize)
        return &gRomData[gba_addr - 0x08000000u];
    fprintf(stderr, "ResolveRomPtr: address 0x%08X is outside ROM\n", gba_addr);
    return NULL;
}

void Port_LoadRom(const char* path) {
    memset(sExtractedPages, 0, sizeof(sExtractedPages));

    /* ---- Step 1: try loading from rom_data/ extracted pages ---- */
    int pagesLoaded = LoadExtractedPages();
    if (pagesLoaded > 0) {
        fprintf(stderr, "ROM data: loaded %d extracted pages from " ROM_EXTRACT_DIR "/\n", pagesLoaded);
    }

    /* ---- Step 2: try baserom.gba to fill in gaps / verify ---- */
    FILE* f = fopen(path, "rb");
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
        fprintf(stderr, "ROM loaded: %u bytes (0x%X) from %s\n", gRomSize, gRomSize, path);
    } else if (pagesLoaded > 0) {
        fprintf(stderr, "ROM file '%s' not found, using %d extracted pages.\n", path, pagesLoaded);
    } else {
        fprintf(stderr, "ERROR: Cannot open ROM file: %s\n", path);
        fprintf(stderr, "  Place baserom.gba (USA) in the working directory,\n");
        fprintf(stderr, "  or provide extracted pages in " ROM_EXTRACT_DIR "/\n");
        abort();
    }

    if (!gRomData || gRomSize == 0) {
        fprintf(stderr, "ERROR: No ROM data available.\n");
        abort();
    }

    /* Verify game code (only if we have the header) */
    if (gRomSize > 0xB0 && memcmp(&gRomData[0xAC], "BZME", 4) != 0) {
        fprintf(stderr, "WARNING: ROM game code is not 'BZME' (USA). Got '%.4s'\n", &gRomData[0xAC]);
    }

    /* gGlobalGfxAndPalettes — huge palette/gfx blob */
    gGlobalGfxAndPalettes = &gRomData[ROM_OFF_GFX_AND_PALETTES];

    /* gGfxGroups — array of ROM pointers → native pointers to GfxItem arrays */
    for (int i = 0; i < GFX_GROUPS_COUNT; i++) {
        u32 ptr;
        memcpy(&ptr, &gRomData[ROM_OFF_GFX_GROUPS + i * 4], 4);
        ((const void**)gGfxGroups)[i] = ResolveRomPtr(ptr);
    }

    /* gPaletteGroups — array of ROM pointers → native PaletteGroup arrays */
    for (int i = 0; i < PALETTE_GROUPS_COUNT; i++) {
        u32 ptr;
        memcpy(&ptr, &gRomData[ROM_OFF_PALETTE_GROUPS + i * 4], 4);
        ((const void**)gPaletteGroups)[i] = ResolveRomPtr(ptr);
        if (i < 10) {
            fprintf(stderr, "  PaletteGroup[%d]: ROM ptr = 0x%08X -> native %p\n", i, ptr, gPaletteGroups[i]);
        }
    }

    /* gFrameObjLists — sprite frame data (self-relative offset table + data) */
    memcpy(gFrameObjLists, &gRomData[ROM_OFF_FRAME_OBJ_LISTS], FRAME_OBJ_LISTS_SIZE);
    fprintf(stderr, "gFrameObjLists loaded (%d bytes from ROM offset 0x%X).\n", FRAME_OBJ_LISTS_SIZE,
            ROM_OFF_FRAME_OBJ_LISTS);

    /* gUnk_08133368 — OBJ palette offset table (used by LoadObjPalette) */
    memcpy(gUnk_08133368, &gRomData[ROM_OFF_OBJ_PALETTES], OBJ_PALETTES_COUNT * 4);
    fprintf(stderr, "gUnk_08133368 loaded (%d entries from ROM offset 0x%X).\n", OBJ_PALETTES_COUNT,
            ROM_OFF_OBJ_PALETTES);

    /* gFixedTypeGfxData — offset+size table for fixed-type gfx (not pointers, just encoded u32 values) */
    memcpy(gFixedTypeGfxData, &gRomData[ROM_OFF_FIXED_TYPE_GFX], FIXED_TYPE_GFX_COUNT * 4);
    fprintf(stderr, "gFixedTypeGfxData loaded (%d entries from ROM offset 0x%X).\n", FIXED_TYPE_GFX_COUNT,
            ROM_OFF_FIXED_TYPE_GFX);

    /* gSpritePtrs — array of SpritePtr {animations*, frames*, ptr*, pad}
     * Each pointer field is a GBA ROM address that needs resolution. */
    {
        const u8* src = &gRomData[ROM_OFF_SPRITE_PTRS];
        for (int i = 0; i < SPRITE_PTRS_COUNT; i++) {
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
        fprintf(stderr, "gSpritePtrs loaded (%d entries from ROM offset 0x%X, pointers resolved).\n", SPRITE_PTRS_COUNT,
                ROM_OFF_SPRITE_PTRS);
    }

    /* Load overlay data tables (size/clipping table etc.) */
    Port_LoadOverlayData(gRomData, gRomSize);

    fprintf(stderr, "ROM symbols resolved (gGlobalGfxAndPalettes, gGfxGroups, gPaletteGroups, gFrameObjLists).\n");

    /* ---- Extract all known ROM regions to rom_data/ ---- */
    EnsureExtractDir();

    /* ROM header (for game code verification) */
    ExtractRegion(0, 0x200);

    /* Brightness/fade tables (gUnk_08000F54, used by Port_MakeFadeBuff256) */
    ExtractRegion(0x000F54, 0x1200 - 0x0F54);

    /* Pointer tables themselves */
    ExtractRegion(ROM_OFF_GFX_GROUPS, GFX_GROUPS_COUNT * 4);
    ExtractRegion(ROM_OFF_PALETTE_GROUPS, PALETTE_GROUPS_COUNT * 4);
    ExtractRegion(ROM_OFF_OBJ_PALETTES, OBJ_PALETTES_COUNT * 4);
    ExtractRegion(ROM_OFF_FRAME_OBJ_LISTS, FRAME_OBJ_LISTS_SIZE);

    /* Global gfx+palette blob — can be very large, extract all of it
     * (from 0x5A2E80 to end of ROM, or a reasonable chunk). */
    if (gRomSize > ROM_OFF_GFX_AND_PALETTES)
        ExtractRegion(ROM_OFF_GFX_AND_PALETTES, gRomSize - ROM_OFF_GFX_AND_PALETTES);

    /* Overlay size table */
    ExtractRegion(0x0B2BE8, 240);

    /* Follow all pointers in gGfxGroups and gPaletteGroups to extract
     * the data they reference as well */
    for (int i = 0; i < GFX_GROUPS_COUNT; i++) {
        u32 ptr;
        memcpy(&ptr, &gRomData[ROM_OFF_GFX_GROUPS + i * 4], 4);
        if (ptr >= 0x08000000u && ptr < 0x08000000u + gRomSize) {
            /* Extract a generous region — GfxItem arrays vary in size,
             * but 4 KB pages will cover them. */
            ExtractRegion(ptr - 0x08000000u, ROM_PAGE_SIZE);
        }
    }
    for (int i = 0; i < PALETTE_GROUPS_COUNT; i++) {
        u32 ptr;
        memcpy(&ptr, &gRomData[ROM_OFF_PALETTE_GROUPS + i * 4], 4);
        if (ptr >= 0x08000000u && ptr < 0x08000000u + gRomSize) {
            ExtractRegion(ptr - 0x08000000u, ROM_PAGE_SIZE);
        }
    }

    Port_PrintRomAccessSummary();
}
