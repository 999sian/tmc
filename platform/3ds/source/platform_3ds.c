#include "platform_3ds.h"

#include <3ds.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t sHeld;
static uint32_t sDown;
static bool sQuickDumpRequested;
static bool sQuickDumpComboWasHeld;
extern void Port_Audio_3DSPump(void);
extern void Port_Audio_Shutdown(void);
extern void Port_SecondScreen_OnTap(int x, int y, int button);

int Platform3DS_Init(void) {
    gfxInit(GSP_RGB565_OES, GSP_RGB565_OES, false);
    gfxSet3D(false);
    romfsInit();
    consoleInit(GFX_BOTTOM, NULL);

    osSetSpeedupEnable(true);
    APT_SetAppCpuTimeLimit(89);
    aptSetHomeAllowed(true);
    hidScanInput();
    sHeld = hidKeysHeld();
    sDown = hidKeysDown();
    return 1;
}

void Platform3DS_Shutdown(void) {
    extern void virtuappu_mode1_shutdown_workers(void);
    virtuappu_mode1_shutdown_workers();
    Port_Audio_Shutdown();
    romfsExit();
    gfxExit();
}

void Platform3DS_ShowSplash(void) {
    FILE* file = fopen("romfs:/splash.rgb565", "rb");
    uint16_t* pixels = NULL;
    if (file) {
        pixels = (uint16_t*)malloc(400u * 240u * sizeof(uint16_t));
        if (!pixels || fread(pixels, sizeof(uint16_t), 400u * 240u, file) != 400u * 240u) {
            free(pixels);
            pixels = NULL;
        }
        fclose(file);
    }
    if (!pixels) return;

    uint16_t width = 0;
    uint16_t height = 0;
    uint16_t* top = (uint16_t*)gfxGetFramebuffer(GFX_TOP, GFX_LEFT, &width, &height);
    if (top) {
        for (int y = 0; y < 240; ++y) {
            for (int x = 0; x < 400; ++x) {
                top[(239 - y) + x * 240] = pixels[y * 400 + x];
            }
        }
        gfxFlushBuffers();
        gfxSwapBuffers();
        gspWaitForVBlank();
        svcSleepThread(1200000000LL);
    }
    free(pixels);
}

static uint16_t MapKeysToGba(uint32_t keys) {
    enum {
        GBA_A = 1u << 0,
        GBA_B = 1u << 1,
        GBA_SELECT = 1u << 2,
        GBA_START = 1u << 3,
        GBA_RIGHT = 1u << 4,
        GBA_LEFT = 1u << 5,
        GBA_UP = 1u << 6,
        GBA_DOWN = 1u << 7,
        GBA_R = 1u << 8,
        GBA_L = 1u << 9,
    };
    const bool quickDumpCombo = (keys & (KEY_L | KEY_R | KEY_A)) == (KEY_L | KEY_R | KEY_A);
    uint16_t input = 0x03ff;
    if ((keys & KEY_A) && !quickDumpCombo) input &= ~GBA_A;
    if (keys & KEY_B) input &= ~GBA_B;
    if (keys & KEY_SELECT) input &= ~GBA_SELECT;
    if (keys & KEY_START) input &= ~GBA_START;
    if (keys & (KEY_DRIGHT | KEY_CPAD_RIGHT)) input &= ~GBA_RIGHT;
    if (keys & (KEY_DLEFT | KEY_CPAD_LEFT)) input &= ~GBA_LEFT;
    if (keys & (KEY_DUP | KEY_CPAD_UP)) input &= ~GBA_UP;
    if (keys & (KEY_DDOWN | KEY_CPAD_DOWN)) input &= ~GBA_DOWN;
    if ((keys & KEY_R) && !quickDumpCombo) input &= ~GBA_R;
    if ((keys & KEY_L) && !quickDumpCombo) input &= ~GBA_L;
    return input;
}

uint16_t Platform3DS_ReadKeyInput(void) { return MapKeysToGba(sHeld); }
uint16_t Platform3DS_ReadKeyDownInput(void) { return MapKeysToGba(sDown); }

uint32_t Platform3DS_KeysHeld(void) {
    return sHeld;
}

void Platform3DS_ReadCircle(float* x, float* y) {
    circlePosition position;
    hidCircleRead(&position);
    if (x) *x = position.dx / 156.0f;
    if (y) *y = -position.dy / 156.0f;
}

uint16_t* Platform3DS_GetFramebuffer(int top, uint16_t* width, uint16_t* height) {
    return (uint16_t*)gfxGetFramebuffer(top ? GFX_TOP : GFX_BOTTOM, GFX_LEFT, width, height);
}

uint64_t Platform3DS_Milliseconds(void) {
    return osGetTime();
}

void Platform3DS_WaitForVBlank(void) {
    extern bool Port_PPU_3DS_UsesGpuPresenter(void);
    if (Port_PPU_3DS_UsesGpuPresenter()) {
        gspWaitForEvent(GSPGPU_EVENT_VBlank0, false);
    } else {
        gfxFlushBuffers();
        gfxSwapBuffers();
        gspWaitForVBlank();
    }
    Port_Audio_3DSPump();

    hidScanInput();
    sHeld = hidKeysHeld();
    sDown = hidKeysDown();
    if (sDown & KEY_TOUCH) {
        touchPosition touch;
        hidTouchRead(&touch);
        Port_SecondScreen_OnTap(touch.px, touch.py, 0);
    }
    const bool quickDumpCombo = (sHeld & (KEY_L | KEY_R | KEY_A)) == (KEY_L | KEY_R | KEY_A);
    if (quickDumpCombo && !sQuickDumpComboWasHeld) sQuickDumpRequested = true;
    sQuickDumpComboWasHeld = quickDumpCombo;
}

void Platform3DS_ShowFatal(const char* title, const char* message) {
    consoleInit(GFX_BOTTOM, NULL);
    consoleClear();
    printf("%s\n\n%s\n\nPress START to exit.\n", title ? title : "Error", message ? message : "");
    while (aptMainLoop()) {
        hidScanInput();
        if (hidKeysDown() & KEY_START) break;
        gspWaitForVBlank();
    }
}

bool Platform3DS_TakeQuickDumpRequest(void) {
    const bool requested = sQuickDumpRequested;
    sQuickDumpRequested = false;
    return requested;
}

void Platform3DS_Debug(const char* message) {
    if (!message) return;
    svcOutputDebugString(message, strlen(message));
    FILE* file = fopen("tmc3ds.log", "ab");
    if (file) {
        fwrite(message, 1, strlen(message), file);
        fclose(file);
    }
}
