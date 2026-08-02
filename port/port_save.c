/*
 * port_save.c — File-backed EEPROM emulation for the PC port.
 *
 * The GBA Minish Cap uses 8 KB EEPROM (1024 blocks of 8 bytes).
 * This module stores the EEPROM data in "tmc.sav" beside the executable
 * (see ResolveSavePath for the cwd-relative compatibility fallback).
 *
 * Implements the four EEPROM BIOS functions:
 *   EEPROMConfigure(u16 type)
 *   EEPROMRead(u16 block, u16* dest)
 *   EEPROMWrite0_8k_Check(u16 block, const u16* src)
 *   EEPROMCompare(u16 block, const u16* src)
 */

#include "port_types.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "port_rom.h" /* Port_ResolveExePath, PORT_PATH_MAX */
#ifdef _WIN32
#include <windows.h> /* MoveFileExA — rename() refuses to clobber on Win32 */
#endif

#define EEPROM_SIZE 8192                           /* 8 KB */
#define EEPROM_BLOCK 8                             /* 8 bytes per block */
#define EEPROM_BLOCKS (EEPROM_SIZE / EEPROM_BLOCK) /* 1024 */
#define SAVE_FILENAME "tmc.sav"

static u8 sEeprom[EEPROM_SIZE];
static int sEepromDirty = 0;     /* set on write, cleared on successful flush */
static int sEepromInited = 0;
static int sLastWrittenBlock = -1; /* tail of the run being buffered, -1 = none */
static char sSavePath[PORT_PATH_MAX];

/* ---- Persistence -------------------------------------------------------- */

/*
 * Pick the save path once. The save belongs beside the executable, matching the
 * ROM's exe-relative lookup: a cwd-relative save means a shortcut launch finds
 * the ROM but writes progress elsewhere, and two launch directories give the
 * player two divergent saves.
 *
 * Compatibility shim: if there is no save beside the exe but a cwd-relative one
 * exists, keep using that so an in-place upgrade doesn't strand old progress.
 */
static void ResolveSavePath(void) {
    FILE* f;

    Port_ResolveExePath(SAVE_FILENAME, sSavePath, sizeof(sSavePath));
    f = fopen(sSavePath, "rb");
    if (f) {
        fclose(f);
        return;
    }
    f = fopen(SAVE_FILENAME, "rb");
    if (f) {
        fclose(f);
        snprintf(sSavePath, sizeof(sSavePath), "%s", SAVE_FILENAME);
    }
}

/*
 * Replace `path` with `size` bytes of `data` so that a crash at any instant
 * leaves either the complete old file or the complete new one, never a
 * half-written save. Returns 1 on success.
 */
static int WriteFileAtomic(const char* path, const void* data, size_t size) {
    char tmp[PORT_PATH_MAX + 8];
    FILE* f;
    int ok, replaced;

    snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    f = fopen(tmp, "wb");
    if (!f)
        return 0;

    /* A full disk surfaces at fwrite/fflush/fclose, never at fopen. */
    ok = fwrite(data, 1, size, f) == size;
    if (fflush(f) != 0)
        ok = 0;
    if (fclose(f) != 0)
        ok = 0;
    if (!ok) {
        remove(tmp);
        return 0;
    }

#ifdef _WIN32
    replaced = MoveFileExA(tmp, path, MOVEFILE_REPLACE_EXISTING) != 0;
#else
    replaced = rename(tmp, path) == 0;
#endif
    if (!replaced) {
        remove(tmp);
        return 0;
    }
    return 1;
}

static void LoadEepromFile(void) {
    FILE* f;
    size_t got;

    ResolveSavePath();

    /* Erased EEPROM reads as 0xFF, and the fill has to happen before the read:
     * a short file used to leave sEeprom's static zero tail in place, and 0x00
     * is indistinguishable from real data, so every affected checksummed slot
     * verified as *empty* rather than corrupt — silent progress loss. */
    memset(sEeprom, 0xFF, EEPROM_SIZE);

    f = fopen(sSavePath, "rb");
    if (!f) {
        fprintf(stderr, "[SAVE] No save file found, starting fresh.\n");
        return;
    }
    got = fread(sEeprom, 1, EEPROM_SIZE, f);
    fclose(f);
    if (got != EEPROM_SIZE) {
        /* Keep the erased fill rather than a partial image, and leave the file
         * untouched on disk (nothing is dirty yet) so it stays recoverable. */
        memset(sEeprom, 0xFF, EEPROM_SIZE);
        fprintf(stderr, "[SAVE] ERROR: %s is truncated (%u of %d bytes), ignoring it.\n", sSavePath,
                (unsigned)got, EEPROM_SIZE);
        return;
    }
    fprintf(stderr, "[SAVE] Loaded save file: %s\n", sSavePath);
}

/* Returns 1 once sEeprom is safely on disk (or was already clean). */
static int FlushEepromFile(void) {
    if (!sEepromDirty)
        return 1;
    if (!WriteFileAtomic(sSavePath, sEeprom, EEPROM_SIZE)) {
        fprintf(stderr, "[SAVE] ERROR: Could not write %s\n", sSavePath);
        return 0;
    }
    sEepromDirty = 0;
    return 1;
}

static void FlushEepromAtExit(void) {
    FlushEepromFile();
}

static void EnsureEepromInited(void) {
    if (sEepromInited)
        return;
    sEepromInited = 1;
    LoadEepromFile();
    /* Guaranteed final flush — the tail of the last write run lives only in
     * sEeprom until something forces it out (see EEPROMWrite0_8k_Check). */
    atexit(FlushEepromAtExit);
}

/* ---- EEPROM BIOS API ---------------------------------------------------- */

u16 EEPROMConfigure(u16 type) {
    EnsureEepromInited();
    /* type = 0x40 → 8 KB, type = 4 → 512 B. We always emulate 8 KB. */
    return 0; /* success */
}

u16 EEPROMRead(u16 block, u16* dest) {
    EnsureEepromInited();
    FlushEepromFile(); /* bound the tail-of-run window; no-op when clean */
    if (block >= EEPROM_BLOCKS)
        return 0x80FF; /* EEPROM_OUT_OF_RANGE */

    memcpy(dest, &sEeprom[block * EEPROM_BLOCK], EEPROM_BLOCK);
    return 0; /* success */
}

u16 EEPROMWrite0_8k_Check(u16 block, const u16* src) {
    EnsureEepromInited();
    if (block >= EEPROM_BLOCKS)
        return 0x80FF; /* EEPROM_OUT_OF_RANGE */

    /* DataWrite() walks a save copy 8 bytes at a time, strictly ascending, and
     * the engine commits two copies plus two status blocks — ~300 blocks per
     * save. Flushing each one meant ~300 full-image rewrites, so a run is
     * buffered in sEeprom and written out when the block sequence breaks:
     * 4 atomic replaces per save instead of 300.
     *
     * Tradeoff: the final run lives only in memory until the next EEPROM read
     * or atexit(). A hard crash in that window loses just that run, and since
     * the engine writes two independent checksummed copies, the earlier one is
     * already complete on disk — the same guarantee real EEPROM gives on a
     * power cut. */
    if (block != sLastWrittenBlock + 1 && !FlushEepromFile())
        return 0x8000; /* EEPROM_COMPARE_FAILED — hardware's "write didn't stick" */

    memcpy(&sEeprom[block * EEPROM_BLOCK], src, EEPROM_BLOCK);
    sEepromDirty = 1;
    sLastWrittenBlock = block;
    return 0; /* success */
}

u16 EEPROMCompare(u16 block, const u16* src) {
    EnsureEepromInited();
    FlushEepromFile(); /* no-op when clean; see EEPROMRead */
    if (block >= EEPROM_BLOCKS)
        return 0x80FF; /* EEPROM_OUT_OF_RANGE */

    if (memcmp(&sEeprom[block * EEPROM_BLOCK], src, EEPROM_BLOCK) != 0)
        return 0x8000; /* EEPROM_COMPARE_FAILED */

    return 0; /* match */
}
