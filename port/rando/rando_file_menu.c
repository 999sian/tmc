/*
 * File-select randomizer setup overlay.
 *
 * SDL_Renderer primitives only: retained static element array, SDL_FRect
 * hitboxes, integer focus index, and fixed char buffers. No heap allocation is
 * performed while the overlay is open or rendering.
 */

#include "rando/rando_file_menu.h"
#include "rando/rando.h"
#include "rando/rando_save.h"
#include "rando/rando_logic.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RANDO_MENU_SEED_MAX 32
#define RANDO_MENU_ELEMENT_COUNT 7

extern void Port_FileSelectRando_StartSlot(int slot);
extern void Port_FileSelectRando_CancelSlot(int slot);

typedef enum RandoMenuElementType {
    RANDO_MENU_TEXTBOX = 0,
    RANDO_MENU_BUTTON,
    RANDO_MENU_CHECKBOX,
    RANDO_MENU_ENUM,
} RandoMenuElementType;

typedef struct RandoMenuElement {
    SDL_FRect rect;
    RandoMenuElementType type;
    const char* label;
} RandoMenuElement;

typedef struct RandoFileMenuState {
    bool open;
    int save_slot;
    int active_element_index;
    char seed_text[RANDO_MENU_SEED_MAX + 1];
    size_t seed_len;
    bool glitchless_logic;
    bool shuffle_kinstones;
    bool shuffle_dojos;
    RandoItemPoolDifficulty difficulty;
    char status[96];
    RandoMenuElement elements[RANDO_MENU_ELEMENT_COUNT];
    /* Logic mode: when a real .logic file is loaded, the toggles are replaced
     * by a scrollable list of its declared settings, applied as overrides. */
    bool logic_mode;
    int logic_row;     /* 0=seed, 1=randomize, 2..=settings, last=generate */
    int logic_scroll;
} RandoFileMenuState;

static SDL_Window* sWindow;
static RandoFileMenuState sMenu;
static int sEnabledCached = -1;
static uint64_t sQuickSeedCounter = 0x853c49e6748fea9bull;

static uint64_t SplitMix64_NextLocal(uint64_t* state) {
    uint64_t z = (*state += 0x9e3779b97f4a7c15ull);
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
    return z ^ (z >> 31);
}

static bool IsSeedChar(char c) {
    return (c >= '0' && c <= '9') ||
           (c >= 'A' && c <= 'Z') ||
           (c >= 'a' && c <= 'z') ||
           c == '-' || c == '_';
}

static const char* DifficultyName(RandoItemPoolDifficulty difficulty) {
    switch (difficulty) {
        case RANDO_ITEM_POOL_NORMAL: return "Normal";
        case RANDO_ITEM_POOL_HARD: return "Hard";
        case RANDO_ITEM_POOL_CHAOS: return "Chaos";
        default: return "Normal";
    }
}

static void SetSeedText(const char* text) {
    size_t n = 0;
    if (text != NULL) {
        while (text[n] != '\0' && n < RANDO_MENU_SEED_MAX) {
            sMenu.seed_text[n] = text[n];
            n++;
        }
    }
    sMenu.seed_text[n] = '\0';
    sMenu.seed_len = n;
}

static void RandomizeSeedText(void) {
    uint64_t state = SDL_GetTicksNS() ^ sQuickSeedCounter;
    uint64_t seed = SplitMix64_NextLocal(&state);
    sQuickSeedCounter += 0xda942042e4dd58b5ull;
    if (seed == 0) seed = 1;
    SDL_snprintf(sMenu.seed_text, sizeof(sMenu.seed_text), "%016llX", (unsigned long long)seed);
    sMenu.seed_len = SDL_strlen(sMenu.seed_text);
}

static void MoveActive(int delta) {
    int next = sMenu.active_element_index + delta;
    if (next < 0) next = RANDO_MENU_ELEMENT_COUNT - 1;
    if (next >= RANDO_MENU_ELEMENT_COUNT) next = 0;
    sMenu.active_element_index = next;
}

static void CycleDifficulty(int delta) {
    int value = (int)sMenu.difficulty + delta;
    if (value < 0) value = (int)RANDO_ITEM_POOL_COUNT - 1;
    if (value >= (int)RANDO_ITEM_POOL_COUNT) value = 0;
    sMenu.difficulty = (RandoItemPoolDifficulty)value;
}

static uint64_t CurrentSeedValue(void) {
    if (sMenu.seed_len == 0) {
        RandomizeSeedText();
    }
    return Rando_SeedFromString(sMenu.seed_text);
}

static void CloseTextInput(void) {
    if (sWindow != NULL) {
        SDL_StopTextInput(sWindow);
    }
}

static void CommitAndStart(void) {
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
        CloseTextInput();
        Port_FileSelectRando_StartSlot(sMenu.save_slot);
    } else {
        SDL_snprintf(sMenu.status, sizeof(sMenu.status), "Seed failed logic verification; try another seed.");
    }
}

static void ActivateCurrent(void) {
    switch (sMenu.active_element_index) {
        case 0:
            break;
        case 1:
            RandomizeSeedText();
            break;
        case 2:
            sMenu.glitchless_logic = !sMenu.glitchless_logic;
            break;
        case 3:
            sMenu.shuffle_kinstones = !sMenu.shuffle_kinstones;
            break;
        case 4:
            sMenu.shuffle_dojos = !sMenu.shuffle_dojos;
            break;
        case 5:
            CycleDifficulty(1);
            break;
        case 6:
            CommitAndStart();
            break;
    }
}

/* ---- Logic mode (real .logic settings) ---------------------------------- */

static int LogicSettingCount(void) {
    return (int)RandoLogic_GetSettingCount();
}

/* Rows: 0 = seed, 1 = randomize, 2..(2+N-1) = settings, (2+N) = generate. */
static int LogicRowCount(void) {
    return 3 + LogicSettingCount();
}

static int LogicGenerateRow(void) {
    return 2 + LogicSettingCount();
}

static int LogicRowToSetting(int row) {
    return (row >= 2 && row < LogicGenerateRow()) ? (row - 2) : -1;
}

/* Change setting `idx` by `delta` and re-parse so the choice drives generation. */
static void ChangeLogicSetting(int idx, int delta) {
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

static void LogicMove(int delta) {
    int rows = LogicRowCount();
    int next = sMenu.logic_row + delta;
    if (next < 0) next = rows - 1;
    if (next >= rows) next = 0;
    sMenu.logic_row = next;
}

static void LogicActivate(void) {
    if (sMenu.logic_row == 1) {
        RandomizeSeedText();
    } else if (sMenu.logic_row == LogicGenerateRow()) {
        CommitAndStart();
    } else {
        int sidx = LogicRowToSetting(sMenu.logic_row);
        if (sidx >= 0) ChangeLogicSetting(sidx, +1);
    }
}

static bool PointInRect(float x, float y, const SDL_FRect* r) {
    return x >= r->x && x < r->x + r->w && y >= r->y && y < r->y + r->h;
}

static void BuildLayout(int window_width, int window_height) {
    const float box_w = 520.0f;
    const float row_h = 28.0f;
    const float row_gap = 6.0f;
    const float box_x = ((float)window_width - box_w) * 0.5f;
    const float box_y = ((float)window_height - 344.0f) * 0.5f;
    float y = box_y + 58.0f;

    static const char* labels[RANDO_MENU_ELEMENT_COUNT] = {
        "Seed Entry",
        "Randomize Seed",
        "Glitchless Logic",
        "Shuffle Kinstones",
        "Shuffle Dojos",
        "Item Difficulty",
        "Generate Seed & Start Game",
    };
    static const RandoMenuElementType types[RANDO_MENU_ELEMENT_COUNT] = {
        RANDO_MENU_TEXTBOX,
        RANDO_MENU_BUTTON,
        RANDO_MENU_CHECKBOX,
        RANDO_MENU_CHECKBOX,
        RANDO_MENU_CHECKBOX,
        RANDO_MENU_ENUM,
        RANDO_MENU_BUTTON,
    };

    for (int i = 0; i < RANDO_MENU_ELEMENT_COUNT; ++i) {
        sMenu.elements[i].rect.x = box_x + 24.0f;
        sMenu.elements[i].rect.y = y;
        sMenu.elements[i].rect.w = box_w - 48.0f;
        sMenu.elements[i].rect.h = row_h;
        sMenu.elements[i].type = types[i];
        sMenu.elements[i].label = labels[i];
        y += row_h + row_gap;
    }
}

static void DrawText(SDL_Renderer* renderer, float x, float y, const char* text,
                     Uint8 r, Uint8 g, Uint8 b, Uint8 a) {
    SDL_SetRenderDrawColor(renderer, r, g, b, a);
    SDL_RenderDebugText(renderer, x, y, text);
}

static void DrawRowText(SDL_Renderer* renderer, int index, const char* value) {
    char line[128];
    const RandoMenuElement* e = &sMenu.elements[index];
    if (value != NULL) {
        SDL_snprintf(line, sizeof(line), "%s: %s", e->label, value);
    } else {
        SDL_snprintf(line, sizeof(line), "%s", e->label);
    }
    DrawText(renderer, e->rect.x + 10.0f, e->rect.y + 8.0f, line,
             index == sMenu.active_element_index ? 255 : 225,
             index == sMenu.active_element_index ? 240 : 230,
             index == sMenu.active_element_index ? 96 : 220,
             255);
}

static void LogicRowText(int row, char* out, size_t out_len) {
    if (row == 0) {
        SDL_snprintf(out, out_len, "Seed: %s%s", sMenu.seed_text, sMenu.logic_row == 0 ? "_" : "");
    } else if (row == 1) {
        SDL_snprintf(out, out_len, "Randomize Seed");
    } else if (row == LogicGenerateRow()) {
        SDL_snprintf(out, out_len, "Generate Seed & Start Game");
    } else {
        const RandoLogicSetting* s = RandoLogic_GetSetting((uint32_t)LogicRowToSetting(row));
        if (s == NULL) { out[0] = '\0'; return; }
        if (s->type == RANDO_SETTING_FLAG) {
            SDL_snprintf(out, out_len, "%s: %s", s->label, s->flag_on ? "On" : "Off");
        } else if (s->type == RANDO_SETTING_DROPDOWN) {
            const char* v = (s->option_index < s->option_count) ? s->opt_label[s->option_index] : "?";
            SDL_snprintf(out, out_len, "%s: %s", s->label, v);
        } else {
            SDL_snprintf(out, out_len, "%s: %d", s->label, s->number);
        }
    }
}

static void RenderLogicSettings(SDL_Renderer* renderer, const SDL_FRect* box) {
    const int visible = 8;
    const float row_h = 26.0f;
    int rows = LogicRowCount();

    if (sMenu.logic_row < sMenu.logic_scroll) sMenu.logic_scroll = sMenu.logic_row;
    if (sMenu.logic_row >= sMenu.logic_scroll + visible) sMenu.logic_scroll = sMenu.logic_row - visible + 1;
    if (sMenu.logic_scroll < 0) sMenu.logic_scroll = 0;

    for (int slot = 0; slot < visible; ++slot) {
        int row = sMenu.logic_scroll + slot;
        if (row >= rows) break;
        SDL_FRect r;
        r.x = box->x + 24.0f;
        r.y = box->y + 70.0f + (float)slot * (row_h + 2.0f);
        r.w = box->w - 48.0f;
        r.h = row_h;
        bool selected = (row == sMenu.logic_row);
        SDL_SetRenderDrawColor(renderer, selected ? 42 : 16, selected ? 48 : 24, selected ? 74 : 44, 232);
        SDL_RenderFillRect(renderer, &r);
        SDL_SetRenderDrawColor(renderer, selected ? 255 : 92, selected ? 224 : 104, selected ? 96 : 132, 255);
        SDL_RenderRect(renderer, &r);
        char line[160];
        LogicRowText(row, line, sizeof(line));
        DrawText(renderer, r.x + 10.0f, r.y + 7.0f, line,
                 selected ? 255 : 225, selected ? 240 : 230, selected ? 96 : 220, 255);
    }
    {
        char foot[64];
        SDL_snprintf(foot, sizeof(foot), "Setting %d / %d   Left/Right change", sMenu.logic_row + 1, rows);
        DrawText(renderer, box->x + 18.0f, box->y + box->h - 28.0f, foot, 150, 210, 170, 255);
    }
}

static void RenderFileMenuUISized(SDL_Renderer* renderer, int window_width, int window_height) {
    SDL_FRect box;
    char seed_line[RANDO_MENU_SEED_MAX + 4];

    if (!sMenu.open || renderer == NULL) return;

    BuildLayout(window_width, window_height);

    box.w = 520.0f;
    box.h = 344.0f;
    box.x = ((float)window_width - box.w) * 0.5f;
    box.y = ((float)window_height - box.h) * 0.5f;

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, 4, 8, 22, 228);
    SDL_RenderFillRect(renderer, &box);
    SDL_SetRenderDrawColor(renderer, 208, 184, 88, 255);
    SDL_RenderRect(renderer, &box);

    DrawText(renderer, box.x + 18.0f, box.y + 14.0f,
             "Randomizer Settings Setup", 255, 240, 128, 255);
    DrawText(renderer, box.x + 18.0f, box.y + 34.0f,
             "D-Pad/Arrows move  Left/Right cycle  Enter/A start  Esc/B cancel",
             185, 196, 216, 255);

    if (sMenu.logic_mode) {
        char logic_line[96];
        RandoLogicStats st = RandoLogic_GetStats();
        SDL_snprintf(logic_line, sizeof(logic_line),
                     "Logic: external .logic (%u locations, %u settings)",
                     st.location_count, RandoLogic_GetSettingCount());
        DrawText(renderer, box.x + 18.0f, box.y + 48.0f, logic_line, 150, 210, 170, 255);
        RenderLogicSettings(renderer, &box);
        return;
    }

    DrawText(renderer, box.x + 18.0f, box.y + 48.0f, "Logic: built-in native graph",
             150, 210, 170, 255);

    for (int i = 0; i < RANDO_MENU_ELEMENT_COUNT; ++i) {
        const bool selected = (i == sMenu.active_element_index);
        const SDL_FRect* r = &sMenu.elements[i].rect;
        SDL_SetRenderDrawColor(renderer, selected ? 42 : 16, selected ? 48 : 24,
                               selected ? 74 : 44, 232);
        SDL_RenderFillRect(renderer, r);
        SDL_SetRenderDrawColor(renderer, selected ? 255 : 92, selected ? 224 : 104,
                               selected ? 96 : 132, 255);
        SDL_RenderRect(renderer, r);
    }

    SDL_snprintf(seed_line, sizeof(seed_line), "%s%s", sMenu.seed_text,
                 sMenu.active_element_index == 0 ? "_" : "");
    DrawRowText(renderer, 0, seed_line);
    DrawRowText(renderer, 1, NULL);
    DrawRowText(renderer, 2, sMenu.glitchless_logic ? "On" : "Off");
    DrawRowText(renderer, 3, sMenu.shuffle_kinstones ? "On" : "Off");
    DrawRowText(renderer, 4, sMenu.shuffle_dojos ? "On" : "Off");
    DrawRowText(renderer, 5, DifficultyName(sMenu.difficulty));
    DrawRowText(renderer, 6, NULL);

    if (sMenu.status[0] != '\0') {
        DrawText(renderer, box.x + 18.0f, box.y + box.h - 28.0f,
                 sMenu.status, 255, 112, 112, 255);
    } else {
        DrawText(renderer, box.x + 18.0f, box.y + box.h - 28.0f,
                 "Seed table is generated directly into native fixed arrays.",
                 150, 210, 170, 255);
    }
}

void Port_RandoFileMenu_SetWindow(SDL_Window* window) {
    sWindow = window;
}

bool Port_RandoFileMenu_ShouldOpenForNewFile(void) {
    /* The overlay masks all game input while open and draws via SDL_Renderer
     * primitives. On the GPU / surface-fallback backends those primitives are
     * never presented, so auto-opening there would mask input behind an
     * invisible menu — a hard freeze. Only engage where it can be drawn; the
     * F8 menu remains the cross-backend activation path. */
    extern bool Port_PPU_OverlaysUseRenderer(void);
    if (!Port_PPU_OverlaysUseRenderer()) {
        return false;
    }
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
    sMenu.active_element_index = 0;
    sMenu.glitchless_logic = defaults.glitchless_logic;
    sMenu.shuffle_kinstones = defaults.shuffle_kinstones;
    sMenu.shuffle_dojos = defaults.shuffle_dojos;
    sMenu.difficulty = defaults.item_difficulty;
    sMenu.logic_mode = RandoLogic_IsLoaded();
    sMenu.logic_row = 0;
    sMenu.logic_scroll = 0;
    if (sMenu.logic_mode) {
        /* Start from the file's declared defaults each time the menu opens. */
        RandoLogic_ClearOverrides();
        RandoLogic_Reparse();
    }
    SetSeedText("MINISH");
    if (sWindow != NULL) {
        SDL_StartTextInput(sWindow);
    }
}

void Port_RandoFileMenu_Close(void) {
    if (!sMenu.open) return;
    sMenu.open = false;
    CloseTextInput();
}

bool Port_RandoFileMenu_IsOpen(void) {
    return sMenu.open;
}

void ProcessFileMenuInput(SDL_Event* event) {
    if (event == NULL || !sMenu.open) return;

    if (event->type == SDL_EVENT_TEXT_INPUT) {
        bool seed_focused = sMenu.logic_mode ? (sMenu.logic_row == 0) : (sMenu.active_element_index == 0);
        if (seed_focused) {
            const char* text = event->text.text;
            for (size_t i = 0; text != NULL && text[i] != '\0'; ++i) {
                if (sMenu.seed_len >= RANDO_MENU_SEED_MAX) break;
                if (!IsSeedChar(text[i])) continue;
                sMenu.seed_text[sMenu.seed_len++] = text[i];
                sMenu.seed_text[sMenu.seed_len] = '\0';
            }
        }
        return;
    }

    if (event->type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
        int w = 0, h = 0;
        if (sWindow != NULL) SDL_GetWindowSizeInPixels(sWindow, &w, &h);
        if (w <= 0) w = 640;
        if (h <= 0) h = 480;
        BuildLayout(w, h);
        for (int i = 0; i < RANDO_MENU_ELEMENT_COUNT; ++i) {
            if (PointInRect(event->button.x, event->button.y, &sMenu.elements[i].rect)) {
                sMenu.active_element_index = i;
                ActivateCurrent();
                return;
            }
        }
        return;
    }

    if (sMenu.logic_mode) {
        if (event->type == SDL_EVENT_KEY_DOWN && !event->key.repeat) {
            switch (event->key.key) {
                case SDLK_UP: LogicMove(-1); return;
                case SDLK_DOWN: case SDLK_TAB: LogicMove(1); return;
                case SDLK_LEFT: { int si = LogicRowToSetting(sMenu.logic_row); if (si >= 0) ChangeLogicSetting(si, -1); return; }
                case SDLK_RIGHT: { int si = LogicRowToSetting(sMenu.logic_row); if (si >= 0) ChangeLogicSetting(si, +1); return; }
                case SDLK_BACKSPACE: if (sMenu.logic_row == 0 && sMenu.seed_len > 0) sMenu.seed_text[--sMenu.seed_len] = '\0'; return;
                case SDLK_RETURN: case SDLK_KP_ENTER: case SDLK_SPACE: LogicActivate(); return;
                case SDLK_ESCAPE: Port_RandoFileMenu_Close(); Port_FileSelectRando_CancelSlot(sMenu.save_slot); return;
                default: return;
            }
        }
        if (event->type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) {
            switch ((SDL_GamepadButton)event->gbutton.button) {
                case SDL_GAMEPAD_BUTTON_DPAD_UP: LogicMove(-1); return;
                case SDL_GAMEPAD_BUTTON_DPAD_DOWN: LogicMove(1); return;
                case SDL_GAMEPAD_BUTTON_DPAD_LEFT: { int si = LogicRowToSetting(sMenu.logic_row); if (si >= 0) ChangeLogicSetting(si, -1); return; }
                case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: { int si = LogicRowToSetting(sMenu.logic_row); if (si >= 0) ChangeLogicSetting(si, +1); return; }
                case SDL_GAMEPAD_BUTTON_SOUTH: case SDL_GAMEPAD_BUTTON_START: LogicActivate(); return;
                case SDL_GAMEPAD_BUTTON_EAST: Port_RandoFileMenu_Close(); Port_FileSelectRando_CancelSlot(sMenu.save_slot); return;
                default: return;
            }
        }
        return;
    }

    if (event->type == SDL_EVENT_KEY_DOWN && !event->key.repeat) {
        switch (event->key.key) {
            case SDLK_UP:
                MoveActive(-1);
                return;
            case SDLK_DOWN:
            case SDLK_TAB:
                MoveActive(1);
                return;
            case SDLK_LEFT:
                if (sMenu.active_element_index == 5) CycleDifficulty(-1);
                else if (sMenu.active_element_index >= 2 && sMenu.active_element_index <= 4) ActivateCurrent();
                return;
            case SDLK_RIGHT:
                if (sMenu.active_element_index == 5) CycleDifficulty(1);
                else if (sMenu.active_element_index >= 2 && sMenu.active_element_index <= 4) ActivateCurrent();
                return;
            case SDLK_BACKSPACE:
                if (sMenu.active_element_index == 0 && sMenu.seed_len > 0) {
                    sMenu.seed_text[--sMenu.seed_len] = '\0';
                }
                return;
            case SDLK_RETURN:
            case SDLK_KP_ENTER:
            case SDLK_SPACE:
                ActivateCurrent();
                return;
            case SDLK_ESCAPE:
                Port_RandoFileMenu_Close();
                Port_FileSelectRando_CancelSlot(sMenu.save_slot);
                return;
            default:
                return;
        }
    }

    if (event->type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) {
        switch ((SDL_GamepadButton)event->gbutton.button) {
            case SDL_GAMEPAD_BUTTON_DPAD_UP:
                MoveActive(-1);
                return;
            case SDL_GAMEPAD_BUTTON_DPAD_DOWN:
                MoveActive(1);
                return;
            case SDL_GAMEPAD_BUTTON_DPAD_LEFT:
                if (sMenu.active_element_index == 5) CycleDifficulty(-1);
                else if (sMenu.active_element_index >= 2 && sMenu.active_element_index <= 4) ActivateCurrent();
                return;
            case SDL_GAMEPAD_BUTTON_DPAD_RIGHT:
                if (sMenu.active_element_index == 5) CycleDifficulty(1);
                else if (sMenu.active_element_index >= 2 && sMenu.active_element_index <= 4) ActivateCurrent();
                return;
            case SDL_GAMEPAD_BUTTON_SOUTH:
            case SDL_GAMEPAD_BUTTON_START:
                ActivateCurrent();
                return;
            case SDL_GAMEPAD_BUTTON_EAST:
                Port_RandoFileMenu_Close();
                Port_FileSelectRando_CancelSlot(sMenu.save_slot);
                return;
            default:
                return;
        }
    }
}

bool Port_RandoFileMenu_HandleEvent(const SDL_Event* event) {
    if (!sMenu.open) return false;
    ProcessFileMenuInput((SDL_Event*)event);
    return true;
}

void Port_RandoFileMenu_Render(SDL_Renderer* renderer, int window_width, int window_height) {
    RenderFileMenuUISized(renderer, window_width, window_height);
}

void RenderFileMenuUI(SDL_Renderer* renderer) {
    int w = 640;
    int h = 480;
    if (renderer != NULL) {
        int out_w = 0, out_h = 0;
        if (SDL_GetCurrentRenderOutputSize(renderer, &out_w, &out_h)) {
            w = out_w;
            h = out_h;
        }
    }
    RenderFileMenuUISized(renderer, w, h);
}
