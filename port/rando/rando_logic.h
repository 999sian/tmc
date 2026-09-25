#ifndef PORT_RANDO_LOGIC_H
#define PORT_RANDO_LOGIC_H

#include "rando/rando.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RANDO_LOGIC_MAX_ITEMS 4096
#define RANDO_LOGIC_MAX_LOCATIONS 4096u
#define RANDO_LOGIC_MAX_HELPERS 4096
#define RANDO_LOGIC_MAX_SYMBOLS 8192
#define RANDO_LOGIC_MAX_NODES 60000
#define RANDO_LOGIC_MAX_DEFINES 2048
#define RANDO_LOGIC_MAX_SETTINGS 320
#define RANDO_LOGIC_MAX_SETTING_OPTIONS 24
#define RANDO_LOGIC_MAX_TAGS 256
#define RANDO_LOGIC_MAX_LOC_TAGS 24
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
    char tab[24];      /* window tab from the directive (e.g. "Main Settings") */
    char group[32];    /* setting group within the tab (e.g. "Dungeon Settings") */
    char tooltip[1152];
    RandoSettingType type;
    bool flag_on;                 /* current state (flag) */
    bool default_flag;            /* built-in default before overrides */
    int number;                   /* current value (number) */
    int default_number;           /* built-in default before overrides */
    int num_min, num_max;         /* number bounds */
    int option_count;             /* dropdown */
    int option_index;             /* current dropdown option */
    int default_option;           /* built-in default option before overrides */
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
    uint32_t tag_count;
    bool loaded;
    bool native_assignable;
    char error[128];
} RandoLogicStats;

void RandoLogic_Reset(void);
bool RandoLogic_IsLoaded(void);
RandoLogicStats RandoLogic_GetStats(void);
bool RandoLogic_LoadBuiltIn(void);
RandoStatus RandoLogic_Generate(uint64_t seed, const RandomizerSettings* settings,
                                uint16_t* out_table, size_t out_table_count,
                                uint64_t* out_seed);
uint8_t RandoLogic_GetGeneratedItemSubtype(uint32_t location_index);
/* Exact match against the last successful built-in generation in this process.
 * Regenerate with the saved seed/settings after reload, then compare before
 * using the retained symbol assignments. */
bool RandoLogic_GeneratedTableMatches(uint64_t seed, const uint16_t* items,
                                      const uint8_t* subtypes, size_t count);
/* Stable rules version and override pairs in insertion order. Returns 0 when
 * no rules are loaded. */
uint64_t RandoLogic_SourceFingerprint(void);
int RandoLogic_FindLocationByKey(uint32_t key);
uint32_t RandoLogic_GetLocationKeyAt(uint32_t index);
uint32_t RandoLogic_GetLocationCountRaw(void);
const char* RandoLogic_GetLocationName(uint32_t index);
RandoLogicLocationType RandoLogic_GetLocationType(uint32_t index);
/* The callback reports inventory quantity for each Items.* symbol. Dungeon
 * key count expressions require more than a boolean ownership answer. */
void RandoLogic_EvaluateReachability(const uint16_t* active_table,
                                     const uint8_t* active_subtypes,
                                     size_t active_count,
                                     uint64_t active_seed,
                                     uint16_t (*get_item_count_fn)(const char* name),
                                     bool* out_reached,
                                     uint32_t location_count);

/* Declared settings. The menu enumerates these; choices are applied as define
 * overrides before rebuilding the model. */
uint32_t RandoLogic_GetSettingCount(void);
const RandoLogicSetting* RandoLogic_GetSetting(uint32_t index);
void RandoLogic_ClearOverrides(void);
void RandoLogic_SetOverride(const char* define, const char* value);
bool RandoLogic_Rebuild(void);

/* UI override enumeration (sidecar persistence): a stored seed reloads with
 * the same choices it was generated under. */
uint32_t RandoLogic_GetOverrideCount(void);
bool RandoLogic_GetOverride(uint32_t index, const char** out_name, const char** out_value);

/* Dungeon-entrance shuffle: assignment of `Items.Entrance.*` dummies onto
 * DungeonEntrance locations from the last successful generation. Returns the
 * entrance item's subtype (0x01..0x08) assigned to location `index`, or -1
 * when `index` is not an entrance location / nothing was assigned. */
int RandoLogic_GetEntranceAssignment(uint32_t location_index);
/* Sidecar restore path: entrance assignments are generation-time state, so a
 * reloaded slot re-injects them after its rules fingerprint is checked. */
void RandoLogic_ClearEntranceAssignments(void);
bool RandoLogic_RestoreEntranceAssignment(uint32_t location_index, int subtype);

/* Per-area music shuffle: assignment from "Area%xMusic" locations; -1 = vanilla. */
int RandoLogic_GetMusicAssignment(uint32_t area);
void RandoLogic_ClearMusicAssignments(void);
bool RandoLogic_RestoreMusicAssignment(uint32_t area, int song);

/* True when the location carries the named pool tag (e.g. "NoSpoiler"). */
bool RandoLogic_LocationHasTagName(uint32_t location_index, const char* tag_name);

/* Bind a native runtime key (e.g. area-room-flag) onto a compiled location
 * that has no direct key. Fills empty keys only. */
bool RandoLogic_BindRuntimeKey(const char* location_name, uint32_t key);
/* Replace a chest key after converting its TileEntity index to the
 * native chest ordinal. Rejects helpers, invalid keys, and collisions. */
bool RandoLogic_SetRuntimeKeyAt(uint32_t index, uint32_t key);

#ifdef __cplusplus
}
#endif

#endif /* PORT_RANDO_LOGIC_H */
