#include "platform_gpu_3ds.h"

#include <3ds.h>
#include <citro2d.h>
#include <string.h>

static C3D_RenderTarget* sTopTarget;
static C3D_RenderTarget* sBottomTarget;
static C3D_Tex sTopTexture;
static C3D_Tex sBottomTexture;
static Tex3DS_SubTexture sTopSubtexture;
static Tex3DS_SubTexture sBottomSubtexture;
static uint32_t* sTopUpload;
static uint32_t* sBottomUpload;
static bool sFrameActive;
static bool sReady;

static u32 TextureTransfer(void) {
    return GX_TRANSFER_FLIP_VERT(0) | GX_TRANSFER_OUT_TILED(1) |
           GX_TRANSFER_RAW_COPY(0) | GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGBA8) |
           GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGBA8) |
           GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO);
}

static void ConfigureArgbTextureEnv(void) {
    C3D_TexEnv* env = C3D_GetTexEnv(0);
    C3D_TexEnvInit(env);
    C3D_TexEnvSrc(env, C3D_RGB, GPU_TEXTURE0, GPU_CONSTANT, GPU_PREVIOUS);
    C3D_TexEnvOpRgb(env, GPU_TEVOP_RGB_SRC_G, GPU_TEVOP_RGB_SRC_COLOR, GPU_TEVOP_RGB_SRC_COLOR);
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
    C3D_TexEnvOpRgb(env, GPU_TEVOP_RGB_SRC_ALPHA, GPU_TEVOP_RGB_SRC_COLOR, GPU_TEVOP_RGB_SRC_COLOR);
    C3D_TexEnvFunc(env, C3D_RGB, GPU_MULTIPLY_ADD);
    C3D_TexEnvColor(env, C2D_Color32(0, 0, 255, 255));
}

bool PlatformGpu3DS_Init(void) {
    sTopUpload = (uint32_t*)linearMemAlign(256u * 256u * sizeof(uint32_t), 0x80);
    sBottomUpload = (uint32_t*)linearMemAlign(512u * 256u * sizeof(uint32_t), 0x80);
    if (!sTopUpload || !sBottomUpload) goto fail_linear;
    memset(sTopUpload, 0, 256u * 256u * sizeof(uint32_t));
    memset(sBottomUpload, 0, 512u * 256u * sizeof(uint32_t));
    if (!C3D_Init(C3D_DEFAULT_CMDBUF_SIZE)) goto fail_linear;
    if (!C2D_Init(32)) {
        C3D_Fini();
        goto fail_linear;
    }
    C2D_Prepare();
    if (!C3D_TexInitVRAM(&sTopTexture, 256, 256, GPU_RGBA8)) goto fail;
    if (!C3D_TexInitVRAM(&sBottomTexture, 512, 256, GPU_RGBA8)) goto fail_top_texture;
    C3D_TexSetFilter(&sTopTexture, GPU_NEAREST, GPU_NEAREST);
    C3D_TexSetFilter(&sBottomTexture, GPU_NEAREST, GPU_NEAREST);
    C3D_TexSetWrap(&sTopTexture, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);
    C3D_TexSetWrap(&sBottomTexture, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);

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
    C3D_TexDelete(&sBottomTexture);
fail_top_texture:
    C3D_TexDelete(&sTopTexture);
fail:
    C2D_Fini();
    C3D_Fini();
fail_linear:
    if (sBottomUpload) linearFree(sBottomUpload);
    if (sTopUpload) linearFree(sTopUpload);
    sBottomUpload = NULL;
    sTopUpload = NULL;
    return false;
}

uint32_t* PlatformGpu3DS_TopBuffer(void) { return sTopUpload; }
uint32_t* PlatformGpu3DS_BottomBuffer(void) { return sBottomUpload; }

void PlatformGpu3DS_FlushTopLines(int first_line, int last_line) {
    if (!sTopUpload || first_line < 0 || last_line <= first_line || last_line > 160) return;
    GSPGPU_FlushDataCache(sTopUpload + first_line * 256,
                          (u32)(last_line - first_line) * 256u * sizeof(uint32_t));
}

void PlatformGpu3DS_BeginTop(const uint32_t* pixels) {
    if (!sReady || !pixels || !C3D_FrameBegin(0)) return;
    sFrameActive = true;
    C3D_SyncDisplayTransfer((u32*)pixels, GX_BUFFER_DIM(256, 256),
                            (u32*)sTopTexture.data, GX_BUFFER_DIM(256, 256), TextureTransfer());
    sTopSubtexture = (Tex3DS_SubTexture){
        .width = 240, .height = 160, .left = 0.0f, .top = 1.0f,
        .right = 240.0f / 256.0f, .bottom = 1.0f - 160.0f / 256.0f,
    };
    const C2D_Image image = { .tex = &sTopTexture, .subtex = &sTopSubtexture };
    const C2D_DrawParams params = {
        .pos = { .x = -60.0f, .y = 80.0f, .w = 360.0f, .h = 240.0f },
        .center = { 0.0f, 0.0f }, .depth = 0.0f, .angle = 0.0f,
    };
    C2D_TargetClear(sTopTarget, C2D_Color32(0, 0, 0, 255));
    C2D_SceneBegin(sTopTarget);
    C2D_DrawImage(image, &params, NULL);
    ConfigureArgbTextureEnv();
}

void PlatformGpu3DS_EndBottom(const uint32_t* pixels, bool changed) {
    if (!sFrameActive || !pixels) return;
    if (changed) {
        GSPGPU_FlushDataCache(pixels, 512u * 256u * sizeof(uint32_t));
        C3D_SyncDisplayTransfer((u32*)pixels, GX_BUFFER_DIM(512, 256),
                                (u32*)sBottomTexture.data, GX_BUFFER_DIM(512, 256), TextureTransfer());
    }
    sBottomSubtexture = (Tex3DS_SubTexture){
        .width = 320, .height = 240, .left = 0.0f, .top = 1.0f,
        .right = 320.0f / 512.0f, .bottom = 1.0f - 240.0f / 256.0f,
    };
    const C2D_Image image = { .tex = &sBottomTexture, .subtex = &sBottomSubtexture };
    const C2D_DrawParams params = {
        .pos = { .x = -40.0f, .y = 40.0f, .w = 320.0f, .h = 240.0f },
        .center = { 0.0f, 0.0f }, .depth = 0.0f, .angle = 0.0f,
    };
    C2D_TargetClear(sBottomTarget, C2D_Color32(0, 0, 0, 255));
    C2D_SceneBegin(sBottomTarget);
    C2D_DrawImage(image, &params, NULL);
    ConfigureArgbTextureEnv();
    C3D_FrameEnd(0);
    sFrameActive = false;
}

void PlatformGpu3DS_Shutdown(void) {
    if (!sReady) return;
    if (sFrameActive) C3D_FrameEnd(0);
    C3D_FrameSync();
    C3D_RenderTargetDelete(sBottomTarget);
    C3D_RenderTargetDelete(sTopTarget);
    C3D_TexDelete(&sBottomTexture);
    C3D_TexDelete(&sTopTexture);
    C2D_Fini();
    C3D_Fini();
    linearFree(sBottomUpload);
    linearFree(sTopUpload);
    sBottomUpload = NULL;
    sTopUpload = NULL;
    sFrameActive = false;
    sReady = false;
}
