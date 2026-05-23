/*
 * port_glslp_runtime.cpp — Step 5 of the .glslp runtime: pull
 * everything from Steps 1–4 together into an N-pass render graph
 * usable by port_gpu_renderer's present path.
 *
 * Current state: Load-side complete. Compiles every pass's vertex +
 * fragment SPIR-V (via Step 3's preprocessor + Step 4's glslang
 * wrapper), creates SDL_GPUShader objects, computes per-pass output
 * sizes. Pipeline creation + per-frame Present + integration with
 * port_gpu_renderer's PresentFrame are the next chunks.
 */

#include "port_glslp_parser.h"

#ifdef TMC_GPU_RENDERER
#include <SDL3/SDL.h>
#include <SDL3/SDL_gpu.h>
#endif

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace {

/* Per-pass GPU state: compiled SPIR-V + created shader modules.
 * Pipeline + intermediate texture come in the next chunk. */
struct PassResources {
    std::vector<uint8_t> vertex_spv;
    std::vector<uint8_t> fragment_spv;
#ifdef TMC_GPU_RENDERER
    SDL_GPUShader*       vertex_shader   = nullptr;
    SDL_GPUShader*       fragment_shader = nullptr;
#endif
    /* Resolved output dimensions in pixels. Computed at Load time
     * given the source framebuffer (240×160) and final viewport
     * (window size). Refreshes if the viewport changes — TODO. */
    int out_w = 0;
    int out_h = 0;
    /* Number of #pragma parameter declarations discovered in this
     * pass's source. Determines ShaderParams uniform-block layout. */
    int parameter_count = 0;
};

struct Runtime {
    bool                       loaded = false;
    PortGlslp::Preset          preset;
    std::vector<PassResources> passes;
#ifdef TMC_GPU_RENDERER
    SDL_GPUDevice* device = nullptr;
#endif
};

static Runtime g_runtime;

static bool ReadEntireFile(const std::string& path, std::string& out) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) return false;
    auto end = f.tellg();
    f.seekg(0);
    out.resize((size_t)end);
    if (!f.read(out.data(), out.size())) {
        out.clear();
        return false;
    }
    return true;
}

/* Compute pass i's output (width, height) from its ScaleType/scale
 * factors. For the MVP we treat the source as 240×160 (the GBA fb)
 * and the viewport as the swapchain dimensions. Future stage 5
 * iteration: chain-source-of-previous-pass for scale_type=source. */
static void ResolvePassSize(const PortGlslp::ShaderPass& p,
                            int src_w, int src_h,
                            int vp_w,  int vp_h,
                            int& out_w, int& out_h) {
    using PortGlslp::ScaleType;

    auto resolve_axis = [&](ScaleType st, float sc, int src, int vp) -> int {
        switch (st) {
            case ScaleType::Source:   return (int)((float)src * sc);
            case ScaleType::Viewport: return (int)((float)vp  * sc);
            case ScaleType::Absolute: return (int)sc;
        }
        return src;
    };
    out_w = resolve_axis(p.scale_type_x, p.scale_x, src_w, vp_w);
    out_h = resolve_axis(p.scale_type_y, p.scale_y, src_h, vp_h);
    if (out_w < 1) out_w = 1;
    if (out_h < 1) out_h = 1;
}

}  // namespace

#ifdef TMC_GPU_RENDERER
/* Accessors implemented in port_gpu_renderer.cpp — declared here so
 * we don't pull its full header. */
extern "C" SDL_GPUDevice* Port_GPU_GetDevice(void);
#endif

extern "C" int Port_GlslpRuntime_Load(const char* glslp_path) {
    /* Stage 5 MVP load:
     *   1. Tear down any previous preset state.
     *   2. Parse the .glslp file via LoadPresetFile.
     *   3. For each pass: read shader source, preprocess, compile
     *      vertex + fragment SPIR-V (cached in <preset_dir>/.spv_cache/).
     *   4. Stash SPIR-V buffers in g_runtime.passes; create
     *      SDL_GPUShader objects so the next chunk can build pipelines.
     *   5. Compute pass output sizes against fb 240×160 and an
     *      assumed viewport 960×640 (refresh when window resizes).
     */
    Port_GlslpRuntime_Unload();

    auto preset_opt = PortGlslp::LoadPresetFile(glslp_path);
    if (!preset_opt) {
        std::fprintf(stderr, "[glslp] Load: preset parse failed for %s\n", glslp_path);
        return 0;
    }
    g_runtime.preset = std::move(*preset_opt);

#ifdef TMC_GPU_RENDERER
    g_runtime.device = Port_GPU_GetDevice();
    if (!g_runtime.device) {
        std::fprintf(stderr, "[glslp] Load: SDL_GPU device unavailable; .glslp runtime needs --gpu_renderer=y\n");
        return 0;
    }
#endif

    /* Cache directory next to the preset itself so different presets
     * keep their compiled .spv blobs isolated. */
    namespace fs = std::filesystem;
    fs::path cache_dir = fs::path(glslp_path).parent_path() / ".spv_cache";

    g_runtime.passes.resize(g_runtime.preset.passes.size());

    const int kSrcW = 240, kSrcH = 160;
    const int kVpW = 960, kVpH = 640;  /* assumed; refresh on resize */

    for (size_t i = 0; i < g_runtime.preset.passes.size(); ++i) {
        const auto& pdef = g_runtime.preset.passes[i];
        auto&       p    = g_runtime.passes[i];

        std::string source_text;
        if (!ReadEntireFile(pdef.path, source_text)) {
            std::fprintf(stderr, "[glslp] Load: pass %zu: failed to read %s\n",
                         i, pdef.path.c_str());
            Port_GlslpRuntime_Unload();
            return 0;
        }

        auto pp = PortGlslp::PreprocessLibretroGlsl(source_text);
        if (!pp) {
            std::fprintf(stderr, "[glslp] Load: pass %zu: preprocessor returned nullopt\n", i);
            Port_GlslpRuntime_Unload();
            return 0;
        }
        p.parameter_count = (int)pp->parameters.size();

        auto vspv = PortGlslp::CompileGlslToSpirv(
            pp->vertex_glsl, PortGlslp::ShaderStage::Vertex, cache_dir.string());
        auto fspv = PortGlslp::CompileGlslToSpirv(
            pp->fragment_glsl, PortGlslp::ShaderStage::Fragment, cache_dir.string());
        if (!vspv || !fspv) {
            std::fprintf(stderr, "[glslp] Load: pass %zu: glslang failed\n", i);
            Port_GlslpRuntime_Unload();
            return 0;
        }
        p.vertex_spv   = std::move(*vspv);
        p.fragment_spv = std::move(*fspv);

#ifdef TMC_GPU_RENDERER
        SDL_GPUShaderCreateInfo vci = {};
        vci.code_size            = p.vertex_spv.size();
        vci.code                 = p.vertex_spv.data();
        vci.entrypoint           = "main";
        vci.format               = SDL_GPU_SHADERFORMAT_SPIRV;
        vci.stage                = SDL_GPU_SHADERSTAGE_VERTEX;
        vci.num_samplers         = 0;
        vci.num_uniform_buffers  = 1;  /* LibretroUniforms */
        p.vertex_shader = SDL_CreateGPUShader(g_runtime.device, &vci);

        SDL_GPUShaderCreateInfo fci = {};
        fci.code_size            = p.fragment_spv.size();
        fci.code                 = p.fragment_spv.data();
        fci.entrypoint           = "main";
        fci.format               = SDL_GPU_SHADERFORMAT_SPIRV;
        fci.stage                = SDL_GPU_SHADERSTAGE_FRAGMENT;
        fci.num_samplers         = 1;  /* primary input sampler */
        fci.num_uniform_buffers  = (p.parameter_count > 0) ? 2 : 1;  /* LibretroUniforms + ShaderParams */
        p.fragment_shader = SDL_CreateGPUShader(g_runtime.device, &fci);

        if (!p.vertex_shader || !p.fragment_shader) {
            std::fprintf(stderr, "[glslp] Load: pass %zu: SDL_CreateGPUShader failed: %s\n",
                         i, SDL_GetError());
            Port_GlslpRuntime_Unload();
            return 0;
        }
#endif

        ResolvePassSize(pdef, kSrcW, kSrcH, kVpW, kVpH, p.out_w, p.out_h);
    }

    g_runtime.loaded = true;
    std::fprintf(stderr,
        "[glslp] loaded preset '%s' — %zu passes, %zu LUTs (textures not yet bound), %zu parameters\n",
        glslp_path,
        g_runtime.preset.passes.size(),
        g_runtime.preset.luts.size(),
        g_runtime.preset.parameters.size());
    return 1;
}

extern "C" void Port_GlslpRuntime_Unload(void) {
#ifdef TMC_GPU_RENDERER
    if (g_runtime.device) {
        for (auto& p : g_runtime.passes) {
            if (p.fragment_shader) {
                SDL_ReleaseGPUShader(g_runtime.device, p.fragment_shader);
                p.fragment_shader = nullptr;
            }
            if (p.vertex_shader) {
                SDL_ReleaseGPUShader(g_runtime.device, p.vertex_shader);
                p.vertex_shader = nullptr;
            }
        }
    }
#endif
    g_runtime.passes.clear();
    g_runtime.preset = PortGlslp::Preset{};
    g_runtime.loaded = false;
}

extern "C" int Port_GlslpRuntime_IsActive(void) {
    return g_runtime.loaded ? 1 : 0;
}
