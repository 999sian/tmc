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

/* .glslp runtime hooks (port_glslp_runtime.cpp). File-scope so the F8
 * preset-picker lambda below can call them through C linkage. */
extern "C" int  Port_GlslpRuntime_Load(const char*);
extern "C" void Port_GlslpRuntime_Unload(void);
extern "C" int  Port_GlslpRuntime_IsActive(void);
#include <backends/imgui_impl_sdl3.h>
#include <backends/imgui_impl_sdlrenderer3.h>
#ifdef TMC_GPU_RENDERER
#include <SDL3/SDL_gpu.h>
#include <backends/imgui_impl_sdlgpu3.h>
#endif

#include "port_runtime_config.h"  /* PortInput enum (PORT_INPUT_*) */
#include "port_randomizer.h"
#include "port_reborn.h"
#include "port_discord_rpc.h"     /* Port_DiscordRpc_IsEnabled / SetEnabled */
#include "rando/rando.h"

#include <cstdio>
#include <cstring>
#include <ctime>
#include <algorithm>
#include <filesystem>
#include <string>
#include <system_error>
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
    if (!window) return;
    /* On GPU builds renderer is intentionally NULL — Port_PPU_Init passes
     * null when the SDL_GPU pipeline owns the swapchain. The GPU branch
     * below handles that case; the SDL_Renderer branch still requires
     * a non-null renderer. */
#ifndef TMC_GPU_RENDERER
    if (!renderer) return;
#endif

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

#ifdef TMC_GPU_RENDERER
    /* GPU path: renderer arg is NULL (Port_PPU_Init passed null when the
     * SDL_GPU pipeline owns the window). Initialise the SDL_GPU ImGui
     * backend instead — its NewFrame/PrepareDrawData/RenderDrawData
     * trio integrates with our existing SDL_GPU PresentFrame. */
    if (renderer == nullptr) {
        extern SDL_GPUDevice* Port_GPU_GetDevice(void);
        extern SDL_GPUTextureFormat Port_GPU_GetSwapchainFormat(void);
        SDL_GPUDevice* dev = Port_GPU_GetDevice();
        SDL_GPUTextureFormat fmt = Port_GPU_GetSwapchainFormat();
        if (!dev || fmt == SDL_GPU_TEXTUREFORMAT_INVALID) {
            fprintf(stderr, "[imgui] GPU device/format unavailable — F8 menu disabled\n");
            ImGui::DestroyContext();
            return;
        }
        if (!ImGui_ImplSDL3_InitForSDLGPU(window)) {
            fprintf(stderr, "[imgui] ImGui_ImplSDL3_InitForSDLGPU failed\n");
            ImGui::DestroyContext();
            return;
        }
        ImGui_ImplSDLGPU3_InitInfo info = {};
        info.Device            = dev;
        info.ColorTargetFormat = fmt;
        info.MSAASamples       = SDL_GPU_SAMPLECOUNT_1;
        if (!ImGui_ImplSDLGPU3_Init(&info)) {
            fprintf(stderr, "[imgui] ImGui_ImplSDLGPU3_Init failed\n");
            ImGui_ImplSDL3_Shutdown();
            ImGui::DestroyContext();
            return;
        }
        sWindow = window;
        sRenderer = nullptr;  /* GPU backend signals "no SDL_Renderer" */
        sImGuiInited = true;
        fprintf(stderr, "[imgui] initialized (v%s, SDL_GPU backend)\n", IMGUI_VERSION);
        return;
    }
#endif

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
    fprintf(stderr, "[imgui] initialized (v%s, SDL_Renderer backend)\n", IMGUI_VERSION);
}

extern "C" void Port_ImGui_Shutdown(void) {
    if (!sImGuiInited) return;
#ifdef TMC_GPU_RENDERER
    if (!sRenderer) {
        ImGui_ImplSDLGPU3_Shutdown();
    } else
#endif
    {
        ImGui_ImplSDLRenderer3_Shutdown();
    }
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
void          Port_PPU_SetVSync(bool enabled);
bool          Port_PPU_VSyncEnabled(void);
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
int         Port_Save_DeleteProfile(const char* path);
int         Port_Save_RenameProfile(const char* oldPath, const char* newPath);
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

    /* VSync — toggle SDL3 swap-on-vblank. ON by default. Issue #118. */
    bool vsync = Port_PPU_VSyncEnabled();
    if (ImGui::Checkbox("VSync", &vsync)) Port_PPU_SetVSync(vsync);

    /* CRT filter */
    ImGui::Text("CRT filter"); ImGui::SameLine(140);
    if (ImGui::Button("<##crt")) Port_PPU_CycleFilter(-1);
    ImGui::SameLine(); ImGui::Text("%s", Port_PPU_FilterName()); ImGui::SameLine();
    if (ImGui::Button(">##crt")) Port_PPU_CycleFilter(+1);

    /* Shader preset (libretro .glslp) — Step 7 picker. Scans the
     * working directory's ./shaders/ subdirectory for *.glslp files;
     * selecting one tears down any active preset and loads the new
     * one through the .glslp runtime. "Off" returns to the stock GPU
     * filter pipeline above. The list is cached and refreshes on
     * the Scan button or after a successful Load. */
    {

        static std::vector<std::string> sPresetPaths;
        static std::string              sActivePreset;
        static bool                     sScannedOnce = false;

        auto refresh = [&] {
            sPresetPaths.clear();
            std::error_code ec;
            for (const auto& root : {"shaders", "glslp", "."}) {
                if (!std::filesystem::is_directory(root, ec)) continue;
                for (auto& entry : std::filesystem::recursive_directory_iterator(root, ec)) {
                    if (entry.is_regular_file(ec) && entry.path().extension() == ".glslp") {
                        sPresetPaths.push_back(entry.path().string());
                    }
                }
            }
            std::sort(sPresetPaths.begin(), sPresetPaths.end());
        };
        if (!sScannedOnce) { refresh(); sScannedOnce = true; }

        ImGui::Text("Shader preset"); ImGui::SameLine(140);
        if (ImGui::Button("Off##preset")) {
            Port_GlslpRuntime_Unload();
            sActivePreset.clear();
            Port_DebugMenu_ToastFromExternal("Shader preset off");
        }
        ImGui::SameLine();
        if (ImGui::Button("Rescan##preset")) refresh();
        ImGui::SameLine();
        ImGui::TextDisabled("(%zu found)", sPresetPaths.size());

        const bool active = Port_GlslpRuntime_IsActive() != 0;
        if (active && !sActivePreset.empty()) {
            ImGui::TextDisabled("  active: %s", sActivePreset.c_str());
        }

        /* Vertical list of presets — clickable. Capped at 8 visible
         * before scrolling so the F8 ribbon stays compact on small
         * windows. */
        if (!sPresetPaths.empty()) {
            ImGui::Indent(140);
            if (ImGui::BeginListBox("##preset_list",
                    ImVec2(420, ImGui::GetTextLineHeightWithSpacing() * 8))) {
                for (size_t i = 0; i < sPresetPaths.size(); ++i) {
                    bool selected = (sPresetPaths[i] == sActivePreset);
                    /* Short display name — strip the leading directory
                     * components so the user sees the .glslp filename. */
                    std::string label = std::filesystem::path(sPresetPaths[i]).filename().string();
                    label += "    "; label += sPresetPaths[i];
                    if (ImGui::Selectable(label.c_str(), selected)) {
                        if (Port_GlslpRuntime_Load(sPresetPaths[i].c_str())) {
                            sActivePreset = sPresetPaths[i];
                            Port_DebugMenu_ToastFromExternal("Preset loaded");
                        } else {
                            Port_DebugMenu_ToastFromExternal("Preset load FAILED — see stderr");
                        }
                    }
                }
                ImGui::EndListBox();
            }
            ImGui::Unindent(140);
        }
    }

    /* Aspect-ratio mode — picks the stage (visible area) that the GBA
     * 3:2 frame sits inside. Wider modes add pillar bars around the
     * frame; their fill is controlled by the next widget. */
    {
        ImGui::Text("Aspect ratio"); ImGui::SameLine(140);
        if (ImGui::Button("<##aspect")) Port_Config_CycleAspectMode(-1);
        ImGui::SameLine();
        ImGui::Text("%s", Port_Config_AspectModeName(Port_Config_AspectMode()));
        ImGui::SameLine();
        if (ImGui::Button(">##aspect")) Port_Config_CycleAspectMode(+1);
    }

    /* Background-fill style for the pillar bars added by the aspect
     * mode. "Blurred" uses a linearly-filtered stretched copy of the
     * current frame for an "ambient mode" halo. */
    {
        ImGui::Text("Background"); ImGui::SameLine(140);
        if (ImGui::Button("<##bgfill")) Port_Config_CycleBgFill(-1);
        ImGui::SameLine();
        ImGui::Text("%s", Port_Config_BgFillName(Port_Config_BgFill()));
        ImGui::SameLine();
        if (ImGui::Button(">##bgfill")) Port_Config_CycleBgFill(+1);

        if (Port_Config_BgFill() == PORT_BG_FILL_SOLID_COLOR) {
            /* Plain int sliders for R/G/B — controller-navigable, no
             * popup state. (An earlier ColorEdit3 here opened a color
             * picker popup whose lifetime could outlive the F8 ribbon
             * and was suspected of crashing on close-via-gamepad.) The
             * trailing ColorButton with NoPicker is a passive swatch. */
            uint8_t r8 = 0, g8 = 0, b8 = 0;
            Port_Config_BgFillColor(&r8, &g8, &b8);
            int rgb[3] = { (int)r8, (int)g8, (int)b8 };
            bool changed = false;
            ImGui::PushItemWidth(120.0f);
            if (ImGui::SliderInt("R##bgcol_r", &rgb[0], 0, 255)) changed = true;
            ImGui::SameLine();
            if (ImGui::SliderInt("G##bgcol_g", &rgb[1], 0, 255)) changed = true;
            ImGui::SameLine();
            if (ImGui::SliderInt("B##bgcol_b", &rgb[2], 0, 255)) changed = true;
            ImGui::PopItemWidth();
            ImGui::SameLine();
            ImVec4 sw = ImVec4(rgb[0] / 255.0f, rgb[1] / 255.0f, rgb[2] / 255.0f, 1.0f);
            ImGui::ColorButton("##bgcol_preview", sw,
                               ImGuiColorEditFlags_NoPicker |
                               ImGuiColorEditFlags_NoTooltip,
                               ImVec2(22, 22));
            if (changed) {
                Port_Config_SetBgFillColor((uint8_t)rgb[0],
                                           (uint8_t)rgb[1],
                                           (uint8_t)rgb[2]);
            }
        }
    }

    /* Discord Rich Presence — opens a local Unix IPC socket
     * (Linux/macOS) or named pipe (Windows) and publishes "area ·
     * hearts · rupees · time" once Discord is running. Enabled by
     * default; no harm if Discord isn't around. */
    {
        bool drp = Port_DiscordRpc_IsEnabled();
        if (ImGui::Checkbox("Discord Rich Presence", &drp)) {
            Port_DiscordRpc_SetEnabled(drp);
        }
        ImGui::SameLine();
        ImGui::TextDisabled("(?)");
        if (ImGui::IsItemHovered()) {
            ImGui::BeginTooltip();
            ImGui::PushTextWrapPos(360.0f);
            ImGui::TextUnformatted("Publishes current area + heart/rupee "
                                   "count to Discord. Works on Linux, "
                                   "macOS, and Windows. Requires the "
                                   "Discord desktop client to be running, "
                                   "and the env var TMC_DISCORD_APP_ID "
                                   "to point at a registered Discord "
                                   "application ID — otherwise this "
                                   "toggle is a no-op (see stderr).");
            ImGui::PopTextWrapPos();
            ImGui::EndTooltip();
        }
    }
}

/* Save current game to EEPROM, then drop the player back at the
 * title screen. Issue #92 / "sleep menu goes back to title".
 *
 * Calling the engine's SetTask(TASK_TITLE) directly is fine here
 * because the F8 menu only opens while the game is in TASK_GAME —
 * SetTask is valid in that state. The save path is gated behind
 * Port_Save_Quicksave so we go through the same EEPROM-write code
 * the F5 quicksave uses (which is known good). */
extern "C" {
void SetTask(unsigned int task);
}
extern "C" int Port_QuickSave_SaveSlot(int slot);
extern "C" int  Port_QuickSave_AutoOnAreaChangeEnabled(void);
extern "C" void Port_QuickSave_SetAutoOnAreaChange(int on);

static void DoQuitToTitle(bool saveFirst) {
    if (saveFirst) {
        /* Slot 0 is the F5/F6 quicksave slot — writing there mirrors
         * the user pressing F5 first. They can still F6-load it on
         * the next launch. */
        Port_QuickSave_SaveSlot(0);
    }
    SetTask(0 /* TASK_TITLE */);
    Port_DebugMenu_Toggle();   /* close the F8 ribbon */
}

static void DrawRibbonSavesTab(void) {
    /* Quit-to-title actions at the top of the tab — high-visibility
     * because the existing pause menu doesn't expose them. */
    ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.35f, 0.55f, 0.30f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.45f, 0.70f, 0.40f, 1.0f));
    if (ImGui::Button("Save & Quit to Title")) DoQuitToTitle(true);
    ImGui::PopStyleColor(2);
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.55f, 0.35f, 0.30f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.70f, 0.45f, 0.40f, 1.0f));
    if (ImGui::Button("Quit to Title (no save)")) DoQuitToTitle(false);
    ImGui::PopStyleColor(2);
    ImGui::Separator();

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
    {
        bool areaOn = Port_QuickSave_AutoOnAreaChangeEnabled() != 0;
        if (ImGui::Checkbox("Auto-save on area change", &areaOn)) {
            Port_QuickSave_SetAutoOnAreaChange(areaOn ? 1 : 0);
        }
        ImGui::SameLine();
        ImGui::TextDisabled("(?)");
        if (ImGui::IsItemHovered()) {
            ImGui::BeginTooltip();
            ImGui::PushTextWrapPos(360.0f);
            ImGui::TextUnformatted("Fires a snapshot to the auto ring "
                                   "every time you transition between "
                                   "areas/rooms. Independent of the "
                                   "interval timer above.");
            ImGui::PopTextWrapPos();
            ImGui::EndTooltip();
        }
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

    /* Rename buffer keyed by index, so each row has its own inline
     * editor that survives across frames while the user is typing. */
    static char sRenameBuf[32][64] = {};
    static int  sRenameRow = -1;
    static int  sConfirmDeleteRow = -1;

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
            ImGui::SameLine();
        }
        /* Rename: clicking opens an inline text box on the next row.
         * Disabled for the default tmc.sav since renaming it away
         * would orphan fresh installs. */
        const bool isDefault = (std::strcmp(names[i], "tmc.sav") == 0);
        if (!isDefault) {
            if (ImGui::Button("Rename")) {
                sRenameRow = i;
                std::strncpy(sRenameBuf[i], names[i], sizeof(sRenameBuf[i]) - 1);
                sRenameBuf[i][sizeof(sRenameBuf[i]) - 1] = '\0';
            }
            ImGui::SameLine();
            if (ImGui::Button("Delete")) sConfirmDeleteRow = i;
        }
        if (sRenameRow == i) {
            ImGui::Indent(40);
            ImGui::PushItemWidth(220);
            ImGui::InputText("##rename", sRenameBuf[i], sizeof(sRenameBuf[i]));
            ImGui::PopItemWidth();
            ImGui::SameLine();
            if (ImGui::Button("OK")) {
                if (Port_Save_RenameProfile(names[i], sRenameBuf[i])) {
                    Port_DebugMenu_ToastFromExternal("Profile renamed");
                } else {
                    Port_DebugMenu_ToastFromExternal("Rename refused (clash / bad name / default)");
                }
                sRenameRow = -1;
            }
            ImGui::SameLine();
            if (ImGui::Button("X")) sRenameRow = -1;
            ImGui::Unindent(40);
        }
        if (sConfirmDeleteRow == i) {
            ImGui::Indent(40);
            ImGui::TextDisabled("Delete %s? This cannot be undone.", names[i]);
            ImGui::SameLine();
            if (ImGui::Button("Confirm delete")) {
                if (Port_Save_DeleteProfile(names[i])) {
                    Port_DebugMenu_ToastFromExternal("Profile deleted");
                } else {
                    Port_DebugMenu_ToastFromExternal("Delete refused (active / bad name)");
                }
                sConfirmDeleteRow = -1;
                sRenameRow = -1;
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel")) sConfirmDeleteRow = -1;
            ImGui::Unindent(40);
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
/* Returns 1 if (area) is safe to warp to (has a friendly name and is
 * not on the known-broken deny-list). Same predicate the dispatch
 * layer uses, so the UI list and the action layer agree on what's
 * warpable. See port_debug_actions.c::kBrokenWarpAreas. */
extern "C" int Port_DebugAction_AreaIsWarpable(unsigned char area);

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
            /* Skip areas that aren't warpable: no friendly name (AREA_
             * NULL_*, numeric AREA_40-style slots) OR named-but-known-
             * broken (Simon's Sim, etc.). Same predicate the dispatch
             * layer uses so list + action stay in sync. See issue #94. */
            if (!Port_DebugAction_AreaIsWarpable(a)) continue;
            char header[96];
            std::snprintf(header, sizeof(header), "0x%02X  %s  (%d rooms)",
                          area, name, roomCount);

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

/* Randomizer tab — wraps the native in-process engine at port/rando/.
 * No file I/O, no shell-out, no .NET dependency. Pressing "Roll" rolls
 * a seed; subsequent item-give intercepts (M1: chest rewards) apply
 * the new permutation immediately. */
extern "C" const char* Port_FindBaseRomPath(void);

static char sRandoSeedBuf[16] = "0";   /* 0 = engine picks */
static char sRandoStatus[1024] = {0};

static void DrawRibbonRandomizerTab(void) {
    /* Source / region — informational only. The engine reads gRomData
     * from the loaded ROM; no separate input file is involved. */
    const char* src_rom = Port_FindBaseRomPath();
    const char* region_label = "(unknown)";
    char region[5] = {0};
    if (src_rom) {
        FILE* f = std::fopen(src_rom, "rb");
        if (f) {
            std::fseek(f, 0xAC, SEEK_SET);
            (void)std::fread(region, 1, 4, f);
            std::fclose(f);
            if (std::strcmp(region, "BZME") == 0)       region_label = "USA (BZME)";
            else if (std::strcmp(region, "BZMP") == 0)  region_label = "EU (BZMP)";
            else if (std::strcmp(region, "BZMJ") == 0)  region_label = "JP (BZMJ) — not supported";
            else                                        region_label = region;
        }
    }

    ImGui::TextWrapped("Native in-process randomizer. Roll a seed and "
                       "items in chests are shuffled live — no ROM "
                       "files written, no restart needed.");
    ImGui::TextWrapped("M1: 8 major progression items (Sword, Gust Jar, "
                       "Pacci Cane, Mole Mitts, Roc's Cape, Pegasus Boots, "
                       "Fire Rod, Ocarina) get permuted by the seed.");
    ImGui::Separator();

    ImGui::Text("Source ROM:  %s", src_rom ? src_rom : "(none)");
    ImGui::Text("Region:      %s", region_label);
    if (Rando_IsActive()) {
        ImGui::TextColored(ImVec4(0.4f, 0.85f, 0.4f, 1.0f),
                           "Active seed: %u", Rando_GetSeed());
    } else {
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "No seed rolled — vanilla.");
    }

    ImGui::Spacing();
    ImGui::InputText("Seed (0 = random)", sRandoSeedBuf, sizeof(sRandoSeedBuf),
                     ImGuiInputTextFlags_CharsDecimal);

    ImGui::Spacing();
    if (ImGui::Button("Roll new seed", ImVec2(180, 0))) {
        uint32_t seed = (uint32_t)std::strtoul(sRandoSeedBuf, nullptr, 10);
        uint32_t chosen = 0;
        Rando_RollSeed(seed, &chosen);
        char spoiler[2048];
        Rando_GetSpoiler(spoiler, sizeof(spoiler));
        std::snprintf(sRandoStatus, sizeof(sRandoStatus),
                      "Rolled seed %u.\n\n%s", chosen, spoiler);
        std::snprintf(sRandoSeedBuf, sizeof(sRandoSeedBuf), "%u", chosen);
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset to vanilla", ImVec2(160, 0))) {
        Rando_Reset();
        sRandoStatus[0] = '\0';
    }

    if (sRandoStatus[0]) {
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::TextWrapped("%s", sRandoStatus);
    }
}

/* Forward decl pulled from port_audio_mute.h, kept local so this file
 * doesn't depend on the new header for the rest of its surface area. */
extern "C" {
typedef enum {
    AUDIO_MUTE_EZLO_VOICE,
    AUDIO_MUTE_NPC_VOICE,
    AUDIO_MUTE_LOW_HEALTH_BEEP,
    AUDIO_MUTE_COUNT,
} AudioMuteCategory;
bool Port_AudioMute_IsEnabled(AudioMuteCategory c);
void Port_AudioMute_SetEnabled(AudioMuteCategory c, bool on);
const char* Port_AudioMute_Label(AudioMuteCategory c);
const char* Port_AudioMute_Description(AudioMuteCategory c);
}

static void DrawRibbonAudioTab(void) {
    ImGui::TextWrapped("Per-category SFX mutes. Each toggle suppresses "
                       "the matching sound IDs at the SoundReq / EnqueueSFX "
                       "entry points — music and other SFX are untouched.");
    ImGui::Separator();
    for (int i = 0; i < (int)AUDIO_MUTE_COUNT; ++i) {
        bool on = Port_AudioMute_IsEnabled((AudioMuteCategory)i);
        const char* label = Port_AudioMute_Label((AudioMuteCategory)i);
        const char* desc  = Port_AudioMute_Description((AudioMuteCategory)i);
        if (ImGui::Checkbox(label, &on)) {
            Port_AudioMute_SetEnabled((AudioMuteCategory)i, on);
        }
        if (desc && desc[0]) {
            ImGui::SameLine();
            ImGui::TextDisabled("(?)");
            if (ImGui::IsItemHovered()) {
                ImGui::BeginTooltip();
                ImGui::PushTextWrapPos(360.0f);
                ImGui::TextUnformatted(desc);
                ImGui::PopTextWrapPos();
                ImGui::EndTooltip();
            }
        }
    }
}

static void DrawRibbonRebornTab(void) {
    ImGui::TextWrapped("Quality-of-life features cherry-picked from "
                       "Minish Cap Reborn (clean-room — does NOT include "
                       "Reborn's GPL-3 source). Toggles persist until "
                       "tmc_pc closes.");
    ImGui::Separator();
    for (int i = 0; i < REBORN_FEAT_COUNT; ++i) {
        bool on = Port_Reborn_IsEnabled((RebornFeature)i);
        const char* label = Port_Reborn_FeatureLabel((RebornFeature)i);
        const char* desc  = Port_Reborn_FeatureDescription((RebornFeature)i);
        if (ImGui::Checkbox(label, &on)) {
            Port_Reborn_SetEnabled((RebornFeature)i, on);
        }
        if (desc && desc[0]) {
            ImGui::SameLine();
            ImGui::TextDisabled("(?)");
            if (ImGui::IsItemHovered()) {
                ImGui::BeginTooltip();
                ImGui::PushTextWrapPos(360.0f);
                ImGui::TextUnformatted(desc);
                ImGui::PopTextWrapPos();
                ImGui::EndTooltip();
            }
        }
    }
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
            if (ImGui::BeginTabItem("Items"))      { DrawRibbonItemsTab();      ImGui::EndTabItem(); }
            if (ImGui::BeginTabItem("Display"))    { DrawRibbonDisplayTab();    ImGui::EndTabItem(); }
            if (ImGui::BeginTabItem("Saves"))      { DrawRibbonSavesTab();      ImGui::EndTabItem(); }
            if (ImGui::BeginTabItem("Profiles"))   { DrawRibbonProfilesTab();   ImGui::EndTabItem(); }
            if (ImGui::BeginTabItem("Equip"))      { DrawRibbonEquipTab();      ImGui::EndTabItem(); }
            if (ImGui::BeginTabItem("Controls"))   { DrawRibbonControlsTab();   ImGui::EndTabItem(); }
            if (ImGui::BeginTabItem("Warp"))       { DrawRibbonWarpTab();       ImGui::EndTabItem(); }
            if (ImGui::BeginTabItem("Randomizer")) { DrawRibbonRandomizerTab(); ImGui::EndTabItem(); }
            if (ImGui::BeginTabItem("Audio"))      { DrawRibbonAudioTab();      ImGui::EndTabItem(); }
            if (ImGui::BeginTabItem("Reborn"))     { DrawRibbonRebornTab();     ImGui::EndTabItem(); }
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

/* Quit-save confirm modal state. The X-button (SDL_EVENT_QUIT) routes
 * through Port_ImGui_RequestQuitModal which arms this flag instead of
 * exiting straight away. The user picks Save & Quit / Quit Without
 * Saving / Cancel. A static "armed" flag survives across frames until
 * the user makes a choice — the modal can't ride a one-shot bool
 * because ImGui::BeginPopupModal needs to be called every frame while
 * it's open. */
static bool sQuitModalArmed = false;
static bool sQuitModalConfirmed = false;  /* set to true on "Save & Quit" or "Quit" — main loop polls and exits */
extern "C" bool Port_ImGui_QuitConfirmed(void) { return sQuitModalConfirmed; }
extern "C" void Port_ImGui_RequestQuitModal(void) {
    /* If a previous confirm already fired, honour it and let the host
     * exit. This catches the rare double-click on the X button. */
    if (sQuitModalConfirmed) return;
    sQuitModalArmed = true;
}

static void DrawQuitModal(void) {
    if (sQuitModalArmed) {
        ImGui::OpenPopup("Quit?");
        sQuitModalArmed = false;
    }
    /* Centre the popup. */
    const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Quit?", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize |
                               ImGuiWindowFlags_NoCollapse)) {
        ImGui::TextUnformatted("Save before quitting?");
        ImGui::Separator();
        ImGui::TextWrapped("Save & Quit writes the current game state to "
                           "quicksave slot 0 (F6 to reload). Quit Without "
                           "Saving exits immediately — any progress since "
                           "your last in-game save is lost.");
        ImGui::Spacing();
        if (ImGui::Button("Save & Quit", ImVec2(140, 0))) {
            Port_QuickSave_SaveSlot(0);
            sQuitModalConfirmed = true;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Quit Without Saving", ImVec2(180, 0))) {
            sQuitModalConfirmed = true;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(100, 0))) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

extern "C" bool Port_ImGui_Render(void) {
    if (!sImGuiInited || !sImGuiEnabled) return false;
    /* SDL_Renderer path needs a renderer; SDL_GPU path runs with
     * sRenderer == nullptr (NewFrame uses ImGui_ImplSDLGPU3 instead
     * and PresentFrame consumes the draw data via the *_Gpu helpers
     * below). */
#ifndef TMC_GPU_RENDERER
    if (!sRenderer) return false;
#endif

    /* Gamepad nav gated on menu-open state. When the menu is closed,
     * ImGui must NOT consume gamepad input — otherwise the focus-by-
     * default behaviour grabs the persistent MENU trigger and the
     * player's A press opens the menu instead of attacking. Toggle the
     * flag each frame so transitions are immediate. */
    static bool sPrevMenuOpen = false;
    const bool menuOpen = Port_DebugMenu_IsOpen();
    {
        ImGuiIO& io = ImGui::GetIO();
        if (menuOpen) {
            io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
        } else {
            io.ConfigFlags &= ~ImGuiConfigFlags_NavEnableGamepad;
        }
    }

#ifdef TMC_GPU_RENDERER
    const bool gpuBackend = (sRenderer == nullptr);
    if (gpuBackend) {
        ImGui_ImplSDLGPU3_NewFrame();
    } else
#endif
    {
        ImGui_ImplSDLRenderer3_NewFrame();
    }
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();

    /* Defensive cleanup on close-transition (open → closed). Without
     * this, ImGui can retain nav focus / active-widget references to
     * ribbon widgets that won't be drawn on the very next frame —
     * causing intermittent crashes when the menu is closed via gamepad
     * (Select+Start) while a widget is focused or being edited. Force-
     * release window focus and any pending popups so the next render
     * starts from a clean state. Safe to call between NewFrame and the
     * first Begin. */
    if (sPrevMenuOpen && !menuOpen) {
        /* Release any window focus so ImGui's nav state doesn't keep a
         * dangling reference to a ribbon widget. Calling with nullptr
         * is the documented "no window focused" path. */
        ImGui::SetWindowFocus(nullptr);
    }
    sPrevMenuOpen = menuOpen;

    /* Toast survives the menu being closed (e.g. after a warp). */
    DrawToast(Port_DebugMenu_Toast());

    /* Always show the click-to-open trigger so mouse/touch users have a
     * way in without the F8 hotkey. */
    DrawMenuTrigger();

    /* Quit-save confirm modal — only renders when armed by
     * Port_ImGui_RequestQuitModal (called from port_bios.c when SDL
     * reports SDL_EVENT_QUIT). Independent of the F8 ribbon state so
     * the user gets a chance to save even with the menu closed. */
    DrawQuitModal();

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
#ifdef TMC_GPU_RENDERER
    if (gpuBackend) {
        /* GPU path: draw_data lives in ImGui's per-frame state until the
         * GPU PresentFrame consumes it via Port_ImGui_RenderDrawDataGpu.
         * We don't call PrepareDrawData here — that needs the cmd buffer
         * from the GPU side. Return true so the caller knows a frame's
         * worth of ImGui work is queued. */
        return true;
    }
#endif
    ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), sRenderer);
    return true;
}

#ifdef TMC_GPU_RENDERER
/* Stage 2: called from Port_GPU_PresentFrame to inject the F8 menu into
 * the same render pass that draws the game framebuffer. Splits the
 * usual one-call render into two halves — PrepareDrawData uploads
 * vertex/index buffers (must happen before BeginGPURenderPass), and
 * RenderDrawData issues the actual draw commands inside the pass. */
extern "C" void Port_ImGui_PrepareDrawDataGpu(SDL_GPUCommandBuffer* cmd) {
    if (!sImGuiInited || !sImGuiEnabled) return;
    if (sRenderer != nullptr) return;  /* SDL_Renderer path doesn't use this */
    ImDrawData* dd = ImGui::GetDrawData();
    if (!dd) return;
    ImGui_ImplSDLGPU3_PrepareDrawData(dd, cmd);
}

extern "C" void Port_ImGui_RenderDrawDataGpu(SDL_GPUCommandBuffer* cmd,
                                             SDL_GPURenderPass* rp) {
    if (!sImGuiInited || !sImGuiEnabled) return;
    if (sRenderer != nullptr) return;
    ImDrawData* dd = ImGui::GetDrawData();
    if (!dd) return;
    ImGui_ImplSDLGPU3_RenderDrawData(dd, cmd, rp, /*pipeline=*/nullptr);
}
#endif
