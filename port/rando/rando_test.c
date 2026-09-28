#include "rando/rando.h"
#include "rando/rando_entrance.h"
#include "rando/rando_logic.h"
#include "item_ids.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint16_t first_items[RANDO_LOGIC_MAX_LOCATIONS];
static uint8_t first_subtypes[RANDO_LOGIC_MAX_LOCATIONS];
static uint16_t restored_items[RANDO_LOGIC_MAX_LOCATIONS];
static uint8_t restored_subtypes[RANDO_LOGIC_MAX_LOCATIONS];

const char* Port_Save_GetActivePath(void) {
    return "rando_test.sav";
}

static int IsShuffledReward(RandoLogicLocationType type) {
    return type == RANDO_LOGIC_LOCATION_DUNGEON_PRIZE || type == RANDO_LOGIC_LOCATION_MAJOR ||
           type == RANDO_LOGIC_LOCATION_DUNGEON || type == RANDO_LOGIC_LOCATION_ANY ||
           type == RANDO_LOGIC_LOCATION_MINOR;
}

static int TestLogicSeed(void) {
    RandomizerSettings settings = Rando_DefaultSettings();
    uint64_t chosen = 0;
    if (Rando_GenerateSeed(8, &settings, &chosen) != RANDO_OK || chosen != 8 ||
        !Rando_IsLogicSeed() || !Rando_VerifyCurrentSeed())
        return 0;

    size_t count = Rando_GetLocationCount();
    uint64_t fingerprint = Rando_GetLogicFingerprint();
    if (count <= RANDO_LOCATION_COUNT || count > RANDO_LOGIC_MAX_LOCATIONS || fingerprint == 0)
        return 0;
    memcpy(first_items, Rando_GetRandomizedItemTable(), count * sizeof(first_items[0]));
    memcpy(first_subtypes, Rando_GetRandomizedItemSubtypeTable(), count);

    unsigned checks = 0;
    for (uint32_t i = 0; i < count; ++i) {
        if (!IsShuffledReward(RandoLogic_GetLocationType(i)))
            continue;
        uint32_t key = RandoLogic_GetLocationKeyAt(i);
        uint8_t item = 0xFF, subtype = 0xFF;
        if (key == UINT32_MAX || RandoLogic_FindLocationByKey(key) != (int)i ||
            !Rando_OverrideLocationKey(key, &item, &subtype) ||
            item != first_items[i] || subtype != first_subtypes[i]) {
            fprintf(stderr, "rando_test: lost native award at %s\n", RandoLogic_GetLocationName(i));
            return 0;
        }
        ++checks;
    }
    if (checks < 250) {
        fprintf(stderr, "rando_test: only %u shuffled rewards bound\n", checks);
        return 0;
    }

    if (Rando_GenerateSeed(8, &settings, &chosen) != RANDO_OK || chosen != 8 ||
        count != Rando_GetLocationCount() || fingerprint != Rando_GetLogicFingerprint() ||
        memcmp(first_items, Rando_GetRandomizedItemTable(), count * sizeof(first_items[0])) != 0 ||
        memcmp(first_subtypes, Rando_GetRandomizedItemSubtypeTable(), count) != 0)
        return 0;
    fprintf(stderr, "rando_test: seed 8 deterministic, %u native rewards keyed\n", checks);
    Rando_Reset();
    return 1;
}

static int TestRestoredOverridesDoNotLeak(void) {
    RandomizerSettings settings = Rando_DefaultSettings();
    if (Rando_GenerateSeed(8, &settings, NULL) != RANDO_OK)
        return 0;
    size_t count = Rando_GetLocationCount();
    uint64_t clean_fingerprint = Rando_GetLogicFingerprint();
    memcpy(first_items, Rando_GetRandomizedItemTable(), count * sizeof(first_items[0]));
    memcpy(first_subtypes, Rando_GetRandomizedItemSubtypeTable(), count);

    Rando_Reset();
    if (RandoLogic_GetOverrideCount() != 0)
        return 0;
    RandoLogic_SetOverride("TEST_STALE_OVERRIDE", "true");
    if (Rando_GenerateSeed(9, &settings, NULL) != RANDO_OK ||
        Rando_GetLogicFingerprint() == clean_fingerprint)
        return 0;
    size_t restored_count = Rando_GetLocationCount();
    uint64_t restored_fingerprint = Rando_GetLogicFingerprint();
    memcpy(restored_items, Rando_GetRandomizedItemTable(), restored_count * sizeof(restored_items[0]));
    memcpy(restored_subtypes, Rando_GetRandomizedItemSubtypeTable(), restored_count);
    if (!Rando_ActivateLogicTable(9, settings, restored_items, restored_subtypes, restored_count,
                                  restored_fingerprint) ||
        Rando_GenerateSeed(8, &settings, NULL) != RANDO_OK ||
        Rando_GetLocationCount() != count || Rando_GetLogicFingerprint() != clean_fingerprint ||
        memcmp(first_items, Rando_GetRandomizedItemTable(), count * sizeof(first_items[0])) != 0 ||
        memcmp(first_subtypes, Rando_GetRandomizedItemSubtypeTable(), count) != 0) {
        fprintf(stderr, "rando_test: restored parser overrides leaked into new seed\n");
        return 0;
    }
    Rando_Reset();
    return RandoLogic_GetOverrideCount() == 0;
}

static int TestUnsupportedAwards(void) {
    RandomizerSettings settings = Rando_DefaultSettings();
    if (Rando_GenerateSeed(8, &settings, NULL) != RANDO_OK || !Rando_IsActive())
        return 0;
    RandoLogic_ClearOverrides();
    RandoLogic_SetOverride("YES_SWORD_PROG", "true");
    RandoStatus result = Rando_GenerateSeed(8, &settings, NULL);
    int inactive = !Rando_IsActive();
    RandoLogic_ClearOverrides();
    Rando_Reset();
    return result == RANDO_BAD_SETTINGS && inactive;
}

static int TestPoolsAndKeysanity(void) {
    RandomizerSettings settings = Rando_DefaultSettings();
    settings.shuffle_dungeon_items = true;
    for (int pool = RANDO_ITEM_POOL_NORMAL; pool < RANDO_ITEM_POOL_COUNT; ++pool) {
        settings.item_difficulty = (RandoItemPoolDifficulty)pool;
        if (Rando_GenerateSeed(8, &settings, NULL) != RANDO_OK || !Rando_VerifyCurrentSeed()) {
            fprintf(stderr, "rando_test: keysanity pool %d failed\n", pool);
            return 0;
        }
        const uint16_t* items = Rando_GetRandomizedItemTable();
        const uint8_t* subtypes = Rando_GetRandomizedItemSubtypeTable();
        unsigned dungeon_awards = 0;
        for (uint32_t i = 0; i < Rando_GetLocationCount(); ++i) {
            if (!IsShuffledReward(RandoLogic_GetLocationType(i)))
                continue;
            if (items[i] == ITEM_SMALL_KEY || items[i] == ITEM_BIG_KEY ||
                items[i] == ITEM_DUNGEON_MAP || items[i] == ITEM_COMPASS) {
                if (!RANDO_SUBTYPE_HAS_ORIGIN(subtypes[i]) || RANDO_SUBTYPE_ORIGIN(subtypes[i]) < 1 ||
                    RANDO_SUBTYPE_ORIGIN(subtypes[i]) > 7) {
                    fprintf(stderr, "rando_test: invalid dungeon origin pool %d at %s (item=%u subtype=%u)\n",
                            pool, RandoLogic_GetLocationName(i), items[i], subtypes[i]);
                    return 0;
                }
                ++dungeon_awards;
            }
        }
        if (dungeon_awards < 40) {
            fprintf(stderr, "rando_test: only %u dungeon awards in keysanity pool %d\n", dungeon_awards, pool);
            return 0;
        }
        Rando_Reset();
    }
    return 1;
}

static int TestObscureLocations(void) {
    RandomizerSettings settings = Rando_DefaultSettings();
    settings.obscure_locations = true;
    if (Rando_GenerateSeed(8, &settings, NULL) != RANDO_OK || !Rando_VerifyCurrentSeed())
        return 0;
    unsigned checks = 0;
    for (uint32_t i = 0; i < Rando_GetLocationCount(); ++i) {
        if (IsShuffledReward(RandoLogic_GetLocationType(i)))
            ++checks;
    }
    if (checks != 352)
        fprintf(stderr, "rando_test: obscure profile has %u checks, expected 352\n", checks);
    Rando_Reset();
    return checks == 352;
}

static int TestSpoiler(void) {
    RandomizerSettings settings = Rando_DefaultSettings();
    settings.obscure_locations = true;
    char* generated = NULL;
    char* restored = NULL;
    int passed = 0;
    if (Rando_GenerateSeed(8, &settings, NULL) != RANDO_OK)
        goto done;

    const size_t count = Rando_GetLocationCount();
    const uint64_t fingerprint = Rando_GetLogicFingerprint();
    const size_t required = Rando_GetSpoiler(NULL, 0);
    char small[32];
    if (required <= 4096 || Rando_GetSpoiler(small, sizeof(small)) != required ||
        small[sizeof(small) - 1] != '\0')
        goto done;

    generated = (char*)malloc(required);
    restored = (char*)malloc(required);
    if (generated == NULL || restored == NULL ||
        Rando_GetSpoiler(generated, required) != required || strlen(generated) + 1 != required)
        goto done;

    uint32_t late = UINT32_MAX;
    for (uint32_t i = (uint32_t)count; i > 0; --i) {
        const uint32_t index = i - 1;
        if (IsShuffledReward(RandoLogic_GetLocationType(index)) &&
            RandoLogic_GetLocationKeyAt(index) != UINT32_MAX &&
            !RandoLogic_LocationHasTagName(index, "NoSpoiler")) {
            late = index;
            break;
        }
    }
    if (late == UINT32_MAX)
        goto done;
    char prefix[256];
    const int prefix_len = snprintf(prefix, sizeof(prefix), "%-40s : ", RandoLogic_GetLocationName(late));
    if (prefix_len <= 0 || (size_t)prefix_len >= sizeof(prefix) || strstr(generated, prefix) == NULL) {
        fprintf(stderr, "rando_test: late keyed location missing from spoiler: %s\n",
                RandoLogic_GetLocationName(late));
        goto done;
    }

    memcpy(restored_items, Rando_GetRandomizedItemTable(), count * sizeof(restored_items[0]));
    memcpy(restored_subtypes, Rando_GetRandomizedItemSubtypeTable(), count);
    if (!Rando_ActivateLogicTable(8, settings, restored_items, restored_subtypes, count, fingerprint) ||
        Rando_GetSpoiler(NULL, 0) != required || Rando_GetSpoiler(restored, required) != required ||
        strcmp(generated, restored) != 0) {
        fprintf(stderr, "rando_test: saved-table spoiler differs from generated spoiler\n");
        goto done;
    }
    passed = 1;

done:
    free(restored);
    free(generated);
    Rando_Reset();
    return passed;
}

static int TestEntranceAssignments(void) {
    RandomizerSettings settings = Rando_DefaultSettings();
    settings.shuffle_entrances = true;
    RandoStatus result = Rando_GenerateSeed(8, &settings, NULL);
    if (result != RANDO_OK || !Rando_VerifyCurrentSeed()) {
        fprintf(stderr, "rando_test: entrance seed generation/verification failed (status=%d)\n", result);
        return 0;
    }
    unsigned seen = 0;
    int unshuffled_dhc = -1;
    for (int i = 0; i < 8; ++i) {
        int dungeon = Rando_Entrance_GetAssignment(i);
        if (dungeon < 0 && i >= 6 && unshuffled_dhc < 0) {
            unshuffled_dhc = i;
            continue;
        }
        if (dungeon < 0 || dungeon >= 8 || (seen & (1u << dungeon))) {
            fprintf(stderr, "rando_test: entrance %d has invalid/duplicate destination %d (seen=%02x)\n",
                    i, dungeon, seen);
            return 0;
        }
        seen |= 1u << dungeon;
    }
    if (unshuffled_dhc < 0 || seen != (0xFFu ^ (1u << unshuffled_dhc)) ||
        Rando_Entrance_GetInverseAssignment(unshuffled_dhc) != -1) {
        fprintf(stderr, "rando_test: incomplete 7-way shuffle (unshuffled=%d seen=%02x)\n",
                unshuffled_dhc, seen);
        return 0;
    }

    int saved_assignments[8];
    for (int i = 0; i < 8; ++i)
        saved_assignments[i] = Rando_Entrance_GetAssignment(i);
    const size_t count = Rando_GetLocationCount();
    const uint64_t fingerprint = Rando_GetLogicFingerprint();
    memcpy(restored_items, Rando_GetRandomizedItemTable(), count * sizeof(restored_items[0]));
    memcpy(restored_subtypes, Rando_GetRandomizedItemSubtypeTable(), count);
    if (!Rando_ActivateLogicTable(8, settings, restored_items, restored_subtypes, count, fingerprint))
        return 0;

    const size_t original_size = Rando_GetSpoiler(NULL, 0);
    char* original = (char*)malloc(original_size);
    char* updated = NULL;
    int spoiler_ok = 0;
    if (original == NULL || Rando_GetSpoiler(original, original_size) != original_size ||
        strstr(original, "Entrances:\n") == NULL)
        goto spoiler_done;
    Rando_Entrance_ClearAssignments();
    const size_t changed_size = Rando_GetSpoiler(NULL, 0);
    updated = (char*)malloc(changed_size > original_size ? changed_size : original_size);
    if (updated == NULL || Rando_GetSpoiler(updated, changed_size) != changed_size ||
        strcmp(original, updated) == 0)
        goto spoiler_done;
    for (int i = 0; i < 8; ++i)
        Rando_Entrance_SetAssignment(i, saved_assignments[i]);
    if (Rando_GetSpoiler(NULL, 0) != original_size ||
        Rando_GetSpoiler(updated, original_size) != original_size ||
        strcmp(original, updated) != 0)
        goto spoiler_done;
    spoiler_ok = 1;

spoiler_done:
    free(updated);
    free(original);
    if (!spoiler_ok) {
        fprintf(stderr, "rando_test: saved entrance destinations missing or stale in spoiler\n");
        return 0;
    }
    Rando_Reset();
    return 1;
}

static int TestLegacyTable(void) {
    uint16_t items[RANDO_LOCATION_COUNT];
    uint8_t subtypes[RANDO_LOCATION_COUNT] = { 0 };
    for (unsigned i = 0; i < RANDO_LOCATION_COUNT; ++i)
        items[i] = Rando_GetLocationDef((RandoLocationId)i)->vanilla_item;
    if (!Rando_ActivateTable(123, Rando_DefaultSettings(), items, subtypes, RANDO_LOCATION_COUNT) ||
        !Rando_IsActive() || Rando_IsLogicSeed() || Rando_GetLocationCount() != RANDO_LOCATION_COUNT)
        return 0;
    Rando_Reset();
    return 1;
}

int main(void) {
    if (!TestLogicSeed() || !TestRestoredOverridesDoNotLeak() || !TestPoolsAndKeysanity() ||
        !TestObscureLocations() || !TestSpoiler() || !TestEntranceAssignments() ||
        !TestUnsupportedAwards() || !TestLegacyTable()) {
        fprintf(stderr, "rando_test: FAIL\n");
        return 1;
    }
    fprintf(stderr, "ALL TESTS PASS\n");
    return 0;
}
