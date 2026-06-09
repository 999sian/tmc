/*
 * port/rando/rando.h — clean-room in-process randomizer.
 *
 * Native graph logic: no ROM patching, no external randomizer process, no
 * heap-owned containers in generation. The active seed writes a fixed
 * LOCATION_COUNT item table that engine reward hooks can query directly.
 */

#ifndef PORT_RANDO_RANDO_H
#define PORT_RANDO_RANDO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    RANDO_OK = 0,
    RANDO_INACTIVE,
    RANDO_INTERNAL,
    RANDO_UNBEATABLE,
    RANDO_BAD_SETTINGS,
} RandoStatus;

#define RANDO_RANDOMIZED_ITEM_TABLE_CAPACITY 4096u

typedef enum {
    RANDO_ITEM_POOL_NORMAL = 0,
    RANDO_ITEM_POOL_HARD,
    RANDO_ITEM_POOL_CHAOS,
    RANDO_ITEM_POOL_COUNT,

} RandoItemPoolDifficulty;

typedef struct RandomizerSettings {
    bool glitchless_logic;
    bool shuffle_kinstones;
    bool shuffle_dojos;
    RandoItemPoolDifficulty item_difficulty;
} RandomizerSettings;

typedef enum {
    RANDO_LOC_CATEGORY_CHEST = 0,
    RANDO_LOC_CATEGORY_DIG_SPOT,
    RANDO_LOC_CATEGORY_GROTTO,
    RANDO_LOC_CATEGORY_HEART_PIECE,
    RANDO_LOC_CATEGORY_DOJO,
    RANDO_LOC_CATEGORY_KEY_ITEM,
    RANDO_LOC_CATEGORY_ELEMENT,
    RANDO_LOC_CATEGORY_KINSTONE_FUSION,
} RandoLocationCategory;

typedef enum {
    RANDO_LOC_LINK_HOUSE_GIFT = 0,
    RANDO_LOC_SMITH_STORAGE_CHEST,
    RANDO_LOC_MINISH_WOODS_HEART,
    RANDO_LOC_MINISH_VILLAGE_REWARD,
    RANDO_LOC_TOWN_SHOP_COUNTER,
    RANDO_LOC_TOWN_DOJO_SPIN,
    RANDO_LOC_DEEPWOOD_ENTRANCE_CHEST,
    RANDO_LOC_DEEPWOOD_BIG_CHEST,
    RANDO_LOC_DEEPWOOD_EARTH_ELEMENT,
    RANDO_LOC_CRENEL_DIG_SPOT,
    RANDO_LOC_CRENEL_GROTTO,
    RANDO_LOC_CRENEL_HERMIT,
    RANDO_LOC_CAVE_FLAMES_BIG_CHEST,
    RANDO_LOC_CAVE_FLAMES_FIRE_ELEMENT,
    RANDO_LOC_CASTOR_WILDS_GROTTO,
    RANDO_LOC_CASTOR_WILDS_HEART,
    RANDO_LOC_FORTRESS_ENTRANCE_CHEST,
    RANDO_LOC_FORTRESS_BIG_CHEST,
    RANDO_LOC_FORTRESS_WIND_ELEMENT,
    RANDO_LOC_LAKE_HYLIA_FUSION,
    RANDO_LOC_LAKE_HYLIA_DOJO_DASH,
    RANDO_LOC_DROPLETS_ENTRANCE_CHEST,
    RANDO_LOC_DROPLETS_BIG_CHEST,
    RANDO_LOC_DROPLETS_WATER_ELEMENT,
    RANDO_LOC_ROYAL_VALLEY_DIG_CAVE,
    RANDO_LOC_GRAVEYARD_KEY_REWARD,
    RANDO_LOC_HYLIA_GROTTO,
    RANDO_LOC_CLOUD_TOPS_CHEST,
    RANDO_LOC_CLOUD_TOPS_FUSION,
    RANDO_LOC_PALACE_WINDS_ENTRANCE,
    RANDO_LOC_PALACE_WINDS_BIG_CHEST,
    RANDO_LOC_PALACE_WINDS_REWARD,
    RANDO_LOC_DARK_HYRULE_CASTLE_ENTRANCE,
    RANDO_LOC_DARK_HYRULE_CASTLE_CHEST,
    RANDO_LOC_FINAL_DOJO_GREAT_SPIN,
    RANDO_LOC_FINAL_KINSTONE_REWARD,
    RANDO_LOCATION_BUILTIN_COUNT,
    RANDO_LOCATION_COUNT = RANDO_RANDOMIZED_ITEM_TABLE_CAPACITY,
} RandoLocationId;

typedef struct RandoLocationDef {
    RandoLocationId id;
    RandoLocationCategory category;
    const char* name;
    uint16_t vanilla_item;
    uint64_t requirements;
} RandoLocationDef;

extern uint16_t randomized_item_table[RANDO_LOCATION_COUNT];

RandomizerSettings Rando_DefaultSettings(void);
uint64_t Rando_SeedFromString(const char* text);

/* Required API for the file-select UI. */
bool GenerateSeed(uint64_t seed, RandomizerSettings settings);

RandoStatus Rando_GenerateSeed(uint64_t seed, const RandomizerSettings* settings, uint64_t* out_seed);
void Rando_Reset(void);
bool Rando_IsActive(void);
uint32_t Rando_GetSeed(void);
uint64_t Rando_GetSeed64(void);
RandomizerSettings Rando_GetSettings(void);
const uint16_t* Rando_GetRandomizedItemTable(void);
size_t Rando_GetLocationCount(void);
const RandoLocationDef* Rando_GetLocationDef(RandoLocationId id);
uint16_t Rando_ResolveLocationItem(RandoLocationId location, uint16_t vanilla_item);
bool Rando_ActivateTable(uint64_t seed, RandomizerSettings settings, const uint16_t* table, size_t count);

bool Rando_VerifyCurrentSeed(void);

bool Rando_OverrideLocationKey(uint32_t location_key, uint8_t* type, uint8_t* subtype);
/* Back-compat wrapper used by existing F8/ImGui menus. */
RandoStatus Rando_RollSeed(uint32_t seed, uint32_t* out_seed);

/* Existing centralized item-give fallback. Prefer
 * Rando_ResolveLocationItem() for new location-aware reward hooks. */
bool Rando_OverrideItem(uint8_t* type, uint8_t* subtype);

size_t Rando_GetSpoiler(char* buf, size_t buflen);

#ifdef __cplusplus
}
#endif

#endif /* PORT_RANDO_RANDO_H */
