#include "gba/io_reg.h"
#include "main.h"
#include "port_audio.h"
#include "port_gba_mem.h"
#include "port_hdma.h"
#include "port_ppu.h"
#include "port_runtime_config.h"
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

    for (size_t i = 0; i < sizeof(sInputMap) / sizeof(sInputMap[0]); i++) {
        if (Port_Config_InputPressed(sInputMap[i].input)) {
            keyinput &= ~sInputMap[i].gbaMask;
        }
    }

    *(vu16*)(gIoMem + REG_OFFSET_KEYINPUT) = keyinput;

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
            bool altHeld = (e.key.mod & SDL_KMOD_ALT) != 0;
            if (e.key.key == SDLK_F11 || (e.key.key == SDLK_RETURN && altHeld)) {
                Port_PPU_ToggleFullscreen();
                continue;
            }
            if (e.key.key == SDLK_F12) {
                Port_PPU_ToggleSmoothing();
                continue;
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
        if (e.type == SDL_EVENT_GAMEPAD_AXIS_MOTION &&
            e.gaxis.axis == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) {
            sFastForward = e.gaxis.value > 16384;
            continue;
        }
        Port_Config_HandleEvent(&e);
    }
}


static u64 lastFrameNs = 0;
static u64 sFpsWindowStartNs = 0;
static u32 sFpsFrameCount = 0;

void VBlankIntrWait(void) {
    u64 nowNs;

    Port_PPU_PresentFrame();
    port_hdma_vblank_reset();

    if (!sFastForward) {
        while (SDL_GetTicksNS() - lastFrameNs < Port_Config_FrameTimeNs()) {
        }
    }

    nowNs = SDL_GetTicksNS();
    lastFrameNs = nowNs;

    if (sFpsWindowStartNs == 0) {
        sFpsWindowStartNs = nowNs;
    }

    sFpsFrameCount++;

    if (nowNs - sFpsWindowStartNs >= 1000000000ULL) {
        double elapsedSec = (double)(nowNs - sFpsWindowStartNs) / 1000000000.0;
        double fps = (elapsedSec > 0.0) ? (double)sFpsFrameCount / elapsedSec : 0.0;
        char title[64];

        SDL_snprintf(title, sizeof(title), "The Minish Cap - %.1f FPS", fps);
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
