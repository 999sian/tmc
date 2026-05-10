#include "gba/io_reg.h"
#include "main.h"
#include "port_audio.h"
#include "port_gba_mem.h"
#include "port_hdma.h"
#include "port_ppu.h"
#include "port_runtime_config.h"
#include "port_softslots.h"
#include "port_types.h"
#include <SDL3/SDL.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static bool gQuitRequested = false;
static bool sFastForward = false;
static int sFrameNum = 0;

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

    {
        extern bool Port_DebugMenu_IsOpen(void);
        /* While either overlay is open, hold all GBA buttons released so
         * the game doesn't observe stray input from key presses we routed
         * to the overlay. The soft-slot configuration overlay piggybacks
         * on this behaviour while it's the active focus. */
        if (Port_DebugMenu_IsOpen() || Port_SoftSlots_ConfigIsOpen()) {
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
}

static void Port_PumpEvents(void) {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        if (e.type == SDL_EVENT_QUIT) {
            gQuitRequested = true;
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
        Port_Config_HandleEvent(&e);
    }
}


static u64 lastFrameNs = 0;
static u64 sFpsWindowStartNs = 0;
static u32 sFpsFrameCount = 0;

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

    Port_PPU_PresentFrame();
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
#define TMC_PORT_VERSION "0.1.2"
#endif
        SDL_snprintf(title, sizeof(title), "The Minish Cap " TMC_PORT_VERSION " - %.1f FPS", fps);
        Port_PPU_SetWindowTitle(title);

        sFpsWindowStartNs = nowNs;
        sFpsFrameCount = 0;
    }

    if (gQuitRequested) {
        exit(0);
    }

    Port_PumpEvents();
    Port_UpdateInput();

    VBlankIntr();
}

/* ---- BIOS functions ---- */

/* LZ77 decompressor (SWI 0x11/0x12) */
static void lz77_decomp(const u8* src, u8* dst) {
    u32 header = src[0] | (src[1] << 8) | (src[2] << 16) | (src[3] << 24);
    u32 decompSize = header >> 8;
    src += 4;

    u32 written = 0;
    while (written < decompSize) {
        u8 flags = *src++;
        for (int i = 7; i >= 0 && written < decompSize; i--) {
            if (flags & (1 << i)) {
                /* Compressed block: 2 bytes → length + distance */
                u8 b1 = *src++;
                u8 b2 = *src++;
                u32 length = ((b1 >> 4) & 0xF) + 3;
                u32 distance = ((b1 & 0xF) << 8) | b2;
                distance += 1;
                for (u32 j = 0; j < length && written < decompSize; j++) {
                    dst[written] = dst[written - distance];
                    written++;
                }
            } else {
                /* Uncompressed byte */
                dst[written++] = *src++;
            }
        }
    }
}

void LZ77UnCompVram(const void* src, void* dst) {
    void* resolved = port_resolve_addr((uintptr_t)dst);
    lz77_decomp((const u8*)src, (u8*)resolved);
}

void LZ77UnCompWram(const void* src, void* dst) {
    void* resolved = port_resolve_addr((uintptr_t)dst);
    lz77_decomp((const u8*)src, (u8*)resolved);
}

/* CpuSet (SWI 0x0B) */
void CpuSet(const void* src, void* dst, u32 cnt) {
    u32 wordCount = cnt & 0x1FFFFF;
    int fill = (cnt >> 24) & 1;
    int is32 = (cnt >> 26) & 1;

    void* resolvedDst = port_resolve_addr((uintptr_t)dst);
    const void* resolvedSrc = port_resolve_addr((uintptr_t)src);

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
    u32 blockCount = cnt & 0x1FFFFF;
    u32 wordCount = blockCount * 8;
    int fill = (cnt >> 24) & 1;

    void* resolvedDst = port_resolve_addr((uintptr_t)dst);
    const void* resolvedSrc = port_resolve_addr((uintptr_t)src);

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
    if (num == 0)
        return 0;
    u32 r = 1;
    while (r * r <= num)
        r++;
    return (u16)(r - 1);
}

/* Div (SWI 0x06) */
s32 Div(s32 num, s32 denom) {
    if (denom == 0)
        return 0;
    return num / denom;
}

/* SoftReset — just exit */
void SoftReset(u32 flags) {
    (void)flags;
    printf("SoftReset called — exiting.\n");
    exit(0);
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
