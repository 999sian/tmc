#include "port_runtime_config.h"

#include <SDL3/SDL.h>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace {

struct Bind {
    SDL_Keycode key = SDLK_UNKNOWN;
    SDL_GamepadButton pad = SDL_GAMEPAD_BUTTON_INVALID;
};

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
}};

u8 sScale = 3;
std::string sUpscaleMethod = "nearest";
u64 sFrameTimeNs = 0;
bool sPortSettingsMenuEnabled = true;
std::array<std::vector<Bind>, PORT_INPUT_COUNT> sBinds;
std::vector<SDL_Gamepad*> sPads;
std::filesystem::path sConfigPath = "config.json";
nlohmann::json sConfigJson;
const std::array<u32, 9> kFpsPresets = { 0, 30, 60, 75, 90, 120, 144, 150, 240 };

nlohmann::json DefaultsJson(void) {
    nlohmann::json j = {
        { "window_scale", 3 },
        { "upscale_method", "nearest" },
        { "frame_time_ns", 0 },
        { "port_settings_menu", true },
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
    sUpscaleMethod = j.value("upscale_method", "nearest");
    sFrameTimeNs = j.value("frame_time_ns", 0ULL);
    sPortSettingsMenuEnabled = j.value("port_settings_menu", true);
    /* Migrate legacy 60 FPS lock to "uncapped" so users get the matheo
     * default behavior without losing their other config. */
    if (sFrameTimeNs == 16666667ULL) {
        sFrameTimeNs = 0;
        sConfigJson["frame_time_ns"] = 0;
        SaveConfig();
    }

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
    int count = 0;
    SDL_JoystickID* ids = SDL_GetGamepads(&count);
    if (verbose) {
        SDL_Log("SDL gamepads found: %d", count);
    }
    for (int i = 0; i < count; i++) {
        OpenGamepad(ids[i]);
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

extern "C" void Port_Config_HandleEvent(const SDL_Event* e) {
    if (e->type == SDL_EVENT_GAMEPAD_ADDED || e->type == SDL_EVENT_JOYSTICK_ADDED) {
        OpenGamepad(e->gdevice.which);
    } else if (e->type == SDL_EVENT_GAMEPAD_REMOVED || e->type == SDL_EVENT_JOYSTICK_REMOVED) {
        CloseGamepad(e->gdevice.which);
    }
}

extern "C" bool Port_Config_InputPressed(PortInput input) {
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
                return true;
            }
        }
    }
    return false;
}

extern "C" void Port_Config_CloseGamepads(void) {
    for (SDL_Gamepad* pad : sPads) {
        SDL_CloseGamepad(pad);
    }
    sPads.clear();
}
