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
static bool sRunning;
static bool sIsNew3DS;
static bool sCore1Available;
static unsigned sCore1TimeLimit;
static bool sBottomWorkerAttempted;
static bool sBottomWorkerRunning;
static bool sBottomWorkerBusy;
static Thread sBottomWorkerThread;
static LightEvent sBottomWorkerStart;
static LightEvent sBottomWorkerDone;
extern void Port_Audio_3DSPump(void);
extern void Port_Audio_Shutdown(void);
extern void Port_SecondScreen_OnTap(int x, int y, int button);

int Platform3DS_Init(void) {
    gfxInit(GSP_RGB565_OES, GSP_RGB565_OES, false);
    gfxSet3D(false);
    romfsInit();
    consoleInit(GFX_BOTTOM, NULL);

    aptSetHomeAllowed(true);
    aptSetSleepAllowed(true);
    APT_CheckNew3DS(&sIsNew3DS);
    if (sIsNew3DS) osSetSpeedupEnable(true);

    static const u32 core1Candidates[] = { 80, 70, 50, 30 };
    for (size_t i = 0; i < sizeof(core1Candidates) / sizeof(core1Candidates[0]); ++i) {
        if (R_FAILED(APT_SetAppCpuTimeLimit(core1Candidates[i]))) continue;
        u32 actual = 0;
        if (R_SUCCEEDED(APT_GetAppCpuTimeLimit(&actual)) && actual > 0) {
            sCore1Available = true;
            sCore1TimeLimit = actual;
            break;
        }
    }

    sRunning = true;
    hidScanInput();
    sHeld = hidKeysHeld();
    sDown = hidKeysDown();
    return 1;
}

void Platform3DS_Shutdown(void) {
    sRunning = false;
    Platform3DS_ShutdownBottomWorker();
    extern void virtuappu_mode1_shutdown_workers(void);
    virtuappu_mode1_shutdown_workers();
    Port_Audio_Shutdown();
    romfsExit();
    gfxExit();
}

bool Platform3DS_IsRunning(void) { return sRunning; }
bool Platform3DS_IsNew3DS(void) { return sIsNew3DS; }
bool Platform3DS_CanUseCore1(void) { return sCore1Available; }
unsigned Platform3DS_Core1TimeLimit(void) { return sCore1TimeLimit; }

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

uint64_t Platform3DS_SystemTick(void) {
    return svcGetSystemTick();
}

uint64_t Platform3DS_TicksPerSecond(void) {
    return SYSCLOCK_ARM11;
}

static void BottomWorkerMain(void* argument) {
    (void)argument;
    for (;;) {
        LightEvent_Wait(&sBottomWorkerStart);
        if (!__atomic_load_n(&sBottomWorkerRunning, __ATOMIC_ACQUIRE)) break;
        __atomic_thread_fence(__ATOMIC_ACQUIRE);
        extern void Port_PPU_3DS_RenderBottomWorker(void);
        Port_PPU_3DS_RenderBottomWorker();
        LightEvent_Signal(&sBottomWorkerDone);
    }
}

static bool EnsureBottomWorker(void) {
    if (sBottomWorkerThread) return true;
    if (sBottomWorkerAttempted) return false;
    sBottomWorkerAttempted = true;

    LightEvent_Init(&sBottomWorkerStart, RESET_ONESHOT);
    LightEvent_Init(&sBottomWorkerDone, RESET_ONESHOT);
    s32 priority = 0x30;
    svcGetThreadPriority(&priority, CUR_THREAD_HANDLE);
    if (priority < 0x3f) ++priority;
    sBottomWorkerRunning = true;
    const int core = sIsNew3DS ? 2 : 0;
    sBottomWorkerThread = threadCreate(BottomWorkerMain, NULL, 64u * 1024u, priority, core, false);
    if (!sBottomWorkerThread) sBottomWorkerRunning = false;
    return sBottomWorkerThread != NULL;
}

bool Platform3DS_SubmitBottomWorker(void) {
    if (sBottomWorkerBusy || !EnsureBottomWorker()) return false;
    __atomic_thread_fence(__ATOMIC_RELEASE);
    sBottomWorkerBusy = true;
    LightEvent_Signal(&sBottomWorkerStart);
    return true;
}

bool Platform3DS_TryFinishBottomWorker(void) {
    if (!sBottomWorkerBusy || !LightEvent_TryWait(&sBottomWorkerDone)) return false;
    __atomic_thread_fence(__ATOMIC_ACQUIRE);
    sBottomWorkerBusy = false;
    return true;
}

void Platform3DS_ShutdownBottomWorker(void) {
    if (!sBottomWorkerThread) return;
    __atomic_store_n(&sBottomWorkerRunning, false, __ATOMIC_RELEASE);
    LightEvent_Signal(&sBottomWorkerStart);
    threadJoin(sBottomWorkerThread, 2000000000ULL);
    threadFree(sBottomWorkerThread);
    sBottomWorkerThread = NULL;
    sBottomWorkerRunning = false;
    sBottomWorkerBusy = false;
    sBottomWorkerAttempted = false;
}

void Platform3DS_WaitForVBlank(void) {
    if (!sRunning || !aptMainLoop()) {
        sRunning = false;
        return;
    }

    Port_Audio_3DSPump();
    extern bool Port_PPU_3DS_UsesGpuPresenter(void);
    if (Port_PPU_3DS_UsesGpuPresenter()) {
        gspWaitForEvent(GSPGPU_EVENT_VBlank0, false);
    } else {
        gfxFlushBuffers();
        gfxSwapBuffers();
        gspWaitForVBlank();
    }
    if (sQuickDumpRequested) {
        extern void Port_PPU_3DS_WriteQuickDump(void);
        sQuickDumpRequested = false;
        Port_PPU_3DS_WriteQuickDump();
    }

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

void Platform3DS_Debug(const char* message) {
    if (!message) return;
    svcOutputDebugString(message, strlen(message));
    FILE* file = fopen("tmc3ds.log", "ab");
    if (file) {
        fwrite(message, 1, strlen(message), file);
        fclose(file);
    }
}

static void ReadFramebufferPixel(const uint8_t* pixel, GSPGPU_FramebufferFormat format,
                                 uint8_t* red, uint8_t* green, uint8_t* blue) {
    switch (format) {
        case GSP_RGB565_OES: {
            uint16_t color;
            memcpy(&color, pixel, sizeof(color));
            *red = (uint8_t)(((color >> 11) & 31u) * 255u / 31u);
            *green = (uint8_t)(((color >> 5) & 63u) * 255u / 63u);
            *blue = (uint8_t)((color & 31u) * 255u / 31u);
            break;
        }
        case GSP_BGR8_OES:
            *blue = pixel[0];
            *green = pixel[1];
            *red = pixel[2];
            break;
        case GSP_RGBA8_OES:
            *red = pixel[0];
            *green = pixel[1];
            *blue = pixel[2];
            break;
        case GSP_RGB5_A1_OES: {
            uint16_t color;
            memcpy(&color, pixel, sizeof(color));
            *red = (uint8_t)(((color >> 11) & 31u) * 255u / 31u);
            *green = (uint8_t)(((color >> 6) & 31u) * 255u / 31u);
            *blue = (uint8_t)(((color >> 1) & 31u) * 255u / 31u);
            break;
        }
        case GSP_RGBA4_OES: {
            uint16_t color;
            memcpy(&color, pixel, sizeof(color));
            *red = (uint8_t)(((color >> 12) & 15u) * 17u);
            *green = (uint8_t)(((color >> 8) & 15u) * 17u);
            *blue = (uint8_t)(((color >> 4) & 15u) * 17u);
            break;
        }
        default:
            *red = *green = *blue = 0;
            break;
    }
}

static bool SaveCapturedFramebufferBmp(const char* path, const GSPGPU_CaptureInfoEntry* capture,
                                       int width, int height) {
    if (!path || !capture || !capture->framebuf0_vaddr) return false;
    const GSPGPU_FramebufferFormat format = (GSPGPU_FramebufferFormat)(capture->format & 7u);
    const unsigned bytesPerPixel = gspGetBytesPerPixel(format);
    if (bytesPerPixel < 2 || bytesPerPixel > 4 || capture->framebuf_widthbytesize == 0) return false;

    const uint8_t* framebuffer = (const uint8_t*)capture->framebuf0_vaddr;
    GSPGPU_InvalidateDataCache(framebuffer, capture->framebuf_widthbytesize * (u32)width);

    FILE* file = fopen(path, "wb");
    if (!file) return false;
    const int rowSize = (width * 3 + 3) & ~3;
    const uint32_t fileSize = 54u + (uint32_t)rowSize * (uint32_t)height;
    const uint8_t header[54] = {
        'B', 'M',
        (uint8_t)fileSize, (uint8_t)(fileSize >> 8), (uint8_t)(fileSize >> 16), (uint8_t)(fileSize >> 24),
        0, 0, 0, 0, 54, 0, 0, 0, 40, 0, 0, 0,
        (uint8_t)width, (uint8_t)(width >> 8), (uint8_t)(width >> 16), (uint8_t)(width >> 24),
        (uint8_t)height, (uint8_t)(height >> 8), (uint8_t)(height >> 16), (uint8_t)(height >> 24),
        1, 0, 24, 0,
    };
    bool ok = fwrite(header, 1, sizeof(header), file) == sizeof(header);
    uint8_t row[400 * 3];
    for (int y = height - 1; ok && y >= 0; --y) {
        memset(row, 0, (size_t)rowSize);
        for (int x = 0; x < width; ++x) {
            const uint8_t* pixel = framebuffer + (size_t)x * capture->framebuf_widthbytesize +
                                   (size_t)(height - 1 - y) * bytesPerPixel;
            uint8_t red, green, blue;
            ReadFramebufferPixel(pixel, format, &red, &green, &blue);
            row[x * 3 + 0] = blue;
            row[x * 3 + 1] = green;
            row[x * 3 + 2] = red;
        }
        ok = fwrite(row, 1, (size_t)rowSize, file) == (size_t)rowSize;
    }
    if (fclose(file) != 0) ok = false;
    return ok;
}

bool Platform3DS_SaveDisplayedScreens(const char* topPath, const char* bottomPath) {
    GSPGPU_CaptureInfo capture;
    memset(&capture, 0, sizeof(capture));
    if (R_FAILED(GSPGPU_ImportDisplayCaptureInfo(&capture))) return false;
    const bool topOk = SaveCapturedFramebufferBmp(topPath, &capture.screencapture[GSP_SCREEN_TOP], 400, 240);
    const bool bottomOk =
        SaveCapturedFramebufferBmp(bottomPath, &capture.screencapture[GSP_SCREEN_BOTTOM], 320, 240);
    return topOk && bottomOk;
}
