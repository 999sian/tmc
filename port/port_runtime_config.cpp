#include "port_runtime_config.h"
#include "port_touch_controls.h"

#include <SDL3/SDL.h>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace {

struct Bind {
    SDL_Keycode key = SDLK_UNKNOWN;
    SDL_GamepadButton pad = SDL_GAMEPAD_BUTTON_INVALID;
    /* Triggers (L2/R2 on Xbox/PS pads) are reported by SDL as analog axes,
     * not buttons, so the bind table allows binding to an axis. A value
     * past kAxisThreshold counts as "pressed". */
    SDL_GamepadAxis axis = SDL_GAMEPAD_AXIS_INVALID;
};

constexpr Sint16 kAxisThreshold = 16384;

struct Def {
    PortInput input;
    const char* name;
    std::initializer_list<const char*> binds;
};

const std::array<Def, PORT_INPUT_COUNT> kDefaults = {{
    { PORT_INPUT_A, "a", { "SDLK:0x00000078", "SDL_GAMEPAD:0x00000000" } },
    { PORT_INPUT_B, "b", { "SDLK:0x0000007a", "SDL_GAMEPAD:0x00000001" } },
    { PORT_INPUT_SELECT, "select", { "SDLK:0x00000008", "SDL_GAMEPAD:0x00000004" } },
    { PORT_INPUT_START, "start", { "SDLK:0x0000000d", "SDL_GAMEPAD:0x00000006" } },
    { PORT_INPUT_RIGHT, "right", { "SDLK:0x4000004f", "SDL_GAMEPAD:0x0000000e" } },
    { PORT_INPUT_LEFT, "left", { "SDLK:0x40000050", "SDL_GAMEPAD:0x0000000d" } },
    { PORT_INPUT_UP, "up", { "SDLK:0x40000052", "SDL_GAMEPAD:0x0000000b" } },
    { PORT_INPUT_DOWN, "down", { "SDLK:0x40000051", "SDL_GAMEPAD:0x0000000c" } },
    { PORT_INPUT_R, "r", { "SDLK:0x00000073", "SDL_GAMEPAD:0x0000000a" } },
    { PORT_INPUT_L, "l", { "SDLK:0x00000061", "SDL_GAMEPAD:0x00000009" } },
    /* Soft-slots: keyboard CV/QE + face-buttons WEST/NORTH + triggers L2/R2.
     * Numeric values: SDL_GAMEPAD_BUTTON_WEST=2, NORTH=3,
     * SDL_GAMEPAD_AXIS_LEFT_TRIGGER=4, RIGHT_TRIGGER=5. */
    { PORT_INPUT_SOFT_X,  "soft_x",  { "SDLK:0x00000063", "SDL_GAMEPAD:0x00000002" } },
    { PORT_INPUT_SOFT_Y,  "soft_y",  { "SDLK:0x00000076", "SDL_GAMEPAD:0x00000003" } },
    { PORT_INPUT_SOFT_L2, "soft_l2", { "SDLK:0x00000071", "SDL_AXIS:0x00000004" } },
    { PORT_INPUT_SOFT_R2, "soft_r2", { "SDLK:0x00000065", "SDL_AXIS:0x00000005" } },
}};

u8 sScale = 3;
u8 sInternalScale = 1;
std::string sUpscaleMethod = "nearest";
u64 sFrameTimeNs = 0;
bool sPortSettingsMenuEnabled = true;
/* Persisted active save-profile filename. Defaults to tmc.sav. The
 * port_save.c EEPROM emulation reads/writes this path; the F8 debug
 * menu's "Save profiles" page switches it. */
std::string sActiveSaveProfile = "tmc.sav";
/* Persisted auto-save settings. On by default — protects new players
 * from losing progress after a crash, since most TMC saves rely on
 * the in-game tetra statue / file-save flow that requires getting
 * back to a save point first. */
bool sAutosaveEnabled = true;
u32  sAutosaveIntervalMs = 60000;
/* Touch input scheme from matheo's launcher integration — kept for
 * Android compatibility. */
PortTouchScheme sTouchScheme = PORT_TOUCH_SCHEME_JOYSTICK;
/* Widescreen pillarbox config — applied in port_ppu.cpp's present path.
 * Defaults reproduce the historical behavior: GBA 3:2 frame fills as
 * much of the window as possible, side bars are black. */
PortAspectMode sAspectMode = PORT_ASPECT_NATIVE_3_2;
PortBgFill     sBgFill = PORT_BG_FILL_BLACK;
u8 sBgFillR = 0, sBgFillG = 0, sBgFillB = 0;
/* Renderer backend selection — read once at PPU init; menu writes
 * here but a restart is needed to apply because the window's
 * swapchain owner is set once and is not live-switchable. */
PortRenderBackend sRenderBackend = PORT_RENDER_BACKEND_AUTO;
bool        sTtsEnabled  = false;
float       sTtsRate     = 0.5f;
float       sTtsPitch    = 0.5f;
float       sTtsVolume   = 0.8f;
std::string sTtsVoice;
std::string sTtsLanguage;
std::array<std::vector<Bind>, PORT_INPUT_COUNT> sBinds;
/* Rebind capture state. -1 = not capturing; otherwise the PortInput
 * whose next key/button/axis press becomes a new binding. The ImGui
 * "Controls" tab toggles this and shows a modal "Press a key..." popup
 * while it's non-negative. */
int sCapturingInput = -1;
/* Edge-detection cache. Set when the corresponding SDL key/button event
 * arrives during the frame; cleared by Port_Config_ClearInputEdges()
 * after KEYINPUT is committed. Catches sub-frame taps (press+release
 * between two polls) that the polled-state path would otherwise miss
 * — useful for frame-perfect rolls / spin-attack inputs. */
std::array<bool, PORT_INPUT_COUNT> sEdgePressed{};
std::vector<SDL_Gamepad*> sPads;
std::filesystem::path sConfigPath = "config.json";
nlohmann::json sConfigJson;
const std::array<u32, 9> kFpsPresets = { 0, 30, 60, 75, 90, 120, 144, 150, 240 };
/* Default cap when config omits frame_time_ns (1e9 ns / 60 Hz). */
constexpr u64 kDefaultFrameTimeNs = 1000000000ULL / 60;

nlohmann::json DefaultsJson(void) {
    nlohmann::json j = {
        { "window_scale", 3 },
        { "internal_scale", 1 },
        { "upscale_method", "nearest" },
        { "frame_time_ns", 0 },
        { "port_settings_menu", true },
        { "active_save_profile", "tmc.sav" },
        { "autosave_enabled", true },
        { "autosave_interval_ms", 60000 },
        { "touch_scheme", "joystick" },
        { "aspect_mode", "native" },
        { "bg_fill", "black" },
        { "bg_fill_color", { 0, 0, 0 } },
        { "render_backend", "auto" },
        /* TTS accessibility — default OFF (privacy + voice synthesis
         * isn't every user's preference). Rate/pitch/volume in [0,1];
         * voice/language empty = backend default. */
        { "tts_enabled", false },
        { "tts_rate", 0.5 },
        { "tts_pitch", 0.5 },
        { "tts_volume", 0.8 },
        { "tts_voice", "" },
        { "tts_language", "" },
        { "bindings", nlohmann::json::object() },
    };
    for (const auto& d : kDefaults) {
        j["bindings"][d.name] = nlohmann::json::array();
        for (const char* bind : d.binds) {
            j["bindings"][d.name].push_back(bind);
        }
    }
    return j;
}

void AddBind(PortInput input, const std::string& name) {
    Bind b;
    if (name.rfind("SDLK:", 0) == 0) {
        b.key = static_cast<SDL_Keycode>(std::strtoul(name.c_str() + 5, nullptr, 0));
        sBinds[input].push_back(b);
    } else if (name.rfind("SDL_GAMEPAD:", 0) == 0) {
        b.pad = static_cast<SDL_GamepadButton>(std::strtoul(name.c_str() + 12, nullptr, 0));
        sBinds[input].push_back(b);
    } else if (name.rfind("SDL_AXIS:", 0) == 0) {
        b.axis = static_cast<SDL_GamepadAxis>(std::strtoul(name.c_str() + 9, nullptr, 0));
        sBinds[input].push_back(b);
    }
}

void LoadBinds(PortInput input, const nlohmann::json& v) {
    if (v.is_string()) {
        AddBind(input, v.get<std::string>());
    } else if (v.is_array()) {
        for (const auto& it : v) {
            if (it.is_string()) {
                AddBind(input, it.get<std::string>());
            }
        }
    }
}

void SaveConfig(void) {
    try {
        std::ofstream(sConfigPath) << sConfigJson.dump(4) << '\n';
    } catch (...) {
    }
}

u64 FrameTimeForFps(u32 fps) {
    if (fps == 0) {
        return 0;
    }
    if (fps > 1000) {
        fps = 1000;
    }
    return 1000000000ULL / fps;
}

#ifdef launcher
static std::string SerializeBindToken(const Bind& b) {
    char buf[96];
    if (b.key != SDLK_UNKNOWN) {
        std::snprintf(buf, sizeof(buf), "SDLK:0x%08x", static_cast<unsigned>(b.key));
        return buf;
    }
    if (b.pad != SDL_GAMEPAD_BUTTON_INVALID) {
        std::snprintf(buf, sizeof(buf), "SDL_GAMEPAD:0x%08x", static_cast<unsigned>(b.pad));
        return buf;
    }
    if (b.axis != SDL_GAMEPAD_AXIS_INVALID) {
        std::snprintf(buf, sizeof(buf), "SDL_AXIS:0x%08x", static_cast<unsigned>(b.axis));
        return buf;
    }
    return {};
}

static void WriteBindingsJsonFromState(void) {
    for (const auto& d : kDefaults) {
        nlohmann::json arr = nlohmann::json::array();
        for (const Bind& b : sBinds[d.input]) {
            std::string tok = SerializeBindToken(b);
            if (!tok.empty()) {
                arr.push_back(std::move(tok));
            }
        }
        sConfigJson["bindings"][d.name] = std::move(arr);
    }
    SaveConfig();
}

extern "C" void Port_Config_SetPortSettingsMenuEnabled(bool enabled) {
    sPortSettingsMenuEnabled = enabled;
    sConfigJson["port_settings_menu"] = enabled;
    SaveConfig();
}

extern "C" const char* Port_Config_InputUiLabel(PortInput input) {
    static const char* const kLabels[PORT_INPUT_COUNT] = {
        "A (jump / talk)",
        "B (attack / cancel)",
        "Select",
        "Start",
        "D-pad Right",
        "D-pad Left",
        "D-pad Up",
        "D-pad Down",
        "R",
        "L",
        "Soft slot X",
        "Soft slot Y",
        "Soft slot L2 / LT",
        "Soft slot R2 / RT",
    };
    if (input < 0 || input >= PORT_INPUT_COUNT) {
        return "?";
    }
    return kLabels[input];
}

extern "C" void Port_Config_FormatBindingsLine(PortInput input, char* out, size_t outCap) {
    if (!out || outCap == 0) {
        return;
    }
    out[0] = '\0';
    if (input < 0 || input >= PORT_INPUT_COUNT) {
        return;
    }
    size_t pos = 0;
    bool first = true;
    for (const Bind& b : sBinds[input]) {
        char piece[112];
        piece[0] = '\0';
        if (b.key != SDLK_UNKNOWN) {
            const char* nm = SDL_GetKeyName(b.key);
            if (nm && nm[0] != '\0') {
                std::snprintf(piece, sizeof(piece), "%s", nm);
            } else {
                std::snprintf(piece, sizeof(piece), "key 0x%x", static_cast<unsigned>(b.key));
            }
        } else if (b.pad != SDL_GAMEPAD_BUTTON_INVALID) {
            const char* nm = SDL_GetGamepadStringForButton(b.pad);
            if (nm && nm[0] != '\0') {
                std::snprintf(piece, sizeof(piece), "%s", nm);
            } else {
                std::snprintf(piece, sizeof(piece), "pad btn %u", static_cast<unsigned>(b.pad));
            }
        } else if (b.axis != SDL_GAMEPAD_AXIS_INVALID) {
            const char* nm = SDL_GetGamepadStringForAxis(b.axis);
            if (nm && nm[0] != '\0') {
                std::snprintf(piece, sizeof(piece), "%s", nm);
            } else {
                std::snprintf(piece, sizeof(piece), "axis %u", static_cast<unsigned>(b.axis));
            }
        }
        if (piece[0] == '\0') {
            continue;
        }
        if (!first) {
            if (pos + 2 < outCap) {
                out[pos++] = ',';
                out[pos++] = ' ';
                out[pos] = '\0';
            }
        }
        first = false;
        const size_t plen = std::strlen(piece);
        if (pos + plen >= outCap) {
            break;
        }
        std::memcpy(out + pos, piece, plen);
        pos += plen;
        out[pos] = '\0';
    }
    if (out[0] == '\0' && outCap > 1) {
        std::snprintf(out, outCap, "(none)");
    }
}

extern "C" void Port_Config_SetKeyboardBindExclusive(PortInput input, int sdl_keycode) {
    if (input < 0 || input >= PORT_INPUT_COUNT) {
        return;
    }
    const SDL_Keycode nk = static_cast<SDL_Keycode>(sdl_keycode);
    std::vector<Bind> kept;
    kept.reserve(sBinds[input].size());
    for (const Bind& b : sBinds[input]) {
        if (b.pad != SDL_GAMEPAD_BUTTON_INVALID || b.axis != SDL_GAMEPAD_AXIS_INVALID) {
            kept.push_back(b);
        }
    }
    sBinds[input] = std::move(kept);
    if (nk != SDLK_UNKNOWN) {
        Bind kb;
        kb.key = nk;
        sBinds[input].insert(sBinds[input].begin(), kb);
    }
    WriteBindingsJsonFromState();
}

extern "C" void Port_Config_FormatGamepadBindingsLine(PortInput input, char* out, size_t outCap) {
    if (!out || outCap == 0) {
        return;
    }
    out[0] = '\0';
    if (input < 0 || input >= PORT_INPUT_COUNT) {
        return;
    }
    size_t pos = 0;
    bool first = true;
    for (const Bind& b : sBinds[input]) {
        if (b.key != SDLK_UNKNOWN) continue;
        char piece[112];
        piece[0] = '\0';
        if (b.pad != SDL_GAMEPAD_BUTTON_INVALID) {
            const char* nm = SDL_GetGamepadStringForButton(b.pad);
            if (nm && nm[0] != '\0') {
                std::snprintf(piece, sizeof(piece), "%s", nm);
            } else {
                std::snprintf(piece, sizeof(piece), "pad btn %u", static_cast<unsigned>(b.pad));
            }
        } else if (b.axis != SDL_GAMEPAD_AXIS_INVALID) {
            const char* nm = SDL_GetGamepadStringForAxis(b.axis);
            if (nm && nm[0] != '\0') {
                std::snprintf(piece, sizeof(piece), "%s", nm);
            } else {
                std::snprintf(piece, sizeof(piece), "axis %u", static_cast<unsigned>(b.axis));
            }
        }
        if (piece[0] == '\0') {
            continue;
        }
        if (!first) {
            if (pos + 2 < outCap) {
                out[pos++] = ',';
                out[pos++] = ' ';
                out[pos] = '\0';
            }
        }
        first = false;
        const size_t plen = std::strlen(piece);
        if (pos + plen >= outCap) {
            break;
        }
        std::memcpy(out + pos, piece, plen);
        pos += plen;
        out[pos] = '\0';
    }
    if (out[0] == '\0' && outCap > 1) {
        std::snprintf(out, outCap, "(none)");
    }
}

extern "C" void Port_Config_SetGamepadBindExclusive(PortInput input, int sdl_gamepad_button) {
    if (input < 0 || input >= PORT_INPUT_COUNT) {
        return;
    }
    const SDL_GamepadButton nb = static_cast<SDL_GamepadButton>(sdl_gamepad_button);
    std::vector<Bind> kept;
    kept.reserve(sBinds[input].size());
    for (const Bind& b : sBinds[input]) {
        if (b.pad == SDL_GAMEPAD_BUTTON_INVALID) {
            kept.push_back(b);
        }
    }
    sBinds[input] = std::move(kept);
    if (nb != SDL_GAMEPAD_BUTTON_INVALID && nb >= 0 && nb < SDL_GAMEPAD_BUTTON_COUNT) {
        Bind pb;
        pb.pad = nb;
        sBinds[input].insert(sBinds[input].begin(), pb);
    }
    WriteBindingsJsonFromState();
}
#endif

} 

static SDL_Gamepad* OpenGamepad(SDL_JoystickID id) {
    for (SDL_Gamepad* pad : sPads) {
        if (SDL_GetGamepadID(pad) == id) {
            return pad;
        }
    }
    if (!SDL_IsGamepad(id)) {
        SDL_Log("SDL device is not a gamepad: %s", SDL_GetGamepadNameForID(id));
        return nullptr;
    }
    SDL_Gamepad* pad = SDL_OpenGamepad(id);
    SDL_Log("Gamepad %s: %s", pad ? "connected" : "open failed", SDL_GetGamepadNameForID(id));
    if (pad) {
        sPads.push_back(pad);
    }
    return pad;
}

static void CloseGamepad(SDL_JoystickID id) {
    for (auto it = sPads.begin(); it != sPads.end(); ++it) {
        if (SDL_GetGamepadID(*it) == id) {
            SDL_Log("Gamepad disconnected: %s", SDL_GetGamepadName(*it));
            SDL_CloseGamepad(*it);
            sPads.erase(it);
            return;
        }
    }
}

extern "C" void Port_Config_Load(const char* path) {
    nlohmann::json j = DefaultsJson();
    const std::filesystem::path p = path ? path : "config.json";
    sConfigPath = p;

    if (std::filesystem::exists(p)) {
        try {
            std::ifstream(p) >> j;
        } catch (...) {
            j = DefaultsJson();
        }
    } else {
        std::ofstream(p) << j.dump(4) << '\n';
    }

    sConfigJson = j;

    int scale = j.value("window_scale", 3);
    sScale = scale >= 1 && scale <= 10 ? (u8)scale : 3;
    int iscale = j.value("internal_scale", 1);
    sInternalScale = iscale >= 1 && iscale <= 10 ? (u8)iscale : 1;
    sUpscaleMethod = j.value("upscale_method", "nearest");
    sFrameTimeNs = j.value("frame_time_ns", 0ULL);
    sPortSettingsMenuEnabled = j.value("port_settings_menu", true);
    sActiveSaveProfile = j.value("active_save_profile", std::string("tmc.sav"));
    sAutosaveEnabled = j.value("autosave_enabled", true);
    sAutosaveIntervalMs = j.value("autosave_interval_ms", 60000u);
    {
        std::string ts = j.value("touch_scheme", std::string("joystick"));
        for (char& c : ts) {
            if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
        }
        sTouchScheme = (ts == "dpad") ? PORT_TOUCH_SCHEME_DPAD : PORT_TOUCH_SCHEME_JOYSTICK;
    }
    {
        std::string am = j.value("aspect_mode", std::string("native"));
        for (char& c : am) { if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a'); }
        if      (am == "16:9" || am == "widescreen")       sAspectMode = PORT_ASPECT_WIDESCREEN_16_9;
        else if (am == "21:9" || am == "ultrawide")        sAspectMode = PORT_ASPECT_ULTRAWIDE_21_9;
        else if (am == "32:9" || am == "super_ultrawide")  sAspectMode = PORT_ASPECT_SUPER_ULTRAWIDE_32_9;
        else                                                sAspectMode = PORT_ASPECT_NATIVE_3_2;

        std::string bf = j.value("bg_fill", std::string("black"));
        for (char& c : bf) { if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a'); }
        if      (bf == "solid"   || bf == "solid_color")   sBgFill = PORT_BG_FILL_SOLID_COLOR;
        else if (bf == "blurred" || bf == "blurred_frame") sBgFill = PORT_BG_FILL_BLURRED_FRAME;
        else                                                sBgFill = PORT_BG_FILL_BLACK;

        const auto& col = j.contains("bg_fill_color") ? j["bg_fill_color"] : nlohmann::json::array();
        if (col.is_array() && col.size() >= 3) {
            auto clamp_u8 = [](int v) -> u8 {
                if (v < 0)   return 0;
                if (v > 255) return 255;
                return (u8)v;
            };
            sBgFillR = clamp_u8(col[0].is_number() ? col[0].get<int>() : 0);
            sBgFillG = clamp_u8(col[1].is_number() ? col[1].get<int>() : 0);
            sBgFillB = clamp_u8(col[2].is_number() ? col[2].get<int>() : 0);
        }
    }
    {
        std::string rb = j.value("render_backend", std::string("auto"));
        for (char& c : rb) { if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a'); }
        if      (rb == "software" || rb == "sw")    sRenderBackend = PORT_RENDER_BACKEND_SOFTWARE;
        else if (rb == "gpu")                       sRenderBackend = PORT_RENDER_BACKEND_GPU;
        else                                         sRenderBackend = PORT_RENDER_BACKEND_AUTO;
    }

    /* TTS settings — read-once at Port_TTS_Init; setters from the
     * F8 menu write back via Port_Config_SetTts* below. */
    sTtsEnabled  = j.value("tts_enabled", false);
    sTtsRate     = (float)j.value("tts_rate",   0.5);
    sTtsPitch    = (float)j.value("tts_pitch",  0.5);
    sTtsVolume   = (float)j.value("tts_volume", 0.8);
    sTtsVoice    = j.value("tts_voice",    std::string());
    sTtsLanguage = j.value("tts_language", std::string());

    for (auto& v : sBinds) {
        v.clear();
    }
    nlohmann::json empty = nlohmann::json::object();
    const auto& b = j.contains("bindings") ? j["bindings"] : empty;
    for (const auto& d : kDefaults) {
        LoadBinds(d.input, b.contains(d.name) ? b[d.name] : DefaultsJson()["bindings"][d.name]);
    }
}

extern "C" u8 Port_Config_WindowScale(void) {
    return sScale;
}

extern "C" const char* Port_Config_UpscaleMethod(void) {
    return sUpscaleMethod.c_str();
}

extern "C" u64 Port_Config_FrameTimeNs(void) {
    return sFrameTimeNs;
}

extern "C" u32 Port_Config_TargetFps(void) {
    if (sFrameTimeNs == 0) {
        return 0;
    }
    return (u32)((1000000000ULL + (sFrameTimeNs / 2)) / sFrameTimeNs);
}

extern "C" bool Port_Config_PortSettingsMenuEnabled(void) {
    return sPortSettingsMenuEnabled;
}

extern "C" const char* Port_Config_ActiveSaveProfile(void) {
    return sActiveSaveProfile.c_str();
}

extern "C" void Port_Config_SetActiveSaveProfile(const char* path) {
    if (path == nullptr || path[0] == '\0') {
        path = "tmc.sav";
    }
    sActiveSaveProfile = path;
    sConfigJson["active_save_profile"] = sActiveSaveProfile;
    SaveConfig();
}

extern "C" bool Port_Config_AutosaveEnabled(void) {
    return sAutosaveEnabled;
}

extern "C" void Port_Config_SetAutosaveEnabled(bool enabled) {
    sAutosaveEnabled = enabled;
    sConfigJson["autosave_enabled"] = enabled;
    SaveConfig();
}

extern "C" u32 Port_Config_AutosaveIntervalMs(void) {
    return sAutosaveIntervalMs;
}

extern "C" void Port_Config_SetAutosaveIntervalMs(u32 ms) {
    if (ms < 5000) ms = 5000;
    if (ms > 600000) ms = 600000;
    sAutosaveIntervalMs = ms;
    sConfigJson["autosave_interval_ms"] = ms;
    SaveConfig();
}

extern "C" void Port_Config_SetWindowScale(u8 scale) {
    if (scale < 1) {
        scale = 1;
    } else if (scale > 10) {
        scale = 10;
    }
    sScale = scale;
    sConfigJson["window_scale"] = static_cast<int>(scale);
    SaveConfig();
}

extern "C" u8 Port_Config_InternalScale(void) {
    return sInternalScale;
}

extern "C" void Port_Config_SetInternalScale(u8 scale) {
    /* Cap at 10×. The scaled framebuffer is malloc'd in port_ppu.cpp
       (Port_PPU_BuildScaledFrame) so it's not bounded by the static
       virtuappu_frame_buffer; the affine-OAM overlay patch is already
       parameterised by `scale` and samples sub-pixel per S step. The
       limit is now SDL_Texture max / fill-rate, not memory: at 10×
       the destination is 2400×1600 = 15 MB and ~3.8 Mpx copied per
       frame. */
    if (scale < 1) scale = 1;
    if (scale > 10) scale = 10;
    sInternalScale = scale;
    sConfigJson["internal_scale"] = static_cast<int>(scale);
    SaveConfig();
}

extern "C" void Port_Config_CycleInternalScale(int direction) {
    int next = (int)sInternalScale + (direction < 0 ? -1 : 1);
    if (next < 1) next = 10;
    if (next > 10) next = 1;
    Port_Config_SetInternalScale((u8)next);
}

extern "C" void Port_Config_SetUpscaleMethod(const char* method) {
    if (method == nullptr || method[0] == '\0') {
        method = "nearest";
    }
    sUpscaleMethod = method;
    sConfigJson["upscale_method"] = sUpscaleMethod;
    SaveConfig();
}

extern "C" void Port_Config_SetTargetFps(u32 fps) {
    sFrameTimeNs = FrameTimeForFps(fps);
    sConfigJson["frame_time_ns"] = sFrameTimeNs;
    SaveConfig();
}

extern "C" PortTouchScheme Port_Config_TouchScheme(void) {
    return sTouchScheme;
}

extern "C" void Port_Config_SetTouchScheme(PortTouchScheme scheme) {
    sTouchScheme = (scheme == PORT_TOUCH_SCHEME_DPAD) ? PORT_TOUCH_SCHEME_DPAD : PORT_TOUCH_SCHEME_JOYSTICK;
    sConfigJson["touch_scheme"] = (sTouchScheme == PORT_TOUCH_SCHEME_DPAD) ? "dpad" : "joystick";
    SaveConfig();
}

extern "C" void Port_Config_CycleTouchScheme(int /*direction*/) {
    Port_Config_SetTouchScheme(sTouchScheme == PORT_TOUCH_SCHEME_DPAD ? PORT_TOUCH_SCHEME_JOYSTICK
                                                                      : PORT_TOUCH_SCHEME_DPAD);
}

extern "C" PortAspectMode Port_Config_AspectMode(void) {
    return sAspectMode;
}

extern "C" const char* Port_Config_AspectModeName(PortAspectMode mode) {
    switch (mode) {
        case PORT_ASPECT_WIDESCREEN_16_9:      return "Widescreen 16:9";
        case PORT_ASPECT_ULTRAWIDE_21_9:       return "Ultrawide 21:9";
        case PORT_ASPECT_SUPER_ULTRAWIDE_32_9: return "Super Ultrawide 32:9";
        case PORT_ASPECT_NATIVE_3_2:
        default:                                return "Native 3:2 (GBA)";
    }
}

extern "C" void Port_Config_SetAspectMode(PortAspectMode mode) {
    if (mode < 0 || mode >= PORT_ASPECT_COUNT) mode = PORT_ASPECT_NATIVE_3_2;
    sAspectMode = mode;
    const char* name = "native";
    switch (mode) {
        case PORT_ASPECT_WIDESCREEN_16_9:      name = "16:9"; break;
        case PORT_ASPECT_ULTRAWIDE_21_9:       name = "21:9"; break;
        case PORT_ASPECT_SUPER_ULTRAWIDE_32_9: name = "32:9"; break;
        case PORT_ASPECT_NATIVE_3_2:
        default:                                name = "native"; break;
    }
    sConfigJson["aspect_mode"] = name;
    SaveConfig();
}

extern "C" void Port_Config_CycleAspectMode(int direction) {
    int next = (int)sAspectMode + (direction < 0 ? -1 : 1);
    if (next < 0) next = PORT_ASPECT_COUNT - 1;
    if (next >= PORT_ASPECT_COUNT) next = 0;
    Port_Config_SetAspectMode((PortAspectMode)next);
}

extern "C" PortBgFill Port_Config_BgFill(void) {
    return sBgFill;
}

extern "C" const char* Port_Config_BgFillName(PortBgFill fill) {
    switch (fill) {
        case PORT_BG_FILL_SOLID_COLOR:   return "Solid color";
        case PORT_BG_FILL_BLURRED_FRAME: return "Blurred frame";
        case PORT_BG_FILL_BLACK:
        default:                          return "Black";
    }
}

extern "C" void Port_Config_SetBgFill(PortBgFill fill) {
    if (fill < 0 || fill >= PORT_BG_FILL_COUNT) fill = PORT_BG_FILL_BLACK;
    sBgFill = fill;
    const char* name = "black";
    switch (fill) {
        case PORT_BG_FILL_SOLID_COLOR:   name = "solid";   break;
        case PORT_BG_FILL_BLURRED_FRAME: name = "blurred"; break;
        case PORT_BG_FILL_BLACK:
        default:                          name = "black";   break;
    }
    sConfigJson["bg_fill"] = name;
    SaveConfig();
}

extern "C" void Port_Config_CycleBgFill(int direction) {
    int next = (int)sBgFill + (direction < 0 ? -1 : 1);
    if (next < 0) next = PORT_BG_FILL_COUNT - 1;
    if (next >= PORT_BG_FILL_COUNT) next = 0;
    Port_Config_SetBgFill((PortBgFill)next);
}

extern "C" void Port_Config_BgFillColor(u8* r, u8* g, u8* b) {
    if (r) *r = sBgFillR;
    if (g) *g = sBgFillG;
    if (b) *b = sBgFillB;
}

extern "C" void Port_Config_SetBgFillColor(u8 r, u8 g, u8 b) {
    sBgFillR = r; sBgFillG = g; sBgFillB = b;
    sConfigJson["bg_fill_color"] = nlohmann::json::array({ (int)r, (int)g, (int)b });
    SaveConfig();
}

extern "C" PortRenderBackend Port_Config_RenderBackend(void) {
    return sRenderBackend;
}

extern "C" const char* Port_Config_RenderBackendName(PortRenderBackend b) {
    switch (b) {
        case PORT_RENDER_BACKEND_SOFTWARE: return "Software";
        case PORT_RENDER_BACKEND_GPU:      return "GPU";
        case PORT_RENDER_BACKEND_AUTO:
        default:                            return "Auto";
    }
}

extern "C" void Port_Config_SetRenderBackend(PortRenderBackend b) {
    if (b < 0 || b >= PORT_RENDER_BACKEND_COUNT) b = PORT_RENDER_BACKEND_AUTO;
    sRenderBackend = b;
    const char* name = "auto";
    switch (b) {
        case PORT_RENDER_BACKEND_SOFTWARE: name = "software"; break;
        case PORT_RENDER_BACKEND_GPU:      name = "gpu";      break;
        case PORT_RENDER_BACKEND_AUTO:
        default:                            name = "auto";     break;
    }
    sConfigJson["render_backend"] = name;
    SaveConfig();
}

extern "C" void Port_Config_CycleRenderBackend(int direction) {
    int next = (int)sRenderBackend + (direction < 0 ? -1 : 1);
    if (next < 0) next = PORT_RENDER_BACKEND_COUNT - 1;
    if (next >= PORT_RENDER_BACKEND_COUNT) next = 0;
    Port_Config_SetRenderBackend((PortRenderBackend)next);
}

extern "C" void Port_Config_CycleTargetFps(int direction) {
    const u32 current = Port_Config_TargetFps();
    size_t index = 0;
    u32 bestDistance = UINT32_MAX;

    for (size_t i = 0; i < kFpsPresets.size(); i++) {
        const u32 preset = kFpsPresets[i];
        const u32 distance = current > preset ? current - preset : preset - current;
        if (distance < bestDistance) {
            bestDistance = distance;
            index = i;
        }
    }

    if (direction < 0) {
        index = index == 0 ? kFpsPresets.size() - 1 : index - 1;
    } else {
        index = index + 1 >= kFpsPresets.size() ? 0 : index + 1;
    }

    Port_Config_SetTargetFps(kFpsPresets[index]);
}

/* Make sure every connected gamepad has an open SDL_Gamepad handle.
 * Called from both Port_Config_OpenGamepads() at startup and re-tried
 * lazily in Port_Config_InputPressed() so a controller that's plugged
 * in (or recognised by SDL) AFTER startup still gets picked up without
 * needing the GAMEPAD_ADDED event to flow through the poll loop. */
static void Port_Config_RescanGamepads(bool verbose) {
    const bool hadPads = !sPads.empty();
    int count = 0;
    SDL_JoystickID* ids = SDL_GetGamepads(&count);
    if (verbose) {
        SDL_Log("SDL gamepads found: %d", count);
    }
    for (int i = 0; i < count; i++) {
        OpenGamepad(ids[i]);
    }
    if (!hadPads && !sPads.empty()) {
        Port_TouchControls_SetGamepadAvailable(true);
    } else if (hadPads && sPads.empty()) {
        Port_TouchControls_SetGamepadAvailable(false);
    }
    SDL_free(ids);
}

extern "C" void Port_Config_OpenGamepads(void) {
    /* Hint nudges to make SDL3 see more devices on Linux/wine where the
     * default backend selection sometimes misses Xinput-shaped pads. */
    SDL_SetHint("SDL_JOYSTICK_HIDAPI", "1");
    SDL_SetHint("SDL_GAMECONTROLLER_USE_BUTTON_LABELS", "1");

    if (!SDL_InitSubSystem(SDL_INIT_JOYSTICK)) {
        SDL_Log("SDL joystick init failed: %s", SDL_GetError());
    }
    if (!SDL_InitSubSystem(SDL_INIT_GAMEPAD)) {
        SDL_Log("SDL gamepad init failed: %s", SDL_GetError());
        return;
    }
    Port_Config_RescanGamepads(true);
}

/* Helper for the capture path: serialize a Bind to the same string
 * shape (`SDLK:0x...`, `SDL_GAMEPAD:0x...`, `SDL_AXIS:0x...`) used in
 * config.json so the new binding round-trips cleanly through Save +
 * Load on next launch. */
static std::string FormatBindForJson(const Bind& b) {
    char buf[40];
    if (b.key != SDLK_UNKNOWN) {
        std::snprintf(buf, sizeof(buf), "SDLK:0x%08x", (unsigned)b.key);
    } else if (b.pad != SDL_GAMEPAD_BUTTON_INVALID) {
        std::snprintf(buf, sizeof(buf), "SDL_GAMEPAD:0x%08x", (unsigned)b.pad);
    } else if (b.axis != SDL_GAMEPAD_AXIS_INVALID) {
        std::snprintf(buf, sizeof(buf), "SDL_AXIS:0x%08x", (unsigned)b.axis);
    } else {
        return std::string();
    }
    return std::string(buf);
}

/* Re-serialize sBinds[input] back into sConfigJson["bindings"][name]
 * and persist. Keeps the JSON in sync with runtime mutations so the
 * next launch picks up the user's rebinds. */
static void PersistBinds(PortInput input) {
    if (input < 0 || input >= PORT_INPUT_COUNT) return;
    const char* name = nullptr;
    for (const auto& d : kDefaults) {
        if (d.input == input) { name = d.name; break; }
    }
    if (!name) return;
    nlohmann::json arr = nlohmann::json::array();
    for (const Bind& b : sBinds[input]) {
        std::string s = FormatBindForJson(b);
        if (!s.empty()) arr.push_back(s);
    }
    if (!sConfigJson.contains("bindings") || !sConfigJson["bindings"].is_object()) {
        sConfigJson["bindings"] = nlohmann::json::object();
    }
    sConfigJson["bindings"][name] = arr;
    SaveConfig();
}

extern "C" void Port_Config_HandleEvent(const SDL_Event* e) {
    /* Touch-controls dispatch first so finger / virtual-pad events on
     * Android route to the touch handler before the keyboard / pad
     * paths below see them. From matheo's launcher integration. */
    Port_TouchControls_HandleEvent(e);

    /* Rebind capture: when the user's pressed an action-edit button in
     * the ImGui Controls tab, the next press of any key / gamepad
     * button / axis triggers becomes that action's new binding. Esc /
     * Right-click cancels without binding. Consume the event so the
     * captured press doesn't also propagate to its old action this
     * frame. */
    if (sCapturingInput >= 0) {
        const PortInput target = (PortInput)sCapturingInput;
        bool captured = false;
        Bind newBind;
        if (e->type == SDL_EVENT_KEY_DOWN && !e->key.repeat) {
            if (e->key.key == SDLK_ESCAPE) {
                /* Cancel without binding. */
                sCapturingInput = -1;
                return;
            }
            newBind.key = e->key.key;
            captured = true;
        } else if (e->type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) {
            newBind.pad = (SDL_GamepadButton)e->gbutton.button;
            captured = true;
        } else if (e->type == SDL_EVENT_GAMEPAD_AXIS_MOTION &&
                   e->gaxis.value > kAxisThreshold) {
            newBind.axis = (SDL_GamepadAxis)e->gaxis.axis;
            captured = true;
        }
        if (captured) {
            sBinds[target].clear();
            sBinds[target].push_back(newBind);
            PersistBinds(target);
            sCapturingInput = -1;
            return;
        }
    }

    if (e->type == SDL_EVENT_GAMEPAD_ADDED || e->type == SDL_EVENT_JOYSTICK_ADDED) {
        OpenGamepad(e->gdevice.which);
    } else if (e->type == SDL_EVENT_GAMEPAD_REMOVED || e->type == SDL_EVENT_JOYSTICK_REMOVED) {
        CloseGamepad(e->gdevice.which);
        Port_TouchControls_SetGamepadAvailable(!sPads.empty());
    } else if (e->type == SDL_EVENT_KEY_DOWN && !e->key.repeat) {
        /* Stamp every PortInput whose binding includes this key as
         * "pressed this frame" so the engine sees the press even if
         * the matching release arrives before the next poll. */
        for (size_t i = 0; i < PORT_INPUT_COUNT; i++) {
            for (const Bind& b : sBinds[i]) {
                if (b.key != SDLK_UNKNOWN && b.key == e->key.key) {
                    sEdgePressed[i] = true;
                    break;
                }
            }
        }
    } else if (e->type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) {
        Port_TouchControls_NotifyGamepadUsed();
        for (size_t i = 0; i < PORT_INPUT_COUNT; i++) {
            for (const Bind& b : sBinds[i]) {
                if (b.pad >= 0 && b.pad < SDL_GAMEPAD_BUTTON_COUNT &&
                    b.pad == (SDL_GamepadButton)e->gbutton.button) {
                    sEdgePressed[i] = true;
                    break;
                }
            }
        }
    } else if (e->type == SDL_EVENT_GAMEPAD_AXIS_MOTION &&
               e->gaxis.value > kAxisThreshold) {
        Port_TouchControls_NotifyGamepadUsed();
        for (size_t i = 0; i < PORT_INPUT_COUNT; i++) {
            for (const Bind& b : sBinds[i]) {
                if (b.axis >= 0 && b.axis < SDL_GAMEPAD_AXIS_COUNT &&
                    b.axis == (SDL_GamepadAxis)e->gaxis.axis) {
                    sEdgePressed[i] = true;
                    break;
                }
            }
        }
    }
}

extern "C" void Port_Config_ClearInputEdges(void) {
    sEdgePressed.fill(false);
}

extern "C" bool Port_Config_InputPressed(PortInput input) {
    if (Port_TouchControls_InputPressed(input)) {
        return true;
    }

    /* Edge cache — set by Port_Config_HandleEvent on KEY_DOWN /
     * GAMEPAD_BUTTON_DOWN events. Lets a sub-frame tap (press+release
     * entirely between two polls) still register as held for one game
     * frame, which the polled-state path below cannot do on its own. */
    if (input >= 0 && input < PORT_INPUT_COUNT && sEdgePressed[input]) {
        return true;
    }

    SDL_UpdateGamepads();
    /* Re-scan every ~1s so a hot-plugged pad starts working even if its
     * GAMEPAD_ADDED event somehow didn't reach the poll loop. */
    static uint32_t sNextRescanAt = 0;
    uint32_t now = SDL_GetTicks();
    if (now >= sNextRescanAt) {
        Port_Config_RescanGamepads(false);
        sNextRescanAt = now + 1000;
    }

    int count = 0;
    const bool* keys = SDL_GetKeyboardState(&count);
    for (const Bind& b : sBinds[input]) {
        SDL_Scancode scan = b.key == SDLK_UNKNOWN ? SDL_SCANCODE_UNKNOWN : SDL_GetScancodeFromKey(b.key, nullptr);
        if (scan != SDL_SCANCODE_UNKNOWN && (int)scan < count && keys[scan]) {
            return true;
        }
        for (SDL_Gamepad* pad : sPads) {
            if (b.pad >= 0 && b.pad < SDL_GAMEPAD_BUTTON_COUNT && SDL_GetGamepadButton(pad, b.pad)) {
                Port_TouchControls_NotifyGamepadUsed();
                return true;
            }
            if (b.axis >= 0 && b.axis < SDL_GAMEPAD_AXIS_COUNT &&
                SDL_GetGamepadAxis(pad, b.axis) > kAxisThreshold) {
                Port_TouchControls_NotifyGamepadUsed();
                return true;
            }
        }
    }
    return false;
}

extern "C" bool Port_Config_SoftSlotPressed(int slot) {
    static const PortInput kMap[4] = {
        PORT_INPUT_SOFT_X, PORT_INPUT_SOFT_Y,
        PORT_INPUT_SOFT_L2, PORT_INPUT_SOFT_R2,
    };
    if (slot < 0 || slot >= 4) return false;
    return Port_Config_InputPressed(kMap[slot]);
}

/* Returns the left-stick reading from the first attached gamepad, in
 * the range [-1, 1] per axis, with Y inverted to match TMC's screen
 * convention (positive Y = down on screen, negative Y = up). Returns
 * false (and leaves outputs untouched) when no gamepad is attached or
 * when SDL hasn't reported any stick activity yet. */
extern "C" bool Port_Config_GetLeftStick(float* outX, float* outY) {
    if (sPads.empty()) {
        return false;
    }
    SDL_Gamepad* pad = sPads.front();
    /* SDL axis values are int16 -32768..32767. Normalise to [-1, 1].
     * +y on SDL = stick down = positive screen-y in TMC (top-left
     * origin), so no sign flip is required despite intuition. */
    const int16_t rawX = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTX);
    const int16_t rawY = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTY);
    if (outX) *outX = (float)rawX / 32767.0f;
    if (outY) *outY = (float)rawY / 32767.0f;
    return true;
}

extern "C" void Port_Config_CloseGamepads(void) {
    for (SDL_Gamepad* pad : sPads) {
        SDL_CloseGamepad(pad);
    }
    sPads.clear();
}

/* ============================================================ */
/*                     Rebind API for the UI                    */
/* ============================================================ */

extern "C" const char* Port_Config_InputName(PortInput input) {
    if (input < 0 || input >= PORT_INPUT_COUNT) return "?";
    for (const auto& d : kDefaults) {
        if (d.input == input) return d.name;
    }
    return "?";
}

extern "C" int Port_Config_BindingCount(PortInput input) {
    if (input < 0 || input >= PORT_INPUT_COUNT) return 0;
    return (int)sBinds[input].size();
}

/* Human-readable label for a single binding. Composite three rows:
 * gamepad buttons get the SDL_GetGamepadStringForButton name where
 * available (Xbox-style "A", "Y", "DPad Up" etc.); axes get the
 * trigger / stick name; keyboard keys use SDL_GetKeyName. Falls back
 * to the raw numeric code if any of those return null. */
extern "C" void Port_Config_BindingLabel(PortInput input, int idx, char* out, int cap) {
    if (!out || cap <= 0) return;
    out[0] = '\0';
    if (input < 0 || input >= PORT_INPUT_COUNT) return;
    if (idx < 0 || idx >= (int)sBinds[input].size()) return;
    const Bind& b = sBinds[input][idx];
    if (b.key != SDLK_UNKNOWN) {
        const char* name = SDL_GetKeyName(b.key);
        std::snprintf(out, cap, "%s", (name && *name) ? name : "Key");
    } else if (b.pad != SDL_GAMEPAD_BUTTON_INVALID) {
        const char* name = SDL_GetGamepadStringForButton(b.pad);
        if (name && *name) std::snprintf(out, cap, "Pad: %s", name);
        else                std::snprintf(out, cap, "Pad button %d", (int)b.pad);
    } else if (b.axis != SDL_GAMEPAD_AXIS_INVALID) {
        const char* name = SDL_GetGamepadStringForAxis(b.axis);
        if (name && *name) std::snprintf(out, cap, "Axis: %s", name);
        else                std::snprintf(out, cap, "Pad axis %d", (int)b.axis);
    }
}

extern "C" void Port_Config_ClearBindings(PortInput input) {
    if (input < 0 || input >= PORT_INPUT_COUNT) return;
    sBinds[input].clear();
    PersistBinds(input);
}

extern "C" void Port_Config_BeginCaptureBinding(PortInput input) {
    if (input < 0 || input >= PORT_INPUT_COUNT) return;
    sCapturingInput = (int)input;
}

extern "C" int Port_Config_IsCapturingBinding(void) {
    return sCapturingInput >= 0 ? 1 : 0;
}

extern "C" PortInput Port_Config_CapturingBindingInput(void) {
    return (PortInput)sCapturingInput;
}

extern "C" void Port_Config_CancelCaptureBinding(void) {
    sCapturingInput = -1;
}

extern "C" void Port_Config_ResetAllBindings(void) {
    /* Re-populate sBinds from the kDefaults table and persist the
     * resulting JSON. Mirrors what Port_Config_Load does on a missing
     * config — same code path. */
    for (auto& v : sBinds) v.clear();
    for (const auto& d : kDefaults) {
        for (const char* bind : d.binds) {
            AddBind(d.input, bind);
        }
    }
    /* Rewrite the bindings object in JSON wholesale. */
    nlohmann::json b = nlohmann::json::object();
    for (const auto& d : kDefaults) {
        nlohmann::json arr = nlohmann::json::array();
        for (const Bind& bind : sBinds[d.input]) {
            std::string s = FormatBindForJson(bind);
            if (!s.empty()) arr.push_back(s);
        }
        b[d.name] = arr;
    }
    sConfigJson["bindings"] = b;
    SaveConfig();
}

/* ------------------------------------------------------------------ */
/*  TTS settings                                                      */
/*                                                                    */
/*  Persisted to config.json so the user's voice / rate / pitch /     */
/*  volume / language survives restarts. The actual TTS service       */
/*  caches these in port_tts.cpp and re-reads on init.                */
/* ------------------------------------------------------------------ */

extern "C" bool Port_Config_GetTtsEnabled(void) { return sTtsEnabled; }
extern "C" void Port_Config_SetTtsEnabled(bool on) {
    sTtsEnabled = on;
    sConfigJson["tts_enabled"] = on;
    SaveConfig();
}

extern "C" float Port_Config_GetTtsRate(void) { return sTtsRate; }
extern "C" void  Port_Config_SetTtsRate(float v) {
    sTtsRate = v;
    sConfigJson["tts_rate"] = (double)v;
    SaveConfig();
}

extern "C" float Port_Config_GetTtsPitch(void) { return sTtsPitch; }
extern "C" void  Port_Config_SetTtsPitch(float v) {
    sTtsPitch = v;
    sConfigJson["tts_pitch"] = (double)v;
    SaveConfig();
}

extern "C" float Port_Config_GetTtsVolume(void) { return sTtsVolume; }
extern "C" void  Port_Config_SetTtsVolume(float v) {
    sTtsVolume = v;
    sConfigJson["tts_volume"] = (double)v;
    SaveConfig();
}

extern "C" const char* Port_Config_GetTtsVoice(void) {
    return sTtsVoice.c_str();
}
extern "C" void Port_Config_SetTtsVoice(const char* v) {
    sTtsVoice = v ? v : "";
    sConfigJson["tts_voice"] = sTtsVoice;
    SaveConfig();
}

extern "C" const char* Port_Config_GetTtsLanguage(void) {
    return sTtsLanguage.c_str();
}
extern "C" void Port_Config_SetTtsLanguage(const char* v) {
    sTtsLanguage = v ? v : "";
    sConfigJson["tts_language"] = sTtsLanguage;
    SaveConfig();
}
