#include "rando/rando.h"
#include "rando/rando_logic.h"
#include "rando/rando_save.h"
#include "item_ids.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint16_t first_items[RANDO_LOGIC_MAX_LOCATIONS];
static uint8_t first_subtypes[RANDO_LOGIC_MAX_LOCATIONS];
static uint16_t restored_items[RANDO_LOGIC_MAX_LOCATIONS];
static uint8_t restored_subtypes[RANDO_LOGIC_MAX_LOCATIONS];
static uint16_t gate_items[RANDO_LOGIC_MAX_LOCATIONS];
static uint8_t gate_subtypes[RANDO_LOGIC_MAX_LOCATIONS];
static bool gate_reached[RANDO_LOGIC_MAX_LOCATIONS];
static unsigned default_check_count;
static bool gate_owns_mitts;

const char* Port_Save_GetActivePath(void) {
    const char* path = getenv("TMC_RANDO_TEST_SAVE");
    return path != NULL ? path : "rando_test.sav";
}

static int IsShuffledReward(RandoLogicLocationType type) {
    return type == RANDO_LOGIC_LOCATION_DUNGEON_PRIZE || type == RANDO_LOGIC_LOCATION_MAJOR ||
           type == RANDO_LOGIC_LOCATION_DUNGEON || type == RANDO_LOGIC_LOCATION_ANY ||
           type == RANDO_LOGIC_LOCATION_MINOR;
}

static int TestBuiltInRuleCompatibility(void) {
    struct ExpectedLocation {
        uint32_t index;
        const char* name;
        uint32_t key;
    } expected[] = {
        {0, "StartSword", UINT32_MAX},
        {23, "Chest_03_08_00", 0x030800u},
        {65, "Chest_19_00_00", 0x190000u},
        {100, "Chest_30_00_01", 0x300001u},
        {214, "Ground_00_00_3C", 0x00003cu},
        {325, "SouthField_Tingle_NPC", UINT32_MAX},
    };
    RandoLogic_ClearOverrides();
    if (!RandoLogic_LoadBuiltIn() || RandoLogic_GetLocationCountRaw() != 326 ||
        RandoLogic_SourceFingerprint() != UINT64_C(0x37ea4d0a0957c8e4)) {
        fprintf(stderr, "rando_test: built-in rule version changed\n");
        return 0;
    }
    for (size_t i = 0; i < sizeof(expected) / sizeof(expected[0]); ++i) {
        const struct ExpectedLocation* loc = &expected[i];
        if (strcmp(RandoLogic_GetLocationName(loc->index), loc->name) != 0 ||
            RandoLogic_GetLocationKeyAt(loc->index) != loc->key) {
            fprintf(stderr, "rando_test: saved rule location %u changed\n", loc->index);
            return 0;
        }
    }
    RandoLogic_Reset();
    return 1;
}

static int TestLogicSeed(void) {
    RandomizerSettings settings = Rando_DefaultSettings();
    uint64_t chosen = 0;
    if (Rando_GenerateSeed(8, &settings, &chosen) != RANDO_OK || chosen != 8 ||
        !Rando_IsLogicSeed() || !Rando_VerifyCurrentSeed())
        return 0;

    size_t count = Rando_GetLocationCount();
    uint64_t fingerprint = Rando_GetLogicFingerprint();
    if (count != 326 || fingerprint != UINT64_C(0x799dfc903d9bf3fd))
        return 0;
    memcpy(first_items, Rando_GetRandomizedItemTable(), count * sizeof(first_items[0]));
    memcpy(first_subtypes, Rando_GetRandomizedItemSubtypeTable(), count);

    unsigned checks = 0;
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t key = RandoLogic_GetLocationKeyAt(i);
        if (key != UINT32_MAX && RandoLogic_FindLocationByKey(key) != (int)i) {
            fprintf(stderr, "rando_test: duplicate native key at %s\n", RandoLogic_GetLocationName(i));
            return 0;
        }
        if (!IsShuffledReward(RandoLogic_GetLocationType(i)))
            continue;
        uint8_t item = 0xFF, subtype = 0xFF;
        if (key == UINT32_MAX || RandoLogic_FindLocationByKey(key) != (int)i ||
            !Rando_OverrideLocationKey(key, &item, &subtype) ||
            item != first_items[i] || subtype != first_subtypes[i]) {
            fprintf(stderr, "rando_test: lost native award at %s\n", RandoLogic_GetLocationName(i));
            return 0;
        }
        ++checks;
    }
    if (checks < 259) {
        fprintf(stderr, "rando_test: only %u shuffled rewards bound\n", checks);
        return 0;
    }
    default_check_count = checks;

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
        fprintf(stderr, "rando_test: restored rule overrides leaked into new seed\n");
        return 0;
    }
    Rando_Reset();
    return RandoLogic_GetOverrideCount() == 0;
}

static uint16_t GateInventory(const char* name) {
    return strcmp(name, "Items.MoleMitts") == 0 ? (uint16_t)gate_owns_mitts : 99;
}

static int TestHyliaDigGate(void) {
    RandomizerSettings settings = Rando_DefaultSettings();
    if (Rando_GenerateSeed(8, &settings, NULL) != RANDO_OK)
        return 0;
    size_t count = Rando_GetLocationCount();
    int chest = RandoLogic_FindLocationByKey(0x00190000u);
    if (chest < 0 || strcmp(RandoLogic_GetLocationName((uint32_t)chest), "Chest_19_00_00") != 0)
        return 0;
    memset(gate_items, 0, count * sizeof(gate_items[0]));
    memset(gate_subtypes, 0, count);
    gate_items[chest] = ITEM_MOLE_MITTS;
    gate_owns_mitts = false;
    RandoLogic_EvaluateReachability(gate_items, gate_subtypes, count, 0, GateInventory, gate_reached,
                                    (uint32_t)count);
    if (gate_reached[chest]) {
        fprintf(stderr, "rando_test: Hylia dig chest reachable before Mole Mitts\n");
        return 0;
    }
    gate_owns_mitts = true;
    RandoLogic_EvaluateReachability(gate_items, gate_subtypes, count, 0, GateInventory, gate_reached,
                                    (uint32_t)count);
    int reachable_with_mitts = gate_reached[chest];
    Rando_Reset();
    return reachable_with_mitts;
}

static int TestUnsupportedModes(void) {
    RandomizerSettings settings = Rando_DefaultSettings();
    settings.shuffle_dungeon_items = true;
    if (Rando_GenerateSeed(8, &settings, NULL) != RANDO_BAD_SETTINGS || Rando_IsActive())
        return 0;
    settings = Rando_DefaultSettings();
    settings.shuffle_entrances = true;
    if (Rando_GenerateSeed(8, &settings, NULL) != RANDO_BAD_SETTINGS || Rando_IsActive())
        return 0;
    settings = Rando_DefaultSettings();
    settings.accessibility = RANDO_ACCESS_ALL_NONKEYS;
    if (Rando_GenerateSeed(8, &settings, NULL) != RANDO_BAD_SETTINGS || Rando_IsActive())
        return 0;
    settings.accessibility = RANDO_ACCESS_ALL_LOCATIONS;
    return Rando_GenerateSeed(8, &settings, NULL) == RANDO_BAD_SETTINGS && !Rando_IsActive();
}

static int TestItemPools(void) {
    RandomizerSettings settings = Rando_DefaultSettings();
    for (int pool = RANDO_ITEM_POOL_NORMAL; pool < RANDO_ITEM_POOL_COUNT; ++pool) {
        settings.item_difficulty = (RandoItemPoolDifficulty)pool;
        if (Rando_GenerateSeed(8, &settings, NULL) != RANDO_OK || !Rando_VerifyCurrentSeed()) {
            fprintf(stderr, "rando_test: item pool %d failed\n", pool);
            return 0;
        }
        unsigned checks = 0;
        for (uint32_t i = 0; i < Rando_GetLocationCount(); ++i) {
            if (IsShuffledReward(RandoLogic_GetLocationType(i)))
                ++checks;
        }
        if (checks < 259) {
            fprintf(stderr, "rando_test: only %u rewards in item pool %d\n", checks, pool);
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
    if (checks <= default_check_count)
        fprintf(stderr, "rando_test: extra ground checks missing (%u vs %u default)\n",
                checks, default_check_count);
    Rando_Reset();
    return checks > default_check_count;
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
    if (strstr(generated, RandoLogic_GetLocationName(late)) == NULL) {
        fprintf(stderr, "rando_test: late keyed location missing from spoiler: %s\n",
                RandoLogic_GetLocationName(late));
        goto done;
    }
    char chest_line[128];
    char ground_line[128];
    snprintf(chest_line, sizeof(chest_line), "%-40s [area 0x03, room 0x08, chest #1] : ", "Chest_03_08_00");
    snprintf(ground_line, sizeof(ground_line), "%-40s [area 0x00, room 0x00, ground flag 0x3C] : ",
             "Ground_00_00_3C");
    if (strstr(generated, chest_line) == NULL || strstr(generated, ground_line) == NULL ||
        strstr(generated, "Hyrule Town - Swiftblade's dojo - Spin Attack lesson [Town_Dojo_NPC1] : ") == NULL) {
        fprintf(stderr, "rando_test: spoiler missing exact physical or scripted check location\n");
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
    if (!TestBuiltInRuleCompatibility() || !TestLogicSeed() || !TestHyliaDigGate() || !TestRestoredOverridesDoNotLeak() || !TestItemPools() ||
        !TestObscureLocations() || !TestSpoiler() || !TestUnsupportedModes() || !TestLegacyTable()) {
        fprintf(stderr, "rando_test: FAIL\n");
        return 1;
    }
    if (getenv("TMC_RANDO_TEST_SAVE") != NULL) {
        int loaded = 0;
        for (int slot = 0; slot < 3; ++slot) {
            if (Port_RandoSave_LoadSlot(slot)) {
                ++loaded;
                Rando_Reset();
            }
        }
        if (loaded == 0) {
            fprintf(stderr, "rando_test: no saved randomizer slot could be loaded\n");
            return 1;
        }
        fprintf(stderr, "rando_test: loaded %d existing randomizer slot(s)\n", loaded);
    }
    fprintf(stderr, "ALL TESTS PASS\n");
    return 0;
}
