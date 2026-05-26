/*
 * port_prelaunch_logo.cpp — Project Picori prelaunch logo loader.
 *
 * The logo PNG (docs/picori-logo.png) is baked into the binary at
 * build time via xmake's utils.bin2c rule (see add_rules block in
 * xmake.lua next to add_files for this file). The generated header
 * `picori-logo.png.h` contains a raw 0xNN, 0xNN, … byte sequence
 * we drop into an array initialiser; the array sizes itself via
 * sizeof so we don't depend on a NUL terminator.
 *
 * At first prelaunch frame we decode the embedded bytes via libpng
 * (same path port_glslp_parser.cpp uses for libretro LUTs), then
 * upload to a backend-appropriate texture:
 *
 *   * SDL_Renderer path → SDL_Texture (ImTextureID = SDL_Texture*).
 *   * SDL_GPU path      → SDL_GPUTexture (ImTextureID = SDL_GPUTexture*
 *                         — current convention for the ImGui SDL_GPU
 *                         backend as of v1.92, per the header docs).
 */

#include <SDL3/SDL.h>
#include <png.h>
#include <imgui.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <vector>

extern "C" {
extern const unsigned char kEmbeddedPicoriLogoPng[];
const unsigned char kEmbeddedPicoriLogoPng[] = {
#include "picori-logo.png.h"
};
extern const std::size_t kEmbeddedPicoriLogoPngSize;
const std::size_t kEmbeddedPicoriLogoPngSize = sizeof(kEmbeddedPicoriLogoPng);
}  /* extern "C" */

namespace {

struct DecodedImage {
    int                width  = 0;
    int                height = 0;
    std::vector<Uint8> rgba;
};

struct PngMemReader {
    const Uint8* data;
    size_t       remaining;
};
void PngReadFromMemory(png_structp png_ptr, png_bytep target, png_size_t n) {
    PngMemReader* r = static_cast<PngMemReader*>(png_get_io_ptr(png_ptr));
    if (r->remaining < n) { png_error(png_ptr, "short read"); return; }
    std::memcpy(target, r->data, n);
    r->data      += n;
    r->remaining -= n;
}

std::optional<DecodedImage> DecodePng(const Uint8* bytes, size_t len) {
    png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
    if (!png) return std::nullopt;
    png_infop info = png_create_info_struct(png);
    if (!info) {
        png_destroy_read_struct(&png, nullptr, nullptr);
        return std::nullopt;
    }
    if (setjmp(png_jmpbuf(png))) {
        png_destroy_read_struct(&png, &info, nullptr);
        return std::nullopt;
    }

    PngMemReader reader{ bytes, len };
    png_set_read_fn(png, &reader, PngReadFromMemory);
    png_read_info(png, info);

    png_uint_32 w = png_get_image_width(png, info);
    png_uint_32 h = png_get_image_height(png, info);
    int bit_depth  = png_get_bit_depth(png, info);
    int color_type = png_get_color_type(png, info);

    if (bit_depth == 16) png_set_strip_16(png);
    if (color_type == PNG_COLOR_TYPE_PALETTE) png_set_palette_to_rgb(png);
    if (color_type == PNG_COLOR_TYPE_GRAY && bit_depth < 8) png_set_expand_gray_1_2_4_to_8(png);
    if (png_get_valid(png, info, PNG_INFO_tRNS)) png_set_tRNS_to_alpha(png);
    if (color_type == PNG_COLOR_TYPE_RGB ||
        color_type == PNG_COLOR_TYPE_GRAY ||
        color_type == PNG_COLOR_TYPE_PALETTE) {
        png_set_filler(png, 0xFF, PNG_FILLER_AFTER);
    }
    if (color_type == PNG_COLOR_TYPE_GRAY ||
        color_type == PNG_COLOR_TYPE_GRAY_ALPHA) {
        png_set_gray_to_rgb(png);
    }
    png_read_update_info(png, info);

    DecodedImage img;
    img.width  = static_cast<int>(w);
    img.height = static_cast<int>(h);
    img.rgba.resize(static_cast<size_t>(w) * h * 4);
    std::vector<png_bytep> rows(h);
    for (png_uint_32 y = 0; y < h; ++y) {
        rows[y] = img.rgba.data() + static_cast<size_t>(y) * w * 4;
    }
    png_read_image(png, rows.data());
    png_destroy_read_struct(&png, &info, nullptr);
    return img;
}

/* Loaded state — populated on the first Port_PrelaunchLogo_EnsureLoaded
 * call and held until process exit. */
bool            sAttempted    = false;
ImTextureID     sTexId        = 0;
int             sLogoW        = 0;
int             sLogoH        = 0;
SDL_Texture*    sRendererTex  = nullptr;
SDL_GPUTexture* sGpuTex       = nullptr;
SDL_GPUDevice*  sOwnerDevice  = nullptr;

bool UploadToSDLRenderer(SDL_Renderer* renderer, const DecodedImage& img) {
    SDL_Texture* tex = SDL_CreateTexture(renderer,
                                         SDL_PIXELFORMAT_RGBA32,
                                         SDL_TEXTUREACCESS_STATIC,
                                         img.width, img.height);
    if (!tex) {
        std::fprintf(stderr, "[prelaunch-logo] SDL_CreateTexture failed: %s\n", SDL_GetError());
        return false;
    }
    SDL_SetTextureScaleMode(tex, SDL_SCALEMODE_LINEAR);
    if (!SDL_UpdateTexture(tex, nullptr, img.rgba.data(), img.width * 4)) {
        std::fprintf(stderr, "[prelaunch-logo] SDL_UpdateTexture failed: %s\n", SDL_GetError());
        SDL_DestroyTexture(tex);
        return false;
    }
    sRendererTex = tex;
    sTexId       = reinterpret_cast<ImTextureID>(tex);
    return true;
}

bool UploadToSDLGPU(SDL_GPUDevice* dev, const DecodedImage& img) {
    SDL_GPUTextureCreateInfo tci = {};
    tci.type   = SDL_GPU_TEXTURETYPE_2D;
    tci.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    tci.usage  = SDL_GPU_TEXTUREUSAGE_SAMPLER;
    tci.width  = static_cast<Uint32>(img.width);
    tci.height = static_cast<Uint32>(img.height);
    tci.layer_count_or_depth = 1;
    tci.num_levels           = 1;
    SDL_GPUTexture* tex = SDL_CreateGPUTexture(dev, &tci);
    if (!tex) {
        std::fprintf(stderr, "[prelaunch-logo] SDL_CreateGPUTexture failed: %s\n", SDL_GetError());
        return false;
    }

    SDL_GPUTransferBufferCreateInfo tbci = {};
    tbci.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    tbci.size  = static_cast<Uint32>(img.width * img.height * 4);
    SDL_GPUTransferBuffer* tb = SDL_CreateGPUTransferBuffer(dev, &tbci);
    if (!tb) {
        std::fprintf(stderr, "[prelaunch-logo] SDL_CreateGPUTransferBuffer failed: %s\n", SDL_GetError());
        SDL_ReleaseGPUTexture(dev, tex);
        return false;
    }
    void* mapped = SDL_MapGPUTransferBuffer(dev, tb, /*cycle=*/false);
    if (!mapped) {
        std::fprintf(stderr, "[prelaunch-logo] SDL_MapGPUTransferBuffer failed: %s\n", SDL_GetError());
        SDL_ReleaseGPUTransferBuffer(dev, tb);
        SDL_ReleaseGPUTexture(dev, tex);
        return false;
    }
    std::memcpy(mapped, img.rgba.data(), img.rgba.size());
    SDL_UnmapGPUTransferBuffer(dev, tb);

    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(dev);
    if (!cmd) {
        SDL_ReleaseGPUTransferBuffer(dev, tb);
        SDL_ReleaseGPUTexture(dev, tex);
        return false;
    }
    SDL_GPUCopyPass* cp = SDL_BeginGPUCopyPass(cmd);
    SDL_GPUTextureTransferInfo src = {};
    src.transfer_buffer = tb;
    src.offset          = 0;
    src.pixels_per_row  = static_cast<Uint32>(img.width);
    src.rows_per_layer  = static_cast<Uint32>(img.height);
    SDL_GPUTextureRegion dst = {};
    dst.texture = tex;
    dst.w = static_cast<Uint32>(img.width);
    dst.h = static_cast<Uint32>(img.height);
    dst.d = 1;
    SDL_UploadToGPUTexture(cp, &src, &dst, /*cycle=*/false);
    SDL_EndGPUCopyPass(cp);
    SDL_SubmitGPUCommandBuffer(cmd);
    SDL_ReleaseGPUTransferBuffer(dev, tb);

    sGpuTex      = tex;
    sOwnerDevice = dev;
    /* ImGui v1.92+ SDL_GPU backend takes SDL_GPUTexture* directly as
     * ImTextureID — see imgui_impl_sdlgpu3.h header docs. */
    sTexId       = reinterpret_cast<ImTextureID>(tex);
    return true;
}

}  // namespace

extern "C" bool Port_PrelaunchLogo_EnsureLoaded(SDL_Renderer*  renderer,
                                                SDL_GPUDevice* gpu_device) {
    if (sAttempted) {
        return sTexId != 0;
    }
    sAttempted = true;

    if (kEmbeddedPicoriLogoPngSize == 0) {
        std::fprintf(stderr, "[prelaunch-logo] embedded PNG empty\n");
        return false;
    }
    auto img = DecodePng(kEmbeddedPicoriLogoPng, kEmbeddedPicoriLogoPngSize);
    if (!img) {
        std::fprintf(stderr, "[prelaunch-logo] embedded PNG decode failed\n");
        return false;
    }

    sLogoW = img->width;
    sLogoH = img->height;

    if (renderer) {
        return UploadToSDLRenderer(renderer, *img);
    }
    if (gpu_device) {
        return UploadToSDLGPU(gpu_device, *img);
    }
    std::fprintf(stderr, "[prelaunch-logo] no backend available\n");
    return false;
}

extern "C" ImTextureID Port_PrelaunchLogo_GetTexId(void) { return sTexId; }
extern "C" int Port_PrelaunchLogo_GetWidth(void)  { return sLogoW; }
extern "C" int Port_PrelaunchLogo_GetHeight(void) { return sLogoH; }

extern "C" void Port_PrelaunchLogo_Shutdown(void) {
    if (sRendererTex) {
        SDL_DestroyTexture(sRendererTex);
        sRendererTex = nullptr;
    }
    if (sGpuTex && sOwnerDevice) {
        SDL_ReleaseGPUTexture(sOwnerDevice, sGpuTex);
        sGpuTex = nullptr;
    }
    sTexId = 0;
    sLogoW = sLogoH = 0;
    sAttempted = false;
}
