#include "gba/io_reg.h"
#include "main.h"
#include "port_config.h"
/* Set by xmake (-DMODE1_GBA_WIDTH=N); falls back to GBA-native 240. */
#ifndef MODE1_GBA_WIDTH
#define MODE1_GBA_WIDTH 240
#endif
#include "port_asset_bootstrap.h"
#include "port_audio.h"
#include "port_gba_mem.h"
#include "port_ppu.h"
#include "port_rom.h"
#include "port_runtime_config.h"
#include "port_types.h"
#include "port_update_check.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
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

static void Port_LogAudioDiagnostics(void) {
    int driverCount = SDL_GetNumAudioDrivers();

    fprintf(stderr,
            "Audio env: SDL_AUDIODRIVER='%s' XDG_RUNTIME_DIR='%s' PULSE_SERVER='%s' PIPEWIRE_REMOTE='%s'\n",
            getenv("SDL_AUDIODRIVER") ? getenv("SDL_AUDIODRIVER") : "",
            getenv("XDG_RUNTIME_DIR") ? getenv("XDG_RUNTIME_DIR") : "",
            getenv("PULSE_SERVER") ? getenv("PULSE_SERVER") : "",
            getenv("PIPEWIRE_REMOTE") ? getenv("PIPEWIRE_REMOTE") : "");

    fprintf(stderr, "SDL compiled audio drivers:");
    for (int i = 0; i < driverCount; i++) {
        fprintf(stderr, " %s", SDL_GetAudioDriver(i));
    }
    fprintf(stderr, "\n");
}

static bool Port_TryInitAudioDriver(const char* audioDriver, bool muteOnSuccess, const char** outError) {
    if (audioDriver && audioDriver[0] != '\0') {
        SDL_SetHint(SDL_HINT_AUDIO_DRIVER, audioDriver);
    } else {
        SDL_ResetHint(SDL_HINT_AUDIO_DRIVER);
    }

    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        if (outError) {
            *outError = SDL_GetError();
        }
        return false;
    }

    if (!Port_Audio_Init()) {
        if (outError) {
            *outError = SDL_GetError();
        }
        Port_Audio_Shutdown();
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        return false;
    }

    fprintf(stderr,
            "SDL audio driver: %s%s\n",
            SDL_GetCurrentAudioDriver() ? SDL_GetCurrentAudioDriver() : "unknown",
            muteOnSuccess ? " (muted dummy backend)" : "");
    gMain.muteAudio = muteOnSuccess ? 1 : 0;
    return true;
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

    if (waylandDisplay && waylandDisplay[0] != '\0') {
        if (Port_TryInitVideo("wayland", NULL, false)) {
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
    const char* forcedDriver = getenv("SDL_AUDIODRIVER");
    static const char* const kPreferredDrivers[] = {
        "pipewire",
        "pulseaudio",
        "alsa",
        "sndio",
    };

    if (forcedDriver && forcedDriver[0] != '\0') {
        if (Port_TryInitAudioDriver(forcedDriver, false, &err)) {
            return;
        }
        fprintf(stderr, "SDL forced audio driver '%s' failed: %s\n", forcedDriver, err ? err : "unknown error");
    }

    if (Port_TryInitAudioDriver(NULL, false, &err)) {
        return;
    }

    fprintf(stderr, "SDL default audio init failed: %s\n", err ? err : "unknown error");

    for (size_t i = 0; i < sizeof(kPreferredDrivers) / sizeof(kPreferredDrivers[0]); i++) {
        const char* driver = kPreferredDrivers[i];
        if (forcedDriver && strcmp(forcedDriver, driver) == 0) {
            continue;
        }
        if (Port_TryInitAudioDriver(driver, false, &err)) {
            fprintf(stderr, "SDL audio recovered by forcing '%s'.\n", driver);
            return;
        }
        fprintf(stderr, "SDL audio driver '%s' failed: %s\n", driver, err ? err : "unknown error");
    }

    if (Port_TryInitAudioDriver("dummy", true, &err)) {
        fprintf(stderr, "Audio device unavailable, using SDL dummy audio driver.\n");
        return;
    }

    Port_LogAudioDiagnostics();
    fprintf(stderr, "Audio disabled: final fallback failed: %s\n", err ? err : "unknown error");
    gMain.muteAudio = 1;
}

/*
 * On Windows mingw, the heap allocator hands out addresses inside the
 * 0x02000000-0x0A000000 range — the same range port_resolve_addr treats
 * as GBA addresses. Heap pointers passed to DmaCopy* (palette/gfx loads
 * from std::vector buffers) get mistranslated to gEwram[] / gVram[] etc,
 * silently reading zeros and stalling the title-screen palette. Reserve
 * the GBA address window before any heap is opened so the OS allocator
 * can't place anything there. Linux glibc keeps malloc above 0x55... so
 * this is a no-op there; the call is Windows-only.
 */
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
static void Port_ReserveGbaAddressSpace(void) {
    static const struct {
        uintptr_t base;
        size_t size;
        const char* name;
    } regions[] = {
        { 0x02000000u, 0x00040000u, "EWRAM" },
        { 0x03000000u, 0x00008000u, "IWRAM" },
        { 0x04000000u, 0x00000400u, "IO" },
        { 0x05000000u, 0x00000400u, "palette" },
        { 0x06000000u, 0x00018000u, "VRAM" },
        { 0x07000000u, 0x00000400u, "OAM" },
        { 0x08000000u, 0x02000000u, "ROM window" },
    };
    for (size_t i = 0; i < sizeof(regions) / sizeof(regions[0]); ++i) {
        LPVOID p = VirtualAlloc((LPVOID)regions[i].base, regions[i].size, MEM_RESERVE, PAGE_NOACCESS);
        if (p == NULL) {
            MEMORY_BASIC_INFORMATION mbi;
            SIZE_T querySize = VirtualQuery((LPCVOID)regions[i].base, &mbi, sizeof(mbi));
            if (querySize == sizeof(mbi) && mbi.State != MEM_FREE) {
                fprintf(stderr, "Skipped %s window 0x%zx-0x%zx (already occupied).\n", regions[i].name,
                        (size_t)regions[i].base, (size_t)(regions[i].base + regions[i].size));
            } else {
                fprintf(stderr, "Skipped %s window 0x%zx-0x%zx (guard unavailable).\n",
                        regions[i].name, (size_t)regions[i].base, (size_t)(regions[i].base + regions[i].size));
            }
        } else {
            fprintf(stderr, "Reserved %s window 0x%zx-0x%zx.\n", regions[i].name, (size_t)regions[i].base,
                    (size_t)(regions[i].base + regions[i].size));
        }
    }
}
#else
static void Port_ReserveGbaAddressSpace(void) { /* not needed on Linux/macOS */ }
#endif

int main(int argc, char* argv[]) {

    /* Must run before any std::vector / new / malloc that could land in
     * the GBA window. Static initializers in C++ files are constructed
     * before main, so even this is technically not early enough — but
     * the affected allocations (Port_LoadPaletteGroupFromAssets cache)
     * happen later, after EnsureAssetGroupCache(), so reserving here is
     * sufficient in practice. */
    Port_ReserveGbaAddressSpace();

    /* Install crash handlers as early as possible so even faults during
     * startup (asset bootstrap, ROM load) auto-capture a bug report. */
    extern void Port_BugReport_InstallCrashHandlers(void);
    Port_BugReport_InstallCrashHandlers();

    fprintf(stderr, "Initializing port layer...\n");

    // Initialize REG_KEYINPUT to all-keys-released (GBA: 1=not pressed)
    *(u16*)(gIoMem + REG_OFFSET_KEYINPUT) = 0x03FF;

    Port_Config_Load("config.json");

    u8 window_scale = Port_Config_WindowScale();
    bool noAudio = false;
    if (argc > 1) {
        for (int i = 1; i < argc; i++) {
            if (strcmp(argv[i], "--window_scale=") == 0 || strncmp(argv[i], "--window_scale=", 15) == 0) {
                const char* valueStr = argv[i] + 15;
                int value = atoi(valueStr);
                if (value >= 1 && value <= 10) {
                    window_scale = (uint8_t)value;
                } else {
                    fprintf(stderr, "Invalid window scale '%s'. Must be an integer between 1 and 10.\n", valueStr);
                }
            }
            else if (strcmp(argv[i], "--no-audio") == 0) {
                noAudio = true;
            }
            else if (strcmp(argv[i], "--help") == 0) {
                fprintf(stderr, "Usage: %s [--window_scale=<value>] [--no-audio]\n", argv[0]);
                fprintf(stderr, "  --window_scale=<value>: Set the window scale (1-10, default is 3)\n");
                fprintf(stderr, "  --no-audio: Skip audio init (workaround for agbplay crash)\n");
                fprintf(stderr, "  config.json: Set window_scale and bindings defaults\n");
                return 0;
            }
            else {
                fprintf(stderr, "Unknown argument: %s\n", argv[i]);
            }
        }
    }

    // Initialize SDL video first. Audio is optional and handled separately.
    if (!Port_InitVideo()) {
        return 1;
    }

    Port_Config_OpenGamepads();

    SDL_Window* window = SDL_CreateWindow(
        "The Minish Cap", MODE1_GBA_WIDTH * window_scale, 160 * window_scale, SDL_WINDOW_RESIZABLE);
    if (window == NULL) {
        fprintf(stderr, "SDL_CreateWindow Error: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    fprintf(stderr, "SDL window created.\n");
    SDL_ShowWindow(window);
    fprintf(stderr, "SDL window shown.\n");
    SDL_RaiseWindow(window);
    fprintf(stderr, "SDL window raised.\n");
    /* SDL_SyncWindow is a convenience flush, not required for correctness.
     * Some packaged Linux/X11 environments have been seen to fault here
     * before any port-side diagnostics are emitted, so avoid it at startup. */
    Port_EnsureAssetsReadyWithDisplay(window);
    fprintf(stderr, "Asset bootstrap complete.\n");
    Port_CheckForUpdates(window);
    fprintf(stderr, "Update check complete.\n");

    Port_LoadRom("baserom.gba");
    fprintf(stderr, "ROM load complete.\n");

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

    // Initialize PPU renderer
    Port_PPU_Init(window);
    fprintf(stderr, "PPU init complete.\n");
    if (noAudio) {
        gMain.muteAudio = 1;
        fprintf(stderr, "Audio disabled by --no-audio flag.\n");
    } else {
        Port_InitAudio();
        fprintf(stderr, "Audio init complete.\n");
    }

    fprintf(stderr, "Port layer initialized. Entering AgbMain...\n");

    AgbMain();

    Port_Audio_Shutdown();
    Port_PPU_Shutdown();
    Port_Config_CloseGamepads();
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
