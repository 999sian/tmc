/*
 * port/rando/rando.cpp — fixed-array clean-room graph randomizer.
 *
 * Generation is intentionally boring: a compact DAG of native Location
 * records, SplitMix64 for deterministic entropy, fixed scratch arrays, and a
 * forward-fill pass followed by verification. No ROM bytes are patched and no
 * heap containers are used by the generation or item-override paths.
 */

#include "rando.h"
#include "rando_logic.h"

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#define RANDO_ARRAY_COUNT(a) (sizeof(a) / sizeof((a)[0]))

/* Authoritative engine item ids (shared with the C engine). */
#include "item_ids.h"

enum ProgressionFlag : uint64_t {
    RF_CAN_MINISH       = 1ull << 0,
    RF_SWORD            = 1ull << 1,
    RF_GUST_JAR         = 1ull << 2,
    RF_PACCI_CANE       = 1ull << 3,
    RF_MOLE_MITTS       = 1ull << 4,
    RF_ROCS_CAPE        = 1ull << 5,
    RF_PEGASUS_BOOTS    = 1ull << 6,
    RF_OCARINA          = 1ull << 7,
    RF_LANTERN          = 1ull << 8,
    RF_BOW              = 1ull << 9,
    RF_GRIP_RING        = 1ull << 10,
    RF_FLIPPERS         = 1ull << 11,
    RF_KINSTONE_BAG     = 1ull << 12,
    RF_EARTH_ELEMENT    = 1ull << 13,
    RF_FIRE_ELEMENT     = 1ull << 14,
    RF_WATER_ELEMENT    = 1ull << 15,
    RF_WIND_ELEMENT     = 1ull << 16,
};

static const uint64_t kStartFlags = RF_CAN_MINISH;
static const uint64_t kGoalFlags =
    RF_SWORD | RF_GUST_JAR | RF_PACCI_CANE | RF_MOLE_MITTS |
    RF_ROCS_CAPE | RF_OCARINA | RF_EARTH_ELEMENT | RF_FIRE_ELEMENT |
    RF_WATER_ELEMENT | RF_WIND_ELEMENT;

static const RandoLocationDef kLocations[RANDO_LOCATION_BUILTIN_COUNT] = {
    { RANDO_LOC_LINK_HOUSE_GIFT,            RANDO_LOC_CATEGORY_KEY_ITEM,        "Link House Gift",              ITEM_SMITH_SWORD,        0 },
    { RANDO_LOC_SMITH_STORAGE_CHEST,        RANDO_LOC_CATEGORY_CHEST,           "Smith Storage Chest",          ITEM_RUPEE20,            0 },
    { RANDO_LOC_MINISH_WOODS_HEART,         RANDO_LOC_CATEGORY_HEART_PIECE,     "Minish Woods Heart",           ITEM_HEART_PIECE,        0 },
    { RANDO_LOC_MINISH_VILLAGE_REWARD,      RANDO_LOC_CATEGORY_KEY_ITEM,        "Minish Village Reward",        ITEM_JABBERNUT,          RF_CAN_MINISH },
    { RANDO_LOC_TOWN_SHOP_COUNTER,          RANDO_LOC_CATEGORY_KEY_ITEM,        "Town Shop Counter",            ITEM_BOTTLE1,            0 },
    { RANDO_LOC_TOWN_DOJO_SPIN,             RANDO_LOC_CATEGORY_DOJO,            "Town Dojo Spin",               ITEM_SKILL_SPIN_ATTACK,  RF_SWORD },
    { RANDO_LOC_DEEPWOOD_ENTRANCE_CHEST,    RANDO_LOC_CATEGORY_CHEST,           "Deepwood Entrance Chest",      ITEM_RUPEE50,            RF_CAN_MINISH },
    { RANDO_LOC_DEEPWOOD_BIG_CHEST,         RANDO_LOC_CATEGORY_CHEST,           "Deepwood Big Chest",           ITEM_GUST_JAR,           RF_GUST_JAR },
    { RANDO_LOC_DEEPWOOD_EARTH_ELEMENT,     RANDO_LOC_CATEGORY_ELEMENT,         "Deepwood Earth Element",       ITEM_EARTH_ELEMENT,      RF_GUST_JAR },
    { RANDO_LOC_CRENEL_DIG_SPOT,            RANDO_LOC_CATEGORY_DIG_SPOT,        "Crenel Dig Spot",              ITEM_RUPEE100,           RF_GUST_JAR },
    { RANDO_LOC_CRENEL_GROTTO,              RANDO_LOC_CATEGORY_GROTTO,          "Crenel Grotto",                ITEM_GRIP_RING,          RF_GUST_JAR | RF_PACCI_CANE },
    { RANDO_LOC_CRENEL_HERMIT,              RANDO_LOC_CATEGORY_KEY_ITEM,        "Crenel Hermit Reward",         ITEM_KINSTONE_BAG,       RF_GUST_JAR | RF_PACCI_CANE },
    { RANDO_LOC_CAVE_FLAMES_BIG_CHEST,      RANDO_LOC_CATEGORY_CHEST,           "Cave of Flames Big Chest",     ITEM_PACCI_CANE,         RF_PACCI_CANE | RF_GRIP_RING },
    { RANDO_LOC_CAVE_FLAMES_FIRE_ELEMENT,   RANDO_LOC_CATEGORY_ELEMENT,         "Cave of Flames Element",       ITEM_FIRE_ELEMENT,       RF_PACCI_CANE | RF_GRIP_RING | RF_LANTERN },
    { RANDO_LOC_CASTOR_WILDS_GROTTO,        RANDO_LOC_CATEGORY_GROTTO,          "Castor Wilds Grotto",          ITEM_PEGASUS_BOOTS,      RF_PEGASUS_BOOTS },
    { RANDO_LOC_CASTOR_WILDS_HEART,         RANDO_LOC_CATEGORY_HEART_PIECE,     "Castor Wilds Heart",           ITEM_HEART_PIECE,        RF_PEGASUS_BOOTS | RF_BOW },
    { RANDO_LOC_FORTRESS_ENTRANCE_CHEST,    RANDO_LOC_CATEGORY_CHEST,           "Fortress Entrance Chest",      ITEM_MOLE_MITTS,         RF_MOLE_MITTS },
    { RANDO_LOC_FORTRESS_BIG_CHEST,         RANDO_LOC_CATEGORY_CHEST,           "Fortress Big Chest",           ITEM_BOW,                RF_MOLE_MITTS | RF_BOW },
    { RANDO_LOC_FORTRESS_WIND_ELEMENT,      RANDO_LOC_CATEGORY_ELEMENT,         "Fortress Wind Element",        ITEM_WIND_ELEMENT,       RF_MOLE_MITTS | RF_BOW },
    { RANDO_LOC_LAKE_HYLIA_FUSION,          RANDO_LOC_CATEGORY_KINSTONE_FUSION, "Lake Hylia Fusion",            ITEM_FLIPPERS,           RF_KINSTONE_BAG },
    { RANDO_LOC_LAKE_HYLIA_DOJO_DASH,       RANDO_LOC_CATEGORY_DOJO,            "Lake Hylia Dojo Dash",         ITEM_SKILL_DASH_ATTACK,  RF_PEGASUS_BOOTS },
    { RANDO_LOC_DROPLETS_ENTRANCE_CHEST,    RANDO_LOC_CATEGORY_CHEST,           "Droplets Entrance Chest",      ITEM_SMALL_KEY,          RF_FLIPPERS | RF_LANTERN },
    { RANDO_LOC_DROPLETS_BIG_CHEST,         RANDO_LOC_CATEGORY_CHEST,           "Droplets Big Chest",           ITEM_LANTERN_OFF,        RF_FLIPPERS | RF_LANTERN },
    { RANDO_LOC_DROPLETS_WATER_ELEMENT,     RANDO_LOC_CATEGORY_ELEMENT,         "Droplets Water Element",       ITEM_WATER_ELEMENT,      RF_FLIPPERS | RF_LANTERN },
    { RANDO_LOC_ROYAL_VALLEY_DIG_CAVE,      RANDO_LOC_CATEGORY_DIG_SPOT,        "Royal Valley Dig Cave",        ITEM_RUPEE200,           RF_LANTERN | RF_MOLE_MITTS },
    { RANDO_LOC_GRAVEYARD_KEY_REWARD,       RANDO_LOC_CATEGORY_KEY_ITEM,        "Graveyard Key Reward",         ITEM_QST_GRAVEYARD_KEY,  RF_LANTERN },
    { RANDO_LOC_HYLIA_GROTTO,               RANDO_LOC_CATEGORY_GROTTO,          "Lake Hylia Grotto",            ITEM_WALLET,             RF_FLIPPERS },
    { RANDO_LOC_CLOUD_TOPS_CHEST,           RANDO_LOC_CATEGORY_CHEST,           "Cloud Tops Chest",             ITEM_ROCS_CAPE,          RF_OCARINA | RF_WATER_ELEMENT },
    { RANDO_LOC_CLOUD_TOPS_FUSION,          RANDO_LOC_CATEGORY_KINSTONE_FUSION, "Cloud Tops Fusion",            ITEM_RUPEE100,           RF_OCARINA | RF_KINSTONE_BAG },
    { RANDO_LOC_PALACE_WINDS_ENTRANCE,      RANDO_LOC_CATEGORY_CHEST,           "Palace Winds Entrance",        ITEM_RUPEE50,            RF_OCARINA | RF_WATER_ELEMENT | RF_ROCS_CAPE },
    { RANDO_LOC_PALACE_WINDS_BIG_CHEST,     RANDO_LOC_CATEGORY_CHEST,           "Palace Winds Big Chest",       ITEM_OCARINA,            RF_OCARINA | RF_WATER_ELEMENT | RF_ROCS_CAPE },
    { RANDO_LOC_PALACE_WINDS_REWARD,        RANDO_LOC_CATEGORY_KEY_ITEM,        "Palace Winds Reward",          ITEM_HEART_CONTAINER,    RF_OCARINA | RF_WATER_ELEMENT | RF_ROCS_CAPE },
    { RANDO_LOC_DARK_HYRULE_CASTLE_ENTRANCE,RANDO_LOC_CATEGORY_KEY_ITEM,        "Dark Hyrule Castle Entrance",  ITEM_POWER_BRACELETS,    RF_EARTH_ELEMENT | RF_FIRE_ELEMENT | RF_WATER_ELEMENT | RF_WIND_ELEMENT | RF_SWORD },
    { RANDO_LOC_DARK_HYRULE_CASTLE_CHEST,   RANDO_LOC_CATEGORY_CHEST,           "Dark Hyrule Castle Chest",     ITEM_HEART_CONTAINER,    RF_EARTH_ELEMENT | RF_FIRE_ELEMENT | RF_WATER_ELEMENT | RF_WIND_ELEMENT | RF_SWORD | RF_ROCS_CAPE },
    { RANDO_LOC_FINAL_DOJO_GREAT_SPIN,      RANDO_LOC_CATEGORY_DOJO,            "Final Dojo Great Spin",        ITEM_SKILL_GREAT_SPIN,   RF_SWORD | RF_ROCS_CAPE },
    { RANDO_LOC_FINAL_KINSTONE_REWARD,      RANDO_LOC_CATEGORY_KINSTONE_FUSION, "Final Kinstone Reward",        ITEM_RUPEE200,           RF_KINSTONE_BAG | RF_OCARINA | RF_WIND_ELEMENT },
};
static_assert(RANDO_ARRAY_COUNT(kLocations) == RANDO_LOCATION_BUILTIN_COUNT, "location table mismatch");

/* Structural pools. Progression changes reachability; major changes power or
 * survivability; junk only pads the pool. */
static const uint16_t kProgressionItems[] = {
    ITEM_SMITH_SWORD, ITEM_GUST_JAR, ITEM_PACCI_CANE, ITEM_MOLE_MITTS,
    ITEM_ROCS_CAPE, ITEM_PEGASUS_BOOTS, ITEM_OCARINA, ITEM_LANTERN_OFF,
    ITEM_BOW, ITEM_GRIP_RING, ITEM_FLIPPERS, ITEM_KINSTONE_BAG,
    ITEM_EARTH_ELEMENT, ITEM_FIRE_ELEMENT, ITEM_WATER_ELEMENT, ITEM_WIND_ELEMENT,
};

static const uint16_t kMajorItems[] = {
    ITEM_HEART_CONTAINER, ITEM_HEART_CONTAINER, ITEM_HEART_PIECE, ITEM_HEART_PIECE,
    ITEM_BOTTLE1, ITEM_WALLET, ITEM_BOMBBAG, ITEM_LARGE_QUIVER,
    ITEM_POWER_BRACELETS, ITEM_BOOMERANG, ITEM_SHIELD,
    ITEM_SKILL_SPIN_ATTACK, ITEM_SKILL_DASH_ATTACK, ITEM_SKILL_GREAT_SPIN,
};

static const uint16_t kJunkNormal[] = {
    ITEM_RUPEE20, ITEM_RUPEE50, ITEM_RUPEE100, ITEM_SHELLS30,
    ITEM_BOMBS10, ITEM_ARROWS10, ITEM_FAIRY, ITEM_HEART,
};

static const uint16_t kJunkHard[] = {
    ITEM_RUPEE1, ITEM_RUPEE5, ITEM_RUPEE20, ITEM_SHELLS,
    ITEM_BOMBS5, ITEM_ARROWS5, ITEM_HEART, ITEM_KINSTONE,
};

static const uint16_t kJunkChaos[] = {
    ITEM_RUPEE1, ITEM_RUPEE200, ITEM_BOMBS5, ITEM_BOMBS10,
    ITEM_ARROWS5, ITEM_ARROWS10, ITEM_FAIRY, ITEM_KINSTONE,
};

typedef struct SplitMix64 {
    uint64_t state;
} SplitMix64;

extern "C" {
uint16_t randomized_item_table[RANDO_LOCATION_COUNT];
}

static uint16_t sCompatibilityRemap[256];
static RandomizerSettings sSettings;
static uint64_t sSeed = 0;
static bool sActive = false;
static bool sActiveExternalLogic = false;
static size_t sActiveLocationCount = RANDO_LOCATION_BUILTIN_COUNT;
static bool sInitialized = false;
static char sSpoiler[4096];
static uint64_t sAutoSeedCounter = 0x9e3779b97f4a7c15ull;

static uint64_t SplitMix64_Next(SplitMix64* rng) {
    uint64_t z = (rng->state += 0x9e3779b97f4a7c15ull);
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
    return z ^ (z >> 31);
}

static uint32_t RngBounded(SplitMix64* rng, uint32_t bound) {
    if (bound <= 1) return 0;
    return (uint32_t)(SplitMix64_Next(rng) % bound);
}

static void ShuffleU16(uint16_t* items, size_t count, SplitMix64* rng) {
    if (count < 2) return;
    for (size_t i = count - 1; i > 0; --i) {
        size_t j = (size_t)RngBounded(rng, (uint32_t)(i + 1));
        uint16_t tmp = items[i];
        items[i] = items[j];
        items[j] = tmp;
    }
}


static uint64_t ChooseAutoSeed(void) {
    SplitMix64 rng;
    rng.state = (uint64_t)time(NULL) ^ sAutoSeedCounter;
    sAutoSeedCounter += 0x517cc1b727220a95ull;
    uint64_t seed = SplitMix64_Next(&rng);
    return seed ? seed : 1ull;
}

static uint64_t ItemFlags(uint16_t item) {
    switch (item) {
        case ITEM_SMITH_SWORD:   return RF_SWORD;
        case ITEM_GUST_JAR:      return RF_GUST_JAR;
        case ITEM_PACCI_CANE:    return RF_PACCI_CANE;
        case ITEM_MOLE_MITTS:    return RF_MOLE_MITTS;
        case ITEM_ROCS_CAPE:     return RF_ROCS_CAPE;
        case ITEM_PEGASUS_BOOTS: return RF_PEGASUS_BOOTS;
        case ITEM_OCARINA:       return RF_OCARINA;
        case ITEM_LANTERN_OFF:   return RF_LANTERN;
        case ITEM_BOW:           return RF_BOW;
        case ITEM_GRIP_RING:     return RF_GRIP_RING;
        case ITEM_FLIPPERS:      return RF_FLIPPERS;
        case ITEM_KINSTONE_BAG:  return RF_KINSTONE_BAG;
        case ITEM_EARTH_ELEMENT: return RF_EARTH_ELEMENT;
        case ITEM_FIRE_ELEMENT:  return RF_FIRE_ELEMENT;
        case ITEM_WATER_ELEMENT: return RF_WATER_ELEMENT;
        case ITEM_WIND_ELEMENT:  return RF_WIND_ELEMENT;
        default:                 return 0;
    }
}

/* Difficulty-scaled, pool-preserving item bijections built from the seed.
 * NORMAL only shuffles collectibles (always beatable); HARD adds non-gating
 * majors; CHAOS adds dungeon-gating progression (may need glitches/be
 * unbeatable without a logic file). */
static const uint16_t kRemapJunkPool[] = {
    ITEM_RUPEE1, ITEM_RUPEE5, ITEM_RUPEE20, ITEM_RUPEE50, ITEM_RUPEE100, ITEM_RUPEE200,
    ITEM_KINSTONE, ITEM_BOMBS5, ITEM_ARROWS5, ITEM_HEART, ITEM_FAIRY, ITEM_SHELLS,
    ITEM_SHELLS30, ITEM_HEART_PIECE, ITEM_BOMBS10, ITEM_ARROWS10,
};
static const uint16_t kRemapMajorPool[] = {
    ITEM_BOOMERANG, ITEM_SHIELD, ITEM_BOTTLE1, ITEM_WALLET, ITEM_BOMBBAG,
    ITEM_LARGE_QUIVER, ITEM_HEART_CONTAINER, ITEM_JABBERNUT, ITEM_KINSTONE_BAG,
    ITEM_SKILL_SPIN_ATTACK, ITEM_SKILL_ROLL_ATTACK, ITEM_SKILL_DASH_ATTACK,
    ITEM_SKILL_ROCK_BREAKER, ITEM_SKILL_SWORD_BEAM, ITEM_SKILL_GREAT_SPIN,
};
static const uint16_t kRemapProgPool[] = {
    ITEM_SMITH_SWORD, ITEM_BOW, ITEM_LANTERN_OFF, ITEM_GUST_JAR, ITEM_PACCI_CANE,
    ITEM_MOLE_MITTS, ITEM_ROCS_CAPE, ITEM_PEGASUS_BOOTS, ITEM_OCARINA,
    ITEM_EARTH_ELEMENT, ITEM_FIRE_ELEMENT, ITEM_WATER_ELEMENT, ITEM_WIND_ELEMENT,
    ITEM_GRIP_RING, ITEM_POWER_BRACELETS, ITEM_FLIPPERS,
    ITEM_QST_LONLON_KEY, ITEM_QST_GRAVEYARD_KEY,
};

static void RemapPool(const uint16_t* pool, size_t n, SplitMix64* rng) {
    uint16_t perm[64];
    if (n < 2 || n > RANDO_ARRAY_COUNT(perm)) return;
    for (size_t i = 0; i < n; ++i) perm[i] = pool[i];
    for (size_t i = n - 1; i > 0; --i) {
        size_t j = (size_t)RngBounded(rng, (uint32_t)(i + 1));
        uint16_t tmp = perm[i];
        perm[i] = perm[j];
        perm[j] = tmp;
    }
    for (size_t i = 0; i < n; ++i) {
        if (pool[i] < RANDO_ARRAY_COUNT(sCompatibilityRemap)) {
            sCompatibilityRemap[pool[i]] = perm[i];
        }
    }
}

static bool LocationEnabled(const RandomizerSettings* settings, const RandoLocationDef* loc) {
    if (!settings->shuffle_kinstones && loc->category == RANDO_LOC_CATEGORY_KINSTONE_FUSION) {
        return false;
    }
    if (!settings->shuffle_dojos && loc->category == RANDO_LOC_CATEGORY_DOJO) {
        return false;
    }
    return true;
}

static uint64_t EffectiveRequirements(const RandomizerSettings* settings, const RandoLocationDef* loc) {
    uint64_t req = loc->requirements;
    if (!settings->glitchless_logic) {
        /* Non-glitchless mode models sequence breaks by relaxing movement-heavy
         * checks while preserving hard story gates and dungeon item gates. */
        req &= ~(RF_PEGASUS_BOOTS | RF_ROCS_CAPE);
    }
    return req;
}

static const char* ItemName(uint16_t item) {
    switch (item) {
        case ITEM_NONE:              return "None";
        case ITEM_SMITH_SWORD:       return "Smith Sword";
        case ITEM_BOMBS:             return "Bombs";
        case ITEM_BOW:               return "Bow";
        case ITEM_BOOMERANG:         return "Boomerang";
        case ITEM_SHIELD:            return "Shield";
        case ITEM_LANTERN_OFF:       return "Lantern";
        case ITEM_GUST_JAR:          return "Gust Jar";
        case ITEM_PACCI_CANE:        return "Cane of Pacci";
        case ITEM_MOLE_MITTS:        return "Mole Mitts";
        case ITEM_ROCS_CAPE:         return "Roc's Cape";
        case ITEM_PEGASUS_BOOTS:     return "Pegasus Boots";
        case ITEM_OCARINA:           return "Ocarina";
        case ITEM_BOTTLE1:           return "Bottle";
        case ITEM_QST_LONLON_KEY:    return "Lon Lon Key";
        case ITEM_QST_GRAVEYARD_KEY: return "Graveyard Key";
        case ITEM_SHELLS:            return "Shells";
        case ITEM_EARTH_ELEMENT:     return "Earth Element";
        case ITEM_FIRE_ELEMENT:      return "Fire Element";
        case ITEM_WATER_ELEMENT:     return "Water Element";
        case ITEM_WIND_ELEMENT:      return "Wind Element";
        case ITEM_GRIP_RING:         return "Grip Ring";
        case ITEM_POWER_BRACELETS:   return "Power Bracelets";
        case ITEM_FLIPPERS:          return "Flippers";
        case ITEM_SKILL_SPIN_ATTACK: return "Spin Attack";
        case ITEM_SKILL_DASH_ATTACK: return "Dash Attack";
        case ITEM_SKILL_GREAT_SPIN:  return "Great Spin";
        case ITEM_SMALL_KEY:         return "Small Key";
        case ITEM_RUPEE1:            return "1 Rupee";
        case ITEM_RUPEE5:            return "5 Rupees";
        case ITEM_RUPEE20:           return "20 Rupees";
        case ITEM_RUPEE50:           return "50 Rupees";
        case ITEM_RUPEE100:          return "100 Rupees";
        case ITEM_RUPEE200:          return "200 Rupees";
        case ITEM_JABBERNUT:         return "Jabbernut";
        case ITEM_KINSTONE:          return "Kinstone";
        case ITEM_BOMBS5:            return "5 Bombs";
        case ITEM_ARROWS5:           return "5 Arrows";
        case ITEM_HEART:             return "Heart";
        case ITEM_FAIRY:             return "Fairy";
        case ITEM_SHELLS30:          return "30 Shells";
        case ITEM_HEART_CONTAINER:   return "Heart Container";
        case ITEM_HEART_PIECE:       return "Heart Piece";
        case ITEM_WALLET:            return "Wallet";
        case ITEM_BOMBBAG:           return "Bomb Bag";
        case ITEM_LARGE_QUIVER:      return "Large Quiver";
        case ITEM_KINSTONE_BAG:      return "Kinstone Bag";
        case ITEM_BOMBS10:           return "10 Bombs";
        case ITEM_ARROWS10:          return "10 Arrows";
        case ITEM_SKILL_FAST_SPIN:   return "Fast Spin";
        case ITEM_SKILL_FAST_SPLIT:  return "Fast Split";
        case ITEM_SKILL_LONG_SPIN:   return "Long Spin";
        default:                     return "Item";
    }
}

static void EnsureInitialized(void) {
    if (sInitialized) return;
    for (size_t i = 0; i < RANDO_LOCATION_COUNT; ++i) {
        randomized_item_table[i] = (i < (size_t)RANDO_LOCATION_BUILTIN_COUNT) ? kLocations[i].vanilla_item : (uint16_t)ITEM_NONE;
    }
    for (size_t i = 0; i < RANDO_ARRAY_COUNT(sCompatibilityRemap); ++i) {
        sCompatibilityRemap[i] = (uint16_t)i;
    }
    sSettings = Rando_DefaultSettings();
    sSpoiler[0] = '\0';
    sInitialized = true;
}

static bool AppendItem(uint16_t* items, size_t* count, size_t cap, uint16_t item) {
    if (*count >= cap) return false;
    items[*count] = item;
    *count += 1;
    return true;
}

static bool BuildFillerPool(const RandomizerSettings* settings, uint16_t* filler, size_t needed) {
    size_t count = 0;
    const uint16_t* junk = kJunkNormal;
    size_t junk_count = RANDO_ARRAY_COUNT(kJunkNormal);

    if (settings->item_difficulty == RANDO_ITEM_POOL_HARD) {
        junk = kJunkHard;
        junk_count = RANDO_ARRAY_COUNT(kJunkHard);
    } else if (settings->item_difficulty == RANDO_ITEM_POOL_CHAOS) {
        junk = kJunkChaos;
        junk_count = RANDO_ARRAY_COUNT(kJunkChaos);
    }

    for (size_t i = 0; i < RANDO_ARRAY_COUNT(kMajorItems) && count < needed; ++i) {
        if (!AppendItem(filler, &count, needed, kMajorItems[i])) return false;
    }
    for (size_t i = 0; count < needed; ++i) {
        if (!AppendItem(filler, &count, needed, junk[i % junk_count])) return false;
    }
    return count == needed;
}

static bool VerifyTable(const uint16_t* table, const RandomizerSettings* settings) {
    bool collected[RANDO_LOCATION_BUILTIN_COUNT];
    uint64_t flags = kStartFlags;
    bool progressed;

    memset(collected, 0, sizeof(collected));
    do {
        progressed = false;
        for (size_t i = 0; i < RANDO_LOCATION_BUILTIN_COUNT; ++i) {
            const RandoLocationDef* loc = &kLocations[i];
            uint64_t req;
            uint64_t gained;
            if (collected[i]) continue;
            req = EffectiveRequirements(settings, loc);
            if ((flags & req) != req) continue;
            collected[i] = true;
            gained = ItemFlags(table[i]);
            if ((gained & ~flags) != 0) {
                flags |= gained;
                progressed = true;
            }
        }
    } while (progressed);

    return (flags & kGoalFlags) == kGoalFlags;
}

static RandoStatus BuildSeedAttempt(uint64_t attempt_seed,
                                    const RandomizerSettings* settings,
                                    uint16_t* out_table) {
    SplitMix64 rng;
    uint16_t progression[RANDO_ARRAY_COUNT(kProgressionItems)];
    uint16_t filler[RANDO_LOCATION_BUILTIN_COUNT];
    uint16_t empty_locs[RANDO_LOCATION_BUILTIN_COUNT];
    bool filled[RANDO_LOCATION_BUILTIN_COUNT];
    uint64_t current_flags = kStartFlags;
    size_t active_count = 0;

    rng.state = attempt_seed;

    for (size_t i = 0; i < RANDO_LOCATION_BUILTIN_COUNT; ++i) {
        bool enabled = LocationEnabled(settings, &kLocations[i]);
        out_table[i] = kLocations[i].vanilla_item;
        filled[i] = !enabled;
        if (enabled) active_count++;
    }

    if (active_count < RANDO_ARRAY_COUNT(kProgressionItems)) {
        return RANDO_BAD_SETTINGS;
    }

    for (size_t i = 0; i < RANDO_ARRAY_COUNT(kProgressionItems); ++i) {
        progression[i] = kProgressionItems[i];
    }
    ShuffleU16(progression, RANDO_ARRAY_COUNT(progression), &rng);

    for (size_t p = 0; p < RANDO_ARRAY_COUNT(progression); ++p) {
        uint16_t candidates[RANDO_LOCATION_BUILTIN_COUNT];
        size_t candidate_count = 0;
        for (size_t i = 0; i < RANDO_LOCATION_BUILTIN_COUNT; ++i) {
            uint64_t req;
            if (filled[i]) continue;
            req = EffectiveRequirements(settings, &kLocations[i]);
            if ((current_flags & req) == req) {
                candidates[candidate_count++] = (uint8_t)i;
            }
        }
        if (candidate_count == 0) {
            return RANDO_UNBEATABLE;
        }
        uint8_t chosen = candidates[RngBounded(&rng, (uint32_t)candidate_count)];
        out_table[chosen] = progression[p];
        filled[chosen] = true;
        current_flags |= ItemFlags(progression[p]);
    }

    size_t empty_count = 0;
    for (size_t i = 0; i < RANDO_LOCATION_BUILTIN_COUNT; ++i) {
        if (!filled[i]) {
            empty_locs[empty_count++] = (uint16_t)i;
        }
    }

    if (!BuildFillerPool(settings, filler, empty_count)) {
        return RANDO_INTERNAL;
    }
    ShuffleU16(filler, empty_count, &rng);
    ShuffleU16(empty_locs, empty_count, &rng);

    for (size_t i = 0; i < empty_count; ++i) {
        out_table[empty_locs[i]] = filler[i];
    }

    return VerifyTable(out_table, settings) ? RANDO_OK : RANDO_UNBEATABLE;
}

static void BuildCompatibilityRemap(void) {
    SplitMix64 rng;
    for (size_t i = 0; i < RANDO_ARRAY_COUNT(sCompatibilityRemap); ++i) {
        sCompatibilityRemap[i] = (uint16_t)i;
    }
    if (sActiveExternalLogic) return;

    rng.state = sSeed ? sSeed : 1ull;
    RemapPool(kRemapJunkPool, RANDO_ARRAY_COUNT(kRemapJunkPool), &rng);
    if (sSettings.item_difficulty >= RANDO_ITEM_POOL_HARD) {
        RemapPool(kRemapMajorPool, RANDO_ARRAY_COUNT(kRemapMajorPool), &rng);
    }
    if (sSettings.item_difficulty >= RANDO_ITEM_POOL_CHAOS) {
        RemapPool(kRemapProgPool, RANDO_ARRAY_COUNT(kRemapProgPool), &rng);
    }
}

static void SpoilerAppend(size_t* pos, const char* fmt, ...) {
    if (*pos >= sizeof(sSpoiler)) return;
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(sSpoiler + *pos, sizeof(sSpoiler) - *pos, fmt, ap);
    va_end(ap);
    if (n <= 0) return;
    size_t wrote = (size_t)n;
    if (wrote >= sizeof(sSpoiler) - *pos) {
        *pos = sizeof(sSpoiler) - 1;
        sSpoiler[*pos] = '\0';
    } else {
        *pos += wrote;
    }
}

static const char* DifficultyName(RandoItemPoolDifficulty difficulty) {
    switch (difficulty) {
        case RANDO_ITEM_POOL_NORMAL: return "Normal";
        case RANDO_ITEM_POOL_HARD:   return "Hard";
        case RANDO_ITEM_POOL_CHAOS:  return "Chaos";
        default:                     return "?";
    }
}

static void BuildSpoiler(uint64_t seed, const RandomizerSettings* settings) {
    size_t pos = 0;
    sSpoiler[0] = '\0';
    SpoilerAppend(&pos, "TMC native graph randomizer\n");
    SpoilerAppend(&pos, "Seed: %llu\n", (unsigned long long)seed);
    SpoilerAppend(&pos, "Logic: %s  Kinstones: %s  Dojos: %s  Pool: %s\n\n",
                  settings->glitchless_logic ? "Glitchless" : "Relaxed",
                  settings->shuffle_kinstones ? "On" : "Vanilla",
                  settings->shuffle_dojos ? "On" : "Vanilla",
                  DifficultyName(settings->item_difficulty));
    if (sActiveExternalLogic) {
        for (size_t i = 0; i < sActiveLocationCount; ++i) {
            uint16_t item = randomized_item_table[i];
            if (item == ITEM_NONE) continue; /* unmapped -> vanilla, skip */
            SpoilerAppend(&pos, "%-40s : %s\n", RandoLogic_GetLocationName((uint32_t)i), ItemName(item));
        }
        return;
    }
    /* Non-logic mode applies a pool-preserving item bijection at every
     * give-item source; list the swaps that actually differ from vanilla. */
    SpoilerAppend(&pos, "Item swaps (pool-preserving):\n");
    for (size_t i = 0; i < RANDO_ARRAY_COUNT(sCompatibilityRemap); ++i) {
        if (sCompatibilityRemap[i] != (uint16_t)i) {
            SpoilerAppend(&pos, "%-18s -> %s\n", ItemName((uint16_t)i), ItemName(sCompatibilityRemap[i]));
        }
    }
}

static RandoStatus ActivateSeed(uint64_t seed, const RandomizerSettings* settings,
                                const uint16_t* table, size_t count) {
    if (count > RANDO_LOCATION_COUNT) return RANDO_BAD_SETTINGS;
    for (size_t i = 0; i < RANDO_LOCATION_COUNT; ++i) {
        randomized_item_table[i] = (i < count) ? table[i] : (uint16_t)ITEM_NONE;
    }
    sSettings = *settings;
    sSeed = seed;
    sActive = true;
    sActiveLocationCount = count;
    BuildCompatibilityRemap();
    BuildSpoiler(seed, settings);
    fprintf(stderr, "[RANDO] seed %llu generated (%s logic, %s pool, %zu locations)\n",
            (unsigned long long)seed,
            settings->glitchless_logic ? "glitchless" : "relaxed",
            DifficultyName(settings->item_difficulty),
            count);
    return RANDO_OK;
}

extern "C" RandomizerSettings Rando_DefaultSettings(void) {
    RandomizerSettings settings;
    settings.glitchless_logic = true;
    settings.shuffle_kinstones = true;
    settings.shuffle_dojos = true;
    settings.item_difficulty = RANDO_ITEM_POOL_NORMAL;
    return settings;
}

extern "C" uint64_t Rando_SeedFromString(const char* text) {
    uint64_t value = 0;
    bool all_digits = true;
    bool any = false;

    if (text == NULL || text[0] == '\0') return 0;

    for (const char* p = text; *p; ++p) {
        if (*p < '0' || *p > '9') {
            all_digits = false;
            break;
        }
        any = true;
    }
    if (all_digits && any) {
        for (const char* p = text; *p; ++p) {
            value = value * 10ull + (uint64_t)(*p - '0');
        }
        return value;
    }

    value = 1469598103934665603ull;
    for (const unsigned char* p = (const unsigned char*)text; *p; ++p) {
        value ^= (uint64_t)(*p);
        value *= 1099511628211ull;
    }
    return value ? value : 1ull;
}

extern "C" RandoStatus Rando_GenerateSeed(uint64_t seed,
                                           const RandomizerSettings* settings,
                                           uint64_t* out_seed) {
    RandomizerSettings local;
    uint16_t candidate[RANDO_LOCATION_COUNT];
    RandoStatus last = RANDO_INTERNAL;

    EnsureInitialized();

    local = settings ? *settings : Rando_DefaultSettings();
    if (local.item_difficulty < RANDO_ITEM_POOL_NORMAL ||
        local.item_difficulty >= RANDO_ITEM_POOL_COUNT) {
        return RANDO_BAD_SETTINGS;
    }

    if (seed == 0) seed = ChooseAutoSeed();

    if (RandoLogic_IsLoaded()) {
        for (uint32_t attempt = 0; attempt < 32; ++attempt) {
            uint64_t attempt_seed = seed + 0x9e3779b97f4a7c15ull * (uint64_t)attempt;
            for (size_t i = 0; i < RANDO_LOCATION_COUNT; ++i) {
                candidate[i] = (i < (size_t)RANDO_LOCATION_BUILTIN_COUNT) ? kLocations[i].vanilla_item : (uint16_t)ITEM_NONE;
            }
            last = RandoLogic_Generate(attempt_seed, &local, candidate, RANDO_LOCATION_COUNT, NULL);
            if (last == RANDO_OK) {
                if (out_seed) *out_seed = seed;
                sActiveExternalLogic = true;
                RandoStatus activated = ActivateSeed(seed, &local, candidate, RandoLogic_GetStats().location_count);
                return activated;
            }
        }
        return last;
    }

    for (uint32_t attempt = 0; attempt < 32; ++attempt) {
        uint64_t attempt_seed = seed + 0x9e3779b97f4a7c15ull * (uint64_t)attempt;
        last = BuildSeedAttempt(attempt_seed, &local, candidate);
        if (last == RANDO_OK) {
            if (out_seed) *out_seed = seed;
            sActiveExternalLogic = false;
            return ActivateSeed(seed, &local, candidate, RANDO_LOCATION_BUILTIN_COUNT);
        }
    }

    return last;
}

extern "C" bool GenerateSeed(uint64_t seed, RandomizerSettings settings) {
    return Rando_GenerateSeed(seed, &settings, NULL) == RANDO_OK;
}


extern "C" void Rando_Reset(void) {
    EnsureInitialized();
    sSeed = 0;
    sActive = false;
    for (size_t i = 0; i < RANDO_LOCATION_COUNT; ++i) {
        randomized_item_table[i] = (i < (size_t)RANDO_LOCATION_BUILTIN_COUNT) ? kLocations[i].vanilla_item : (uint16_t)ITEM_NONE;
    }
    sActiveLocationCount = (size_t)RANDO_LOCATION_BUILTIN_COUNT;
    for (size_t i = 0; i < RANDO_ARRAY_COUNT(sCompatibilityRemap); ++i) {
        sCompatibilityRemap[i] = (uint16_t)i;
    }
    sSpoiler[0] = '\0';
    fprintf(stderr, "[RANDO] reset to vanilla\n");
    sActiveExternalLogic = false;
}

extern "C" bool Rando_IsActive(void) {
    return sActive;
}

extern "C" uint32_t Rando_GetSeed(void) {
    return (uint32_t)sSeed;
}

extern "C" uint64_t Rando_GetSeed64(void) {
    return sSeed;
}

extern "C" RandomizerSettings Rando_GetSettings(void) {
    EnsureInitialized();
    return sSettings;
}

extern "C" const uint16_t* Rando_GetRandomizedItemTable(void) {
    EnsureInitialized();
    return randomized_item_table;
}

extern "C" size_t Rando_GetLocationCount(void) {
    return sActiveLocationCount;
}

extern "C" const RandoLocationDef* Rando_GetLocationDef(RandoLocationId id) {
    if ((unsigned)id >= RANDO_LOCATION_BUILTIN_COUNT) return NULL;
    return &kLocations[(unsigned)id];
}

extern "C" uint16_t Rando_ResolveLocationItem(RandoLocationId location, uint16_t vanilla_item) {
    EnsureInitialized();
    if (!sActive) return vanilla_item;
    if ((unsigned)location >= sActiveLocationCount) return vanilla_item;
    return randomized_item_table[(unsigned)location];
}

extern "C" bool Rando_OverrideLocationKey(uint32_t location_key, uint8_t* type, uint8_t* subtype) {
    EnsureInitialized();
    if (!sActive || type == NULL) return false;

    if (!sActiveExternalLogic) return false;
    int loc = RandoLogic_FindLocationByKey(location_key);
    if (loc < 0 || (size_t)loc >= sActiveLocationCount) return false;
    uint16_t item = randomized_item_table[(size_t)loc];
    /* 0 = item not mapped to a native engine id -> leave the vanilla reward. */
    if (item == 0 || item == *type || item > 0xffu) return false;
    *type = (uint8_t)item;
    return true;
}

extern "C" bool Rando_ActivateTable(uint64_t seed, RandomizerSettings settings, const uint16_t* table, size_t count) {
    EnsureInitialized();
    sActiveExternalLogic = (count > RANDO_LOCATION_BUILTIN_COUNT);
    return table != NULL && ActivateSeed(seed, &settings, table, count) == RANDO_OK;
}

extern "C" bool Rando_VerifyCurrentSeed(void) {
    EnsureInitialized();
    if (!sActive) return false;
    if (sActiveExternalLogic) return true;
    return VerifyTable(randomized_item_table, &sSettings);
}

extern "C" bool Rando_OverrideItem(uint8_t* type, uint8_t* subtype) {
    (void)subtype;
    EnsureInitialized();
    if (!sActive || type == NULL) return false;
    uint16_t old_item = *type;
    if (old_item >= RANDO_ARRAY_COUNT(sCompatibilityRemap)) return false;
    uint16_t new_item = sCompatibilityRemap[old_item];
    if (new_item == old_item || new_item > 0xFFu) return false;
    *type = (uint8_t)new_item;
    return true;
}

extern "C" size_t Rando_GetSpoiler(char* buf, size_t buflen) {
    EnsureInitialized();
    if (buf == NULL || buflen == 0) return 0;
    size_t n = strlen(sSpoiler);
    if (n > buflen - 1) n = buflen - 1;
    memcpy(buf, sSpoiler, n);
    buf[n] = '\0';
    return n;
}
