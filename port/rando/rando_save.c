/*
 * Native randomizer save sidecar.
 *
 * Keeps seed/settings/item table outside the vanilla EEPROM layout. One file
 * follows the active save profile: tmc.sav -> tmc.randomizer,
 * tmc_profile.sav -> tmc_profile.randomizer.
 */

#include "rando/rando_save.h"
#include "rando/rando.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define RANDO_SIDECAR_SLOTS 3
/* Sidecar format version. Slot location keys/indices encode area-room-chest
 * where the chest index is the TileEntity iteration order from room data
 * (see Rando_RoomChestIndex). ANY change to that encoding or to the slot
 * layout below requires bumping this version. */
#define RANDO_SIDECAR_VERSION 1u

static const char kMagic[8] = { 'T', 'M', 'C', 'R', 'N', 'D', 'O', '1' };

extern const char* Port_Save_GetActivePath(void);

typedef struct RandoSidecarSlot {
    uint8_t active;
    uint8_t glitchless_logic;
    uint8_t shuffle_kinstones;
    uint8_t shuffle_dojos;
    uint8_t item_difficulty;
    uint8_t reserved[3];
    uint64_t seed;
    uint32_t count;
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
            rec->item_difficulty >= RANDO_ITEM_POOL_COUNT) {
            fprintf(stderr,
                    "[rando] warning: sidecar slot %d corrupt (count=%u, difficulty=%u); cleared\n",
                    i, rec->count, rec->item_difficulty);
            memset(rec, 0, sizeof(*rec));
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

    if (!Rando_ActivateTable(rec->seed, settings, rec->table, rec->count)) return false;
    fprintf(stderr, "[RANDO] loaded sidecar slot %d (%u locations)\n", slot, rec->count);
    return true;
}

void Port_RandoSave_ClearSlot(int slot) {
    if (slot < 0 || slot >= RANDO_SIDECAR_SLOTS) return;
    (void)LoadAll();
    memset(&sSidecar.slots[slot], 0, sizeof(sSidecar.slots[slot]));
    (void)SaveAll();
}
