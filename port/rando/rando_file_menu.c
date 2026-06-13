/*
 * port/rando/rando_file_menu.c — native file-select setup menu backend.
 */

#include "rando/rando_file_menu.h"
#include "rando/rando.h"
#include "rando/rando_save.h"
#include "port_runtime_config.h"

extern void Rando_Cosmetic_Apply(void);

#include <SDL3/SDL.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern void Port_FileSelectRando_StartSlot(int slot);
extern bool Port_ImGui_CanPresent(void);
extern void Port_FileSelectRando_CancelSlot(int slot);

typedef struct RandoFileMenuState {
    bool open;
    int save_slot;
    char seed_text[RANDO_FILE_MENU_SEED_MAX + 1];
    size_t seed_len;
    bool glitchless_logic;
    bool shuffle_kinstones;
    bool shuffle_dojos;
    bool open_world;
    RandoItemPoolDifficulty difficulty;
    bool homewarp;
    bool start_sword;
    bool early_crests;
    bool instant_text;
    int tunic_color;
    int heart_color;
    char status[96];
} RandoFileMenuState;

static RandoFileMenuState sMenu;
static int sEnabledCached = -1;
static bool sRandoOptionEnabled = false;
static bool sRandoOptionLoaded = false;
static bool sShowSidebar = false;
static uint64_t sQuickSeedCounter = 0x853c49e6748fea9bull;

static void EnsureRandoOptionLoaded(void) {
    if (sRandoOptionLoaded) return;
    sRandoOptionLoaded = true;
    sRandoOptionEnabled = Port_Config_GetRandoEnabled();
}

static uint64_t SplitMix64_NextLocal(uint64_t* state) {
    uint64_t z = (*state += 0x9e3779b97f4a7c15ull);
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
    return z ^ (z >> 31);
}

char* Port_RandoFileMenu_SeedBuffer(void) {
    return sMenu.seed_text;
}

void Port_RandoFileMenu_SetSeed(const char* text) {
    if (text == NULL) text = "";
    SDL_snprintf(sMenu.seed_text, sizeof(sMenu.seed_text), "%s", text);
    Port_RandoFileMenu_SeedEdited();
}

void Port_RandoFileMenu_RandomizeSeed(void) {
    SplitMix64_NextLocal(&sQuickSeedCounter);
    uint32_t val = (uint32_t)(sQuickSeedCounter & 0xFFFFFFFFu);
    char buf[16];
    SDL_snprintf(buf, sizeof(buf), "%u", val);
    Port_RandoFileMenu_SetSeed(buf);
}

bool Port_RandoFileMenu_IsSeedChar(char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

void Port_RandoFileMenu_SeedEdited(void) {
    sMenu.seed_text[RANDO_FILE_MENU_SEED_MAX] = '\0';
    sMenu.seed_len = SDL_strlen(sMenu.seed_text);
}

const char* Port_RandoFileMenu_Status(void) {
    return sMenu.status;
}

bool* Port_RandoFileMenu_GlitchlessLogic(void) {
    return &sMenu.glitchless_logic;
}

bool* Port_RandoFileMenu_ShuffleKinstones(void) {
    return &sMenu.shuffle_kinstones;
}

bool* Port_RandoFileMenu_ShuffleDojos(void) {
    return &sMenu.shuffle_dojos;
}

bool* Port_RandoFileMenu_OpenWorld(void) {
    return &sMenu.open_world;
}

int Port_RandoFileMenu_Difficulty(void) {
    return (int)sMenu.difficulty;
}

void Port_RandoFileMenu_SetDifficulty(int difficulty) {
    if (difficulty < 0) difficulty = 0;
    if (difficulty >= (int)RANDO_ITEM_POOL_COUNT) difficulty = (int)RANDO_ITEM_POOL_COUNT - 1;
    sMenu.difficulty = (RandoItemPoolDifficulty)difficulty;
}

bool* Port_RandoFileMenu_Homewarp(void) { return &sMenu.homewarp; }
bool* Port_RandoFileMenu_StartSword(void) { return &sMenu.start_sword; }
bool* Port_RandoFileMenu_EarlyCrests(void) { return &sMenu.early_crests; }
bool* Port_RandoFileMenu_InstantText(void) { return &sMenu.instant_text; }
int*  Port_RandoFileMenu_TunicColor(void) { return &sMenu.tunic_color; }
int*  Port_RandoFileMenu_HeartColor(void) { return &sMenu.heart_color; }

static uint64_t CurrentSeedValue(void) {
    if (sMenu.seed_len == 0) {
        Port_RandoFileMenu_RandomizeSeed();
    }
    return Rando_SeedFromString(sMenu.seed_text);
}

static void PersistMenuSettings(void) {
    Port_Config_SetRandoSettings(sMenu.glitchless_logic, sMenu.shuffle_kinstones,
                                 sMenu.shuffle_dojos, sMenu.open_world, (int)sMenu.difficulty,
                                 sMenu.homewarp, sMenu.start_sword, sMenu.early_crests,
                                 sMenu.instant_text, sMenu.tunic_color, sMenu.heart_color);
}

void Port_RandoFileMenu_CommitAndStart(void) {
    RandomizerSettings settings;
    uint64_t seed;

    settings.glitchless_logic = sMenu.glitchless_logic;
    settings.shuffle_kinstones = sMenu.shuffle_kinstones;
    settings.shuffle_dojos = sMenu.shuffle_dojos;
    settings.open_world = sMenu.open_world;
    settings.item_difficulty = sMenu.difficulty;
    settings.homewarp = sMenu.homewarp;
    settings.start_sword = sMenu.start_sword;
    settings.early_crests = sMenu.early_crests;
    settings.instant_text = sMenu.instant_text;
    settings.tunic_color = sMenu.tunic_color;
    settings.heart_color = sMenu.heart_color;

    PersistMenuSettings();

    seed = CurrentSeedValue();
    if (GenerateSeed(seed, settings)) {
        if (!Port_RandoSave_SaveActiveSlot(sMenu.save_slot)) {
            SDL_snprintf(sMenu.status, sizeof(sMenu.status), "Generated seed, but sidecar save failed.");
            return;
        }
        sMenu.status[0] = '\0';
        sMenu.open = false;
        Port_FileSelectRando_StartSlot(sMenu.save_slot);
    } else {
        SDL_snprintf(sMenu.status, sizeof(sMenu.status), "Seed failed logic verification; try another seed.");
    }
}

void Port_RandoFileMenu_Cancel(void) {
    if (!sMenu.open) return;
    PersistMenuSettings();
    sMenu.open = false;
    Port_FileSelectRando_CancelSlot(sMenu.save_slot);
}

bool Port_RandoFileMenu_ShouldOpenForNewFile(void) {
    if (!Port_ImGui_CanPresent()) {
        return false;
    }
    if (sEnabledCached < 0) {
        const char* env = getenv("TMC_RANDO_FILE_MENU");
        sEnabledCached = (env == NULL || env[0] == '\0' || env[0] != '0') ? 1 : 0;
    }
    return sEnabledCached != 0 && sRandoOptionEnabled;
}

bool Port_RandoFileMenu_GetRandoOptionEnabled(void) {
    EnsureRandoOptionLoaded();
    return sRandoOptionEnabled;
}

void Port_RandoFileMenu_SetRandoOptionEnabled(bool enabled) {
    EnsureRandoOptionLoaded();
    if (sRandoOptionEnabled == enabled) return;
    sRandoOptionEnabled = enabled;
    Port_Config_SetRandoEnabled(enabled);
    if (!enabled) {
        RandomizerSettings defaults = Rando_DefaultSettings();
        Port_Config_SetRandoSettings(defaults.glitchless_logic, defaults.shuffle_kinstones,
                                     defaults.shuffle_dojos, defaults.open_world,
                                     (int)defaults.item_difficulty, defaults.homewarp,
                                     defaults.start_sword, defaults.early_crests,
                                     defaults.instant_text, defaults.tunic_color,
                                     defaults.heart_color);
    }
}

void Port_RandoFileMenu_RestorePersistedSettings(void) {
    EnsureRandoOptionLoaded();
    if (Rando_IsActive()) {
        Rando_Cosmetic_Apply();
    }
}
void Port_RandoFileMenu_PersistLogicOverrides(void) {
}

void Port_RandoFileMenu_Open(int save_slot) {
    memset(&sMenu, 0, sizeof(sMenu));
    sMenu.open = true;
    sMenu.save_slot = save_slot;
    sMenu.glitchless_logic = Port_Config_GetRandoGlitchless();
    sMenu.shuffle_kinstones = Port_Config_GetRandoKinstones();
    sMenu.shuffle_dojos = Port_Config_GetRandoDojos();
    sMenu.open_world = Port_Config_GetRandoOpenWorld();
    Port_RandoFileMenu_SetDifficulty(Port_Config_GetRandoItemPool());
    sMenu.homewarp = Port_Config_GetRandoHomewarp();
    sMenu.start_sword = Port_Config_GetRandoStartSword();
    sMenu.early_crests = Port_Config_GetRandoEarlyCrests();
    sMenu.instant_text = Port_Config_GetRandoInstantText();
    sMenu.tunic_color = Port_Config_GetRandoTunicColor();
    sMenu.heart_color = Port_Config_GetRandoHeartColor();
    Port_RandoFileMenu_SetSeed("MINISH");
}

void Port_RandoFileMenu_Close(void) {
    sMenu.open = false;
}

bool Port_RandoFileMenu_IsOpen(void) {
    extern bool Rando_IsInFileSelect(void);
    return sMenu.open || (Rando_IsInFileSelect() && sShowSidebar);
}

bool Port_RandoFileMenu_IsModalOpen(void) {
    return sMenu.open;
}

bool Port_RandoFileMenu_IsSidebarOpen(void) {
    return sShowSidebar;
}

void Port_RandoFileMenu_SetSidebarOpen(bool open) {
    sShowSidebar = open;
}

void Port_RandoFileMenu_ToggleSidebar(void) {
    sShowSidebar = !sShowSidebar;
}
