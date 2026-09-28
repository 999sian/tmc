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
#include "port_imgui_menu.h"
#include <imgui.h>

/* .glslp runtime hooks (port_glslp_runtime.cpp). File-scope so the F8
 * preset-picker lambda below can call them through C linkage. */
extern "C" int Port_GlslpRuntime_Load(const char*);
extern "C" void Port_GlslpRuntime_Unload(void);
extern "C" int Port_GlslpRuntime_IsActive(void);
#include <backends/imgui_impl_sdl3.h>
#include <backends/imgui_impl_sdlrenderer3.h>
#ifdef TMC_GPU_RENDERER
#include <SDL3/SDL_gpu.h>
#include <backends/imgui_impl_sdlgpu3.h>
#endif

#include "port_debug_query.h"
#include "port_debug_actions.h"
#include "port_runtime_config.h" /* PortInput enum (PORT_INPUT_*) */
#include "item_ids.h"            /* ITEM_* / BOTTLE_CHARM_* enum ids (C++-safe split header) */
#include <cstring>               /* strcmp — group-header breaks in the item toggle list */
#include <cstdio>                /* snprintf — dungeon selector labels */

extern "C" u32* gTranslations[];
extern "C" void Port_ApplyLanguage(void);

#include "port_widescreen.h"
#include "port_gpu_renderer.h"
#include "port_voxel.h"
#include "port_prelaunch_logo.h"
#include "port_reborn.h"
#include "port_discord_rpc.h" /* Port_DiscordRpc_IsEnabled / SetEnabled */
#include "port_tts.h"         /* Port_TTS_* — accessibility tab + focus reader */
#include "port_a11y_cues.h"   /* Port_A11y_ScanSurroundings — navigation cues */
#ifdef TMC_RA
#include "port_ra_ui.h" /* Port_RA_UI_DrawTab / DrawOverlay — RetroAchievements */
#endif
#include "rando/rando.h"
#include "rando/rando_logic.h"
#include "rando/rando_file_menu.h"
#include "port_softslots.h"
#include "rando/rando_runtime.h"
#include "rando/rando_keymap.h"
#include "item_ids.h"

extern "C" {
unsigned GetInventoryValue(unsigned item);
unsigned CheckLocalFlagByBank(unsigned bankOffset, unsigned flag);
unsigned GetFlagBankOffset(unsigned area);
unsigned CheckGlobalFlag(unsigned flag);
const char* Port_DebugQuery_FlagName(int bank, int index);
const char* Port_DebugQuery_FlagDesc(int bank, int index);
}

#include <cstdio>
#include <cstring>
#include <ctime>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

/* The menu state machine lives in port_debug_menu.cpp. We don't include
 * its header (it doesn't expose the page-stack internals) — instead the
 * legacy file exposes a small accessor API just for us. */
extern "C" {
bool Port_DebugMenu_IsOpen(void);
int Port_DebugMenu_PageDepth(void);
const char* Port_DebugMenu_PageTitle(int depth);
int Port_DebugMenu_PageItemCount(int depth);
const char* Port_DebugMenu_PageItemLabel(int depth, int idx);
int Port_DebugMenu_PageCursor(int depth);
void Port_DebugMenu_PageSetCursor(int depth, int idx);
void Port_DebugMenu_PageActivate(int depth, int idx);   /* Enter on item */
void Port_DebugMenu_PageCycleLeft(int depth, int idx);  /* Left arrow */
void Port_DebugMenu_PageCycleRight(int depth, int idx); /* Right arrow */
const char* Port_DebugMenu_Toast(void);                 /* NULL if expired */
}

static bool sImGuiInited = false;
static bool sRibbonEnabled = true; /* Office-style ribbon at top */
static SDL_Window* sWindow = nullptr;
static SDL_Renderer* sRenderer = nullptr;

extern "C" void Port_ImGui_Init(SDL_Window* window, SDL_Renderer* renderer) {
    if (sImGuiInited)
        return;
    if (!window)
        return;
    /* On GPU builds renderer is intentionally NULL — Port_PPU_Init passes
     * null when the SDL_GPU pipeline owns the swapchain. The GPU branch
     * below handles that case; the SDL_Renderer branch still requires
     * a non-null renderer. */
#ifndef TMC_GPU_RENDERER
    if (!renderer)
        return;
#endif

    /* Apply the persisted F8 menu style (ribbon vs classic) now that config
     * has been loaded (issue #146). */
    sRibbonEnabled = Port_Config_GetRibbonEnabled();

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr; /* don't write imgui.ini next to binary */
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
    /* Project Picori theme — heavier rounding + deep-green accents
     * inspired by the Dusklight TP PC port UI. The previous blue palette
     * stayed for the in-game F8 dev menu vibe; this theme leans into the
     * Minish-Cap green character (Ezlo, Link's hat, Minish leaves) and
     * card-like surfaces with bigger rounding so the launcher screen
     * and config tabs feel cohesive. */
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 10.0f;
    style.ChildRounding = 8.0f;
    style.FrameRounding = 8.0f;
    style.PopupRounding = 8.0f;
    style.ScrollbarRounding = 10.0f;
    style.TabRounding = 8.0f;
    style.GrabRounding = 8.0f;
    style.WindowBorderSize = 0.0f; /* card look — solid fills, no outline */
    style.FrameBorderSize = 0.0f;
    style.PopupBorderSize = 0.0f;
    style.WindowPadding = ImVec2(18, 16);
    style.FramePadding = ImVec2(14, 9); /* bigger touch targets */
    style.ItemSpacing = ImVec2(12, 10);
    style.ItemInnerSpacing = ImVec2(10, 6);
    style.ScrollbarSize = 18.0f; /* finger-draggable */
    style.GrabMinSize = 18.0f;
    style.IndentSpacing = 22.0f;
    /* Bump the global font size 1.4× without re-loading a font atlas.
     * ImGui scales the default ProggyClean upward; the resulting glyphs
     * are crisp enough at native resolution for menu use, and big
     * enough to be readable on the Deck at hand-held distance. */
    io.FontGlobalScale = 1.4f;
#ifdef __ANDROID__
    /* Touch pass: a tablet is driven by fingers at arm's length, not a
     * pointer. Scale the whole style so every hit target clears ~48dp
     * (Android's minimum comfortable touch target), fatten scrollbars
     * into real drag handles, and bump the font again over the desktop
     * 1.4x. ScaleAllSizes multiplies paddings/rounding/grab sizes in
     * one shot so proportions stay intact. */
    style.ScaleAllSizes(1.55f);
    style.ScrollbarSize = 34.0f;                  /* fat, thumb-sized scroll handle  */
    style.GrabMinSize = 30.0f;                    /* slider grabs                    */
    style.FramePadding.y += 6.0f;                 /* taller rows = taller tap areas  */
    style.ItemSpacing.y += 4.0f;                  /* breathing room between rows     */
    style.TouchExtraPadding = ImVec2(6.0f, 6.0f); /* forgiving hit test */
    io.FontGlobalScale = 2.0f;
#endif
    ImVec4* colors = style.Colors;
    /* Greens — primary accent (a deep, slightly-warm green that
     * reads as "Minish leaf"), with brighter / dimmer variants. */
    const ImVec4 accentDim = ImVec4(0.18f, 0.32f, 0.22f, 1.00f);
    const ImVec4 accent = ImVec4(0.28f, 0.55f, 0.34f, 1.00f);
    const ImVec4 accentLit = ImVec4(0.40f, 0.72f, 0.46f, 1.00f);
    /* Surface — near-black with a faint cool tint so the green pops. */
    const ImVec4 bgBase = ImVec4(0.058f, 0.07f, 0.07f, 0.96f);
    const ImVec4 bgChild = ImVec4(0.085f, 0.10f, 0.10f, 1.00f);
    const ImVec4 bgFrame = ImVec4(0.13f, 0.15f, 0.15f, 1.00f);
    const ImVec4 bgFrameH = ImVec4(0.17f, 0.21f, 0.20f, 1.00f);

    colors[ImGuiCol_WindowBg] = bgBase;
    colors[ImGuiCol_ChildBg] = bgChild;
    colors[ImGuiCol_PopupBg] = bgBase;
    colors[ImGuiCol_FrameBg] = bgFrame;
    colors[ImGuiCol_FrameBgHovered] = bgFrameH;
    colors[ImGuiCol_FrameBgActive] = accentDim;
    colors[ImGuiCol_TitleBg] = ImVec4(0.07f, 0.10f, 0.09f, 1.00f);
    colors[ImGuiCol_TitleBgActive] = accentDim;
    colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.05f, 0.07f, 0.06f, 0.75f);
    colors[ImGuiCol_MenuBarBg] = ImVec4(0.10f, 0.12f, 0.11f, 1.00f);
    colors[ImGuiCol_Header] = ImVec4(accent.x, accent.y, accent.z, 0.32f);
    colors[ImGuiCol_HeaderHovered] = ImVec4(accent.x, accent.y, accent.z, 0.60f);
    colors[ImGuiCol_HeaderActive] = accent;
    colors[ImGuiCol_Button] = bgFrame;
    colors[ImGuiCol_ButtonHovered] = accent;
    colors[ImGuiCol_ButtonActive] = accentLit;
    colors[ImGuiCol_Tab] = ImVec4(0.10f, 0.13f, 0.11f, 1.00f);
    colors[ImGuiCol_TabHovered] = accent;
    colors[ImGuiCol_TabActive] = accentDim;
    colors[ImGuiCol_TabUnfocused] = ImVec4(0.07f, 0.09f, 0.08f, 1.00f);
    colors[ImGuiCol_TabUnfocusedActive] = ImVec4(0.13f, 0.18f, 0.15f, 1.00f);
    colors[ImGuiCol_Separator] = ImVec4(0.20f, 0.24f, 0.22f, 1.00f);
    colors[ImGuiCol_SeparatorHovered] = accent;
    colors[ImGuiCol_SeparatorActive] = accentLit;
    colors[ImGuiCol_ResizeGrip] = ImVec4(accent.x, accent.y, accent.z, 0.25f);
    colors[ImGuiCol_ResizeGripHovered] = ImVec4(accent.x, accent.y, accent.z, 0.55f);
    colors[ImGuiCol_ResizeGripActive] = accent;
    colors[ImGuiCol_SliderGrab] = accent;
    colors[ImGuiCol_SliderGrabActive] = accentLit;
    colors[ImGuiCol_CheckMark] = accentLit;
    colors[ImGuiCol_ScrollbarBg] = ImVec4(0.05f, 0.06f, 0.06f, 1.00f);
    colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.20f, 0.24f, 0.22f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabHovered] = accentDim;
    colors[ImGuiCol_ScrollbarGrabActive] = accent;
    colors[ImGuiCol_Text] = ImVec4(0.93f, 0.94f, 0.92f, 1.00f);
    colors[ImGuiCol_TextDisabled] = ImVec4(0.50f, 0.54f, 0.50f, 1.00f);
    colors[ImGuiCol_TextSelectedBg] = ImVec4(accent.x, accent.y, accent.z, 0.40f);

#ifdef TMC_GPU_RENDERER
    /* GPU path: renderer arg is NULL (Port_PPU_Init passed null when the
     * SDL_GPU pipeline owns the window). Initialise the SDL_GPU ImGui
     * backend instead — its NewFrame/PrepareDrawData/RenderDrawData
     * trio integrates with our existing SDL_GPU PresentFrame. */
    if (renderer == nullptr) {
        SDL_GPUDevice* dev = Port_GPU_GetDevice();
        SDL_GPUTextureFormat fmt = Port_GPU_GetSwapchainFormat();
        if (!dev || fmt == SDL_GPU_TEXTUREFORMAT_INVALID) {
            fprintf(stderr, "[imgui] GPU device/format unavailable - F8 menu disabled\n");
            ImGui::DestroyContext();
            return;
        }
        if (!ImGui_ImplSDL3_InitForSDLGPU(window)) {
            fprintf(stderr, "[imgui] ImGui_ImplSDL3_InitForSDLGPU failed\n");
            ImGui::DestroyContext();
            return;
        }
        ImGui_ImplSDLGPU3_InitInfo info = {};
        info.Device = dev;
        info.ColorTargetFormat = fmt;
        info.MSAASamples = SDL_GPU_SAMPLECOUNT_1;
        if (!ImGui_ImplSDLGPU3_Init(&info)) {
            fprintf(stderr, "[imgui] ImGui_ImplSDLGPU3_Init failed\n");
            ImGui_ImplSDL3_Shutdown();
            ImGui::DestroyContext();
            return;
        }
        sWindow = window;
        sRenderer = nullptr; /* GPU backend signals "no SDL_Renderer" */
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
    if (!sImGuiInited)
        return;
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

/* True when the per-frame ImGui pass can actually present UI this run:
 * init succeeded (Renderer or GPU backend) and the runtime toggle is on.
 * The surface fallback backend never initialises ImGui, and the GPU
 * device probe can fail — gates that auto-open input-masking overlays
 * (file-select randomizer setup) check this so they never open an
 * invisible modal over a masked game (= softlock). */
extern "C" bool Port_ImGui_CanPresent(void) {
    if (!sImGuiInited)
        return false;
#ifndef TMC_GPU_RENDERER
    if (!sRenderer)
        return false;
#endif
    return true;
}

/* True when an ImGui text widget currently has keyboard focus (e.g. the
 * seed entry field). The port input layer consults this before letting a
 * keyboard key that doubles as a GBA button (default L = 'a', a valid seed
 * char) close the file-select setup sidebar, so typing a seed isn't
 * interrupted. */
extern "C" bool Port_ImGui_WantsTextInput(void) {
    if (!sImGuiInited)
        return false;
    return ImGui::GetIO().WantTextInput;
}
extern "C" bool Port_ImGui_WantsMouse(void) {
    return sImGuiInited && ImGui::GetIO().WantCaptureMouse;
}
extern "C" void Port_ImGui_HandleEvent(const SDL_Event* event) {
    if (!sImGuiInited)
        return;
    ImGui_ImplSDL3_ProcessEvent(event);
#ifdef __ANDROID__
    /* Touch drag-to-scroll: ImGui has no native flick/drag scrolling —
     * on desktop the wheel does it; on a tablet nothing does, and long
     * tabs (Warp's area list, Items) are unusable. Convert vertical
     * finger motion into wheel events while a menu is up.
     *
     * Deliberately NOT gated on IsAnyItemActive: a finger resting on a
     * row button activates it instantly, which would veto the very drag
     * that's supposed to scroll. Buttons commit on RELEASE and ImGui
     * cancels a press whose item scrolls out from under the pointer, so
     * scrolling over buttons is safe. Horizontal wheel is dropped —
     * sliders are horizontal drags; injecting dx would fight them and
     * almost nothing scrolls horizontally. Text-input focus (drag =
     * text selection) suppresses injection entirely. */
    if (event->type == SDL_EVENT_FINGER_MOTION && !ImGui::GetIO().WantTextInput &&
        (Port_DebugMenu_IsOpen() || Port_RandoFileMenu_IsOpen())) {
        ImGuiIO& io = ImGui::GetIO();
        const float dyPx = event->tfinger.dy * io.DisplaySize.y;
        /* One wheel notch scrolls ~67px in ImGui; convert px so content
         * tracks the finger 1:1-ish. Sign: finger down = content down. */
        io.AddMouseWheelEvent(0.0f, dyPx / 67.0f);
    }
#endif
}

extern "C" bool Port_ImGui_IsEnabled(void) {
    return true;
}
extern "C" bool Port_ImGui_RibbonEnabled(void) {
    return sRibbonEnabled;
}
extern "C" void Port_ImGui_SetRibbonEnabled(bool enabled) {
    sRibbonEnabled = enabled;
}

static void RandoUi_HelpTooltip(const char* text);
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

void Port_PPU_ToggleFullscreen(void);
bool Port_PPU_IsFullscreen(void);
void Port_PPU_ApplyCursorVisibility(void);
void Port_PPU_SetVSync(bool enabled);
bool Port_PPU_VSyncEnabled(void);
void Port_PPU_SetColorCorrection(bool enabled);
bool Port_PPU_ColorCorrectionEnabled(void);
void Port_PPU_SetPersistence(bool enabled, float rho);
void Port_PPU_CycleWindowScale(int direction);
void Port_PPU_ApplyWindowScale(void);
unsigned char Port_PPU_WindowScale(void);
void Port_PPU_CyclePresentationMode(int direction);
const char* Port_PPU_PresentationModeName(void);
void Port_PPU_CycleFilter(int direction);
const char* Port_PPU_FilterName(void);
unsigned int Port_Config_TargetFps(void);
void Port_Config_CycleTargetFps(int direction);
unsigned char Port_Config_InternalScale(void);
void Port_Config_CycleInternalScale(int direction);
void Port_Audio_SetGbaAccurate(bool accurate);
bool Port_Audio_IsGbaAccurate(void);
void Port_Audio_SetWidth(float width);
float Port_Audio_GetWidth(void);
void Port_Audio_SetReverbLevel(int level);
int Port_Audio_GetReverbLevel(void);
void Port_Audio_SetMasterVolume(float volume);
float Port_Audio_GetMasterVolume(void);

int Port_QuickSave_SaveSlot(int slot);
int Port_QuickSave_LoadSlot(int slot);
int Port_QuickSave_HasSlot(int slot);
unsigned long long Port_QuickSave_SlotTimestamp(int slot);
int Port_QuickSave_SlotCount(void);
int Port_QuickSave_AutoSlotBase(void);
int Port_QuickSave_AutoEnabled(void);
void Port_QuickSave_SetAutoEnabled(int enabled);
unsigned int Port_QuickSave_AutoIntervalMs(void);
void Port_QuickSave_SetAutoIntervalMs(unsigned int ms);
bool Port_Config_AutosaveEnabled(void);
void Port_Config_SetAutosaveEnabled(bool enabled);
void Port_Config_SetAutosaveIntervalMs(unsigned int ms);

const char* Port_Save_GetActivePath(void);
int Port_Save_SetActivePath(const char* path);
int Port_Save_SaveAsProfile(const char* path);
int Port_Save_ListProfiles(char (*out)[64], int max);
int Port_Save_DeleteProfile(const char* path);
int Port_Save_RenameProfile(const char* oldPath, const char* newPath);
void Port_Config_SetActiveSaveProfile(const char* path);

const char* Port_SoftSlots_GetSlotLabel(int slot);
void Port_SoftSlots_CycleAssignment(int slot, int direction);

const char* Port_Config_InputName(PortInput input);
int Port_Config_BindingCount(PortInput input);
void Port_Config_BindingLabel(PortInput input, int idx, char* out, int cap);
void Port_Config_ClearBindings(PortInput input);
void Port_Config_BeginCaptureBinding(PortInput input);
void Port_Config_BeginAddBinding(PortInput input);
int Port_Config_IsCapturingBinding(void);
int Port_Config_CapturingBindingInput(void);
void Port_Config_CancelCaptureBinding(void);
void Port_Config_ResetAllBindings(void);

void Port_DebugMenu_Toggle(void);

/* Speedrun practice mode (port_practice.c). u16/u64 are declared here as the
 * underlying fixed-width types; extern "C" matches by symbol name so this
 * stays ABI-compatible with the C definitions. */
unsigned long long Port_Practice_ElapsedFrames(void);
bool Port_Practice_TimerRunning(void);
void Port_Practice_TimerReset(void);
void Port_Practice_TimerToggle(void);
void Port_Practice_AddSplit(void);
int Port_Practice_SplitCount(void);
unsigned long long Port_Practice_SplitAt(int i);
void Port_Practice_ClearSplits(void);
unsigned short Port_Practice_CurrentInputMask(void);
unsigned short Port_Practice_HistoryAt(int index);
int Port_Practice_HistoryCount(void);
int Port_Practice_SetPoint(void);
int Port_Practice_LoadPoint(void);
bool Port_Practice_HasPoint(void);
bool Port_Practice_IsPaused(void);
void Port_Practice_TogglePause(void);
}

/* Mini-toast for ribbon actions so the user sees "Saved" etc. without
 * having to look at stderr. Reuses the legacy Toast() path through the
 * existing public toast accessor. */
extern "C" void Port_DebugMenu_ToastFromExternal(const char* msg);

#include "port_imgui_items_tab.inc"

#include "port_imgui_flags_tab.inc"

#include "port_imgui_display_tab.inc"

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
extern "C" int Port_QuickSave_AutoOnAreaChangeEnabled(void);
extern "C" void Port_QuickSave_SetAutoOnAreaChange(int on);

static void DoQuitToTitle(bool saveFirst) {
    if (saveFirst) {
        /* Slot 0 is the F5/F6 quicksave slot — writing there mirrors
         * the user pressing F5 first. They can still F6-load it on
         * the next launch. */
        Port_QuickSave_SaveSlot(0);
    }
    SetTask(0 /* TASK_TITLE */);
    Port_DebugMenu_Toggle(); /* close the F8 ribbon */
}
static bool DrawRegionLanguageControls(bool prelaunch) {
    bool regionChanged = false;
    ImGui::SeparatorText("ROM Region & Language");

    int preferredRegion = Port_Config_PreferredRegion();
    if (preferredRegion < -1 || preferredRegion > 2)
        preferredRegion = -1;

    const char* regionNames[] = {
        "Auto (Use first valid ROM)",
        "USA (baserom.gba)",
        "EU (baserom_eu.gba)",
        "JP (baserom_jp.gba)",
    };
    int regionIdx = preferredRegion + 1; // map -1..2 to 0..3
    ImGui::SetNextItemWidth(270);
    if (ImGui::Combo("Preferred ROM", &regionIdx, regionNames, 4)) {
        Port_Config_SetPreferredRegion(regionIdx - 1);
        regionChanged = true;
    }
    ImGui::SameLine();
    ImGui::TextDisabled(prelaunch ? "(used when Play starts)" : "(restart required)");

    constexpr int kLanguageCount = 6;
    int preferredLanguage = Port_Config_PreferredLanguage();
    if (preferredLanguage < -1 || preferredLanguage >= kLanguageCount)
        preferredLanguage = -1;

    const char* langNames[] = {
        "Auto (ROM/save default)", "Japanese", "English", "French", "German", "Spanish", "Italian",
    };
    int langIdx = preferredLanguage + 1; // map -1..5 to 0..6

    ImGui::SetNextItemWidth(270);
    if (ImGui::BeginCombo("Language", langNames[langIdx])) {
        for (int i = 0; i < 7; ++i) {
            bool isSupported = true;
            char label[128];
            std::strcpy(label, langNames[i]);

            if (!prelaunch && i > 0) {
                const int langVal = i - 1;
                if (gTranslations[langVal] == nullptr) {
                    isSupported = false;
                    std::strcat(label, " (not supported by loaded ROM)");
                }
            }

            const bool selected = (i == langIdx);
            if (!isSupported)
                ImGui::BeginDisabled();
            if (ImGui::Selectable(label, selected)) {
                Port_Config_SetPreferredLanguage(i - 1);
                if (!prelaunch)
                    Port_ApplyLanguage();
            }
            if (!isSupported)
                ImGui::EndDisabled();
        }
        ImGui::EndCombo();
    }
    if (prelaunch) {
        ImGui::TextDisabled("Language is applied after the selected ROM loads.");
    }
    return regionChanged;
}

#include "port_imgui_saves_tab.inc"

#include "port_imgui_profiles_tab.inc"

#include "port_imgui_controls_tab.inc"

#include "port_imgui_equip_tab.inc"

#include "port_imgui_warp_tab.inc"

#include "port_imgui_randomizer_tab.inc"

#include "port_imgui_audio_tab.inc"

#include "port_imgui_accessibility_tab.inc"

#include "port_imgui_reborn_tab.inc"

#include "port_imgui_practice_tab.inc"

#include "port_imgui_map_editor_tab.inc"

#include "port_imgui_entities_tab.inc"

#include "port_imgui_memory_tab.inc"

static void DrawRibbon(void) {
    ImGuiIO& io = ImGui::GetIO();
    const float ribbonW = io.DisplaySize.x;
    ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(ribbonW, 0), ImGuiCond_Always);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    if (ImGui::Begin("##ribbon", nullptr,
                     ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                         ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_AlwaysAutoResize |
                         ImGuiWindowFlags_NoCollapse)) {
        /* Close button anchored to the top-right of the ribbon. The
         * persistent corner trigger sits behind the ribbon when it's
         * open, so without this button users on mouse-only or who
         * forgot the F8/Select+Start hotkey have no way out. Render
         * it BEFORE the tab bar so it sits at the very top edge. */
        const float closeW = 80.0f;
        ImGui::SameLine(ImGui::GetWindowWidth() - closeW - 12.0f);
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.55f, 0.20f, 0.20f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.75f, 0.30f, 0.30f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.85f, 0.35f, 0.35f, 1.0f));
        if (ImGui::Button("Close X", ImVec2(closeW, 0))) {
            Port_DebugMenu_Toggle();
        }
        ImGui::PopStyleColor(3);

        if (ImGui::BeginTabBar("##ribbonTabs", ImGuiTabBarFlags_None)) {
            if (ImGui::BeginTabItem("Items")) {
                DrawRibbonItemsTab();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Display")) {
                DrawRibbonDisplayTab();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Saves")) {
                DrawRibbonSavesTab();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Profiles")) {
                DrawRibbonProfilesTab();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Equip")) {
                DrawRibbonEquipTab();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Controls")) {
                DrawRibbonControlsTab();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Warp")) {
                DrawRibbonWarpTab();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Entities")) {
                DrawRibbonEntitiesTab();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Flags")) {
                DrawRibbonFlagsTab();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Memory")) {
                DrawRibbonMemoryTab();
                ImGui::EndTabItem();
            }
            if ((!Rando_IsInGameplay() || Rando_IsActive()) && ImGui::BeginTabItem("Randomizer")) {
                DrawRibbonRandomizerTab();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Audio")) {
                DrawRibbonAudioTab();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Accessibility")) {
                DrawRibbonAccessibilityTab();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Reborn")) {
                DrawRibbonRebornTab();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Practice")) {
                DrawRibbonPracticeTab();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Map Editor")) {
                DrawRibbonMapEditorTab();
                ImGui::EndTabItem();
            }
#ifdef TMC_RA
            if (ImGui::BeginTabItem("Achievements")) {
                Port_RA_UI_DrawTab();
                ImGui::EndTabItem();
            }
#endif
            ImGui::EndTabBar();
        }
        /* Footer with the mode toggle + hotkey hint. */
        ImGui::Separator();
        /* Same persisted setting as the prelaunch card's "Additional
         * Settings" checkbox — exposed here too since this menu is the
         * only way to reach it once skip_launcher has already skipped
         * the prelaunch screen once. */
        bool skipLauncher = Port_Config_GetSkipLauncher();
        if (ImGui::Checkbox("Skip launcher on future runs", &skipLauncher)) {
            Port_Config_SetSkipLauncher(skipLauncher);
        }
        bool useRibbon = sRibbonEnabled;
        if (ImGui::Checkbox("Ribbon mode (uncheck for classic menu)", &useRibbon)) {
            sRibbonEnabled = useRibbon;
            Port_Config_SetRibbonEnabled(useRibbon); /* persist (#146) */
        }
        ImGui::SameLine();
        ImGui::TextDisabled("(F8 or Select+Start also toggles)");
        ImGui::TextDisabled("F5/F6 quicksave/load   F9 bug report   -   see the Controls tab for all hotkeys");
    }
    ImGui::End();
    ImGui::PopStyleVar();
}

/* Layout helpers — keep all the styling decisions in one place so it's
 * easy to tweak the look without hunting through draw code. */
static void DrawToast(const char* text) {
    if (!text || !*text)
        return;
    ImGuiIO& io = ImGui::GetIO();
    ImVec2 vpSize = io.DisplaySize;
    const float pad = 12.0f;
    ImGui::SetNextWindowBgAlpha(0.85f);
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.94f, 0.25f, 1.0f));
    ImGui::SetNextWindowPos(ImVec2(vpSize.x * 0.5f, vpSize.y - pad - 24.0f), ImGuiCond_Always, ImVec2(0.5f, 0.0f));
    if (ImGui::Begin("##toast", nullptr,
                     ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                         ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoFocusOnAppearing |
                         ImGuiWindowFlags_NoInputs)) {
        ImGui::TextUnformatted(text);
    }
    ImGui::End();
    ImGui::PopStyleColor();
}

/* ---- Speedrun practice overlay ---------------------------------------- *
 * Non-interactive (NoInputs) HUD drawn every frame, independent of the F8
 * menu, gated by the practice_* config toggles. Timer top-centre; input
 * display + rolling history bottom-centre. All state from port_practice.c. */

static void Practice_FormatFrames(unsigned long long frames, char* out, size_t cap) {
    unsigned long long totalMs = frames * 1000ull / 60ull; /* 60 fps IGT */
    unsigned ms = (unsigned)(totalMs % 1000);
    unsigned long long totalS = totalMs / 1000;
    unsigned s = (unsigned)(totalS % 60);
    unsigned m = (unsigned)(totalS / 60);
    snprintf(out, cap, "%u:%02u.%03u", m, s, ms);
}

/* Button rows shared by the held-glyph line and the history grid. */
static const struct {
    int bit;
    const char* name;
} kPracticeBtns[] = {
    { PORT_INPUT_A, "A" },      { PORT_INPUT_B, "B" },       { PORT_INPUT_L, "L" },    { PORT_INPUT_R, "R" },
    { PORT_INPUT_UP, "^" },     { PORT_INPUT_DOWN, "v" },    { PORT_INPUT_LEFT, "<" }, { PORT_INPUT_RIGHT, ">" },
    { PORT_INPUT_START, "St" }, { PORT_INPUT_SELECT, "Se" },
};
static const int kPracticeBtnCount = (int)(sizeof(kPracticeBtns) / sizeof(kPracticeBtns[0]));

static void Practice_DrawHeldGlyphs(unsigned short mask) {
    for (int i = 0; i < kPracticeBtnCount; ++i) {
        bool on = (mask & (unsigned short)(1u << kPracticeBtns[i].bit)) != 0;
        ImVec4 col = on ? ImVec4(0.30f, 0.85f, 0.45f, 1.0f) : ImVec4(0.35f, 0.35f, 0.35f, 1.0f);
        ImGui::TextColored(col, "%s", kPracticeBtns[i].name);
        if (i != kPracticeBtnCount - 1)
            ImGui::SameLine();
    }
}

static void Practice_DrawHistory(void) {
    const int cols = 60; /* ~1 second of frames */
    const float cw = 4.0f, ch = 9.0f, labelW = 16.0f;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 origin = ImGui::GetCursorScreenPos();
    for (int r = 0; r < kPracticeBtnCount; ++r) {
        float y = origin.y + r * ch;
        dl->AddText(ImVec2(origin.x, y), IM_COL32(180, 180, 180, 255), kPracticeBtns[r].name);
        for (int c = 0; c < cols; ++c) {
            /* Rightmost column (c=cols-1) is the newest sample (history idx 0). */
            unsigned short m = Port_Practice_HistoryAt(cols - 1 - c);
            if (m & (unsigned short)(1u << kPracticeBtns[r].bit)) {
                float x = origin.x + labelW + c * cw;
                dl->AddRectFilled(ImVec2(x, y), ImVec2(x + cw - 1.0f, y + ch - 1.0f), IM_COL32(80, 200, 120, 255));
            }
        }
    }
    ImGui::Dummy(ImVec2(labelW + cols * cw, kPracticeBtnCount * ch));
}

/* ---- FPS counter overlay ----------------------------------------------
 * Top-right HUD gated by show_fps. Under decoupled pacing render rate and
 * game speed are separate numbers, so both are shown: FPS is what the
 * display gets, TPS is how fast the game is actually running (60 = correct
 * speed regardless of the FPS cap). Rates refresh once per second in
 * port_bios.c. */
extern "C" {
extern double gPortPaceFps;
extern double gPortPaceTps;
extern bool gPortPaceDecoupled;
}

static void DrawFpsOverlay(void) {
    if (!Port_Config_GetShowFps())
        return;

    /* Foreground draw list: on top of every ImGui window (incl. the F8
     * menu), MangoHud-style, and costs no window/focus bookkeeping. */
    char fpsTxt[24], tpsTxt[24];
    snprintf(fpsTxt, sizeof(fpsTxt), "%.0f FPS", gPortPaceFps);
    snprintf(tpsTxt, sizeof(tpsTxt), " / %.0f TPS", gPortPaceTps);

    ImDrawList* dl = ImGui::GetForegroundDrawList();
    ImGuiIO& io = ImGui::GetIO();
    const float pad = 10.0f;
    const float inset = 5.0f;
    ImVec2 fpsSz = ImGui::CalcTextSize(fpsTxt);
    ImVec2 tpsSz = gPortPaceDecoupled ? ImGui::CalcTextSize(tpsTxt) : ImVec2(0, 0);
    ImVec2 boxMax = ImVec2(io.DisplaySize.x - pad, pad + fpsSz.y + inset * 2);
    ImVec2 boxMin = ImVec2(boxMax.x - (fpsSz.x + tpsSz.x + inset * 2), pad);
    dl->AddRectFilled(boxMin, boxMax, IM_COL32(0, 0, 0, 150), 4.0f);
    ImVec2 cur = ImVec2(boxMin.x + inset, boxMin.y + inset);
    dl->AddText(cur, IM_COL32(90, 230, 115, 255), fpsTxt);
    if (gPortPaceDecoupled) {
        cur.x += fpsSz.x;
        /* Game speed: yellow at the correct rate (60, or 59.73 parity),
         * red when it deviates (overloaded machine or fast-forward). */
        bool nominal = gPortPaceTps > 58.0 && gPortPaceTps < 62.0;
        ImU32 col = nominal ? IM_COL32(255, 240, 76, 255) : IM_COL32(255, 115, 90, 255);
        dl->AddText(cur, col, tpsTxt);
    }
}

static void DrawPracticeOverlay(void) {
    const bool showTimer = Port_Config_GetPracticeShowTimer();
    const bool showInputs = Port_Config_GetPracticeShowInputs();
    const bool showHistory = Port_Config_GetPracticeShowHistory();
    if (!showTimer && !showInputs && !showHistory)
        return;

    ImGuiIO& io = ImGui::GetIO();
    const ImVec2 vp = io.DisplaySize;
    const float pad = 10.0f;
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                                   ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoFocusOnAppearing |
                                   ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoInputs;

    if (showTimer) {
        char buf[32];
        Practice_FormatFrames(Port_Practice_ElapsedFrames(), buf, sizeof(buf));
        ImGui::SetNextWindowBgAlpha(0.75f);
        ImGui::SetNextWindowPos(ImVec2(vp.x * 0.5f, pad), ImGuiCond_Always, ImVec2(0.5f, 0.0f));
        if (ImGui::Begin("##practice_timer", nullptr, flags)) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.94f, 0.30f, 1.0f));
            ImGui::TextUnformatted(buf);
            ImGui::PopStyleColor();
            ImGui::SameLine();
            ImGui::TextDisabled("(%llu)", (unsigned long long)Port_Practice_ElapsedFrames());
            if (Port_Practice_IsPaused()) {
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "PAUSED");
            } else if (!Port_Practice_TimerRunning()) {
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "STOP");
            }
        }
        ImGui::End();
    }

    if (showInputs || showHistory) {
        ImGui::SetNextWindowBgAlpha(0.70f);
        ImGui::SetNextWindowPos(ImVec2(vp.x * 0.5f, vp.y - pad), ImGuiCond_Always, ImVec2(0.5f, 1.0f));
        if (ImGui::Begin("##practice_inputs", nullptr, flags)) {
            if (showInputs)
                Practice_DrawHeldGlyphs(Port_Practice_CurrentInputMask());
            if (showHistory) {
                if (showInputs)
                    ImGui::Spacing();
                Practice_DrawHistory();
            }
        }
        ImGui::End();
    }
}

static void DrawMenuPage(int depth) {
    const char* title = Port_DebugMenu_PageTitle(depth);
    const int count = Port_DebugMenu_PageItemCount(depth);
    const int cursor = Port_DebugMenu_PageCursor(depth);
    if (!title || count <= 0)
        return;

    ImGuiIO& io = ImGui::GetIO();
    const float panelW = 460.0f;
    const float maxH = io.DisplaySize.y * 0.85f;
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_Always,
                            ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(panelW, 0), ImGuiCond_Always);
    ImGui::SetNextWindowSizeConstraints(ImVec2(panelW, 0), ImVec2(panelW, maxH));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowMinSize, ImVec2(panelW, 0));
    if (ImGui::Begin(title, nullptr,
                     ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                         ImGuiWindowFlags_AlwaysAutoResize)) {
        if (ImGui::BeginChild("##items", ImVec2(0, ImGui::GetTextLineHeightWithSpacing() * 22.0f), false,
                              ImGuiWindowFlags_None)) {
            for (int i = 0; i < count; ++i) {
                const char* label = Port_DebugMenu_PageItemLabel(depth, i);
                if (!label)
                    continue;
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
        if (depth == 0)
            ImGui::TextUnformatted("F5/F6 quicksave/load   F9 bug report   (Controls tab: all keys)");
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
    /* One-shot discovery hint: mark it seen the moment the menu is first
     * opened by ANY path (F8, gamepad Select+Start, or this button), so it
     * never nags a returning player again. */
    if (open && !Port_Config_GetMenuHintSeen()) {
        Port_Config_SetMenuHintSeen(true);
    }
    const bool showHint = !open && !Port_Config_GetMenuHintSeen();
    const float pad = 6.0f;
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - pad, pad), ImGuiCond_Always, ImVec2(1.0f, 0.0f));

    /* Closed: 12% alpha background, ~minimal padding, single-glyph label —
     * so the trigger reads as a faint corner dot rather than an opaque UI
     * element overlapping the player's eye-line. First run (showHint): draw
     * it boldly with a spelled-out label + the F8 key so a new player learns
     * the settings door exists. */
    if (open) {
        ImGui::SetNextWindowBgAlpha(0.85f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6, 4));
    } else if (showHint) {
        ImGui::SetNextWindowBgAlpha(0.85f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6, 4));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6, 3));
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.35f, 0.55f, 0.95f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.30f, 0.45f, 0.65f, 1.00f));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.00f, 1.00f, 1.00f, 1.00f));
    } else {
        ImGui::SetNextWindowBgAlpha(0.12f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(2, 2));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4, 2));
        /* Make the closed-state button itself low-alpha too; ImGui's
         * hover state will bump it on its own when the cursor lands. */
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.22f, 0.28f, 0.30f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.30f, 0.45f, 0.65f, 1.00f));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.92f, 0.92f, 0.92f, 0.50f));
    }

    /* NoNavInputs + NoNavFocus keep gamepad/keyboard nav from ever
     * targeting this button, so A on the controller can't accidentally
     * open the menu during gameplay. Mouse/touch click still works. */
    if (ImGui::Begin("##menu_trigger", nullptr,
                     ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                         ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_AlwaysAutoResize |
                         ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNavInputs |
                         ImGuiWindowFlags_NoNavFocus)) {
        /* Closed: single triple-bar ASCII '=' stacked into a hamburger
         * shape (the default ImGui font doesn't ship U+2261 ≡). First run:
         * spelled-out "Settings (F8)". Open: clear close label. */
        const char* label = open ? " CLOSE MENU " : (showHint ? " Settings  (F8) " : "[=]");
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
static bool sQuitModalConfirmed = false; /* set to true on "Save & Quit" or "Quit" — main loop polls and exits */
extern "C" bool Port_ImGui_QuitConfirmed(void) {
    return sQuitModalConfirmed;
}
extern "C" void Port_ImGui_RequestQuitModal(void) {
    /* If a previous confirm already fired, honour it and let the host
     * exit. This catches the rare double-click on the X button. */
    if (sQuitModalConfirmed)
        return;
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
    if (ImGui::BeginPopupModal("Quit?", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse)) {
        ImGui::TextUnformatted("Save before quitting?");
        ImGui::Separator();
        ImGui::TextWrapped("Save & Quit writes the current game state to "
                           "quicksave slot 0 (F6 to reload). Quit Without "
                           "Saving exits immediately - any progress since "
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

/* ---- File-select randomizer setup modal ---------------------------------
 * State machine + commit logic live in rando/rando_file_menu.c; this draws
 * it with ImGui so it presents on every backend (the old SDL_Renderer-
 * primitive overlay was invisible on SDL_GPU). Opened by src/fileselect.c
 * on new-file creation (STATE_RANDOMIZER_CONFIG); closes via Start/Cancel,
 * Escape, or gamepad B. Game input stays masked while open (port_bios.c
 * holds KEYINPUT released and swallows SDL events). */
static void DrawRandoFileMenuModal(void) {
    bool forceOpen = Port_RandoFileMenu_IsModalOpen();
    bool shouldShow = forceOpen || (Rando_IsInFileSelect() && Port_RandoFileMenu_IsSidebarOpen());
    if (!shouldShow)
        return;

    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const float padding = 12.0f;
    const float sidebarW = 380.0f;
    const float sidebarH = vp->WorkSize.y - 2 * padding;

    ImGui::SetNextWindowPos(ImVec2(vp->Pos.x + vp->Size.x - sidebarW - padding, vp->Pos.y + padding), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(sidebarW, sidebarH), ImGuiCond_Always);

    if (ImGui::Begin("##port_setup_sidebar", nullptr,
                     ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                         ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoSavedSettings)) {

        ImGui::TextColored(ImVec4(0.78f, 0.95f, 0.78f, 1.0f), "PORT & RANDOMIZER SETUP");
        ImGui::Separator();

        // Randomizer checkbox (toggle rando vs vanilla)
        bool randoEnabled = Port_RandoFileMenu_GetRandoOptionEnabled();
        if (ImGui::Checkbox("Enable Randomizer Mode", &randoEnabled)) {
            Port_RandoFileMenu_SetRandoOptionEnabled(randoEnabled);
        }
        RandoUi_HelpTooltip("On: Starting a new save slot will roll a randomized seed using "
                            "the settings below.\n\n"
                            "Off (default): New slots start as a normal, unmodified vanilla game.");

        ImGui::Separator();

        // 1. RANDOMIZER SETUP SECTION (Only active if enabled)
        if (randoEnabled) {
            if (ImGui::CollapsingHeader("Randomizer Setup", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::SetNextItemWidth(180);
                if (ImGui::InputText("Seed (empty = random)", Port_RandoFileMenu_SeedBuffer(),
                                     RANDO_FILE_MENU_SEED_MAX + 1, ImGuiInputTextFlags_EnterReturnsTrue)) {
                    Port_RandoFileMenu_SeedEdited();
                    Port_RandoFileMenu_CommitAndStart();
                }
                if (ImGui::IsItemEdited())
                    Port_RandoFileMenu_SeedEdited();
                ImGui::SameLine();
                if (ImGui::Button("Randomize"))
                    Port_RandoFileMenu_RandomizeSeed();

                ImGui::Spacing();
                ImGui::TextDisabled("Logic: default.logic");
                int difficulty = Port_RandoFileMenu_Difficulty();
                ImGui::SetNextItemWidth(160);
                if (ImGui::Combo("Item pool", &difficulty, kRandoPoolCombo, RANDO_ITEM_POOL_COUNT)) {
                    Port_RandoFileMenu_SetDifficulty(difficulty);
                }
                RandoUi_HelpTooltip(kRandoPoolTooltip);
                ImGui::Checkbox("Glitchless logic", Port_RandoFileMenu_GlitchlessLogic());
                ImGui::SameLine();
                ImGui::Checkbox("Obscure spots", Port_RandoFileMenu_ObscureLocations());
                ImGui::SameLine();
                ImGui::Checkbox("Kinstones", Port_RandoFileMenu_ShuffleKinstones());
                ImGui::SameLine();
                ImGui::Checkbox("Entrances", Port_RandoFileMenu_ShuffleEntrances());
                ImGui::SameLine();
                ImGui::Checkbox("Dojos", Port_RandoFileMenu_ShuffleDojos());
                ImGui::Checkbox("Dungeon items", Port_RandoFileMenu_ShuffleDungeonItems());
                RandoUi_HelpTooltip("Off: dungeon items stay in their own dungeons. On: "
                                    "keys, maps, compasses and big keys can appear anywhere.");
                ImGui::Checkbox("Open world", Port_RandoFileMenu_OpenWorld());
                RandoUi_HelpTooltip("Every permanent obstacle (trees, cracked blocks, bomb "
                                    "walls, switches, non-key doors, ...) starts pre-solved, "
                                    "matching the GBA randomizer's World Settings \"Open\".");
                ImGui::SameLine();
                ImGui::Checkbox("Sleep warp", Port_RandoFileMenu_Homewarp());
                ImGui::Checkbox("Start Sword", Port_RandoFileMenu_StartSword());
                ImGui::SameLine();
                ImGui::Checkbox("Early Crests", Port_RandoFileMenu_EarlyCrests());
                ImGui::SameLine();
                ImGui::Checkbox("Fast Text", Port_RandoFileMenu_InstantText());

                static const char* kTunicColors[] = { "Green", "Red", "Blue", "Purple", "Orange", "Grey", "Random" };
                static const char* kHeartColors[] = { "Red", "Blue", "Green", "Yellow", "Purple", "Rainbow", "Random" };
                ImGui::SetNextItemWidth(160);
                ImGui::Combo("Tunic color", Port_RandoFileMenu_TunicColor(), kTunicColors, 7);
                ImGui::SetNextItemWidth(160);
                ImGui::Combo("Heart color", Port_RandoFileMenu_HeartColor(), kHeartColors, 7);

                ImGui::SetNextItemWidth(160);
                ImGui::Combo("Accessibility", Port_RandoFileMenu_Accessibility(), kRandoAccessCombo,
                             RANDO_ACCESS_COUNT);
                RandoUi_HelpTooltip(kRandoAccessTooltip);
                if (!*Port_RandoFileMenu_GlitchlessLogic()) {
                    ImGui::CheckboxFlags(kRandoTrickOcarina, Port_RandoFileMenu_Tricks(), RANDO_TRICK_OCARINA_GLITCH);
                    ImGui::CheckboxFlags(kRandoTrickCrenel, Port_RandoFileMenu_Tricks(), RANDO_TRICK_CRENEL_CLIP);
                    ImGui::CheckboxFlags(kRandoTrickPjs, Port_RandoFileMenu_Tricks(), RANDO_TRICK_PORTAL_JUMP_STORAGE);
                    RandoUi_HelpTooltip(kRandoTrickTooltip);
                }

                ImGui::Spacing();
                const char* status = Port_RandoFileMenu_Status();
                if (status[0]) {
                    ImGui::TextColored(ImVec4(1.0f, 0.44f, 0.44f, 1.0f), "%s", status);
                }

                {
                    char sfp[16];
                    std::snprintf(sfp, sizeof(sfp), "%08X", Port_RandoFileMenu_Fingerprint());
                    ImGui::Text("Menu settings hash: %s", sfp);
                    RandoUi_HelpTooltip("The exact logic fingerprint appears after generation.");
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Copy##fpsidebar")) {
                        ImGui::SetClipboardText(sfp);
                    }
                }

                if (forceOpen) {
                    /* Only show Generate/Cancel actions when the GBA state is actively
                     * waiting for input on a new file creation slot. */
                    const float actionW = (sidebarW - 32.0f) / 2.0f;
                    if (ImGui::Button("Generate & Start", ImVec2(actionW, 0))) {
                        Port_RandoFileMenu_CommitAndStart();
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Cancel", ImVec2(actionW, 0))) {
                        Port_RandoFileMenu_Cancel();
                    }
                    ImGui::TextDisabled("Enter starts   Esc / Gamepad B cancels");
                } else {
                    ImGui::TextDisabled("Select an empty save slot to generate and start.");
                }
            }
        } else {
            ImGui::TextDisabled("Randomizer: disabled (Vanilla game).");
        }

        // 2. GENERAL PORT SETTINGS (Always available)
        if (ImGui::CollapsingHeader("Display & Video")) {
            DrawRibbonDisplayTab();
        }
        if (ImGui::CollapsingHeader("Audio & Sound")) {
            DrawRibbonAudioTab();
        }
        if (ImGui::CollapsingHeader("Save Profiles")) {
            DrawRibbonProfilesTab();
        }
        if (ImGui::CollapsingHeader("Accessibility")) {
            DrawRibbonAccessibilityTab();
        }

        /* Close via Escape / Gamepad B. The manual sidebar additionally
         * closes on a second press of the GBA L button, but that press is
         * masked here (port_bios.c swallows game input while the menu is
         * open) so it is handled in Port_PumpEvents instead. */
        const bool popupOpen = ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);

        if (!popupOpen && !ImGui::IsAnyItemActive() && !ImGui::IsAnyItemFocused()) {
            const bool esc_pressed =
                ImGui::IsKeyPressed(ImGuiKey_Escape, false) || ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false);

            if (esc_pressed) {
                if (forceOpen) {
                    Port_RandoFileMenu_Cancel();
                } else {
                    Port_RandoFileMenu_SetSidebarOpen(false);
                    Rando_PlayCancelSfx();
                }
            }
        }

        if (forceOpen) {
            if (Port_RandoFileMenu_IsOpen() && !popupOpen && !ImGui::IsAnyItemActive() && !ImGui::IsAnyItemFocused() &&
                (ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false))) {
                Port_RandoFileMenu_CommitAndStart();
            }
        } else {
            /* Close button for the sidebar when opened manually */
            if (ImGui::Button("Close Sidebar", ImVec2(-1, 30))) {
                Port_RandoFileMenu_SetSidebarOpen(false);
                Rando_PlayCancelSfx();
            }
        }
    }
    ImGui::End();
}

extern "C" bool Port_ImGui_Render(void) {
    if (!sImGuiInited)
        return false;
    /* SDL_Renderer path needs a renderer; SDL_GPU path runs with
     * sRenderer == nullptr (NewFrame uses ImGui_ImplSDLGPU3 instead
     * and PresentFrame consumes the draw data via the *_Gpu helpers
     * below). */
#ifndef TMC_GPU_RENDERER
    if (!sRenderer)
        return false;
#endif

    /* Gamepad nav gated on overlay-open state. When no overlay is open,
     * ImGui must NOT consume gamepad input — otherwise the focus-by-
     * default behaviour grabs the persistent MENU trigger and the
     * player's A press opens the menu instead of attacking. Toggle the
     * flag each frame so transitions are immediate. The file-select
     * randomizer modal counts too: it is gamepad-navigated while the
     * game's KEYINPUT is masked by port_bios.c. */
    static bool sPrevMenuOpen = false;
    const bool menuOpen = Port_DebugMenu_IsOpen();
    const bool navWanted = menuOpen || Port_RandoFileMenu_IsOpen();
    {
        ImGuiIO& io = ImGui::GetIO();
        if (navWanted) {
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

    /* Soft-slot config overlay — replaces the SDL_Renderer-only popup
     * from port_softslots.c with an ImGui equivalent so it works on
     * both backends (the GPU path has no SDL_Renderer to draw the
     * legacy version into). Centered modal-style window; closes via
     * Enter/Escape, which Port_SoftSlots_HandleConfigKey already
     * handles independently. */
    extern bool Port_SoftSlots_ConfigIsOpen(void);
    extern const char* Port_SoftSlots_GetSlotLabel(int slot);
    extern void Port_SoftSlots_CycleAssignment(int slot, int direction);
    extern void Port_SoftSlots_ConfigClose(void);
    if (Port_SoftSlots_ConfigIsOpen()) {
        const ImGuiViewport* vp = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(ImVec2(vp->Pos.x + vp->Size.x * 0.5f, vp->Pos.y + vp->Size.y * 0.5f), ImGuiCond_Always,
                                ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(ImVec2(380, 0));
        if (ImGui::Begin("##softslot_config", nullptr,
                         ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                             ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoSavedSettings)) {
            ImGui::TextColored(ImVec4(0.78f, 0.86f, 1.0f, 1.0f), "EXTRA EQUIP SLOTS");
            ImGui::Separator();
            for (int s = 0; s < 4; ++s) {
                ImGui::PushID(s);
                ImGui::Text("%s", Port_SoftSlots_GetSlotLabel(s));
                ImGui::SameLine(260.0f);
                DrawSoftSlotCycleButtons(s);
                ImGui::PopID();
            }
            ImGui::Separator();
            ImGui::TextDisabled("Up/Down pick   Left/Right cycle   Enter/Esc done");
            if (ImGui::IsKeyPressed(ImGuiKey_Escape) || ImGui::IsKeyPressed(ImGuiKey_Enter)) {
                Port_SoftSlots_ConfigClose();
            }
        }
        ImGui::End();
    }

    /* File-select randomizer setup modal — drawn here (per-frame ImGui
     * pass) so it presents on every backend, independent of the F8 menu. */
    DrawRandoFileMenuModal();

    /* File-select discoverability: the L button opens the Port & Randomizer
     * setup sidebar, but nothing on the vanilla file screen says so. Show a
     * small bottom hint whenever we're on the file screen with the sidebar
     * closed and the L gate enabled. */
    if (Rando_IsInFileSelect() && !Port_RandoFileMenu_IsSidebarOpen() && !Port_RandoFileMenu_IsModalOpen() &&
        Port_Config_PortSettingsMenuEnabled()) {
        const ImGuiViewport* fvp = ImGui::GetMainViewport();
        ImGui::SetNextWindowBgAlpha(0.55f);
        ImGui::SetNextWindowPos(ImVec2(fvp->Pos.x + fvp->Size.x * 0.5f, fvp->Pos.y + fvp->Size.y - 10.0f),
                                ImGuiCond_Always, ImVec2(0.5f, 1.0f));
        if (ImGui::Begin("##rando_l_hint", nullptr,
                         ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoNav |
                             ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextColored(ImVec4(0.85f, 0.95f, 0.85f, 1.0f), "Press  L  for Port & Randomizer setup");
        }
        ImGui::End();
    }

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
            /* Classic mode has no ribbon footer, so without this it would be a
             * one-way trap. Offer an explicit way back to ribbon mode. */
            ImGui::SetNextWindowBgAlpha(0.85f);
            if (ImGui::Begin("##classic_to_ribbon", nullptr,
                             ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize |
                                 ImGuiWindowFlags_NoSavedSettings)) {
                if (ImGui::SmallButton("Switch to ribbon mode")) {
                    sRibbonEnabled = true;
                    Port_Config_SetRibbonEnabled(true); /* persist (#146) */
                }
            }
            ImGui::End();
        }
    }

    /* Future-friendly: per-frame focus reader hook. ImGui doesn't
     * expose a label-string from the focus ID (labels are hashed
     * into IDs at widget time) so the per-tab handlers call
     * Port_TTS_OnFocusChanged manually for each row when they want
     * announcements. This block is intentionally left empty for
     * now — keep the slot reserved next to Render() so future
     * work that DOES carry labels through DataID has an obvious
     * place to plug in. */

    DrawRandoTrackerOverlay();
    DrawPracticeOverlay();
    DrawFpsOverlay();
#ifdef TMC_RA
    Port_RA_UI_DrawOverlay();
#endif
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
    if (sRenderer) {
        int w = 0, h = 0;
        SDL_GetWindowSize(sWindow, &w, &h);
        Port_LevelEditor_Render(sRenderer, w, h);
    }
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
    if (!sImGuiInited)
        return;
    if (sRenderer != nullptr)
        return; /* SDL_Renderer path doesn't use this */
    ImDrawData* dd = ImGui::GetDrawData();
    if (!dd)
        return;
    ImGui_ImplSDLGPU3_PrepareDrawData(dd, cmd);
}

extern "C" void Port_ImGui_RenderDrawDataGpu(SDL_GPUCommandBuffer* cmd, SDL_GPURenderPass* rp) {
    if (!sImGuiInited)
        return;
    if (sRenderer != nullptr)
        return;
    ImDrawData* dd = ImGui::GetDrawData();
    if (!dd)
        return;
    ImGui_ImplSDLGPU3_RenderDrawData(dd, cmd, rp, /*pipeline=*/nullptr);
}
#endif

/* Project Picori prelaunch — builds and presents a centred ImGui
 * card with embedded logo, title / subtitle, version, ROM filename,
 * and Play / Change-ROM buttons. Returns false if ImGui isn't ready
 * (caller falls back to the plain boot splash).
 *
 * Button presses are reported through the out_play / out_change_rom
 * pointers (caller may pass NULL to ignore). On the SDL_Renderer
 * backend, presents the frame inline. On the SDL_GPU backend, builds
 * + Render()s the draw data and returns true — the caller must follow
 * up with Port_GPU_PresentPrelaunchFrame() to present it. */
/* First-launch asset-extraction progress screen for the SDL_GPU backend.
 * The SDL_Renderer path draws DrawProgressScreen (port_asset_bootstrap.cpp);
 * GPU builds have no SDL_Renderer, so they previously extracted with no UI
 * and the window looked hung. This builds + renders one ImGui frame (same
 * NewFrame structure as the prelaunch card) so it can be presented on the GPU
 * swapchain via Port_GPU_PresentPrelaunchFrame. Returns true when draw data
 * is ready to present. `fraction` is 0..1; `phase` is the current phase name. */
extern "C" bool Port_ImGui_RenderExtractProgress(const char* phase, float fraction, int phase_index, int phase_total) {
    if (!sImGuiInited)
        return false;

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

    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const ImVec2 center(vp->WorkPos.x + vp->WorkSize.x * 0.5f, vp->WorkPos.y + vp->WorkSize.y * 0.5f);
    ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(440, 0), ImGuiCond_Always);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(28, 24));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 12.0f);
    if (ImGui::Begin("##extract_progress", nullptr,
                     ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                         ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings |
                         ImGuiWindowFlags_NoScrollbar)) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.40f, 0.72f, 0.46f, 1.00f));
        ImGui::SetWindowFontScale(1.6f);
        ImGui::TextUnformatted("Extracting game assets");
        ImGui::SetWindowFontScale(1.0f);
        ImGui::PopStyleColor();

        ImGui::Dummy(ImVec2(0, 6));
        float frac = fraction < 0.0f ? 0.0f : (fraction > 1.0f ? 1.0f : fraction);
        ImGui::ProgressBar(frac, ImVec2(-1.0f, 0.0f));
        ImGui::Dummy(ImVec2(0, 4));

        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.70f, 0.78f, 0.70f, 1.00f));
        ImGui::Text("loading %s   (phase %d/%d)", (phase && phase[0]) ? phase : "preparing", phase_index, phase_total);
        ImGui::PopStyleColor();
#ifdef __ANDROID__
        ImGui::TextDisabled("One-time setup - this can take a minute on first launch.");
#else
        ImGui::TextDisabled("One-time first-launch extraction. See terminal for detail.");
#endif
    }
    ImGui::End();
    ImGui::PopStyleVar(2);

    ImGui::Render();
    return true;
}

/* Horizontally center a single line of text in the current window. */
static void CenteredText(const char* t) {
    ImGui::SetCursorPosX((ImGui::GetWindowSize().x - ImGui::CalcTextSize(t).x) * 0.5f);
    ImGui::TextUnformatted(t);
}

extern "C" bool Port_ImGui_RenderPrelaunch(bool rom_present, const char* version, const char* rom_name, bool* out_play,
                                           bool* out_change_rom) {
    if (out_play)
        *out_play = false;
    if (out_change_rom)
        *out_change_rom = false;
    if (!sImGuiInited)
        return false;

#ifdef TMC_GPU_RENDERER
    const bool gpuBackend = (sRenderer == nullptr);
    Port_PrelaunchLogo_EnsureLoaded(sRenderer, gpuBackend ? Port_GPU_GetDevice() : nullptr);
    if (gpuBackend) {
        ImGui_ImplSDLGPU3_NewFrame();
    } else
#else
    Port_PrelaunchLogo_EnsureLoaded(sRenderer, nullptr);
#endif
    {
        ImGui_ImplSDLRenderer3_NewFrame();
    }
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();

    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const ImVec2 viewport_center(vp->WorkPos.x + vp->WorkSize.x * 0.5f, vp->WorkPos.y + vp->WorkSize.y * 0.5f);
    ImGui::SetNextWindowPos(viewport_center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(620, 0), ImGuiCond_Always);
    /* Cap the card's auto-height to the visible work area so a small or
     * default-sized window never pushes the Select ROM / Play buttons
     * off-screen; with the scrollbar enabled (below) they stay reachable
     * without having to resize the window first (v0.6 oversight). */
    ImGui::SetNextWindowSizeConstraints(ImVec2(620, 0.0f), ImVec2(620, vp->WorkSize.y));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(36, 32));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 14.0f);
    if (ImGui::Begin("##prelaunch", nullptr,
                     ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                         ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings)) {
        const float win_w = ImGui::GetWindowSize().x;
        const ImVec4 accent(0.40f, 0.72f, 0.46f, 1.00f);
        const ImVec4 subtxt(0.70f, 0.78f, 0.70f, 1.00f);

        /* Logo centred at top; skipped if it failed to load. */
        const ImTextureID logo_tex = Port_PrelaunchLogo_GetTexId();
        if (logo_tex != 0) {
            const float DISPLAY = 160.0f;
            ImGui::SetCursorPosX((win_w - DISPLAY) * 0.5f);
            ImGui::Image(logo_tex, ImVec2(DISPLAY, DISPLAY));
            ImGui::Dummy(ImVec2(0, 8));
        }

        ImGui::PushStyleColor(ImGuiCol_Text, accent);
        ImGui::SetWindowFontScale(2.4f);
        CenteredText("PROJECT PICORI");
        ImGui::SetWindowFontScale(1.0f);
        ImGui::PopStyleColor();

        ImGui::PushStyleColor(ImGuiCol_Text, subtxt);
        CenteredText("Minish Cap PC Port");
        ImGui::PopStyleColor();

        ImGui::Dummy(ImVec2(0, 16));
        ImGui::Separator();
        ImGui::Dummy(ImVec2(0, 14));

        if (rom_present) {
            ImGui::PushStyleColor(ImGuiCol_Text, subtxt);
            ImGui::TextUnformatted("Version");
            ImGui::PopStyleColor();
            ImGui::SameLine(170.0f);
            ImGui::TextUnformatted(version ? version : "?");

            ImGui::PushStyleColor(ImGuiCol_Text, subtxt);
            ImGui::TextUnformatted("ROM");
            ImGui::PopStyleColor();
            ImGui::SameLine(170.0f);
            ImGui::TextUnformatted(rom_name ? rom_name : "?");
            ImGui::SameLine();
            /* Right-align the Change-ROM button to the edge of the card. */
            {
                const char* lbl = "Change ROM...";
                float bw = ImGui::CalcTextSize(lbl).x + ImGui::GetStyle().FramePadding.x * 2.0f;
                float pad = ImGui::GetStyle().WindowPadding.x;
                ImGui::SameLine(win_w - pad - bw);
                if (ImGui::Button(lbl)) {
                    if (out_change_rom)
                        *out_change_rom = true;
                }
            }
        } else {
            /* First-launch / missing-ROM state: dominate the card with a
             * "Select your Minish Cap ROM" prompt + big button. No Play
             * yet — there's nothing to play. */
            ImGui::PushStyleColor(ImGuiCol_Text, subtxt);
            CenteredText("No ROM found.");
            CenteredText("Project Picori needs your own Minish Cap dump (.gba).");
            CenteredText("We identify it by SHA-1 - filename is irrelevant.");
            ImGui::PopStyleColor();
        }
        ImGui::Dummy(ImVec2(0, 14));
        (void)DrawRegionLanguageControls(true);

        /* Persisted opt-out for this screen itself: writes config.json's
         * "skip_launcher" immediately on toggle (same effect as
         * --skip-launcher / TMC_AUTOPLAY=1, applied on the *next* run —
         * see docs/env-vars.md). */
        {
            ImGui::Dummy(ImVec2(0, 10));
            ImGui::SeparatorText("Additional Settings");
            bool skipLauncher = Port_Config_GetSkipLauncher();
            ImGui::PushStyleColor(ImGuiCol_Text, subtxt);
            if (ImGui::Checkbox("Skip launcher on future runs", &skipLauncher)) {
                Port_Config_SetSkipLauncher(skipLauncher);
            }
            ImGui::PopStyleColor();
        }

        ImGui::Dummy(ImVec2(0, 22));

        /* Big centred action button: Play when a ROM is loaded, Select
         * ROM when none. Enter / Space activates whichever is shown. */
        {
            const bool is_select = !rom_present;
            const char* lbl = is_select ? "Select ROM..." : "Play";
            const ImVec2 sz(is_select ? 260.0f : 220.0f, 48.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 12.0f);
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.42f, 0.24f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.28f, 0.55f, 0.34f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.40f, 0.72f, 0.46f, 1.0f));
            ImGui::SetCursorPosX((win_w - sz.x) * 0.5f);
            ImGui::SetWindowFontScale(1.4f);
            const bool clicked = ImGui::Button(lbl, sz) || ImGui::IsKeyPressed(ImGuiKey_Enter) ||
                                 ImGui::IsKeyPressed(ImGuiKey_KeypadEnter) || ImGui::IsKeyPressed(ImGuiKey_Space);
            if (clicked) {
                if (is_select) {
                    if (out_change_rom)
                        *out_change_rom = true;
                } else {
                    if (out_play)
                        *out_play = true;
                }
            }
            ImGui::SetWindowFontScale(1.0f);
            ImGui::PopStyleColor(3);
            ImGui::PopStyleVar();
        }

        ImGui::Dummy(ImVec2(0, 6));
        ImGui::PushStyleColor(ImGuiCol_Text, subtxt);
        CenteredText(rom_present ? "Press Enter or click Play to start"
                                 : "Press Enter or click to pick your .gba file");
        ImGui::PopStyleColor();
    }
    ImGui::End();
    ImGui::PopStyleVar(2);

    ImGui::Render();

#ifdef TMC_GPU_RENDERER
    if (gpuBackend) {
        return true;
    }
#endif
    if (sRenderer) {
        SDL_SetRenderDrawColor(sRenderer, 15, 18, 18, 255);
        SDL_RenderClear(sRenderer);
        ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), sRenderer);
        SDL_RenderPresent(sRenderer);
    }
    return true;
}
