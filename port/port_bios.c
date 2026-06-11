#include "gba/io_reg.h"
#include "save.h"   /* gSave for Discord-RPC stat reads */
#include "main.h"
#include "port_audio.h"
#include "port_asset_loader.h"
#include "port_gba_mem.h"
#include "port_hdma.h"
#include "port_ppu.h"
#include "port_runtime_config.h"
#include "port_softslots.h"
#include "port_touch_controls.h"
#include "rando/rando_file_menu.h"
#include "port_types.h"
#include <SDL3/SDL.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <setjmp.h>

static bool gQuitRequested = false;
static bool sFastForward = false;
static int sFrameNum = 0;

/* Soft-reset re-entry. AgbMain arms this with setjmp() at the top of the
 * game loop; SoftReset() longjmp()s back to re-run AgbMain's init (which
 * RegisterRamResets everything except EWRAM, preserving save data) — the
 * native equivalent of the GBA BIOS soft reset back to the title. Shared
 * with src/main.c (extern there). */
jmp_buf gPortSoftResetJmp;
int gPortSoftResetArmed = 0;

static const void* Port_ResolveCopySrc(const void* src, u32 size) {
    if (Port_IsLoadedAssetBytes(src, size)) {
        return src;
    }
    return port_resolve_addr((uintptr_t)src);
}

typedef struct {
    PortInput input;
    u16 gbaMask;
} PortInputMapEntry;

static const PortInputMapEntry sInputMap[] = {
    { PORT_INPUT_A, A_BUTTON },
    { PORT_INPUT_B, B_BUTTON },
    { PORT_INPUT_SELECT, SELECT_BUTTON },
    { PORT_INPUT_START, START_BUTTON },
    { PORT_INPUT_RIGHT, DPAD_RIGHT },
    { PORT_INPUT_LEFT, DPAD_LEFT },
    { PORT_INPUT_UP, DPAD_UP },
    { PORT_INPUT_DOWN, DPAD_DOWN },
    { PORT_INPUT_R, R_BUTTON },
    { PORT_INPUT_L, L_BUTTON },
};

extern Main gMain;
extern void VBlankIntr(void);

u64 DivAndModCombined(s32 num, s32 denom) {
    s32 quotient;
    s32 remainder;

    if (denom == 0)
        return 0;

    quotient = num / denom;
    remainder = num % denom;
    return ((u64)(u32)remainder << 32) | (u32)quotient;
}

static void Port_UpdateInput(void) {
    u16 keyinput = 0x03FF;

    /* Headless end-to-end randomizer check (TMC_REPRO_RANDO=1): drives the
     * file-select overlay + generation + the engine item-override hooks and
     * asserts items actually change. Runs BEFORE the overlay input mask so
     * it can keep ticking (and push synthetic SDL events) while its own
     * modal is open — the ImGui-keyboard stage depends on that. */
    {
        extern void Port_ReproRando_Tick(unsigned int frame);
        Port_ReproRando_Tick(sFrameNum);
    }

    {
        extern bool Port_DebugMenu_IsOpen(void);
        /* While either overlay is open, hold all GBA buttons released so
         * the game doesn't observe stray input from key presses we routed
         * to the overlay. The soft-slot configuration overlay piggybacks
         * on this behaviour while it's the active focus. */
        if (Port_DebugMenu_IsOpen() || Port_SoftSlots_ConfigIsOpen() ||
            Port_InGameSettingsModalIsOpen() || Port_RandoFileMenu_IsOpen()) {
            *(vu16*)(gIoMem + REG_OFFSET_KEYINPUT) = keyinput;
            Port_SoftSlots_TickPause();
            sFrameNum++;
            return;
        }
    }

    for (size_t i = 0; i < sizeof(sInputMap) / sizeof(sInputMap[0]); i++) {
        if (Port_Config_InputPressed(sInputMap[i].input)) {
            keyinput &= ~sInputMap[i].gbaMask;
        }
    }

    /* Soft-slots (X / Y / L2 / R2): when one is held with an item
     * assigned, force GBA B_BUTTON pressed so the engine spawns the
     * soft-slot's item via the regular B-dispatch path. The override of
     * which item to spawn lives in src/playerUtils.c via
     * Port_SoftSlots_GetEffectiveBItem(); the save data is untouched. */
    Port_SoftSlots_Update();
    if (Port_SoftSlots_IsBHeld()) {
        keyinput &= ~B_BUTTON;
    }

    /* Decay the pause-active grace counter. The engine's Subtask_PauseMenu
     * pumps it back up to N each frame the start menu is open, so this
     * naturally drops to 0 a few frames after the menu closes. */
    Port_SoftSlots_TickPause();

    *(vu16*)(gIoMem + REG_OFFSET_KEYINPUT) = keyinput;

    /* Edge cache served its purpose for this frame's KEYINPUT — clear
     * so the next frame starts fresh and a held key reverts to the
     * polled-state path. */
    Port_Config_ClearInputEdges();

    sFrameNum++;
    if (gMain.task == 0 && sFrameNum > 300 && sFrameNum < 310) {
        *(vu16*)(gIoMem + REG_OFFSET_KEYINPUT) &= ~START_BUTTON;
    }

    /* Issue #99 auto-repro harness. Set TMC_REPRO_MAZAAL=1 to drive
     * the game from the title screen into the FoW boss room mid-
     * phase-3 so the [mazaal] diagnostic logs in mazaalMacro.c
     * capture the pillar-spawn state on the broken path without
     * needing a human to navigate. Exits at frame 4000. */
    {
        extern void Port_ReproMazaal_Tick(unsigned int frame);
        Port_ReproMazaal_Tick(sFrameNum);
    }

    /* Issues #138/#79 auto-repro harness. Set TMC_REPRO_LITAREA=1 to warp
     * into a litArea OBJ-window dark room and capture the framebuffer. */
    {
        extern void Port_ReproLitArea_Tick(unsigned int frame);
        Port_ReproLitArea_Tick(sFrameNum);
    }

    /* Four Sword Sanctuary clone/button crash repro (followup to #130).
     * Set TMC_REPRO_CLONEBUTTON=1 to warp into the sanctuary and exercise
     * sub_08081FF8 with a NULL-hitbox clone in gPlayerClones[]. */
    {
        extern void Port_ReproCloneButton_Tick(unsigned int frame);
        Port_ReproCloneButton_Tick(sFrameNum);
    }

    /* Vaati Castle takeover cutscene repro (#93/#109). Set TMC_REPRO_TAKEOVER=1
     * to launch the takeover aux-cutscene and watch the inner orchestrator's
     * [orch-pc] trace (native script drives it by default; TMC_TAKEOVER_WD=1
     * forces the old sub_08053BBC fallback watchdog). */
    {
        extern void Port_ReproTakeover_Tick(unsigned int frame);
        Port_ReproTakeover_Tick(sFrameNum);
    }

    /* JailBars prison-door texture repro (#149). Set TMC_REPRO_JAILBARS=1 to
     * warp into DHC B2 Prison, open the door via its flag, and confirm the
     * door reaches action 3 (open frame) instead of stalling in action 2. */
    {
        extern void Port_ReproJailBars_Tick(unsigned int frame);
        Port_ReproJailBars_Tick(sFrameNum);
    }

    /* AngryStatue reward-flag repro (#77). Set TMC_REPRO_ANGRYSTATUE=1 to warp
     * into DHC 1F Loop Left, destroy the four statues at once, and confirm the
     * manager's completion flag (field_0x3e) is populated and gets set. */
    {
        extern void Port_ReproAngryStatue_Tick(unsigned int frame);
        Port_ReproAngryStatue_Tick(sFrameNum);
    }

    /* Vaati transform crash repro (#151). Set TMC_REPRO_VAATI=1 (+ TMC_VAATI_SLOT)
     * to restore a boss-fight save-state snapshot and let the transform run. */
    {
        extern void Port_ReproVaati_Tick(unsigned int frame);
        Port_ReproVaati_Tick(sFrameNum);
    }

    /* "Credits don't load" repro. Set TMC_REPRO_CREDITS=1 to force the
     * staffroll task (the ending script's SetTask(TASK_STAFFROLL)) and
     * snapshot the credits framebuffer headless. */
    {
        extern void Port_ReproCredits_Tick(unsigned int frame);
        Port_ReproCredits_Tick(sFrameNum);
    }

    /* Performance-capture harness (TMC_PERFCAP=1): drive into gameplay and
     * dump a complete PPU snapshot for the standalone render microbench. */
    {
        extern void Port_ReproPerfcap_Tick(unsigned int frame);
        Port_ReproPerfcap_Tick(sFrameNum);
    }

    /* "Crash talking to the cat woman" repro (#152). Set TMC_REPRO_CATPERSON=1
     * to drive in-game and invoke the cat person's talk handler (sub_08062048,
     * type 0x0e, global_progress 5) — the DIALOG_CALL_FUNC path that jumped to a
     * raw GBA address before the fix. */
    {
        extern void Port_ReproCatPerson_Tick(unsigned int frame);
        Port_ReproCatPerson_Tick(sFrameNum);
    }

    /* Randomizer cosmetic palette overrides (tunic / heart colors from
     * !eventdefine). Content-addressed gPaletteBuffer rewrite; runs before
     * WaitForNextFrame()'s FadeVBlank() upload. No-op without an active
     * seed. See port/rando/rando_cosmetic.cpp. */
    {
        extern void Rando_Cosmetic_Tick(void);
        Rando_Cosmetic_Tick();
    }

    /* Post-warp safe-spawn nudge (issue #94). No-op except in the few
     * frames after a debug-menu warp completes. */
    {
        extern void Port_DebugAction_WarpTick(void);
        Port_DebugAction_WarpTick();
    }

}

static void Port_PumpEvents(void) {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        /* Forward every event to ImGui so its window/keyboard tracking
         * stays in sync. ImGui only consumes input when a widget is
         * actively hovered/focused; game input passes through. */
        {
            extern void Port_ImGui_HandleEvent(const SDL_Event*);
            Port_ImGui_HandleEvent(&e);
        }
        if (e.type == SDL_EVENT_QUIT) {
            /* Route the close-button (X) / OS-quit signal through the
             * ImGui modal first so users get a chance to save. The
             * modal sets Port_ImGui_QuitConfirmed() once the user
             * picks Save&Quit or Quit-Without-Saving; the per-frame
             * check below promotes that into gQuitRequested. */
            extern void Port_ImGui_RequestQuitModal(void);
            Port_ImGui_RequestQuitModal();
            continue;
        }
        if (Port_RandoFileMenu_IsOpen()) {
            /* The ImGui modal owns the randomizer-setup input (the event
             * was already forwarded to ImGui above); swallow everything
             * else so game hotkeys stay masked while it is open. */
            continue;
        }
        if (e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat) {
            /* Soft-slot config overlay is highest priority: it consumes
             * navigation keys before the rest of the routing fires. */
            if (Port_SoftSlots_ConfigIsOpen()) {
                if (Port_SoftSlots_ConfigHandleKey((int)e.key.key)) {
                    continue;
                }
            } else if (e.key.key == SDLK_BACKSLASH && Port_SoftSlots_IsPauseActive()) {
                Port_SoftSlots_ConfigOpen();
                continue;
            }
            bool altHeld = (e.key.mod & SDL_KMOD_ALT) != 0;
            if (e.key.key == SDLK_F11 || (e.key.key == SDLK_RETURN && altHeld)) {
                Port_PPU_ToggleFullscreen();
                continue;
            }
            if (e.key.key == SDLK_F12) {
                Port_PPU_ToggleSmoothing();
                continue;
            }
            if (e.key.key == SDLK_F8) {
                extern void Port_DebugMenu_Toggle(void);
                Port_DebugMenu_Toggle();
                continue;
            }
            if (e.key.key == SDLK_F7) {
                /* TTS master toggle — works whether the F8 menu is
                 * open or not so screen-reader users don't need to
                 * navigate the menu to flip TTS off. */
                extern bool Port_TTS_GetEnabled(void);
                extern void Port_TTS_SetEnabled(bool);
                extern void Port_TTS_AnnounceMessage(const char*);
                bool now = !Port_TTS_GetEnabled();
                Port_TTS_SetEnabled(now);
                /* Announce the new state if we just turned ON; if
                 * turning OFF, SetEnabled already cleared the queue. */
                if (now) Port_TTS_AnnounceMessage("Text to speech enabled.");
                continue;
            }
            if (e.key.key == SDLK_F6 &&
                (e.key.mod & (SDL_KMOD_SHIFT | SDL_KMOD_CTRL | SDL_KMOD_ALT)) == 0) {
                /* F6 (no modifiers) stops TTS. The unmodified F6
                 * was previously quickload — that one keeps its
                 * Shift+F6 / Ctrl+F6 bindings. Plain F6 is now the
                 * universal "shut up" key for the TTS user. */
                extern void Port_TTS_Stop(void);
                Port_TTS_Stop();
                /* Don't `continue` — fall through so quickload still
                 * fires on shifted F6 below. Actually unconditional
                 * continue here changes existing behaviour; leave
                 * quickload as-is and let F6 do BOTH (stop speech +
                 * quickload). Stopping speech is harmless during a
                 * load. */
            }
            if (e.key.key == SDLK_F9) {
                /* Capture a bug-report bundle (screenshot + save + state
                 * dump) into a timestamped folder next to the binary so
                 * playtesters can attach it to a GitHub issue. */
                extern char* Port_BugReport_Capture(const char* reason);
                extern void  Port_DebugMenu_ToastFromExternal(const char* msg);
                char* dir = Port_BugReport_Capture("user");
                if (dir) {
                    /* Resolve absolute path so the user knows exactly
                     * where the bundle ended up (CWD varies by launcher
                     * — Steam, double-click, terminal). realpath needs
                     * stdlib.h; declared inline so we don't pull a
                     * heavy header chain just for this.  Windows/MinGW
                     * uses _fullpath with the same signature shape. */
                    char  abs[4096];
                    char* resolved;
#ifdef _WIN32
                    extern char* _fullpath(char*, const char*, size_t);
                    resolved = _fullpath(abs, dir, sizeof(abs));
#else
                    extern char* realpath(const char*, char*);
                    resolved = realpath(dir, abs);
#endif
                    const char* shown = resolved ? abs : dir;
                    fprintf(stderr, "[BUG] F9 capture → %s\n", shown);
                    char msg[256];
                    snprintf(msg, sizeof(msg), "Bug report saved: %.230s",
                             resolved ? abs : dir);
                    Port_DebugMenu_ToastFromExternal(msg);
                    free(dir);
                } else {
                    fprintf(stderr, "[BUG] F9 capture FAILED — check stderr for mkdir errors\n");
                    Port_DebugMenu_ToastFromExternal("Bug report capture FAILED — see stderr");
                }
                continue;
            }
            if (e.key.key == SDLK_F5) {
                extern int Port_QuickSave(void);
                Port_QuickSave();
                continue;
            }
            if (e.key.key == SDLK_F6) {
                extern int Port_QuickLoad(void);
                Port_QuickLoad();
                continue;
            }
            /* F1..F4 — numbered save-state slots. Plain press = load,
             * Shift+Fn = save. Mirrors emulator-style hotkey conventions.
             * (Matheo's branch bound F1 to an InGameSettingsModal; we
             * already surface those settings via F8 → Reborn / Display
             * tabs, so the save-state binding wins here.) */
            if (e.key.key >= SDLK_F1 && e.key.key <= SDLK_F4) {
                extern int Port_QuickSave_SaveSlot(int slot);
                extern int Port_QuickSave_LoadSlot(int slot);
                const int slot = (int)(e.key.key - SDLK_F1) + 1;
                const bool shift = (e.key.mod & SDL_KMOD_SHIFT) != 0;
                if (shift) {
                    Port_QuickSave_SaveSlot(slot);
                } else {
                    Port_QuickSave_LoadSlot(slot);
                }
                continue;
            }
            /* When the debug menu is open, route key presses to it and
             * suppress further handling so the game itself doesn't see
             * the keystroke. */
            {
                extern bool Port_DebugMenu_IsOpen(void);
                extern bool Port_DebugMenu_HandleKey(int sdlKey);
                if (Port_DebugMenu_IsOpen() && Port_DebugMenu_HandleKey((int)e.key.key)) {
                    continue;
                }
            }
            if (e.key.key == SDLK_TAB) {
                sFastForward = true;
                continue;
            }
        }
        if (e.type == SDL_EVENT_KEY_UP && e.key.key == SDLK_TAB) {
            sFastForward = false;
            continue;
        }
        /* Fast-forward via keyboard TAB only. The previous RIGHT_TRIGGER
         * gamepad shortcut conflicted with the default soft-slot R2 binding
         * (port_softslots.c) — pulling the trigger would simultaneously
         * fast-forward and fire a soft-slot item. */

        /* Gamepad shortcut to open the F8 debug menu: Select + Start.
         * Neither button alone is overloaded (Select opens the map in
         * the engine; Start opens the pause menu), but the combo is
         * unused by the game so it's a safe binding for Steam Deck
         * users who don't have a keyboard within reach. Tracker pair is
         * file-scope-static so it survives across SDL events; we update
         * on both up and down edges so a release-and-re-press re-arms.
         * The toggle fires once on the down-edge of whichever button
         * completes the pair. */
        {
            static bool s_select_held = false, s_start_held = false;
            const bool is_down = (e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN);
            const bool is_up   = (e.type == SDL_EVENT_GAMEPAD_BUTTON_UP);
            if (is_down || is_up) {
                if (e.gbutton.button == SDL_GAMEPAD_BUTTON_BACK)  s_select_held = is_down;
                if (e.gbutton.button == SDL_GAMEPAD_BUTTON_START) s_start_held  = is_down;
                if (is_down && s_select_held && s_start_held) {
                    extern void Port_DebugMenu_Toggle(void);
                    Port_DebugMenu_Toggle();
                    s_select_held = false;
                    s_start_held  = false;
                }
            }
        }

        Port_Config_HandleEvent(&e);
    }

    if (Port_TouchControls_ConsumeSettingsRequest()) {
        extern bool Port_DebugMenu_IsOpen(void);
        if (!Port_DebugMenu_IsOpen() && !Port_SoftSlots_ConfigIsOpen() &&
            !Port_InGameSettingsModalIsOpen()) {
            Port_OpenInGameSettingsModal();
        }
    }
}


static u64 lastFrameNs = 0;
static u64 sFpsWindowStartNs = 0;
static u32 sFpsFrameCount = 0;

/* --- Performance phase timers (TMC_PROFILE=1). Off by default; cheap (two
 * SDL_GetTicksNS reads per frame). Decomposes each uncapped frame into
 * game-tick (engine + entity update), present (all of Port_PPU_PresentFrame),
 * and the PPU rasterizer (virtuappu_render_frame, accumulated by
 * port_ppu.cpp). Reports a rolling average to stderr every 600 frames. */
static u64 sProfLastPresentEndNs = 0;
static u64 sProfAccGameNs = 0;
static u64 sProfAccPresentNs = 0;
static u32 sProfFrames = 0;
u64 gPortProfileRenderNs = 0; /* updated from port_ppu.cpp */

int Port_Profile_Enabled(void) {
    static int en = -1;
    if (en < 0) {
        const char* e = getenv("TMC_PROFILE");
        en = (e && *e && e[0] != '0') ? 1 : 0;
    }
    return en;
}

void VBlankIntrWait(void) {
    u64 nowNs;

    /* Toggle VSync based on whether we're trying to run faster than the
     * display refresh: fast-forward, or a target FPS preset > 60. With
     * VSync on, SDL_RenderPresent caps us at the display rate regardless
     * of the busy-wait timer below — so #26 reports of fast-forward and
     * the FPS preset menu having no effect on Windows are actually the
     * display refresh holding us. */
    {
        u32 targetFps = Port_Config_TargetFps();
        bool wantVsync = !sFastForward && targetFps != 0 && targetFps <= 60;
        Port_PPU_SetVSync(wantVsync);
    }

    if (Port_Profile_Enabled()) {
        u64 entry = SDL_GetTicksNS();
        if (sProfLastPresentEndNs != 0) {
            sProfAccGameNs += entry - sProfLastPresentEndNs;
        }
        Port_PPU_PresentFrame();
        u64 pEnd = SDL_GetTicksNS();
        sProfAccPresentNs += pEnd - entry;
        sProfLastPresentEndNs = pEnd;
        if (++sProfFrames >= 600) {
            double n = (double)sProfFrames;
            double game = sProfAccGameNs / 1e6 / n;
            double present = sProfAccPresentNs / 1e6 / n;
            double render = gPortProfileRenderNs / 1e6 / n;
            double total = game + present;
            fprintf(stderr,
                    "[profile] %u frames: game=%.3f present=%.3f (render=%.3f) ms/frame | "
                    "%.0f fps uncapped | render=%.0f%% of frame\n",
                    sProfFrames, game, present, render,
                    total > 0.0 ? 1000.0 / total : 0.0,
                    total > 0.0 ? 100.0 * render / total : 0.0);
            sProfAccGameNs = sProfAccPresentNs = 0;
            gPortProfileRenderNs = 0;
            sProfFrames = 0;
        }
    } else {
        Port_PPU_PresentFrame();
    }
    port_hdma_vblank_reset();

    /* Deadline-based pacing: each frame's target is the previous
     * frame's target + frameTimeNs (a fixed cadence on an ideal grid),
     * not "now + frameTimeNs" (which drifts as game-tick work load
     * varies). The drift version produced visible micro-stutter when
     * a heavy frame consumed a few ms more than usual; deadline pacing
     * absorbs that variance into the next wait without lagging the
     * cadence. If we fall more than one frame behind real time
     * (e.g. paused at a breakpoint, OS hitch), snap forward so we
     * don't burn CPU catching up. */
    if (!sFastForward) {
        const u64 frameTimeNs = Port_Config_FrameTimeNs();
        if (frameTimeNs != 0) {
            u64 deadline = lastFrameNs + frameTimeNs;
            u64 now = SDL_GetTicksNS();
            if (now > deadline + frameTimeNs) {
                /* Fell behind the ideal grid by >1 frame — snap forward. */
                deadline = now;
            }
            while (SDL_GetTicksNS() < deadline) {
            }
            lastFrameNs = deadline;
        } else {
            lastFrameNs = SDL_GetTicksNS();
        }
    } else {
        lastFrameNs = SDL_GetTicksNS();
    }

    nowNs = lastFrameNs;

    if (sFpsWindowStartNs == 0) {
        sFpsWindowStartNs = nowNs;
    }

    sFpsFrameCount++;

    if (nowNs - sFpsWindowStartNs >= 1000000000ULL) {
        double elapsedSec = (double)(nowNs - sFpsWindowStartNs) / 1000000000.0;
        double fps = (elapsedSec > 0.0) ? (double)sFpsFrameCount / elapsedSec : 0.0;
        char title[96];

/* TMC_PORT_VERSION is set by xmake.lua's add_defines; the fallback below
 * is just for IDE indexers that don't see the build flags. */
#ifndef TMC_PORT_VERSION
#define TMC_PORT_VERSION "0.5.0"
#endif
        SDL_snprintf(title, sizeof(title), "The Minish Cap " TMC_PORT_VERSION " - %.1f FPS", fps);
        Port_PPU_SetWindowTitle(title);

        sFpsWindowStartNs = nowNs;
        sFpsFrameCount = 0;
    }

    /* The ImGui quit-confirm modal is rendered once per frame from
     * port_imgui_menu.cpp; when the user picks Save & Quit or Quit
     * Without Saving it sets the confirmed flag, which we promote
     * into gQuitRequested here so the loop unwinds normally. */
    {
        extern bool Port_ImGui_QuitConfirmed(void);
        if (Port_ImGui_QuitConfirmed()) gQuitRequested = true;
    }

    if (gQuitRequested) {
        exit(0);
    }

    Port_PumpEvents();
    Port_UpdateInput();

    /* Auto-save (no-op unless enabled in F8 menu). Runs at frame
     * boundaries so capture is always at a consistent post-tick state. */
    {
        extern void Port_QuickSave_AutoTick(void);
        Port_QuickSave_AutoTick();
    }

    /* Discord Rich Presence (no-op unless toggled on in F8 → Display).
     * Self-throttled to 1 update per 15s inside the client, so per-
     * frame calls are cheap. Health is stored as eighths of a heart
     * (24 = 3 hearts), hence the >>3. */
    {
        extern void Port_DiscordRpc_Update(const char* area_name,
                                           int hearts_now, int hearts_max,
                                           int rupees);
        extern const char* Port_DebugQuery_AreaName(unsigned char area);
        const char* areaName = Port_DebugQuery_AreaName(gRoomControls.area);
        Port_DiscordRpc_Update(
            areaName,
            (int)(gSave.stats.health >> 3), (int)(gSave.stats.maxHealth >> 3),
            (int)gSave.stats.rupees);
    }

    VBlankIntr();
}

/* ---- BIOS functions ---- */

/* Bytes addressable from a GBA address to the end of its memory region,
 * mirroring the ranges in gba_TryMemPtr. 0 = region unknown (caller treats
 * as unbounded). Used to clamp decompressor output so a corrupt ROM header
 * can't overrun a fixed host buffer (gVram/gEwram/gIwram). */
static size_t Port_GbaRegionBytesLeft(uintptr_t gbaAddr) {
    if (gbaAddr >= 0x02000000u && gbaAddr < 0x02040000u) return 0x02040000u - gbaAddr; /* EWRAM */
    if (gbaAddr >= 0x03000000u && gbaAddr < 0x03008000u) return 0x03008000u - gbaAddr; /* IWRAM */
    if (gbaAddr >= 0x06000000u && gbaAddr < 0x06018000u) return 0x06018000u - gbaAddr; /* VRAM  */
    return 0;
}

/* If `src` lies inside the loaded ROM image, return one-past-the-last ROM
 * byte so a decompressor can't read off the end of a truncated/corrupt ROM.
 * NULL (unbounded) for asset-resolved sources from our own trusted pipeline. */
static const u8* Port_RomBufferEnd(const void* src) {
    if (gRomData && (const u8*)src >= gRomData && (const u8*)src < gRomData + gRomSize)
        return gRomData + gRomSize;
    return NULL;
}

/* LZ77 decompressor (SWI 0x11/0x12) */
static void lz77_decomp(const u8* src, u8* dst, size_t dstCap, const u8* srcEnd) {
    if (srcEnd && src + 4 > srcEnd)
        return;
    u32 header = src[0] | (src[1] << 8) | (src[2] << 16) | (src[3] << 24);
    u32 decompSize = header >> 8;
    src += 4;

    /* Clamp the declared output size to the destination region so a
     * corrupt/crafted header can't overrun the host buffer. For a valid
     * ROM decompSize is always <= the region, so behavior is unchanged. */
    if (dstCap && decompSize > dstCap)
        decompSize = (u32)dstCap;

    u32 written = 0;
    while (written < decompSize) {
        if (srcEnd && src >= srcEnd)
            break;
        u8 flags = *src++;
        for (int i = 7; i >= 0 && written < decompSize; i--) {
            if (flags & (1 << i)) {
                /* Compressed block: 2 bytes → length + distance */
                if (srcEnd && src + 2 > srcEnd)
                    return;
                u8 b1 = *src++;
                u8 b2 = *src++;
                u32 length = ((b1 >> 4) & 0xF) + 3;
                u32 distance = ((b1 & 0xF) << 8) | b2;
                distance += 1;
                /* A back-reference pointing before the output start is
                 * malformed; refuse rather than wild-read host memory. */
                if (distance > written)
                    return;
                for (u32 j = 0; j < length && written < decompSize; j++) {
                    dst[written] = dst[written - distance];
                    written++;
                }
            } else {
                /* Uncompressed byte */
                if (srcEnd && src >= srcEnd)
                    return;
                dst[written++] = *src++;
            }
        }
    }
}

void LZ77UnCompVram(const void* src, void* dst) {
    void* resolved = port_resolve_addr((uintptr_t)dst);
    lz77_decomp((const u8*)src, (u8*)resolved,
                Port_GbaRegionBytesLeft((uintptr_t)dst), Port_RomBufferEnd(src));
}

void LZ77UnCompWram(const void* src, void* dst) {
    void* resolved = port_resolve_addr((uintptr_t)dst);
    lz77_decomp((const u8*)src, (u8*)resolved,
                Port_GbaRegionBytesLeft((uintptr_t)dst), Port_RomBufferEnd(src));
}

/* CpuSet (SWI 0x0B) */
void CpuSet(const void* src, void* dst, u32 cnt) {
    u32 wordCount = cnt & 0x1FFFFF;
    int fill = (cnt >> 24) & 1;
    int is32 = (cnt >> 26) & 1;
    u32 byteCount = is32 ? wordCount * 4 : wordCount * 2;

    void* resolvedDst = port_resolve_addr((uintptr_t)dst);
    const void* resolvedSrc = Port_ResolveCopySrc(src, byteCount);

    if (is32) {
        const u32* s = (const u32*)resolvedSrc;
        u32* d = (u32*)resolvedDst;
        u32 val = *s;
        for (u32 i = 0; i < wordCount; i++) {
            d[i] = fill ? val : s[i];
        }
    } else {
        const u16* s = (const u16*)resolvedSrc;
        u16* d = (u16*)resolvedDst;
        u16 val = *s;
        for (u32 i = 0; i < wordCount; i++) {
            d[i] = fill ? val : s[i];
        }
    }
}

/* CpuFastSet (SWI 0x0C) */
void CpuFastSet(const void* src, void* dst, u32 cnt) {
    u32 wordCount = cnt & 0x1FFFFF; /* low 21 bits = 32-bit word count */
    int fill = (cnt >> 24) & 1;

    void* resolvedDst = port_resolve_addr((uintptr_t)dst);
    const void* resolvedSrc = Port_ResolveCopySrc(src, wordCount * 4);

    const u32* s = (const u32*)resolvedSrc;
    u32* d = (u32*)resolvedDst;

    if (fill) {
        u32 val = *s;
        for (u32 i = 0; i < wordCount; i++)
            d[i] = val;
    } else {
        memcpy(d, s, wordCount * 4);
    }
}

/* RegisterRamReset — stub */
void RegisterRamReset(u32 flags) {
    if (flags & RESET_EWRAM) {
        memset(gEwram, 0, sizeof(gEwram));
    }

    if (flags & RESET_IWRAM) {
        memset(gIwram, 0, sizeof(gIwram));
    }

    if (flags & RESET_PALETTE) {
        memset(gBgPltt, 0, sizeof(gBgPltt));
        memset(gObjPltt, 0, sizeof(gObjPltt));
    }

    if (flags & RESET_VRAM) {
        memset(gVram, 0, sizeof(gVram));
    }

    if (flags & RESET_OAM) {
        memset(gOamMem, 0, sizeof(gOamMem));
    }

    if (flags & RESET_SIO_REGS) {
        // SIO register range (subset in IO space): 0x120-0x12A.
        memset(gIoMem + 0x120, 0, 0x0C);
    }

    if (flags & RESET_SOUND_REGS) {
        // Sound register blocks in IO space.
        memset(gIoMem + 0x060, 0, 0x28);
        memset(gIoMem + 0x090, 0, 0x18);
        Port_Audio_Reset();
    }

    if (flags & RESET_REGS) {
        memset(gIoMem, 0, sizeof(gIoMem));
        // GBA KEYINPUT idle state: all keys released.
        *(vu16*)(gIoMem + REG_OFFSET_KEYINPUT) = 0x03FF;
        Port_Audio_Reset();
    }
}

/* Sqrt (SWI 0x08) */
u16 Sqrt(u32 num) {
    /* Bit-by-bit integer sqrt — O(16) and overflow-free. The old
     * `while (r*r <= num) r++` looped forever near num=0xFFFFFFFF
     * because r*r wraps in u32. Returns floor(sqrt(num)), matching the
     * GBA BIOS Sqrt (SWI 0x08). */
    u32 result = 0;
    u32 bit = 1u << 30; /* highest power of four <= 2^31 */
    while (bit > num)
        bit >>= 2;
    while (bit != 0) {
        if (num >= result + bit) {
            num -= result + bit;
            result = (result >> 1) + bit;
        } else {
            result >>= 1;
        }
        bit >>= 2;
    }
    return (u16)result;
}

/* Div (SWI 0x06) */
s32 Div(s32 num, s32 denom) {
    if (denom == 0)
        return 0;
    /* INT_MIN / -1 overflows the signed result and is UB on x86 (SIGFPE).
     * ARM SDIV yields INT_MIN here, so match that explicitly. */
    if (denom == -1 && num == (s32)0x80000000)
        return (s32)0x80000000;
    return num / denom;
}

/* SoftReset (SWI 0x00) — restart the game to the title, GBA-faithful.
 *
 * The GBA BIOS soft reset re-runs the cartridge from its entry point,
 * preserving EWRAM (so save data survives) and resetting the rest. Here we
 * longjmp back to AgbMain's setjmp at the top of its init, which re-runs
 * RegisterRamReset(RESET_ALL & ~RESET_EWRAM) + the full re-init. This is the
 * end-of-credits path (staffroll.c StaffrollTask_State3 -> DoSoftReset), the
 * Start+Select+A+B reset combo, and the game-over -> title path.
 *
 * It must NOT exit(): besides being the wrong behaviour (closing the app when
 * the player beats the game), process exit runs C++ static destructors over
 * still-joinable worker threads (TTS/audio) -> std::terminate -> SIGABRT. */
void SoftReset(u32 flags) {
    (void)flags;
    if (gPortSoftResetArmed) {
        longjmp(gPortSoftResetJmp, 1);
    }
    /* Reset requested before the game loop armed the jump target (should not
     * happen in practice). Leave without running the crashing teardown. */
    _Exit(0);
}

/* BgAffineSet (SWI 0x0E) */
void BgAffineSet(struct BgAffineSrcData* src, struct BgAffineDstData* dst, s32 count) {
    for (s32 i = 0; i < count; i++) {
        dst[i].pa = src[i].sx;
        dst[i].pb = 0;
        dst[i].pc = 0;
        dst[i].pd = src[i].sy;
        dst[i].dx = src[i].texX - src[i].scrX * src[i].sx;
        dst[i].dy = src[i].texY - src[i].scrY * src[i].sy;
    }
}

/* ObjAffineSet (SWI 0x0F)
 *
 * GBA BIOS computes the *inverse* texture-mapping matrix: hardware applies
 * pa/pb/pc/pd to screen-relative coordinates to produce texture coordinates.
 * For a visible scale of sx, the matrix uses 1/sx — so doubling sx halves
 * the sampled-texture step per screen pixel and the sprite *grows*.
 *
 *   pa =  cos(θ) / sx
 *   pb = -sin(θ) / sy
 *   pc =  sin(θ) / sx
 *   pd =  cos(θ) / sy
 *
 * Inputs sx/sy are 8.8 fixed point (0x100 = 1.0). Output pa/pb/pc/pd are
 * also 8.8 fixed point. Each is written as one s16 at `offset`-byte
 * intervals — for OAM (offset=8), that puts the four values in the
 * affineParam field of 4 consecutive OAM entries.
 */
void ObjAffineSet(struct ObjAffineSrcData* src, void* dst, s32 count, s32 offset) {
    u8* d = (u8*)dst;
    for (s32 i = 0; i < count; i++) {
        s32 sx = src[i].xScale;
        s32 sy = src[i].yScale;
        u16 theta = src[i].rotation;
        double angle;
        double cosA;
        double sinA;
        s16 pa;
        s16 pb;
        s16 pc;
        s16 pd;

        if (sx == 0) sx = 1;
        if (sy == 0) sy = 1;

        /* GBA angle (0-0xFFFF = 0-360°) → radians */
        angle = (double)theta * 3.14159265358979323846 * 2.0 / 65536.0;
        cosA = cos(angle);
        sinA = sin(angle);
        pa = (s16)(sx * cosA);
        pb = (s16)(-sx * sinA);
        pc = (s16)(sy * sinA);
        pd = (s16)(sy * cosA);

        *(s16*)(d + 0 * offset) = pa;
        *(s16*)(d + 1 * offset) = pb;
        *(s16*)(d + 2 * offset) = pc;
        *(s16*)(d + 3 * offset) = pd;

        d += 4 * offset;
    }
}
