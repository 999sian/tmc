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
#include <stdio.h>
#include <string.h>

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
