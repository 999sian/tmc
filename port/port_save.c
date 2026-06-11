/*
 * port_save.c — File-backed EEPROM emulation + multi-profile support.
 *
 * The GBA Minish Cap uses 8 KB EEPROM (1024 blocks of 8 bytes).
 * This module stores it in `tmc.sav` (the default profile) or
 * `tmc_<name>.sav` (named profile) next to the executable.
 *
 * On-disk format (mGBA-compatible)
 * --------------------------------
 * Files are stored in the byte order mGBA/VBA-M use for EEPROM saves:
 * each 8-byte block holds its 64-bit unit in wire-transmission order,
 * which is byte-reversed relative to the game's RAM buffer (the GBA
 * driver in src/eeprom.c shifts units out data[3]→data[0], MSB-first).
 * In memory we keep game-RAM order so the BIOS shims stay straight
 * memcpys; blocks are reversed on load/flush. A Minish Cap .sav from
 * mGBA drops in directly and vice versa. Legacy port saves (RAM order
 * on disk) are detected by the save signature and migrated once, with
 * the original kept as <name>.sav.bak.
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

/* ---- On-disk byte order -------------------------------------------------- */

/* First 8-byte block of every initialized TMC save ("AGBZELDA:..."), in
 * game-RAM order and in on-disk (mGBA wire) order. Same in all regions. */
#define EEPROM_SIG_RAM "AGBZELDA"

/* Reverse each 8-byte block in place: converts between game-RAM order
 * (in-memory) and mGBA/VBA-M wire order (on-disk). Involution: applying
 * it twice is the identity, so blank 0xFF images are unaffected. */
static void ReverseEepromBlocks(u8* buf) {
    for (int b = 0; b < EEPROM_SIZE; b += EEPROM_BLOCK) {
        for (int i = 0; i < EEPROM_BLOCK / 2; i++) {
            u8 t = buf[b + i];
            buf[b + i] = buf[b + EEPROM_BLOCK - 1 - i];
            buf[b + EEPROM_BLOCK - 1 - i] = t;
        }
    }
}

/* Write the in-memory EEPROM to f in on-disk order. 1 on full write. */
static int WriteEepromDiskOrder(FILE* f) {
    static u8 disk[EEPROM_SIZE];
    memcpy(disk, sEeprom, EEPROM_SIZE);
    ReverseEepromBlocks(disk);
    return fwrite(disk, 1, EEPROM_SIZE, f) == EEPROM_SIZE;
}

/* Write the in-memory EEPROM to `path` atomically: serialize to a sibling
 * temp file, flush it through to disk, then rename over the target. A crash
 * or power loss leaves either the old complete file or the new complete
 * file — never the truncated one that plain "wb" + fwrite produced (which
 * the next load treated as a blank save, silently wiping progress).
 * Returns 1 on success. */
static int WriteEepromAtomic(const char* path) {
    char tmp[SAVE_FILENAME_MAX + 8];
    if ((size_t)snprintf(tmp, sizeof(tmp), "%s.tmp", path) >= sizeof(tmp))
        return 0;

    FILE* f = fopen(tmp, "wb");
    if (!f)
        return 0;

    int ok = WriteEepromDiskOrder(f);
    if (ok) {
        fflush(f);
#ifdef _WIN32
        _commit(_fileno(f));
#else
        fsync(fileno(f));
#endif
    }
    if (fclose(f) != 0)
        ok = 0;
    if (!ok) {
        remove(tmp);
        return 0;
    }

#ifdef _WIN32
    if (!MoveFileExA(tmp, path, MOVEFILE_REPLACE_EXISTING)) {
        remove(tmp);
        return 0;
    }
#else
    if (rename(tmp, path) != 0) {
        remove(tmp);
        return 0;
    }
#endif
    return 1;
}

static void FlushEepromFile(void);
static int IsManagedProfilePath(const char* path);

/* ---- Persistence -------------------------------------------------------- */

static void LoadEepromFile(void) {
    FILE* f = fopen(sActivePath, "rb");
    if (!f) {
        memset(sEeprom, 0xFF, EEPROM_SIZE); /* blank EEPROM = 0xFF */
        fprintf(stderr, "[SAVE] No save file at %s, starting fresh.\n", sActivePath);
        return;
    }
    const size_t got = fread(sEeprom, 1, EEPROM_SIZE, f);
    fclose(f);
    if (got != EEPROM_SIZE) {
        fprintf(stderr, "[SAVE] ERROR: short read on %s (%zu/%d bytes), starting fresh.\n",
                sActivePath, got, EEPROM_SIZE);
        memset(sEeprom, 0xFF, EEPROM_SIZE); /* blank EEPROM = 0xFF */
        return;
    }
    if (memcmp(sEeprom, EEPROM_SIG_RAM, EEPROM_BLOCK) == 0) {
        /* Legacy port-format file (game-RAM order on disk). The buffer
         * is already in the order we keep in memory; keep the original
         * bytes as .bak, then rewrite the file in on-disk order. */
        char bak[SAVE_FILENAME_MAX + 4];
        snprintf(bak, sizeof(bak), "%s.bak", sActivePath);
        int backedUp = 0;
        FILE* bf = fopen(bak, "wb");
        if (bf) {
            backedUp = fwrite(sEeprom, 1, EEPROM_SIZE, bf) == EEPROM_SIZE;
            backedUp &= fclose(bf) == 0;
        }
        fprintf(stderr, "[SAVE] Migrating %s to mGBA byte order (backup: %s)%s.\n",
                sActivePath, bak, backedUp ? "" : " — BACKUP FAILED");
        sEepromDirty = 1;
        FlushEepromFile();
    } else {
        /* mGBA/VBA-M order — or blank/uninitialized, where reversal is
         * inconsequential. Convert to game-RAM order in memory. */
        ReverseEepromBlocks(sEeprom);
        fprintf(stderr, "[SAVE] Loaded save file: %s\n", sActivePath);
    }
}

static void FlushEepromFile(void) {
    if (!sEepromDirty)
        return;
    if (WriteEepromAtomic(sActivePath)) {
        sEepromDirty = 0;
    } else {
        /* Keep the dirty flag so the next flush retries. */
        fprintf(stderr, "[SAVE] ERROR: atomic write of %s failed; will retry.\n",
                sActivePath);
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
    } else if (!IsManagedProfilePath(path)) {
        /* The active-profile name comes from config.json (user-editable);
         * refuse anything outside the tmc.sav / tmc_<name>.sav lane so a
         * crafted value can't redirect saves elsewhere on disk. */
        fprintf(stderr, "[SAVE] Ignoring unmanaged save profile '%s'; using %s.\n",
                path, DEFAULT_SAVE_FILENAME);
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
    /* Only allow writing into the managed profile lane so the "save as"
     * UI can't be pointed at an arbitrary host path. */
    if (!IsManagedProfilePath(path)) return 0;
    /* Ensure EEPROM was loaded at least once so we have meaningful data
     * to copy. (Right after launch, before any read, sEeprom is zeroed.) */
    if (!sEepromInited) {
        LoadEepromFile();
        sEepromInited = 1;
    }
    return WriteEepromAtomic(path);
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

/* Returns 1 if the path is something we created and should be willing
 * to delete or rename — tmc.sav or tmc_<name>.sav. Anything else gets
 * refused so a stray ../../etc/passwd argument can't escape the
 * profile lane. */
static int IsManagedProfilePath(const char* path) {
    if (path == NULL || path[0] == '\0') return 0;
    if (strchr(path, '/') != NULL) return 0;
    if (strchr(path, '\\') != NULL) return 0;
    if (strstr(path, "..") != NULL) return 0;
    if (strcmp(path, DEFAULT_SAVE_FILENAME) == 0) return 1;
    if (strncmp(path, "tmc_", 4) != 0) return 0;
    const size_t len = strlen(path);
    if (len <= 8) return 0; /* "tmc_X.sav" minimum */
    if (strcmp(path + len - 4, ".sav") != 0) return 0;
    return 1;
}

/* Delete a profile file. Refuses if the profile is currently active
 * (caller should switch first) or if the name doesn't look like one
 * of ours. Returns 1 on success. */
int Port_Save_DeleteProfile(const char* path) {
    if (!IsManagedProfilePath(path)) return 0;
    if (strcmp(path, sActivePath) == 0) return 0; /* refuse to delete active */
    return remove(path) == 0 ? 1 : 0;
}

/* Rename a profile file. Both args must look like managed profile
 * names. The default tmc.sav cannot be renamed away (it's our fallback
 * for fresh installs). If renaming the active profile, also updates
 * sActivePath so subsequent reads/writes hit the new name. */
int Port_Save_RenameProfile(const char* oldPath, const char* newPath) {
    if (!IsManagedProfilePath(oldPath)) return 0;
    if (!IsManagedProfilePath(newPath)) return 0;
    if (strcmp(oldPath, DEFAULT_SAVE_FILENAME) == 0) return 0; /* don't rename default away */
    if (strcmp(oldPath, newPath) == 0) return 1; /* no-op */
    /* Refuse clobbering an existing file — fail-stop is safer than
     * silently replacing somebody else's save. */
    FILE* probe = fopen(newPath, "rb");
    if (probe) { fclose(probe); return 0; }
    if (rename(oldPath, newPath) != 0) return 0;
    if (strcmp(oldPath, sActivePath) == 0) {
        strncpy(sActivePath, newPath, SAVE_FILENAME_MAX - 1);
        sActivePath[SAVE_FILENAME_MAX - 1] = '\0';
    }
    return 1;
}
