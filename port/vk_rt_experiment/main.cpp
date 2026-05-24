/* port/vk_rt_experiment/main.cpp
 *
 * Driver for the standalone RT scaffold. Boots the Engine, builds
 * a minimal test scene (a procedural diffuse atlas + a handful of
 * quads at different Z layers), and runs the per-frame loop:
 *     beginFrame → rebuildAS → dispatchRays → endFrame
 *
 * This is not a port of TMC — it's a "does the pipeline produce
 * pixels" smoke test for the scaffold. Successful run shows a
 * 4-layer scene with the BG2 (back) blue, BG1 green, BG0 yellow,
 * Sprite red, UI white-bordered. With the closest-hit shader's
 * shadow trace, the foreground quads cast shadows onto the
 * background where the sun ray gets occluded.
 *
 * Exit ESC or close the window.
 */

#include "Engine.h"
#include "RenderLayerManager.h"
#include "RayTracingPipeline.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace tmc_vkrt;

/* Build a 256×256 procedural RGBA atlas with five named cells laid
 * out in a 1×5 row. Each cell is 256×51 (rounded), with a solid
 * colour matching the layer it'll be sampled by:
 *   cell 0: 0.10–0.30 UV-x → white border (UI)
 *   cell 1: 0.30–0.50            → red (sprite)
 *   cell 2: 0.50–0.70            → yellow (BG0)
 *   cell 3: 0.70–0.90            → green (BG1)
 *   cell 4: 0.90–1.00            → blue (BG2)
 *
 * Returns the RGBA bytes (size = w*h*4). */
static std::vector<uint8_t> buildAtlas(uint32_t& outW, uint32_t& outH) {
    const uint32_t W = 256;
    const uint32_t H = 256;
    outW = W; outH = H;
    std::vector<uint8_t> px(W * H * 4u, 0);

    struct Cell { float u0, u1; uint8_t r, g, b; };
    const Cell cells[5] = {
        { 0.10f, 0.30f, 240, 240, 240 },  /* UI    : near-white */
        { 0.30f, 0.50f, 230,  60,  50 },  /* Sprite: red */
        { 0.50f, 0.70f, 235, 200,  60 },  /* BG0   : yellow */
        { 0.70f, 0.90f,  80, 180,  90 },  /* BG1   : green */
        { 0.90f, 1.00f,  60, 110, 200 },  /* BG2   : blue */
    };
    for (const Cell& c : cells) {
        uint32_t x0 = (uint32_t)(c.u0 * W);
        uint32_t x1 = (uint32_t)(c.u1 * W);
        for (uint32_t y = 0; y < H; ++y) {
            for (uint32_t x = x0; x < x1; ++x) {
                uint8_t* p = &px[(y * W + x) * 4u];
                p[0] = c.r; p[1] = c.g; p[2] = c.b; p[3] = 255;
            }
        }
    }
    /* Cell 0 (UI): hollow box — clear inside so emissive=0 quads can
     * sample alpha=0 and discard via the rchit's a<0.01 check. */
    {
        uint32_t x0 = (uint32_t)(cells[0].u0 * W) + 6;
        uint32_t x1 = (uint32_t)(cells[0].u1 * W) - 6;
        for (uint32_t y = 6; y < H - 6; ++y) {
            for (uint32_t x = x0; x < x1; ++x) {
                uint8_t* p = &px[(y * W + x) * 4u];
                p[3] = 0;  /* transparent interior */
            }
        }
    }
    return px;
}

/* Upload `pixels` to a freshly-created sampled-image with a one-
 * shot copy from a staging buffer. Returns the image, view, memory,
 * and sampler — caller frees them on shutdown. */
struct AtlasGpu {
    VkImage        image = VK_NULL_HANDLE;
    VkImageView    view  = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkSampler      sampler = VK_NULL_HANDLE;
};

static AtlasGpu uploadAtlas(Engine& eng, const std::vector<uint8_t>& px,
                            uint32_t w, uint32_t h) {
    AtlasGpu out;

    /* Image */
    VkImageCreateInfo ici{};
    ici.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ici.imageType     = VK_IMAGE_TYPE_2D;
    ici.format        = VK_FORMAT_R8G8B8A8_UNORM;
    ici.extent        = { w, h, 1 };
    ici.mipLevels     = 1;
    ici.arrayLayers   = 1;
    ici.samples       = VK_SAMPLE_COUNT_1_BIT;
    ici.tiling        = VK_IMAGE_TILING_OPTIMAL;
    ici.usage         = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    ici.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
    ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    check(vkCreateImage(eng.device(), &ici, nullptr, &out.image), "atlas vkCreateImage");

    VkMemoryRequirements mr{};
    vkGetImageMemoryRequirements(eng.device(), out.image, &mr);
    VkMemoryAllocateInfo mai{};
    mai.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    mai.allocationSize  = mr.size;
    mai.memoryTypeIndex = eng.findMemoryType(mr.memoryTypeBits,
                                             VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    check(vkAllocateMemory(eng.device(), &mai, nullptr, &out.memory), "atlas vkAllocateMemory");
    check(vkBindImageMemory(eng.device(), out.image, out.memory, 0), "atlas vkBindImageMemory");

    /* Staging buffer */
    VkBuffer stagingBuf = VK_NULL_HANDLE;
    VkDeviceMemory stagingMem = VK_NULL_HANDLE;
    eng.allocateBuffer(px.size(), VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                       VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                       VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                       &stagingBuf, &stagingMem);
    void* p = nullptr;
    check(vkMapMemory(eng.device(), stagingMem, 0, px.size(), 0, &p),
          "atlas staging vkMapMemory");
    std::memcpy(p, px.data(), px.size());
    vkUnmapMemory(eng.device(), stagingMem);

    /* Transition → copy → transition to SHADER_READ_ONLY_OPTIMAL */
    eng.oneShot([&](VkCommandBuffer cmd) {
        VkImageMemoryBarrier b1{};
        b1.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        b1.oldLayout           = VK_IMAGE_LAYOUT_UNDEFINED;
        b1.newLayout           = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        b1.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b1.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b1.image               = out.image;
        b1.subresourceRange    = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        b1.srcAccessMask       = 0;
        b1.dstAccessMask       = VK_ACCESS_TRANSFER_WRITE_BIT;
        vkCmdPipelineBarrier(cmd,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
            0, 0, nullptr, 0, nullptr, 1, &b1);

        VkBufferImageCopy region{};
        region.bufferOffset      = 0;
        region.bufferRowLength   = 0;
        region.bufferImageHeight = 0;
        region.imageSubresource  = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.imageOffset       = {0, 0, 0};
        region.imageExtent       = {w, h, 1};
        vkCmdCopyBufferToImage(cmd, stagingBuf, out.image,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                               1, &region);

        VkImageMemoryBarrier b2 = b1;
        b2.oldLayout     = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        b2.newLayout     = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        b2.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        b2.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(cmd,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,
            0, 0, nullptr, 0, nullptr, 1, &b2);
    });

    vkDestroyBuffer(eng.device(), stagingBuf, nullptr);
    vkFreeMemory(eng.device(), stagingMem, nullptr);

    /* View */
    VkImageViewCreateInfo vci{};
    vci.sType            = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    vci.image            = out.image;
    vci.viewType         = VK_IMAGE_VIEW_TYPE_2D;
    vci.format           = VK_FORMAT_R8G8B8A8_UNORM;
    vci.components       = {VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY,
                            VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY};
    vci.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    check(vkCreateImageView(eng.device(), &vci, nullptr, &out.view), "atlas vkCreateImageView");

    /* Linear sampler with clamp-to-edge (matches the rchit's
     * normalised-UV expectation). */
    VkSamplerCreateInfo sci{};
    sci.sType         = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sci.magFilter     = VK_FILTER_LINEAR;
    sci.minFilter     = VK_FILTER_LINEAR;
    sci.addressModeU  = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sci.addressModeV  = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sci.addressModeW  = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sci.mipmapMode    = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    check(vkCreateSampler(eng.device(), &sci, nullptr, &out.sampler), "atlas vkCreateSampler");

    return out;
}

/* Cell UV ranges in the procedural atlas above. */
struct CellUV { float u0, v0, u1, v1; };
static constexpr CellUV kCellUI     = {0.10f, 0.00f, 0.30f, 1.00f};
static constexpr CellUV kCellSprite = {0.30f, 0.00f, 0.50f, 1.00f};
static constexpr CellUV kCellBG0    = {0.50f, 0.00f, 0.70f, 1.00f};
static constexpr CellUV kCellBG1    = {0.70f, 0.00f, 0.90f, 1.00f};
static constexpr CellUV kCellBG2    = {0.90f, 0.00f, 1.00f, 1.00f};

int main() {
    Engine engine;
    if (!engine.init(960, 640, "tmc-vkrt-experiment")) {
        std::fprintf(stderr, "[main] Engine.init failed\n");
        return 1;
    }
    std::fprintf(stderr, "[main] engine up; swapchain %ux%u\n",
                 engine.swapchainWidth(), engine.swapchainHeight());

    RenderLayerManager layers(engine);
    RayTracingPipeline rt(engine, layers);

    /* Compiled-shader directory: ./shaders relative to the binary's
     * cwd. The build script writes *.spv there. */
    std::fprintf(stderr, "[main] step: createPipeline\n");
    rt.createPipeline("./shaders");
    std::fprintf(stderr, "[main] step: pipeline ok\n");

    /* Procedural atlas. NOTE: build the pixels into a named variable
     * BEFORE the uploadAtlas call — C++ doesn't sequence function
     * arguments, so `uploadAtlas(eng, buildAtlas(aw,ah), aw, ah)`
     * would let `aw`/`ah` be read as 0 before buildAtlas updates
     * them, then uploadAtlas creates a 0×0 image. */
    uint32_t aw = 0, ah = 0;
    std::fprintf(stderr, "[main] step: uploadAtlas\n");
    std::vector<uint8_t> atlasPixels = buildAtlas(aw, ah);
    AtlasGpu atlas = uploadAtlas(engine, atlasPixels, aw, ah);
    std::fprintf(stderr, "[main] step: atlas ok (%ux%u)\n", aw, ah);
    rt.setAtlas(atlas.view, atlas.sampler);

    /* Frame loop */
    while (engine.pumpEvents()) {
        /* Stage one frame's quads. The world-space frame is 240×160;
         * the layers stack at z = 0.0..0.8 per the spec. */
        layers.beginFrame();

        /* BG2 (back, blue) — fills the entire playfield */
        layers.drawBgQuad(Layer::BG2,
                          kCellBG2.u0, kCellBG2.v0, kCellBG2.u1, kCellBG2.v1);

        /* BG1 (mid, green) — a left-half band */
        layers.drawSprite(Layer::BG1, 0.0f, 0.0f, 120.0f, 160.0f,
                          kCellBG1.u0, kCellBG1.v0, kCellBG1.u1, kCellBG1.v1);

        /* BG0 (front, yellow) — a smaller right-side patch */
        layers.drawSprite(Layer::BG0, 140.0f, 30.0f, 80.0f, 80.0f,
                          kCellBG0.u0, kCellBG0.v0, kCellBG0.u1, kCellBG0.v1);

        /* Sprite (red) — a 16×16 quad sliding across the screen */
        static uint32_t frame = 0;
        const float x = 30.0f + std::fmod((float)frame * 0.8f, 180.0f);
        layers.drawSprite(Layer::Sprite, x, 70.0f, 16.0f, 16.0f,
                          kCellSprite.u0, kCellSprite.v0,
                          kCellSprite.u1, kCellSprite.v1);

        /* UI border (white, transparent interior) — top-corner box */
        layers.drawSprite(Layer::UI, 4.0f, 4.0f, 32.0f, 16.0f,
                          kCellUI.u0, kCellUI.v0, kCellUI.u1, kCellUI.v1);

        ++frame;

        /* Upload to GPU buffers, rebuild AS, dispatch rays. */
        static bool firstFrame = true;
        if (firstFrame) std::fprintf(stderr, "[main] step: first flushToBuffers\n");
        layers.flushToBuffers();
        if (firstFrame) std::fprintf(stderr, "[main] step: flushToBuffers ok\n");

        VkCommandBuffer cmd = engine.beginFrame();
        if (cmd == VK_NULL_HANDLE) continue;  /* swapchain out of date */

        if (layers.quadCount() > 0) {
            if (firstFrame) std::fprintf(stderr, "[main] step: rebuildAS\n");
            rt.rebuildAS(cmd);
            if (firstFrame) std::fprintf(stderr, "[main] step: rebuildAS ok, dispatchRays\n");
            rt.dispatchRays(cmd, engine.swapchainWidth(), engine.swapchainHeight());
            if (firstFrame) std::fprintf(stderr, "[main] step: dispatchRays returned (deferred)\n");
        }

        engine.endFrame();
        if (firstFrame) std::fprintf(stderr, "[main] step: endFrame ok\n");
        firstFrame = false;
    }

    /* Tidy up GPU resources we created here. The Engine + RT + layer
     * destructors free everything they own. */
    vkDeviceWaitIdle(engine.device());
    if (atlas.sampler) vkDestroySampler(engine.device(), atlas.sampler, nullptr);
    if (atlas.view)    vkDestroyImageView(engine.device(), atlas.view, nullptr);
    if (atlas.image)   vkDestroyImage(engine.device(), atlas.image, nullptr);
    if (atlas.memory)  vkFreeMemory(engine.device(), atlas.memory, nullptr);

    return 0;
}
