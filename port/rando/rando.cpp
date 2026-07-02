/*
 * port/rando/rando.cpp — fixed-array graph randomizer for Project Picori.
 *
 * Derived from the GPL-3.0 Minish Cap randomizer (MinishMaker,
 * minishmaker/randomizer): shares its .logic format and randomization
 * behaviour. Distributed under the GPL-3.0; see THIRD-PARTY-LICENSES.md.
 */

#include "rando.h"
#include "rando_entrance.h"
#include "rando_music.h"
#include "item_ids.h"
#include "rando_keymap.h"

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#define RANDO_ARRAY_COUNT(a) (sizeof(a) / sizeof((a)[0]))

enum RandoHelperId {
    RH_SOUTH_FIELD,
    RH_TOWN,
    RH_NORTH_FIELD,
    RH_EASTERN_HILLS,
    RH_MINISH_WOODS,
    RH_MINISH_VILLAGE,
    RH_TRILBY,
    RH_CRENEL_BASE,
    RH_CRENEL,
    RH_CASTOR_WILDS,
    RH_WIND_RUINS,
    RH_ROYAL_VALLEY,
    RH_LON_LON,
    RH_LAKE_HYLIA,
    RH_FALLS,
    RH_CLOUD_TOPS,
    RH_WIND_TRIBE,
    RH_DEEPWOOD,
    RH_DEEPWOOD_BOSS,
    RH_COF,
    RH_COF_BOSS,
    RH_FORTRESS,
    RH_FORTRESS_BOSS,
    RH_DROPLETS,
    RH_DROPLETS_BOSS,
    RH_ROYAL_CRYPT,
    RH_PALACE,
    RH_PALACE_BOSS,
    RH_FOUR_ELEMENTS,
    RH_DHC,
    RH_GOAL,
    RH_COUNT
};

static const RandoLocationDef kLocations[RANDO_LOCATION_COUNT] = {
    { 0x002211E0u,
      RANDO_LOC_CATEGORY_KEY_ITEM,
      "Link's House - Floor Item 1",
      ITEM_NONE,
      RH_SOUTH_FIELD,
      false,
      false,
      { ITEM_NONE, ITEM_NONE, ITEM_NONE } },
    { 0x002211E1u,
      RANDO_LOC_CATEGORY_KEY_ITEM,
      "Link's House - Floor Item 2",
      ITEM_NONE,
      RH_SOUTH_FIELD,
      false,
      false,
      { ITEM_NONE, ITEM_NONE, ITEM_NONE } },
    { 0x0048014Fu,
      RANDO_LOC_CATEGORY_CHEST,
      "Deepwood Shrine - 1F Blue Warp Heart Piece",
      ITEM_HEART_PIECE,
      RH_DEEPWOOD,
      false,
      false,
      { ITEM_GUST_JAR, ITEM_NONE, ITEM_NONE } },
    { 0x00480552u,
      RANDO_LOC_CATEGORY_CHEST,
      "Deepwood Shrine - 1F Madderpillar Heart Piece",
      ITEM_HEART_PIECE,
      RH_DEEPWOOD,
      false,
      false,
      { ITEM_GUST_JAR, ITEM_NONE, ITEM_NONE } },
    { 0x00480830u,
      RANDO_LOC_CATEGORY_CHEST,
      "Deepwood Shrine - 1F East Mulldozer Fight Item",
      ITEM_SMALL_KEY,
      RH_DEEPWOOD,
      true,
      false,
      { ITEM_NONE, ITEM_NONE, ITEM_NONE } },
    { 0x00490047u,
      RANDO_LOC_CATEGORY_CHEST,
      "Deepwood Shrine - Boss Item",
      ITEM_HEART_CONTAINER,
      RH_DEEPWOOD_BOSS,
      false,
      false,
      { ITEM_NONE, ITEM_NONE, ITEM_NONE } },
    { RANDO_SCRIPTED_KEY(RANDO_SCRIPTED_KEY_SPECIAL, RANDO_SPECIAL_KEY_DEEPWOOD_PRIZE, 0, 0),
      RANDO_LOC_CATEGORY_KEY_ITEM,
      "Deepwood Shrine - Prize",
      ITEM_EARTH_ELEMENT,
      RH_DEEPWOOD_BOSS,
      false,
      false,
      { ITEM_NONE, ITEM_NONE, ITEM_NONE } },
    { 0x0050053Eu,
      RANDO_LOC_CATEGORY_CHEST,
      "Cave of Flames - 1F Item 1",
      ITEM_RUPEE20,
      RH_COF,
      false,
      false,
      { ITEM_NONE, ITEM_NONE, ITEM_NONE } },
    { 0x0050053Fu,
      RANDO_LOC_CATEGORY_CHEST,
      "Cave of Flames - 1F Item 2",
      ITEM_RUPEE20,
      RH_COF,
      false,
      false,
      { ITEM_NONE, ITEM_NONE, ITEM_NONE } },
    { 0x00500540u,
      RANDO_LOC_CATEGORY_CHEST,
      "Cave of Flames - 1F Item 3",
      ITEM_RUPEE20,
      RH_COF,
      false,
      false,
      { ITEM_NONE, ITEM_NONE, ITEM_NONE } },
    { 0x00500541u,
      RANDO_LOC_CATEGORY_CHEST,
      "Cave of Flames - 1F Item 4",
      ITEM_RUPEE20,
      RH_COF,
      false,
      false,
      { ITEM_NONE, ITEM_NONE, ITEM_NONE } },
    { 0x00500542u,
      RANDO_LOC_CATEGORY_CHEST,
      "Cave of Flames - 1F Item 5",
      ITEM_RUPEE20,
      RH_COF,
      false,
      false,
      { ITEM_NONE, ITEM_NONE, ITEM_NONE } },
    { 0x0050063Cu,
      RANDO_LOC_CATEGORY_CHEST,
      "Cave of Flames - B1 Heart Piece",
      ITEM_HEART_PIECE,
      RH_COF,
      false,
      false,
      { ITEM_PACCI_CANE, ITEM_NONE, ITEM_NONE } },
    { 0x0051003Au,
      RANDO_LOC_CATEGORY_CHEST,
      "Cave of Flames - Boss Item",
      ITEM_HEART_CONTAINER,
      RH_COF_BOSS,
      false,
      false,
      { ITEM_NONE, ITEM_NONE, ITEM_NONE } },
    { RANDO_SCRIPTED_KEY(RANDO_SCRIPTED_KEY_SPECIAL, RANDO_SPECIAL_KEY_COF_PRIZE, 0, 0),
      RANDO_LOC_CATEGORY_KEY_ITEM,
      "Cave of Flames - Prize",
      ITEM_FIRE_ELEMENT,
      RH_COF_BOSS,
      false,
      false,
      { ITEM_NONE, ITEM_NONE, ITEM_NONE } },
    { 0x0018004Eu,
      RANDO_LOC_CATEGORY_CHEST,
      "Fortress of Winds - Entrance 1F Right Item",
      ITEM_RUPEE50,
      RH_FORTRESS,
      false,
      false,
      { ITEM_NONE, ITEM_NONE, ITEM_NONE } },
    { 0x00582447u,
      RANDO_LOC_CATEGORY_CHEST,
      "Fortress of Winds - Entrance 1F Right Heart Piece",
      ITEM_HEART_PIECE,
      RH_FORTRESS,
      false,
      false,
      { ITEM_NONE, ITEM_NONE, ITEM_NONE } },
    { 0x00180155u,
      RANDO_LOC_CATEGORY_CHEST,
      "Fortress of Winds - Left 2F Item 1",
      ITEM_RUPEE20,
      RH_FORTRESS,
      false,
      false,
      { ITEM_MOLE_MITTS, ITEM_NONE, ITEM_NONE } },
    { 0x00180156u,
      RANDO_LOC_CATEGORY_CHEST,
      "Fortress of Winds - Left 2F Item 2",
      ITEM_RUPEE20,
      RH_FORTRESS,
      false,
      false,
      { ITEM_MOLE_MITTS, ITEM_NONE, ITEM_NONE } },
    { 0x00180157u,
      RANDO_LOC_CATEGORY_CHEST,
      "Fortress of Winds - Left 2F Item 3",
      ITEM_RUPEE20,
      RH_FORTRESS,
      false,
      false,
      { ITEM_MOLE_MITTS, ITEM_NONE, ITEM_NONE } },
    { 0x00180158u,
      RANDO_LOC_CATEGORY_CHEST,
      "Fortress of Winds - Left 2F Item 4",
      ITEM_RUPEE20,
      RH_FORTRESS,
      false,
      false,
      { ITEM_MOLE_MITTS, ITEM_NONE, ITEM_NONE } },
    { 0x00180159u,
      RANDO_LOC_CATEGORY_CHEST,
      "Fortress of Winds - Left 2F Item 5",
      ITEM_RUPEE20,
      RH_FORTRESS,
      false,
      false,
      { ITEM_MOLE_MITTS, ITEM_NONE, ITEM_NONE } },
    { 0x0018015Au,
      RANDO_LOC_CATEGORY_CHEST,
      "Fortress of Winds - Left 2F Item 6",
      ITEM_RUPEE20,
      RH_FORTRESS,
      false,
      false,
      { ITEM_MOLE_MITTS, ITEM_NONE, ITEM_NONE } },
    { 0x0018015Bu,
      RANDO_LOC_CATEGORY_CHEST,
      "Fortress of Winds - Left 2F Item 7",
      ITEM_RUPEE20,
      RH_FORTRESS,
      false,
      false,
      { ITEM_MOLE_MITTS, ITEM_NONE, ITEM_NONE } },
    { 0x00180153u,
      RANDO_LOC_CATEGORY_CHEST,
      "Fortress of Winds - Back Right Dig Room Top Pot",
      ITEM_RUPEE50,
      RH_FORTRESS,
      false,
      false,
      { ITEM_MOLE_MITTS, ITEM_NONE, ITEM_NONE } },
    { 0x00180154u,
      RANDO_LOC_CATEGORY_CHEST,
      "Fortress of Winds - Back Right Dig Room Bottom Pot",
      ITEM_SHELLS30,
      RH_FORTRESS,
      false,
      false,
      { ITEM_MOLE_MITTS, ITEM_NONE, ITEM_NONE } },
    { 0x0058203Fu,
      RANDO_LOC_CATEGORY_CHEST,
      "Fortress of Winds - Left 3F Item Drop",
      ITEM_SMALL_KEY,
      RH_FORTRESS,
      true,
      false,
      { ITEM_NONE, ITEM_NONE, ITEM_NONE } },
    { 0x00582241u,
      RANDO_LOC_CATEGORY_CHEST,
      "Fortress of Winds - Right 3F Item Drop",
      ITEM_SMALL_KEY,
      RH_FORTRESS,
      true,
      false,
      { ITEM_NONE, ITEM_NONE, ITEM_NONE } },
    { 0x0058142Eu,
      RANDO_LOC_CATEGORY_CHEST,
      "Fortress of Winds - Back Right Statue Item Drop",
      ITEM_SMALL_KEY,
      RH_FORTRESS,
      true,
      false,
      { ITEM_NONE, ITEM_NONE, ITEM_NONE } },
    { 0x00180464u,
      RANDO_LOC_CATEGORY_CHEST,
      "Fortress of Winds - Back Right Minish Item Drop",
      ITEM_SMALL_KEY,
      RH_FORTRESS,
      true,
      false,
      { ITEM_NONE, ITEM_NONE, ITEM_NONE } },
    { 0x00581632u,
      RANDO_LOC_CATEGORY_CHEST,
      "Fortress of Winds - Boss Item",
      ITEM_HEART_CONTAINER,
      RH_FORTRESS_BOSS,
      false,
      false,
      { ITEM_NONE, ITEM_NONE, ITEM_NONE } },
    { RANDO_SCRIPTED_KEY(RANDO_SCRIPTED_KEY_SPECIAL, RANDO_SPECIAL_KEY_FORTRESS_PRIZE, 0, 0),
      RANDO_LOC_CATEGORY_KEY_ITEM,
      "Fortress of Winds - Prize",
      ITEM_OCARINA,
      RH_FORTRESS_BOSS,
      false,
      false,
      { ITEM_NONE, ITEM_NONE, ITEM_NONE } },
    { 0x00600695u,
      RANDO_LOC_CATEGORY_CHEST,
      "Temple of Droplets - Left Path B1 Waterfall Underwater 1",
      ITEM_RUPEE20,
      RH_DROPLETS,
      false,
      false,
      { ITEM_FLIPPERS, ITEM_NONE, ITEM_NONE } },
    { 0x00600696u,
      RANDO_LOC_CATEGORY_CHEST,
      "Temple of Droplets - Left Path B1 Waterfall Underwater 2",
      ITEM_RUPEE20,
      RH_DROPLETS,
      false,
      false,
      { ITEM_FLIPPERS, ITEM_NONE, ITEM_NONE } },
    { 0x00600697u,
      RANDO_LOC_CATEGORY_CHEST,
      "Temple of Droplets - Left Path B1 Waterfall Underwater 3",
      ITEM_RUPEE20,
      RH_DROPLETS,
      false,
      false,
      { ITEM_FLIPPERS, ITEM_NONE, ITEM_NONE } },
    { 0x00600698u,
      RANDO_LOC_CATEGORY_CHEST,
      "Temple of Droplets - Left Path B1 Waterfall Underwater 4",
      ITEM_RUPEE20,
      RH_DROPLETS,
      false,
      false,
      { ITEM_FLIPPERS, ITEM_NONE, ITEM_NONE } },
    { 0x00600699u,
      RANDO_LOC_CATEGORY_CHEST,
      "Temple of Droplets - Left Path B1 Waterfall Underwater 5",
      ITEM_RUPEE20,
      RH_DROPLETS,
      false,
      false,
      { ITEM_FLIPPERS, ITEM_NONE, ITEM_NONE } },
    { 0x0060069Au,
      RANDO_LOC_CATEGORY_CHEST,
      "Temple of Droplets - Left Path B1 Waterfall Underwater 6",
      ITEM_RUPEE20,
      RH_DROPLETS,
      false,
      false,
      { ITEM_FLIPPERS, ITEM_NONE, ITEM_NONE } },
    { 0x00600D85u,
      RANDO_LOC_CATEGORY_CHEST,
      "Temple of Droplets - Left Path B1 Underpass Item 1",
      ITEM_RUPEE20,
      RH_DROPLETS,
      false,
      false,
      { ITEM_NONE, ITEM_NONE, ITEM_NONE } },
    { 0x00600D86u,
      RANDO_LOC_CATEGORY_CHEST,
      "Temple of Droplets - Left Path B1 Underpass Item 2",
      ITEM_RUPEE20,
      RH_DROPLETS,
      false,
      false,
      { ITEM_NONE, ITEM_NONE, ITEM_NONE } },
    { 0x00600D87u,
      RANDO_LOC_CATEGORY_CHEST,
      "Temple of Droplets - Left Path B1 Underpass Item 3",
      ITEM_RUPEE20,
      RH_DROPLETS,
      false,
      false,
      { ITEM_NONE, ITEM_NONE, ITEM_NONE } },
    { 0x00600D88u,
      RANDO_LOC_CATEGORY_CHEST,
      "Temple of Droplets - Left Path B1 Underpass Item 4",
      ITEM_RUPEE20,
      RH_DROPLETS,
      false,
      false,
      { ITEM_NONE, ITEM_NONE, ITEM_NONE } },
    { 0x00600D89u,
      RANDO_LOC_CATEGORY_CHEST,
      "Temple of Droplets - Left Path B1 Underpass Item 5",
      ITEM_RUPEE20,
      RH_DROPLETS,
      false,
      false,
      { ITEM_NONE, ITEM_NONE, ITEM_NONE } },
    { 0x00600A39u,
      RANDO_LOC_CATEGORY_CHEST,
      "Temple of Droplets - Right Path B1 Pot",
      ITEM_KINSTONE,
      RH_DROPLETS,
      false,
      false,
      { ITEM_NONE, ITEM_NONE, ITEM_NONE } },
    { 0x0060258Au,
      RANDO_LOC_CATEGORY_CHEST,
      "Temple of Droplets - Right Path B2 Underpass Item 1",
      ITEM_RUPEE20,
      RH_DROPLETS,
      false,
      false,
      { ITEM_LANTERN_OFF, ITEM_NONE, ITEM_NONE } },
    { 0x0060258Bu,
      RANDO_LOC_CATEGORY_CHEST,
      "Temple of Droplets - Right Path B2 Underpass Item 2",
      ITEM_RUPEE20,
      RH_DROPLETS,
      false,
      false,
      { ITEM_LANTERN_OFF, ITEM_NONE, ITEM_NONE } },
    { 0x0060258Cu,
      RANDO_LOC_CATEGORY_CHEST,
      "Temple of Droplets - Right Path B2 Underpass Item 3",
      ITEM_RUPEE20,
      RH_DROPLETS,
      false,
      false,
      { ITEM_LANTERN_OFF, ITEM_NONE, ITEM_NONE } },
    { 0x0060258Du,
      RANDO_LOC_CATEGORY_CHEST,
      "Temple of Droplets - Right Path B2 Underpass Item 4",
      ITEM_RUPEE20,
      RH_DROPLETS,
      false,
      false,
      { ITEM_LANTERN_OFF, ITEM_NONE, ITEM_NONE } },
    { 0x0060258Eu,
      RANDO_LOC_CATEGORY_CHEST,
      "Temple of Droplets - Right Path B2 Underpass Item 5",
      ITEM_RUPEE20,
      RH_DROPLETS,
      false,
      false,
      { ITEM_LANTERN_OFF, ITEM_NONE, ITEM_NONE } },
    { 0x0060318Fu,
      RANDO_LOC_CATEGORY_CHEST,
      "Temple of Droplets - Left Path B2 Waterfall Underwater 1",
      ITEM_RUPEE20,
      RH_DROPLETS,
      false,
      false,
      { ITEM_FLIPPERS, ITEM_NONE, ITEM_NONE } },
    { 0x00603190u,
      RANDO_LOC_CATEGORY_CHEST,
      "Temple of Droplets - Left Path B2 Waterfall Underwater 2",
      ITEM_RUPEE20,
      RH_DROPLETS,
      false,
      false,
      { ITEM_FLIPPERS, ITEM_NONE, ITEM_NONE } },
    { 0x00603191u,
      RANDO_LOC_CATEGORY_CHEST,
      "Temple of Droplets - Left Path B2 Waterfall Underwater 3",
      ITEM_RUPEE20,
      RH_DROPLETS,
      false,
      false,
      { ITEM_FLIPPERS, ITEM_NONE, ITEM_NONE } },
    { 0x00603192u,
      RANDO_LOC_CATEGORY_CHEST,
      "Temple of Droplets - Left Path B2 Waterfall Underwater 4",
      ITEM_RUPEE20,
      RH_DROPLETS,
      false,
      false,
      { ITEM_FLIPPERS, ITEM_NONE, ITEM_NONE } },
    { 0x00603193u,
      RANDO_LOC_CATEGORY_CHEST,
      "Temple of Droplets - Left Path B2 Waterfall Underwater 5",
      ITEM_RUPEE20,
      RH_DROPLETS,
      false,
      false,
      { ITEM_FLIPPERS, ITEM_NONE, ITEM_NONE } },
    { 0x00603194u,
      RANDO_LOC_CATEGORY_CHEST,
      "Temple of Droplets - Left Path B2 Waterfall Underwater 6",
      ITEM_RUPEE20,
      RH_DROPLETS,
      false,
      false,
      { ITEM_FLIPPERS, ITEM_NONE, ITEM_NONE } },
    { 0x0060347Au,
      RANDO_LOC_CATEGORY_CHEST,
      "Temple of Droplets - Left Path B2 Underwater Pot",
      ITEM_SMALL_KEY,
      RH_DROPLETS,
      false,
      false,
      { ITEM_FLIPPERS, ITEM_NONE, ITEM_NONE } },
    { 0x00602F6Fu,
      RANDO_LOC_CATEGORY_CHEST,
      "Temple of Droplets - Right Path B2 Mulldozers Item Drop",
      ITEM_SMALL_KEY,
      RH_DROPLETS,
      true,
      false,
      { ITEM_LANTERN_OFF, ITEM_NONE, ITEM_NONE } },
    { 0x0060204Fu,
      RANDO_LOC_CATEGORY_CHEST,
      "Temple of Droplets - Entrance B2 West Iceblock",
      ITEM_BIG_KEY,
      RH_DROPLETS,
      false,
      false,
      { ITEM_LANTERN_OFF, ITEM_NONE, ITEM_NONE } },
    { 0x00602152u,
      RANDO_LOC_CATEGORY_CHEST,
      "Temple of Droplets - Entrance B2 East Iceblock",
      ITEM_SMALL_KEY,
      RH_DROPLETS,
      false,
      false,
      { ITEM_LANTERN_OFF, ITEM_NONE, ITEM_NONE } },
    { 0x00600E40u,
      RANDO_LOC_CATEGORY_CHEST,
      "Temple of Droplets - Boss Item",
      ITEM_HEART_CONTAINER,
      RH_DROPLETS_BOSS,
      false,
      false,
      { ITEM_NONE, ITEM_NONE, ITEM_NONE } },
    { RANDO_SCRIPTED_KEY(RANDO_SCRIPTED_KEY_SPECIAL, RANDO_SPECIAL_KEY_DROPLETS_PRIZE, 0, 0),
      RANDO_LOC_CATEGORY_KEY_ITEM,
      "Temple of Droplets - Prize",
      ITEM_WATER_ELEMENT,
      RH_DROPLETS_BOSS,
      false,
      false,
      { ITEM_NONE, ITEM_NONE, ITEM_NONE } },
    { 0x006804B6u,
      RANDO_LOC_CATEGORY_CHEST,
      "Royal Crypt - Left Item",
      ITEM_KINSTONE,
      RH_ROYAL_CRYPT,
      false,
      false,
      { ITEM_NONE, ITEM_NONE, ITEM_NONE } },
    { 0x006804B7u,
      RANDO_LOC_CATEGORY_CHEST,
      "Royal Crypt - Right Item",
      ITEM_KINSTONE,
      RH_ROYAL_CRYPT,
      false,
      false,
      { ITEM_NONE, ITEM_NONE, ITEM_NONE } },
    { 0x006808C4u,
      RANDO_LOC_CATEGORY_CHEST,
      "Royal Crypt - Gibdo Left Item",
      ITEM_BOMBS10,
      RH_ROYAL_CRYPT,
      false,
      false,
      { ITEM_NONE, ITEM_NONE, ITEM_NONE } },
    { 0x006808C5u,
      RANDO_LOC_CATEGORY_CHEST,
      "Royal Crypt - Gibdo Right Item",
      ITEM_SMALL_KEY,
      RH_ROYAL_CRYPT,
      false,
      false,
      { ITEM_NONE, ITEM_NONE, ITEM_NONE } },
    { RANDO_SCRIPTED_KEY(RANDO_SCRIPTED_KEY_SPECIAL, RANDO_SPECIAL_KEY_CRYPT_PRIZE, 0, 0),
      RANDO_LOC_CATEGORY_KEY_ITEM,
      "Royal Crypt - Prize",
      ITEM_NONE,
      RH_ROYAL_CRYPT,
      false,
      false,
      { ITEM_PACCI_CANE, ITEM_NONE, ITEM_NONE } },
    { 0x00700F80u,
      RANDO_LOC_CATEGORY_CHEST,
      "Palace of Winds - 2nd Half 4F Heart Piece",
      ITEM_HEART_PIECE,
      RH_PALACE,
      false,
      false,
      { ITEM_ROCS_CAPE, ITEM_NONE, ITEM_NONE } },
    { 0x0070215Au,
      RANDO_LOC_CATEGORY_CHEST,
      "Palace of Winds - 1st Half 2F Item 1",
      ITEM_RUPEE20,
      RH_PALACE,
      false,
      false,
      { ITEM_NONE, ITEM_NONE, ITEM_NONE } },
    { 0x0070215Bu,
      RANDO_LOC_CATEGORY_CHEST,
      "Palace of Winds - 1st Half 2F Item 2",
      ITEM_RUPEE20,
      RH_PALACE,
      false,
      false,
      { ITEM_NONE, ITEM_NONE, ITEM_NONE } },
    { 0x0070215Cu,
      RANDO_LOC_CATEGORY_CHEST,
      "Palace of Winds - 1st Half 2F Item 3",
      ITEM_RUPEE20,
      RH_PALACE,
      false,
      false,
      { ITEM_NONE, ITEM_NONE, ITEM_NONE } },
    { 0x0070215Du,
      RANDO_LOC_CATEGORY_CHEST,
      "Palace of Winds - 1st Half 2F Item 4",
      ITEM_RUPEE20,
      RH_PALACE,
      false,
      false,
      { ITEM_NONE, ITEM_NONE, ITEM_NONE } },
    { 0x0070215Eu,
      RANDO_LOC_CATEGORY_CHEST,
      "Palace of Winds - 1st Half 2F Item 5",
      ITEM_RUPEE20,
      RH_PALACE,
      false,
      false,
      { ITEM_NONE, ITEM_NONE, ITEM_NONE } },
    { 0x00702059u,
      RANDO_LOC_CATEGORY_CHEST,
      "Palace of Winds - 1st Half 3F Pot Puzzle Item Drop",
      ITEM_SMALL_KEY,
      RH_PALACE,
      false,
      false,
      { ITEM_POWER_BRACELETS, ITEM_NONE, ITEM_NONE } },
    { 0x00700841u,
      RANDO_LOC_CATEGORY_CHEST,
      "Palace of Winds - 1st Half 5F Ball And Chain Soldiers Item Drop",
      ITEM_SMALL_KEY,
      RH_PALACE,
      true,
      false,
      { ITEM_NONE, ITEM_NONE, ITEM_NONE } },
    { 0x0070007Du,
      RANDO_LOC_CATEGORY_CHEST,
      "Palace of Winds - Boss Item",
      ITEM_HEART_CONTAINER,
      RH_PALACE_BOSS,
      false,
      false,
      { ITEM_NONE, ITEM_NONE, ITEM_NONE } },
    { RANDO_SCRIPTED_KEY(RANDO_SCRIPTED_KEY_SPECIAL, RANDO_SPECIAL_KEY_PALACE_PRIZE, 0, 0),
      RANDO_LOC_CATEGORY_KEY_ITEM,
      "Palace of Winds - Prize",
      ITEM_WIND_ELEMENT,
      RH_PALACE_BOSS,
      false,
      false,
      { ITEM_NONE, ITEM_NONE, ITEM_NONE } },
    { RANDO_SCRIPTED_KEY(RANDO_SCRIPTED_KEY_SPECIAL, RANDO_SPECIAL_KEY_DHC_KING, 0, 0),
      RANDO_LOC_CATEGORY_KEY_ITEM,
      "Dark Hyrule Castle - B2 King",
      ITEM_NONE,
      RH_DHC,
      false,
      false,
      { ITEM_NONE, ITEM_NONE, ITEM_NONE } },
    { 0x00210AB8u,
      RANDO_LOC_CATEGORY_KEY_ITEM,
      "Hyrule Town - Inn Backdoor Heart Piece",
      ITEM_HEART_PIECE,
      RH_TOWN,
      false,
      false,
      { ITEM_ROCS_CAPE, ITEM_NONE, ITEM_NONE } },
    { 0x002305B4u,
      RANDO_LOC_CATEGORY_KEY_ITEM,
      "Hyrule Town - Music House Heart Piece",
      ITEM_HEART_PIECE,
      RH_TOWN,
      false,
      false,
      { ITEM_QST_CARLOV_MEDAL, ITEM_NONE, ITEM_NONE } },
    { 0x0011027Eu,
      RANDO_LOC_CATEGORY_KEY_ITEM,
      "Hyrule Town - School Path Heart Piece",
      ITEM_HEART_PIECE,
      RH_TOWN,
      false,
      false,
      { ITEM_ROCS_CAPE, ITEM_NONE, ITEM_NONE } },
    { 0x006200C3u,
      RANDO_LOC_CATEGORY_KEY_ITEM,
      "Hyrule Town - Fountain Heart Piece",
      ITEM_HEART_PIECE,
      RH_TOWN,
      false,
      false,
      { ITEM_FLIPPERS, ITEM_NONE, ITEM_NONE } },
    { 0x00621013u,
      RANDO_LOC_CATEGORY_KEY_ITEM,
      "Hyrule Town - Under Library Underwater",
      ITEM_RUPEE50,
      RH_TOWN,
      false,
      false,
      { ITEM_FLIPPERS, ITEM_NONE, ITEM_NONE } },
    { RANDO_SCRIPTED_KEY(RANDO_SCRIPTED_KEY_SPECIAL, RANDO_SPECIAL_KEY_BELL_HP, 0, 0),
      RANDO_LOC_CATEGORY_KEY_ITEM,
      "Hyrule Town - Bell Heart Piece",
      ITEM_HEART_PIECE,
      RH_TOWN,
      false,
      false,
      { ITEM_ROCS_CAPE, ITEM_NONE, ITEM_NONE } },
    { RANDO_SCRIPTED_KEY(RANDO_SCRIPTED_KEY_STOCKWELL, RANDO_STOCKWELL_SLOT_80, 0, 0),
      RANDO_LOC_CATEGORY_KEY_ITEM,
      "Hyrule Town - Shop 80 Item",
      ITEM_WALLET,
      RH_TOWN,
      false,
      false,
      { ITEM_NONE, ITEM_NONE, ITEM_NONE } },
    { RANDO_SCRIPTED_KEY(RANDO_SCRIPTED_KEY_STOCKWELL, RANDO_STOCKWELL_SLOT_300, 0, 0),
      RANDO_LOC_CATEGORY_KEY_ITEM,
      "Hyrule Town - Shop 300 Item",
      ITEM_BOMBBAG,
      RH_TOWN,
      false,
      false,
      { ITEM_WALLET, ITEM_NONE, ITEM_NONE } },
    { RANDO_SCRIPTED_KEY(RANDO_SCRIPTED_KEY_STOCKWELL, RANDO_STOCKWELL_SLOT_600, 0, 0),
      RANDO_LOC_CATEGORY_KEY_ITEM,
      "Hyrule Town - Shop 600 Item",
      ITEM_LARGE_QUIVER,
      RH_TOWN,
      false,
      false,
      { ITEM_WALLET, ITEM_NONE, ITEM_NONE } },
    { RANDO_SCRIPTED_KEY(RANDO_SCRIPTED_KEY_STOCKWELL, RANDO_STOCKWELL_SLOT_EXTRA_600, 0, 0),
      RANDO_LOC_CATEGORY_KEY_ITEM,
      "Hyrule Town - Shop Extra 600 Item",
      ITEM_LARGE_QUIVER,
      RH_TOWN,
      false,
      false,
      { ITEM_WALLET, ITEM_NONE, ITEM_NONE } },
    { RANDO_SCRIPTED_KEY(RANDO_SCRIPTED_KEY_STOCKWELL, RANDO_STOCKWELL_SLOT_DOGFOOD, 0, 0),
      RANDO_LOC_CATEGORY_KEY_ITEM,
      "Hyrule Town - Shop Behind Counter Item",
      ITEM_BOTTLE1,
      RH_TOWN,
      false,
      false,
      { ITEM_NONE, ITEM_NONE, ITEM_NONE } },
    { RANDO_SCRIPTED_KEY(RANDO_SCRIPTED_KEY_SPECIAL, RANDO_SPECIAL_KEY_RED_BOOK, 0, 0),
      RANDO_LOC_CATEGORY_KEY_ITEM,
      "Hyrule Town - Jullieta Item",
      ITEM_QST_BOOK1,
      RH_TOWN,
      false,
      false,
      { ITEM_ROCS_CAPE, ITEM_NONE, ITEM_NONE } },
    { RANDO_SCRIPTED_KEY(RANDO_SCRIPTED_KEY_SPECIAL, RANDO_SPECIAL_KEY_GREEN_BOOK, 0, 0),
      RANDO_LOC_CATEGORY_KEY_ITEM,
      "Hyrule Town - Dr Left Attic Item",
      ITEM_QST_BOOK2,
      RH_TOWN,
      false,
      false,
      { ITEM_ROCS_CAPE, ITEM_NONE, ITEM_NONE } },
    { RANDO_SCRIPTED_KEY(RANDO_SCRIPTED_KEY_SPECIAL, RANDO_SPECIAL_KEY_LIBRARY_YELLOW_MINISH, 0, 0),
      RANDO_LOC_CATEGORY_KEY_ITEM,
      "Hyrule Town - Library Yellow Minish Reward",
      ITEM_FLIPPERS,
      RH_TOWN,
      false,
      false,
      { ITEM_QST_BOOK1, ITEM_QST_BOOK2, ITEM_QST_BOOK3 } },
    { RANDO_SCRIPTED_KEY(RANDO_SCRIPTED_KEY_SPECIAL, RANDO_SPECIAL_KEY_CAFE_LADY, 0, 0),
      RANDO_LOC_CATEGORY_KEY_ITEM,
      "Hyrule Town - Cafe Lady Reward",
      ITEM_NONE,
      RH_TOWN,
      false,
      false,
      { ITEM_NONE, ITEM_NONE, ITEM_NONE } },
    { RANDO_SCRIPTED_KEY(RANDO_SCRIPTED_KEY_SPECIAL, RANDO_SPECIAL_KEY_CARLOV_MEDAL, 0, 0),
      RANDO_LOC_CATEGORY_KEY_ITEM,
      "Hyrule Town - Carlov Reward",
      ITEM_QST_CARLOV_MEDAL,
      RH_TOWN,
      false,
      false,
      { ITEM_NONE, ITEM_NONE, ITEM_NONE } },
    { RANDO_SCRIPTED_KEY(RANDO_SCRIPTED_KEY_SPECIAL, RANDO_SPECIAL_KEY_SIMULATION, 0, 0),
      RANDO_LOC_CATEGORY_KEY_ITEM,
      "Hyrule Town - Simulation Chest",
      ITEM_NONE,
      RH_TOWN,
      false,
      false,
      { ITEM_NONE, ITEM_NONE, ITEM_NONE } },
    { RANDO_SCRIPTED_KEY(RANDO_SCRIPTED_KEY_GORON_MERCHANT, 0, 0, 0),
      RANDO_LOC_CATEGORY_KEY_ITEM,
      "Hyrule Town - Goron Merchant 1 Left",
      ITEM_KINSTONE,
      RH_TOWN,
      false,
      false,
      { ITEM_KINSTONE_BAG, ITEM_NONE, ITEM_NONE } },
    { RANDO_SCRIPTED_KEY(RANDO_SCRIPTED_KEY_GORON_MERCHANT, 0, 1, 0),
      RANDO_LOC_CATEGORY_KEY_ITEM,
      "Hyrule Town - Goron Merchant 1 Middle",
      ITEM_KINSTONE,
      RH_TOWN,
      false,
      false,
      { ITEM_KINSTONE_BAG, ITEM_NONE, ITEM_NONE } },
    { RANDO_SCRIPTED_KEY(RANDO_SCRIPTED_KEY_GORON_MERCHANT, 0, 2, 0),
      RANDO_LOC_CATEGORY_KEY_ITEM,
      "Hyrule Town - Goron Merchant 1 Right",
      ITEM_RUPEE50,
      RH_TOWN,
      false,
      false,
      { ITEM_KINSTONE_BAG, ITEM_NONE, ITEM_NONE } },
    { RANDO_SCRIPTED_KEY(RANDO_SCRIPTED_KEY_GORON_MERCHANT, 1, 0, 0),
      RANDO_LOC_CATEGORY_KEY_ITEM,
      "Hyrule Town - Goron Merchant 2 Left",
      ITEM_KINSTONE,
      RH_TOWN,
      false,
      false,
      { ITEM_KINSTONE_BAG, ITEM_NONE, ITEM_NONE } },
    { RANDO_SCRIPTED_KEY(RANDO_SCRIPTED_KEY_GORON_MERCHANT, 1, 1, 0),
      RANDO_LOC_CATEGORY_KEY_ITEM,
      "Hyrule Town - Goron Merchant 2 Middle",
      ITEM_KINSTONE,
      RH_TOWN,
      false,
      false,
      { ITEM_KINSTONE_BAG, ITEM_NONE, ITEM_NONE } },
    { RANDO_SCRIPTED_KEY(RANDO_SCRIPTED_KEY_GORON_MERCHANT, 1, 2, 0),
      RANDO_LOC_CATEGORY_KEY_ITEM,
      "Hyrule Town - Goron Merchant 2 Right",
      ITEM_RUPEE50,
      RH_TOWN,
      false,
      false,
      { ITEM_KINSTONE_BAG, ITEM_NONE, ITEM_NONE } },
    { RANDO_SCRIPTED_KEY(RANDO_SCRIPTED_KEY_GORON_MERCHANT, 2, 0, 0),
      RANDO_LOC_CATEGORY_KEY_ITEM,
      "Hyrule Town - Goron Merchant 3 Left",
      ITEM_KINSTONE,
      RH_TOWN,
      false,
      false,
      { ITEM_KINSTONE_BAG, ITEM_NONE, ITEM_NONE } },
    { RANDO_SCRIPTED_KEY(RANDO_SCRIPTED_KEY_GORON_MERCHANT, 2, 1, 0),
      RANDO_LOC_CATEGORY_KEY_ITEM,
      "Hyrule Town - Goron Merchant 3 Middle",
      ITEM_KINSTONE,
      RH_TOWN,
      false,
      false,
      { ITEM_KINSTONE_BAG, ITEM_NONE, ITEM_NONE } },
    { RANDO_SCRIPTED_KEY(RANDO_SCRIPTED_KEY_GORON_MERCHANT, 2, 2, 0),
      RANDO_LOC_CATEGORY_KEY_ITEM,
      "Hyrule Town - Goron Merchant 3 Right",
      ITEM_RUPEE50,
      RH_TOWN,
      false,
      false,
      { ITEM_KINSTONE_BAG, ITEM_NONE, ITEM_NONE } },
    { RANDO_SCRIPTED_KEY(RANDO_SCRIPTED_KEY_GORON_MERCHANT, 3, 0, 0),
      RANDO_LOC_CATEGORY_KEY_ITEM,
      "Hyrule Town - Goron Merchant 4 Left",
      ITEM_KINSTONE,
      RH_TOWN,
      false,
      false,
      { ITEM_KINSTONE_BAG, ITEM_NONE, ITEM_NONE } },
    { RANDO_SCRIPTED_KEY(RANDO_SCRIPTED_KEY_GORON_MERCHANT, 3, 1, 0),
      RANDO_LOC_CATEGORY_KEY_ITEM,
      "Hyrule Town - Goron Merchant 4 Middle",
      ITEM_KINSTONE,
      RH_TOWN,
      false,
      false,
      { ITEM_KINSTONE_BAG, ITEM_NONE, ITEM_NONE } },
    { RANDO_SCRIPTED_KEY(RANDO_SCRIPTED_KEY_GORON_MERCHANT, 3, 2, 0),
      RANDO_LOC_CATEGORY_KEY_ITEM,
      "Hyrule Town - Goron Merchant 4 Right",
      ITEM_RUPEE50,
      RH_TOWN,
      false,
      false,
      { ITEM_KINSTONE_BAG, ITEM_NONE, ITEM_NONE } },
    { RANDO_SCRIPTED_KEY(RANDO_SCRIPTED_KEY_GORON_MERCHANT, 4, 0, 0),
      RANDO_LOC_CATEGORY_KEY_ITEM,
      "Hyrule Town - Goron Merchant 5 Left",
      ITEM_KINSTONE,
      RH_TOWN,
      false,
      false,
      { ITEM_KINSTONE_BAG, ITEM_NONE, ITEM_NONE } },
    { RANDO_SCRIPTED_KEY(RANDO_SCRIPTED_KEY_GORON_MERCHANT, 4, 1, 0),
      RANDO_LOC_CATEGORY_KEY_ITEM,
      "Hyrule Town - Goron Merchant 5 Middle",
      ITEM_KINSTONE,
      RH_TOWN,
      false,
      false,
      { ITEM_KINSTONE_BAG, ITEM_NONE, ITEM_NONE } },
    { RANDO_SCRIPTED_KEY(RANDO_SCRIPTED_KEY_GORON_MERCHANT, 4, 2, 0),
      RANDO_LOC_CATEGORY_KEY_ITEM,
      "Hyrule Town - Goron Merchant 5 Right",
      ITEM_RUPEE50,
      RH_TOWN,
      false,
      false,
      { ITEM_KINSTONE_BAG, ITEM_NONE, ITEM_NONE } },
    { RANDO_SCRIPTED_KEY(RANDO_SCRIPTED_KEY_CUCCO, 0, 0, 0),
      RANDO_LOC_CATEGORY_KEY_ITEM,
      "Hyrule Town - Cuccos Level 1 Reward",
      ITEM_NONE,
      RH_TOWN,
      false,
      false,
      { ITEM_NONE, ITEM_NONE, ITEM_NONE } },
    { RANDO_SCRIPTED_KEY(RANDO_SCRIPTED_KEY_CUCCO, 1, 0, 0),
      RANDO_LOC_CATEGORY_KEY_ITEM,
      "Hyrule Town - Cuccos Level 2 Reward",
      ITEM_NONE,
      RH_TOWN,
      false,
      false,
      { ITEM_NONE, ITEM_NONE, ITEM_NONE } },
    { RANDO_SCRIPTED_KEY(RANDO_SCRIPTED_KEY_CUCCO, 2, 0, 0),
      RANDO_LOC_CATEGORY_KEY_ITEM,
      "Hyrule Town - Cuccos Level 3 Reward",
      ITEM_NONE,
      RH_TOWN,
      false,
      false,
      { ITEM_NONE, ITEM_NONE, ITEM_NONE } },
    { RANDO_SCRIPTED_KEY(RANDO_SCRIPTED_KEY_CUCCO, 3, 0, 0),
      RANDO_LOC_CATEGORY_KEY_ITEM,
      "Hyrule Town - Cuccos Level 4 Reward",
      ITEM_NONE,
      RH_TOWN,
      false,
      false,
      { ITEM_NONE, ITEM_NONE, ITEM_NONE } },
    { RANDO_SCRIPTED_KEY(RANDO_SCRIPTED_KEY_CUCCO, 4, 0, 0),
      RANDO_LOC_CATEGORY_KEY_ITEM,
      "Hyrule Town - Cuccos Level 5 Reward",
      ITEM_NONE,
      RH_TOWN,
      false,
      false,
      { ITEM_NONE, ITEM_NONE, ITEM_NONE } },
    { RANDO_SCRIPTED_KEY(RANDO_SCRIPTED_KEY_CUCCO, 5, 0, 0),
      RANDO_LOC_CATEGORY_KEY_ITEM,
      "Hyrule Town - Cuccos Level 6 Reward",
      ITEM_NONE,
      RH_TOWN,
      false,
      false,
      { ITEM_NONE, ITEM_NONE, ITEM_NONE } },
    { RANDO_SCRIPTED_KEY(RANDO_SCRIPTED_KEY_CUCCO, 6, 0, 0),
      RANDO_LOC_CATEGORY_KEY_ITEM,
      "Hyrule Town - Cuccos Level 7 Reward",
      ITEM_NONE,
      RH_TOWN,
      false,
      false,
      { ITEM_NONE, ITEM_NONE, ITEM_NONE } },
    { RANDO_SCRIPTED_KEY(RANDO_SCRIPTED_KEY_CUCCO, 7, 0, 0),
      RANDO_LOC_CATEGORY_KEY_ITEM,
      "Hyrule Town - Cuccos Level 8 Reward",
      ITEM_NONE,
      RH_TOWN,
      false,
      false,
      { ITEM_NONE, ITEM_NONE, ITEM_NONE } },
    { RANDO_SCRIPTED_KEY(RANDO_SCRIPTED_KEY_CUCCO, 8, 0, 0),
      RANDO_LOC_CATEGORY_KEY_ITEM,
      "Hyrule Town - Cuccos Level 9 Reward",
      ITEM_NONE,
      RH_TOWN,
      false,
      false,
      { ITEM_NONE, ITEM_NONE, ITEM_NONE } },
    { RANDO_SCRIPTED_KEY(RANDO_SCRIPTED_KEY_CUCCO, 9, 0, 0),
      RANDO_LOC_CATEGORY_KEY_ITEM,
      "Hyrule Town - Cuccos Level 10 Reward",
      ITEM_HEART_PIECE,
      RH_TOWN,
      false,
      false,
      { ITEM_ROCS_CAPE, ITEM_NONE, ITEM_NONE } },
    { RANDO_SCRIPTED_KEY(RANDO_SCRIPTED_KEY_SPECIAL, RANDO_SPECIAL_KEY_SHOE_SHOP, 0, 0),
      RANDO_LOC_CATEGORY_KEY_ITEM,
      "Hyrule Town - Shoe Shop Reward",
      ITEM_PEGASUS_BOOTS,
      RH_TOWN,
      false,
      false,
      { ITEM_QST_MUSHROOM, ITEM_NONE, ITEM_NONE } },
    { RANDO_SCRIPTED_KEY(RANDO_SCRIPTED_KEY_DOJO, 0, 0, 0),
      RANDO_LOC_CATEGORY_DOJO,
      "Hyrule Town - Dojo Reward 1",
      ITEM_SKILL_SPIN_ATTACK,
      RH_TOWN,
      true,
      false,
      { ITEM_NONE, ITEM_NONE, ITEM_NONE } },
    { RANDO_SCRIPTED_KEY(RANDO_SCRIPTED_KEY_DOJO, 1, 0, 0),
      RANDO_LOC_CATEGORY_DOJO,
      "Hyrule Town - Dojo Reward 2",
      ITEM_SKILL_ROCK_BREAKER,
      RH_TOWN,
      true,
      false,
      { ITEM_NONE, ITEM_NONE, ITEM_NONE } },
    { RANDO_SCRIPTED_KEY(RANDO_SCRIPTED_KEY_DOJO, 2, 0, 0),
      RANDO_LOC_CATEGORY_DOJO,
      "Hyrule Town - Dojo Reward 3",
      ITEM_SKILL_DASH_ATTACK,
      RH_TOWN,
      true,
      false,
      { ITEM_NONE, ITEM_NONE, ITEM_NONE } },
    { RANDO_SCRIPTED_KEY(RANDO_SCRIPTED_KEY_DOJO, 3, 0, 0),
      RANDO_LOC_CATEGORY_DOJO,
      "Hyrule Town - Dojo Reward 4",
      ITEM_SKILL_ROLL_ATTACK,
      RH_TOWN,
      true,
      false,
      { ITEM_NONE, ITEM_NONE, ITEM_NONE } },
    { 0x00350481u,
      RANDO_LOC_CATEGORY_KEY_ITEM,
      "South Hyrule Field - Minish Size Water Hole Heart Piece",
      ITEM_HEART_PIECE,
      RH_SOUTH_FIELD,
      false,
      false,
      { ITEM_FLIPPERS, ITEM_NONE, ITEM_NONE } },
    { RANDO_SCRIPTED_KEY(RANDO_SCRIPTED_KEY_SPECIAL, RANDO_SPECIAL_KEY_TINGLE_TROPHY, 0, 0),
      RANDO_LOC_CATEGORY_KEY_ITEM,
      "South Hyrule Field - Tingle Reward",
      ITEM_QST_TINGLE_TROPHY,
      RH_SOUTH_FIELD,
      false,
      false,
      { ITEM_ROCS_CAPE, ITEM_NONE, ITEM_NONE } },
    { 0x00130046u,
      RANDO_LOC_CATEGORY_KEY_ITEM,
      "Eastern Hills - Farm Dig Cave Item",
      ITEM_RUPEE20,
      RH_EASTERN_HILLS,
      false,
      false,
      { ITEM_MOLE_MITTS, ITEM_NONE, ITEM_NONE } },
    { 0x0000003Cu,
      RANDO_LOC_CATEGORY_KEY_ITEM,
      "Minish Woods - Top Heart Piece",
      ITEM_HEART_PIECE,
      RH_MINISH_WOODS,
      false,
      false,
      { ITEM_NONE, ITEM_NONE, ITEM_NONE } },
    { 0x0000003Du,
      RANDO_LOC_CATEGORY_KEY_ITEM,
      "Minish Woods - Bottom Heart Piece",
      ITEM_HEART_PIECE,
      RH_MINISH_WOODS,
      false,
      false,
      { ITEM_NONE, ITEM_NONE, ITEM_NONE } },
    { 0x0035097Au,
      RANDO_LOC_CATEGORY_KEY_ITEM,
      "Minish Woods - Flipper Hole Heart Piece",
      ITEM_HEART_PIECE,
      RH_MINISH_WOODS,
      false,
      false,
      { ITEM_FLIPPERS, ITEM_NONE, ITEM_NONE } },
    { RANDO_SCRIPTED_KEY(RANDO_SCRIPTED_KEY_SPECIAL, RANDO_SPECIAL_KEY_WITCH_HUT, 0, 0),
      RANDO_LOC_CATEGORY_KEY_ITEM,
      "Minish Woods - Witch Hut Item",
      ITEM_QST_MUSHROOM,
      RH_MINISH_WOODS,
      false,
      false,
      { ITEM_NONE, ITEM_NONE, ITEM_NONE } },
    { RANDO_SCRIPTED_KEY(RANDO_SCRIPTED_KEY_SPECIAL, RANDO_SPECIAL_KEY_BOMB_MINISH_BAG, 0, 0),
      RANDO_LOC_CATEGORY_KEY_ITEM,
      "Minish Woods - Bomb Minish Reward 1",
      ITEM_BOMBBAG,
      RH_MINISH_WOODS,
      false,
      true,
      { ITEM_JABBERNUT, ITEM_NONE, ITEM_NONE } },
    { RANDO_SCRIPTED_KEY(RANDO_SCRIPTED_KEY_SPECIAL, RANDO_SPECIAL_KEY_BOMB_MINISH_REMOTES, 0, 0),
      RANDO_LOC_CATEGORY_KEY_ITEM,
      "Minish Woods - Bomb Minish Reward 2",
      ITEM_NONE,
      RH_MINISH_WOODS,
      false,
      true,
      { ITEM_JABBERNUT, ITEM_NONE, ITEM_NONE } },
    { 0x000101C2u,
      RANDO_LOC_CATEGORY_KEY_ITEM,
      "Minish Village - Heart Piece",
      ITEM_HEART_PIECE,
      RH_MINISH_VILLAGE,
      false,
      false,
      { ITEM_NONE, ITEM_NONE, ITEM_NONE } },
    { RANDO_SCRIPTED_KEY(RANDO_SCRIPTED_KEY_SPECIAL, RANDO_SPECIAL_KEY_JABBER_NUT, 0, 0),
      RANDO_LOC_CATEGORY_KEY_ITEM,
      "Minish Village - Barrel House Item",
      ITEM_JABBERNUT,
      RH_MINISH_VILLAGE,
      false,
      false,
      { ITEM_NONE, ITEM_NONE, ITEM_NONE } },
    { RANDO_SCRIPTED_KEY(RANDO_SCRIPTED_KEY_SPECIAL, RANDO_SPECIAL_KEY_MINISH_GREAT_FAIRY, 0, 0),
      RANDO_LOC_CATEGORY_KEY_ITEM,
      "Minish Woods - Great Fairy Reward",
      ITEM_NONE,
      RH_MINISH_WOODS,
      false,
      true,
      { ITEM_NONE, ITEM_NONE, ITEM_NONE } },
    { RANDO_SCRIPTED_KEY(RANDO_SCRIPTED_KEY_SCRUB, RANDO_SCRUB_KEY_BOTTLE, 0, 0),
      RANDO_LOC_CATEGORY_KEY_ITEM,
      "Trilby Highlands - Scrub Reward",
      ITEM_BOTTLE1,
      RH_TRILBY,
      false,
      false,
      { ITEM_SHIELD, ITEM_NONE, ITEM_NONE } },
    { 0x0006044Bu,
      RANDO_LOC_CATEGORY_KEY_ITEM,
      "Mount Crenel Base - Entrance Vine",
      ITEM_KINSTONE,
      RH_CRENEL_BASE,
      false,
      false,
      { ITEM_NONE, ITEM_NONE, ITEM_NONE } },
    { 0x00260943u,
      RANDO_LOC_CATEGORY_KEY_ITEM,
      "Mount Crenel Base - Fairy Cave Item 1",
      ITEM_RUPEE20,
      RH_CRENEL_BASE,
      false,
      true,
      { ITEM_NONE, ITEM_NONE, ITEM_NONE } },
    { 0x00260944u,
      RANDO_LOC_CATEGORY_KEY_ITEM,
      "Mount Crenel Base - Fairy Cave Item 2",
      ITEM_RUPEE20,
      RH_CRENEL_BASE,
      false,
      true,
      { ITEM_NONE, ITEM_NONE, ITEM_NONE } },
    { 0x00260945u,
      RANDO_LOC_CATEGORY_KEY_ITEM,
      "Mount Crenel Base - Fairy Cave Item 3",
      ITEM_RUPEE20,
      RH_CRENEL_BASE,
      false,
      true,
      { ITEM_NONE, ITEM_NONE, ITEM_NONE } },
    { 0x00260840u,
      RANDO_LOC_CATEGORY_KEY_ITEM,
      "Mount Crenel Base - Water Cave Heart Piece",
      ITEM_HEART_PIECE,
      RH_CRENEL_BASE,
      false,
      true,
      { ITEM_FLIPPERS, ITEM_NONE, ITEM_NONE } },
    { 0x00140045u,
      RANDO_LOC_CATEGORY_KEY_ITEM,
      "Mount Crenel - Dig Cave Heart Piece",
      ITEM_HEART_PIECE,
      RH_CRENEL_BASE,
      false,
      false,
      { ITEM_MOLE_MITTS, ITEM_NONE, ITEM_NONE } },
    { 0x0026057Du,
      RANDO_LOC_CATEGORY_KEY_ITEM,
      "Mount Crenel - Fairy Cave Heart Piece",
      ITEM_HEART_PIECE,
      RH_CRENEL,
      false,
      true,
      { ITEM_NONE, ITEM_NONE, ITEM_NONE } },
    { RANDO_SCRIPTED_KEY(RANDO_SCRIPTED_KEY_SCRUB, RANDO_SCRUB_KEY_GRIP, 0, 0),
      RANDO_LOC_CATEGORY_KEY_ITEM,
      "Mount Crenel - Scrub Reward",
      ITEM_GRIP_RING,
      RH_CRENEL,
      false,
      false,
      { ITEM_SHIELD, ITEM_NONE, ITEM_NONE } },
    { 0x00250080u,
      RANDO_LOC_CATEGORY_KEY_ITEM,
      "Mount Crenel - Dojo Heart Piece",
      ITEM_HEART_PIECE,
      RH_CRENEL,
      false,
      false,
      { ITEM_NONE, ITEM_NONE, ITEM_NONE } },
    { RANDO_SCRIPTED_KEY(RANDO_SCRIPTED_KEY_DOJO, 4, 0, 0),
      RANDO_LOC_CATEGORY_DOJO,
      "Mount Crenel - Dojo Reward",
      ITEM_SKILL_GREAT_SPIN,
      RH_CRENEL,
      true,
      false,
      { ITEM_NONE, ITEM_NONE, ITEM_NONE } },
    { RANDO_SCRIPTED_KEY(RANDO_SCRIPTED_KEY_SPECIAL, RANDO_SPECIAL_KEY_CRENEL_GREAT_FAIRY, 0, 0),
      RANDO_LOC_CATEGORY_KEY_ITEM,
      "Mount Crenel - Great Fairy Reward",
      ITEM_NONE,
      RH_CRENEL,
      false,
      true,
      { ITEM_NONE, ITEM_NONE, ITEM_NONE } },
    { 0x001000BAu,
      RANDO_LOC_CATEGORY_KEY_ITEM,
      "Mount Crenel - Melari Upper Top Middle Dig",
      ITEM_KINSTONE,
      RH_CRENEL,
      false,
      false,
      { ITEM_MOLE_MITTS, ITEM_NONE, ITEM_NONE } },
    { 0x001000BCu,
      RANDO_LOC_CATEGORY_KEY_ITEM,
      "Mount Crenel - Melari Upper Top Right Dig",
      ITEM_KINSTONE,
      RH_CRENEL,
      false,
      false,
      { ITEM_MOLE_MITTS, ITEM_NONE, ITEM_NONE } },
    { 0x001000BDu,
      RANDO_LOC_CATEGORY_KEY_ITEM,
      "Mount Crenel - Melari Upper Middle Right Dig",
      ITEM_KINSTONE,
      RH_CRENEL,
      false,
      false,
      { ITEM_MOLE_MITTS, ITEM_NONE, ITEM_NONE } },
    { 0x001000BFu,
      RANDO_LOC_CATEGORY_KEY_ITEM,
      "Mount Crenel - Melari Upper Middle Left Dig",
      ITEM_KINSTONE,
      RH_CRENEL,
      false,
      false,
      { ITEM_MOLE_MITTS, ITEM_NONE, ITEM_NONE } },
    { RANDO_SCRIPTED_KEY(RANDO_SCRIPTED_KEY_SPECIAL, RANDO_SPECIAL_KEY_MELARI, 0, 0),
      RANDO_LOC_CATEGORY_KEY_ITEM,
      "Mount Crenel - Melari Reward",
      ITEM_NONE,
      RH_CRENEL,
      false,
      false,
      { ITEM_NONE, ITEM_NONE, ITEM_NONE } },
    { 0x002A0438u,
      RANDO_LOC_CATEGORY_KEY_ITEM,
      "Castor Wilds - Near Waterfall Cave Heart Piece",
      ITEM_HEART_PIECE,
      RH_CASTOR_WILDS,
      false,
      true,
      { ITEM_NONE, ITEM_NONE, ITEM_NONE } },
    { 0x0025047Fu,
      RANDO_LOC_CATEGORY_KEY_ITEM,
      "Castor Wilds - Dojo Heart Piece",
      ITEM_HEART_PIECE,
      RH_CASTOR_WILDS,
      false,
      false,
      { ITEM_NONE, ITEM_NONE, ITEM_NONE } },
    { RANDO_SCRIPTED_KEY(RANDO_SCRIPTED_KEY_DOJO, 7, 0, 0),
      RANDO_LOC_CATEGORY_DOJO,
      "Castor Wilds - Dojo Reward",
      ITEM_SKILL_FAST_SPLIT,
      RH_CASTOR_WILDS,
      true,
      false,
      { ITEM_NONE, ITEM_NONE, ITEM_NONE } },
    { RANDO_SCRIPTED_KEY(RANDO_SCRIPTED_KEY_DOJO, 8, 0, 0),
      RANDO_LOC_CATEGORY_DOJO,
      "Castor Wilds - Waterfall Fusion Dojo Reward",
      ITEM_SKILL_FAST_SPIN,
      RH_CASTOR_WILDS,
      true,
      false,
      { ITEM_KINSTONE_BAG, ITEM_NONE, ITEM_NONE } },
    { 0x0035037Eu,
      RANDO_LOC_CATEGORY_KEY_ITEM,
      "Wind Ruins - Minish Cave Heart Piece",
      ITEM_HEART_PIECE,
      RH_WIND_RUINS,
      false,
      false,
      { ITEM_NONE, ITEM_NONE, ITEM_NONE } },
    { 0x0034005Du,
      RANDO_LOC_CATEGORY_KEY_ITEM,
      "Royal Valley - Graveyard Left Grave Heart Piece",
      ITEM_HEART_PIECE,
      RH_ROYAL_VALLEY,
      false,
      false,
      { ITEM_NONE, ITEM_NONE, ITEM_NONE } },
    { 0x00340100u,
      RANDO_LOC_CATEGORY_KEY_ITEM,
      "Royal Valley - Lost Woods Chest",
      ITEM_SHELLS30,
      RH_ROYAL_VALLEY,
      false,
      false,
      { ITEM_NONE, ITEM_NONE, ITEM_NONE } },
    { RANDO_SCRIPTED_KEY(RANDO_SCRIPTED_KEY_SPECIAL, RANDO_SPECIAL_KEY_VALLEY_GREAT_FAIRY, 0, 0),
      RANDO_LOC_CATEGORY_KEY_ITEM,
      "Royal Valley - Great Fairy Reward",
      ITEM_NONE,
      RH_ROYAL_VALLEY,
      false,
      true,
      { ITEM_NONE, ITEM_NONE, ITEM_NONE } },
    { RANDO_SCRIPTED_KEY(RANDO_SCRIPTED_KEY_SPECIAL, RANDO_SPECIAL_KEY_DAMPE, 0, 0),
      RANDO_LOC_CATEGORY_KEY_ITEM,
      "Royal Valley - Dampe Reward",
      ITEM_QST_GRAVEYARD_KEY,
      RH_NORTH_FIELD,
      false,
      false,
      { ITEM_NONE, ITEM_NONE, ITEM_NONE } },
    { 0x0032157Bu,
      RANDO_LOC_CATEGORY_KEY_ITEM,
      "North Hyrule Field - Heart Piece",
      ITEM_HEART_PIECE,
      RH_NORTH_FIELD,
      false,
      true,
      { ITEM_NONE, ITEM_NONE, ITEM_NONE } },
    { 0x0003068Fu,
      RANDO_LOC_CATEGORY_KEY_ITEM,
      "North Hyrule Field - Dig Spot",
      ITEM_RUPEE50,
      RH_NORTH_FIELD,
      false,
      false,
      { ITEM_MOLE_MITTS, ITEM_NONE, ITEM_NONE } },
    { RANDO_SCRIPTED_KEY(RANDO_SCRIPTED_KEY_DOJO, 10, 0, 0),
      RANDO_LOC_CATEGORY_DOJO,
      "North Hyrule Field - Waterfall Fusion Dojo Reward",
      ITEM_SKILL_LONG_SPIN,
      RH_NORTH_FIELD,
      true,
      false,
      { ITEM_KINSTONE_BAG, ITEM_NONE, ITEM_NONE } },
    { 0x00250583u,
      RANDO_LOC_CATEGORY_KEY_ITEM,
      "Hyrule Castle Garden - Dojo Heart Piece",
      ITEM_HEART_PIECE,
      RH_NORTH_FIELD,
      false,
      false,
      { ITEM_NONE, ITEM_NONE, ITEM_NONE } },
    { RANDO_SCRIPTED_KEY(RANDO_SCRIPTED_KEY_DOJO, 5, 0, 0),
      RANDO_LOC_CATEGORY_DOJO,
      "Hyrule Castle Garden - Dojo Reward",
      ITEM_SKILL_SWORD_BEAM,
      RH_NORTH_FIELD,
      true,
      false,
      { ITEM_NONE, ITEM_NONE, ITEM_NONE } },
    { 0x001103BAu,
      RANDO_LOC_CATEGORY_KEY_ITEM,
      "Lon Lon Ranch - Path Heart Piece",
      ITEM_HEART_PIECE,
      RH_LON_LON,
      false,
      false,
      { ITEM_NONE, ITEM_NONE, ITEM_NONE } },
    { 0x0003057Fu,
      RANDO_LOC_CATEGORY_KEY_ITEM,
      "Lon Lon Ranch - Dig Spot",
      ITEM_RUPEE50,
      RH_LON_LON,
      false,
      false,
      { ITEM_MOLE_MITTS, ITEM_NONE, ITEM_NONE } },
    { 0x0003057Eu,
      RANDO_LOC_CATEGORY_KEY_ITEM,
      "Lake Hylia - Cape Cave Lon Lon Heart Piece",
      ITEM_HEART_PIECE,
      RH_LON_LON,
      false,
      false,
      { ITEM_ROCS_CAPE, ITEM_NONE, ITEM_NONE } },
    { 0x000B0008u,
      RANDO_LOC_CATEGORY_KEY_ITEM,
      "Lake Hylia - Small Island Heart Piece",
      ITEM_HEART_PIECE,
      RH_LAKE_HYLIA,
      false,
      false,
      { ITEM_FLIPPERS, ITEM_NONE, ITEM_NONE } },
    { 0x000B000Au,
      RANDO_LOC_CATEGORY_KEY_ITEM,
      "Lake Hylia - Bottom Heart Piece",
      ITEM_HEART_PIECE,
      RH_LAKE_HYLIA,
      false,
      false,
      { ITEM_FLIPPERS, ITEM_NONE, ITEM_NONE } },
    { 0x00250682u,
      RANDO_LOC_CATEGORY_KEY_ITEM,
      "Lake Hylia - Dojo Heart Piece",
      ITEM_HEART_PIECE,
      RH_LAKE_HYLIA,
      false,
      false,
      { ITEM_NONE, ITEM_NONE, ITEM_NONE } },
    { RANDO_SCRIPTED_KEY(RANDO_SCRIPTED_KEY_DOJO, 6, 0, 0),
      RANDO_LOC_CATEGORY_DOJO,
      "Lake Hylia - Dojo Reward",
      ITEM_SKILL_PERIL_BEAM,
      RH_LAKE_HYLIA,
      true,
      false,
      { ITEM_NONE, ITEM_NONE, ITEM_NONE } },
    { RANDO_SCRIPTED_KEY(RANDO_SCRIPTED_KEY_SPECIAL, RANDO_SPECIAL_KEY_DOG_BOTTLE, 0, 0),
      RANDO_LOC_CATEGORY_KEY_ITEM,
      "Lake Hylia - Dog Reward",
      ITEM_BOTTLE1,
      RH_LAKE_HYLIA,
      false,
      false,
      { ITEM_NONE, ITEM_NONE, ITEM_NONE } },
    { RANDO_SCRIPTED_KEY(RANDO_SCRIPTED_KEY_SPECIAL, RANDO_SPECIAL_KEY_BLUE_BOOK, 0, 0),
      RANDO_LOC_CATEGORY_KEY_ITEM,
      "Lake Hylia - Mayor Cabin Item",
      ITEM_QST_BOOK3,
      RH_LAKE_HYLIA,
      false,
      false,
      { ITEM_POWER_BRACELETS, ITEM_NONE, ITEM_NONE } },
    { 0x000A00A1u,
      RANDO_LOC_CATEGORY_KEY_ITEM,
      "Veil Falls - Entrance Heart Piece",
      ITEM_HEART_PIECE,
      RH_FALLS,
      false,
      false,
      { ITEM_NONE, ITEM_NONE, ITEM_NONE } },
    { 0x000A00ABu,
      RANDO_LOC_CATEGORY_KEY_ITEM,
      "Veil Falls Lower - Heart Piece",
      ITEM_HEART_PIECE,
      RH_FALLS,
      false,
      true,
      { ITEM_NONE, ITEM_NONE, ITEM_NONE } },
    { 0x000A00A3u,
      RANDO_LOC_CATEGORY_KEY_ITEM,
      "Veil Falls Lower - Rock Item 1",
      ITEM_RUPEE20,
      RH_FALLS,
      false,
      true,
      { ITEM_NONE, ITEM_NONE, ITEM_NONE } },
    { 0x000A00A4u,
      RANDO_LOC_CATEGORY_KEY_ITEM,
      "Veil Falls Lower - Rock Item 2",
      ITEM_RUPEE20,
      RH_FALLS,
      false,
      true,
      { ITEM_NONE, ITEM_NONE, ITEM_NONE } },
    { 0x000A00A5u,
      RANDO_LOC_CATEGORY_KEY_ITEM,
      "Veil Falls Lower - Rock Item 3",
      ITEM_RUPEE20,
      RH_FALLS,
      false,
      true,
      { ITEM_NONE, ITEM_NONE, ITEM_NONE } },
    { RANDO_SCRIPTED_KEY(RANDO_SCRIPTED_KEY_DOJO, 9, 0, 0),
      RANDO_LOC_CATEGORY_DOJO,
      "Veil Falls Lower - Waterfall Fusion Dojo Reward",
      ITEM_SKILL_DOWN_THRUST,
      RH_FALLS,
      true,
      false,
      { ITEM_KINSTONE_BAG, ITEM_NONE, ITEM_NONE } },
    { 0x000A00A8u,
      RANDO_LOC_CATEGORY_KEY_ITEM,
      "Veil Falls - North Dig Spot",
      ITEM_KINSTONE,
      RH_FALLS,
      false,
      false,
      { ITEM_MOLE_MITTS, ITEM_NONE, ITEM_NONE } },
    { 0x000A00AAu,
      RANDO_LOC_CATEGORY_KEY_ITEM,
      "Veil Falls - South Dig Spot",
      ITEM_KINSTONE,
      RH_FALLS,
      false,
      false,
      { ITEM_MOLE_MITTS, ITEM_NONE, ITEM_NONE } },
    { 0x0033084Du,
      RANDO_LOC_CATEGORY_KEY_ITEM,
      "Veil Falls - Rupee Cave Top Top",
      ITEM_RUPEE20,
      RH_FALLS,
      false,
      true,
      { ITEM_NONE, ITEM_NONE, ITEM_NONE } },
    { 0x0033084Eu,
      RANDO_LOC_CATEGORY_KEY_ITEM,
      "Veil Falls - Rupee Cave Top Left",
      ITEM_RUPEE20,
      RH_FALLS,
      false,
      true,
      { ITEM_NONE, ITEM_NONE, ITEM_NONE } },
    { 0x0033084Fu,
      RANDO_LOC_CATEGORY_KEY_ITEM,
      "Veil Falls - Rupee Cave Top Middle",
      ITEM_RUPEE20,
      RH_FALLS,
      false,
      true,
      { ITEM_NONE, ITEM_NONE, ITEM_NONE } },
    { 0x00330850u,
      RANDO_LOC_CATEGORY_KEY_ITEM,
      "Veil Falls - Rupee Cave Top Right",
      ITEM_RUPEE20,
      RH_FALLS,
      false,
      true,
      { ITEM_NONE, ITEM_NONE, ITEM_NONE } },
    { 0x00330851u,
      RANDO_LOC_CATEGORY_KEY_ITEM,
      "Veil Falls - Rupee Cave Top Bottom",
      ITEM_RUPEE20,
      RH_FALLS,
      false,
      true,
      { ITEM_NONE, ITEM_NONE, ITEM_NONE } },
    { 0x00330852u,
      RANDO_LOC_CATEGORY_KEY_ITEM,
      "Veil Falls - Rupee Cave Side Top",
      ITEM_RUPEE20,
      RH_FALLS,
      false,
      true,
      { ITEM_NONE, ITEM_NONE, ITEM_NONE } },
    { 0x00330853u,
      RANDO_LOC_CATEGORY_KEY_ITEM,
      "Veil Falls - Rupee Cave Side Left",
      ITEM_RUPEE20,
      RH_FALLS,
      false,
      true,
      { ITEM_NONE, ITEM_NONE, ITEM_NONE } },
    { 0x00330854u,
      RANDO_LOC_CATEGORY_KEY_ITEM,
      "Veil Falls - Rupee Cave Side Right",
      ITEM_RUPEE20,
      RH_FALLS,
      false,
      true,
      { ITEM_NONE, ITEM_NONE, ITEM_NONE } },
    { 0x00330855u,
      RANDO_LOC_CATEGORY_KEY_ITEM,
      "Veil Falls - Rupee Cave Side Bottom",
      ITEM_RUPEE20,
      RH_FALLS,
      false,
      true,
      { ITEM_NONE, ITEM_NONE, ITEM_NONE } },
    { 0x00330856u,
      RANDO_LOC_CATEGORY_KEY_ITEM,
      "Veil Falls - Rupee Cave Underwater Top Left",
      ITEM_RUPEE50,
      RH_FALLS,
      false,
      true,
      { ITEM_FLIPPERS, ITEM_NONE, ITEM_NONE } },
    { 0x00330857u,
      RANDO_LOC_CATEGORY_KEY_ITEM,
      "Veil Falls - Rupee Cave Underwater Top Right",
      ITEM_RUPEE50,
      RH_FALLS,
      false,
      true,
      { ITEM_FLIPPERS, ITEM_NONE, ITEM_NONE } },
    { 0x00330858u,
      RANDO_LOC_CATEGORY_KEY_ITEM,
      "Veil Falls - Rupee Cave Underwater Middle Left",
      ITEM_RUPEE50,
      RH_FALLS,
      false,
      true,
      { ITEM_FLIPPERS, ITEM_NONE, ITEM_NONE } },
    { 0x00330859u,
      RANDO_LOC_CATEGORY_KEY_ITEM,
      "Veil Falls - Rupee Cave Underwater Middle Right",
      ITEM_RUPEE50,
      RH_FALLS,
      false,
      true,
      { ITEM_FLIPPERS, ITEM_NONE, ITEM_NONE } },
    { 0x0033085Au,
      RANDO_LOC_CATEGORY_KEY_ITEM,
      "Veil Falls - Rupee Cave Underwater Bottom Left",
      ITEM_RUPEE50,
      RH_FALLS,
      false,
      true,
      { ITEM_FLIPPERS, ITEM_NONE, ITEM_NONE } },
    { 0x0033085Bu,
      RANDO_LOC_CATEGORY_KEY_ITEM,
      "Veil Falls - Rupee Cave Underwater Bottom Right",
      ITEM_RUPEE50,
      RH_FALLS,
      false,
      true,
      { ITEM_FLIPPERS, ITEM_NONE, ITEM_NONE } },
    { RANDO_SCRIPTED_KEY(RANDO_SCRIPTED_KEY_SPECIAL, RANDO_SPECIAL_KEY_BIGGORON, 0, 0),
      RANDO_LOC_CATEGORY_KEY_ITEM,
      "Veil Falls - Biggoron",
      ITEM_NONE,
      RH_FALLS,
      false,
      false,
      { ITEM_KINSTONE_BAG, ITEM_NONE, ITEM_NONE } },
    { 0x000802F4u,
      RANDO_LOC_CATEGORY_KEY_ITEM,
      "Cloud Tops - North Kill",
      ITEM_KINSTONE,
      RH_CLOUD_TOPS,
      true,
      false,
      { ITEM_NONE, ITEM_NONE, ITEM_NONE } },
    { 0x000802F6u,
      RANDO_LOC_CATEGORY_KEY_ITEM,
      "Cloud Tops - South Kill",
      ITEM_KINSTONE,
      RH_CLOUD_TOPS,
      true,
      false,
      { ITEM_NONE, ITEM_NONE, ITEM_NONE } },
    { 0x000801E5u,
      RANDO_LOC_CATEGORY_KEY_ITEM,
      "Cloud Tops - North West Dig Spot",
      ITEM_KINSTONE,
      RH_CLOUD_TOPS,
      false,
      false,
      { ITEM_MOLE_MITTS, ITEM_NONE, ITEM_NONE } },
    { 0x000801E6u,
      RANDO_LOC_CATEGORY_KEY_ITEM,
      "Cloud Tops - North East Dig Spot",
      ITEM_KINSTONE,
      RH_CLOUD_TOPS,
      false,
      false,
      { ITEM_MOLE_MITTS, ITEM_NONE, ITEM_NONE } },
    { 0x000801E7u,
      RANDO_LOC_CATEGORY_KEY_ITEM,
      "Cloud Tops - South Middle Dig Spot",
      ITEM_KINSTONE,
      RH_CLOUD_TOPS,
      false,
      false,
      { ITEM_MOLE_MITTS, ITEM_NONE, ITEM_NONE } },
    { 0x000801E8u,
      RANDO_LOC_CATEGORY_KEY_ITEM,
      "Cloud Tops - South East Top Dig Spot",
      ITEM_KINSTONE,
      RH_CLOUD_TOPS,
      false,
      false,
      { ITEM_MOLE_MITTS, ITEM_NONE, ITEM_NONE } },
    { 0x000801E9u,
      RANDO_LOC_CATEGORY_KEY_ITEM,
      "Cloud Tops - South Dig Spot",
      ITEM_KINSTONE,
      RH_CLOUD_TOPS,
      false,
      false,
      { ITEM_MOLE_MITTS, ITEM_NONE, ITEM_NONE } },
    { 0x000801EAu,
      RANDO_LOC_CATEGORY_KEY_ITEM,
      "Cloud Tops - South Right Dig Spot",
      ITEM_KINSTONE,
      RH_CLOUD_TOPS,
      false,
      false,
      { ITEM_MOLE_MITTS, ITEM_NONE, ITEM_NONE } },
    { 0x000801EBu,
      RANDO_LOC_CATEGORY_KEY_ITEM,
      "Cloud Tops - South East Bottom Dig Spot",
      ITEM_KINSTONE,
      RH_CLOUD_TOPS,
      false,
      false,
      { ITEM_MOLE_MITTS, ITEM_NONE, ITEM_NONE } },
    { RANDO_SCRIPTED_KEY(RANDO_SCRIPTED_KEY_SPECIAL, RANDO_SPECIAL_KEY_GREGAL_SHELLS, 0, 0),
      RANDO_LOC_CATEGORY_KEY_ITEM,
      "Wind Tribe - 2F Gregal Reward 1",
      ITEM_SHELLS30,
      RH_WIND_TRIBE,
      false,
      false,
      { ITEM_NONE, ITEM_NONE, ITEM_NONE } },
    { RANDO_SCRIPTED_KEY(RANDO_SCRIPTED_KEY_SPECIAL, RANDO_SPECIAL_KEY_GREGAL_LIGHT_ARROW, 0, 0),
      RANDO_LOC_CATEGORY_KEY_ITEM,
      "Wind Tribe - 2F Gregal Reward 2",
      ITEM_LIGHT_ARROW,
      RH_WIND_TRIBE,
      false,
      false,
      { ITEM_GUST_JAR, ITEM_NONE, ITEM_NONE } },
};

static const uint16_t kProgressionItems[] = { ITEM_GREEN_SWORD,
                                              ITEM_RED_SWORD,
                                              ITEM_BOMBS,
                                              ITEM_BOW,
                                              ITEM_BOOMERANG,
                                              ITEM_SHIELD,
                                              ITEM_LANTERN_OFF,
                                              ITEM_GUST_JAR,
                                              ITEM_PACCI_CANE,
                                              ITEM_MOLE_MITTS,
                                              ITEM_ROCS_CAPE,
                                              ITEM_PEGASUS_BOOTS,
                                              ITEM_OCARINA,
                                              ITEM_GRIP_RING,
                                              ITEM_FLIPPERS,
                                              ITEM_POWER_BRACELETS,
                                              ITEM_BOTTLE1,
                                              ITEM_LIGHT_ARROW,
                                              ITEM_EARTH_ELEMENT,
                                              ITEM_FIRE_ELEMENT,
                                              ITEM_WATER_ELEMENT,
                                              ITEM_WIND_ELEMENT,
                                              ITEM_QST_GRAVEYARD_KEY,
                                              ITEM_QST_LONLON_KEY,
                                              ITEM_JABBERNUT,
                                              ITEM_QST_BOOK1,
                                              ITEM_QST_BOOK2,
                                              ITEM_QST_BOOK3,
                                              ITEM_QST_MUSHROOM,
                                              ITEM_KINSTONE_BAG };

static const uint16_t kMajorItems[] = { ITEM_WALLET,          ITEM_WALLET,       ITEM_WALLET,
                                        ITEM_BOMBBAG,         ITEM_LARGE_QUIVER, ITEM_QST_TINGLE_TROPHY,
                                        ITEM_QST_CARLOV_MEDAL };

static const uint16_t kDojoSkills[] = { ITEM_SKILL_SPIN_ATTACK,  ITEM_SKILL_ROLL_ATTACK, ITEM_SKILL_DASH_ATTACK,
                                        ITEM_SKILL_ROCK_BREAKER, ITEM_SKILL_SWORD_BEAM,  ITEM_SKILL_GREAT_SPIN,
                                        ITEM_SKILL_DOWN_THRUST,  ITEM_SKILL_PERIL_BEAM,  ITEM_SKILL_FAST_SPIN,
                                        ITEM_SKILL_FAST_SPLIT,   ITEM_SKILL_LONG_SPIN };

static const uint16_t kFillerPool[] = { ITEM_HEART_CONTAINER, ITEM_HEART_PIECE, ITEM_SHELLS30, ITEM_RUPEE20,
                                        ITEM_RUPEE50,         ITEM_RUPEE100,    ITEM_BOMBS10,  ITEM_ARROWS10 };

static const uint16_t kRemapJunkPool[] = {
    ITEM_RUPEE1,   ITEM_RUPEE5,      ITEM_RUPEE20, ITEM_RUPEE50,  ITEM_RUPEE100, ITEM_RUPEE200,
    ITEM_KINSTONE, ITEM_BOMBS5,      ITEM_ARROWS5, ITEM_HEART,    ITEM_FAIRY,    ITEM_SHELLS,
    ITEM_SHELLS30, ITEM_HEART_PIECE, ITEM_BOMBS10, ITEM_ARROWS10,
};
static const uint16_t kRemapMajorPool[] = {
    ITEM_BOOMERANG,
    ITEM_SHIELD,
    ITEM_BOTTLE1,
    ITEM_WALLET,
    ITEM_BOMBBAG,
    ITEM_LARGE_QUIVER,
    ITEM_HEART_CONTAINER,
    ITEM_JABBERNUT,
    ITEM_KINSTONE_BAG,
    ITEM_SKILL_SPIN_ATTACK,
    ITEM_SKILL_ROLL_ATTACK,
    ITEM_SKILL_DASH_ATTACK,
    ITEM_SKILL_ROCK_BREAKER,
    ITEM_SKILL_SWORD_BEAM,
    ITEM_SKILL_GREAT_SPIN,
};
static const uint16_t kRemapProgPool[] = {
    ITEM_SMITH_SWORD,     ITEM_BOW,
    ITEM_LANTERN_OFF,     ITEM_GUST_JAR,
    ITEM_PACCI_CANE,      ITEM_MOLE_MITTS,
    ITEM_ROCS_CAPE,       ITEM_PEGASUS_BOOTS,
    ITEM_OCARINA,         ITEM_EARTH_ELEMENT,
    ITEM_FIRE_ELEMENT,    ITEM_WATER_ELEMENT,
    ITEM_WIND_ELEMENT,    ITEM_GRIP_RING,
    ITEM_POWER_BRACELETS, ITEM_FLIPPERS,
    ITEM_QST_LONLON_KEY,  ITEM_QST_GRAVEYARD_KEY,
};

typedef struct SplitMix64 {
    uint64_t state;
} SplitMix64;

extern "C" {
uint16_t randomized_item_table[RANDO_LOCATION_COUNT];
uint8_t randomized_item_subtype_table[RANDO_LOCATION_COUNT];
}

static uint16_t sCompatibilityRemap[256];
static RandomizerSettings sSettings;
static uint64_t sSeed = 0;
static bool sActive = false;
static bool sInitialized = false;
static char sSpoiler[8192];
static uint64_t sAutoSeedCounter = 0x9e3779b97f4a7c15ull;

static uint64_t SplitMix64_Next(SplitMix64* rng) {
    uint64_t z = (rng->state += 0x9e3779b97f4a7c15ull);
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
    return z ^ (z >> 31);
}

static uint32_t RngBounded(SplitMix64* rng, uint32_t bound) {
    if (bound <= 1)
        return 0;
    return (uint32_t)(SplitMix64_Next(rng) % bound);
}

static void ShuffleU16(uint16_t* items, size_t count, SplitMix64* rng) {
    if (count < 2)
        return;
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

static bool HasSword(const bool* items) {
    return items[ITEM_SMITH_SWORD] || items[ITEM_GREEN_SWORD] || items[ITEM_RED_SWORD] || items[ITEM_BLUE_SWORD] ||
           items[ITEM_FOURSWORD];
}

static bool HasBombs(const bool* items) {
    return items[ITEM_BOMBS] || items[ITEM_BOMBS5] || items[ITEM_BOMBS10] || items[ITEM_REMOTE_BOMBS];
}

extern "C" int Rando_Entrance_GetInverseAssignment(int interior_idx);
extern "C" RandoHelperId ExteriorHelperForDoor(int door_idx);

extern "C" RandoHelperId ExteriorHelperForDoor(int door_idx) {
    switch (door_idx) {
        case 0:
            return RH_MINISH_WOODS;
        case 1:
            return RH_CRENEL;
        case 2:
            return RH_WIND_RUINS;
        case 3:
            return RH_LAKE_HYLIA;
        case 4:
            return RH_ROYAL_VALLEY;
        case 5:
            return RH_CLOUD_TOPS;
        case 6:
            return RH_NORTH_FIELD;
        case 7:
            return RH_NORTH_FIELD;
        default:
            return RH_SOUTH_FIELD;
    }
}

static void EvaluateHelpers(const RandomizerSettings* settings, const bool* items, bool* helpers) {
    helpers[RH_SOUTH_FIELD] = true;
    helpers[RH_TOWN] = true;
    helpers[RH_NORTH_FIELD] = true;
    helpers[RH_EASTERN_HILLS] = true;
    helpers[RH_MINISH_WOODS] = true;
    helpers[RH_MINISH_VILLAGE] = true;
    helpers[RH_TRILBY] = true;

    bool changed = true;
    while (changed) {
        changed = false;

#define UPDATE_HELPER(id, expr)   \
    do {                          \
        bool val = (expr);        \
        if (helpers[id] != val) { \
            helpers[id] = val;    \
            changed = true;       \
        }                         \
    } while (0)

        bool has_bombs = HasBombs(items);
        bool has_sword = HasSword(items);
        bool has_bottle = items[ITEM_BOTTLE1] || items[ITEM_BOTTLE2] || items[ITEM_BOTTLE3] || items[ITEM_BOTTLE4];

        /* Glitch-logic tier: tricks are honored only when glitchless logic is off.
         * Each trick also requires the real-game prerequisite for the glitch. */
        uint32_t tricks = settings->glitchless_logic ? 0u : settings->tricks;
        bool og = (tricks & RANDO_TRICK_OCARINA_GLITCH) && items[ITEM_OCARINA];
        bool crenel_clip = (tricks & RANDO_TRICK_CRENEL_CLIP) && has_bottle;
        bool pjs = (tricks & RANDO_TRICK_PORTAL_JUMP_STORAGE) && items[ITEM_OCARINA];
        UPDATE_HELPER(RH_CRENEL_BASE, has_bombs || has_bottle);
        UPDATE_HELPER(RH_CRENEL, helpers[RH_CRENEL_BASE] && (items[ITEM_GRIP_RING] || has_bottle));
        // Crenel Clip (glitch): Mt. Crenel -> Western Woods -> Castor Wilds without Boots/Flippers.
        UPDATE_HELPER(RH_CASTOR_WILDS,
                      items[ITEM_PEGASUS_BOOTS] || items[ITEM_FLIPPERS] || (helpers[RH_CRENEL] && crenel_clip));
        UPDATE_HELPER(RH_WIND_RUINS, helpers[RH_CASTOR_WILDS] && has_sword);
        UPDATE_HELPER(RH_ROYAL_VALLEY,
                      helpers[RH_NORTH_FIELD] && items[ITEM_QST_GRAVEYARD_KEY] && items[ITEM_LANTERN_OFF]);
        UPDATE_HELPER(RH_LON_LON, items[ITEM_QST_LONLON_KEY]);
        UPDATE_HELPER(RH_LAKE_HYLIA, helpers[RH_LON_LON] && (items[ITEM_FLIPPERS] || items[ITEM_OCARINA]));
        UPDATE_HELPER(RH_FALLS, helpers[RH_NORTH_FIELD] && has_sword);
        UPDATE_HELPER(RH_CLOUD_TOPS, (helpers[RH_FALLS] && items[ITEM_GRIP_RING] && items[ITEM_MOLE_MITTS] &&
                                      items[ITEM_FLIPPERS] && has_bombs && items[ITEM_KINSTONE_BAG]) ||
                                         (helpers[RH_TOWN] && pjs));
        UPDATE_HELPER(RH_WIND_TRIBE, helpers[RH_CLOUD_TOPS]);

        // Dynamic entrance mappings
        int dws_door = Rando_Entrance_GetInverseAssignment(0);
        int cof_door = Rando_Entrance_GetInverseAssignment(1);
        int fow_door = Rando_Entrance_GetInverseAssignment(2);
        int tod_door = Rando_Entrance_GetInverseAssignment(3);
        int cry_door = Rando_Entrance_GetInverseAssignment(4);
        int pow_door = Rando_Entrance_GetInverseAssignment(5);
        int dhc_door = Rando_Entrance_GetInverseAssignment(6);

        UPDATE_HELPER(RH_DEEPWOOD, helpers[dws_door >= 0 ? ExteriorHelperForDoor(dws_door) : RH_MINISH_WOODS]);
        UPDATE_HELPER(RH_DEEPWOOD_BOSS, helpers[RH_DEEPWOOD] && items[ITEM_GUST_JAR] && has_sword);
        UPDATE_HELPER(RH_COF, helpers[cof_door >= 0 ? ExteriorHelperForDoor(cof_door) : RH_CRENEL]);
        UPDATE_HELPER(RH_COF_BOSS, helpers[RH_COF] && items[ITEM_PACCI_CANE] && has_bombs && has_sword);
        UPDATE_HELPER(RH_FORTRESS, helpers[fow_door >= 0 ? ExteriorHelperForDoor(fow_door) : RH_WIND_RUINS]);
        UPDATE_HELPER(RH_FORTRESS_BOSS, helpers[RH_FORTRESS] && items[ITEM_MOLE_MITTS] && items[ITEM_BOW] && has_sword);
        // Ocarina Glitch (glitch): doorway OG reaches the Temple of Droplets interior without Flippers.
        UPDATE_HELPER(RH_DROPLETS, helpers[tod_door >= 0 ? ExteriorHelperForDoor(tod_door) : RH_LAKE_HYLIA] &&
                                       (items[ITEM_FLIPPERS] || og) && items[ITEM_GUST_JAR]);
        UPDATE_HELPER(RH_DROPLETS_BOSS,
                      helpers[RH_DROPLETS] && items[ITEM_LANTERN_OFF] && items[ITEM_GUST_JAR] && has_sword);
        UPDATE_HELPER(RH_ROYAL_CRYPT,
                      helpers[cry_door >= 0 ? ExteriorHelperForDoor(cry_door) : RH_ROYAL_VALLEY] && has_sword);
        UPDATE_HELPER(RH_PALACE, helpers[pow_door >= 0 ? ExteriorHelperForDoor(pow_door) : RH_CLOUD_TOPS] &&
                                     items[ITEM_ROCS_CAPE]);
        UPDATE_HELPER(RH_PALACE_BOSS, helpers[RH_PALACE] && items[ITEM_ROCS_CAPE] && items[ITEM_BOW] && has_sword);

        bool four_elements = items[ITEM_EARTH_ELEMENT] && items[ITEM_FIRE_ELEMENT] && items[ITEM_WATER_ELEMENT] &&
                             items[ITEM_WIND_ELEMENT];
        UPDATE_HELPER(RH_FOUR_ELEMENTS, four_elements);
        UPDATE_HELPER(RH_DHC, helpers[RH_FOUR_ELEMENTS] && has_sword && has_bombs && items[ITEM_BOW] &&
                                  items[ITEM_ROCS_CAPE] && items[ITEM_LANTERN_OFF] &&
                                  helpers[dhc_door >= 0 ? ExteriorHelperForDoor(dhc_door) : RH_NORTH_FIELD]);
        UPDATE_HELPER(RH_GOAL, helpers[RH_DHC]);

#undef UPDATE_HELPER
    }
}

static bool IsObscureLocation(const RandoLocationDef* loc) {
    if (strstr(loc->name, "Dig Spot") != NULL)
        return true;
    if (strstr(loc->name, "Graveyard") != NULL)
        return true;
    if (strstr(loc->name, "Dojo") != NULL)
        return false;
    if (strstr(loc->name, "Scrub Reward") != NULL)
        return true;
    if (strstr(loc->name, "DHC -") != NULL)
        return false;
    if (strstr(loc->name, "Melari Upper") != NULL && strstr(loc->name, "Dig") != NULL)
        return true;
    return false;
}
static bool LocationEnabled(const RandomizerSettings* settings, const RandoLocationDef* loc) {
    if (!settings->shuffle_dojos && loc->category == RANDO_LOC_CATEGORY_DOJO) {
        return false;
    }
    if (!settings->obscure_locations && IsObscureLocation(loc)) {
        return false;
    }
    return true;
}

static bool CanAccessLocation(const RandoLocationDef& loc, const bool* helpers, const bool* items) {
    if (!helpers[loc.helper])
        return false;
    if (loc.req_sword && !HasSword(items))
        return false;
    if (loc.req_bombs && !HasBombs(items))
        return false;
    for (int i = 0; i < 3; ++i) {
        if (loc.req_items[i] != ITEM_NONE && !items[loc.req_items[i]])
            return false;
    }
    return true;
}

static const char* ItemName(uint16_t item) {
    switch (item) {
        case ITEM_NONE:
            return "None";
        case ITEM_SMITH_SWORD:
            return "Smith Sword";
        case ITEM_GREEN_SWORD:
            return "Green Sword";
        case ITEM_RED_SWORD:
            return "Red Sword";
        case ITEM_BLUE_SWORD:
            return "Blue Sword";
        case ITEM_FOURSWORD:
            return "Four Sword";
        case ITEM_BOMBS:
            return "Bombs";
        case ITEM_REMOTE_BOMBS:
            return "Remote Bombs";
        case ITEM_BOW:
            return "Bow";
        case ITEM_LIGHT_ARROW:
            return "Light Arrow";
        case ITEM_BOOMERANG:
            return "Boomerang";
        case ITEM_MAGIC_BOOMERANG:
            return "Magic Boomerang";
        case ITEM_SHIELD:
            return "Shield";
        case ITEM_MIRROR_SHIELD:
            return "Mirror Shield";
        case ITEM_LANTERN_OFF:
            return "Lantern";
        case ITEM_GUST_JAR:
            return "Gust Jar";
        case ITEM_PACCI_CANE:
            return "Cane of Pacci";
        case ITEM_MOLE_MITTS:
            return "Mole Mitts";
        case ITEM_ROCS_CAPE:
            return "Roc's Cape";
        case ITEM_PEGASUS_BOOTS:
            return "Pegasus Boots";
        case ITEM_OCARINA:
            return "Ocarina";
        case ITEM_GRIP_RING:
            return "Grip Ring";
        case ITEM_FLIPPERS:
            return "Flippers";
        case ITEM_POWER_BRACELETS:
            return "Power Bracelets";
        case ITEM_BOTTLE1:
            return "Bottle";
        case ITEM_QST_MUSHROOM:
            return "Mushroom";
        case ITEM_QST_LONLON_KEY:
            return "Lon Lon Key";
        case ITEM_QST_GRAVEYARD_KEY:
            return "Graveyard Key";
        case ITEM_JABBERNUT:
            return "Jabbernut";
        case ITEM_QST_BOOK1:
            return "Red Book";
        case ITEM_QST_BOOK2:
            return "Green Book";
        case ITEM_QST_BOOK3:
            return "Blue Book";
        case ITEM_QST_TINGLE_TROPHY:
            return "Tingle Trophy";
        case ITEM_QST_CARLOV_MEDAL:
            return "Carlov Medal";
        case ITEM_EARTH_ELEMENT:
            return "Earth Element";
        case ITEM_FIRE_ELEMENT:
            return "Fire Element";
        case ITEM_WATER_ELEMENT:
            return "Water Element";
        case ITEM_WIND_ELEMENT:
            return "Wind Element";
        case ITEM_SKILL_SPIN_ATTACK:
            return "Spin Attack";
        case ITEM_SKILL_ROLL_ATTACK:
            return "Roll Attack";
        case ITEM_SKILL_DASH_ATTACK:
            return "Dash Attack";
        case ITEM_SKILL_ROCK_BREAKER:
            return "Rock Breaker";
        case ITEM_SKILL_SWORD_BEAM:
            return "Sword Beam";
        case ITEM_SKILL_GREAT_SPIN:
            return "Great Spin";
        case ITEM_SKILL_DOWN_THRUST:
            return "Down Thrust";
        case ITEM_SKILL_PERIL_BEAM:
            return "Peril Beam";
        case ITEM_SKILL_FAST_SPIN:
            return "Fast Spin";
        case ITEM_SKILL_FAST_SPLIT:
            return "Fast Split";
        case ITEM_SKILL_LONG_SPIN:
            return "Long Spin";
        case ITEM_DUNGEON_MAP:
            return "Dungeon Map";
        case ITEM_COMPASS:
            return "Compass";
        case ITEM_BIG_KEY:
            return "Big Key";
        case ITEM_SMALL_KEY:
            return "Small Key";
        case ITEM_WALLET:
            return "Wallet";
        case ITEM_BOMBBAG:
            return "Bomb Bag";
        case ITEM_LARGE_QUIVER:
            return "Large Quiver";
        case ITEM_KINSTONE_BAG:
            return "Kinstone Bag";
        case ITEM_HEART_CONTAINER:
            return "Heart Container";
        case ITEM_HEART_PIECE:
            return "Heart Piece";
        case ITEM_RUPEE20:
            return "20 Rupees";
        case ITEM_RUPEE50:
            return "50 Rupees";
        case ITEM_RUPEE100:
            return "100 Rupees";
        case ITEM_KINSTONE:
            return "Kinstone";
        case ITEM_SHELLS30:
            return "30 Shells";
        case ITEM_BOMBS10:
            return "10 Bombs";
        case ITEM_ARROWS10:
            return "10 Arrows";
        default:
            return "Item";
    }
}

static void EnsureInitialized(void) {
    if (sInitialized)
        return;
    for (size_t i = 0; i < RANDO_LOCATION_COUNT; ++i) {
        randomized_item_table[i] = kLocations[i].vanilla_item;
        randomized_item_subtype_table[i] = 0;
    }
    for (size_t i = 0; i < RANDO_ARRAY_COUNT(sCompatibilityRemap); ++i) {
        sCompatibilityRemap[i] = (uint16_t)i;
    }
    sSettings = Rando_DefaultSettings();
    sSpoiler[0] = '\0';
    sInitialized = true;
}

static bool VerifyTable(const uint16_t* table, const RandomizerSettings* settings) {
    bool collected[RANDO_LOCATION_COUNT];
    bool helpers[RH_COUNT];
    bool current_items[256];
    bool progressed;

    memset(collected, 0, sizeof(collected));
    memset(helpers, 0, sizeof(helpers));
    memset(current_items, 0, sizeof(current_items));

    if (settings->start_sword) {
        current_items[ITEM_SMITH_SWORD] = true;
    }
    EvaluateHelpers(settings, current_items, helpers);

    do {
        progressed = false;
        for (size_t i = 0; i < RANDO_LOCATION_COUNT; ++i) {
            if (collected[i])
                continue;
            if (!LocationEnabled(settings, &kLocations[i]))
                continue;
            if (kLocations[i].category == RANDO_LOC_CATEGORY_CHEST && kLocations[i].vanilla_item == ITEM_SMALL_KEY) {
                // Skip unshuffled small keys in verification (they stay vanilla)
                continue;
            }
            if (!CanAccessLocation(kLocations[i], helpers, current_items))
                continue;

            collected[i] = true;
            uint16_t item = table[i];
            if (item != ITEM_NONE && !current_items[item]) {
                current_items[item] = true;
                progressed = true;
            }
        }
        if (progressed) {
            EvaluateHelpers(settings, current_items, helpers);
        }
    } while (progressed);

    return helpers[RH_GOAL];
}

// Forward reachability sweep: from a base inventory, repeatedly collect items
// from reachable already-filled locations until no new location opens, filling
// out_reachable for every location index. Shared by assumed fill.
static void SweepReachable(const RandomizerSettings* settings, const bool* base_items, const uint16_t* table,
                           const bool* filled, bool* out_reachable) {
    bool owned[256];
    bool helpers[RH_COUNT];
    bool progressed;

    memcpy(owned, base_items, sizeof(owned));
    memset(out_reachable, 0, sizeof(bool) * RANDO_LOCATION_COUNT);
    EvaluateHelpers(settings, owned, helpers);

    do {
        progressed = false;
        for (size_t i = 0; i < RANDO_LOCATION_COUNT; ++i) {
            if (out_reachable[i])
                continue;
            if (!LocationEnabled(settings, &kLocations[i]))
                continue;
            if (!CanAccessLocation(kLocations[i], helpers, owned))
                continue;

            out_reachable[i] = true;
            if (filled[i]) {
                uint16_t item = table[i];
                if (item != ITEM_NONE && item < 256 && !owned[item]) {
                    owned[item] = true;
                    progressed = true;
                }
            }
        }
        if (progressed) {
            EvaluateHelpers(settings, owned, helpers);
        }
    } while (progressed);
}

extern "C" void Rando_Entrance_ClearAssignments(void);
extern "C" void Rando_Entrance_SetAssignment(int loc_idx, int interior_idx);

static RandoStatus BuildSeedAttempt(uint64_t attempt_seed, const RandomizerSettings* settings, uint16_t* out_table,
                                    uint8_t* out_subtypes) {
    SplitMix64 rng;
    rng.state = attempt_seed;

    // Reset entrance assignments (if entrance shuffle is on, we'll randomize them)
    Rando_Entrance_ClearAssignments();
    if (settings->shuffle_entrances) { // coupled dungeon-entrance shuffle
        uint8_t perm[8];
        for (int i = 0; i < 8; ++i)
            perm[i] = i;
        for (int i = 7; i > 0; --i) {
            int j = RngBounded(&rng, i + 1);
            uint8_t tmp = perm[i];
            perm[i] = perm[j];
            perm[j] = tmp;
        }
        for (int i = 0; i < 8; ++i) {
            Rando_Entrance_SetAssignment(i, perm[i]);
        }
    }

    bool filled[RANDO_LOCATION_COUNT];

    memset(filled, 0, sizeof(filled));

    for (size_t i = 0; i < RANDO_LOCATION_COUNT; ++i) {
        out_table[i] = kLocations[i].vanilla_item;
        out_subtypes[i] = 0;

        // Unshuffled locations (keys, etc.) stay vanilla
        if (kLocations[i].category == RANDO_LOC_CATEGORY_CHEST && kLocations[i].vanilla_item == ITEM_SMALL_KEY) {
            filled[i] = true;
        }
        // Disabled locations stay vanilla/empty
        if (!LocationEnabled(settings, &kLocations[i])) {
            filled[i] = true;
        }
    }

    // 1. Place progression items via assumed fill: assume every progression
    //    item is held, then remove and place them one at a time into a location
    //    still reachable without it (sweeping already-placed items). Matches
    //    OoTR/ALttP/TPR/SoH. Relies on the progression pool being singletons.
    bool assumed_items[256];
    memset(assumed_items, 0, sizeof(assumed_items));
    for (uint16_t it : kProgressionItems) {
        assumed_items[it] = true;
    }

    uint16_t progression[RANDO_ARRAY_COUNT(kProgressionItems)];
    memcpy(progression, kProgressionItems, sizeof(kProgressionItems));
    ShuffleU16(progression, RANDO_ARRAY_COUNT(progression), &rng);

    bool reachable[RANDO_LOCATION_COUNT];
    uint16_t candidates[RANDO_LOCATION_COUNT];

    for (size_t p = 0; p < RANDO_ARRAY_COUNT(progression); ++p) {
        uint16_t item = progression[p];

        // Remove the item we are about to place from the assumed inventory.
        assumed_items[item] = false;

        bool base_items[256];
        memcpy(base_items, assumed_items, sizeof(base_items));
        if (settings->start_sword) {
            base_items[ITEM_SMITH_SWORD] = true;
        }

        SweepReachable(settings, base_items, out_table, filled, reachable);

        size_t candidate_count = 0;
        for (size_t i = 0; i < RANDO_LOCATION_COUNT; ++i) {
            if (!filled[i] && reachable[i]) {
                candidates[candidate_count++] = (uint16_t)i;
            }
        }

        if (candidate_count == 0) {
            return RANDO_UNBEATABLE;
        }

        uint16_t chosen = candidates[RngBounded(&rng, (uint32_t)candidate_count)];
        out_table[chosen] = item;
        filled[chosen] = true;
    }

    // 2. Build remaining pool dynamically
    size_t empty_count = 0;
    for (size_t i = 0; i < RANDO_LOCATION_COUNT; ++i) {
        if (!filled[i])
            empty_count++;
    }

    uint16_t* remaining_pool = new uint16_t[empty_count];
    size_t pool_idx = 0;

    // Add major items
    for (uint16_t item : kMajorItems) {
        if (pool_idx < empty_count)
            remaining_pool[pool_idx++] = item;
    }
    if (settings->shuffle_dojos) {
        for (uint16_t item : kDojoSkills) {
            if (pool_idx < empty_count)
                remaining_pool[pool_idx++] = item;
        }
    }

    // Top up with filler items
    while (pool_idx < empty_count) {
        uint16_t filler = kFillerPool[RngBounded(&rng, RANDO_ARRAY_COUNT(kFillerPool))];
        remaining_pool[pool_idx++] = filler;
    }

    ShuffleU16(remaining_pool, empty_count, &rng);

    // Place remaining pool
    size_t place_idx = 0;
    for (size_t i = 0; i < RANDO_LOCATION_COUNT; ++i) {
        if (!filled[i]) {
            out_table[i] = remaining_pool[place_idx++];

            if (out_table[i] == ITEM_KINSTONE) {
                static const uint8_t kKinstoneSubtypes[] = { 0x70, 0x71, 0x72, 0x73, 0x74, 0x75 };
                out_subtypes[i] = kKinstoneSubtypes[RngBounded(&rng, sizeof(kKinstoneSubtypes))];
            }
        }
    }

    delete[] remaining_pool;

    return VerifyTable(out_table, settings) ? RANDO_OK : RANDO_UNBEATABLE;
}

static void RemapPool(const uint16_t* pool, size_t n, SplitMix64* rng) {
    uint16_t perm[64];
    if (n < 2 || n > RANDO_ARRAY_COUNT(perm))
        return;
    for (size_t i = 0; i < n; ++i)
        perm[i] = pool[i];
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

static void BuildCompatibilityRemap(void) {
    SplitMix64 rng;
    for (size_t i = 0; i < RANDO_ARRAY_COUNT(sCompatibilityRemap); ++i) {
        sCompatibilityRemap[i] = (uint16_t)i;
    }
    rng.state = sSeed ? sSeed : 1ull;
    RemapPool(kRemapJunkPool, RANDO_ARRAY_COUNT(kRemapJunkPool), &rng);
    if (sSettings.glitchless_logic)
        return;
    if (sSettings.item_difficulty >= RANDO_ITEM_POOL_HARD) {
        RemapPool(kRemapMajorPool, RANDO_ARRAY_COUNT(kRemapMajorPool), &rng);
    }
    if (sSettings.item_difficulty >= RANDO_ITEM_POOL_CHAOS) {
        RemapPool(kRemapProgPool, RANDO_ARRAY_COUNT(kRemapProgPool), &rng);
    }
}

static void SpoilerAppend(size_t* pos, const char* fmt, ...) {
    if (*pos >= sizeof(sSpoiler))
        return;
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(sSpoiler + *pos, sizeof(sSpoiler) - *pos, fmt, ap);
    va_end(ap);
    if (n <= 0)
        return;
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
        case RANDO_ITEM_POOL_NORMAL:
            return "Normal";
        case RANDO_ITEM_POOL_HARD:
            return "Hard";
        case RANDO_ITEM_POOL_CHAOS:
            return "Chaos";
        default:
            return "?";
    }
}

static void BuildSpoiler(uint64_t seed, const RandomizerSettings* settings) {
    size_t pos = 0;
    SpoilerAppend(&pos, "Seed: %llu\n", (unsigned long long)seed);
    SpoilerAppend(
        &pos,
        "Settings: Pool=%s Glitchless=%d Tricks=0x%X Kinstones=%d Entrances=%d Dojos=%d OpenWorld=%d Homewarp=%d\n\n",
        DifficultyName(settings->item_difficulty), settings->glitchless_logic,
        (unsigned)(settings->glitchless_logic ? 0u : settings->tricks), settings->shuffle_kinstones,
        settings->shuffle_entrances, settings->shuffle_dojos, settings->open_world, settings->homewarp);

    SpoilerAppend(&pos, "Locations:\n");
    for (size_t i = 0; i < RANDO_LOCATION_COUNT; ++i) {
        uint16_t item = randomized_item_table[i];
        if (item == ITEM_NONE)
            continue;
        SpoilerAppend(&pos, "%-40s : %s\n", kLocations[i].name, ItemName(item));
    }
}

static RandoStatus ActivateSeed(uint64_t seed, const RandomizerSettings* settings, const uint16_t* table,
                                const uint8_t* subtype_table, size_t count) {
    if (count > RANDO_LOCATION_COUNT)
        return RANDO_BAD_SETTINGS;
    extern void Rando_Music_ClearAssignments(void);
    Rando_Music_ClearAssignments();
    for (size_t i = 0; i < RANDO_LOCATION_COUNT; ++i) {
        randomized_item_table[i] = (i < count) ? table[i] : (uint16_t)ITEM_NONE;
        randomized_item_subtype_table[i] = (subtype_table != NULL && i < count) ? subtype_table[i] : 0;
    }
    sSettings = *settings;
    sSeed = seed;
    sActive = true;
    BuildCompatibilityRemap();
    BuildSpoiler(seed, settings);
    fprintf(stderr, "[RANDO] seed %llu generated (native logic, %s pool, %zu locations)\n", (unsigned long long)seed,
            DifficultyName(settings->item_difficulty), count);
    return RANDO_OK;
}

extern "C" RandomizerSettings Rando_DefaultSettings(void) {
    RandomizerSettings settings;
    settings.glitchless_logic = true;
    settings.obscure_locations = false;
    settings.shuffle_kinstones = true;
    settings.shuffle_entrances = false;
    settings.shuffle_dojos = true;
    settings.open_world = false;
    settings.item_difficulty = RANDO_ITEM_POOL_NORMAL;
    settings.tricks = 0u;
    settings.homewarp = true;
    settings.start_sword = true;
    settings.early_crests = true;
    settings.instant_text = true;
    settings.tunic_color = 0;
    settings.heart_color = 0;
    return settings;
}

extern "C" uint64_t Rando_SeedFromString(const char* text) {
    uint64_t value = 0;
    bool all_digits = true;
    bool any = false;

    if (text == NULL || text[0] == '\0')
        return 0;

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

extern "C" RandoStatus Rando_GenerateSeed(uint64_t seed, const RandomizerSettings* settings, uint64_t* out_seed) {
    RandomizerSettings local;
    uint16_t candidate[RANDO_LOCATION_COUNT];
    uint8_t candidate_subtypes[RANDO_LOCATION_COUNT];
    RandoStatus last = RANDO_INTERNAL;

    EnsureInitialized();

    local = settings ? *settings : Rando_DefaultSettings();
    if (local.item_difficulty < RANDO_ITEM_POOL_NORMAL || local.item_difficulty >= RANDO_ITEM_POOL_COUNT) {
        return RANDO_BAD_SETTINGS;
    }

    if (seed == 0)
        seed = ChooseAutoSeed();

    for (uint32_t attempt = 0; attempt < 32; ++attempt) {
        uint64_t attempt_seed = seed + 0x9e3779b97f4a7c15ull * (uint64_t)attempt;
        last = BuildSeedAttempt(attempt_seed, &local, candidate, candidate_subtypes);
        if (last == RANDO_OK) {
            if (out_seed)
                *out_seed = seed;
            return ActivateSeed(seed, &local, candidate, candidate_subtypes, RANDO_LOCATION_COUNT);
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
        randomized_item_table[i] = kLocations[i].vanilla_item;
        randomized_item_subtype_table[i] = 0;
    }
    for (size_t i = 0; i < RANDO_ARRAY_COUNT(sCompatibilityRemap); ++i) {
        sCompatibilityRemap[i] = (uint16_t)i;
    }
    extern void Rando_Entrance_ClearAssignments(void);
    extern void Rando_Music_ClearAssignments(void);
    Rando_Entrance_ClearAssignments();
    Rando_Music_ClearAssignments();
    sSpoiler[0] = '\0';
    fprintf(stderr, "[RANDO] reset to vanilla\n");
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

/* Cosmetics (tunic/heart) are pure palette overrides — they never affect
 * item placement or logic — so they can be changed live on an active seed
 * without regenerating. */
extern "C" void Rando_SetCosmetics(int tunic_color, int heart_color) {
    EnsureInitialized();
    sSettings.tunic_color = tunic_color;
    sSettings.heart_color = heart_color;
}

extern "C" const uint16_t* Rando_GetRandomizedItemTable(void) {
    EnsureInitialized();
    return randomized_item_table;
}
extern "C" const uint8_t* Rando_GetRandomizedItemSubtypeTable(void) {
    EnsureInitialized();
    return randomized_item_subtype_table;
}

extern "C" size_t Rando_GetLocationCount(void) {
    return RANDO_LOCATION_COUNT;
}

extern "C" const RandoLocationDef* Rando_GetLocationDef(RandoLocationId id) {
    if ((unsigned)id >= RANDO_LOCATION_COUNT)
        return NULL;
    return &kLocations[(unsigned)id];
}

extern "C" uint16_t Rando_ResolveLocationItem(RandoLocationId location, uint16_t vanilla_item) {
    EnsureInitialized();
    if (!sActive)
        return vanilla_item;
    if ((unsigned)location >= RANDO_LOCATION_COUNT)
        return vanilla_item;
    return randomized_item_table[(unsigned)location];
}

extern "C" bool Rando_OverrideLocationKey(uint32_t location_key, uint8_t* type, uint8_t* subtype) {
    EnsureInitialized();
    if (!sActive || type == NULL)
        return false;

    // Linear search of locations since the count is small (~211)
    for (size_t i = 0; i < RANDO_LOCATION_COUNT; ++i) {
        if (kLocations[i].key == location_key) {
            uint16_t item = randomized_item_table[i];
            uint8_t item_subtype = randomized_item_subtype_table[i];
            if (item == ITEM_NONE)
                return false;
            *type = (uint8_t)item;
            if (subtype != NULL)
                *subtype = item_subtype;
            return true;
        }
    }
    return false;
}

extern "C" bool Rando_ActivateTable(uint64_t seed, RandomizerSettings settings, const uint16_t* table,
                                    const uint8_t* subtype_table, size_t count) {
    EnsureInitialized();
    return ActivateSeed(seed, &settings, table, subtype_table, count) == RANDO_OK;
}

extern "C" bool Rando_VerifyCurrentSeed(void) {
    EnsureInitialized();
    if (!sActive)
        return false;
    return VerifyTable(randomized_item_table, &sSettings);
}

extern "C" bool Rando_OverrideItem(uint8_t* type, uint8_t* subtype) {
    (void)subtype;
    EnsureInitialized();
    if (!sActive || type == NULL)
        return false;
    uint8_t val = *type;
    if (val < RANDO_ARRAY_COUNT(sCompatibilityRemap)) {
        *type = (uint8_t)sCompatibilityRemap[val];
        return true;
    }
    return false;
}

extern "C" size_t Rando_GetSpoiler(char* buf, size_t buflen) {
    EnsureInitialized();
    if (buf == NULL || buflen == 0)
        return 0;
    size_t len = strlen(sSpoiler);
    if (len >= buflen)
        len = buflen - 1;
    memcpy(buf, sSpoiler, len);
    buf[len] = '\0';
    return len;
}