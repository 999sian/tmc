/* port/vk_rt_experiment/RenderLayerManager.cpp
 *
 * Host-side staging + GPU upload for the per-frame quad batch.
 * Buffers are allocated DEVICE_LOCAL with the storage and AS-build-
 * input usage bits so the BLAS builder can ingest them directly,
 * and uploaded from a HOST_VISIBLE staging buffer via vkCmdCopyBuffer.
 *
 * The host-staged vertices come out of beginFrame() into vectors;
 * drawSprite / drawBgQuad append; flushToBuffers does one staging
 * upload + one vkCmdCopyBuffer per buffer type.
 */

#include "RenderLayerManager.h"

#include <cstring>

namespace tmc_vkrt {

RenderLayerManager::RenderLayerManager(Engine& eng)
    : mEngine(eng) {
    /* Reserve enough headroom for a typical frame (a few hundred
     * sprites + background quads). Avoids realloc churn within a
     * frame. */
    mVertices.reserve(4 * 512);
    mIndices.reserve(6 * 512);
}

RenderLayerManager::~RenderLayerManager() {
    freeBuffers();
}

void RenderLayerManager::beginFrame() {
    mVertices.clear();
    mIndices.clear();
}

void RenderLayerManager::drawSprite(Layer layer, float x, float y, float w, float h,
                                    float uvU0, float uvV0, float uvU1, float uvV1,
                                    bool emissive) {
    emitQuad(layer, x, y, w, h, uvU0, uvV0, uvU1, uvV1, emissive);
}

void RenderLayerManager::drawBgQuad(Layer layer,
                                    float uvU0, float uvV0, float uvU1, float uvV1) {
    /* Full GBA frame at 240×160 (the engine's logical screen size).
     * The orthographic camera maps this 1:1 to the storage image. */
    emitQuad(layer, 0.0f, 0.0f, 240.0f, 160.0f, uvU0, uvV0, uvU1, uvV1, false);
}

void RenderLayerManager::emitQuad(Layer layer, float x, float y, float w, float h,
                                  float uvU0, float uvV0, float uvU1, float uvV1,
                                  bool emissive) {
    const float z = kLayerZ[(size_t)layer];
    const float e = emissive ? 1.0f : 0.0f;

    /* Vertex order: top-left, top-right, bottom-right, bottom-left
     * (counter-clockwise when viewed from -Z, which is where the
     * camera sits). Indices in kQuadIndices triangulate this. */
    const uint32_t base = (uint32_t)mVertices.size();
    mVertices.push_back({ {x,       y,       z}, {uvU0, uvV0}, e });
    mVertices.push_back({ {x + w,   y,       z}, {uvU1, uvV0}, e });
    mVertices.push_back({ {x + w,   y + h,   z}, {uvU1, uvV1}, e });
    mVertices.push_back({ {x,       y + h,   z}, {uvU0, uvV1}, e });

    for (uint32_t k = 0; k < 6; ++k) {
        mIndices.push_back(base + kQuadIndices[k]);
    }
}

void RenderLayerManager::flushToBuffers() {
    freeBuffers();
    if (mVertices.empty()) return;

    const VkDeviceSize vSize = mVertices.size() * sizeof(Vertex);
    const VkDeviceSize iSize = mIndices.size()  * sizeof(uint32_t);

    /* Device-local destination buffers. Usage flags:
     *   VERTEX_BUFFER       — for completeness (unused by RT path
     *                         but useful when debugging via raster)
     *   STORAGE_BUFFER      — closest-hit shader reads vertices
     *                         through the storage-buffer binding
     *   AS_BUILD_INPUT_READ — BLAS reads positions via device address
     *   SHADER_DEVICE_ADDRESS — needed to obtain the address used by
     *                           the AS-geometry struct
     *   TRANSFER_DST        — destination of the staging copy */
    const VkBufferUsageFlags kVtxUsage =
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT |
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
        VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
        VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
        VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    const VkBufferUsageFlags kIdxUsage =
        VK_BUFFER_USAGE_INDEX_BUFFER_BIT |
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
        VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
        VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
        VK_BUFFER_USAGE_TRANSFER_DST_BIT;

    mEngine.allocateBuffer(vSize, kVtxUsage, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                           &mBuffers.vertexBuffer, &mBuffers.vertexMemory);
    mEngine.allocateBuffer(iSize, kIdxUsage, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                           &mBuffers.indexBuffer, &mBuffers.indexMemory);

    /* Staging buffer — host-visible, used once. */
    VkBuffer       stagingV = VK_NULL_HANDLE;
    VkDeviceMemory stagingVMem = VK_NULL_HANDLE;
    VkBuffer       stagingI = VK_NULL_HANDLE;
    VkDeviceMemory stagingIMem = VK_NULL_HANDLE;
    const VkMemoryPropertyFlags kHostProps =
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

    mEngine.allocateBuffer(vSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                           kHostProps, &stagingV, &stagingVMem);
    mEngine.allocateBuffer(iSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                           kHostProps, &stagingI, &stagingIMem);

    /* Map → copy → unmap, then submit a one-shot copy. */
    void* ptr = nullptr;
    check(vkMapMemory(mEngine.device(), stagingVMem, 0, vSize, 0, &ptr),
          "vkMapMemory (vertex staging)");
    std::memcpy(ptr, mVertices.data(), (size_t)vSize);
    vkUnmapMemory(mEngine.device(), stagingVMem);

    check(vkMapMemory(mEngine.device(), stagingIMem, 0, iSize, 0, &ptr),
          "vkMapMemory (index staging)");
    std::memcpy(ptr, mIndices.data(), (size_t)iSize);
    vkUnmapMemory(mEngine.device(), stagingIMem);

    mEngine.oneShot([&](VkCommandBuffer cmd) {
        VkBufferCopy vCopy{};
        vCopy.size = vSize;
        vkCmdCopyBuffer(cmd, stagingV, mBuffers.vertexBuffer, 1, &vCopy);
        VkBufferCopy iCopy{};
        iCopy.size = iSize;
        vkCmdCopyBuffer(cmd, stagingI, mBuffers.indexBuffer, 1, &iCopy);
    });

    vkDestroyBuffer(mEngine.device(), stagingV, nullptr);
    vkFreeMemory(mEngine.device(), stagingVMem, nullptr);
    vkDestroyBuffer(mEngine.device(), stagingI, nullptr);
    vkFreeMemory(mEngine.device(), stagingIMem, nullptr);

    /* Capture device addresses for the AS builder. */
    VkBufferDeviceAddressInfo bda{};
    bda.sType  = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
    bda.buffer = mBuffers.vertexBuffer;
    mBuffers.vertexAddr = vkGetBufferDeviceAddress(mEngine.device(), &bda);
    bda.buffer = mBuffers.indexBuffer;
    mBuffers.indexAddr  = vkGetBufferDeviceAddress(mEngine.device(), &bda);

    mBuffers.vertexCount = (uint32_t)mVertices.size();
    mBuffers.indexCount  = (uint32_t)mIndices.size();
}

void RenderLayerManager::freeBuffers() {
    if (mBuffers.vertexBuffer) vkDestroyBuffer(mEngine.device(), mBuffers.vertexBuffer, nullptr);
    if (mBuffers.vertexMemory) vkFreeMemory(mEngine.device(), mBuffers.vertexMemory, nullptr);
    if (mBuffers.indexBuffer)  vkDestroyBuffer(mEngine.device(), mBuffers.indexBuffer,  nullptr);
    if (mBuffers.indexMemory)  vkFreeMemory(mEngine.device(), mBuffers.indexMemory,  nullptr);
    mBuffers = GeometryBuffers{};
}

}  /* namespace tmc_vkrt */
