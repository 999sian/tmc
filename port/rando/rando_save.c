/*
 * Native randomizer save sidecar.
 *
 * Keeps seed/settings/item table outside the vanilla EEPROM layout. One file
 * follows the active save profile: tmc.sav -> tmc.randomizer,
 * tmc_profile.sav -> tmc_profile.randomizer.
 */

#include "rando/rando_save.h"
#include "rando/rando.h"
#include "rando/rando_logic.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define RANDO_SIDECAR_SLOTS 3
/* Sidecar format version. Slot location keys/indices encode area-room-chest
 * where the chest index is the TileEntity iteration order from room data
 * (see Rando_RoomChestIndex). ANY change to that encoding or to the slot
 * layout below requires bumping this version.
 * v2: per-slot .logic define overrides + entrance assignments, so a reloaded
 * seed restores its eventdefine context and entrance shuffle. */
#define RANDO_SIDECAR_VERSION 2u
#define RANDO_SIDECAR_MAX_OVERRIDES 64
#define RANDO_SIDECAR_MAX_ENTRANCES 16

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
    uint8_t reserved;
    uint16_t override_count;
    uint16_t entrance_count;
    uint8_t reserved2[2];
    uint32_t logic_location_count; /* parse fingerprint for index validity */
    uint64_t seed;
    uint32_t count;
    RandoSidecarOverride overrides[RANDO_SIDECAR_MAX_OVERRIDES];
    RandoSidecarEntrance entrances[RANDO_SIDECAR_MAX_ENTRANCES];
    uint16_t table[RANDO_LOCATION_COUNT];
} RandoSidecarSlot;

typedef struct RandoSidecarFile {
    RandoSidecarSlot slots[RANDO_SIDECAR_SLOTS];
} RandoSidecarFile;

static RandoSidecarFile sSidecar;

static void BuildSidecarPath(char* out, size_t out_len) {
    const char* save = Port_Save_GetActivePath();
    if (save == NULL || save[0] == '\0') save = "tmc.sav";
    snprintf(out, out_len, "%s", save);
    char* slash = strrchr(out, '/');
#ifdef _WIN32
    char* backslash = strrchr(out, '\\');
    if (backslash != NULL && (slash == NULL || backslash > slash)) slash = backslash;
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

static bool LoadAll(void) {
    char path[512];
    memset(&sSidecar, 0, sizeof(sSidecar));
    BuildSidecarPath(path, sizeof(path));
    FILE* f = fopen(path, "rb");
    if (f == NULL) return false;

    char magic[sizeof(kMagic)];
    uint32_t version = 0;
    uint32_t capacity = 0;
    bool ok = fread(magic, 1, sizeof(magic), f) == sizeof(magic) &&
              fread(&version, sizeof(version), 1, f) == 1 &&
              fread(&capacity, sizeof(capacity), 1, f) == 1 &&
              memcmp(magic, kMagic, sizeof(kMagic)) == 0 &&
              version == RANDO_SIDECAR_VERSION &&
              capacity == RANDO_LOCATION_COUNT &&
              fread(&sSidecar, sizeof(sSidecar), 1, f) == 1;
    fclose(f);
    if (!ok) {
        /* v1 sidecars lack overrides/entrances — clean break, but say so. */
        if (memcmp(magic, kMagic, sizeof(kMagic)) == 0 && version != RANDO_SIDECAR_VERSION) {
            fprintf(stderr, "[RANDO] sidecar version %u unsupported (want %u); ignoring file\n",
                    version, RANDO_SIDECAR_VERSION);
        }
        memset(&sSidecar, 0, sizeof(sSidecar));
        return false;
    }

    /* A corrupted/crafted file passed the header check; never let per-slot
     * fields drive out-of-range reads downstream (Rando_ActivateTable,
     * randomized_item_table indexing). Clear any slot that is out of range. */
    for (int i = 0; i < RANDO_SIDECAR_SLOTS; ++i) {
        RandoSidecarSlot* rec = &sSidecar.slots[i];
        if (!rec->active) continue;
        if (rec->count == 0 || rec->count > RANDO_LOCATION_COUNT ||
            rec->item_difficulty >= RANDO_ITEM_POOL_COUNT ||
            rec->override_count > RANDO_SIDECAR_MAX_OVERRIDES ||
            rec->entrance_count > RANDO_SIDECAR_MAX_ENTRANCES) {
            fprintf(stderr,
                    "[rando] warning: sidecar slot %d corrupt (count=%u, difficulty=%u); cleared\n",
                    i, rec->count, rec->item_difficulty);
            memset(rec, 0, sizeof(*rec));
            continue;
        }
        /* Force-terminate strings; disarm out-of-range entrance indices. */
        for (uint32_t o = 0; o < rec->override_count; ++o) {
            rec->overrides[o].name[sizeof(rec->overrides[o].name) - 1] = '\0';
            rec->overrides[o].value[sizeof(rec->overrides[o].value) - 1] = '\0';
        }
        for (uint32_t e = 0; e < rec->entrance_count; ++e) {
            if (rec->entrances[e].location_index >= RANDO_LOCATION_COUNT) {
                rec->entrances[e].subtype = -1;
            }
        }
    }
    return ok;
}

static bool SaveAll(void) {
    char path[512];
    BuildSidecarPath(path, sizeof(path));
    FILE* f = fopen(path, "wb");
    if (f == NULL) return false;
    const uint32_t version = RANDO_SIDECAR_VERSION;
    const uint32_t capacity = RANDO_LOCATION_COUNT;
    bool ok = fwrite(kMagic, 1, sizeof(kMagic), f) == sizeof(kMagic) &&
              fwrite(&version, sizeof(version), 1, f) == 1 &&
              fwrite(&capacity, sizeof(capacity), 1, f) == 1 &&
              fwrite(&sSidecar, sizeof(sSidecar), 1, f) == 1;
    fclose(f);
    return ok;
}

bool Port_RandoSave_SaveActiveSlot(int slot) {
    if (slot < 0 || slot >= RANDO_SIDECAR_SLOTS || !Rando_IsActive()) return false;
    (void)LoadAll();

    RandoSidecarSlot* rec = &sSidecar.slots[slot];
    const uint16_t* table = Rando_GetRandomizedItemTable();
    RandomizerSettings settings = Rando_GetSettings();
    size_t count = Rando_GetLocationCount();
    if (count > RANDO_LOCATION_COUNT) return false;

    memset(rec, 0, sizeof(*rec));
    rec->active = 1;
    rec->glitchless_logic = settings.glitchless_logic ? 1 : 0;
    rec->shuffle_kinstones = settings.shuffle_kinstones ? 1 : 0;
    rec->shuffle_dojos = settings.shuffle_dojos ? 1 : 0;
    rec->item_difficulty = (uint8_t)settings.item_difficulty;
    rec->seed = Rando_GetSeed64();
    rec->count = (uint32_t)count;
    memcpy(rec->table, table, count * sizeof(rec->table[0]));

    /* Persist the .logic define overrides this seed was generated under and
     * the generation-time entrance assignments — both are required to restore
     * the seed's full context after a process restart. */
    if (RandoLogic_IsLoaded()) {
        uint32_t oc = RandoLogic_GetOverrideCount();
        if (oc > RANDO_SIDECAR_MAX_OVERRIDES) {
            fprintf(stderr, "[RANDO] warning: %u overrides exceed sidecar cap %d; extras dropped\n",
                    oc, RANDO_SIDECAR_MAX_OVERRIDES);
            oc = RANDO_SIDECAR_MAX_OVERRIDES;
        }
        for (uint32_t o = 0; o < oc; ++o) {
            const char* nm = NULL;
            const char* val = NULL;
            if (!RandoLogic_GetOverride(o, &nm, &val)) break;
            snprintf(rec->overrides[o].name, sizeof(rec->overrides[o].name), "%s", nm);
            snprintf(rec->overrides[o].value, sizeof(rec->overrides[o].value), "%s", val);
            rec->override_count++;
        }
        rec->logic_location_count = RandoLogic_GetLocationCountRaw();
        for (uint32_t l = 0; l < rec->logic_location_count; ++l) {
            int e = RandoLogic_GetEntranceAssignment(l);
            if (e < 0) continue;
            if (rec->entrance_count >= RANDO_SIDECAR_MAX_ENTRANCES) {
                fprintf(stderr, "[RANDO] warning: entrance assignments exceed sidecar cap %d\n",
                        RANDO_SIDECAR_MAX_ENTRANCES);
                break;
            }
            rec->entrances[rec->entrance_count].location_index = (uint16_t)l;
            rec->entrances[rec->entrance_count].subtype = (int16_t)e;
            rec->entrance_count++;
        }
    }

    if (!SaveAll()) return false;
    fprintf(stderr, "[RANDO] saved sidecar slot %d (%u locations)\n", slot, rec->count);
    return true;
}

bool Port_RandoSave_LoadSlot(int slot) {
    if (slot < 0 || slot >= RANDO_SIDECAR_SLOTS) return false;
    if (!LoadAll()) return false;

    RandoSidecarSlot* rec = &sSidecar.slots[slot];
    if (!rec->active || rec->count == 0 || rec->count > RANDO_LOCATION_COUNT) return false;

    RandomizerSettings settings = Rando_DefaultSettings();
    settings.glitchless_logic = rec->glitchless_logic != 0;
    settings.shuffle_kinstones = rec->shuffle_kinstones != 0;
    settings.shuffle_dojos = rec->shuffle_dojos != 0;
    if (rec->item_difficulty < RANDO_ITEM_POOL_COUNT) {
        settings.item_difficulty = (RandoItemPoolDifficulty)rec->item_difficulty;
    }

    /* Restore the seed's .logic define overrides and reparse BEFORE
     * activation, so eventdefine-driven runtime features (damage multi,
     * cosmetics, start-inventory context) evaluate against the settings the
     * seed was generated with — not the file defaults. The stored item table
     * below stays authoritative for placement regardless. */
    if (RandoLogic_IsLoaded()) {
        RandoLogic_ClearOverrides();
        for (uint32_t o = 0; o < rec->override_count; ++o) {
            RandoLogic_SetOverride(rec->overrides[o].name, rec->overrides[o].value);
        }
        RandoLogic_Reparse();
    }

    if (!Rando_ActivateTable(rec->seed, settings, rec->table, rec->count)) return false;

    /* Entrance assignments are generation-time state: re-inject them when the
     * reparsed logic matches the parse the slot was saved under. */
    if (RandoLogic_IsLoaded() && rec->entrance_count > 0) {
        if (rec->logic_location_count == RandoLogic_GetLocationCountRaw()) {
            RandoLogic_ClearEntranceAssignments();
            for (uint32_t e = 0; e < rec->entrance_count; ++e) {
                if (rec->entrances[e].subtype < 0) continue;
                RandoLogic_RestoreEntranceAssignment(rec->entrances[e].location_index,
                                                     rec->entrances[e].subtype);
            }
        } else {
            fprintf(stderr,
                    "[RANDO] warning: .logic changed since slot %d was saved "
                    "(%u vs %u locations); entrance shuffle not restored\n",
                    slot, rec->logic_location_count, RandoLogic_GetLocationCountRaw());
        }
    }
    fprintf(stderr, "[RANDO] loaded sidecar slot %d (%u locations, %u overrides, %u entrances)\n",
            slot, rec->count, rec->override_count, rec->entrance_count);
    return true;
}

void Port_RandoSave_ClearSlot(int slot) {
    if (slot < 0 || slot >= RANDO_SIDECAR_SLOTS) return;
    (void)LoadAll();
    memset(&sSidecar.slots[slot], 0, sizeof(sSidecar.slots[slot]));
    (void)SaveAll();
}
