#include "gba/io_reg.h"
#include "main.h"
#include <stdbool.h>
#include "port_config.h"
/* Set by xmake (-DMODE1_GBA_WIDTH=N); falls back to GBA-native 240. */
#ifndef MODE1_GBA_WIDTH
#define MODE1_GBA_WIDTH 240
#endif
/* Height stays at GBA-native 160 always — widescreen only stretches X. */
#ifndef MODE1_GBA_HEIGHT
#define MODE1_GBA_HEIGHT 160
#endif
#include "port_asset_bootstrap.h"
#include "port_audio.h"
#include "port_gba_mem.h"
#include "port_ppu.h"
#include "port_rom.h"
#include "port_runtime_config.h"
#include "port_tts.h"
#include "port_types.h"
#include "port_update_check.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <SDL3/SDL.h>
#include "port_launcher_bootstrap.h"

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
#include <stdint.h>
#include <windows.h>

static int s_gba_va_reserve_done;

static uintptr_t Port_AlignDownU(uintptr_t x, DWORD gran) {
    return (x / (uintptr_t)gran) * (uintptr_t)gran;
}

static void Port_ReserveGbaAddressSpace(void);

#if defined(__GNUC__)
__attribute__((constructor(101)))
#endif
static void Port_ReserveGbaAddressSpaceEarly(void) {
    Port_ReserveGbaAddressSpace();
}

static void Port_ReserveGbaAddressSpace(void) {
    const uintptr_t range_lo = 0x02000000u;
    const uintptr_t range_hi = 0x0A000000u;
    const size_t want_bytes = (size_t)(range_hi - range_lo);

    if (s_gba_va_reserve_done) {
        return;
    }

    SYSTEM_INFO si;
    GetSystemInfo(&si);
    const DWORD page = si.dwPageSize ? si.dwPageSize : 4096u;

    LPVOID whole = VirtualAlloc((LPVOID)range_lo, (SIZE_T)want_bytes, MEM_RESERVE, PAGE_NOACCESS);
    if (whole != NULL) {
        if (getenv("TMC_VERBOSE_GBA_VA")) {
            fprintf(stderr, "Reserved GBA address window 0x%zx-0x%zx (heap can't land here).\n",
                    (size_t)range_lo, (size_t)range_hi);
        }
        s_gba_va_reserve_done = 1;
        return;
    }

    fprintf(stderr,
            "WARN: Single-block GBA VA reserve failed (err=%lu); filling window by sub-regions.\n",
            (unsigned long)GetLastError());

    size_t reserved = 0;
    uintptr_t q = range_lo;
    while (q < range_hi) {
        MEMORY_BASIC_INFORMATION mbi;
        if (VirtualQuery((LPCVOID)q, &mbi, sizeof(mbi)) == 0) {
            fprintf(stderr, "WARN: VirtualQuery stopped at 0x%zx (err=%lu).\n", (size_t)q,
                    (unsigned long)GetLastError());
            break;
        }

        const uintptr_t reg_base = (uintptr_t)mbi.BaseAddress;
        const uintptr_t reg_end = reg_base + (uintptr_t)mbi.RegionSize;

        if (q < reg_base) {
            q = reg_base;
            continue;
        }

        if (mbi.State != MEM_FREE) {
            q = reg_end;
            continue;
        }

        const uintptr_t lo_in = reg_base > range_lo ? reg_base : range_lo;
        const uintptr_t hi_in = reg_end < range_hi ? reg_end : range_hi;
        if (lo_in < hi_in) {
            uintptr_t u = Port_AlignDownU(lo_in, page);
            if (u < lo_in) {
                u += (uintptr_t)page;
            }
            while (u < hi_in) {
                uintptr_t next = u + (uintptr_t)page;
                if (next > hi_in) {
                    next = hi_in;
                }
                if (u >= lo_in) {
                    const SIZE_T sz = (SIZE_T)(next - u);
                    if (sz > 0 && VirtualAlloc((LPVOID)u, sz, MEM_RESERVE, PAGE_NOACCESS) != NULL) {
                        reserved += (size_t)sz;
                    }
                }
                if (next <= u) {
                    break;
                }
                u = next;
            }
        }

        q = reg_end;
    }

    if (reserved + (size_t)page >= want_bytes) {
        if (getenv("TMC_VERBOSE_GBA_VA")) {
            fprintf(stderr,
                    "Reserved GBA address window 0x%zx-0x%zx (%zu / %zu bytes, sub-regions; heap can't land here).\n",
                    (size_t)range_lo, (size_t)range_hi, reserved, want_bytes);
        }
    } else if (reserved > 0u) {
        fprintf(stderr,
                "WARN: Partial GBA VA reserve %zu / %zu bytes; heap may still use gaps — DmaCopy risk remains.\n",
                reserved, want_bytes);
    } else {
        fprintf(stderr,
                "WARN: Could not reserve GBA address window 0x%zx-0x%zx; DmaCopy may misbehave.\n",
                (size_t)range_lo, (size_t)range_hi);
    }

    s_gba_va_reserve_done = 1;
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

    fprintf(stderr, "Initializing port layer...\n");
    {
        extern void Port_DebugVerbose_Init(void);
        Port_DebugVerbose_Init();
    }

    /* Auto-capture on SIGSEGV/SIGABRT/SIGBUS — dropped from port_main.c by
     * upstream refactor 5987bf9b1 and not re-added when #108 brought
     * port_bugreport.cpp back. Without this, crashes produce no bundle
     * (F9 manual capture still works via port_bios.c). */
    {
        extern void Port_BugReport_InstallCrashHandlers(void);
        Port_BugReport_InstallCrashHandlers();
    }

    // Initialize REG_KEYINPUT to all-keys-released (GBA: 1=not pressed)
    *(u16*)(gIoMem + REG_OFFSET_KEYINPUT) = 0x03FF;

    Port_Config_Load("config.json");

    /* Honour the persisted save-profile choice. Default ("tmc.sav") is
     * applied if the config doesn't name one. */
    {
        extern const char* Port_Config_ActiveSaveProfile(void);
        extern void Port_Save_SetActivePath(const char* path);
        Port_Save_SetActivePath(Port_Config_ActiveSaveProfile());
    }

    /* Auto-save defaults to on. Apply the persisted interval too. */
    {
        extern bool Port_Config_AutosaveEnabled(void);
        extern u32  Port_Config_AutosaveIntervalMs(void);
        extern void Port_QuickSave_SetAutoEnabled(int enabled);
        extern void Port_QuickSave_SetAutoIntervalMs(u32 ms);
        Port_QuickSave_SetAutoEnabled(Port_Config_AutosaveEnabled() ? 1 : 0);
        Port_QuickSave_SetAutoIntervalMs(Port_Config_AutosaveIntervalMs());
    }

    u8 window_scale = Port_Config_WindowScale();
    bool noAudio = false;
    const char* glslpPath = NULL;
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
            else if (strcmp(argv[i], "--loose-assets") == 0) {
                Port_LooseAssetsRequested = 1;
            }
            else if (strcmp(argv[i], "--no-audio") == 0) {
                noAudio = true;
            }
            else if (strncmp(argv[i], "--glslp=", 8) == 0) {
                glslpPath = argv[i] + 8;
            }
            else if (strcmp(argv[i], "--help") == 0) {
                fprintf(stderr, "Usage: %s [--window_scale=<value>] [--loose-assets] [--no-audio] [--glslp=<path>]\n", argv[0]);
                fprintf(stderr, "  --window_scale=<value>: Set the window scale (1-10, default is 3)\n");
                fprintf(stderr, "  --loose-assets:         Ignore assets/*.pak archives and read loose files instead.\n");
                fprintf(stderr, "  --no-audio:             Skip audio init (workaround for agbplay crash)\n");
                fprintf(stderr, "  --glslp=<path>:         Load a libretro .glslp shader preset (requires --gpu_renderer=y build).\n");
                fprintf(stderr, "                          Equivalent to setting TMC_GLSLP_PRESET=<path> env var.\n");
                fprintf(stderr, "  config.json: Set window_scale and bindings defaults\n");
                return 0;
            }
            else {
                fprintf(stderr, "Unknown argument: %s\n", argv[i]);
            }
        }
    }
    /* Expose the path to port_gpu_renderer's startup probe by setting
     * the env var the existing TMC_GLSLP_PRESET check reads. Keeps the
     * load path single-sourced — CLI just becomes another way to fill
     * the same env var. setenv lives behind POSIX feature macros that
     * the build doesn't define on every platform; use a forward extern
     * declaration so we don't need to crank up _POSIX_C_SOURCE. */
    if (glslpPath) {
#if defined(_WIN32)
        /* MinGW lacks POSIX setenv; _putenv_s is the equivalent. */
        extern int _putenv_s(const char*, const char*);
        _putenv_s("TMC_GLSLP_PRESET", glslpPath);
#else
        extern int setenv(const char*, const char*, int);
        setenv("TMC_GLSLP_PRESET", glslpPath, /*overwrite=*/1);
#endif
    }

    // Initialize SDL video first. Audio is optional and handled separately.
    if (!Port_InitVideo()) {
        return 1;
    }

    Port_Config_OpenGamepads();

    /* ROM probe deferred — the prelaunch screen (rendered after PPU
     * init below) handles ROM selection via Port_RomPicker_PromptAndInstall.
     * On first launch with no ROM, the prelaunch shows a "Select ROM..."
     * card. Once the user picks a valid dump, we drop into the regular
     * Port_LoadRom / asset-extract / audio-init chain. */
    const char* romPath = NULL;

    /* Use SDL_CreateWindowAndRenderer so SDL picks the renderer
     * driver (opengl/vulkan/...) and creates the window with the
     * matching visual flags atomically. Calling SDL_CreateRenderer
     * AFTER SDL_CreateWindow on Linux/X11 forces SDL to internally
     * destroy and recreate the X11 window to add SDL_WINDOW_OPENGL,
     * which the user sees as "first window opens, goes black,
     * closes, then second window opens." Confirmed by H9 logs:
     * window flags went from 0x220 → 0x222 across the first
     * SDL_CreateRenderer call, with driver=opengl. */
    SDL_Window* window = NULL;
#ifndef TMC_GPU_RENDERER
    SDL_Renderer* prerenderer = NULL;
#endif
    /* TMC_PC_VERSION is defined by xmake.lua via add_defines("TMC_PC_VERSION=\"...\"") */
    char window_title[64];
    SDL_snprintf(window_title, sizeof(window_title), "The Minish Cap " TMC_PC_VERSION);
#ifdef TMC_GPU_RENDERER
    /* SDL_GPU wants exclusive ownership of the window's swapchain
     * surface. If we go through SDL_CreateWindowAndRenderer, the
     * SDL_Renderer atomically creates its Vulkan surface and SDL_GPU's
     * subsequent ClaimWindow gets back VK_ERROR_SURFACE_LOST_KHR
     * (Wayland: "surface already exists"). Use bare SDL_CreateWindow
     * so SDL_GPU can claim the swapchain cleanly. The bootstrap-splash
     * code paths still call Port_PaintBootSplash unconditionally; on
     * GPU builds that's a no-op (no SDL_Renderer to draw on) — the boot
     * splash trades away on this build for the GPU pipeline.
     *
     * Non-Apple platforms keep SDL_WINDOW_VULKAN here to preserve the
     * existing Vulkan swapchain path. On macOS, that flag makes
     * SDL_CreateWindow call SDL_Vulkan_LoadLibrary immediately and abort
     * startup if MoltenVK is not bundled, before AUTO can fall back to
     * the software SDL_Renderer path. Leave the window Vulkan-flag-free
     * on macOS; SDL_GPU can still attempt its claim later, and failure
     * there falls back cleanly in Port_PPU_Init. */
    SDL_WindowFlags window_flags = SDL_WINDOW_RESIZABLE;
#if !defined(__APPLE__)
    window_flags |= SDL_WINDOW_VULKAN;
#endif
    window = SDL_CreateWindow(window_title,
                              240 * window_scale, 160 * window_scale,
                              window_flags);
    if (!window) {
        fprintf(stderr, "SDL_CreateWindow Error: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }
#else
    if (!SDL_CreateWindowAndRenderer(
            window_title,
            240 * window_scale, 160 * window_scale,
            SDL_WINDOW_RESIZABLE,
            &window, &prerenderer)) {
        fprintf(stderr, "SDL_CreateWindowAndRenderer Error: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    (void)prerenderer; /* Owned by the window; retrieved via SDL_GetRenderer(window) later. */
#endif

    /* Set window icon BEFORE first present so the title-bar and
     * taskbar entry never flash the default SDL icon. */
    {
        extern SDL_Surface* Port_CreateAppIcon(void);
        SDL_Surface* icon = Port_CreateAppIcon();
        if (icon) {
            SDL_SetWindowIcon(window, icon);
            SDL_DestroySurface(icon);
        }
    }

    SDL_ShowWindow(window);
    SDL_RaiseWindow(window);
    SDL_SyncWindow(window);

#ifdef launcher
    if (!Port_RunBootstrapLauncher(window)) {
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 0;
    }
#endif

    /* Paint a "LOADING" splash IMMEDIATELY so the window never
     * shows a blank black rectangle.
     *
     * GPU build: stand up SDL_GPU EARLY (before any boot UI) so its
     * swapchain claim doesn't fight an already-present SDL_Renderer
     * surface. After ClaimWindow succeeds, Port_GPU_PaintBootSplash
     * clears the swapchain to a dark teal — replaces the
     * SDL_Renderer-based splash on GPU builds with something the user
     * can see (it's not a blank window any more). Asset extractor's
     * progress bar still uses SDL_Renderer though, so we still pass
     * NULL window to the extractor on GPU builds.
     *
     * Non-GPU build: the SDL_Renderer that SDL_CreateWindowAndRenderer
     * created is already attached to the window; Port_PaintBootSplash
     * fetches it via SDL_GetRenderer and presents the "LOADING" card. */
#ifdef TMC_GPU_RENDERER
    /* Skip GPU init entirely if the user pinned the renderer to
     * Software in F8 — otherwise the GPU device claims the window
     * (esp. the wayland surface) and the later SDL_Renderer create
     * in Port_PPU_Init conflicts with it ("Surface got destroyed
     * already" / "Wayland display connection closed by server"
     * fatal error). Force-GPU and Auto both want the early init. */
    if (Port_Config_RenderBackend() != PORT_RENDER_BACKEND_SOFTWARE) {
        extern bool Port_GPU_Init(SDL_Window*);
        extern bool Port_GPU_ClaimWindow(SDL_Window*, int, int);
        extern bool Port_GPU_PaintBootSplash(void);
        Port_GPU_Init(window);
        Port_GPU_ClaimWindow(window, MODE1_GBA_WIDTH, MODE1_GBA_HEIGHT);
        Port_GPU_PaintBootSplash();
    } else {
        Port_PaintBootSplash(window, "LOADING");
    }
#else
    Port_PaintBootSplash(window, "LOADING");
#endif

    /* PPU init first — creates the SDL_Renderer / SDL_GPU device + the
     * ImGui context. Doesn't touch ROM data, so we can stand up the
     * prelaunch UI before the user has even pointed us at a .gba.
     *
     * On non-GPU builds the GPU stub still needs to fire for the
     * build-flag-off no-op path. */
#ifndef TMC_GPU_RENDERER
    {
        extern bool Port_GPU_Init(SDL_Window*);
        Port_GPU_Init(window);
    }
#endif
    Port_PPU_Init(window);

#ifdef TMC_GPU_RENDERER
    {
        extern bool Port_GPU_PaintBootSplash(void);
        Port_GPU_PaintBootSplash();
    }
#else
    Port_PaintBootSplash(window, "LOADING");
#endif
    fprintf(stderr, "PPU init complete.\n");

    /* TTS init runs after PPU so ImGui is up (the F8 menu's TTS tab
     * uses it immediately) but well before AgbMain — accessibility
     * announcements for the prelaunch screen need a working backend
     * BEFORE the prelaunch frame loop runs. Idempotent + no-op if no
     * backend is available; never blocks. */
    {
        extern bool Port_TTS_Init(void);
        Port_TTS_Init();
    }

    /* ====================================================================
     * Project Picori prelaunch screen.
     *
     * Renders before any ROM is loaded. If no .gba is present next to
     * the exe, the card shows a "Select your ROM" prompt and a big
     * Select-ROM button. Once the user picks a valid dump (SHA-1
     * matched against the known TMC hashes — see port_rom_picker.c),
     * the card flips to the regular state with version / ROM / Play.
     *
     * Play exits the loop and we fall through to Port_LoadRom + asset
     * extraction + audio init. Quit during the loop cleanly shuts down
     * the partial init we did so far.
     * ==================================================================== */
    {
        extern bool Port_ImGui_RenderPrelaunch(bool, const char*, const char*, bool*, bool*);
        extern void Port_ImGui_HandleEvent(const SDL_Event*);
        extern bool Port_GPU_PresentPrelaunchFrame(void);
        extern bool Port_GPU_PaintBootSplash(void);
        extern void Port_GPU_Shutdown(void);
        extern void Port_DiscordRpc_Shutdown(void);
        extern int  Port_RomPicker_PromptAndInstall(void);

        romPath = Port_FindBaseRomPath();
        fprintf(stderr, "Prelaunch: %s — waiting for user.\n",
                romPath ? "ROM detected" : "no ROM yet");

        /* Announce the prelaunch screen for screen-reader users.
         * Port_TTS_Init ran a few lines above so the backend is up;
         * Port_TTS_Speak is a no-op when TTS is disabled in config. */
        {
            PortTtsOptions opts = {0};
            opts.priority = PORT_TTS_PRIO_URGENT;
            opts.rate = opts.pitch = opts.volume = 0.0f / 0.0f;
            opts.dedupe = false;
            if (romPath) {
                Port_TTS_Speak("Project Picori, the Minish Cap PC port. "
                               "ROM detected. Press Enter or Space to play.",
                               &opts);
            } else {
                Port_TTS_Speak("Project Picori, the Minish Cap PC port. "
                               "No ROM found. Press Enter or Space to "
                               "select your Minish Cap GBA ROM file.",
                               &opts);
            }
        }

        bool done = false;
        if (romPath && getenv("TMC_AUTOPLAY")) {
            fprintf(stderr, "Prelaunch: TMC_AUTOPLAY set — skipping menu.\n");
            done = true;
        }
        while (!done) {
            /* Re-derive the displayable filename each iteration in case
             * a Change-ROM swap moved the file. */
            char rom_name_buf[1024];
            const char* rom_name = "(none)";
            if (romPath) {
                const char* slash = strrchr(romPath, '/');
#ifdef _WIN32
                const char* bslash = strrchr(romPath, '\\');
                if (bslash && (!slash || bslash > slash)) slash = bslash;
#endif
                if (slash && slash[1]) {
                    snprintf(rom_name_buf, sizeof(rom_name_buf), "%s", slash + 1);
                } else {
                    snprintf(rom_name_buf, sizeof(rom_name_buf), "%s", romPath);
                }
                rom_name = rom_name_buf;
            }

            const bool rom_present = (romPath != NULL);
            bool play_clicked = false;
            bool change_rom_clicked = false;
            const bool imgui_built = Port_ImGui_RenderPrelaunch(
                rom_present, TMC_PC_VERSION, rom_name,
                &play_clicked, &change_rom_clicked);
#ifdef TMC_GPU_RENDERER
            if (imgui_built) {
                Port_GPU_PresentPrelaunchFrame();
            } else {
                Port_GPU_PaintBootSplash();
            }
#else
            (void)imgui_built; /* SDL_Renderer path presents inline. */
#endif

            if (play_clicked && rom_present) {
                fprintf(stderr, "Prelaunch: play_clicked.\n");
                done = true;
            } else if (change_rom_clicked) {
                fprintf(stderr, "Prelaunch: change_rom_clicked — calling picker.\n");
                int rc = Port_RomPicker_PromptAndInstall();
                fprintf(stderr, "Prelaunch: picker returned %d.\n", rc);
                if (rc == 0) {
                    romPath = Port_FindBaseRomPath();
                    fprintf(stderr, "Prelaunch: post-picker romPath=%s.\n",
                            romPath ? romPath : "(still NULL)");
                    /* Re-announce so the user hears the new state
                     * without having to look at the screen. */
                    {
                        PortTtsOptions opts = {0};
                        opts.priority = PORT_TTS_PRIO_URGENT;
                        opts.rate = opts.pitch = opts.volume = 0.0f / 0.0f;
                        opts.dedupe = false;
                        if (romPath) {
                            Port_TTS_Speak("ROM loaded. Press Enter or "
                                           "Space to play.", &opts);
                        } else {
                            Port_TTS_Speak("No ROM selected.", &opts);
                        }
                    }
                }
            } else if (play_clicked) {
                fprintf(stderr, "Prelaunch: play_clicked but no ROM (ignored).\n");
            }

            SDL_Event ev;
            while (SDL_PollEvent(&ev)) {
                Port_ImGui_HandleEvent(&ev);
                if (ev.type == SDL_EVENT_QUIT) {
                    Port_GPU_Shutdown();
                    Port_DiscordRpc_Shutdown();
                    Port_PPU_Shutdown();
                    Port_Config_CloseGamepads();
                    SDL_DestroyWindow(window);
                    SDL_Quit();
                    return 0;
                }
            }
            SDL_Delay(16);
        }
        fprintf(stderr, "Prelaunch: Play — loading ROM and assets.\n");
    }

    /* Play pressed and romPath is set. The rest of the original init
     * order runs now (ROM load → asset extraction → mods → audio). */
    Port_LoadRom(romPath);
#ifdef TMC_GPU_RENDERER
    Port_EnsureAssetsReadyWithDisplay(NULL, gRomData, gRomSize);
#else
    Port_EnsureAssetsReadyWithDisplay(window, gRomData, gRomSize);
#endif
    Port_CheckForUpdates(window);

    /* Mod loader (Tier 1: asset overrides). Scans <exe>/mods/ for
     * subdirectories whose files shadow runtime asset paths. Active set
     * controlled by TMC_MODS env var; otherwise all mods/* directories
     * are loaded in alphabetical order. */
    {
        extern void Port_Mods_Init(void);
        Port_Mods_Init();
    }

    /* Now that the ROM and asset tables are loaded, re-set the window
     * icon — Port_CreateAppIcon prefers the ROM-extracted Ezlo sprite
     * over the procedural fallback once gRomData/gSpritePtrs/gFrameObjLists
     * are populated. */
    {
        extern SDL_Surface* Port_CreateAppIcon(void);
        SDL_Surface* icon = Port_CreateAppIcon();
        if (icon) {
            SDL_SetWindowIcon(window, icon);
            SDL_DestroySurface(icon);
        }
    }

    /* Region cross-check between the compile-time `EU` macro and the
     * runtime-detected ROM region. Mismatch is non-fatal but flags
     * that asset offsets may be wrong. */
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

    if (noAudio) {
        gMain.muteAudio = 1;
        fprintf(stderr, "Audio disabled by --no-audio flag.\n");
    } else {
        Port_InitAudio();
        fprintf(stderr, "Audio init complete.\n");
    }

    /* Last bridging splash before the game's title fade-in takes
     * over. After this the engine drives the frame loop. */
#ifdef TMC_GPU_RENDERER
    {
        extern bool Port_GPU_PaintBootSplash(void);
        Port_GPU_PaintBootSplash();
    }
#else
    Port_PaintBootSplash(window, "STARTING");
#endif
    fprintf(stderr, "Port layer initialized. Entering AgbMain...\n");

    AgbMain();

    {
        extern void Port_TTS_Shutdown(void);
        Port_TTS_Shutdown();
    }
    {
        extern void Port_GPU_Shutdown(void);
        Port_GPU_Shutdown();
    }

    {
        extern void Port_DiscordRpc_Shutdown(void);
        Port_DiscordRpc_Shutdown();
    }
    Port_Audio_Shutdown();
    Port_PPU_Shutdown();
    Port_Config_CloseGamepads();
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
