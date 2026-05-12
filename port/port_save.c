/*
 * port_save.c — File-backed EEPROM emulation + multi-profile support.
 *
 * The GBA Minish Cap uses 8 KB EEPROM (1024 blocks of 8 bytes).
 * This module stores it in `tmc.sav` (the default profile) or
 * `tmc_<name>.sav` (named profile) next to the executable.
 *
 * Profile model
 * -------------
 * A *profile* is one named save file. The active profile's filename is
 * persisted in config.json so it sticks across launches. The first run
 * uses `tmc.sav` for backwards compatibility with existing installs.
 *
 * Switching the active profile mid-game is allowed: the next time the
 * game reads EEPROM (e.g. when the user returns to the file-select
 * screen) it will see the new profile's data. The current in-memory
 * `gSave` does NOT auto-reload — players who want to be loading from
 * the new profile should return to title and pick a save slot.
 *
 * Implements the four EEPROM BIOS functions:
 *   EEPROMConfigure(u16 type)
 *   EEPROMRead(u16 block, u16* dest)
 *   EEPROMWrite0_8k_Check(u16 block, const u16* src)
 *   EEPROMCompare(u16 block, const u16* src)
 *
 * Plus a small profile-management API consumed by port_debug_menu.cpp.
 */

#include "port_types.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#include <io.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#define EEPROM_SIZE 8192                           /* 8 KB */
#define EEPROM_BLOCK 8                             /* 8 bytes per block */
#define EEPROM_BLOCKS (EEPROM_SIZE / EEPROM_BLOCK) /* 1024 */
#define DEFAULT_SAVE_FILENAME "tmc.sav"
#define SAVE_FILENAME_MAX 64

static u8 sEeprom[EEPROM_SIZE];
static int sEepromDirty = 0; /* set on write, cleared on flush */
static int sEepromInited = 0;
static char sActivePath[SAVE_FILENAME_MAX] = DEFAULT_SAVE_FILENAME;

/* ---- Persistence -------------------------------------------------------- */

static void LoadEepromFile(void) {
    FILE* f = fopen(sActivePath, "rb");
    if (f) {
        fread(sEeprom, 1, EEPROM_SIZE, f);
        fclose(f);
        fprintf(stderr, "[SAVE] Loaded save file: %s\n", sActivePath);
    } else {
        memset(sEeprom, 0xFF, EEPROM_SIZE); /* blank EEPROM = 0xFF */
        fprintf(stderr, "[SAVE] No save file at %s, starting fresh.\n", sActivePath);
    }
}

static void FlushEepromFile(void) {
    if (!sEepromDirty)
        return;
    FILE* f = fopen(sActivePath, "wb");
    if (f) {
        fwrite(sEeprom, 1, EEPROM_SIZE, f);
        fclose(f);
        sEepromDirty = 0;
    } else {
        fprintf(stderr, "[SAVE] ERROR: Could not write %s\n", sActivePath);
    }
}

/* ---- EEPROM BIOS API ---------------------------------------------------- */

u16 EEPROMConfigure(u16 type) {
    if (!sEepromInited) {
        LoadEepromFile();
        sEepromInited = 1;
    }
    /* type = 0x40 → 8 KB, type = 4 → 512 B. We always emulate 8 KB. */
    (void)type;
    return 0; /* success */
}

u16 EEPROMRead(u16 block, u16* dest) {
    if (!sEepromInited) {
        LoadEepromFile();
        sEepromInited = 1;
    }
    if (block >= EEPROM_BLOCKS)
        return 0x80FF; /* EEPROM_OUT_OF_RANGE */

    memcpy(dest, &sEeprom[block * EEPROM_BLOCK], EEPROM_BLOCK);
    return 0; /* success */
}

u16 EEPROMWrite0_8k_Check(u16 block, const u16* src) {
    if (!sEepromInited) {
        LoadEepromFile();
        sEepromInited = 1;
    }
    if (block >= EEPROM_BLOCKS)
        return 0x80FF; /* EEPROM_OUT_OF_RANGE */

    memcpy(&sEeprom[block * EEPROM_BLOCK], src, EEPROM_BLOCK);
    sEepromDirty = 1;

    /* Flush to disk immediately to avoid data loss on crash */
    FlushEepromFile();
    return 0; /* success */
}

u16 EEPROMCompare(u16 block, const u16* src) {
    if (!sEepromInited) {
        LoadEepromFile();
        sEepromInited = 1;
    }
    if (block >= EEPROM_BLOCKS)
        return 0x80FF; /* EEPROM_OUT_OF_RANGE */

    if (memcmp(&sEeprom[block * EEPROM_BLOCK], src, EEPROM_BLOCK) != 0)
        return 0x8000; /* EEPROM_COMPARE_FAILED */

    return 0; /* match */
}

/* ---- Profile management ------------------------------------------------- */

/* Public: invoked by port_main.c once at startup to honour the persisted
 * choice from config.json. Quietly no-ops on a missing/null path so the
 * default tmc.sav stays in effect. */
void Port_Save_SetActivePath(const char* path) {
    if (path == NULL || path[0] == '\0') {
        path = DEFAULT_SAVE_FILENAME;
    }
    /* If the EEPROM was already loaded under the old path, flush it
     * first so the user doesn't lose pending writes when switching. */
    if (sEepromInited && sEepromDirty) {
        FlushEepromFile();
    }
    strncpy(sActivePath, path, sizeof(sActivePath) - 1);
    sActivePath[sizeof(sActivePath) - 1] = '\0';
    /* Force a reload on next access so any read after this point hits
     * the new file. */
    sEepromInited = 0;
    sEepromDirty = 0;
}

const char* Port_Save_GetActivePath(void) {
    return sActivePath;
}

/* Snapshot the in-memory EEPROM into a named profile file without
 * changing the active profile. Useful for "Save current state as a new
 * profile" — keep playing in the current profile while the named copy
 * captures right-now state. Returns 0 on failure. */
int Port_Save_SaveAsProfile(const char* path) {
    if (path == NULL || path[0] == '\0') return 0;
    /* Ensure EEPROM was loaded at least once so we have meaningful data
     * to copy. (Right after launch, before any read, sEeprom is zeroed.) */
    if (!sEepromInited) {
        LoadEepromFile();
        sEepromInited = 1;
    }
    FILE* f = fopen(path, "wb");
    if (!f) return 0;
    const size_t got = fwrite(sEeprom, 1, EEPROM_SIZE, f);
    fclose(f);
    return got == EEPROM_SIZE ? 1 : 0;
}

/* List `tmc.sav` and `tmc_*.sav` files in cwd. Caller passes a fixed-
 * size [count][SAVE_FILENAME_MAX] char buffer; we fill up to `max` entries
 * and return the count written. Order is filesystem-defined. */
int Port_Save_ListProfiles(char out[][SAVE_FILENAME_MAX], int max) {
    int n = 0;
#ifdef _WIN32
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA("tmc*.sav", &fd);
    if (h == INVALID_HANDLE_VALUE) return 0;
    do {
        if (n >= max) break;
        const char* name = fd.cFileName;
        if (strcmp(name, DEFAULT_SAVE_FILENAME) == 0 ||
            strncmp(name, "tmc_", 4) == 0) {
            strncpy(out[n], name, SAVE_FILENAME_MAX - 1);
            out[n][SAVE_FILENAME_MAX - 1] = '\0';
            n++;
        }
    } while (FindNextFileA(h, &fd));
    FindClose(h);
#else
    DIR* d = opendir(".");
    if (!d) return 0;
    struct dirent* ent;
    while ((ent = readdir(d)) != NULL) {
        if (n >= max) break;
        const char* name = ent->d_name;
        const size_t len = strlen(name);
        if (len < 4) continue;
        /* Match `tmc.sav` exactly OR `tmc_*.sav`. */
        if (strcmp(name, DEFAULT_SAVE_FILENAME) == 0 ||
            (strncmp(name, "tmc_", 4) == 0 && len > 8 &&
             strcmp(name + len - 4, ".sav") == 0)) {
            strncpy(out[n], name, SAVE_FILENAME_MAX - 1);
            out[n][SAVE_FILENAME_MAX - 1] = '\0';
            n++;
        }
    }
    closedir(d);
#endif
    return n;
}

int Port_Save_FilenameMax(void) { return SAVE_FILENAME_MAX; }
