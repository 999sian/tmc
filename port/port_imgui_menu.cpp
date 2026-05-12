/*
 * port_imgui_menu.cpp — Dear ImGui drawing layer for the F8 debug menu.
 *
 * Replaces the SDL_RenderDebugText-based overlay in port_debug_menu.cpp
 * with an ImGui window that looks (and feels) like a proper modern UI:
 * styled panels, hover/selection highlights, real fonts, scrollable lists.
 *
 * Architecture choice — keep the menu *state* (page stack, cursor,
 * action lambdas, label callbacks) in port_debug_menu.cpp untouched, and
 * have this file render *from* that state. Input still flows through
 * Port_DebugMenu_HandleKey so all the existing key bindings (Up/Down,
 * Enter, Left/Right cycle, Esc back, PgUp/PgDn, Home/End) keep working.
 *
 * The ImGui context is owned here. Init/Shutdown are called from
 * port_main.c after SDL is up. The per-frame begin/end pair is called
 * from port_ppu.cpp around the SDL_RenderPresent so the menu draws on
 * top of the rasterized GBA frame.
 *
 * Toggling between ImGui and the legacy SDL-text path: set
 * sPortImGuiEnabled from outside (default on) — when off, this whole TU
 * is a no-op and port_debug_menu.cpp's classic renderer runs instead.
 */

#include <SDL3/SDL.h>
#include <imgui.h>
#include <backends/imgui_impl_sdl3.h>
#include <backends/imgui_impl_sdlrenderer3.h>

#include "port_runtime_config.h"  /* PortInput enum (PORT_INPUT_*) */

#include <cstdio>
#include <cstring>
#include <ctime>
#include <string>
#include <vector>

/* The menu state machine lives in port_debug_menu.cpp. We don't include
 * its header (it doesn't expose the page-stack internals) — instead the
 * legacy file exposes a small accessor API just for us. */
extern "C" {
bool Port_DebugMenu_IsOpen(void);
int  Port_DebugMenu_PageDepth(void);
const char* Port_DebugMenu_PageTitle(int depth);
int  Port_DebugMenu_PageItemCount(int depth);
const char* Port_DebugMenu_PageItemLabel(int depth, int idx);
int  Port_DebugMenu_PageCursor(int depth);
void Port_DebugMenu_PageSetCursor(int depth, int idx);
void Port_DebugMenu_PageActivate(int depth, int idx);   /* Enter on item */
void Port_DebugMenu_PageCycleLeft(int depth, int idx);  /* Left arrow */
void Port_DebugMenu_PageCycleRight(int depth, int idx); /* Right arrow */
const char* Port_DebugMenu_Toast(void);                 /* NULL if expired */
}

static bool sImGuiInited = false;
static bool sImGuiEnabled = true;          /* runtime toggle */
static bool sRibbonEnabled = true;         /* Office-style ribbon at top */
static SDL_Window* sWindow = nullptr;
static SDL_Renderer* sRenderer = nullptr;

extern "C" void Port_ImGui_Init(SDL_Window* window, SDL_Renderer* renderer) {
    if (sImGuiInited) return;
    if (!window || !renderer) return;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;  /* don't write imgui.ini next to binary */
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    /* Gamepad nav so Steam Deck users (and anyone on a controller) can
     * drive the menu without keyboard/mouse. SDL3 backend forwards the
     * connected gamepad's stick + D-pad + A/B as ImGui nav inputs. */
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
    /* Don't capture keyboard from the game — we render to a window
     * that's also receiving game input; let game keys pass through
     * unless an ImGui widget genuinely wants them. */
    io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;

    /* Modern dark style with chunky padding so the UI stays touch- and
     * Steam-Deck-friendly. The Deck's 7" 1280×800 screen is small in
     * physical pixels but high DPI relative to the player's hands; what
     * looks chunky on a desktop monitor reads as comfortably sized on
     * the Deck. Players on a normal monitor still get a clean look. */
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 6.0f;
    style.FrameRounding = 6.0f;
    style.ScrollbarRounding = 6.0f;
    style.TabRounding = 6.0f;
    style.GrabRounding = 6.0f;
    style.WindowPadding = ImVec2(14, 12);
    style.FramePadding = ImVec2(12, 8);          /* bigger touch targets */
    style.ItemSpacing = ImVec2(10, 8);
    style.ScrollbarSize = 18.0f;                 /* finger-draggable */
    style.GrabMinSize = 16.0f;
    /* Bump the global font size 1.4× without re-loading a font atlas.
     * ImGui scales the default ProggyClean upward; the resulting glyphs
     * are crisp enough at native resolution for menu use, and big
     * enough to be readable on the Deck at hand-held distance. */
    io.FontGlobalScale = 1.4f;
    ImVec4* colors = style.Colors;
    colors[ImGuiCol_WindowBg]       = ImVec4(0.07f, 0.08f, 0.10f, 0.94f);
    colors[ImGuiCol_TitleBgActive]  = ImVec4(0.12f, 0.20f, 0.32f, 1.00f);
    colors[ImGuiCol_Header]         = ImVec4(0.20f, 0.30f, 0.50f, 0.50f);
    colors[ImGuiCol_HeaderHovered]  = ImVec4(0.25f, 0.40f, 0.65f, 0.70f);
    colors[ImGuiCol_HeaderActive]   = ImVec4(0.30f, 0.50f, 0.80f, 1.00f);
    colors[ImGuiCol_Button]         = ImVec4(0.18f, 0.22f, 0.28f, 1.00f);
    colors[ImGuiCol_ButtonHovered]  = ImVec4(0.30f, 0.45f, 0.65f, 1.00f);
    colors[ImGuiCol_ButtonActive]   = ImVec4(0.40f, 0.55f, 0.75f, 1.00f);
    colors[ImGuiCol_Text]           = ImVec4(0.92f, 0.92f, 0.92f, 1.00f);
    colors[ImGuiCol_TextDisabled]   = ImVec4(0.55f, 0.55f, 0.55f, 1.00f);

    if (!ImGui_ImplSDL3_InitForSDLRenderer(window, renderer)) {
        fprintf(stderr, "[imgui] ImGui_ImplSDL3 init failed\n");
        ImGui::DestroyContext();
        return;
    }
    if (!ImGui_ImplSDLRenderer3_Init(renderer)) {
        fprintf(stderr, "[imgui] ImGui_ImplSDLRenderer3 init failed\n");
        ImGui_ImplSDL3_Shutdown();
        ImGui::DestroyContext();
        return;
    }

    sWindow = window;
    sRenderer = renderer;
    sImGuiInited = true;
    fprintf(stderr, "[imgui] initialized (v%s)\n", IMGUI_VERSION);
}

extern "C" void Port_ImGui_Shutdown(void) {
    if (!sImGuiInited) return;
    ImGui_ImplSDLRenderer3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    sImGuiInited = false;
}

extern "C" void Port_ImGui_HandleEvent(const SDL_Event* event) {
    if (!sImGuiInited || !sImGuiEnabled) return;
    ImGui_ImplSDL3_ProcessEvent(event);
}

extern "C" bool Port_ImGui_IsEnabled(void) { return sImGuiEnabled; }
extern "C" void Port_ImGui_SetEnabled(bool enabled) { sImGuiEnabled = enabled; }
extern "C" bool Port_ImGui_RibbonEnabled(void) { return sRibbonEnabled; }
extern "C" void Port_ImGui_SetRibbonEnabled(bool enabled) { sRibbonEnabled = enabled; }

/* ------------------------------------------------------------------ */
/*   Externs for the ribbon's direct-action widgets                   */
/* ------------------------------------------------------------------ */
/* The classic menu builders compose action lambdas internally; the
 * ribbon bypasses the page stack and calls these underlying actions
 * directly, exposing each setting as a proper ImGui widget instead of
 * a list row. Same backing functions either way, so behaviour matches. */
extern "C" {
void Port_DebugAction_GiveAllItems(void);
void Port_DebugAction_MaxHearts(void);
void Port_DebugAction_HealFull(void);
void Port_DebugAction_MaxRupees(void);
void Port_DebugAction_MaxShells(void);
void Port_DebugAction_AllKinstones(void);

void          Port_PPU_ToggleFullscreen(void);
bool          Port_PPU_IsFullscreen(void);
void          Port_PPU_CycleWindowScale(int direction);
unsigned char Port_PPU_WindowScale(void);
void          Port_PPU_CyclePresentationMode(int direction);
const char*   Port_PPU_PresentationModeName(void);
void          Port_PPU_CycleFilter(int direction);
const char*   Port_PPU_FilterName(void);
unsigned int  Port_Config_TargetFps(void);
void          Port_Config_CycleTargetFps(int direction);
unsigned char Port_Config_InternalScale(void);
void          Port_Config_CycleInternalScale(int direction);

int  Port_QuickSave_SaveSlot(int slot);
int  Port_QuickSave_LoadSlot(int slot);
int  Port_QuickSave_HasSlot(int slot);
unsigned long long Port_QuickSave_SlotTimestamp(int slot);
int  Port_QuickSave_SlotCount(void);
int  Port_QuickSave_AutoSlotBase(void);
int  Port_QuickSave_AutoEnabled(void);
void Port_QuickSave_SetAutoEnabled(int enabled);
unsigned int Port_QuickSave_AutoIntervalMs(void);
void Port_QuickSave_SetAutoIntervalMs(unsigned int ms);
bool Port_Config_AutosaveEnabled(void);
void Port_Config_SetAutosaveEnabled(bool enabled);
void Port_Config_SetAutosaveIntervalMs(unsigned int ms);

const char* Port_Save_GetActivePath(void);
void        Port_Save_SetActivePath(const char* path);
int         Port_Save_SaveAsProfile(const char* path);
int         Port_Save_ListProfiles(char (*out)[64], int max);
void        Port_Config_SetActiveSaveProfile(const char* path);

const char* Port_SoftSlots_GetSlotLabel(int slot);
void        Port_SoftSlots_CycleAssignment(int slot, int direction);

const char* Port_Config_InputName(int input);
int  Port_Config_BindingCount(int input);
void Port_Config_BindingLabel(int input, int idx, char* out, int cap);
void Port_Config_ClearBindings(int input);
void Port_Config_BeginCaptureBinding(int input);
int  Port_Config_IsCapturingBinding(void);
int  Port_Config_CapturingBindingInput(void);
void Port_Config_CancelCaptureBinding(void);
void Port_Config_ResetAllBindings(void);

int Port_DebugQuery_AreaRoomCount(unsigned char area);
int Port_DebugAction_Warp(unsigned char area, unsigned char room,
                          unsigned short x, unsigned short y,
                          unsigned char layer);
const char* Port_DebugQuery_AreaName(unsigned char area);

void Port_DebugMenu_Toggle(void);
}

/* Mini-toast for ribbon actions so the user sees "Saved" etc. without
 * having to look at stderr. Reuses the legacy Toast() path through the
 * existing public toast accessor. */
extern "C" void Port_DebugMenu_ToastFromExternal(const char* msg);

static void DrawRibbonItemsTab(void) {
    if (ImGui::Button("Unlock all items"))      { Port_DebugAction_GiveAllItems(); Port_DebugMenu_ToastFromExternal("All items granted"); }
    ImGui::SameLine();
    if (ImGui::Button("Max hearts"))            { Port_DebugAction_MaxHearts();    Port_DebugMenu_ToastFromExternal("Hearts maxed"); }
    ImGui::SameLine();
    if (ImGui::Button("Heal"))                  { Port_DebugAction_HealFull();     Port_DebugMenu_ToastFromExternal("Healed"); }
    ImGui::SameLine();
    if (ImGui::Button("999 rupees"))            { Port_DebugAction_MaxRupees();    Port_DebugMenu_ToastFromExternal("999 rupees"); }
    ImGui::SameLine();
    if (ImGui::Button("999 shells"))            { Port_DebugAction_MaxShells();    Port_DebugMenu_ToastFromExternal("999 shells"); }
    ImGui::SameLine();
    if (ImGui::Button("All kinstones fused"))   { Port_DebugAction_AllKinstones(); Port_DebugMenu_ToastFromExternal("All kinstones"); }
}

static void DrawRibbonDisplayTab(void) {
    /* Scale */
    int scale = (int)Port_PPU_WindowScale();
    ImGui::Text("Window scale"); ImGui::SameLine(140);
    if (ImGui::Button("<##scale")) Port_PPU_CycleWindowScale(-1);
    ImGui::SameLine(); ImGui::Text("%dx", scale); ImGui::SameLine();
    if (ImGui::Button(">##scale")) Port_PPU_CycleWindowScale(+1);

    /* Filter */
    ImGui::Text("Filter"); ImGui::SameLine(140);
    if (ImGui::Button("<##filter")) Port_PPU_CyclePresentationMode(-1);
    ImGui::SameLine(); ImGui::Text("%s", Port_PPU_PresentationModeName()); ImGui::SameLine();
    if (ImGui::Button(">##filter")) Port_PPU_CyclePresentationMode(+1);

    /* FPS */
    ImGui::Text("Target FPS"); ImGui::SameLine(140);
    if (ImGui::Button("<##fps")) Port_Config_CycleTargetFps(-1);
    ImGui::SameLine();
    unsigned fps = Port_Config_TargetFps();
    if (fps == 0) ImGui::Text("uncapped");
    else          ImGui::Text("%u", fps);
    ImGui::SameLine();
    if (ImGui::Button(">##fps")) Port_Config_CycleTargetFps(+1);

    /* Internal scale */
    ImGui::Text("Internal"); ImGui::SameLine(140);
    if (ImGui::Button("<##iscale")) Port_Config_CycleInternalScale(-1);
    ImGui::SameLine();
    ImGui::Text("%ux", (unsigned)Port_Config_InternalScale());
    ImGui::SameLine();
    if (ImGui::Button(">##iscale")) Port_Config_CycleInternalScale(+1);

    /* Fullscreen */
    bool fs = Port_PPU_IsFullscreen();
    if (ImGui::Checkbox("Fullscreen", &fs)) Port_PPU_ToggleFullscreen();

    /* CRT filter */
    ImGui::Text("CRT filter"); ImGui::SameLine(140);
    if (ImGui::Button("<##crt")) Port_PPU_CycleFilter(-1);
    ImGui::SameLine(); ImGui::Text("%s", Port_PPU_FilterName()); ImGui::SameLine();
    if (ImGui::Button(">##crt")) Port_PPU_CycleFilter(+1);
}

static void DrawRibbonSavesTab(void) {
    /* Auto-save controls at the top. */
    bool autoOn = Port_QuickSave_AutoEnabled();
    if (ImGui::Checkbox("Auto-save", &autoOn)) {
        Port_QuickSave_SetAutoEnabled(autoOn ? 1 : 0);
        Port_Config_SetAutosaveEnabled(autoOn);
    }
    ImGui::SameLine(180);
    int sec = (int)(Port_QuickSave_AutoIntervalMs() / 1000u);
    if (ImGui::SliderInt("Interval (s)", &sec, 5, 600)) {
        Port_QuickSave_SetAutoIntervalMs((unsigned)sec * 1000u);
        Port_Config_SetAutosaveIntervalMs((unsigned)sec * 1000u);
    }
    ImGui::Separator();

    /* Slot grid: each slot is one row with Save / Load buttons + timestamp. */
    const int n = Port_QuickSave_SlotCount();
    const int autoBase = Port_QuickSave_AutoSlotBase();
    for (int s = 0; s < n; ++s) {
        ImGui::PushID(s);
        const char* tag;
        char tagbuf[16];
        if (s == 0) tag = "Quick";
        else if (s < autoBase) { std::snprintf(tagbuf, sizeof(tagbuf), "Slot %d", s); tag = tagbuf; }
        else { std::snprintf(tagbuf, sizeof(tagbuf), "Auto %d", s - autoBase + 1); tag = tagbuf; }
        ImGui::Text("%-7s", tag); ImGui::SameLine(80);

        if (ImGui::Button("Save")) {
            if (Port_QuickSave_SaveSlot(s)) Port_DebugMenu_ToastFromExternal("Saved");
        }
        ImGui::SameLine();
        if (Port_QuickSave_HasSlot(s)) {
            if (ImGui::Button("Load")) {
                if (Port_QuickSave_LoadSlot(s)) Port_DebugMenu_ToastFromExternal("Loaded");
            }
        } else {
            ImGui::BeginDisabled(); ImGui::Button("Load"); ImGui::EndDisabled();
        }
        ImGui::SameLine();
        unsigned long long ts = Port_QuickSave_SlotTimestamp(s);
        if (ts == 0) {
            ImGui::TextDisabled("(empty)");
        } else {
            time_t tt = (time_t)ts;
            struct tm tm_buf;
#ifdef _WIN32
            localtime_s(&tm_buf, &tt);
#else
            localtime_r(&tt, &tm_buf);
#endif
            char timestr[32];
            std::strftime(timestr, sizeof(timestr), "%Y-%m-%d %H:%M:%S", &tm_buf);
            ImGui::TextDisabled("%s", timestr);
        }
        ImGui::PopID();
    }
}

static void DrawRibbonProfilesTab(void) {
    char names[32][64];
    const int n = Port_Save_ListProfiles(names, 32);
    const std::string activeNow = Port_Save_GetActivePath();

    ImGui::Text("Active profile: %s", activeNow.c_str());
    ImGui::Separator();

    for (int i = 0; i < n; ++i) {
        ImGui::PushID(i);
        bool isActive = (std::string(names[i]) == activeNow);
        if (isActive) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.94f, 0.25f, 1.0f));
            ImGui::Bullet();
            ImGui::SameLine();
            ImGui::Text("%s (active)", names[i]);
            ImGui::PopStyleColor();
        } else {
            ImGui::Text(" %s", names[i]);
        }
        ImGui::SameLine(280);
        if (!isActive) {
            if (ImGui::Button("Activate")) {
                Port_Save_SetActivePath(names[i]);
                Port_Config_SetActiveSaveProfile(names[i]);
                Port_DebugMenu_ToastFromExternal("Profile activated — go to title to load");
            }
        }
        ImGui::PopID();
    }

    ImGui::Separator();
    if (ImGui::Button("+ Save current as new profile")) {
        char name[64];
        int k = 1;
        for (; k <= 99; ++k) {
            std::snprintf(name, sizeof(name), "tmc_%d.sav", k);
            FILE* probe = std::fopen(name, "rb");
            if (!probe) break;
            std::fclose(probe);
        }
        if (k > 99) Port_DebugMenu_ToastFromExternal("No free profile slots (1-99)");
        else if (Port_Save_SaveAsProfile(name)) {
            char msg[96];
            std::snprintf(msg, sizeof(msg), "Saved current as %s", name);
            Port_DebugMenu_ToastFromExternal(msg);
        } else {
            Port_DebugMenu_ToastFromExternal("Save failed");
        }
    }
}

/* Friendly display name for each action — matches the GBA button names
 * users actually think in. The Port_Config side stores them as
 * short ids ("a", "b", "soft_l2") for config.json compactness. */
static const char* InputLabel(int input) {
    switch (input) {
        case PORT_INPUT_A:       return "A button (action)";
        case PORT_INPUT_B:       return "B button (sword)";
        case PORT_INPUT_SELECT:  return "Select";
        case PORT_INPUT_START:   return "Start (pause)";
        case PORT_INPUT_RIGHT:   return "D-pad Right";
        case PORT_INPUT_LEFT:    return "D-pad Left";
        case PORT_INPUT_UP:      return "D-pad Up";
        case PORT_INPUT_DOWN:    return "D-pad Down";
        case PORT_INPUT_R:       return "R (item slot 2)";
        case PORT_INPUT_L:       return "L (item slot 1)";
        case PORT_INPUT_SOFT_X:  return "Soft slot X";
        case PORT_INPUT_SOFT_Y:  return "Soft slot Y";
        case PORT_INPUT_SOFT_L2: return "Soft slot L2";
        case PORT_INPUT_SOFT_R2: return "Soft slot R2";
        default:                 return Port_Config_InputName(input);
    }
}

static void DrawRibbonControlsTab(void) {
    ImGui::TextWrapped("Click 'Set' next to an action, then press the new key or controller button. "
                       "Esc cancels. Mappings save to config.json automatically.");
    ImGui::Separator();

    /* Two-column-ish table: action label | bindings + buttons. */
    if (ImGui::BeginTable("##controls", 3,
                          ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_RowBg)) {
        ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthFixed, 200.0f);
        ImGui::TableSetupColumn("Bindings", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("##actions", ImGuiTableColumnFlags_WidthFixed, 180.0f);

        for (int i = 0; i < PORT_INPUT_COUNT; ++i) {
            ImGui::PushID(i);
            ImGui::TableNextRow();

            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(InputLabel(i));

            ImGui::TableSetColumnIndex(1);
            const int n = Port_Config_BindingCount(i);
            if (n == 0) {
                ImGui::TextDisabled("(unbound)");
            } else {
                for (int b = 0; b < n; ++b) {
                    char label[64];
                    Port_Config_BindingLabel(i, b, label, sizeof(label));
                    if (b > 0) ImGui::SameLine(0, 6);
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.30f, 0.45f, 1.0f));
                    ImGui::Button(label);
                    ImGui::PopStyleColor();
                }
            }

            ImGui::TableSetColumnIndex(2);
            if (ImGui::Button("Set")) {
                Port_Config_BeginCaptureBinding(i);
                ImGui::OpenPopup("Capture binding");
            }
            ImGui::SameLine();
            if (ImGui::Button("Clear")) {
                Port_Config_ClearBindings(i);
            }

            /* Modal popup that hangs around until the capture path
             * commits (which clears IsCapturingBinding). We render the
             * popup per-row but OpenPopup is fine because only one is
             * open at a time. */
            ImVec2 center = ImGui::GetMainViewport()->GetCenter();
            ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
            if (ImGui::BeginPopupModal("Capture binding", nullptr,
                                       ImGuiWindowFlags_AlwaysAutoResize |
                                       ImGuiWindowFlags_NoMove)) {
                ImGui::Text("Press a key or controller button for:");
                ImGui::TextColored(ImVec4(1, 0.94f, 0.25f, 1), "%s", InputLabel(i));
                ImGui::Text("Esc cancels.");
                ImGui::Separator();
                if (ImGui::Button("Cancel") ||
                    !Port_Config_IsCapturingBinding()) {
                    Port_Config_CancelCaptureBinding();
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndPopup();
            }

            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    ImGui::Separator();
    ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.55f, 0.30f, 0.20f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.75f, 0.40f, 0.30f, 1.0f));
    if (ImGui::Button("Reset all to defaults")) {
        Port_Config_ResetAllBindings();
        Port_DebugMenu_ToastFromExternal("Bindings reset");
    }
    ImGui::PopStyleColor(2);
}

static void DrawRibbonEquipTab(void) {
    for (int s = 0; s < 4; ++s) {
        ImGui::PushID(s);
        ImGui::Text("%s", Port_SoftSlots_GetSlotLabel(s)); ImGui::SameLine(280);
        if (ImGui::Button("<")) Port_SoftSlots_CycleAssignment(s, -1);
        ImGui::SameLine();
        if (ImGui::Button(">")) Port_SoftSlots_CycleAssignment(s, +1);
        ImGui::PopID();
    }
}

extern "C" int Port_DebugQuery_RoomDimensions(unsigned char area, unsigned char room,
                                              unsigned short* w, unsigned short* h);

static char sWarpFilter[64] = "";

/* Override lookup from port_debug_actions.c — returns 1 + fills x/y/layer
 * when (area, room) has a curated safe-spawn entry, else 0. Used by the
 * Warp tab so high-traffic rooms whose geometric center is a wall (boss
 * arenas, dungeon entrances, town buildings) drop Link on walkable
 * ground instead of an obstacle. See issue #94. */
extern "C" int Port_DebugAction_WarpSpawnOverride(unsigned char area, unsigned char room,
                                                  unsigned short* x, unsigned short* y,
                                                  unsigned char* layer);

static void DrawRibbonWarpTab(void) {
    /* Filter bar — type to narrow the area list. Empty filter = show
     * everything. Case-insensitive substring match. Steam Deck users
     * can ignore the filter and just scroll. */
    ImGui::SetNextItemWidth(280);
    ImGui::InputTextWithHint("##warpFilter", "filter by area name (e.g. 'castle')",
                             sWarpFilter, sizeof(sWarpFilter));
    ImGui::SameLine();
    if (ImGui::Button("Clear")) sWarpFilter[0] = '\0';
    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();
    ImGui::TextDisabled("L/R bumpers = page jump");

    /* Letter strip — issue #76. The area list is long (>140 entries)
     * and previously the only way to traverse was one-line-at-a-time
     * scrolling. Buttons here filter to areas whose name starts with
     * that letter, giving instant A-Z jumps. */
    static char sLetterFilter = 0; /* 0 = no letter filter */
    {
        const char* kLetters = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
        if (ImGui::SmallButton("All##warpLet")) sLetterFilter = 0;
        for (const char* p = kLetters; *p; ++p) {
            ImGui::SameLine();
            char id[6];
            std::snprintf(id, sizeof(id), "%c##wl", *p);
            if (sLetterFilter == *p) {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.30f, 0.55f, 0.30f, 1.0f));
                if (ImGui::SmallButton(id)) sLetterFilter = 0;
                ImGui::PopStyleColor();
            } else {
                if (ImGui::SmallButton(id)) sLetterFilter = *p;
            }
        }
    }

    /* Scrollable area list. Each area is a collapsible header that
     * reveals its rooms as a button grid when opened. Headers are
     * controller-navigable (D-pad up/down) and the inner buttons take
     * keyboard / gamepad nav too. */
    ImGui::Separator();
    const float listH = ImGui::GetTextLineHeightWithSpacing() * 18.0f;
    if (ImGui::BeginChild("##warpList", ImVec2(0, listH), ImGuiChildFlags_NavFlattened,
                          0)) {
        /* L1/R1 bumpers = page jump while this child is focused/hovered.
         * PgUp / PgDn keys give the same shortcut on keyboard. Home /
         * End jump to the ends of the list. Issue #76. */
        const bool listFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) ||
                                 ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows);
        if (listFocused) {
            const float pageStep = listH * 0.9f;
            if (ImGui::IsKeyPressed(ImGuiKey_GamepadL1, false) ||
                ImGui::IsKeyPressed(ImGuiKey_PageUp,   false)) {
                ImGui::SetScrollY(ImGui::GetScrollY() - pageStep);
            }
            if (ImGui::IsKeyPressed(ImGuiKey_GamepadR1, false) ||
                ImGui::IsKeyPressed(ImGuiKey_PageDown, false)) {
                ImGui::SetScrollY(ImGui::GetScrollY() + pageStep);
            }
            if (ImGui::IsKeyPressed(ImGuiKey_Home, false)) {
                ImGui::SetScrollY(0.0f);
            }
            if (ImGui::IsKeyPressed(ImGuiKey_End, false)) {
                ImGui::SetScrollY(ImGui::GetScrollMaxY());
            }
        }
        const std::string filter = sWarpFilter[0] ? std::string(sWarpFilter) : std::string();
        std::string filter_lower = filter;
        for (auto& c : filter_lower) c = (char)std::tolower((unsigned char)c);

        int shown = 0;
        for (unsigned int area = 0; area < 0x90; ++area) {
            unsigned char a = (unsigned char)area;
            int roomCount = Port_DebugQuery_AreaRoomCount(a);
            if (roomCount <= 0) continue;

            const char* name = Port_DebugQuery_AreaName(a);
            char header[96];
            if (name) std::snprintf(header, sizeof(header), "0x%02X  %s  (%d rooms)", area, name, roomCount);
            else      std::snprintf(header, sizeof(header), "0x%02X  Area  (%d rooms)", area, roomCount);

            if (!filter_lower.empty()) {
                std::string hl(header);
                for (auto& c : hl) c = (char)std::tolower((unsigned char)c);
                if (hl.find(filter_lower) == std::string::npos) continue;
            }
            /* Letter strip — match against the first letter of the
             * area name itself (skip the "0xNN  " prefix). */
            if (sLetterFilter && name) {
                char first = name[0];
                if (first >= 'a' && first <= 'z') first = (char)(first - 'a' + 'A');
                if (first != sLetterFilter) continue;
            }
            shown++;

            ImGui::PushID((int)area);
            if (ImGui::CollapsingHeader(header)) {
                ImGui::Indent();
                /* Quick-warp button for the area's first room. Uses the
                 * curated safe-spawn override when one exists. */
                if (ImGui::Button("Warp here (room 0)")) {
                    unsigned short sx = 0x80, sy = 0x80;
                    unsigned char  slayer = 0;
                    if (!Port_DebugAction_WarpSpawnOverride(a, 0, &sx, &sy, &slayer)) {
                        unsigned short w0 = 0, h0 = 0;
                        if (Port_DebugQuery_RoomDimensions(a, 0, &w0, &h0)) {
                            sx = w0 ? (unsigned short)(w0 / 2) : 0x80;
                            sy = h0 ? (unsigned short)(h0 / 2) : 0x80;
                        }
                        slayer = 1;
                    }
                    if (Port_DebugAction_Warp(a, 0, sx, sy, slayer)) {
                        char msg[96];
                        std::snprintf(msg, sizeof(msg), "Warp -> 0x%02X room 0", area);
                        Port_DebugMenu_ToastFromExternal(msg);
                    } else {
                        Port_DebugMenu_ToastFromExternal("Warp ignored: not in gameplay");
                    }
                }
                /* Per-room buttons in a 3-column grid for compactness. */
                int col = 0;
                for (int r = 0; r < roomCount; ++r) {
                    unsigned short w = 0, h = 0;
                    if (!Port_DebugQuery_RoomDimensions(a, (unsigned char)r, &w, &h)) continue;
                    char roomLabel[48];
                    std::snprintf(roomLabel, sizeof(roomLabel), "Room 0x%02X", r);
                    ImGui::PushID(r);
                    if (ImGui::Button(roomLabel, ImVec2(120, 0))) {
                        unsigned short cx = 0, cy = 0;
                        unsigned char  layer = 1;
                        if (!Port_DebugAction_WarpSpawnOverride(a, (unsigned char)r,
                                                                &cx, &cy, &layer)) {
                            cx = w ? (unsigned short)(w / 2) : 0x80;
                            cy = h ? (unsigned short)(h / 2) : 0x80;
                            layer = 1;
                        }
                        if (Port_DebugAction_Warp(a, (unsigned char)r, cx, cy, layer)) {
                            char msg[96];
                            std::snprintf(msg, sizeof(msg), "Warp -> 0x%02X room 0x%02X", area, r);
                            Port_DebugMenu_ToastFromExternal(msg);
                        } else {
                            Port_DebugMenu_ToastFromExternal("Warp ignored: not in gameplay");
                        }
                    }
                    ImGui::PopID();
                    if ((col % 3) != 2) ImGui::SameLine();
                    col++;
                }
                ImGui::Unindent();
            }
            ImGui::PopID();
        }
        if (shown == 0) {
            ImGui::TextDisabled("No areas match the filter.");
        }
    }
    ImGui::EndChild();
}

static void DrawRibbon(void) {
    ImGuiIO& io = ImGui::GetIO();
    const float ribbonW = io.DisplaySize.x;
    ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(ribbonW, 0), ImGuiCond_Always);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    if (ImGui::Begin("##ribbon", nullptr,
                     ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                     ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse)) {
        /* Close button anchored to the top-right of the ribbon. The
         * persistent corner trigger sits behind the ribbon when it's
         * open, so without this button users on mouse-only or who
         * forgot the F8/Select+Start hotkey have no way out. Render
         * it BEFORE the tab bar so it sits at the very top edge. */
        const float closeW = 80.0f;
        ImGui::SameLine(ImGui::GetWindowWidth() - closeW - 12.0f);
        ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.55f, 0.20f, 0.20f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.75f, 0.30f, 0.30f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.85f, 0.35f, 0.35f, 1.0f));
        if (ImGui::Button("Close X", ImVec2(closeW, 0))) {
            Port_DebugMenu_Toggle();
        }
        ImGui::PopStyleColor(3);

        if (ImGui::BeginTabBar("##ribbonTabs", ImGuiTabBarFlags_None)) {
            if (ImGui::BeginTabItem("Items"))    { DrawRibbonItemsTab();    ImGui::EndTabItem(); }
            if (ImGui::BeginTabItem("Display"))  { DrawRibbonDisplayTab();  ImGui::EndTabItem(); }
            if (ImGui::BeginTabItem("Saves"))    { DrawRibbonSavesTab();    ImGui::EndTabItem(); }
            if (ImGui::BeginTabItem("Profiles")) { DrawRibbonProfilesTab(); ImGui::EndTabItem(); }
            if (ImGui::BeginTabItem("Equip"))    { DrawRibbonEquipTab();    ImGui::EndTabItem(); }
            if (ImGui::BeginTabItem("Controls")) { DrawRibbonControlsTab(); ImGui::EndTabItem(); }
            if (ImGui::BeginTabItem("Warp"))     { DrawRibbonWarpTab();     ImGui::EndTabItem(); }
            ImGui::EndTabBar();
        }
        /* Footer with the mode toggle + hotkey hint. */
        ImGui::Separator();
        bool useRibbon = sRibbonEnabled;
        if (ImGui::Checkbox("Ribbon mode (uncheck for classic menu)", &useRibbon)) {
            sRibbonEnabled = useRibbon;
        }
        ImGui::SameLine();
        ImGui::TextDisabled("(F8 or Select+Start also toggles)");
    }
    ImGui::End();
    ImGui::PopStyleVar();
}

/* Layout helpers — keep all the styling decisions in one place so it's
 * easy to tweak the look without hunting through draw code. */
static void DrawToast(const char* text) {
    if (!text || !*text) return;
    ImGuiIO& io = ImGui::GetIO();
    ImVec2 vpSize = io.DisplaySize;
    const float pad = 12.0f;
    ImGui::SetNextWindowBgAlpha(0.85f);
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.94f, 0.25f, 1.0f));
    ImGui::SetNextWindowPos(ImVec2(vpSize.x * 0.5f, vpSize.y - pad - 24.0f),
                            ImGuiCond_Always, ImVec2(0.5f, 0.0f));
    if (ImGui::Begin("##toast", nullptr,
                     ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize |
                     ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoInputs)) {
        ImGui::TextUnformatted(text);
    }
    ImGui::End();
    ImGui::PopStyleColor();
}

static void DrawMenuPage(int depth) {
    const char* title = Port_DebugMenu_PageTitle(depth);
    const int count = Port_DebugMenu_PageItemCount(depth);
    const int cursor = Port_DebugMenu_PageCursor(depth);
    if (!title || count <= 0) return;

    ImGuiIO& io = ImGui::GetIO();
    const float panelW = 460.0f;
    const float maxH = io.DisplaySize.y * 0.85f;
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f),
                            ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(panelW, 0), ImGuiCond_Always);
    ImGui::SetNextWindowSizeConstraints(ImVec2(panelW, 0), ImVec2(panelW, maxH));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowMinSize, ImVec2(panelW, 0));
    if (ImGui::Begin(title, nullptr,
                     ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize)) {
        if (ImGui::BeginChild("##items", ImVec2(0, ImGui::GetTextLineHeightWithSpacing() * 22.0f),
                              false, ImGuiWindowFlags_None)) {
            for (int i = 0; i < count; ++i) {
                const char* label = Port_DebugMenu_PageItemLabel(depth, i);
                if (!label) continue;
                bool selected = i == cursor;

                /* Render as a Selectable so it gets a hover background.
                 * Spans available width so the hover hit-box reaches the
                 * right edge of the panel. */
                ImGui::PushID(i);
                if (selected) {
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.94f, 0.25f, 1.0f));
                }
                if (ImGui::Selectable(label, selected, ImGuiSelectableFlags_AllowDoubleClick)) {
                    Port_DebugMenu_PageSetCursor(depth, i);
                    if (ImGui::IsMouseDoubleClicked(0)) {
                        Port_DebugMenu_PageActivate(depth, i);
                    }
                }
                /* Right-click → cycle right (shortcut for value items). */
                if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(1)) {
                    Port_DebugMenu_PageSetCursor(depth, i);
                    Port_DebugMenu_PageCycleRight(depth, i);
                }
                if (selected) {
                    ImGui::PopStyleColor();
                    /* Keep the cursor row visible when keyboard nav
                     * scrolls past the edge of the child window. */
                    if (ImGui::GetScrollMaxY() > 0.0f) {
                        ImGui::SetScrollHereY(0.5f);
                    }
                }
                ImGui::PopID();
            }
        }
        ImGui::EndChild();

        ImGui::Separator();
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.6f, 0.6f, 1.0f));
        ImGui::TextUnformatted("Up/Dn move  Enter activate  L/R cycle  Esc back");
        ImGui::TextUnformatted("Double-click activate  Right-click cycle");
        ImGui::PopStyleColor();
    }
    ImGui::End();
    ImGui::PopStyleVar();
}

/* Persistent click target so users on mouse/touch can open the menu
 * without the F8 hotkey. When the menu is closed we render the
 * smallest, faintest possible affordance — a single "≡" glyph in the
 * top-right — so gameplay isn't covered. The window auto-opacifies on
 * hover. When the menu IS open, the same widget switches to a clear
 * "CLOSE" label since at that point the menu UI already obscures the
 * background, so visibility is fine. */
static void DrawMenuTrigger(void) {
    ImGuiIO& io = ImGui::GetIO();
    const bool open = Port_DebugMenu_IsOpen();
    const float pad = 6.0f;
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - pad, pad),
                            ImGuiCond_Always, ImVec2(1.0f, 0.0f));

    /* Closed: 12% alpha background, ~minimal padding, single-glyph
     * label — so the trigger reads as a faint corner dot rather than
     * an opaque UI element overlapping the player's eye-line. The
     * frame highlights on hover (ImGui handles that for us). */
    if (open) {
        ImGui::SetNextWindowBgAlpha(0.85f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6, 4));
    } else {
        ImGui::SetNextWindowBgAlpha(0.12f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(2, 2));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4, 2));
        /* Make the closed-state button itself low-alpha too; ImGui's
         * hover state will bump it on its own when the cursor lands. */
        ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.18f, 0.22f, 0.28f, 0.30f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.30f, 0.45f, 0.65f, 1.00f));
        ImGui::PushStyleColor(ImGuiCol_Text,          ImVec4(0.92f, 0.92f, 0.92f, 0.50f));
    }

    /* NoNavInputs + NoNavFocus keep gamepad/keyboard nav from ever
     * targeting this button, so A on the controller can't accidentally
     * open the menu during gameplay. Mouse/touch click still works. */
    if (ImGui::Begin("##menu_trigger", nullptr,
                     ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                     ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoFocusOnAppearing |
                     ImGuiWindowFlags_NoNavInputs | ImGuiWindowFlags_NoNavFocus)) {
        /* Closed: single triple-bar ASCII '=' stacked into a hamburger
         * shape (the default ImGui font doesn't ship U+2261 ≡). Open:
         * spelled-out label so the close target reads clearly. */
        const char* label = open ? " CLOSE MENU " : "[=]";
        if (ImGui::Button(label)) {
            Port_DebugMenu_Toggle();
        }
        if (open) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.6f, 0.6f, 1.0f));
            ImGui::TextUnformatted("Gamepad: D-pad nav   A activate   B back");
            ImGui::PopStyleColor();
        }
    }
    ImGui::End();
    if (open) {
        ImGui::PopStyleVar();
    } else {
        ImGui::PopStyleColor(3);
        ImGui::PopStyleVar(2);
    }
}

extern "C" bool Port_ImGui_Render(void) {
    if (!sImGuiInited || !sImGuiEnabled) return false;
    if (!sRenderer) return false;

    /* Gamepad nav gated on menu-open state. When the menu is closed,
     * ImGui must NOT consume gamepad input — otherwise the focus-by-
     * default behaviour grabs the persistent MENU trigger and the
     * player's A press opens the menu instead of attacking. Toggle the
     * flag each frame so transitions are immediate. */
    {
        ImGuiIO& io = ImGui::GetIO();
        if (Port_DebugMenu_IsOpen()) {
            io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
        } else {
            io.ConfigFlags &= ~ImGuiConfigFlags_NavEnableGamepad;
        }
    }

    ImGui_ImplSDLRenderer3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();

    /* Toast survives the menu being closed (e.g. after a warp). */
    DrawToast(Port_DebugMenu_Toast());

    /* Always show the click-to-open trigger so mouse/touch users have a
     * way in without the F8 hotkey. */
    DrawMenuTrigger();

    if (Port_DebugMenu_IsOpen()) {
        if (sRibbonEnabled) {
            DrawRibbon();
        } else {
            /* Render the deepest page only (legacy behaviour: submenu
             * hides its parent). */
            int depth = Port_DebugMenu_PageDepth() - 1;
            if (depth >= 0) {
                DrawMenuPage(depth);
            }
        }
    }

    ImGui::Render();
    ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), sRenderer);
    return true;
}
