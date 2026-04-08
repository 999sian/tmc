#include "gba/io_reg.h"
#include "main.h"
#include "port_config.h"
#include "port_audio.h"
#include "port_gba_mem.h"
#include "port_ppu.h"
#include "port_rom.h"
#include "port_types.h"
#include <stdlib.h>
#include "stdio.h"
#include <SDL3/SDL.h>

/*
 * Region-specific asset offset header is included based on detected ROM.
 * Both are always available; the correct mapDataBase is selected at runtime.
 */
#ifdef EU
#include "port_offset_EU.h"
#else
#include "port_offset_USA.h"
#endif

static bool Port_TryInitVideo(const char* videoDriver, const char* renderDriver, bool headless) {
    if (videoDriver) {
        SDL_SetHint(SDL_HINT_VIDEO_DRIVER, videoDriver);
    } else {
        SDL_ResetHint(SDL_HINT_VIDEO_DRIVER);
    }

    if (renderDriver) {
        SDL_SetHint(SDL_HINT_RENDER_DRIVER, renderDriver);
    } else {
        SDL_ResetHint(SDL_HINT_RENDER_DRIVER);
    }

    if (SDL_Init(SDL_INIT_VIDEO)) {
        const char* currentDriver = SDL_GetCurrentVideoDriver();

        fprintf(stderr, "SDL video driver: %s\n", currentDriver ? currentDriver : "unknown");
        if (headless) {
            fprintf(stderr, "SDL initialized with headless video driver '%s'.\n", videoDriver);
        }
        return true;
    }

    return false;
}

static void Port_LogVideoDiagnostics(void) {
    int driverCount = SDL_GetNumVideoDrivers();

    fprintf(stderr,
            "Video env: DISPLAY='%s' WAYLAND_DISPLAY='%s' XDG_SESSION_TYPE='%s'\n",
            getenv("DISPLAY") ? getenv("DISPLAY") : "",
            getenv("WAYLAND_DISPLAY") ? getenv("WAYLAND_DISPLAY") : "",
            getenv("XDG_SESSION_TYPE") ? getenv("XDG_SESSION_TYPE") : "");

    fprintf(stderr, "SDL compiled video drivers:");
    for (int i = 0; i < driverCount; i++) {
        fprintf(stderr, " %s", SDL_GetVideoDriver(i));
    }
    fprintf(stderr, "\n");
}

static bool Port_InitVideo(void) {
    const char* err = NULL;
    const char* forcedDriver = getenv("SDL_VIDEODRIVER");
    const char* display = getenv("DISPLAY");
    const char* waylandDisplay = getenv("WAYLAND_DISPLAY");

    if (forcedDriver && forcedDriver[0] != '\0') {
        if (Port_TryInitVideo(NULL, NULL, false)) {
            return true;
        }
        err = SDL_GetError();
        SDL_Quit();
    }

    if (display && display[0] != '\0') {
        if (Port_TryInitVideo("x11", NULL, false)) {
            return true;
        }
        err = SDL_GetError();
        SDL_Quit();
    }

    if (waylandDisplay && waylandDisplay[0] != '\0') {
        if (Port_TryInitVideo("wayland", NULL, false)) {
            return true;
        }
        err = SDL_GetError();
        SDL_Quit();
    }

    if (Port_TryInitVideo(NULL, NULL, false)) {
        return true;
    }
    err = SDL_GetError();

    SDL_Quit();
    if (Port_TryInitVideo("dummy", "software", true)) {
        fprintf(stderr, "Initial SDL error: %s\n", err ? err : "unknown error");
        return true;
    }

    Port_LogVideoDiagnostics();
    fprintf(stderr, "SDL video init failed: normal='%s', fallback='%s'\n", err ? err : "unknown error", SDL_GetError());
    return false;
}

static void Port_InitAudio(void) {
    const char* err = NULL;

    if (SDL_InitSubSystem(SDL_INIT_AUDIO) && Port_Audio_Init()) {
        return;
    }

    err = SDL_GetError();
    Port_Audio_Shutdown();
    SDL_QuitSubSystem(SDL_INIT_AUDIO);

    SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy");
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) && Port_Audio_Init()) {
        fprintf(stderr, "Audio device unavailable, using SDL dummy audio driver.\n");
        gMain.muteAudio = 1;
        return;
    }

    fprintf(stderr, "Audio disabled: normal='%s', fallback='%s'\n", err ? err : "unknown error", SDL_GetError());
    Port_Audio_Shutdown();
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
    gMain.muteAudio = 1;
}

int main() {

    fprintf(stderr, "Initializing port layer...\n");

    // Initialize REG_KEYINPUT to all-keys-released (GBA: 1=not pressed)
    *(u16*)(gIoMem + REG_OFFSET_KEYINPUT) = 0x03FF;

    // Load ROM data (auto-detects USA/EU from game code)
    Port_LoadRom("baserom.gba");

    // Verify ROM region matches compiled region
#ifdef EU
    if (gRomRegion != ROM_REGION_EU) {
        fprintf(stderr,
                "WARNING: This binary was compiled for EU but the ROM is %s.\n"
                "         Asset offsets may be incorrect. Rebuild with the correct --game_version.\n",
                gRomRegion == ROM_REGION_USA ? "USA" : "UNKNOWN");
    }
#else
    if (gRomRegion != ROM_REGION_USA) {
        fprintf(stderr,
                "WARNING: This binary was compiled for USA but the ROM is %s.\n"
                "         Asset offsets may be incorrect. Rebuild with: xmake f --game_version=EU\n",
                gRomRegion == ROM_REGION_EU ? "EU" : "UNKNOWN");
    }
#endif

    // Initialize SDL video first. Audio is optional and handled separately.
    if (!Port_InitVideo()) {
        return 1;
    }

    // GBA resolution × 3
    SDL_Window* window = SDL_CreateWindow("The Minish Cap", 240 * 3, 160 * 3, 0);
    if (window == NULL) {
        fprintf(stderr, "SDL_CreateWindow Error: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    SDL_ShowWindow(window);
    SDL_RaiseWindow(window);
    SDL_SyncWindow(window);

    // Initialize PPU renderer
    Port_PPU_Init(window);
    Port_InitAudio();

    fprintf(stderr, "Port layer initialized. Entering AgbMain...\n");

    AgbMain();

    Port_Audio_Shutdown();
    Port_PPU_Shutdown();
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
