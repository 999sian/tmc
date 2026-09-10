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
#include "region.h"
#include <errno.h>
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
/* >0 while inside a game save transaction (HandleSaveInProgress). Block
 * writes inside a transaction only mark dirty; the single atomic flush
 * happens at Port_Save_EndTransaction(). Outside a transaction (boot-time
 * InitSaveData, status repair) writes keep the old flush-immediately
 * behavior. */
static int sSaveTxnDepth = 0;
/* 1 after a failed flush, reset on success — so a persistent ENOSPC/EIO
 * logs once per failure burst instead of once per write. */
static int sFlushFailedLast = 0;
static int sEepromInited = 0;
/* Existing malformed/wrong-region files are readable only as an unavailable
 * EEPROM. Never let InitSaveData turn them into a fresh save implicitly. The
 * user can still explicitly clear/switch the profile through the existing UI. */
static int sEepromWriteBlocked = 0;
static char sActivePath[SAVE_FILENAME_MAX] = DEFAULT_SAVE_FILENAME;
/* 1 once the user has explicitly chosen a named profile (config.json), so the
 * per-region default below must NOT override their choice. 0 in the default
 * case, where the multi-region build isolates each region into its own file. */
static int sExplicitProfile = 0;

/* ---- On-disk byte order -------------------------------------------------- */

/* First 8-byte block of every initialized TMC save ("AGBZELDA:..."), in
 * game-RAM order and in on-disk (mGBA wire) order. Same in all regions. */
#define EEPROM_SIG_RAM "AGBZELDA"
/* Full 0x20-byte signature record (src/save.c sSignatureLong); USA vs EU/JP. */
#define EEPROM_SIGNATURE_USA "AGBZELDA:THE MINISH CAP:ZELDA 5"
#define EEPROM_SIGNATURE_EU_JP "AGBZELDA:THE MINISH CAP:ZELDA 3"

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

/* ---- Image classification ------------------------------------------------
 * Decide what a candidate 8 KiB image is BEFORE the game sees it, so a file we
 * can't prove is ours (short, oversized, garbage, another region's save) is
 * never handed to InitSaveData as "blank" and then overwritten. */

static int BufferIsAll(const u8* data, size_t size, u8 value) {
    for (size_t i = 0; i < size; ++i)
        if (data[i] != value)
            return 0;
    return 1;
}

/* 1 = USA signature, 2 = EU/JP signature, 0 = neither. */
static int EepromSignatureKindAt(const u8* ramImage, u32 offset) {
    if (memcmp(ramImage + offset, EEPROM_SIGNATURE_USA, 0x20) == 0)
        return 1;
    if (memcmp(ramImage + offset, EEPROM_SIGNATURE_EU_JP, 0x20) == 0)
        return 2;
    return 0;
}

static u16 ReadU16LE(const u8* data) {
    return (u16)((u16)data[0] | ((u16)data[1] << 8));
}

static u32 ReadU32LE(const u8* data) {
    return (u32)data[0] | ((u32)data[1] << 8) | ((u32)data[2] << 16) | ((u32)data[3] << 24);
}

/* Mirrors src/save.c CalculateChecksum over a byte image. */
static u16 CalculateImageChecksum(const u8* data, u32 size) {
    u32 checksum = 0;
    while (size != 0) {
        checksum += ReadU16LE(data) ^ size;
        data += 2;
        size -= 2;
    }
    return (u16)checksum;
}

/* A record is valid when either of its two SaveFileStatus copies is an
 * untouched/deleted marker or an 'MCZ3' status whose checksum covers the data
 * (src/save.c ReadSaveFileStatus + VerifyChecksum). */
static int EepromStatusValidForData(const u8* ramImage, u32 statusOffset, u32 dataOffset, u32 dataSize) {
    for (unsigned copy = 0; copy < 2; ++copy) {
        const u8* statusBytes = ramImage + statusOffset + copy * 8u;
        const u16 checksum1 = ReadU16LE(statusBytes);
        const u16 checksum2 = ReadU16LE(statusBytes + 2);
        const u32 status = ReadU32LE(statusBytes + 4);
        if ((status == (u32)'TINI' || status == (u32)'FleD') && checksum1 == 0xFFFF && checksum2 == 0xFFFF)
            return 1;
        if (status == (u32)'MCZ3' && checksum2 == (u16)(-checksum1)) {
            u16 expected = CalculateImageChecksum(statusBytes + 4, 4);
            expected = (u16)(expected + CalculateImageChecksum(ramImage + dataOffset, dataSize));
            if (checksum1 == expected)
                return 1;
        }
    }
    return 0;
}

/* Number of save records (3 slots, header, misc) with at least one valid
 * status+data copy. Rows mirror src/save.c gSaveFileEEPROMAddresses. Retail
 * reads each record independently, so one damaged slot must not hide the
 * others. */
static unsigned EepromValidRecordCount(const u8* ramImage) {
    static const struct {
        u16 size, status1, status2, data1, data2;
    } records[] = {
        { 0x500, 0x30, 0x1030, 0x80, 0x1080 },
        { 0x500, 0x40, 0x1040, 0x580, 0x1580 },
        { 0x500, 0x50, 0x1050, 0xA80, 0x1A80 },
        { 0x10, 0x20, 0x1020, 0x70, 0x1070 },
        { 0x20, 0x60, 0x1060, 0xF80, 0x1F80 },
    };
    unsigned validCount = 0;
    for (size_t i = 0; i < sizeof(records) / sizeof(records[0]); ++i) {
        if (EepromStatusValidForData(ramImage, records[i].status1, records[i].data1, records[i].size) ||
            EepromStatusValidForData(ramImage, records[i].status2, records[i].data2, records[i].size))
            ++validCount;
    }
    return validCount;
}

typedef enum {
    EEPROM_IMAGE_INVALID = 0,
    EEPROM_IMAGE_BLANK,
    EEPROM_IMAGE_ACTIVE_REGION,
    EEPROM_IMAGE_OTHER_REGION,
} EepromImageClass;

static EepromImageClass ClassifyRamEepromImage(const u8* ramImage) {
    const int signature1 = EepromSignatureKindAt(ramImage, 0);
    const int signature2 = EepromSignatureKindAt(ramImage, 0x1000);
    const int activeSignature = (REGION_IS_EU || REGION_IS_JP) ? 2 : 1;

    if (BufferIsAll(ramImage, EEPROM_SIZE, 0xFF))
        return EEPROM_IMAGE_BLANK;
    /* Two recognized but different region signatures cannot be repaired
     * automatically: choosing either one would overwrite the other copy. */
    if (signature1 != 0 && signature2 != 0 && signature1 != signature2)
        return EEPROM_IMAGE_INVALID;
    if (signature1 == activeSignature || signature2 == activeSignature) {
        /* Require semantic evidence beyond the signature, but accept a partial
         * image when any record still has a valid duplicated status/checksum. */
        return EepromValidRecordCount(ramImage) != 0 ? EEPROM_IMAGE_ACTIVE_REGION : EEPROM_IMAGE_INVALID;
    }
    if (signature1 != 0 || signature2 != 0)
        return EEPROM_IMAGE_OTHER_REGION;
    return EEPROM_IMAGE_INVALID;
}

/* Load exactly one raw 8 KiB file into ramImage (game-RAM order on return)
 * and select its byte order only when a known signature (or an all-FF blank
 * image) proves the interpretation. *legacyRamOrder = 1 when the file was
 * in legacy port (game-RAM) order and needs the byte-order migration. */
static EepromImageClass ReadAndClassifyEepromFile(const char* path, u8* ramImage, int* legacyRamOrder) {
    FILE* file = fopen(path, "rb");
    if (file == NULL)
        return EEPROM_IMAGE_INVALID;
    const size_t got = fread(ramImage, 1, EEPROM_SIZE, file);
    const int trailing = got == EEPROM_SIZE ? fgetc(file) : EOF;
    const int readOk = !ferror(file);
    const int closeOk = fclose(file) == 0;
    if (got != EEPROM_SIZE || trailing != EOF || !readOk || !closeOk)
        return EEPROM_IMAGE_INVALID;

    *legacyRamOrder = 0;
    EepromImageClass cls = ClassifyRamEepromImage(ramImage);
    if (cls == EEPROM_IMAGE_ACTIVE_REGION || cls == EEPROM_IMAGE_OTHER_REGION) {
        *legacyRamOrder = 1;
        return cls;
    }
    if (cls == EEPROM_IMAGE_BLANK)
        return cls;

    ReverseEepromBlocks(ramImage);
    return ClassifyRamEepromImage(ramImage);
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
        /* #20: a full buffer + failed flush/sync is exactly the ENOSPC/EIO
         * case — treat it as a failed write instead of reporting success. */
        ok = fflush(f) == 0;
#ifdef _WIN32
        if (ok)
            ok = _commit(_fileno(f)) == 0;
#else
        if (ok)
            ok = fsync(fileno(f)) == 0;
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

#ifdef MULTI_REGION
/*
 * Region-isolated saves (M5). The fat binary plays USA/EU/JP from one executable;
 * each region's in-game save signature differs ("ZELDA 5" USA vs "ZELDA 3" EU/JP),
 * so a shared save file makes InitSaveData wipe the other region's data on load.
 * Give each region its own default store so switching ROMs never wipes or corrupts:
 *   USA -> tmc.sav (unchanged, back-compat with existing installs)
 *   EU  -> tmc_eu.sav
 *   JP  -> tmc_jp.sav
 * An explicit named profile (config.json) is region-agnostic and left untouched.
 * Resolved lazily here because the active region is only known after Port_LoadRom,
 * which runs after the startup Port_Save_SetActivePath() call.
 */
static void ResolveRegionDefaultPath(void) {
    const char* name;
    if (sExplicitProfile) {
        return;
    }
    if (REGION_IS_EU) {
        name = "tmc_eu.sav";
    } else if (REGION_IS_JP) {
        name = "tmc_jp.sav";
    } else {
        name = DEFAULT_SAVE_FILENAME; /* USA baseline keeps tmc.sav */
    }
    snprintf(sActivePath, sizeof(sActivePath), "%s", name);
}
#endif

static void LoadEepromFile(void) {
    int legacyRamOrder = 0;
#ifdef MULTI_REGION
    ResolveRegionDefaultPath();
#endif
    sEepromWriteBlocked = 0;
    FILE* probe = fopen(sActivePath, "rb");
    if (!probe) {
        const int openError = errno;
        memset(sEeprom, 0xFF, EEPROM_SIZE); /* blank EEPROM = 0xFF */
        if (openError == ENOENT) {
            fprintf(stderr, "[SAVE] No save file at %s, starting fresh.\n", sActivePath);
        } else {
            sEepromWriteBlocked = 1;
            fprintf(stderr, "[SAVE] ERROR: cannot read %s; writes are disabled and the file is untouched.\n",
                    sActivePath);
        }
        return;
    }
    fclose(probe);

    switch (ReadAndClassifyEepromFile(sActivePath, sEeprom, &legacyRamOrder)) {
    case EEPROM_IMAGE_INVALID:
        memset(sEeprom, 0xFF, EEPROM_SIZE);
        sEepromWriteBlocked = 1;
        fprintf(stderr,
                "[SAVE] ERROR: %s is not one exact, recognized 8 KiB EEPROM image; writes are disabled and the "
                "file is untouched.\n",
                sActivePath);
        return;
    case EEPROM_IMAGE_OTHER_REGION:
        memset(sEeprom, 0xFF, EEPROM_SIZE);
        sEepromWriteBlocked = 1;
        fprintf(stderr,
                "[SAVE] ERROR: %s belongs to another ROM region; writes are disabled so InitSaveData cannot erase "
                "it.\n",
                sActivePath);
        return;
    case EEPROM_IMAGE_BLANK:
        fprintf(stderr, "[SAVE] Loaded blank EEPROM image: %s\n", sActivePath);
        return;
    case EEPROM_IMAGE_ACTIVE_REGION:
        break;
    }

    if (legacyRamOrder) {
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
        fprintf(stderr, "[SAVE] Migrating %s to mGBA byte order (backup: %s)%s.\n", sActivePath, bak,
                backedUp ? "" : " — BACKUP FAILED");
        sEepromDirty = 1;
        FlushEepromFile();
    } else {
        /* ReadAndClassifyEepromFile already converted mGBA/VBA-M order to
         * game-RAM order after validating the active-region signature. */
        fprintf(stderr, "[SAVE] Loaded save file: %s\n", sActivePath);
    }
}

static void FlushEepromFile(void) {
    if (!sEepromDirty)
        return;
    if (WriteEepromAtomic(sActivePath)) {
        sEepromDirty = 0;
        if (sFlushFailedLast) {
            fprintf(stderr, "[SAVE] atomic write of %s recovered.\n", sActivePath);
            sFlushFailedLast = 0;
        }
    } else {
        /* Keep the dirty flag so the next flush retries; log once per
         * failure burst, not once per block write. */
        if (!sFlushFailedLast) {
            fprintf(stderr, "[SAVE] ERROR: atomic write of %s failed; will retry.\n", sActivePath);
            sFlushFailedLast = 1;
        }
    }
}

/* ---- Save transactions (#19) --------------------------------------------
 * A single in-game save is ~324 back-to-back EEPROMWrite0_8k_Check calls
 * (two full SaveFile copies + status blocks). Flushing the 8 KB file
 * atomically (temp+fsync+rename) per BLOCK meant ~324 fsync'd rewrites on
 * the game thread — a 150 ms..multi-second hitch. The game's save entry
 * points (src/save.c DataDoubleWriteWithStatus) bracket the burst so the
 * whole transaction flushes exactly once at the end. */
void Port_Save_BeginTransaction(void) {
    sSaveTxnDepth++;
}

/* Returns 1 when everything the transaction wrote is durably on disk. A
 * write-blocked profile never reports success: the game must show the
 * save-failed path instead of believing a save it could not make. */
int Port_Save_EndTransaction(void) {
    if (sSaveTxnDepth > 0)
        sSaveTxnDepth--;
    if (sSaveTxnDepth == 0)
        FlushEepromFile();
    return !sEepromWriteBlocked && !sEepromDirty;
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
    if (sEepromWriteBlocked)
        return 0x8000; /* preserve malformed/wrong-region backing file */

    memcpy(&sEeprom[block * EEPROM_BLOCK], src, EEPROM_BLOCK);
    sEepromDirty = 1;

    /* Flush immediately (crash safety) unless a save transaction is open —
     * then the single flush happens at Port_Save_EndTransaction(). */
    if (sSaveTxnDepth == 0)
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
        fprintf(stderr, "[SAVE] Ignoring unmanaged save profile '%s'; using %s.\n", path, DEFAULT_SAVE_FILENAME);
        path = DEFAULT_SAVE_FILENAME;
    }
    /* If the EEPROM was already loaded under the old path, flush it
     * first so the user doesn't lose pending writes when switching. */
    if (sEepromInited && sEepromDirty) {
        FlushEepromFile();
    }
    strncpy(sActivePath, path, sizeof(sActivePath) - 1);
    sActivePath[sizeof(sActivePath) - 1] = '\0';
    /* A named profile (anything other than the default tmc.sav) is the user's
     * explicit, region-agnostic choice; the multi-region per-region default
     * (ResolveRegionDefaultPath) must not override it. */
    sExplicitProfile = (strcmp(path, DEFAULT_SAVE_FILENAME) != 0);
    /* Force a reload on next access so any read after this point hits
     * the new file. */
    sEepromInited = 0;
    sEepromDirty = 0;
    sEepromWriteBlocked = 0;
}

const char* Port_Save_GetActivePath(void) {
    return sActivePath;
}

/* Snapshot the in-memory EEPROM into a named profile file without
 * changing the active profile. Useful for "Save current state as a new
 * profile" — keep playing in the current profile while the named copy
 * captures right-now state. Returns 0 on failure. */
int Port_Save_SaveAsProfile(const char* path) {
    if (path == NULL || path[0] == '\0')
        return 0;
    /* Only allow writing into the managed profile lane so the "save as"
     * UI can't be pointed at an arbitrary host path. */
    if (!IsManagedProfilePath(path))
        return 0;
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
    if (h == INVALID_HANDLE_VALUE)
        return 0;
    do {
        if (n >= max)
            break;
        const char* name = fd.cFileName;
        if (strcmp(name, DEFAULT_SAVE_FILENAME) == 0 || strncmp(name, "tmc_", 4) == 0) {
            strncpy(out[n], name, SAVE_FILENAME_MAX - 1);
            out[n][SAVE_FILENAME_MAX - 1] = '\0';
            n++;
        }
    } while (FindNextFileA(h, &fd));
    FindClose(h);
#else
    DIR* d = opendir(".");
    if (!d)
        return 0;
    struct dirent* ent;
    while ((ent = readdir(d)) != NULL) {
        if (n >= max)
            break;
        const char* name = ent->d_name;
        const size_t len = strlen(name);
        if (len < 4)
            continue;
        /* Match `tmc.sav` exactly OR `tmc_*.sav`. */
        if (strcmp(name, DEFAULT_SAVE_FILENAME) == 0 ||
            (strncmp(name, "tmc_", 4) == 0 && len > 8 && strcmp(name + len - 4, ".sav") == 0)) {
            strncpy(out[n], name, SAVE_FILENAME_MAX - 1);
            out[n][SAVE_FILENAME_MAX - 1] = '\0';
            n++;
        }
    }
    closedir(d);
#endif
    return n;
}

int Port_Save_FilenameMax(void) {
    return SAVE_FILENAME_MAX;
}

/* Returns 1 if the path is something we created and should be willing
 * to delete or rename — tmc.sav or tmc_<name>.sav. Anything else gets
 * refused so a stray ../../etc/passwd argument can't escape the
 * profile lane. */
static int IsManagedProfilePath(const char* path) {
    if (path == NULL || path[0] == '\0')
        return 0;
    if (strchr(path, '/') != NULL)
        return 0;
    if (strchr(path, '\\') != NULL)
        return 0;
    if (strstr(path, "..") != NULL)
        return 0;
    if (strcmp(path, DEFAULT_SAVE_FILENAME) == 0)
        return 1;
    if (strncmp(path, "tmc_", 4) != 0)
        return 0;
    const size_t len = strlen(path);
    if (len <= 8)
        return 0; /* "tmc_X.sav" minimum */
    if (strcmp(path + len - 4, ".sav") != 0)
        return 0;
    return 1;
}

/* Delete a profile file. Refuses if the profile is currently active
 * (caller should switch first) or if the name doesn't look like one
 * of ours. Returns 1 on success. */
int Port_Save_DeleteProfile(const char* path) {
    if (!IsManagedProfilePath(path))
        return 0;
    if (strcmp(path, sActivePath) == 0)
        return 0; /* refuse to delete active */
    return remove(path) == 0 ? 1 : 0;
}

/* Rename a profile file. Both args must look like managed profile
 * names. The default tmc.sav cannot be renamed away (it's our fallback
 * for fresh installs). If renaming the active profile, also updates
 * sActivePath so subsequent reads/writes hit the new name. */
int Port_Save_RenameProfile(const char* oldPath, const char* newPath) {
    if (!IsManagedProfilePath(oldPath))
        return 0;
    if (!IsManagedProfilePath(newPath))
        return 0;
    if (strcmp(oldPath, DEFAULT_SAVE_FILENAME) == 0)
        return 0; /* don't rename default away */
    if (strcmp(oldPath, newPath) == 0)
        return 1; /* no-op */
    /* Refuse clobbering an existing file — fail-stop is safer than
     * silently replacing somebody else's save. */
    FILE* probe = fopen(newPath, "rb");
    if (probe) {
        fclose(probe);
        return 0;
    }
    if (rename(oldPath, newPath) != 0)
        return 0;
    if (strcmp(oldPath, sActivePath) == 0) {
        strncpy(sActivePath, newPath, SAVE_FILENAME_MAX - 1);
        sActivePath[SAVE_FILENAME_MAX - 1] = '\0';
    }
    return 1;
}
