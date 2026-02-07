#include "gba/io_reg.h"
#include "main.h"
#include "port_gba_mem.h"
#include "port_ppu.h"
#include "port_types.h"
#include <SDL3/SDL.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <windows.h>

static bool gQuitRequested = false;
static int sFrameNum = 0;

extern Main gMain;
extern void VBlankIntr(void);

static void Port_UpdateInput(void) {
    const bool* keys = SDL_GetKeyboardState(NULL);
    u16 keyinput = 0x03FF;

    if (keys[SDL_SCANCODE_A])
        keyinput &= ~A_BUTTON;
    if (keys[SDL_SCANCODE_Z])
        keyinput &= ~B_BUTTON;
    if (keys[SDL_SCANCODE_BACKSPACE])
        keyinput &= ~SELECT_BUTTON;
    if (keys[SDL_SCANCODE_RETURN])
        keyinput &= ~START_BUTTON;
    if (keys[SDL_SCANCODE_RIGHT])
        keyinput &= ~DPAD_RIGHT;
    if (keys[SDL_SCANCODE_LEFT])
        keyinput &= ~DPAD_LEFT;
    if (keys[SDL_SCANCODE_UP])
        keyinput &= ~DPAD_UP;
    if (keys[SDL_SCANCODE_DOWN])
        keyinput &= ~DPAD_DOWN;
    if (keys[SDL_SCANCODE_S])
        keyinput &= ~R_BUTTON;
    if (keys[SDL_SCANCODE_Q])
        keyinput &= ~L_BUTTON;

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
        }
    }
}

void VBlankIntrWait(void) {
    Port_PumpEvents();
    Port_UpdateInput();

    VBlankIntr();

    Port_PPU_PresentFrame();

#ifdef _WIN32
    Sleep(16);
#else
    SDL_Delay(16);
#endif

    if (gQuitRequested) {
        exit(0);
    }
}

/* ======== BIOS function implementations ======== */

/*
 * LZ77UnCompWram / LZ77UnCompVram — GBA BIOS LZ77 decompressor (SWI 0x11/0x12).
 *
 * Header (4 bytes): bits 7-4 = type (1=LZ77), bits 3-0 = reserved,
 *                   bits 31-8 = uncompressed size.
 * Then a stream of flag bytes + data.
 */
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
    /* dst may be a GBA address (e.g. VRAM) — resolve it */
    void* resolved = port_resolve_addr((uintptr_t)dst);
    lz77_decomp((const u8*)src, (u8*)resolved);
}

void LZ77UnCompWram(const void* src, void* dst) {
    void* resolved = port_resolve_addr((uintptr_t)dst);
    lz77_decomp((const u8*)src, (u8*)resolved);
}

/*
 * CpuSet — GBA BIOS SWI 0x0B.
 * Copies or fills memory in 16- or 32-bit units.
 * cnt bits:  0-20 = word count, bit 24 = fixed source (fill mode),
 *            bit 26 = 32-bit mode (else 16-bit).
 */
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

/*
 * CpuFastSet — GBA BIOS SWI 0x0C.
 * Like CpuSet but always 32-bit and transfers in 8-word blocks.
 */
void CpuFastSet(const void* src, void* dst, u32 cnt) {
    u32 wordCount = cnt & 0x1FFFFF;
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

/* RegisterRamReset — no-op on PC */
void RegisterRamReset(u32 flags) {
    (void)flags;
}

/* SoftReset — exit on PC */
void SoftReset(u32 flags) {
    (void)flags;
    printf("SoftReset called — exiting.\n");
    exit(0);
}

/* BgAffineSet — GBA BIOS SWI 0x0E */
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

/* ObjAffineSet — GBA BIOS SWI 0x0F */
void ObjAffineSet(struct ObjAffineSrcData* src, void* dst, s32 count, s32 offset) {
    s16* d = (s16*)dst;
    for (s32 i = 0; i < count; i++) {
        d[0] = src[i].xScale; /* pa */
        d[1] = 0;             /* pb */
        d[2] = 0;             /* pc */
        d[3] = src[i].yScale; /* pd */
        d = (s16*)((u8*)d + offset);
    }
}
