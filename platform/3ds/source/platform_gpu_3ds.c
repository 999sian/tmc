#include "platform_gpu_3ds.h"

#include <3ds.h>
#include <citro2d.h>
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

static C3D_RenderTarget* sTopTarget;
static C3D_RenderTarget* sBottomTarget;
static C3D_Tex sTopTexture;
static C3D_Tex sBottomTexture;
static C3D_Tex sCrtScanTexture;
static C3D_Tex sCrtMaskTexture;
static Tex3DS_SubTexture sTopSubtexture;
static Tex3DS_SubTexture sBottomSubtexture;
static uint32_t* sTopUpload;
static uint32_t* sBottomUploads[2];
static void* sC2dFlushBase;
static size_t sC2dFlushSize;
static bool sFrameActive;
static bool sReady;
static PlatformGpu3DSStats sStats;
static unsigned sTopPresentWidth = 240;
static bool sCrtTexturesReady;
static int sAppliedTopFilter = -1;

enum {
    TOP_TEXTURE_WIDTH = 512,
    TOP_TEXTURE_HEIGHT = 256,
    CRT_SCAN_WIDTH = 8,
    CRT_SCAN_HEIGHT = 256,
    CRT_MASK_WIDTH = 512,
    CRT_MASK_HEIGHT = 8,
};

extern u32 __ctru_linear_heap;
extern u32 __ctru_linear_heap_size;
extern bool Port_Config_GetShowFps(void);
extern int Port_Config_Get3DSAspectRatio(void);
extern int Port_Config_Get3DSDisplayMode(void);
extern double Port_PPU_3DS_CurrentFps(void);

enum {
    TOP_ASPECT_WIDE = 0,
    TOP_ASPECT_NORMAL,
    TOP_ASPECT_STRETCH,
};

enum {
    TOP_DISPLAY_PIXEL_PERFECT = 0,
    TOP_DISPLAY_SCALED,
    TOP_DISPLAY_BLUR,
    TOP_DISPLAY_CRT,
};

static u32 TextureTransfer(void) {
    return GX_TRANSFER_FLIP_VERT(0) | GX_TRANSFER_OUT_TILED(1) |
           GX_TRANSFER_RAW_COPY(0) | GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGBA8) |
           GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGBA8) |
           GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO);
}

static void ConfigureAbgrTextureEnv(void) {
    C3D_TexEnv* env = C3D_GetTexEnv(0);
    C3D_TexEnvInit(env);
    C3D_TexEnvSrc(env, C3D_RGB, GPU_TEXTURE0, GPU_CONSTANT, GPU_PREVIOUS);
    C3D_TexEnvOpRgb(env, GPU_TEVOP_RGB_SRC_ALPHA, GPU_TEVOP_RGB_SRC_COLOR, GPU_TEVOP_RGB_SRC_COLOR);
    C3D_TexEnvFunc(env, C3D_RGB, GPU_MODULATE);
    C3D_TexEnvSrc(env, C3D_Alpha, GPU_CONSTANT, GPU_CONSTANT, GPU_CONSTANT);
    C3D_TexEnvFunc(env, C3D_Alpha, GPU_REPLACE);
    C3D_TexEnvColor(env, C2D_Color32(255, 0, 0, 255));

    env = C3D_GetTexEnv(1);
    C3D_TexEnvInit(env);
    C3D_TexEnvSrc(env, C3D_RGB, GPU_TEXTURE0, GPU_CONSTANT, GPU_PREVIOUS);
    C3D_TexEnvOpRgb(env, GPU_TEVOP_RGB_SRC_B, GPU_TEVOP_RGB_SRC_COLOR, GPU_TEVOP_RGB_SRC_COLOR);
    C3D_TexEnvFunc(env, C3D_RGB, GPU_MULTIPLY_ADD);
    C3D_TexEnvColor(env, C2D_Color32(0, 255, 0, 255));

    env = C3D_GetTexEnv(2);
    C3D_TexEnvInit(env);
    C3D_TexEnvSrc(env, C3D_RGB, GPU_TEXTURE0, GPU_CONSTANT, GPU_PREVIOUS);
    C3D_TexEnvOpRgb(env, GPU_TEVOP_RGB_SRC_G, GPU_TEVOP_RGB_SRC_COLOR, GPU_TEVOP_RGB_SRC_COLOR);
    C3D_TexEnvFunc(env, C3D_RGB, GPU_MULTIPLY_ADD);
    C3D_TexEnvColor(env, C2D_Color32(0, 0, 255, 255));
}

static void DeleteCrtTextures(void) {
    if (!sCrtTexturesReady) return;
    C3D_TexDelete(&sCrtMaskTexture);
    C3D_TexDelete(&sCrtScanTexture);
    sCrtTexturesReady = false;
}

/* The PICA200 has no programmable fragment stage suitable for a desktop
 * CRT shader. Two tiny physical-pixel textures reproduce the useful parts
 * instead: one scanline column and one RGB-mask row, multiplied over the
 * finished frame in two fixed-function draws. */
static bool InitCrtTextures(void) {
    uint32_t* scan = (uint32_t*)linearMemAlign(CRT_SCAN_WIDTH * CRT_SCAN_HEIGHT * sizeof(uint32_t), 0x80);
    uint32_t* mask = (uint32_t*)linearMemAlign(CRT_MASK_WIDTH * CRT_MASK_HEIGHT * sizeof(uint32_t), 0x80);
    bool scanReady = false;
    if (!scan || !mask) goto fail;
    if (!C3D_TexInitVRAM(&sCrtScanTexture, CRT_SCAN_WIDTH, CRT_SCAN_HEIGHT, GPU_RGBA8)) goto fail;
    scanReady = true;
    if (!C3D_TexInitVRAM(&sCrtMaskTexture, CRT_MASK_WIDTH, CRT_MASK_HEIGHT, GPU_RGBA8)) goto fail;

    const float lineHeight = 240.0f / 160.0f;
    const float scanDepth = 0.28f * (lineHeight - 1.0f);
    for (int y = 0; y < CRT_SCAN_HEIGHT; ++y) {
        float brightness = 1.0f;
        if (y < 240) {
            const float line = (float)y / lineHeight;
            const float phase = line - floorf(line);
            brightness -= scanDepth * (0.5f + 0.5f * cosf(phase * 6.2831853f));
        }
        const uint8_t value = (uint8_t)(brightness * 255.0f + 0.5f);
        const uint32_t color = C2D_Color32(value, value, value, 255);
        for (int x = 0; x < CRT_SCAN_WIDTH; ++x) scan[y * CRT_SCAN_WIDTH + x] = color;
    }

    for (int y = 0; y < CRT_MASK_HEIGHT; ++y) {
        for (int x = 0; x < CRT_MASK_WIDTH; ++x) {
            uint8_t rgb[3] = { 224, 224, 224 };
            rgb[x % 3] = 255;
            mask[y * CRT_MASK_WIDTH + x] = C2D_Color32(rgb[0], rgb[1], rgb[2], 255);
        }
    }

    GSPGPU_FlushDataCache(scan, CRT_SCAN_WIDTH * CRT_SCAN_HEIGHT * sizeof(uint32_t));
    C3D_SyncDisplayTransfer(scan, GX_BUFFER_DIM(CRT_SCAN_WIDTH, CRT_SCAN_HEIGHT),
                            (u32*)sCrtScanTexture.data, GX_BUFFER_DIM(CRT_SCAN_WIDTH, CRT_SCAN_HEIGHT),
                            TextureTransfer());
    GSPGPU_FlushDataCache(mask, CRT_MASK_WIDTH * CRT_MASK_HEIGHT * sizeof(uint32_t));
    C3D_SyncDisplayTransfer(mask, GX_BUFFER_DIM(CRT_MASK_WIDTH, CRT_MASK_HEIGHT),
                            (u32*)sCrtMaskTexture.data, GX_BUFFER_DIM(CRT_MASK_WIDTH, CRT_MASK_HEIGHT),
                            TextureTransfer());

    C3D_TexSetFilter(&sCrtScanTexture, GPU_NEAREST, GPU_NEAREST);
    C3D_TexSetFilter(&sCrtMaskTexture, GPU_NEAREST, GPU_NEAREST);
    C3D_TexSetWrap(&sCrtScanTexture, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);
    C3D_TexSetWrap(&sCrtMaskTexture, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);
    linearFree(mask);
    linearFree(scan);
    sCrtTexturesReady = true;
    return true;

fail:
    if (scanReady) C3D_TexDelete(&sCrtScanTexture);
    if (mask) linearFree(mask);
    if (scan) linearFree(scan);
    return false;
}

bool PlatformGpu3DS_Init(void) {
    memset(&sStats, 0, sizeof(sStats));
    memset(&sCrtScanTexture, 0, sizeof(sCrtScanTexture));
    memset(&sCrtMaskTexture, 0, sizeof(sCrtMaskTexture));
    sCrtTexturesReady = false;
    sAppliedTopFilter = -1;
    sC2dFlushBase = NULL;
    sC2dFlushSize = 0;
    sTopUpload = (uint32_t*)linearMemAlign(TOP_TEXTURE_WIDTH * TOP_TEXTURE_HEIGHT * sizeof(uint32_t), 0x80);
    sBottomUploads[0] = (uint32_t*)linearMemAlign(512u * 256u * sizeof(uint32_t), 0x80);
    sBottomUploads[1] = (uint32_t*)linearMemAlign(512u * 256u * sizeof(uint32_t), 0x80);
    if (!sTopUpload || !sBottomUploads[0] || !sBottomUploads[1]) goto fail_linear;
    memset(sTopUpload, 0, TOP_TEXTURE_WIDTH * TOP_TEXTURE_HEIGHT * sizeof(uint32_t));
    memset(sBottomUploads[0], 0, 512u * 256u * sizeof(uint32_t));
    memset(sBottomUploads[1], 0, 512u * 256u * sizeof(uint32_t));
    GSPGPU_FlushDataCache(sTopUpload, TOP_TEXTURE_WIDTH * TOP_TEXTURE_HEIGHT * sizeof(uint32_t));
    GSPGPU_FlushDataCache(sBottomUploads[0], 512u * 256u * sizeof(uint32_t));
    GSPGPU_FlushDataCache(sBottomUploads[1], 512u * 256u * sizeof(uint32_t));
    if (!C3D_Init(C3D_DEFAULT_CMDBUF_SIZE)) goto fail_linear;
    if (!C2D_Init(32)) {
        C3D_Fini();
        goto fail_linear;
    }
    C2D_Prepare();
    C3D_BufInfo* c2dBuffers = C3D_GetBufInfo();
    if (c2dBuffers && c2dBuffers->bufCount > 0) {
        const u32 heapPhysical = osConvertVirtToPhys((void*)__ctru_linear_heap);
        const u32 vertexPhysical = c2dBuffers->base_paddr + c2dBuffers->buffers[0].offset;
        const uintptr_t heapStart = (uintptr_t)__ctru_linear_heap;
        const uintptr_t heapEnd = heapStart + __ctru_linear_heap_size;
        const uintptr_t vertexAddress = heapStart + (u32)(vertexPhysical - heapPhysical);
        const uintptr_t flushStart = vertexAddress & ~(uintptr_t)0x7Fu;
        uintptr_t flushEnd = flushStart + 64u * 1024u;
        if (flushEnd > heapEnd) flushEnd = heapEnd;
        if (flushStart >= heapStart && flushStart < flushEnd) {
            sC2dFlushBase = (void*)flushStart;
            sC2dFlushSize = flushEnd - flushStart;
        }
    }
    sStats.linearHeapBytes = __ctru_linear_heap_size;
    sStats.c2dFlushBytes = (uint32_t)sC2dFlushSize;
    sStats.c2dFlushAddress = (uintptr_t)sC2dFlushBase;
    sStats.topUploadAddress = (uintptr_t)sTopUpload;
    sStats.bottomUploadAddress[0] = (uintptr_t)sBottomUploads[0];
    sStats.bottomUploadAddress[1] = (uintptr_t)sBottomUploads[1];
    if (!C3D_TexInitVRAM(&sTopTexture, TOP_TEXTURE_WIDTH, TOP_TEXTURE_HEIGHT, GPU_RGBA8)) goto fail;
    if (!C3D_TexInitVRAM(&sBottomTexture, 512, 256, GPU_RGBA8)) goto fail_top_texture;
    C3D_TexSetFilter(&sTopTexture, GPU_NEAREST, GPU_NEAREST);
    C3D_TexSetFilter(&sBottomTexture, GPU_NEAREST, GPU_NEAREST);
    C3D_TexSetWrap(&sTopTexture, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);
    C3D_TexSetWrap(&sBottomTexture, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);
    InitCrtTextures();

    sTopTarget = C3D_RenderTargetCreate(240, 400, GPU_RB_RGBA8, GPU_RB_DEPTH16);
    sBottomTarget = C3D_RenderTargetCreate(240, 320, GPU_RB_RGBA8, GPU_RB_DEPTH16);
    if (!sTopTarget || !sBottomTarget) goto fail_targets;
    const u32 output = GX_TRANSFER_FLIP_VERT(0) | GX_TRANSFER_OUT_TILED(0) |
                       GX_TRANSFER_RAW_COPY(0) | GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGBA8) |
                       GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGB565) |
                       GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO);
    C3D_RenderTargetSetOutput(sTopTarget, GFX_TOP, GFX_LEFT, output);
    C3D_RenderTargetSetOutput(sBottomTarget, GFX_BOTTOM, GFX_LEFT, output);
    sReady = true;
    return true;

fail_targets:
    if (sBottomTarget) C3D_RenderTargetDelete(sBottomTarget);
    if (sTopTarget) C3D_RenderTargetDelete(sTopTarget);
    DeleteCrtTextures();
    C3D_TexDelete(&sBottomTexture);
fail_top_texture:
    C3D_TexDelete(&sTopTexture);
fail:
    C2D_Fini();
    C3D_Fini();
fail_linear:
    if (sBottomUploads[1]) linearFree(sBottomUploads[1]);
    if (sBottomUploads[0]) linearFree(sBottomUploads[0]);
    if (sTopUpload) linearFree(sTopUpload);
    sBottomUploads[0] = NULL;
    sBottomUploads[1] = NULL;
    sTopUpload = NULL;
    return false;
}

uint32_t* PlatformGpu3DS_TopBuffer(void) { return sTopUpload; }
uint32_t* PlatformGpu3DS_BottomBuffer(unsigned index) {
    return index < 2 ? sBottomUploads[index] : NULL;
}

static void SetTopTextureFilter(int mode) {
    const int linear = mode == TOP_DISPLAY_BLUR || mode == TOP_DISPLAY_CRT;
    if (linear == sAppliedTopFilter) return;
    C3D_TexSetFilter(&sTopTexture, linear ? GPU_LINEAR : GPU_NEAREST,
                     linear ? GPU_LINEAR : GPU_NEAREST);
    sAppliedTopFilter = linear;
}

static void DrawCrtOverlay(float x, float y, float w, float h) {
    if (!sCrtTexturesReady) return;

    Tex3DS_SubTexture scanSub = {
        .width = CRT_SCAN_WIDTH, .height = 240, .left = 0.0f, .top = 1.0f,
        .right = 1.0f, .bottom = 1.0f - 240.0f / CRT_SCAN_HEIGHT,
    };
    Tex3DS_SubTexture maskSub = {
        .width = (u16)(w + 0.5f), .height = CRT_MASK_HEIGHT, .left = 0.0f, .top = 1.0f,
        .right = w / CRT_MASK_WIDTH, .bottom = 0.0f,
    };
    const C2D_Image scanImage = { .tex = &sCrtScanTexture, .subtex = &scanSub };
    const C2D_Image maskImage = { .tex = &sCrtMaskTexture, .subtex = &maskSub };
    const C2D_DrawParams params = {
        .pos = { .x = x, .y = y, .w = w, .h = h },
        .center = { 0.0f, 0.0f }, .depth = 0.25f, .angle = 0.0f,
    };

    /* output = overlay RGB * framebuffer RGB */
    C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_DST_COLOR, GPU_ZERO, GPU_ONE, GPU_ZERO);
    C2D_DrawImage(scanImage, &params, NULL);
    C2D_DrawImage(maskImage, &params, NULL);
    ConfigureAbgrTextureEnv();
    C2D_Flush();
    C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA,
                   GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA);
}

typedef struct {
    char ch;
    uint8_t rows[7];
} StatusGlyph;

static const StatusGlyph kStatusGlyphs[] = {
    { '0', { 0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E } },
    { '1', { 0x04, 0x0C, 0x14, 0x04, 0x04, 0x04, 0x1F } },
    { '2', { 0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F } },
    { '3', { 0x1E, 0x01, 0x01, 0x0E, 0x01, 0x01, 0x1E } },
    { '4', { 0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02 } },
    { '5', { 0x1F, 0x10, 0x10, 0x1E, 0x01, 0x01, 0x1E } },
    { '6', { 0x0E, 0x10, 0x10, 0x1E, 0x11, 0x11, 0x0E } },
    { '7', { 0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08 } },
    { '8', { 0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E } },
    { '9', { 0x0E, 0x11, 0x11, 0x0F, 0x01, 0x01, 0x0E } },
    { 'D', { 0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E } },
    { 'F', { 0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10 } },
    { 'G', { 0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0E } },
    { 'I', { 0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x1F } },
    { 'M', { 0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11 } },
    { 'N', { 0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11 } },
    { 'P', { 0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10 } },
    { 'S', { 0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E } },
    { 'U', { 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E } },
};

static const uint8_t* FindStatusGlyph(char ch) {
    for (size_t i = 0; i < sizeof(kStatusGlyphs) / sizeof(kStatusGlyphs[0]); ++i) {
        if (kStatusGlyphs[i].ch == ch) return kStatusGlyphs[i].rows;
    }
    return NULL;
}

static float StatusTextWidth(const char* text, float scale) {
    const size_t length = strlen(text);
    return length == 0 ? 0.0f : ((float)length * 6.0f - 1.0f) * scale;
}

static void DrawStatusText(const char* text, float x, float y, float z, float scale, uint32_t color) {
    for (; *text != '\0'; ++text, x += 6.0f * scale) {
        const uint8_t* rows = FindStatusGlyph(*text);
        if (!rows) continue;
        for (int row = 0; row < 7; ++row) {
            int col = 0;
            while (col < 5) {
                while (col < 5 && (rows[row] & (1u << (4 - col))) == 0) ++col;
                const int start = col;
                while (col < 5 && (rows[row] & (1u << (4 - col))) != 0) ++col;
                if (start < col) {
                    C2D_DrawRectSolid(x + start * scale, y + row * scale, z,
                                      (col - start) * scale, scale, color);
                }
            }
        }
    }
}

static void DrawFpsOverlay(void) {
    if (!Port_Config_GetShowFps()) return;

    unsigned fps = (unsigned)(Port_PPU_3DS_CurrentFps() + 0.5);
    if (fps > 999u) fps = 999u;
    char label[16];
    snprintf(label, sizeof(label), "FPS %u", fps);
    const float width = StatusTextWidth(label, 2.0f);
    C2D_DrawRectSolid(6.0f, 214.0f, 0.7f, width + 8.0f, 20.0f, C2D_Color32(0, 0, 0, 210));
    DrawStatusText(label, 10.0f, 217.0f, 0.8f, 2.0f, C2D_Color32(255, 255, 255, 255));
}

static void DrawTopImage(const uint32_t* pixels, unsigned width) {
    if (width < 240u) width = 240u;
    if (width > 266u) width = 266u;
    sTopPresentWidth = width;

    GSPGPU_FlushDataCache(pixels, TOP_TEXTURE_WIDTH * 160u * sizeof(uint32_t));
    C3D_SyncDisplayTransfer((u32*)pixels, GX_BUFFER_DIM(TOP_TEXTURE_WIDTH, TOP_TEXTURE_HEIGHT),
                            (u32*)sTopTexture.data, GX_BUFFER_DIM(TOP_TEXTURE_WIDTH, TOP_TEXTURE_HEIGHT),
                            TextureTransfer());
    sTopSubtexture = (Tex3DS_SubTexture){
        .width = (u16)width, .height = 160, .left = 0.0f, .top = 1.0f,
        .right = (float)width / TOP_TEXTURE_WIDTH, .bottom = 1.0f - 160.0f / TOP_TEXTURE_HEIGHT,
    };
    const C2D_Image image = { .tex = &sTopTexture, .subtex = &sTopSubtexture };
    const int aspect = Port_Config_Get3DSAspectRatio();
    const int displayMode = Port_Config_Get3DSDisplayMode();
    SetTopTextureFilter(displayMode);

    float drawW;
    float drawH;
    if (displayMode == TOP_DISPLAY_PIXEL_PERFECT) {
        drawW = (float)width;
        drawH = 160.0f;
    } else {
        drawH = 240.0f;
        if (aspect == TOP_ASPECT_STRETCH) {
            drawW = 400.0f;
        } else if (aspect == TOP_ASPECT_WIDE && width >= 266u) {
            drawW = 400.0f;
        } else {
            drawW = 360.0f;
        }
    }
    const float drawX = (400.0f - drawW) * 0.5f;
    const float drawY = (240.0f - drawH) * 0.5f;
    const C2D_DrawParams params = {
        .pos = { .x = drawX, .y = drawY, .w = drawW, .h = drawH },
        .center = { 0.0f, 0.0f }, .depth = 0.0f, .angle = 0.0f,
    };
    C2D_TargetClear(sTopTarget, C2D_Color32(0, 0, 0, 255));
    C2D_SceneBegin(sTopTarget);
    C2D_DrawImage(image, &params, NULL);
    ConfigureAbgrTextureEnv();
    if (displayMode == TOP_DISPLAY_CRT && sCrtTexturesReady) {
        C2D_Flush();
        DrawCrtOverlay(drawX, drawY, drawW, drawH);
    }
    DrawFpsOverlay();
}

void PlatformGpu3DS_BeginTop(const uint32_t* pixels, unsigned width) {
    if (!sReady || !pixels) return;
    if (!C3D_FrameBegin(0)) {
        ++sStats.frameBeginFailures;
        return;
    }
    sFrameActive = true;
    DrawTopImage(pixels, width);
    ++sStats.topTransfers;
}

void PlatformGpu3DS_EndBottom(const uint32_t* pixels, bool changed) {
    if (!sFrameActive || !pixels) return;
    if (changed) {
        GSPGPU_FlushDataCache(pixels, 512u * 240u * sizeof(uint32_t));
        C3D_SyncDisplayTransfer((u32*)pixels, GX_BUFFER_DIM(512, 256),
                                (u32*)sBottomTexture.data, GX_BUFFER_DIM(512, 256), TextureTransfer());
        ++sStats.bottomTransfers;
    }
    sBottomSubtexture = (Tex3DS_SubTexture){
        .width = 320, .height = 240, .left = 0.0f, .top = 1.0f,
        .right = 320.0f / 512.0f, .bottom = 1.0f - 240.0f / 256.0f,
    };
    const C2D_Image image = { .tex = &sBottomTexture, .subtex = &sBottomSubtexture };
    const C2D_DrawParams params = {
        .pos = { .x = 0.0f, .y = 0.0f, .w = 320.0f, .h = 240.0f },
        .center = { 0.0f, 0.0f }, .depth = 0.0f, .angle = 0.0f,
    };
    C2D_TargetClear(sBottomTarget, C2D_Color32(0, 0, 0, 255));
    C2D_SceneBegin(sBottomTarget);
    C2D_DrawImage(image, &params, NULL);
    ConfigureAbgrTextureEnv();
    C2D_Flush();
    if (sC2dFlushBase && sC2dFlushSize) {
        GSPGPU_FlushDataCache(sC2dFlushBase, sC2dFlushSize);
        sStats.boundedFlushBytes += sC2dFlushSize;
    }
    C3D_FrameEnd(GX_CMDLIST_FLUSH);
    ++sStats.frames;
    sStats.drawingTime = C3D_GetDrawingTime();
    sStats.processingTime = C3D_GetProcessingTime();
    sFrameActive = false;
}

void PlatformGpu3DS_ShowDumpingOverlay(void) {
    if (!sReady || !sTopUpload || !sBottomUploads[0] || !C3D_FrameBegin(0)) return;
    sFrameActive = true;
    DrawTopImage(sTopUpload, sTopPresentWidth);
    C2D_DrawRectSolid(122.0f, 12.0f, 0.5f, 156.0f, 34.0f, C2D_Color32(0, 0, 0, 220));
    const char* dumping = "DUMPING";
    const float dumpingWidth = StatusTextWidth(dumping, 3.0f);
    DrawStatusText(dumping, 200.0f - dumpingWidth * 0.5f, 19.0f, 0.6f, 3.0f,
                   C2D_Color32(255, 255, 255, 255));

    sBottomSubtexture = (Tex3DS_SubTexture){
        .width = 320, .height = 240, .left = 0.0f, .top = 1.0f,
        .right = 320.0f / 512.0f, .bottom = 1.0f - 240.0f / 256.0f,
    };
    const C2D_Image bottomImage = { .tex = &sBottomTexture, .subtex = &sBottomSubtexture };
    const C2D_DrawParams bottomParams = {
        .pos = { .x = 0.0f, .y = 0.0f, .w = 320.0f, .h = 240.0f },
        .center = { 0.0f, 0.0f }, .depth = 0.0f, .angle = 0.0f,
    };
    C2D_SceneBegin(sBottomTarget);
    C2D_DrawImage(bottomImage, &bottomParams, NULL);
    ConfigureAbgrTextureEnv();
    C2D_Flush();
    if (sC2dFlushBase && sC2dFlushSize) GSPGPU_FlushDataCache(sC2dFlushBase, sC2dFlushSize);
    C3D_FrameEnd(GX_CMDLIST_FLUSH);
    sFrameActive = false;
    gspWaitForEvent(GSPGPU_EVENT_VBlank0, false);
}

void PlatformGpu3DS_GetStats(PlatformGpu3DSStats* stats) {
    if (stats) *stats = sStats;
}

void PlatformGpu3DS_Shutdown(void) {
    if (!sReady) return;
    if (sFrameActive) {
        C2D_Flush();
        if (sC2dFlushBase && sC2dFlushSize) GSPGPU_FlushDataCache(sC2dFlushBase, sC2dFlushSize);
        C3D_FrameEnd(GX_CMDLIST_FLUSH);
    }
    if (!aptShouldClose()) C3D_FrameSync();
    C3D_RenderTargetDelete(sBottomTarget);
    C3D_RenderTargetDelete(sTopTarget);
    DeleteCrtTextures();
    C3D_TexDelete(&sBottomTexture);
    C3D_TexDelete(&sTopTexture);
    C2D_Fini();
    C3D_Fini();
    linearFree(sBottomUploads[1]);
    linearFree(sBottomUploads[0]);
    linearFree(sTopUpload);
    sBottomUploads[0] = NULL;
    sBottomUploads[1] = NULL;
    sTopUpload = NULL;
    sC2dFlushBase = NULL;
    sC2dFlushSize = 0;
    sAppliedTopFilter = -1;
    sFrameActive = false;
    sReady = false;
}
