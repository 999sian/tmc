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

#include <SDL3/SDL.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

extern void Port_FileSelectRando_StartSlot(int slot);
extern void Port_FileSelectRando_CancelSlot(int slot);

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
static uint64_t sQuickSeedCounter = 0x853c49e6748fea9bull;

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
}

void Port_RandoFileMenu_SetLogicOption(int idx, int option_index) {
    const RandoLogicSetting* s = RandoLogic_GetSetting((uint32_t)idx);
    if (s == NULL || s->type != RANDO_SETTING_DROPDOWN) return;
    if (option_index < 0 || option_index >= s->option_count) return;
    if (option_index == s->option_index) return;
    RandoLogic_SetOverride(s->define, s->opt_value[option_index]);
    RandoLogic_Reparse();
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
}

void Port_RandoFileMenu_CommitAndStart(void) {
    RandomizerSettings settings;
    uint64_t seed;

    settings.glitchless_logic = sMenu.glitchless_logic;
    settings.shuffle_kinstones = sMenu.shuffle_kinstones;
    settings.shuffle_dojos = sMenu.shuffle_dojos;
    settings.item_difficulty = sMenu.difficulty;

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
    sMenu.open = false;
    Port_FileSelectRando_CancelSlot(sMenu.save_slot);
}

bool Port_RandoFileMenu_ShouldOpenForNewFile(void) {
    /* The ImGui modal presents on every backend (SDL_Renderer and SDL_GPU),
     * so the only gate left is the explicit kill-switch. */
    if (sEnabledCached < 0) {
        const char* env = getenv("TMC_RANDO_FILE_MENU");
        sEnabledCached = (env == NULL || env[0] == '\0' || env[0] != '0') ? 1 : 0;
    }
    return sEnabledCached != 0;
}

void Port_RandoFileMenu_Open(int save_slot) {
    RandomizerSettings defaults = Rando_DefaultSettings();
    memset(&sMenu, 0, sizeof(sMenu));
    sMenu.open = true;
    sMenu.save_slot = save_slot;
    sMenu.glitchless_logic = defaults.glitchless_logic;
    sMenu.shuffle_kinstones = defaults.shuffle_kinstones;
    sMenu.shuffle_dojos = defaults.shuffle_dojos;
    sMenu.difficulty = defaults.item_difficulty;
    sMenu.logic_mode = RandoLogic_IsLoaded();
    if (sMenu.logic_mode) {
        /* Start from the file's declared defaults each time the menu opens. */
        RandoLogic_ClearOverrides();
        RandoLogic_Reparse();
    }
    Port_RandoFileMenu_SetSeed("MINISH");
}

void Port_RandoFileMenu_Close(void) {
    sMenu.open = false;
}

bool Port_RandoFileMenu_IsOpen(void) {
    return sMenu.open;
}
