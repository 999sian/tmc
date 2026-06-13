#ifndef PORT_RANDO_LOGIC_H
#define PORT_RANDO_LOGIC_H

#include "rando/rando.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RANDO_LOGIC_MAX_LOCATIONS RANDO_LOCATION_COUNT
#define RANDO_LOGIC_MAX_SETTINGS 320
#define RANDO_LOGIC_MAX_COLOR_SETS 8

typedef enum RandoSettingType {
    RANDO_SETTING_FLAG = 0,
    RANDO_SETTING_DROPDOWN,
    RANDO_SETTING_NUMBER,
    RANDO_SETTING_COLOR,
} RandoSettingType;

typedef struct RandoLogicSetting {
    char define[48];
    char label[48];
    char tab[24];
    char group[32];
    char tooltip[1152];
    RandoSettingType type;
    bool flag_on;
    bool default_flag;
    int number;
    int default_number;
    int num_min, num_max;
    int option_count;
    int option_index;
    int default_option;
    char opt_label[24][40];
    char opt_value[24][32];
} RandoLogicSetting;

typedef enum RandoLogicLocationType {
    RANDO_LOGIC_LOCATION_UNKNOWN = 0,
    RANDO_LOGIC_LOCATION_HELPER,
    RANDO_LOGIC_LOCATION_MAJOR,
} RandoLogicLocationType;

typedef struct RandoLogicStats {
    uint32_t item_count;
    uint32_t location_count;
    uint32_t helper_count;
    uint32_t symbol_count;
    uint32_t node_count;
    uint32_t define_count;
    uint32_t native_mapped_items;
    uint32_t tag_count;
    uint32_t prize_rule_count;
    uint32_t eventdefine_count;
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
uint8_t RandoLogic_GetGeneratedItemSubtype(uint32_t location_index);
int RandoLogic_FindLocationByKey(uint32_t key);
uint32_t RandoLogic_GetLocationKeyAt(uint32_t index);
uint32_t RandoLogic_GetLocationCountRaw(void);
const char* RandoLogic_GetLocationName(uint32_t index);
RandoLogicLocationType RandoLogic_GetLocationType(uint32_t index);
void RandoLogic_EvaluateReachability(const uint16_t* active_table,
                                     bool (*check_item_fn)(const char* name),
                                     bool* out_reached,
                                     uint32_t location_count);
uint32_t RandoLogic_GetSettingCount(void);
const RandoLogicSetting* RandoLogic_GetSetting(uint32_t index);
void RandoLogic_ClearOverrides(void);
void RandoLogic_SetOverride(const char* define, const char* value);
bool RandoLogic_Reparse(void);
uint32_t RandoLogic_GetOverrideCount(void);
bool RandoLogic_GetOverride(uint32_t index, const char** out_name, const char** out_value);
int RandoLogic_GetEntranceAssignment(uint32_t location_index);
void RandoLogic_ClearEntranceAssignments(void);
bool RandoLogic_RestoreEntranceAssignment(uint32_t location_index, int subtype);
int RandoLogic_GetMusicAssignment(uint32_t area);
void RandoLogic_ClearMusicAssignments(void);
bool RandoLogic_RestoreMusicAssignment(uint32_t area, int song);
bool RandoLogic_LocationHasTagName(uint32_t location_index, const char* tag_name);
bool RandoLogic_BindRuntimeKey(const char* location_name, uint32_t key);
uint32_t RandoLogic_GetEventDefineCount(void);
const char* RandoLogic_GetEventDefineName(uint32_t index);
bool RandoLogic_HasEventDefine(const char* name, bool* out_has_value);
bool RandoLogic_EvalEventDefine(const char* name, uint64_t seed, uint32_t* out_value);

#ifdef __cplusplus
}
#endif

#endif