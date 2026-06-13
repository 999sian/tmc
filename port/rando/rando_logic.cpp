#include "rando/rando_logic.h"

#include <string.h>

extern "C" void RandoLogic_Reset(void) {}
extern "C" bool RandoLogic_LoadText(const char* text, size_t len) {
    (void)text;
    (void)len;
    return true;
}
extern "C" bool RandoLogic_IsLoaded(void) {
    return true;
}
extern "C" RandoLogicStats RandoLogic_GetStats(void) {
    RandoLogicStats stats = {};
    stats.location_count = (uint32_t)Rando_GetLocationCount();
    stats.loaded = true;
    stats.native_assignable = true;
    return stats;
}
extern "C" bool RandoLogic_LoadDefaultFiles(void) {
    return true;
}
extern "C" RandoStatus RandoLogic_Generate(uint64_t seed, const RandomizerSettings* settings,
                                            uint16_t* out_table, size_t out_table_count,
                                            uint64_t* out_seed) {
    (void)out_table;
    (void)out_table_count;
    return Rando_GenerateSeed(seed, settings, out_seed);
}
extern "C" uint8_t RandoLogic_GetGeneratedItemSubtype(uint32_t location_index) {
    const uint8_t* table = Rando_GetRandomizedItemSubtypeTable();
    return location_index < Rando_GetLocationCount() ? table[location_index] : 0;
}
extern "C" int RandoLogic_FindLocationByKey(uint32_t key) {
    for (uint32_t i = 0; i < Rando_GetLocationCount(); ++i) {
        const RandoLocationDef* def = Rando_GetLocationDef((RandoLocationId)i);
        if (def != NULL && def->key == key) return (int)i;
    }
    return -1;
}
extern "C" uint32_t RandoLogic_GetLocationKeyAt(uint32_t index) {
    const RandoLocationDef* def = Rando_GetLocationDef((RandoLocationId)index);
    return def ? def->key : UINT32_MAX;
}
extern "C" uint32_t RandoLogic_GetLocationCountRaw(void) {
    return (uint32_t)Rando_GetLocationCount();
}
extern "C" const char* RandoLogic_GetLocationName(uint32_t index) {
    const RandoLocationDef* def = Rando_GetLocationDef((RandoLocationId)index);
    return def ? def->name : "";
}
extern "C" RandoLogicLocationType RandoLogic_GetLocationType(uint32_t index) {
    (void)index;
    return RANDO_LOGIC_LOCATION_MAJOR;
}
extern "C" void RandoLogic_EvaluateReachability(const uint16_t* active_table,
                                                 bool (*check_item_fn)(const char* name),
                                                 bool* out_reached,
                                                 uint32_t location_count) {
    (void)active_table;
    (void)check_item_fn;
    for (uint32_t i = 0; i < location_count; ++i) out_reached[i] = false;
}
extern "C" uint32_t RandoLogic_GetSettingCount(void) { return 0; }
extern "C" const RandoLogicSetting* RandoLogic_GetSetting(uint32_t index) {
    (void)index;
    return NULL;
}
extern "C" void RandoLogic_ClearOverrides(void) {}
extern "C" void RandoLogic_SetOverride(const char* define, const char* value) {
    (void)define;
    (void)value;
}
extern "C" bool RandoLogic_Reparse(void) { return true; }
extern "C" uint32_t RandoLogic_GetOverrideCount(void) { return 0; }
extern "C" bool RandoLogic_GetOverride(uint32_t index, const char** out_name, const char** out_value) {
    (void)index;
    if (out_name) *out_name = NULL;
    if (out_value) *out_value = NULL;
    return false;
}
extern "C" int RandoLogic_GetEntranceAssignment(uint32_t location_index) {
    (void)location_index;
    return -1;
}
extern "C" void RandoLogic_ClearEntranceAssignments(void) {}
extern "C" bool RandoLogic_RestoreEntranceAssignment(uint32_t location_index, int subtype) {
    (void)location_index;
    (void)subtype;
    return true;
}
extern "C" int RandoLogic_GetMusicAssignment(uint32_t area) {
    (void)area;
    return -1;
}
extern "C" void RandoLogic_ClearMusicAssignments(void) {}
extern "C" bool RandoLogic_RestoreMusicAssignment(uint32_t area, int song) {
    (void)area;
    (void)song;
    return true;
}
extern "C" bool RandoLogic_LocationHasTagName(uint32_t location_index, const char* tag_name) {
    (void)location_index;
    (void)tag_name;
    return false;
}
extern "C" bool RandoLogic_BindRuntimeKey(const char* location_name, uint32_t key) {
    (void)location_name;
    (void)key;
    return false;
}
extern "C" uint32_t RandoLogic_GetEventDefineCount(void) { return 0; }
extern "C" const char* RandoLogic_GetEventDefineName(uint32_t index) {
    (void)index;
    return NULL;
}
extern "C" bool RandoLogic_HasEventDefine(const char* name, bool* out_has_value) {
    (void)name;
    if (out_has_value) *out_has_value = false;
    return false;
}
extern "C" bool RandoLogic_EvalEventDefine(const char* name, uint64_t seed, uint32_t* out_value) {
    (void)name;
    (void)seed;
    if (out_value) *out_value = 0;
    return false;
}
