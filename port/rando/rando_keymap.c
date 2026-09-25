/*
 * Native pickup keys for scripted and special Picori rule locations.
 *
 * Direct chest and flagged ground-item keys live in built-in rules. These
 * remaining aliases bind special ground pickups and scripted rewards to
 * the keys emitted by their game hooks. Ground flags are USA baseline IDs;
 * the multi-region build remaps them before seed generation.
 */

#include "rando/rando_keymap.h"

#include "flags.h"
#include "game.h"
#include "rando/rando_logic.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct RandoKeymapEntry {
    const char* location; /* rule location name */
    uint8_t area;
    uint8_t room;
    uint8_t flag; /* USA-baseline ItemOnGroundEntity.flag */
} RandoKeymapEntry;

typedef struct RandoScriptedKeyEntry {
    const char* location; /* rule location name */
    uint32_t key;         /* Rando_BuildScriptedKey(...) */
} RandoScriptedKeyEntry;

static const RandoKeymapEntry kGroundKeys[] = {
    { "Droplets_Entrance_B2_WestIceblock", 0x60, 0x20, 0x4F },
    { "Smith_Floor_Item1", 0x22, 0x11, 0xE0 },
    { "Smith_Floor_Item2", 0x22, 0x11, 0xE1 },
};

static const RandoScriptedKeyEntry kScriptedKeys[] = {
    { "Town_Shop_80Item", RANDO_SCRIPTED_KEY(RANDO_SCRIPTED_KEY_STOCKWELL, RANDO_STOCKWELL_SLOT_80, 0, 0) },
    { "Town_Shop_300Item", RANDO_SCRIPTED_KEY(RANDO_SCRIPTED_KEY_STOCKWELL, RANDO_STOCKWELL_SLOT_300, 0, 0) },
    { "Town_Dojo_NPC1", RANDO_SCRIPTED_KEY(RANDO_SCRIPTED_KEY_DOJO, 0, 0, 0) },
    { "Town_Dojo_NPC2", RANDO_SCRIPTED_KEY(RANDO_SCRIPTED_KEY_DOJO, 1, 0, 0) },
    { "Town_Dojo_NPC3", RANDO_SCRIPTED_KEY(RANDO_SCRIPTED_KEY_DOJO, 2, 0, 0) },
    { "Town_Dojo_NPC4", RANDO_SCRIPTED_KEY(RANDO_SCRIPTED_KEY_DOJO, 3, 0, 0) },
    { "Crenel_Dojo_NPC", RANDO_SCRIPTED_KEY(RANDO_SCRIPTED_KEY_DOJO, 4, 0, 0) },
    { "Castle_Dojo_NPC", RANDO_SCRIPTED_KEY(RANDO_SCRIPTED_KEY_DOJO, 5, 0, 0) },
    { "Hylia_Dojo_NPC", RANDO_SCRIPTED_KEY(RANDO_SCRIPTED_KEY_DOJO, 6, 0, 0) },
    { "Swamp_Dojo_NPC", RANDO_SCRIPTED_KEY(RANDO_SCRIPTED_KEY_DOJO, 7, 0, 0) },
    { "Swamp_WaterfallFusion_DojoNPC", RANDO_SCRIPTED_KEY(RANDO_SCRIPTED_KEY_DOJO, 8, 0, 0) },
    { "FallsLower_WaterfallFusion_DojoNPC", RANDO_SCRIPTED_KEY(RANDO_SCRIPTED_KEY_DOJO, 9, 0, 0) },
    { "NorthField_WaterfallFusion_DojoNPC", RANDO_SCRIPTED_KEY(RANDO_SCRIPTED_KEY_DOJO, 10, 0, 0) },
    { "Town_Cuccos_Lv_10_NPC", RANDO_SCRIPTED_KEY(RANDO_SCRIPTED_KEY_CUCCO, 9, 0, 0) },
    { "Hylia_DogNPC", RANDO_SCRIPTED_KEY(RANDO_SCRIPTED_KEY_SPECIAL, RANDO_SPECIAL_KEY_DOG_BOTTLE, 0, 0) },
    { "MinishVillage_BarrelHouse_Item",
      RANDO_SCRIPTED_KEY(RANDO_SCRIPTED_KEY_SPECIAL, RANDO_SPECIAL_KEY_JABBER_NUT, 0, 0) },
    { "Town_Jullieta_Item", RANDO_SCRIPTED_KEY(RANDO_SCRIPTED_KEY_SPECIAL, RANDO_SPECIAL_KEY_RED_BOOK, 0, 0) },
    { "Town_DrLeft_AtticItem",
      RANDO_SCRIPTED_KEY(RANDO_SCRIPTED_KEY_SPECIAL, RANDO_SPECIAL_KEY_GREEN_BOOK, 0, 0) },
    { "Hylia_MayorCabin_Item", RANDO_SCRIPTED_KEY(RANDO_SCRIPTED_KEY_SPECIAL, RANDO_SPECIAL_KEY_BLUE_BOOK, 0, 0) },
    { "Crenel_Melari_NPC", RANDO_SCRIPTED_KEY(RANDO_SCRIPTED_KEY_SPECIAL, RANDO_SPECIAL_KEY_MELARI, 0, 0) },
    { "Town_ShoeShop_NPC", RANDO_SCRIPTED_KEY(RANDO_SCRIPTED_KEY_SPECIAL, RANDO_SPECIAL_KEY_SHOE_SHOP, 0, 0) },
    { "MinishWoods_BombMinish_NPC1",
      RANDO_SCRIPTED_KEY(RANDO_SCRIPTED_KEY_SPECIAL, RANDO_SPECIAL_KEY_BOMB_MINISH_BAG, 0, 0) },
    { "MinishWoods_BombMinish_NPC2",
      RANDO_SCRIPTED_KEY(RANDO_SCRIPTED_KEY_SPECIAL, RANDO_SPECIAL_KEY_BOMB_MINISH_REMOTES, 0, 0) },
    { "Minish_GreatFairy_NPC",
      RANDO_SCRIPTED_KEY(RANDO_SCRIPTED_KEY_SPECIAL, RANDO_SPECIAL_KEY_MINISH_GREAT_FAIRY, 0, 0) },
    { "Crenel_GreatFairy_NPC",
      RANDO_SCRIPTED_KEY(RANDO_SCRIPTED_KEY_SPECIAL, RANDO_SPECIAL_KEY_CRENEL_GREAT_FAIRY, 0, 0) },
    { "Valley_GreatFairy_NPC",
      RANDO_SCRIPTED_KEY(RANDO_SCRIPTED_KEY_SPECIAL, RANDO_SPECIAL_KEY_VALLEY_GREAT_FAIRY, 0, 0) },
    { "Valley_DampeNPC", RANDO_SCRIPTED_KEY(RANDO_SCRIPTED_KEY_SPECIAL, RANDO_SPECIAL_KEY_DAMPE, 0, 0) },
    { "MinishWoods_WitchHut_Item",
      RANDO_SCRIPTED_KEY(RANDO_SCRIPTED_KEY_SPECIAL, RANDO_SPECIAL_KEY_WITCH_HUT, 0, 0) },
    { "Falls_Biggoron", RANDO_SCRIPTED_KEY(RANDO_SCRIPTED_KEY_SPECIAL, RANDO_SPECIAL_KEY_BIGGORON, 0, 0) },
    { "Town_Library_YellowMinish_NPC",
      RANDO_SCRIPTED_KEY(RANDO_SCRIPTED_KEY_SPECIAL, RANDO_SPECIAL_KEY_LIBRARY_YELLOW_MINISH, 0, 0) },
    { "Deepwood_Prize", RANDO_SCRIPTED_KEY(RANDO_SCRIPTED_KEY_SPECIAL, RANDO_SPECIAL_KEY_DEEPWOOD_PRIZE, 0, 0) },
    { "CoF_Prize", RANDO_SCRIPTED_KEY(RANDO_SCRIPTED_KEY_SPECIAL, RANDO_SPECIAL_KEY_COF_PRIZE, 0, 0) },
    { "Droplets_Prize", RANDO_SCRIPTED_KEY(RANDO_SCRIPTED_KEY_SPECIAL, RANDO_SPECIAL_KEY_DROPLETS_PRIZE, 0, 0) },
    { "Palace_Prize", RANDO_SCRIPTED_KEY(RANDO_SCRIPTED_KEY_SPECIAL, RANDO_SPECIAL_KEY_PALACE_PRIZE, 0, 0) },
    { "Town_CafeLady_NPC", RANDO_SCRIPTED_KEY(RANDO_SCRIPTED_KEY_SPECIAL, RANDO_SPECIAL_KEY_CAFE_LADY, 0, 0) },
    { "Crypt_Prize", RANDO_SCRIPTED_KEY(RANDO_SCRIPTED_KEY_SPECIAL, RANDO_SPECIAL_KEY_CRYPT_PRIZE, 0, 0) },
    { "WindTribe_2F_Gregal_NPC1",
      RANDO_SCRIPTED_KEY(RANDO_SCRIPTED_KEY_SPECIAL, RANDO_SPECIAL_KEY_GREGAL_SHELLS, 0, 0) },
    { "WindTribe_2F_Gregal_NPC2",
      RANDO_SCRIPTED_KEY(RANDO_SCRIPTED_KEY_SPECIAL, RANDO_SPECIAL_KEY_GREGAL_LIGHT_ARROW, 0, 0) },
    { "Trilby_Scrub_NPC", RANDO_SCRIPTED_KEY(RANDO_SCRIPTED_KEY_SCRUB, RANDO_SCRUB_KEY_BOTTLE, 0, 0) },
    { "Crenel_Scrub_NPC", RANDO_SCRIPTED_KEY(RANDO_SCRIPTED_KEY_SCRUB, RANDO_SCRUB_KEY_GRIP, 0, 0) },
    { "Fortress_Prize", RANDO_SCRIPTED_KEY(RANDO_SCRIPTED_KEY_SPECIAL, RANDO_SPECIAL_KEY_FORTRESS_PRIZE, 0, 0) },
    { "Town_Bell_HP", RANDO_SCRIPTED_KEY(RANDO_SCRIPTED_KEY_SPECIAL, RANDO_SPECIAL_KEY_BELL_HP, 0, 0) },
    { "SouthField_Tingle_NPC",
      RANDO_SCRIPTED_KEY(RANDO_SCRIPTED_KEY_SPECIAL, RANDO_SPECIAL_KEY_TINGLE_TROPHY, 0, 0) },
};

static unsigned BindGroundItemKeys(bool log_misses) {
    unsigned bound = 0;
    for (unsigned i = 0; i < (unsigned)(sizeof(kGroundKeys) / sizeof(kGroundKeys[0])); ++i) {
        const RandoKeymapEntry* e = &kGroundKeys[i];
        uint8_t flag = e->flag;
#if defined(PC_PORT) && defined(MULTI_REGION)
        flag = (uint8_t)Port_RemapBaselineLocalFlag(GetFlagBankOffset(e->area), flag);
#endif
        uint32_t key = ((uint32_t)e->area << 16) | ((uint32_t)e->room << 8) | (uint32_t)flag;
        if (RandoLogic_BindRuntimeKey(e->location, key)) {
            ++bound;
        } else if (log_misses) {
            fprintf(stderr, "[RANDO] keymap miss (ground): %s\n", e->location);
        }
    }
    return bound;
}

static unsigned BindScriptedKeys(bool log_misses) {
    unsigned bound = 0;
    for (unsigned i = 0; i < (unsigned)(sizeof(kScriptedKeys) / sizeof(kScriptedKeys[0])); ++i) {
        if (RandoLogic_BindRuntimeKey(kScriptedKeys[i].location, kScriptedKeys[i].key)) {
            ++bound;
        } else if (log_misses) {
            fprintf(stderr, "[RANDO] keymap miss (scripted): %s\n", kScriptedKeys[i].location);
        }
    }
    return bound;
}

void Rando_Keymap_Apply(void) {
    if (!RandoLogic_IsLoaded()) {
        return;
    }
    bool log_misses = getenv("TMC_RANDO_DEBUG") != NULL;
    unsigned ground_bound = BindGroundItemKeys(log_misses);
    unsigned scripted_bound = BindScriptedKeys(log_misses);
    fprintf(stderr, "[RANDO] keymap: bound %u/%u ground-item + %u/%u scripted locations\n",
            ground_bound, (unsigned)(sizeof(kGroundKeys) / sizeof(kGroundKeys[0])), scripted_bound,
            (unsigned)(sizeof(kScriptedKeys) / sizeof(kScriptedKeys[0])));
}
