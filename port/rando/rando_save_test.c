/* Run: cc -std=c11 -D_DEFAULT_SOURCE -Iport -Iinclude \
 *      port/rando/rando_save_test.c -o /tmp/rando_save_test && /tmp/rando_save_test
 * Exercises the actual sidecar loader against v6/v7 records and v8 logic seeds. */
#include <assert.h>
#include <stdlib.h>

#include "rando_save.c"

typedef struct LegacySlot211 {
    uint8_t active, glitchless_logic, shuffle_kinstones, shuffle_dojos, item_difficulty, open_world;
    uint16_t override_count, entrance_count;
    uint8_t shuffle_entrances, accessibility;
    uint32_t tricks, logic_location_count;
    uint64_t seed;
    uint32_t count;
    RandoSidecarOverride overrides[RANDO_SIDECAR_MAX_OVERRIDES];
    RandoSidecarEntrance entrances[RANDO_SIDECAR_MAX_ENTRANCES];
    int16_t music[RANDO_SIDECAR_MUSIC_AREAS];
    uint16_t table[RANDO_SIDECAR_V6_FIRST_CAPACITY];
    uint8_t subtype_table[RANDO_SIDECAR_V6_FIRST_CAPACITY];
    uint8_t obscure_locations, homewarp, start_sword, early_crests;
    uint8_t instant_text, tunic_color, heart_color, reserved3;
} LegacySlot211;

static char sSavePath[256];
static bool sActive;
static uint64_t sSeed;
static RandomizerSettings sSettings;
static uint16_t sTable[RANDO_LOGIC_MAX_LOCATIONS];
static uint8_t sSubtype[RANDO_LOGIC_MAX_LOCATIONS];
static size_t sLocationCount = RANDO_LOCATION_COUNT;
static bool sLogicSeed;
static uint64_t sLogicFingerprint;
static bool sLogicAvailable = true;
static bool sLogicLoaded = true;
static uint64_t sLogicVersion = 1;
static uint32_t sParserLocationCount = RANDO_LOGIC_MAX_LOCATIONS;
static RandoSidecarOverride sParserOverrides[RANDO_SIDECAR_MAX_LOGIC_OVERRIDES];
static uint32_t sParserOverrideCount;
static int sEntrances[8];
static int sMusic[RANDO_SIDECAR_MUSIC_AREAS];

const char* Port_Save_GetActivePath(void) { return sSavePath; }
bool Rando_IsActive(void) { return sActive; }
uint64_t Rando_GetSeed64(void) { return sSeed; }
RandomizerSettings Rando_GetSettings(void) { return sSettings; }
size_t Rando_GetLocationCount(void) { return sLocationCount; }
bool Rando_IsLogicSeed(void) { return sLogicSeed; }
uint64_t Rando_GetLogicFingerprint(void) { return sLogicFingerprint; }
const uint16_t* Rando_GetRandomizedItemTable(void) { return sTable; }
const uint8_t* Rando_GetRandomizedItemSubtypeTable(void) { return sSubtype; }
RandomizerSettings Rando_DefaultSettings(void) {
    RandomizerSettings settings = { 0 };
    settings.homewarp = true;
    return settings;
}
const RandoLocationDef* Rando_GetLocationDef(RandoLocationId id) {
    static RandoLocationDef def;
    def.vanilla_item = (uint16_t)(500 + id);
    return &def;
}
bool Rando_ActivateTable(uint64_t seed, RandomizerSettings settings, const uint16_t* table,
                         const uint8_t* subtype_table, size_t count) {
    sSeed = seed;
    sSettings = settings;
    sActive = true;
    sLogicSeed = false;
    sLocationCount = count;
    memcpy(sTable, table, count * sizeof(sTable[0]));
    memcpy(sSubtype, subtype_table, count);
    return true;
}
void RandoLogic_ClearOverrides(void) { sParserOverrideCount = 0; }
void RandoLogic_SetOverride(const char* name, const char* value) {
    assert(sParserOverrideCount < RANDO_SIDECAR_MAX_LOGIC_OVERRIDES);
    RandoSidecarOverride* rec = &sParserOverrides[sParserOverrideCount++];
    assert(strlen(name) < sizeof(rec->name) && strlen(value) < sizeof(rec->value));
    strcpy(rec->name, name);
    strcpy(rec->value, value);
}
uint32_t RandoLogic_GetOverrideCount(void) { return sParserOverrideCount; }
bool RandoLogic_GetOverride(uint32_t index, const char** name, const char** value) {
    if (index >= sParserOverrideCount) return false;
    *name = sParserOverrides[index].name;
    *value = sParserOverrides[index].value;
    return true;
}
bool RandoLogic_LoadDefaultFiles(void) { sLogicLoaded = sLogicAvailable; return sLogicLoaded; }
uint64_t RandoLogic_SourceFingerprint(void) {
    if (!sLogicLoaded) return 0;
    uint64_t hash = sLogicVersion;
    for (uint32_t i = 0; i < sParserOverrideCount; ++i) {
        for (const char* p = sParserOverrides[i].name; *p; ++p) hash = hash * 33 + (unsigned char)*p;
        for (const char* p = sParserOverrides[i].value; *p; ++p) hash = hash * 33 + (unsigned char)*p;
    }
    return hash;
}
uint32_t RandoLogic_GetLocationCountRaw(void) { return sParserLocationCount; }
bool Rando_ActivateLogicTable(uint64_t seed, RandomizerSettings settings, const uint16_t* table,
                              const uint8_t* subtypes, size_t count, uint64_t fingerprint) {
    if (fingerprint != RandoLogic_SourceFingerprint() || count != sParserLocationCount) return false;
    sSeed = seed;
    sSettings = settings;
    sActive = sLogicSeed = true;
    sLogicFingerprint = fingerprint;
    sLocationCount = count;
    memcpy(sTable, table, count * sizeof(sTable[0]));
    memcpy(sSubtype, subtypes, count);
    return true;
}
void Rando_Entrance_ClearAssignments(void) {
    for (size_t i = 0; i < 8; ++i) sEntrances[i] = -1;
}
void Rando_Entrance_SetAssignment(int loc, int interior) { sEntrances[loc] = interior; }
int Rando_Entrance_GetAssignment(int loc) { return sEntrances[loc]; }
void Rando_Music_ClearAssignments(void) {
    for (size_t i = 0; i < RANDO_SIDECAR_MUSIC_AREAS; ++i) sMusic[i] = -1;
}
void Rando_Music_SetAssignment(int area, int song) { sMusic[area] = song; }
int Rando_Music_GetAssignment(int area) { return sMusic[area]; }

static void WriteLegacy(uint32_t version, uint32_t capacity) {
    char path[512];
    BuildSidecarPath(path, sizeof(path));
    FILE* f = fopen(path, "wb");
    assert(f);
    assert(version == 6 || (version == 7 && capacity == 228));
    assert(fwrite(kMagic, sizeof(kMagic), 1, f) == 1);
    assert(fwrite(&version, sizeof(version), 1, f) == 1);
    assert(fwrite(&capacity, sizeof(capacity), 1, f) == 1);
    for (int i = 0; i < 3; ++i) {
        RandoSidecarSlot slot = { 0 };
        slot.active = 1;
        slot.count = capacity;
        slot.seed = 100 + i;
        slot.override_count = 1;
        memcpy(slot.overrides[0].name, "test-option", sizeof("test-option"));
        slot.entrance_count = 1;
        slot.entrances[0].location_index = i;
        slot.entrances[0].subtype = i + 1;
        for (size_t a = 0; a < RANDO_SIDECAR_MUSIC_AREAS; ++a) slot.music[a] = -1;
        slot.music[5] = 30 + i;
        slot.homewarp = i & 1;
        slot.tunic_color = i + 3;
        for (uint32_t j = 0; j < capacity; ++j) slot.table[j] = (uint16_t)(1000 + i * 300 + j);
        if (capacity == 228 && i == 1) {
            slot.table[42] = ITEM_BIG_KEY;
            slot.subtype_table[42] = 0x80;
            slot.shuffle_dungeon_items = 1;
        }

        if (capacity == 211) {
            LegacySlot211 old = { 0 };
            assert(offsetof(LegacySlot211, table) == offsetof(RandoSidecarSlot, table));
            memcpy(&old, &slot, offsetof(LegacySlot211, table));
            memcpy(old.table, slot.table, sizeof(old.table));
            memcpy(old.subtype_table, slot.subtype_table, sizeof(old.subtype_table));
            memcpy(&old.obscure_locations, &slot.obscure_locations, 8);
            assert(fwrite(&old, sizeof(old), 1, f) == 1);
        } else {
            const size_t alignment = offsetof(RandoSidecarAlignProbe, slot);
            const size_t unpadded = offsetof(RandoSidecarSlot, table) + 3u * capacity + 8u;
            const size_t old_size = unpadded + (alignment - unpadded % alignment) % alignment;
            assert(fwrite(&slot, old_size, 1, f) == 1);
        }
    }
    assert(fclose(f) == 0);
}

static void CheckLegacy(uint32_t version, uint32_t capacity) {
    WriteLegacy(version, capacity);
    assert(Port_RandoSave_LoadSlot(2));
    assert(Port_RandoSave_LastLoadStatus() == PORT_RANDO_SAVE_LOADED);
    assert(Port_RandoSave_LoadedLegacySlot());
    assert(sSeed == 102 && sTable[210] == 1600 + 210);
    assert(sSettings.homewarp == false && sSettings.tunic_color == 5);
    assert(sEntrances[2] == 3 && sMusic[5] == 32);
    if (capacity == 211) assert(sTable[211] == 711); /* new check pinned vanilla */
    assert(!sSettings.shuffle_dungeon_items);
    assert(Port_RandoSave_LoadSlot(1));
    assert(sSettings.shuffle_dungeon_items == (capacity == 228));
    Port_RandoSave_CopySlot(0, 0); /* rewrite as v8 without changing any slot */
    char path[512];
    BuildSidecarPath(path, sizeof(path));
    FILE* f = fopen(path, "rb");
    assert(f);
    char header[16];
    assert(fread(header, sizeof(header), 1, f) == 1);
    assert(fclose(f) == 0);
    uint32_t written_version, written_capacity;
    memcpy(&written_version, header + 8, 4);
    memcpy(&written_capacity, header + 12, 4);
    assert(written_version == 8 && written_capacity == RANDO_LOGIC_MAX_LOCATIONS);
    assert(Port_RandoSave_LoadSlot(0));
    assert(!Port_RandoSave_LoadedLegacySlot());
    assert(sSeed == 100 && sTable[210] == 1000 + 210);
    assert(Port_RandoSave_LoadSlot(1));
    assert(sSeed == 101 && sTable[210] == 1300 + 210);
    assert(sSettings.shuffle_dungeon_items == (capacity == 228));
    assert(Port_RandoSave_SaveActiveSlot(1)); /* migrated setting survives a game save */
    assert(Port_RandoSave_LoadSlot(1));
    assert(sSettings.shuffle_dungeon_items == (capacity == 228));
    assert(Port_RandoSave_LoadSlot(2));
    assert(sSeed == 102 && sTable[210] == 1600 + 210);
    assert(sSidecar.slots[2].override_count == 1);
    assert(strcmp(sSidecar.slots[2].overrides[0].name, "test-option") == 0);
}

static void CheckLogicSeed(void) {
    WriteLegacy(7, 228);
    assert(Port_RandoSave_LoadSlot(0));
    RandoLogic_ClearOverrides();
    for (unsigned i = 0; i < 205; ++i) {
        char name[48];
        char value[32];
        snprintf(name, sizeof(name), "option%03u", i);
        snprintf(value, sizeof(value), "%u", i);
        RandoLogic_SetOverride(name, value);
    }
    sLogicSeed = true;
    sSeed = 0x123456789abcdef0ull;
    sLocationCount = RANDO_LOGIC_MAX_LOCATIONS;
    sSettings.start_sword = true;
    for (uint32_t i = 0; i < sLocationCount; ++i) {
        sTable[i] = (uint16_t)(i & 0xff);
        sSubtype[i] = (uint8_t)(i >> 8);
    }
    sLogicFingerprint = RandoLogic_SourceFingerprint();
    assert(Port_RandoSave_SaveActiveSlot(1));
    assert(Port_RandoSave_LoadSlot(0)); /* saving logic did not erase another slot */
    assert(sSeed == 100 && !sLogicSeed);
    RandoLogic_ClearOverrides();
    assert(Port_RandoSave_LoadSlot(1));
    assert(sLogicSeed && sSeed == 0x123456789abcdef0ull);
    assert(sLocationCount == RANDO_LOGIC_MAX_LOCATIONS);
    assert(sTable[4095] == 255 && sSubtype[4095] == 15);
    assert(sSettings.start_sword && sParserOverrideCount == 205);
    assert(strcmp(sParserOverrides[204].name, "option204") == 0);
    sLogicVersion++;
    assert(!Port_RandoSave_LoadSlot(1)); /* changed logic must not misindex the seed */
    assert(Port_RandoSave_LastLoadStatus() == PORT_RANDO_SAVE_INCOMPATIBLE);
    sLogicVersion--;
    sLogicAvailable = false;
    assert(!Port_RandoSave_LoadSlot(1)); /* missing logic must not silently use vanilla */
    assert(Port_RandoSave_LastLoadStatus() == PORT_RANDO_SAVE_INCOMPATIBLE);
    sLogicAvailable = true;
    assert(Port_RandoSave_LoadSlot(1));
    Port_RandoSave_CopySlot(1, 2);
    Port_RandoSave_ClearSlot(1);
    assert(!Port_RandoSave_LoadSlot(1));
    assert(Port_RandoSave_LastLoadStatus() == PORT_RANDO_SAVE_NONE);
    assert(Port_RandoSave_LoadSlot(2));
    assert(sTable[4095] == 255 && sSubtype[4095] == 15);
    assert(Port_RandoSave_LoadSlot(0));
    assert(sSeed == 100 && !sLogicSeed);
}

static void CheckActiveBinding(void) {
    sActive = true;
    sSeed = 12345;
    sLogicFingerprint = 67890;
    sLocationCount = 2;
    sTable[0] = ITEM_SMALL_KEY;
    sTable[1] = ITEM_BIG_KEY;
    sSubtype[0] = 0x81;
    sSubtype[1] = 0x82;
    Rando_Entrance_ClearAssignments();
    uint64_t original = Port_RandoSave_ActiveBindingHash();
    assert(original != 0);
    sSettings.heart_color++;
    assert(Port_RandoSave_ActiveBindingHash() == original); /* live cosmetic */
    sTable[1]++;
    assert(Port_RandoSave_ActiveBindingHash() != original);
    sTable[1]--;
    sSubtype[1]++;
    assert(Port_RandoSave_ActiveBindingHash() != original);
    sSubtype[1]--;
    Rando_Entrance_SetAssignment(0, 1);
    assert(Port_RandoSave_ActiveBindingHash() != original);
    Rando_Entrance_ClearAssignments();
    sSettings.open_world = !sSettings.open_world;
    assert(Port_RandoSave_ActiveBindingHash() != original);
    sSettings.open_world = !sSettings.open_world;
    assert(Port_RandoSave_ActiveBindingHash() == original);
}

static uint32_t ReadV8SlotCount(const char* path, int slot) {
    FILE* f = fopen(path, "rb");
    assert(f);
    assert(fseek(f, 16 + (long)slot * sizeof(RandoSidecarSlot) + offsetof(RandoSidecarSlot, count), SEEK_SET) == 0);
    uint32_t count;
    assert(fread(&count, sizeof(count), 1, f) == 1);
    assert(fclose(f) == 0);
    return count;
}

int main(void) {
    char tmp[] = "/tmp/picori-rando-sidecar-XXXXXX";
    int fd = mkstemp(tmp);
    assert(fd >= 0);
    close(fd);
    remove(tmp);
    assert((size_t)snprintf(sSavePath, sizeof(sSavePath), "%s.sav", tmp) < sizeof(sSavePath));
    assert(!Port_RandoSave_LoadSlot(0));
    assert(Port_RandoSave_LastLoadStatus() == PORT_RANDO_SAVE_NONE);

    CheckLegacy(6, 211);
    CheckLegacy(6, 228);
    CheckLegacy(7, 228);
    CheckLogicSeed();
    CheckActiveBinding();

    char path[512];
    BuildSidecarPath(path, sizeof(path));
    sSidecar.slots[1].active = 1;
    sSidecar.slots[1].count = RANDO_LOGIC_MAX_LOCATIONS + 1;
    sSidecar.slots[1].logic_mode = 1;
    sSidecar.slots[1].logic_fingerprint = 1;
    assert(WriteSidecarFile(path));
    assert(!Port_RandoSave_LoadSlot(1));
    assert(Port_RandoSave_LastLoadStatus() == PORT_RANDO_SAVE_INCOMPATIBLE);
    assert(Port_RandoSave_LoadSlot(0));
    Port_RandoSave_CopySlot(0, 0);
    char backup[520];
    assert((size_t)snprintf(backup, sizeof(backup), "%s.bak", path) < sizeof(backup));
    FILE* saved_corrupt = fopen(backup, "rb");
    assert(saved_corrupt);
    assert(fclose(saved_corrupt) == 0);
    assert(ReadV8SlotCount(backup, 1) == RANDO_LOGIC_MAX_LOCATIONS + 1);
    sSidecar.slots[1].active = 1;
    sSidecar.slots[1].count = RANDO_LOGIC_MAX_LOCATIONS + 2;
    sSidecar.slots[1].logic_mode = 1;
    sSidecar.slots[1].logic_fingerprint = 1;
    assert(WriteSidecarFile(path));
    Port_RandoSave_CopySlot(0, 0);
    char backup2[520];
    assert((size_t)snprintf(backup2, sizeof(backup2), "%s.bak.1", path) < sizeof(backup2));
    assert(ReadV8SlotCount(backup, 1) == RANDO_LOGIC_MAX_LOCATIONS + 1);
    assert(ReadV8SlotCount(backup2, 1) == RANDO_LOGIC_MAX_LOCATIONS + 2);

    FILE* f = fopen(path, "ab");
    assert(f);
    assert(fputc(0, f) != EOF);
    assert(fclose(f) == 0);
    assert(!Port_RandoSave_LoadSlot(0)); /* extra bytes cannot be silently accepted */
    assert(Port_RandoSave_LastLoadStatus() == PORT_RANDO_SAVE_INCOMPATIBLE);

    WriteLegacy(6, 211);
    f = fopen(path, "rb+");
    assert(f);
    assert(fseek(f, -1, SEEK_END) == 0);
    long shortened_length = ftell(f);
    assert(shortened_length > 0);
    assert(ftruncate(fileno(f), shortened_length) == 0);
    assert(fclose(f) == 0);
    assert(!Port_RandoSave_LoadSlot(0)); /* truncated padding is also rejected */
    assert(Port_RandoSave_LastLoadStatus() == PORT_RANDO_SAVE_INCOMPATIBLE);
    remove(path);
    puts("rando_save_test: v6/v7 migration, v8 logic save/reload and length checks passed");
    return 0;
}
