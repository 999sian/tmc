/*
 * File-select randomizer setup — state machine + commit logic.
 *
 * The modal itself is drawn by port_imgui_menu.cpp (DrawRandoFileMenuModal)
 * inside the per-frame ImGui pass, so it presents on every render backend
 * (SDL_Renderer and SDL_GPU alike). This file owns the menu state, the
 * seed text, the setting mutations (override + reparse for .logic
 * settings), and the Generate/Cancel commit paths. Fixed char buffers,
 * no heap allocation while the overlay is open.
 *
 * Input masking while open: port_bios.c holds all GBA buttons released
 * (Port_UpdateInput) and swallows SDL events after forwarding them to
 * ImGui (Port_PumpEvents), and src/fileselect.c stays parked in
 * STATE_RANDOMIZER_CONFIG until CommitAndStart/Cancel transition out.
 */

#include "rando/rando_file_menu.h"
#include "rando/rando.h"
#include "rando/rando_save.h"
#include "rando/rando_logic.h"
#include "port_runtime_config.h"

#include <SDL3/SDL.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern void Port_FileSelectRando_StartSlot(int slot);
extern void Port_FileSelectRando_CancelSlot(int slot);
extern bool Port_ImGui_CanPresent(void);

typedef struct RandoFileMenuState {
    bool open;
    int save_slot;
    char seed_text[RANDO_FILE_MENU_SEED_MAX + 1];
    size_t seed_len;
    bool glitchless_logic;
    bool shuffle_kinstones;
    bool shuffle_dojos;
    RandoItemPoolDifficulty difficulty;
    char status[96];
    /* When a real .logic file is loaded the engine toggles are replaced by
     * the file's declared settings, applied as overrides. */
    bool logic_mode;
} RandoFileMenuState;

static RandoFileMenuState sMenu;
static int sEnabledCached = -1;
static bool sRandoOptionEnabled = false;
static bool sRandoOptionLoaded = false;
static bool sShowSidebar = false;
static uint64_t sQuickSeedCounter = 0x853c49e6748fea9bull;

/* The "Enable Randomizer Mode" toggle persists in config.json
 * (issue #155). Lazy-loaded so every entry point — modal, sidebar,
 * ShouldOpenForNewFile — sees the restored value without ordering
 * constraints against Port_Config_Load. */
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

bool Port_RandoFileMenu_IsSeedChar(char c) {
    return (c >= '0' && c <= '9') ||
           (c >= 'A' && c <= 'Z') ||
           (c >= 'a' && c <= 'z') ||
           c == '-' || c == '_';
}

void Port_RandoFileMenu_SetSeed(const char* text) {
    size_t n = 0;
    if (text != NULL) {
        while (text[n] != '\0' && n < RANDO_FILE_MENU_SEED_MAX) {
            sMenu.seed_text[n] = text[n];
            n++;
        }
    }
    sMenu.seed_text[n] = '\0';
    sMenu.seed_len = n;
}

void Port_RandoFileMenu_RandomizeSeed(void) {
    uint64_t state = SDL_GetTicksNS() ^ sQuickSeedCounter;
    uint64_t seed = SplitMix64_NextLocal(&state);
    sQuickSeedCounter += 0xda942042e4dd58b5ull;
    if (seed == 0) seed = 1;
    SDL_snprintf(sMenu.seed_text, sizeof(sMenu.seed_text), "%016llX", (unsigned long long)seed);
    sMenu.seed_len = SDL_strlen(sMenu.seed_text);
}

char* Port_RandoFileMenu_SeedBuffer(void) {
    return sMenu.seed_text;
}

/* The ImGui InputText edits seed_text in place (char-filtered through
 * Port_RandoFileMenu_IsSeedChar); resync the cached length afterwards. */
void Port_RandoFileMenu_SeedEdited(void) {
    sMenu.seed_text[RANDO_FILE_MENU_SEED_MAX] = '\0';
    sMenu.seed_len = SDL_strlen(sMenu.seed_text);
}

const char* Port_RandoFileMenu_Status(void) {
    return sMenu.status;
}

bool Port_RandoFileMenu_LogicMode(void) {
    return sMenu.logic_mode;
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

int Port_RandoFileMenu_Difficulty(void) {
    return (int)sMenu.difficulty;
}

void Port_RandoFileMenu_SetDifficulty(int difficulty) {
    if (difficulty < 0) difficulty = 0;
    if (difficulty >= (int)RANDO_ITEM_POOL_COUNT) difficulty = (int)RANDO_ITEM_POOL_COUNT - 1;
    sMenu.difficulty = (RandoItemPoolDifficulty)difficulty;
}

static uint64_t CurrentSeedValue(void) {
    if (sMenu.seed_len == 0) {
        Port_RandoFileMenu_RandomizeSeed();
    }
    return Rando_SeedFromString(sMenu.seed_text);
}

/* Change setting `idx` by `delta` and re-parse so the choice drives
 * generation. Flags toggle regardless of delta sign. */
void Port_RandoFileMenu_ChangeLogicSetting(int idx, int delta) {
    const RandoLogicSetting* s = RandoLogic_GetSetting((uint32_t)idx);
    if (s == NULL) return;
    char value[32];
    switch (s->type) {
        case RANDO_SETTING_FLAG:
            SDL_snprintf(value, sizeof(value), "%s", s->flag_on ? "false" : "true");
            break;
        case RANDO_SETTING_DROPDOWN: {
            if (s->option_count <= 0) return;
            int oi = s->option_index + delta;
            if (oi < 0) oi = s->option_count - 1;
            if (oi >= s->option_count) oi = 0;
            SDL_snprintf(value, sizeof(value), "%s", s->opt_value[oi]);
            break;
        }
        case RANDO_SETTING_NUMBER: {
            int v = s->number + delta;
            if (v < s->num_min) v = s->num_min;
            if (v > s->num_max) v = s->num_max;
            SDL_snprintf(value, sizeof(value), "%d", v);
            break;
        }
        default:
            return;
    }
    RandoLogic_SetOverride(s->define, value);
    RandoLogic_Reparse();
    Port_RandoFileMenu_PersistLogicOverrides();
}

void Port_RandoFileMenu_SetLogicOption(int idx, int option_index) {
    const RandoLogicSetting* s = RandoLogic_GetSetting((uint32_t)idx);
    if (s == NULL || s->type != RANDO_SETTING_DROPDOWN) return;
    if (option_index < 0 || option_index >= s->option_count) return;
    if (option_index == s->option_index) return;
    RandoLogic_SetOverride(s->define, s->opt_value[option_index]);
    RandoLogic_Reparse();
    Port_RandoFileMenu_PersistLogicOverrides();
}

void Port_RandoFileMenu_SetLogicNumber(int idx, int value) {
    const RandoLogicSetting* s = RandoLogic_GetSetting((uint32_t)idx);
    if (s == NULL || s->type != RANDO_SETTING_NUMBER) return;
    if (value < s->num_min) value = s->num_min;
    if (value > s->num_max) value = s->num_max;
    if (value == s->number) return;
    char text[32];
    SDL_snprintf(text, sizeof(text), "%d", value);
    RandoLogic_SetOverride(s->define, text);
    RandoLogic_Reparse();
    Port_RandoFileMenu_PersistLogicOverrides();
}

/* Persist the current .logic define overrides to config.json so the
 * player's settings survive a restart (issue #155). Also called from the
 * ImGui mutation paths in port_imgui_menu.cpp (setting rows, presets,
 * cosmetics) so sidebar tweaks stick without requiring a generation. */
void Port_RandoFileMenu_PersistLogicOverrides(void) {
    uint32_t i, n;
    Port_Config_RandoOverridesBegin();
    n = RandoLogic_GetOverrideCount();
    for (i = 0; i < n; i++) {
        const char* name = NULL;
        const char* value = NULL;
        if (RandoLogic_GetOverride(i, &name, &value)) {
            Port_Config_RandoOverridesAppend(name, value);
        }
    }
    Port_Config_RandoOverridesCommit();
}

static void PersistMenuSettings(void) {
    Port_Config_SetRandoSettings(sMenu.glitchless_logic, sMenu.shuffle_kinstones,
                                 sMenu.shuffle_dojos, (int)sMenu.difficulty);
}

void Port_RandoFileMenu_CommitAndStart(void) {
    RandomizerSettings settings;
    uint64_t seed;

    settings.glitchless_logic = sMenu.glitchless_logic;
    settings.shuffle_kinstones = sMenu.shuffle_kinstones;
    settings.shuffle_dojos = sMenu.shuffle_dojos;
    settings.item_difficulty = sMenu.difficulty;

    /* Settings persist whether or not generation succeeds — they are the
     * player's choice, not a property of the seed. */
    PersistMenuSettings();
    if (sMenu.logic_mode) {
        Port_RandoFileMenu_PersistLogicOverrides();
    }

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
    /* Keep the player's tweaks across the cancel too. */
    PersistMenuSettings();
    sMenu.open = false;
    Port_FileSelectRando_CancelSlot(sMenu.save_slot);
}

bool Port_RandoFileMenu_ShouldOpenForNewFile(void) {
    /* The ImGui modal presents on the SDL_Renderer and SDL_GPU backends.
     * If ImGui can't present this run (surface fallback backend, GPU
     * device probe failed, or runtime-disabled), the modal would open
     * invisible while port_bios.c masks all game input = softlock, so
     * stay on the vanilla new-file flow instead. TMC_RANDO_FILE_MENU=0
     * is the explicit kill-switch. */
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
        /* "Resets to vanilla when switched off" (issue #155): drop the
         * remembered settings back to defaults and clear any .logic
         * define overrides. An already-active seed on a running save is
         * untouched — this only affects future new files. */
        RandomizerSettings defaults = Rando_DefaultSettings();
        Port_Config_SetRandoSettings(defaults.glitchless_logic, defaults.shuffle_kinstones,
                                     defaults.shuffle_dojos, (int)defaults.item_difficulty);
        if (RandoLogic_IsLoaded()) {
            RandoLogic_ClearOverrides();
            RandoLogic_Reparse();
        }
        Port_RandoFileMenu_PersistLogicOverrides();
    }
}

/* Startup restore (port_main.c, right after RandoLogic_LoadDefaultFiles):
 * re-apply the persisted .logic define overrides so the settings browser,
 * the new-file modal, and generation all see the player's last
 * configuration instead of the file defaults. */
void Port_RandoFileMenu_RestorePersistedSettings(void) {
    EnsureRandoOptionLoaded();
    if (!RandoLogic_IsLoaded()) return;
    size_t n = Port_Config_RandoOverrideCount();
    if (n == 0) return;
    for (size_t i = 0; i < n; i++) {
        const char* name = NULL;
        const char* value = NULL;
        if (Port_Config_RandoOverrideAt(i, &name, &value)) {
            RandoLogic_SetOverride(name, value);
        }
    }
    RandoLogic_Reparse();
    fprintf(stderr, "[RANDO] restored %u persisted setting override(s)\n", (unsigned)n);
}

void Port_RandoFileMenu_Open(int save_slot) {
    memset(&sMenu, 0, sizeof(sMenu));
    sMenu.open = true;
    sMenu.save_slot = save_slot;
    /* Settings persist across opens and restarts (issue #155); they only
     * fall back to Rando_DefaultSettings() via the config defaults or
     * when the rando option is switched off. */
    sMenu.glitchless_logic = Port_Config_GetRandoGlitchless();
    sMenu.shuffle_kinstones = Port_Config_GetRandoKinstones();
    sMenu.shuffle_dojos = Port_Config_GetRandoDojos();
    Port_RandoFileMenu_SetDifficulty(Port_Config_GetRandoItemPool());
    sMenu.logic_mode = RandoLogic_IsLoaded();
    /* .logic overrides are global parser state and already reflect the
     * persisted + sidebar-tweaked configuration; opening the modal no
     * longer resets them to file defaults. */
    Port_RandoFileMenu_SetSeed("MINISH");
}

void Port_RandoFileMenu_Close(void) {
    sMenu.open = false;
}

bool Port_RandoFileMenu_IsOpen(void) {
    extern bool Rando_IsInFileSelect(void);
    return sMenu.open || (Rando_IsInFileSelect() && sShowSidebar);
}

/* True only for the forced new-file setup modal (driven by
 * Port_RandoFileMenu_Open). The manually-toggled sidebar is reported
 * by Port_RandoFileMenu_IsSidebarOpen() instead; callers that must
 * treat the two differently (e.g. the L-button close) use this. */
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
