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
#include "rando/rando_logic.h"
#include "rando/rando_music.h"
#include "item_ids.h"

#include <errno.h>
#include <stddef.h>
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
 * bitmask (glitch-logic tier) so a seed's logic tier restores exactly.
 * v8: indexed rule tables, source fingerprint, and rule overrides. */
#define RANDO_SIDECAR_VERSION 8u
#define RANDO_SIDECAR_V7_VERSION 7u
#define RANDO_SIDECAR_V6_FIRST_CAPACITY 211u
#define RANDO_SIDECAR_V6_LAST_CAPACITY 228u
#define RANDO_SIDECAR_MAX_OVERRIDES 64
#define RANDO_SIDECAR_MAX_LOGIC_OVERRIDES 320
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
    uint8_t accessibility;         /* v6: was reserved2 (always 0 == RANDO_ACCESS_GOAL, so no bump) */
    uint32_t tricks;               /* v5: RANDO_TRICK_* bitmask (glitch-logic tier) */
    uint32_t logic_location_count; /* rule location count for index validity */
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
    uint8_t shuffle_dungeon_items; /* v7: v6 reserved byte */
    uint8_t logic_mode;             /* v8: table is indexed by rule location */
    uint64_t logic_fingerprint;
    uint16_t logic_override_count;
    RandoSidecarOverride logic_overrides[RANDO_SIDECAR_MAX_LOGIC_OVERRIDES];
    uint16_t logic_table[RANDO_LOGIC_MAX_LOCATIONS];
    uint8_t logic_subtype_table[RANDO_LOGIC_MAX_LOCATIONS];
} RandoSidecarSlot;

typedef struct RandoSidecarFile {
    RandoSidecarSlot slots[RANDO_SIDECAR_SLOTS];
} RandoSidecarFile;

typedef struct RandoSidecarAlignProbe {
    char byte;
    RandoSidecarSlot slot;
} RandoSidecarAlignProbe;

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
static bool sSidecarMissing;
static uint8_t sInvalidSlotMask;
static PortRandoSaveLoadStatus sLastLoadStatus;

static uint64_t BindingMix(uint64_t hash, uint64_t value, unsigned bytes) {
    for (unsigned i = 0; i < bytes; ++i) {
        hash = (hash ^ (uint8_t)value) * UINT64_C(1099511628211);
        value >>= 8;
    }
    return hash;
}

uint64_t Port_RandoSave_ActiveBindingHash(void) {
    if (!Rando_IsActive())
        return 0;
    size_t count = Rando_GetLocationCount();
    if (count == 0 || count > RANDO_LOGIC_MAX_LOCATIONS)
        return 0;
    /* Only generation identity belongs here. Live cosmetic edits can update
     * the sidecar before EEPROM without invalidating an earlier save. */
    RandomizerSettings settings = Rando_GetSettings();
    uint32_t options = (settings.glitchless_logic ? 1u : 0u) |
                       (settings.obscure_locations ? 2u : 0u) |
                       (settings.shuffle_kinstones ? 4u : 0u) |
                       (settings.shuffle_entrances ? 8u : 0u) |
                       (settings.shuffle_dojos ? 16u : 0u) |
                       (settings.open_world ? 32u : 0u) |
                       (settings.shuffle_dungeon_items ? 64u : 0u) |
                       (settings.start_sword ? 128u : 0u);
    uint64_t hash = UINT64_C(14695981039346656037);
    hash = BindingMix(hash, Rando_GetSeed64(), 8);
    hash = BindingMix(hash, Rando_GetLogicFingerprint(), 8);
    hash = BindingMix(hash, count, 4);
    hash = BindingMix(hash, options, 4);
    hash = BindingMix(hash, settings.item_difficulty, 4);
    hash = BindingMix(hash, settings.tricks, 4);
    hash = BindingMix(hash, settings.accessibility, 4);
    const uint16_t* table = Rando_GetRandomizedItemTable();
    const uint8_t* subtypes = Rando_GetRandomizedItemSubtypeTable();
    for (size_t i = 0; i < count; ++i) {
        hash = BindingMix(hash, table[i], 2);
        hash = BindingMix(hash, subtypes[i], 1);
    }
    for (int i = 0; i < 8; ++i)
        hash = BindingMix(hash, (uint8_t)(Rando_Entrance_GetAssignment(i) + 1), 1);
    return hash ? hash : 1;
}

static bool LoadAll(void) {
    char path[512];
    sLoadedVersion = 0;
    sSidecarMissing = false;
    sInvalidSlotMask = 0;
    memset(&sSidecar, 0, sizeof(sSidecar));
    BuildSidecarPath(path, sizeof(path));
    FILE* f = fopen(path, "rb");
    if (f == NULL) {
        sSidecarMissing = errno == ENOENT;
        return false;
    }

    char magic[sizeof(kMagic)] = { 0 };
    uint32_t version = 0;
    uint32_t capacity = 0;
    bool ok = fread(magic, 1, sizeof(magic), f) == sizeof(magic) && fread(&version, sizeof(version), 1, f) == 1 &&
              fread(&capacity, sizeof(capacity), 1, f) == 1 && memcmp(magic, kMagic, sizeof(kMagic)) == 0 &&
              ((version == 6 && (capacity == RANDO_SIDECAR_V6_FIRST_CAPACITY ||
                                 capacity == RANDO_SIDECAR_V6_LAST_CAPACITY)) ||
               (version == RANDO_SIDECAR_V7_VERSION && capacity == RANDO_SIDECAR_V6_LAST_CAPACITY) ||
               (version == RANDO_SIDECAR_VERSION && capacity == RANDO_LOGIC_MAX_LOCATIONS));
    if (ok) {
        sLoadedVersion = version;
        if (version == RANDO_SIDECAR_VERSION) {
            ok = fread(&sSidecar, sizeof(sSidecar), 1, f) == 1;
        } else {
            /* v6/v7 arrays are sized by the header capacity. Read separately
             * so 211-location slots do not shift later slots or settings. */
            const size_t alignment = offsetof(RandoSidecarAlignProbe, slot);
            const size_t unpadded = offsetof(RandoSidecarSlot, table) + 3u * capacity + 8u;
            const size_t padding = (alignment - unpadded % alignment) % alignment;
            for (int i = 0; i < RANDO_SIDECAR_SLOTS; ++i) {
                RandoSidecarSlot* rec = &sSidecar.slots[i];
                if (fread(rec, 1, offsetof(RandoSidecarSlot, table), f) != offsetof(RandoSidecarSlot, table) ||
                    fread(rec->table, sizeof(rec->table[0]), capacity, f) != capacity ||
                    fread(rec->subtype_table, 1, capacity, f) != capacity ||
                    fread(&rec->obscure_locations, 1, 8, f) != 8) {
                    ok = false;
                    break;
                }
                for (size_t p = 0; p < padding; ++p) {
                    if (fgetc(f) == EOF) {
                        ok = false;
                        break;
                    }
                }
                if (!ok)
                    break;
            }
        }
        if (ok && (fgetc(f) != EOF || ferror(f)))
            ok = false;
    }
    fclose(f);
    if (!ok) {
        if (memcmp(magic, kMagic, sizeof(kMagic)) == 0)
            fprintf(stderr, "[RANDO] sidecar version %u, capacity %u, or length unsupported; ignoring file\n", version,
                    capacity);
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
        bool invalid_logic = rec->logic_mode > 1 ||
                             (rec->logic_mode && (rec->count > RANDO_LOGIC_MAX_LOCATIONS ||
                                                  rec->logic_fingerprint == 0 ||
                                                  rec->logic_override_count > RANDO_SIDECAR_MAX_LOGIC_OVERRIDES));
        bool invalid_legacy = !rec->logic_mode && rec->count > RANDO_LOCATION_COUNT;
        if (rec->count == 0 || invalid_logic || invalid_legacy || rec->item_difficulty >= RANDO_ITEM_POOL_COUNT ||
            rec->override_count > RANDO_SIDECAR_MAX_OVERRIDES || rec->entrance_count > RANDO_SIDECAR_MAX_ENTRANCES) {
            fprintf(stderr, "[rando] warning: sidecar slot %d corrupt (count=%u, difficulty=%u); cleared\n", i,
                    rec->count, rec->item_difficulty);
            sInvalidSlotMask |= (uint8_t)(1u << i);
            memset(rec, 0, sizeof(*rec));
            continue;
        }
        /* Force-terminate strings; disarm out-of-range entrance indices. */
        for (uint32_t o = 0; o < rec->override_count; ++o) {
            rec->overrides[o].name[sizeof(rec->overrides[o].name) - 1] = '\0';
            rec->overrides[o].value[sizeof(rec->overrides[o].value) - 1] = '\0';
        }
        for (uint32_t o = 0; o < rec->logic_override_count; ++o) {
            rec->logic_overrides[o].name[sizeof(rec->logic_overrides[o].name) - 1] = '\0';
            rec->logic_overrides[o].value[sizeof(rec->logic_overrides[o].value) - 1] = '\0';
        }
        for (uint32_t e = 0; e < rec->entrance_count; ++e) {
            if (rec->entrances[e].location_index >= RANDO_LOCATION_COUNT || rec->entrances[e].subtype < 0 ||
                rec->entrances[e].subtype > 7) {
                rec->entrances[e].subtype = -1;
            }
        }
        if (version == 6) {
            /* v6 did not persist this setting. Every shuffled dungeon item
             * carries the origin bit; pinned vanilla items never do. */
            rec->shuffle_dungeon_items = 0;
            for (uint32_t j = 0; j < rec->count; ++j) {
                uint16_t item = rec->table[j];
                if ((item == ITEM_BIG_KEY || item == ITEM_DUNGEON_MAP || item == ITEM_COMPASS) &&
                    RANDO_SUBTYPE_HAS_ORIGIN(rec->subtype_table[j])) {
                    rec->shuffle_dungeon_items = 1;
                    break;
                }
            }
        }
        /* Locations appended since this save was made stay vanilla. Giving
         * them explicit placements also keeps them on the keyed award path,
         * which protects their items from the incidental-item remap. */
        if (!rec->logic_mode) {
            for (uint32_t j = rec->count; j < RANDO_LOCATION_COUNT; ++j) {
                rec->table[j] = Rando_GetLocationDef((RandoLocationId)j)->vanilla_item;
                rec->subtype_table[j] = 0;
            }
            rec->count = RANDO_LOCATION_COUNT;
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
    const uint32_t capacity = RANDO_LOGIC_MAX_LOCATIONS;
    bool ok = fwrite(kMagic, 1, sizeof(kMagic), f) == sizeof(kMagic) && fwrite(&version, sizeof(version), 1, f) == 1 &&
              fwrite(&capacity, sizeof(capacity), 1, f) == 1 && fwrite(&sSidecar, sizeof(sSidecar), 1, f) == 1;
    if (ok) {
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

/* Keep every unreadable sidecar before replacing it. Return false if the
 * source exists but no backup can be made: overwriting it would lose saves. */
static bool BackupSidecarIfPresent(void) {
    char path[512];
    char bak[520];
    BuildSidecarPath(path, sizeof(path));
    FILE* probe = fopen(path, "rb");
    if (probe == NULL)
        return errno == ENOENT;
    fclose(probe);
    for (unsigned i = 0; i < 1000; ++i) {
        int n = i ? snprintf(bak, sizeof(bak), "%s.bak.%u", path, i) : snprintf(bak, sizeof(bak), "%s.bak", path);
        if (n < 0 || (size_t)n >= sizeof(bak))
            return false;
#ifdef _WIN32
        if (CopyFileA(path, bak, TRUE)) {
#else
        /* link() refuses an existing destination, unlike rename(). */
        if (link(path, bak) == 0) {
#endif
            fprintf(stderr, "[RANDO] unreadable sidecar preserved as %s before overwrite\n", bak);
            return true;
        }
#ifdef _WIN32
        DWORD error = GetLastError();
        if (error != ERROR_ALREADY_EXISTS && error != ERROR_FILE_EXISTS)
            return false;
#else
        if (errno != EEXIST)
            return false;
#endif
    }
    return false;
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
    if ((!LoadAll() || sInvalidSlotMask) && !BackupSidecarIfPresent())
        return false;

    RandoSidecarSlot* rec = &sSidecar.slots[slot];
    const uint16_t* table = Rando_GetRandomizedItemTable();
    const uint8_t* subtype_table = Rando_GetRandomizedItemSubtypeTable();
    RandomizerSettings settings = Rando_GetSettings();
    size_t count = Rando_GetLocationCount();
    bool logic_mode = Rando_IsLogicSeed();
    if (count == 0 || count > (logic_mode ? RANDO_LOGIC_MAX_LOCATIONS : RANDO_LOCATION_COUNT))
        return false;

    uint64_t fingerprint = 0;
    uint32_t override_count = 0;
    if (logic_mode) {
        fingerprint = Rando_GetLogicFingerprint();
        override_count = RandoLogic_GetOverrideCount();
        if (fingerprint == 0 || fingerprint != RandoLogic_SourceFingerprint() ||
            count != RandoLogic_GetLocationCountRaw() ||
            override_count > RANDO_SIDECAR_MAX_LOGIC_OVERRIDES)
            return false;
    }

    memset(rec, 0, sizeof(*rec));
    rec->active = 1;
    rec->glitchless_logic = settings.glitchless_logic ? 1 : 0;
    rec->shuffle_kinstones = settings.shuffle_kinstones ? 1 : 0;
    rec->shuffle_entrances = settings.shuffle_entrances ? 1 : 0;
    rec->shuffle_dojos = settings.shuffle_dojos ? 1 : 0;
    rec->open_world = settings.open_world ? 1 : 0;
    rec->item_difficulty = (uint8_t)settings.item_difficulty;
    rec->tricks = settings.tricks;
    rec->accessibility = (uint8_t)settings.accessibility;
    rec->seed = Rando_GetSeed64();
    rec->count = (uint32_t)count;
    if (logic_mode) {
        rec->logic_mode = 1;
        rec->logic_fingerprint = fingerprint;
        rec->logic_override_count = (uint16_t)override_count;
        for (uint32_t i = 0; i < override_count; ++i) {
            const char* name;
            const char* value;
            if (!RandoLogic_GetOverride(i, &name, &value) || name == NULL || value == NULL ||
                strlen(name) >= sizeof(rec->logic_overrides[i].name) ||
                strlen(value) >= sizeof(rec->logic_overrides[i].value))
                return false;
            strcpy(rec->logic_overrides[i].name, name);
            strcpy(rec->logic_overrides[i].value, value);
        }
        memcpy(rec->logic_table, table, count * sizeof(rec->logic_table[0]));
        memcpy(rec->logic_subtype_table, subtype_table, count);
    } else {
        memcpy(rec->table, table, count * sizeof(rec->table[0]));
        memcpy(rec->subtype_table, subtype_table, count);
    }

    rec->tricks = settings.tricks;
    rec->obscure_locations = settings.obscure_locations;
    rec->homewarp = settings.homewarp;
    rec->start_sword = settings.start_sword;
    rec->early_crests = settings.early_crests;
    rec->instant_text = settings.instant_text;
    rec->tunic_color = (uint8_t)settings.tunic_color;
    rec->heart_color = (uint8_t)settings.heart_color;
    rec->shuffle_dungeon_items = settings.shuffle_dungeon_items ? 1 : 0;
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
    sLastLoadStatus = PORT_RANDO_SAVE_INCOMPATIBLE;
    if (slot < 0 || slot >= RANDO_SIDECAR_SLOTS)
        return false;
    if (!LoadAll()) {
        if (sSidecarMissing)
            sLastLoadStatus = PORT_RANDO_SAVE_NONE;
        return false;
    }

    if (sInvalidSlotMask & (1u << slot))
        return false;

    RandoSidecarSlot* rec = &sSidecar.slots[slot];
    if (!rec->active) {
        sLastLoadStatus = PORT_RANDO_SAVE_NONE;
        return false;
    }
    if (rec->count == 0)
        return false;

    RandomizerSettings settings = Rando_DefaultSettings();
    settings.glitchless_logic = rec->glitchless_logic != 0;
    settings.shuffle_kinstones = rec->shuffle_kinstones != 0;
    settings.shuffle_entrances = rec->shuffle_entrances != 0;
    settings.shuffle_dojos = rec->shuffle_dojos != 0;
    settings.open_world = rec->open_world != 0;
    settings.tricks = rec->tricks;
    settings.shuffle_dungeon_items = rec->shuffle_dungeon_items != 0;
    if (rec->accessibility < RANDO_ACCESS_COUNT) {
        settings.accessibility = (RandoAccessibility)rec->accessibility;
    }
    if (rec->item_difficulty < RANDO_ITEM_POOL_COUNT) {
        settings.item_difficulty = (RandoItemPoolDifficulty)rec->item_difficulty;
    }
    if (sLoadedVersion >= 6) {
        settings.obscure_locations = rec->obscure_locations;
        settings.homewarp = rec->homewarp;
        settings.start_sword = rec->start_sword;
        settings.early_crests = rec->early_crests;
        settings.instant_text = rec->instant_text;
        settings.tunic_color = rec->tunic_color;
        settings.heart_color = rec->heart_color;
    }

    if (rec->logic_mode) {
        RandoLogic_ClearOverrides();
        for (uint32_t i = 0; i < rec->logic_override_count; ++i) {
            RandoLogic_SetOverride(rec->logic_overrides[i].name, rec->logic_overrides[i].value);
        }
        if (!RandoLogic_LoadBuiltIn() || RandoLogic_SourceFingerprint() != rec->logic_fingerprint ||
            RandoLogic_GetLocationCountRaw() != rec->count ||
            !Rando_ActivateLogicTable(rec->seed, settings, rec->logic_table, rec->logic_subtype_table,
                                      rec->count, rec->logic_fingerprint)) {
            fprintf(stderr, "[RANDO] sidecar slot %d logic changed or unavailable; seed not activated\n", slot);
            return false;
        }
    } else if (!Rando_ActivateTable(rec->seed, settings, rec->table, rec->subtype_table, rec->count)) {
        return false;
    }

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
    sLastLoadStatus = PORT_RANDO_SAVE_LOADED;
    return true;
}

PortRandoSaveLoadStatus Port_RandoSave_LastLoadStatus(void) {
    return sLastLoadStatus;
}

bool Port_RandoSave_LoadedLegacySlot(void) {
    return sLastLoadStatus == PORT_RANDO_SAVE_LOADED && sLoadedVersion < RANDO_SIDECAR_VERSION;
}

void Port_RandoSave_ClearSlot(int slot) {
    if (slot < 0 || slot >= RANDO_SIDECAR_SLOTS)
        return;
    if ((!LoadAll() || sInvalidSlotMask) && !BackupSidecarIfPresent())
        return;
    memset(&sSidecar.slots[slot], 0, sizeof(sSidecar.slots[slot]));
    (void)SaveAll();
}

void Port_RandoSave_CopySlot(int src, int dst) {
    if (src < 0 || src >= RANDO_SIDECAR_SLOTS || dst < 0 || dst >= RANDO_SIDECAR_SLOTS)
        return;
    if ((!LoadAll() || sInvalidSlotMask) && !BackupSidecarIfPresent())
        return;
    sSidecar.slots[dst] = sSidecar.slots[src];
    (void)SaveAll();
}
