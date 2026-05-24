/* port/vk_rt_experiment/FrameSource.cpp
 *
 * libpng-backed PNG decoder + advance() ring. Decoded frames are
 * stored as flat uint8_t[w*h*4] RGBA buffers; alpha is 255 for the
 * RGB-source screenshots we expect (the GBA framebuffer is opaque).
 */

#include "FrameSource.h"

#include <png.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>

namespace tmc_vkrt {

bool FrameSource::loadDirectory(const std::string& dir) {
    std::error_code ec;
    if (!std::filesystem::is_directory(dir, ec)) {
        std::fprintf(stderr, "[fsrc] '%s' is not a directory\n", dir.c_str());
        return false;
    }

    /* Collect candidate filenames first so we can sort them — PNG
     * load order would otherwise depend on filesystem iteration. */
    std::vector<std::string> paths;
    for (auto& entry : std::filesystem::directory_iterator(dir, ec)) {
        if (!entry.is_regular_file(ec)) continue;
        const auto& p = entry.path();
        if (p.extension() != ".png") continue;
        const std::string name = p.filename().string();
        if (name.rfind("tmc-", 0) != 0) continue;  /* only tmc-*.png */
        paths.push_back(p.string());
    }
    /* Numeric-aware sort so tmc-2.png comes before tmc-10.png. */
    std::sort(paths.begin(), paths.end(),
              [](const std::string& a, const std::string& b) {
                  /* Compare by length first (shorter = smaller number
                   * when the prefix is identical), then lexicographic
                   * within same length. */
                  if (a.size() != b.size()) return a.size() < b.size();
                  return a < b;
              });

    mFrames.clear();
    for (const std::string& path : paths) {
        auto rgba = decodePng(path);
        if (!rgba.empty()) {
            mFrames.push_back(std::move(rgba));
        }
    }
    std::fprintf(stderr, "[fsrc] loaded %zu/%zu PNG frame(s) from %s\n",
                 mFrames.size(), paths.size(), dir.c_str());
    return !mFrames.empty();
}

std::vector<uint8_t> FrameSource::decodePng(const std::string& path) {
    std::FILE* fp = std::fopen(path.c_str(), "rb");
    if (!fp) return {};

    /* libpng boilerplate. The struct + io setup is identical across
     * every project; comments stay light. */
    png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
    if (!png) { std::fclose(fp); return {}; }
    png_infop info = png_create_info_struct(png);
    if (!info) {
        png_destroy_read_struct(&png, nullptr, nullptr);
        std::fclose(fp);
        return {};
    }
    if (setjmp(png_jmpbuf(png))) {
        png_destroy_read_struct(&png, &info, nullptr);
        std::fclose(fp);
        return {};
    }
    png_init_io(png, fp);
    png_read_info(png, info);

    png_uint_32 w = png_get_image_width(png, info);
    png_uint_32 h = png_get_image_height(png, info);
    png_byte    colorType = png_get_color_type(png, info);
    png_byte    bitDepth  = png_get_bit_depth(png, info);

    /* Normalise to 8-bit RGBA via libpng's transform pipeline. The
     * screenshots are 8-bit RGB; tRNS / palette / 16-bit paths get
     * handled too in case anything's weird. */
    if (bitDepth == 16) png_set_strip_16(png);
    if (colorType == PNG_COLOR_TYPE_PALETTE) png_set_palette_to_rgb(png);
    if (colorType == PNG_COLOR_TYPE_GRAY && bitDepth < 8) png_set_expand_gray_1_2_4_to_8(png);
    if (png_get_valid(png, info, PNG_INFO_tRNS)) png_set_tRNS_to_alpha(png);
    if (colorType == PNG_COLOR_TYPE_RGB || colorType == PNG_COLOR_TYPE_GRAY ||
        colorType == PNG_COLOR_TYPE_PALETTE) {
        png_set_filler(png, 0xFF, PNG_FILLER_AFTER);
    }
    if (colorType == PNG_COLOR_TYPE_GRAY || colorType == PNG_COLOR_TYPE_GRAY_ALPHA) {
        png_set_gray_to_rgb(png);
    }
    png_read_update_info(png, info);

    if ((int)w != kFrameW || (int)h != kFrameH) {
        std::fprintf(stderr,
            "[fsrc] %s is %ux%u, expected %dx%d — skipping\n",
            path.c_str(), w, h, kFrameW, kFrameH);
        png_destroy_read_struct(&png, &info, nullptr);
        std::fclose(fp);
        return {};
    }

    std::vector<uint8_t> out(kFrameBytes);
    /* png_read_image takes one row pointer per row, top-to-bottom. */
    std::vector<png_bytep> rows(kFrameH);
    for (int y = 0; y < kFrameH; ++y) {
        rows[y] = out.data() + (size_t)y * kFrameW * 4;
    }
    png_read_image(png, rows.data());
    png_read_end(png, nullptr);

    png_destroy_read_struct(&png, &info, nullptr);
    std::fclose(fp);
    return out;
}

const uint8_t* FrameSource::advance(int framesPerStep) {
    if (mFrames.empty()) return nullptr;
    if (framesPerStep < 1) framesPerStep = 1;
    if (++mCallsSinceStep >= framesPerStep) {
        mCallsSinceStep = 0;
        mCurrent = (mCurrent + 1) % mFrames.size();
    }
    return mFrames[mCurrent].data();
}

}  /* namespace tmc_vkrt */
