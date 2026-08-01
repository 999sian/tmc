#include "port_ppu.h"

#include "port_gba_mem.h"
#include "port_hdma.h"
#include "port_second_screen.h"
#include "port_second_screen_state.h"
#include "platform_3ds.h"
#include "platform_gpu_3ds.h"

#include "virtuappu.h"
#include "cpu/mode1.h"

#include <stdbool.h>
#include <stdint.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#define GBA_W 240
#define GBA_H 160

static uint32_t sBottom[320 * 240];
static uint32_t* sBottomUpload;
static uint32_t* sTopUpload;
static SecondScreenSnapshot sPreviousBottomSnapshot;
static uint32_t sBottomTick;
static bool sBottomReady;
static bool sGpuPresenterReady;
static bool sInitialized;
static uint32_t sFrameNumber;
#ifdef TMC_3DS_DIAGNOSTICS
static unsigned sDiagnosticFrames;
static uint64_t sDiagnosticStartMs;

static void DumpPpuSnapshot(const char* path) {
    FILE* file = fopen(path, "wb");
    if (!file) return;
    static const char magic[4] = { 'P', 'P', 'U', '1' };
    static const uint32_t sizes[5] = { 0x400u, 0x18000u, 0x200u, 0x200u, 0x400u };
    fwrite(magic, 1, sizeof(magic), file);
    fwrite(sizes, sizeof(uint32_t), 5, file);
    fwrite(gIoMem, 1, 0x400u, file);
    fwrite(gVram, 1, 0x18000u, file);
    fwrite(gBgPltt, 1, 0x200u, file);
    fwrite(gObjPltt, 1, 0x200u, file);
    fwrite(gOamMem, 1, 0x400u, file);
    fclose(file);
}
#endif

static uint16_t AbgrToRgb565(uint32_t p) {
    const uint16_t r = (uint16_t)((p & 0xffu) >> 3);
    const uint16_t g = (uint16_t)(((p >> 8) & 0xffu) >> 2);
    const uint16_t b = (uint16_t)(((p >> 16) & 0xffu) >> 3);
    return (uint16_t)((r << 11) | (g << 5) | b);
}

static uint32_t AbgrToArgb8888(uint32_t p) {
    return 0xff000000u | ((p & 0xffu) << 16) | (p & 0xff00u) | ((p >> 16) & 0xffu);
}

static bool WriteBlob(const char* path, const void* data, size_t size) {
    FILE* file = fopen(path, "wb");
    if (!file) return false;
    const bool ok = fwrite(data, 1, size, file) == size;
    if (fclose(file) != 0) return false;
    return ok;
}

static void MakeTimestamp(char* stamp, size_t stampSize) {
    time_t now = time(NULL);
    struct tm* tmNow = now > 0 ? localtime(&now) : NULL;
    if (tmNow) {
        strftime(stamp, stampSize, "%Y%m%d-%H%M%S", tmNow);
    } else {
        snprintf(stamp, stampSize, "unknown-time");
    }
}

static bool CreateDumpDirectory(char* out, size_t outSize) {
    if (!out || outSize == 0) return false;
    if (mkdir("dumps", 0777) != 0 && errno != EEXIST) return false;

    char stamp[32];
    MakeTimestamp(stamp, sizeof(stamp));
    for (int attempt = 0; attempt < 100; ++attempt) {
        if (attempt == 0) {
            snprintf(out, outSize, "dumps/dump-%s", stamp);
        } else {
            snprintf(out, outSize, "dumps/dump-%s-%02d", stamp, attempt);
        }
        if (mkdir(out, 0777) == 0) return true;
        if (errno != EEXIST) break;
    }
    out[0] = 0;
    return false;
}

static bool SaveArgb8888Bmp(const char* path, const uint32_t* pixels,
                            int pitchPixels, int width, int height) {
    if (!path || !pixels || pitchPixels <= 0 || width <= 0 || height <= 0) return false;
    FILE* file = fopen(path, "wb");
    if (!file) return false;

    const int rowSize = (width * 3 + 3) & ~3;
    const uint32_t fileSize = 54u + (uint32_t)rowSize * (uint32_t)height;
    const uint8_t header[54] = {
        'B', 'M',
        (uint8_t)fileSize, (uint8_t)(fileSize >> 8), (uint8_t)(fileSize >> 16), (uint8_t)(fileSize >> 24),
        0, 0, 0, 0, 54, 0, 0, 0,
        40, 0, 0, 0,
        (uint8_t)width, (uint8_t)(width >> 8), (uint8_t)(width >> 16), (uint8_t)(width >> 24),
        (uint8_t)height, (uint8_t)(height >> 8), (uint8_t)(height >> 16), (uint8_t)(height >> 24),
        1, 0, 24, 0,
    };
    bool ok = fwrite(header, 1, sizeof(header), file) == sizeof(header);
    uint8_t row[400 * 3];
    if (rowSize > (int)sizeof(row)) ok = false;
    for (int y = height - 1; ok && y >= 0; --y) {
        memset(row, 0, (size_t)rowSize);
        const uint32_t* src = pixels + (size_t)y * (size_t)pitchPixels;
        for (int x = 0; x < width; ++x) {
            const uint32_t c = src[x];
            row[x * 3 + 0] = (uint8_t)c;
            row[x * 3 + 1] = (uint8_t)(c >> 8);
            row[x * 3 + 2] = (uint8_t)(c >> 16);
        }
        ok = fwrite(row, 1, (size_t)rowSize, file) == (size_t)rowSize;
    }
    if (fclose(file) != 0) ok = false;
    return ok;
}

static void WriteQuickDump(void) {
    char dir[128];
    if (!CreateDumpDirectory(dir, sizeof(dir))) return;

    char path[192];
    snprintf(path, sizeof(path), "%s/top-screen.bmp", dir);
    SaveArgb8888Bmp(path, sTopUpload, 256, GBA_W, GBA_H);
    snprintf(path, sizeof(path), "%s/bottom-screen.bmp", dir);
    SaveArgb8888Bmp(path, sBottomUpload, 512, 320, 240);
    snprintf(path, sizeof(path), "%s/vram.bin", dir);
    WriteBlob(path, gVram, sizeof(gVram));
    snprintf(path, sizeof(path), "%s/palettes.bin", dir);
    WriteBlob(path, gBgPltt, sizeof(gBgPltt));
    snprintf(path, sizeof(path), "%s/oam.bin", dir);
    WriteBlob(path, gOamMem, sizeof(gOamMem));

    snprintf(path, sizeof(path), "%s/info.txt", dir);
    FILE* info = fopen(path, "wb");
    if (info) {
        fprintf(info, "The Minish Cap 3DS v0.2 quick dump\n");
        fprintf(info, "Frame: %lu\n", (unsigned long)sFrameNumber);
        fprintf(info, "Top screenshot: 240x160 ARGB8888 BMP\n");
        fprintf(info, "Bottom screenshot: 320x240 ARGB8888 BMP\n");
        fprintf(info, "Trigger: L + R + A\n");
        fclose(info);
    }

    char message[192];
    snprintf(message, sizeof(message), "[tmc3ds] quick dump written to %s\n", dir);
    Platform3DS_Debug(message);
}

void Port_PPU_3DS_PresentLine(int line, const uint32_t* pixels) {
    if (!pixels) return;
    for (int sx = 0; sx < GBA_W; ++sx) {
        sTopUpload[line * 256 + sx] = AbgrToArgb8888(pixels[sx]);
    }
}

void Port_PPU_3DS_FlushLines(int first_line, int last_line) {
    PlatformGpu3DS_FlushTopLines(first_line, last_line);
}

void Port_PPU_Init(SDL_Window* window) {
    (void)window;
    VirtuaPPUMode1GbaMemory memory = { gIoMem, gVram, gBgPltt, gObjPltt, gOamMem };
    virtuappu_mode1_bind_gba_memory(&memory);
    virtuappu_registers.frame_width = GBA_W;
    virtuappu_registers.frame_pitch = GBA_W;
    virtuappu_registers.mode = 1;
    Port_SecondScreen_Init();
    memset(sBottom, 0, sizeof(sBottom));
    memset(&sPreviousBottomSnapshot, 0, sizeof(sPreviousBottomSnapshot));
    sBottomReady = false;
    sGpuPresenterReady = PlatformGpu3DS_Init();
    sTopUpload = PlatformGpu3DS_TopBuffer();
    sBottomUpload = PlatformGpu3DS_BottomBuffer();
    sInitialized = sGpuPresenterReady;
}

void Port_PPU_PresentFrame(void) {
    if (!sInitialized) return;
    ++sFrameNumber;

#ifdef TMC_3DS_DIAGNOSTICS
    const uint64_t frameStart = Platform3DS_Milliseconds();
#endif
    const uint16_t dispcnt = (uint16_t)(gIoMem[0] | (gIoMem[1] << 8));
    const uint8_t mode = (uint8_t)(dispcnt & 7);
    virtuappu_registers.mode = (mode == 1 || mode == 2) ? 2 : 1;
    virtuappu_registers.frame_width = GBA_W;
    virtuappu_registers.frame_pitch = GBA_W;
    virtuappu_mode1_pre_line_callback = port_hdma_has_active_channels() ? port_hdma_step_line : NULL;
    virtuappu_mode1_bg2x_hdma_strobe = port_hdma_dest_overlaps(gIoMem + 0x28, gIoMem + 0x2c) != 0;
    virtuappu_mode1_bg2y_hdma_strobe = port_hdma_dest_overlaps(gIoMem + 0x2c, gIoMem + 0x30) != 0;

    virtuappu_render_frame();
#ifdef TMC_3DS_DIAGNOSTICS
    const uint64_t renderEnd = Platform3DS_Milliseconds();

    const unsigned diagnosticFrame = sDiagnosticFrames++;
    if (diagnosticFrame == 0) sDiagnosticStartMs = frameStart;
    if (diagnosticFrame < 3 || diagnosticFrame == 60 || diagnosticFrame == 180 || diagnosticFrame == 360) {
        char message[256];
        snprintf(message, sizeof(message),
                 "[tmc3ds] frame=%u dispcnt=%04x io=%02x%02x vram=%02x%02x%02x%02x pal=%04x,%04x out=%08lx,%08lx\n",
                 diagnosticFrame, dispcnt, gIoMem[0], gIoMem[1], gVram[0], gVram[1], gVram[2], gVram[3],
                 gBgPltt[0], gBgPltt[1], (unsigned long)virtuappu_frame_buffer[0],
                 (unsigned long)virtuappu_frame_buffer[1]);
        Platform3DS_Debug(message);
    }
    if (diagnosticFrame == 2) DumpPpuSnapshot("tmc3ds-frame2.ppu1");
    if (diagnosticFrame == 60) DumpPpuSnapshot("tmc3ds-frame60.ppu1");
#endif

    PlatformGpu3DS_BeginTop(sTopUpload);
#ifdef TMC_3DS_DIAGNOSTICS
    const uint64_t topEnd = Platform3DS_Milliseconds();
#endif

    SecondScreenSnapshot snap;
    Port_SecondScreenState_Read(&snap);
    const bool bottomChanged = !sBottomReady ||
                               memcmp(&snap, &sPreviousBottomSnapshot, sizeof(snap)) != 0;
    if (bottomChanged) {
        Port_SecondScreen_PaintInto(sBottom, 320, 240, 320, &snap, sBottomTick++);
        for (int y = 0; y < 240; ++y) {
            for (int x = 0; x < 320; ++x) {
                sBottomUpload[y * 512 + x] = AbgrToArgb8888(sBottom[y * 320 + x]);
            }
        }
        sPreviousBottomSnapshot = snap;
        sBottomReady = true;
    }
    PlatformGpu3DS_EndBottom(sBottomUpload, bottomChanged);
    if (Platform3DS_TakeQuickDumpRequest()) {
        WriteQuickDump();
    }
#ifdef TMC_3DS_DIAGNOSTICS
    if (diagnosticFrame < 8 || diagnosticFrame == 60) {
        char message[160];
        const uint64_t frameEnd = Platform3DS_Milliseconds();
        snprintf(message, sizeof(message), "[tmc3ds] timing frame=%u render=%llums top=%llums bottom=%llums total=%llums\n",
                 diagnosticFrame, (unsigned long long)(renderEnd - frameStart),
                 (unsigned long long)(topEnd - renderEnd), (unsigned long long)(frameEnd - topEnd),
                 (unsigned long long)(frameEnd - frameStart));
        Platform3DS_Debug(message);
        if (diagnosticFrame == 60) {
            snprintf(message, sizeof(message), "[tmc3ds] cadence 60_frames=%llums\n",
                     (unsigned long long)(frameEnd - sDiagnosticStartMs));
            Platform3DS_Debug(message);
        }
    }
#endif
}

void Port_PPU_SetPresentIsFirstOfTick(bool first) { (void)first; }
void Port_PPU_SetWindowTitle(const char* title) { (void)title; }
void Port_PPU_ToggleFullscreen(void) {}
bool Port_PPU_IsFullscreen(void) { return true; }
void Port_PPU_CycleWindowScale(int direction) { (void)direction; }
unsigned char Port_PPU_WindowScale(void) { return 1; }
void Port_PPU_ApplyWindowScale(void) {}
void Port_PPU_ToggleSmoothing(void) {}
void Port_PPU_CyclePresentationMode(int direction) { (void)direction; }
const char* Port_PPU_PresentationModeName(void) { return "3DS native"; }
void Port_PPU_CycleFilter(int direction) { (void)direction; }
const char* Port_PPU_FilterName(void) { return "Off"; }
void Port_PPU_SetVSync(bool enabled) { (void)enabled; }
bool Port_PPU_VSyncEnabled(void) { return true; }
unsigned Port_PPU_DisplayRefreshRate(void) { return 60; }
void Port_PPU_SetColorCorrection(bool enabled) { (void)enabled; }
bool Port_PPU_ColorCorrectionEnabled(void) { return false; }
void Port_PPU_SetPersistence(bool enabled, float rho) { (void)enabled; (void)rho; }
bool Port_PPU_3DS_UsesGpuPresenter(void) { return sGpuPresenterReady; }
void Port_PPU_Shutdown(void) {
    sInitialized = false;
    if (!sGpuPresenterReady) return;
    PlatformGpu3DS_Shutdown();
    sGpuPresenterReady = false;
}
void Port_OpenInGameSettingsModal(void) {}
bool Port_InGameSettingsModalIsOpen(void) { return false; }
