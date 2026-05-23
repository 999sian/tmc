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
    SDL_GPUShader*           vertex_shader   = nullptr;
    SDL_GPUShader*           fragment_shader = nullptr;
    SDL_GPUGraphicsPipeline* pipeline        = nullptr;
    /* Intermediate texture this pass renders into. nullptr for the
     * last pass (renders directly to the swapchain instead). */
    SDL_GPUTexture*          out_texture     = nullptr;
#endif
    /* Resolved output dimensions in pixels. Computed at Load time
     * given the source framebuffer (240×160) and final viewport
     * (window size). Refreshes if the viewport changes — TODO. */
    int out_w = 0;
    int out_h = 0;
    /* Parameters declared by this pass's source via `#pragma parameter`.
     * Order matches the ShaderParams uniform-block layout the
     * preprocessor emitted (preprocessor walks the pragmas in source
     * order and emits the block fields in the same order). PresentFrame
     * pushes the values in this order; runtime override values come
     * from g_runtime.preset.parameters via ID lookup. */
    std::vector<PortGlslp::ShaderParam> params;
};

struct Runtime {
    bool                       loaded = false;
    PortGlslp::Preset          preset;
    std::vector<PassResources> passes;
#ifdef TMC_GPU_RENDERER
    SDL_GPUDevice*       device         = nullptr;
    SDL_GPUSampler*      sampler_linear = nullptr;
    SDL_GPUSampler*      sampler_nearest = nullptr;
    SDL_GPUTextureFormat swap_format    = SDL_GPU_TEXTUREFORMAT_INVALID;
    /* Stage-5 MVP: assume swapchain ~ 960×640 at startup and don't
     * re-resize intermediate textures when the window changes. A
     * later iteration adds an OnResize hook that re-allocates the
     * viewport-scale passes. */
    Uint32               assumed_vp_w   = 960;
    Uint32               assumed_vp_h   = 640;
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
extern "C" SDL_GPUDevice*       Port_GPU_GetDevice(void);
extern "C" SDL_GPUTextureFormat Port_GPU_GetSwapchainFormat(void);
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
    g_runtime.swap_format = Port_GPU_GetSwapchainFormat();

    /* Two shared samplers — linear and nearest. Per-pass filter_linear
     * picks which one to bind. address_mode honours the preset's
     * wrap_mode (clamp by default). */
    if (!g_runtime.sampler_linear) {
        SDL_GPUSamplerCreateInfo sci = {};
        sci.min_filter     = SDL_GPU_FILTER_LINEAR;
        sci.mag_filter     = SDL_GPU_FILTER_LINEAR;
        sci.mipmap_mode    = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
        sci.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
        sci.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
        sci.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
        g_runtime.sampler_linear = SDL_CreateGPUSampler(g_runtime.device, &sci);
    }
    if (!g_runtime.sampler_nearest) {
        SDL_GPUSamplerCreateInfo sci = {};
        sci.min_filter     = SDL_GPU_FILTER_NEAREST;
        sci.mag_filter     = SDL_GPU_FILTER_NEAREST;
        sci.mipmap_mode    = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
        sci.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
        sci.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
        sci.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
        g_runtime.sampler_nearest = SDL_CreateGPUSampler(g_runtime.device, &sci);
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
        p.params = std::move(pp->parameters);

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
        fci.num_uniform_buffers  = p.params.empty() ? 1 : 2;  /* LibretroUniforms + ShaderParams */
        p.fragment_shader = SDL_CreateGPUShader(g_runtime.device, &fci);

        if (!p.vertex_shader || !p.fragment_shader) {
            std::fprintf(stderr, "[glslp] Load: pass %zu: SDL_CreateGPUShader failed: %s\n",
                         i, SDL_GetError());
            Port_GlslpRuntime_Unload();
            return 0;
        }
#endif

        ResolvePassSize(pdef, kSrcW, kSrcH, kVpW, kVpH, p.out_w, p.out_h);

#ifdef TMC_GPU_RENDERER
        const bool is_last = (i == g_runtime.preset.passes.size() - 1);

        /* Intermediate texture — every pass except the last writes
         * to its own offscreen texture; the last pass renders to
         * the swapchain. Use SDR R8G8B8A8 for now; pdef.float_framebuffer
         * could pick R16G16B16A16_FLOAT in a future iteration. */
        if (!is_last) {
            SDL_GPUTextureCreateInfo tci = {};
            tci.type   = SDL_GPU_TEXTURETYPE_2D;
            tci.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
            tci.usage  = SDL_GPU_TEXTUREUSAGE_SAMPLER | SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
            tci.width  = (Uint32)p.out_w;
            tci.height = (Uint32)p.out_h;
            tci.layer_count_or_depth = 1;
            tci.num_levels           = 1;
            p.out_texture = SDL_CreateGPUTexture(g_runtime.device, &tci);
            if (!p.out_texture) {
                std::fprintf(stderr, "[glslp] Load: pass %zu: intermediate texture create failed: %s\n",
                             i, SDL_GetError());
                Port_GlslpRuntime_Unload();
                return 0;
            }
        }

        /* Pipeline — target format matches what this pass renders to.
         * Last pass renders to swapchain (sSwapFormat); earlier
         * passes to R8G8B8A8 intermediates. */
        SDL_GPUColorTargetDescription ct = {};
        ct.format = is_last ? g_runtime.swap_format : SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
        ct.blend_state.enable_blend = false;
        SDL_GPUGraphicsPipelineTargetInfo ti = {};
        ti.num_color_targets = 1;
        ti.color_target_descriptions = &ct;

        SDL_GPUGraphicsPipelineCreateInfo gpci = {};
        gpci.vertex_shader            = p.vertex_shader;
        gpci.fragment_shader          = p.fragment_shader;
        gpci.primitive_type           = SDL_GPU_PRIMITIVETYPE_TRIANGLESTRIP;
        gpci.rasterizer_state.fill_mode  = SDL_GPU_FILLMODE_FILL;
        gpci.rasterizer_state.cull_mode  = SDL_GPU_CULLMODE_NONE;
        gpci.rasterizer_state.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
        gpci.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;
        gpci.target_info = ti;
        p.pipeline = SDL_CreateGPUGraphicsPipeline(g_runtime.device, &gpci);
        if (!p.pipeline) {
            std::fprintf(stderr, "[glslp] Load: pass %zu: pipeline create failed: %s\n",
                         i, SDL_GetError());
            Port_GlslpRuntime_Unload();
            return 0;
        }
#endif
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
            if (p.pipeline)        { SDL_ReleaseGPUGraphicsPipeline(g_runtime.device, p.pipeline);        p.pipeline = nullptr; }
            if (p.out_texture)     { SDL_ReleaseGPUTexture(g_runtime.device, p.out_texture);              p.out_texture = nullptr; }
            if (p.fragment_shader) { SDL_ReleaseGPUShader(g_runtime.device, p.fragment_shader);           p.fragment_shader = nullptr; }
            if (p.vertex_shader)   { SDL_ReleaseGPUShader(g_runtime.device, p.vertex_shader);             p.vertex_shader = nullptr; }
        }
    }
#endif
    g_runtime.passes.clear();
    g_runtime.preset = PortGlslp::Preset{};
    g_runtime.loaded = false;
}

#ifdef TMC_GPU_RENDERER
/* Per-frame uniform data matching the LibretroUniforms block in
 * the preprocessor's stage headers. std140 layout: each vec2 takes
 * 8 bytes but aligns to 16 in mixed-types blocks. We use the simple
 * "pad to 16-byte aligned" layout to match the block declaration. */
struct LibretroUniformsPacked {
    float OutputSize[4];        /* xy = (w, h), zw = unused (std140 pad) */
    float TextureSize[4];
    float InputSize[4];
    uint32_t FrameCount;
    int32_t  FrameDirection;
    float    _pad_a[2];
    float OriginalSize[4];
    float FinalViewportSize[4];
};

/* Step 5 present — walks every pass in the loaded preset. Source for
 * pass 0 is `src_texture` (the GBA framebuffer); for pass i > 0 it's
 * the previous pass's out_texture. Last pass renders into
 * `swap_texture`. Caller is responsible for the surrounding command-
 * buffer / acquire / submit / ImGui-overlay scaffolding. */
extern "C" bool Port_GlslpRuntime_PresentFrame(SDL_GPUCommandBuffer* cmd,
                                               SDL_GPUTexture*       src_texture,
                                               SDL_GPUTexture*       swap_texture,
                                               int                   swap_w,
                                               int                   swap_h,
                                               int                   src_w,
                                               int                   src_h,
                                               uint32_t              frame_counter) {
    if (!g_runtime.loaded || g_runtime.passes.empty()) return false;
    if (!cmd || !src_texture || !swap_texture) return false;

    SDL_GPUTexture* prev_input = src_texture;
    int             prev_w     = src_w;
    int             prev_h     = src_h;

    for (size_t i = 0; i < g_runtime.passes.size(); ++i) {
        const auto& pdef = g_runtime.preset.passes[i];
        auto&       p    = g_runtime.passes[i];
        const bool  is_last = (i == g_runtime.passes.size() - 1);

        SDL_GPUTexture* target = is_last ? swap_texture : p.out_texture;
        const int       tw     = is_last ? swap_w : p.out_w;
        const int       th     = is_last ? swap_h : p.out_h;

        SDL_GPUColorTargetInfo color = {};
        color.texture     = target;
        color.clear_color = SDL_FColor{0.0f, 0.0f, 0.0f, 1.0f};
        color.load_op     = SDL_GPU_LOADOP_CLEAR;
        color.store_op    = SDL_GPU_STOREOP_STORE;

        SDL_GPURenderPass* rp = SDL_BeginGPURenderPass(cmd, &color, 1, nullptr);
        SDL_GPUViewport vp = {};
        vp.x = 0.0f; vp.y = 0.0f;
        vp.w = (float)tw; vp.h = (float)th;
        vp.min_depth = 0.0f; vp.max_depth = 1.0f;
        SDL_SetGPUViewport(rp, &vp);

        SDL_BindGPUGraphicsPipeline(rp, p.pipeline);

        SDL_GPUTextureSamplerBinding tsb = {};
        tsb.texture = prev_input;
        tsb.sampler = pdef.filter_linear ? g_runtime.sampler_linear
                                          : g_runtime.sampler_nearest;
        SDL_BindGPUFragmentSamplers(rp, 0, &tsb, 1);

        /* LibretroUniforms — same data pushed to both stages, since
         * the preprocessor's headers declare an identical block in
         * vert and frag. */
        LibretroUniformsPacked u = {};
        u.OutputSize[0]        = (float)tw;       u.OutputSize[1]        = (float)th;
        u.TextureSize[0]       = (float)prev_w;   u.TextureSize[1]       = (float)prev_h;
        u.InputSize[0]         = (float)prev_w;   u.InputSize[1]         = (float)prev_h;
        u.FrameCount           = frame_counter;
        u.FrameDirection       = 1;
        u.OriginalSize[0]      = (float)src_w;    u.OriginalSize[1]      = (float)src_h;
        u.FinalViewportSize[0] = (float)swap_w;   u.FinalViewportSize[1] = (float)swap_h;
        SDL_PushGPUVertexUniformData(cmd, 0, &u, sizeof(u));
        SDL_PushGPUFragmentUniformData(cmd, 0, &u, sizeof(u));

        /* ShaderParams — the per-pass uniform block matching the
         * `layout(set = 3, binding = 1) uniform ShaderParams { ... }`
         * the preprocessor emitted. Fields are in #pragma-parameter
         * declaration order (p.params); we look up each by ID in the
         * preset's overrides and fall back to the parameter's
         * compiled-in default.
         *
         * std140 layout: each float in a uniform block gets a vec4
         * slot (16-byte aligned). Pad accordingly. */
        if (!p.params.empty()) {
            const size_t n = p.params.size();
            std::vector<float> padded(n * 4, 0.0f);
            for (size_t k = 0; k < n; ++k) {
                float v = p.params[k].default_value;
                for (const auto& pp : g_runtime.preset.parameters) {
                    if (pp.id == p.params[k].id) { v = pp.current_value; break; }
                }
                padded[k * 4] = v;
            }
            SDL_PushGPUFragmentUniformData(cmd, 1, padded.data(),
                                            (Uint32)(padded.size() * sizeof(float)));
        }

        SDL_DrawGPUPrimitives(rp, 4, 1, 0, 0);
        SDL_EndGPURenderPass(rp);

        if (!is_last) {
            prev_input = p.out_texture;
            prev_w     = p.out_w;
            prev_h     = p.out_h;
        }
    }
    return true;
}
#endif  /* TMC_GPU_RENDERER */

extern "C" int Port_GlslpRuntime_IsActive(void) {
    return g_runtime.loaded ? 1 : 0;
}
