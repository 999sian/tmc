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

static uint32_t* sBottomUploads[2];
static uint32_t* sTopUpload;
static uint32_t sBottomTick;
static bool sBottomReady;
static bool sBottomTextureReady;
static bool sBottomWorkerPending;
static int sBottomFrontBuffer;
static int sBottomWorkerBuffer;
static uint32_t sBottomWorkerTick;
static SecondScreenSnapshot sBottomWorkerSnapshot;
static uint64_t sBottomWorkerLastTicks;
static bool sGpuPresenterReady;
static bool sInitialized;
static uint32_t sFrameNumber;
static uint64_t sPerfFirstFrameTick;
static uint64_t sPerfLastFrameTick;
static uint64_t sPerfRenderTicks;
static uint64_t sPerfTopTicks;
static uint64_t sPerfBottomTicks;
static uint64_t sPerfTotalTicks;
static uint64_t sPerfSamples;
static uint64_t sPerfBottomSamples;
static uint64_t sPerfRenderMaxTicks;
static uint64_t sPerfBottomMaxTicks;
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

static double TicksToMilliseconds(uint64_t ticks) {
    return (double)ticks * 1000.0 / (double)Platform3DS_TicksPerSecond();
}

void Port_PPU_3DS_WriteQuickDump(void) {
    if (!sInitialized) return;
    char dir[128];
    if (!CreateDumpDirectory(dir, sizeof(dir))) return;

    char topPath[192];
    char bottomPath[192];
    snprintf(topPath, sizeof(topPath), "%s/top-screen.bmp", dir);
    snprintf(bottomPath, sizeof(bottomPath), "%s/bottom-screen.bmp", dir);
    const bool screensOk = Platform3DS_SaveDisplayedScreens(topPath, bottomPath);

    char path[192];
    snprintf(path, sizeof(path), "%s/vram.bin", dir);
    WriteBlob(path, gVram, sizeof(gVram));
    snprintf(path, sizeof(path), "%s/io-registers.bin", dir);
    WriteBlob(path, gIoMem, sizeof(gIoMem));
    snprintf(path, sizeof(path), "%s/palettes.bin", dir);
    WriteBlob(path, gBgPltt, sizeof(gBgPltt));
    snprintf(path, sizeof(path), "%s/oam.bin", dir);
    WriteBlob(path, gOamMem, sizeof(gOamMem));

    snprintf(path, sizeof(path), "%s/info.txt", dir);
    FILE* info = fopen(path, "wb");
    if (info) {
        const double sampleCount = sPerfSamples ? (double)sPerfSamples : 1.0;
        const double bottomSampleCount = sPerfBottomSamples ? (double)sPerfBottomSamples : 1.0;
        const double elapsedSeconds = sPerfLastFrameTick > sPerfFirstFrameTick
                                          ? (double)(sPerfLastFrameTick - sPerfFirstFrameTick) /
                                                (double)Platform3DS_TicksPerSecond()
                                          : 0.0;
        const double measuredFps = elapsedSeconds > 0.0 && sPerfSamples > 1
                                       ? (double)(sPerfSamples - 1u) / elapsedSeconds
                                       : 0.0;
        fprintf(info, "The Minish Cap 3DS v" TMC_PORT_VERSION " quick dump\n");
        fprintf(info, "Frame: %lu\n", (unsigned long)sFrameNumber);
        fprintf(info, "Top screenshot: 400x240 displayed framebuffer BMP\n");
        fprintf(info, "Bottom screenshot: 320x240 displayed framebuffer BMP\n");
        fprintf(info, "Displayed framebuffer capture: %s\n", screensOk ? "OK" : "FAILED");
        fprintf(info, "System: %s\n", Platform3DS_IsNew3DS() ? "New Nintendo 3DS" : "Old Nintendo 3DS");
        fprintf(info, "Core 1 time limit: %u%%\n", Platform3DS_Core1TimeLimit());
        fprintf(info, "Measured cadence: %.2f FPS\n", measuredFps);
        fprintf(info, "PPU render: average %.3f ms, maximum %.3f ms\n",
                TicksToMilliseconds(sPerfRenderTicks) / sampleCount, TicksToMilliseconds(sPerfRenderMaxTicks));
        fprintf(info, "Top presentation CPU work: average %.3f ms\n",
                TicksToMilliseconds(sPerfTopTicks) / sampleCount);
        fprintf(info, "Bottom-screen paint worker: average %.3f ms, maximum %.3f ms\n",
                TicksToMilliseconds(sPerfBottomTicks) / bottomSampleCount,
                TicksToMilliseconds(sPerfBottomMaxTicks));
        fprintf(info, "Main-thread render/presentation CPU work: average %.3f ms\n",
                TicksToMilliseconds(sPerfTotalTicks) / sampleCount);
        fprintf(info, "Bottom-screen refresh: 20 Hz\n");
        fprintf(info, "Trigger: L + R + A\n");
        fclose(info);
    }

    char message[192];
    snprintf(message, sizeof(message), "[tmc3ds] quick dump written to %s\n", dir);
    Platform3DS_Debug(message);
}

void Port_PPU_3DS_RenderBottomWorker(void) {
    const uint64_t startTick = Platform3DS_SystemTick();
    Port_SecondScreen_PaintInto(sBottomUploads[sBottomWorkerBuffer], 320, 240, 512,
                                &sBottomWorkerSnapshot, sBottomWorkerTick);
    sBottomWorkerLastTicks = Platform3DS_SystemTick() - startTick;
}

static void RecordBottomWorkerTiming(void) {
    if (sFrameNumber < 120u) return;
    ++sPerfBottomSamples;
    sPerfBottomTicks += sBottomWorkerLastTicks;
    if (sBottomWorkerLastTicks > sPerfBottomMaxTicks) sPerfBottomMaxTicks = sBottomWorkerLastTicks;
}

void Port_PPU_Init(SDL_Window* window) {
    (void)window;
    VirtuaPPUMode1GbaMemory memory = { gIoMem, gVram, gBgPltt, gObjPltt, gOamMem };
    virtuappu_mode1_bind_gba_memory(&memory);
    virtuappu_registers.frame_width = GBA_W;
    virtuappu_registers.frame_pitch = GBA_W;
    virtuappu_registers.mode = 1;
    Port_SecondScreen_Init();
    sBottomReady = false;
    sBottomTextureReady = false;
    sBottomWorkerPending = false;
    sBottomFrontBuffer = 0;
    sBottomWorkerBuffer = 1;
    sBottomTick = 0;
    sFrameNumber = 0;
    sPerfFirstFrameTick = 0;
    sPerfLastFrameTick = 0;
    sPerfRenderTicks = 0;
    sPerfTopTicks = 0;
    sPerfBottomTicks = 0;
    sPerfTotalTicks = 0;
    sPerfSamples = 0;
    sPerfBottomSamples = 0;
    sPerfRenderMaxTicks = 0;
    sPerfBottomMaxTicks = 0;
    sGpuPresenterReady = PlatformGpu3DS_Init();
    sTopUpload = PlatformGpu3DS_TopBuffer();
    sBottomUploads[0] = PlatformGpu3DS_BottomBuffer(0);
    sBottomUploads[1] = PlatformGpu3DS_BottomBuffer(1);
    sInitialized = sGpuPresenterReady && sTopUpload && sBottomUploads[0] && sBottomUploads[1];
    virtuappu_mode1_set_output_buffer(sInitialized ? sTopUpload : NULL, 256);
}

void Port_PPU_PresentFrame(void) {
    if (!sInitialized) return;
    ++sFrameNumber;
    const uint64_t frameStartTick = Platform3DS_SystemTick();

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
    const uint64_t renderEndTick = Platform3DS_SystemTick();
#ifdef TMC_3DS_DIAGNOSTICS
    const uint64_t renderEnd = Platform3DS_Milliseconds();

    const unsigned diagnosticFrame = sDiagnosticFrames++;
    if (diagnosticFrame == 0) sDiagnosticStartMs = frameStart;
    if (diagnosticFrame < 3 || diagnosticFrame == 60 || diagnosticFrame == 180 || diagnosticFrame == 360) {
        char message[256];
        snprintf(message, sizeof(message),
                 "[tmc3ds] frame=%u dispcnt=%04x io=%02x%02x vram=%02x%02x%02x%02x pal=%04x,%04x out=%08lx,%08lx\n",
                 diagnosticFrame, dispcnt, gIoMem[0], gIoMem[1], gVram[0], gVram[1], gVram[2], gVram[3],
                 gBgPltt[0], gBgPltt[1], (unsigned long)sTopUpload[0],
                 (unsigned long)sTopUpload[1]);
        Platform3DS_Debug(message);
    }
    if (diagnosticFrame == 2) DumpPpuSnapshot("tmc3ds-frame2.ppu1");
    if (diagnosticFrame == 60) DumpPpuSnapshot("tmc3ds-frame60.ppu1");
#endif

    PlatformGpu3DS_BeginTop(sTopUpload);
    const uint64_t topEndTick = Platform3DS_SystemTick();
#ifdef TMC_3DS_DIAGNOSTICS
    const uint64_t topEnd = Platform3DS_Milliseconds();
#endif

    bool bottomChanged = false;
    if (sBottomWorkerPending && Platform3DS_TryFinishBottomWorker()) {
        sBottomFrontBuffer = sBottomWorkerBuffer;
        sBottomWorkerPending = false;
        sBottomReady = true;
        bottomChanged = true;
        RecordBottomWorkerTiming();
    }

    const bool bottomUpdateDue = !sBottomReady || (sFrameNumber % 3u) == 0u;
    if (!sBottomWorkerPending && bottomUpdateDue) {
        sBottomWorkerBuffer = 1 - sBottomFrontBuffer;
        Port_SecondScreenState_Read(&sBottomWorkerSnapshot);
        sBottomWorkerTick = sBottomTick++;
        if (Platform3DS_SubmitBottomWorker()) {
            sBottomWorkerPending = true;
        } else {
            Port_PPU_3DS_RenderBottomWorker();
            sBottomFrontBuffer = sBottomWorkerBuffer;
            sBottomReady = true;
            bottomChanged = true;
            RecordBottomWorkerTiming();
        }
    }
    if (!sBottomTextureReady) {
        bottomChanged = true;
        sBottomTextureReady = true;
    }
    PlatformGpu3DS_EndBottom(sBottomUploads[sBottomFrontBuffer], bottomChanged);
    const uint64_t frameEndTick = Platform3DS_SystemTick();

    if (sFrameNumber >= 120u) {
        const uint64_t renderTicks = renderEndTick - frameStartTick;
        const uint64_t topTicks = topEndTick - renderEndTick;
        if (sPerfSamples == 0) sPerfFirstFrameTick = frameStartTick;
        sPerfLastFrameTick = frameStartTick;
        ++sPerfSamples;
        sPerfRenderTicks += renderTicks;
        sPerfTopTicks += topTicks;
        sPerfTotalTicks += frameEndTick - frameStartTick;
        if (renderTicks > sPerfRenderMaxTicks) sPerfRenderMaxTicks = renderTicks;
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
    Platform3DS_ShutdownBottomWorker();
    virtuappu_mode1_set_output_buffer(NULL, 0);
    if (!sGpuPresenterReady) return;
    PlatformGpu3DS_Shutdown();
    sGpuPresenterReady = false;
}
void Port_OpenInGameSettingsModal(void) {}
bool Port_InGameSettingsModalIsOpen(void) { return false; }
