#ifndef PORT_RANDO_LOGIC_H
#define PORT_RANDO_LOGIC_H

#include "rando/rando.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RANDO_LOGIC_MAX_ITEMS 4096
#define RANDO_LOGIC_MAX_LOCATIONS RANDO_RANDOMIZED_ITEM_TABLE_CAPACITY
#define RANDO_LOGIC_MAX_HELPERS 4096
#define RANDO_LOGIC_MAX_SYMBOLS 8192
#define RANDO_LOGIC_MAX_NODES 60000
#define RANDO_LOGIC_MAX_DEFINES 2048
#define RANDO_LOGIC_MAX_SETTINGS 320
#define RANDO_LOGIC_MAX_SETTING_OPTIONS 10

typedef enum RandoSettingType {
    RANDO_SETTING_FLAG = 0,
    RANDO_SETTING_DROPDOWN,
    RANDO_SETTING_NUMBER,
} RandoSettingType;

typedef struct RandoLogicSetting {
    char define[48];
    char label[48];
    RandoSettingType type;
    bool flag_on;                 /* current state (flag) */
    int number;                   /* current value (number) */
    int num_min, num_max;         /* number bounds */
    int option_count;             /* dropdown */
    int option_index;             /* current dropdown option */
    char opt_label[RANDO_LOGIC_MAX_SETTING_OPTIONS][40];
    char opt_value[RANDO_LOGIC_MAX_SETTING_OPTIONS][32];
} RandoLogicSetting;

typedef enum RandoLogicItemType {
    RANDO_LOGIC_ITEM_UNKNOWN = 0,
    RANDO_LOGIC_ITEM_MUSIC,
    RANDO_LOGIC_ITEM_DUNGEON_ENTRANCE,
    RANDO_LOGIC_ITEM_DUNGEON_CONSTRAINT,
    RANDO_LOGIC_ITEM_OVERWORLD_CONSTRAINT,
    RANDO_LOGIC_ITEM_DUNGEON_PRIZE,
    RANDO_LOGIC_ITEM_DUNGEON_MAJOR,
    RANDO_LOGIC_ITEM_DUNGEON_MINOR,
    RANDO_LOGIC_ITEM_MAJOR,
    RANDO_LOGIC_ITEM_MINOR,
    RANDO_LOGIC_ITEM_FILLER,
} RandoLogicItemType;

typedef enum RandoLogicLocationType {
    RANDO_LOGIC_LOCATION_UNKNOWN = 0,
    RANDO_LOGIC_LOCATION_MUSIC,
    RANDO_LOGIC_LOCATION_HELPER,
    RANDO_LOGIC_LOCATION_UNSHUFFLED,
    RANDO_LOGIC_LOCATION_UNSHUFFLED_PRIZE,
    RANDO_LOGIC_LOCATION_DUNGEON_ENTRANCE,
    RANDO_LOGIC_LOCATION_DUNGEON_CONSTRAINT,
    RANDO_LOGIC_LOCATION_OVERWORLD_CONSTRAINT,
    RANDO_LOGIC_LOCATION_DUNGEON_PRIZE,
    RANDO_LOGIC_LOCATION_MAJOR,
    RANDO_LOGIC_LOCATION_DUNGEON,
    RANDO_LOGIC_LOCATION_ANY,
    RANDO_LOGIC_LOCATION_MINOR,
    RANDO_LOGIC_LOCATION_INACCESSIBLE,
} RandoLogicLocationType;

typedef struct RandoLogicStats {
    uint32_t item_count;
    uint32_t location_count;
    uint32_t helper_count;
    uint32_t symbol_count;
    uint32_t node_count;
    uint32_t define_count;
    uint32_t native_mapped_items;
    bool loaded;
    bool native_assignable;
    char error[128];
} RandoLogicStats;

void RandoLogic_Reset(void);
bool RandoLogic_LoadText(const char* text, size_t len);
bool RandoLogic_IsLoaded(void);
RandoLogicStats RandoLogic_GetStats(void);
bool RandoLogic_LoadDefaultFiles(void);
RandoStatus RandoLogic_Generate(uint64_t seed, const RandomizerSettings* settings,
                                uint16_t* out_table, size_t out_table_count,
                                uint64_t* out_seed);
int RandoLogic_FindLocationByKey(uint32_t key);
uint32_t RandoLogic_GetLocationKeyAt(uint32_t index);
uint32_t RandoLogic_GetLocationCountRaw(void);
const char* RandoLogic_GetLocationName(uint32_t index);

/* Declared settings (from !flag/!dropdown/!numberbox). The menu enumerates
 * these, lets the player change them, and the choices are applied as define
 * overrides + a reparse before generation. */
uint32_t RandoLogic_GetSettingCount(void);
const RandoLogicSetting* RandoLogic_GetSetting(uint32_t index);
void RandoLogic_ClearOverrides(void);
void RandoLogic_SetOverride(const char* define, const char* value);
bool RandoLogic_Reparse(void);

#ifdef __cplusplus
}
#endif

#endif /* PORT_RANDO_LOGIC_H */
