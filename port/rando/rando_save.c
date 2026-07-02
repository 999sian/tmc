/*
 * Native randomizer save sidecar.
 *
 * Keeps seed/settings/item table outside the vanilla EEPROM layout. One file
 * follows the active save profile: tmc.sav -> tmc.randomizer,
 * tmc_profile.sav -> tmc_profile.randomizer.
 */

#include "rando/rando_save.h"
#include "rando/rando.h"
#include "rando/rando_entrance.h"
#include "rando/rando_music.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#include <io.h>
#else
#include <unistd.h>
extern int fileno(FILE*);
#endif

#define RANDO_SIDECAR_SLOTS 3
/* Sidecar format version. Slot location keys/indices encode area-room-chest
 * where the chest index is the TileEntity iteration order from room data
 * (see Rando_RoomChestIndex). ANY change to that encoding or to the slot
 * layout below requires bumping this version.
 * v2: per-slot .logic define overrides + entrance assignments, so a reloaded
 * seed restores its eventdefine context and entrance shuffle.
 * v3: per-slot per-area music assignments (MUSIC_RANDO).
 * v4: per-location reward subtypes (shell counts, kinstone piece ids, dungeon
 * item ids) so same-item placements restore exactly across reloads.
 * v5: shuffle_entrances flag (decoupled from shuffle_kinstones) + tricks
 * bitmask (glitch-logic tier) so a seed's logic tier restores exactly. */
#define RANDO_SIDECAR_VERSION 6u
#define RANDO_SIDECAR_MAX_OVERRIDES 64
#define RANDO_SIDECAR_MAX_ENTRANCES 16
#define RANDO_SIDECAR_MUSIC_AREAS 256

static const char kMagic[8] = { 'T', 'M', 'C', 'R', 'N', 'D', 'O', '1' };

extern const char* Port_Save_GetActivePath(void);

typedef struct RandoSidecarOverride {
    char name[48];
    char value[32];
} RandoSidecarOverride;

typedef struct RandoSidecarEntrance {
    uint16_t location_index;
    int16_t subtype;
} RandoSidecarEntrance;

typedef struct RandoSidecarSlot {
    uint8_t active;
    uint8_t glitchless_logic;
    uint8_t shuffle_kinstones;
    uint8_t shuffle_dojos;
    uint8_t item_difficulty;
    uint8_t open_world; /* was `reserved` (always 0) before the option existed */
    uint16_t override_count;
    uint16_t entrance_count;
    uint8_t shuffle_entrances;     /* v5: was reserved2[0] */
    uint8_t reserved2;             /* v5: was reserved2[1] */
    uint32_t tricks;               /* v5: RANDO_TRICK_* bitmask (glitch-logic tier) */
    uint32_t logic_location_count; /* parse fingerprint for index validity */
    uint64_t seed;
    uint32_t count;
    RandoSidecarOverride overrides[RANDO_SIDECAR_MAX_OVERRIDES];
    RandoSidecarEntrance entrances[RANDO_SIDECAR_MAX_ENTRANCES];
    int16_t music[RANDO_SIDECAR_MUSIC_AREAS]; /* per-area song id, -1 = vanilla */
    uint16_t table[RANDO_LOCATION_COUNT];
    uint8_t subtype_table[RANDO_LOCATION_COUNT];
    uint8_t obscure_locations;
    uint8_t homewarp;
    uint8_t start_sword;
    uint8_t early_crests;
    uint8_t instant_text;
    uint8_t tunic_color;
    uint8_t heart_color;
    uint8_t reserved3;
} RandoSidecarSlot;

typedef struct RandoSidecarFile {
    RandoSidecarSlot slots[RANDO_SIDECAR_SLOTS];
} RandoSidecarFile;

static RandoSidecarFile sSidecar;

static void BuildSidecarPath(char* out, size_t out_len) {
    const char* save = Port_Save_GetActivePath();
    if (save == NULL || save[0] == '\0')
        save = "tmc.sav";
    snprintf(out, out_len, "%s", save);
    char* slash = strrchr(out, '/');
#ifdef _WIN32
    char* backslash = strrchr(out, '\\');
    if (backslash != NULL && (slash == NULL || backslash > slash))
        slash = backslash;
#endif
    char* dot = strrchr(out, '.');
    if (dot != NULL && (slash == NULL || dot > slash)) {
        snprintf(dot, out_len - (size_t)(dot - out), ".randomizer");
    } else {
        size_t n = strlen(out);
        if (n + sizeof(".randomizer") <= out_len) {
            memcpy(out + n, ".randomizer", sizeof(".randomizer"));
        }
    }
}

static uint32_t sLoadedVersion = 0;
static bool LoadAll(void) {
    char path[512];
    sLoadedVersion = 0;
    memset(&sSidecar, 0, sizeof(sSidecar));
    BuildSidecarPath(path, sizeof(path));
    FILE* f = fopen(path, "rb");
    if (f == NULL)
        return false;

    char magic[sizeof(kMagic)];
    uint32_t version = 0;
    uint32_t capacity = 0;
    bool ok = fread(magic, 1, sizeof(magic), f) == sizeof(magic) && fread(&version, sizeof(version), 1, f) == 1 &&
              fread(&capacity, sizeof(capacity), 1, f) == 1 && memcmp(magic, kMagic, sizeof(kMagic)) == 0 &&
              capacity == RANDO_LOCATION_COUNT;
    if (ok) {
        sLoadedVersion = version;
        size_t slot_size = (version >= 6) ? sizeof(RandoSidecarSlot) : (sizeof(RandoSidecarSlot) - 8);
        for (int i = 0; i < RANDO_SIDECAR_SLOTS; ++i) {
            if (fread(&sSidecar.slots[i], slot_size, 1, f) != 1) {
                ok = false;
                break;
            }
        }
    }
    fclose(f);
    if (!ok) {
        /* v1 sidecars lack overrides/entrances — clean break, but say so. */
        if (memcmp(magic, kMagic, sizeof(kMagic)) == 0 && version != RANDO_SIDECAR_VERSION) {
            fprintf(stderr, "[RANDO] sidecar version %u unsupported (want %u); ignoring file\n", version,
                    RANDO_SIDECAR_VERSION);
        }
        memset(&sSidecar, 0, sizeof(sSidecar));
        return false;
    }

    /* A corrupted/crafted file passed the header check; never let per-slot
     * fields drive out-of-range reads downstream (Rando_ActivateTable,
     * randomized_item_table indexing). Clear any slot that is out of range. */
    for (int i = 0; i < RANDO_SIDECAR_SLOTS; ++i) {
        RandoSidecarSlot* rec = &sSidecar.slots[i];
        if (!rec->active)
            continue;
        if (rec->count == 0 || rec->count > RANDO_LOCATION_COUNT || rec->item_difficulty >= RANDO_ITEM_POOL_COUNT ||
            rec->override_count > RANDO_SIDECAR_MAX_OVERRIDES || rec->entrance_count > RANDO_SIDECAR_MAX_ENTRANCES) {
            fprintf(stderr, "[rando] warning: sidecar slot %d corrupt (count=%u, difficulty=%u); cleared\n", i,
                    rec->count, rec->item_difficulty);
            memset(rec, 0, sizeof(*rec));
            continue;
        }
        /* Force-terminate strings; disarm out-of-range entrance indices. */
        for (uint32_t o = 0; o < rec->override_count; ++o) {
            rec->overrides[o].name[sizeof(rec->overrides[o].name) - 1] = '\0';
            rec->overrides[o].value[sizeof(rec->overrides[o].value) - 1] = '\0';
        }
        for (uint32_t e = 0; e < rec->entrance_count; ++e) {
            if (rec->entrances[e].location_index >= RANDO_LOCATION_COUNT || rec->entrances[e].subtype < 0 ||
                rec->entrances[e].subtype > 7) {
                rec->entrances[e].subtype = -1;
            }
        }
    }
    return ok;
}

/* Write the whole sidecar atomically: serialize to a sibling temp file, flush
 * it through to disk, then rename over the target. A crash or power loss
 * leaves either the old complete file or the new one, never a truncated image
 * the next LoadAll() would reject (and then zero on the following save). */
static bool WriteSidecarFile(const char* path) {
    char tmp[520];
    if ((size_t)snprintf(tmp, sizeof(tmp), "%s.tmp", path) >= sizeof(tmp))
        return false;
    FILE* f = fopen(tmp, "wb");
    if (f == NULL)
        return false;
    const uint32_t version = RANDO_SIDECAR_VERSION;
    const uint32_t capacity = RANDO_LOCATION_COUNT;
    bool ok = fwrite(kMagic, 1, sizeof(kMagic), f) == sizeof(kMagic) && fwrite(&version, sizeof(version), 1, f) == 1 &&
              fwrite(&capacity, sizeof(capacity), 1, f) == 1 && fwrite(&sSidecar, sizeof(sSidecar), 1, f) == 1;
    if (ok) {
        fflush(f);
#ifdef _WIN32
        _commit(_fileno(f));
#else
        fsync(fileno(f));
#endif
    }
    if (fclose(f) != 0)
        ok = false;
    if (!ok) {
        remove(tmp);
        return false;
    }
#ifdef _WIN32
    if (!MoveFileExA(tmp, path, MOVEFILE_REPLACE_EXISTING)) {
        remove(tmp);
        return false;
    }
#else
    if (rename(tmp, path) != 0) {
        remove(tmp);
        return false;
    }
#endif
    return true;
}

/* If a sidecar file exists on disk, rename it to <path>.bak. Called when
 * LoadAll() failed (corrupt / older-version file) right before an overwrite,
 * so the unreadable slots are preserved for recovery/migration instead of
 * being silently zeroed. No-op when no file exists (normal first save). */
static void BackupSidecarIfPresent(void) {
    char path[512];
    char bak[520];
    BuildSidecarPath(path, sizeof(path));
    FILE* probe = fopen(path, "rb");
    if (probe == NULL)
        return;
    fclose(probe);
    if ((size_t)snprintf(bak, sizeof(bak), "%s.bak", path) >= sizeof(bak))
        return;
    remove(bak);
    if (rename(path, bak) == 0) {
        fprintf(stderr, "[RANDO] unreadable sidecar preserved as %s before overwrite\n", bak);
    }
}

static bool SaveAll(void) {
    char path[512];
    BuildSidecarPath(path, sizeof(path));
    return WriteSidecarFile(path);
}

bool Port_RandoSave_SaveActiveSlot(int slot) {
    if (slot < 0 || slot >= RANDO_SIDECAR_SLOTS || !Rando_IsActive())
        return false;
    /* Preserve an existing-but-unreadable sidecar before overwriting: LoadAll
     * memsets all slots on any parse failure, so a corrupt/older file would
     * otherwise have its other slots silently zeroed by SaveAll below. */
    if (!LoadAll())
        BackupSidecarIfPresent();

    RandoSidecarSlot* rec = &sSidecar.slots[slot];
    const uint16_t* table = Rando_GetRandomizedItemTable();
    const uint8_t* subtype_table = Rando_GetRandomizedItemSubtypeTable();
    RandomizerSettings settings = Rando_GetSettings();
    size_t count = Rando_GetLocationCount();
    if (count > RANDO_LOCATION_COUNT)
        return false;

    memset(rec, 0, sizeof(*rec));
    rec->active = 1;
    rec->glitchless_logic = settings.glitchless_logic ? 1 : 0;
    rec->shuffle_kinstones = settings.shuffle_kinstones ? 1 : 0;
    rec->shuffle_entrances = settings.shuffle_entrances ? 1 : 0;
    rec->shuffle_dojos = settings.shuffle_dojos ? 1 : 0;
    rec->open_world = settings.open_world ? 1 : 0;
    rec->item_difficulty = (uint8_t)settings.item_difficulty;
    rec->tricks = settings.tricks;
    rec->seed = Rando_GetSeed64();
    rec->count = (uint32_t)count;
    memcpy(rec->table, table, count * sizeof(rec->table[0]));
    memcpy(rec->subtype_table, subtype_table, count * sizeof(rec->subtype_table[0]));

    rec->tricks = settings.tricks;
    rec->obscure_locations = settings.obscure_locations;
    rec->homewarp = settings.homewarp;
    rec->start_sword = settings.start_sword;
    rec->early_crests = settings.early_crests;
    rec->instant_text = settings.instant_text;
    rec->tunic_color = (uint8_t)settings.tunic_color;
    rec->heart_color = (uint8_t)settings.heart_color;
    /* Save entrance assignments */
    rec->entrance_count = 0;
    for (int i = 0; i < 8; ++i) {
        int e = Rando_Entrance_GetAssignment(i);
        if (e >= 0) {
            rec->entrances[rec->entrance_count].location_index = (uint16_t)i;
            rec->entrances[rec->entrance_count].subtype = (int16_t)e;
            rec->entrance_count++;
        }
    }

    /* Save music assignments */
    for (uint32_t a = 0; a < RANDO_SIDECAR_MUSIC_AREAS; ++a) {
        rec->music[a] = (int16_t)Rando_Music_GetAssignment(a);
    }

    if (!SaveAll())
        return false;
    fprintf(stderr, "[RANDO] saved sidecar slot %d (%u locations)\n", slot, rec->count);
    return true;
}

bool Port_RandoSave_LoadSlot(int slot) {
    if (slot < 0 || slot >= RANDO_SIDECAR_SLOTS)
        return false;
    if (!LoadAll())
        return false;

    RandoSidecarSlot* rec = &sSidecar.slots[slot];
    if (!rec->active || rec->count == 0 || rec->count > RANDO_LOCATION_COUNT)
        return false;

    RandomizerSettings settings = Rando_DefaultSettings();
    settings.glitchless_logic = rec->glitchless_logic != 0;
    settings.shuffle_kinstones = rec->shuffle_kinstones != 0;
    settings.shuffle_entrances = rec->shuffle_entrances != 0;
    settings.shuffle_dojos = rec->shuffle_dojos != 0;
    settings.open_world = rec->open_world != 0;
    settings.tricks = rec->tricks;
    if (rec->item_difficulty < RANDO_ITEM_POOL_COUNT) {
        settings.item_difficulty = (RandoItemPoolDifficulty)rec->item_difficulty;
    }
    settings.tricks = rec->tricks;
    if (sLoadedVersion >= 6) {
        settings.obscure_locations = rec->obscure_locations;
        settings.homewarp = rec->homewarp;
        settings.start_sword = rec->start_sword;
        settings.early_crests = rec->early_crests;
        settings.instant_text = rec->instant_text;
        settings.tunic_color = rec->tunic_color;
        settings.heart_color = rec->heart_color;
    }

    // No logic define overrides to restore anymore

    if (!Rando_ActivateTable(rec->seed, settings, rec->table, rec->subtype_table, rec->count))
        return false;

    /* Restore entrance and music assignments */
    Rando_Entrance_ClearAssignments();
    for (uint32_t e = 0; e < rec->entrance_count; ++e) {
        Rando_Entrance_SetAssignment(rec->entrances[e].location_index, rec->entrances[e].subtype);
    }

    Rando_Music_ClearAssignments();
    for (uint32_t a = 0; a < RANDO_SIDECAR_MUSIC_AREAS; ++a) {
        if (rec->music[a] >= 0) {
            Rando_Music_SetAssignment(a, rec->music[a]);
        }
    }

    fprintf(stderr, "[RANDO] loaded sidecar slot %d (%u locations, %u entrances)\n", slot, rec->count,
            rec->entrance_count);
    return true;
}

void Port_RandoSave_ClearSlot(int slot) {
    if (slot < 0 || slot >= RANDO_SIDECAR_SLOTS)
        return;
    if (!LoadAll())
        BackupSidecarIfPresent();
    memset(&sSidecar.slots[slot], 0, sizeof(sSidecar.slots[slot]));
    (void)SaveAll();
}

void Port_RandoSave_CopySlot(int src, int dst) {
    if (src < 0 || src >= RANDO_SIDECAR_SLOTS || dst < 0 || dst >= RANDO_SIDECAR_SLOTS)
        return;
    if (!LoadAll())
        BackupSidecarIfPresent();
    sSidecar.slots[dst] = sSidecar.slots[src];
    (void)SaveAll();
}
