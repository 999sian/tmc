#include "port_runtime_config.h"

#include <SDL3/SDL.h>
#include <array>
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
u64 sFrameTimeNs = 16666667;
std::array<std::vector<Bind>, PORT_INPUT_COUNT> sBinds;
std::vector<SDL_Gamepad*> sPads;

nlohmann::json DefaultsJson(void) {
    nlohmann::json j = { { "window_scale", 3 }, { "upscale_method", "nearest" }, { "frame_time_ns", 16666667 }, { "bindings", nlohmann::json::object() } };
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

    if (std::filesystem::exists(p)) {
        try {
            std::ifstream(p) >> j;
        } catch (...) {
            j = DefaultsJson();
        }
    } else {
        std::ofstream(p) << j.dump(4) << '\n';
    }

    int scale = j.value("window_scale", 3);
    sScale = scale >= 1 && scale <= 10 ? (u8)scale : 3;
    sUpscaleMethod = j.value("upscale_method", "nearest");
    sFrameTimeNs = j.value("frame_time_ns", 16666667ULL);
    if (sFrameTimeNs < 1000000) {
        sFrameTimeNs = 1000000;
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

extern "C" void Port_Config_OpenGamepads(void) {
    int count = 0;
    if (!SDL_InitSubSystem(SDL_INIT_GAMEPAD)) {
        SDL_Log("SDL gamepad init failed: %s", SDL_GetError());
        return;
    }
    SDL_JoystickID* ids = SDL_GetGamepads(&count);
    SDL_Log("SDL gamepads found: %d", count);
    for (int i = 0; i < count; i++) {
        OpenGamepad(ids[i]);
    }
    SDL_free(ids);
}

extern "C" void Port_Config_HandleEvent(const SDL_Event* e) {
    if (e->type == SDL_EVENT_GAMEPAD_ADDED) {
        OpenGamepad(e->gdevice.which);
    } else if (e->type == SDL_EVENT_GAMEPAD_REMOVED) {
        CloseGamepad(e->gdevice.which);
    }
}

extern "C" bool Port_Config_InputPressed(PortInput input) {
    SDL_UpdateGamepads();
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
