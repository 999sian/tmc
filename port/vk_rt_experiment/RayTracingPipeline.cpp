/* port/vk_rt_experiment/RayTracingPipeline.cpp
 *
 * Implementation of BLAS/TLAS construction + RT pipeline + SBT.
 *
 * AS-build outline (per frame):
 *   1. vkGetAccelerationStructureBuildSizesKHR — query required
 *      buffer + scratch sizes given the current primitive count.
 *   2. Allocate (or resize) the BLAS backing buffer + scratch.
 *   3. vkCreateAccelerationStructureKHR — wraps the BLAS handle
 *      around the buffer.
 *   4. vkCmdBuildAccelerationStructuresKHR — actual build with the
 *      vertex/index geometry from RenderLayerManager.
 *   5. Memory barrier so the TLAS build observes the finished BLAS.
 *   6. Update the instance-buffer entry to reference the BLAS's
 *      device address with identity transform.
 *   7. Repeat steps 1–4 for the TLAS (with one instance).
 *
 * BLAS + TLAS are torn down + rebuilt every frame. Production code
 * would refit when possible; refit-vs-rebuild is omitted here for
 * clarity in the scaffold.
 *
 * SBT layout:
 *   The pipeline has three shader groups: 0=rgen, 1=miss, 2=hit.
 *   vkGetRayTracingShaderGroupHandlesKHR returns a packed array of
 *   `groupCount * shaderGroupHandleSize` bytes. We re-emit them
 *   into the SBT buffer with stride = shaderGroupHandleAlignment-
 *   rounded-up `shaderGroupHandleSize`, and offsets 0, stride, 2*stride.
 */

#include "RayTracingPipeline.h"

#include <cstdio>
#include <cstring>
#include <fstream>

namespace tmc_vkrt {

/* Round `value` up to the nearest multiple of `align`. */
static VkDeviceSize alignUp(VkDeviceSize value, VkDeviceSize align) {
    return (value + align - 1) & ~(align - 1);
}

RayTracingPipeline::RayTracingPipeline(Engine& eng, RenderLayerManager& layers)
    : mEngine(eng), mLayers(layers) {}

RayTracingPipeline::~RayTracingPipeline() {
    destroyAS();
    if (mScratchBuffer)   vkDestroyBuffer(mEngine.device(), mScratchBuffer,   nullptr);
    if (mScratchMemory)   vkFreeMemory(mEngine.device(), mScratchMemory,   nullptr);
    if (mInstancesBuffer) vkDestroyBuffer(mEngine.device(), mInstancesBuffer, nullptr);
    if (mInstancesMemory) vkFreeMemory(mEngine.device(), mInstancesMemory, nullptr);
    if (mSbtBuffer) vkDestroyBuffer(mEngine.device(), mSbtBuffer, nullptr);
    if (mSbtMemory) vkFreeMemory(mEngine.device(), mSbtMemory, nullptr);
    if (mPipeline)            vkDestroyPipeline(mEngine.device(), mPipeline, nullptr);
    if (mPipelineLayout)      vkDestroyPipelineLayout(mEngine.device(), mPipelineLayout, nullptr);
    if (mDescriptorPool)      vkDestroyDescriptorPool(mEngine.device(), mDescriptorPool, nullptr);
    if (mDescriptorSetLayout) vkDestroyDescriptorSetLayout(mEngine.device(), mDescriptorSetLayout, nullptr);
}

void RayTracingPipeline::createPipeline(const std::string& shaderDir) {
    /* Resolve KHR entry points. vkGetDeviceProcAddr is the right
     * choice here (not vkGetInstanceProcAddr) because these are
     * device-level functions. */
    auto resolve = [this](const char* name) -> void* {
        void* p = (void*)vkGetDeviceProcAddr(mEngine.device(), name);
        if (!p) throw Error(std::string("vkGetDeviceProcAddr failed for ") + name);
        return p;
    };
    pfnCreateAS              = (PFN_vkCreateAccelerationStructureKHR)              resolve("vkCreateAccelerationStructureKHR");
    pfnDestroyAS             = (PFN_vkDestroyAccelerationStructureKHR)             resolve("vkDestroyAccelerationStructureKHR");
    pfnGetBuildSizes         = (PFN_vkGetAccelerationStructureBuildSizesKHR)       resolve("vkGetAccelerationStructureBuildSizesKHR");
    pfnCmdBuildAS            = (PFN_vkCmdBuildAccelerationStructuresKHR)           resolve("vkCmdBuildAccelerationStructuresKHR");
    pfnGetASDeviceAddress    = (PFN_vkGetAccelerationStructureDeviceAddressKHR)    resolve("vkGetAccelerationStructureDeviceAddressKHR");
    pfnCreateRTPipelines     = (PFN_vkCreateRayTracingPipelinesKHR)                resolve("vkCreateRayTracingPipelinesKHR");
    pfnGetShaderGroupHandles = (PFN_vkGetRayTracingShaderGroupHandlesKHR)          resolve("vkGetRayTracingShaderGroupHandlesKHR");
    pfnCmdTraceRays          = (PFN_vkCmdTraceRaysKHR)                             resolve("vkCmdTraceRaysKHR");

    createDescriptorLayout();
    createDescriptorPool();
    createDescriptorSet();
    createPipelineLayout();
    createRayTracingPipeline(shaderDir);
    createShaderBindingTable();
}

void RayTracingPipeline::createDescriptorLayout() {
    /* Six bindings, one descriptor each (atlas uses an array of
     * exactly 1 for now; descriptor-indexing future-proofs this for
     * multi-atlas scenes). */
    VkDescriptorSetLayoutBinding bindings[6]{};

    /* binding 0: TLAS */
    bindings[0].binding         = 0;
    bindings[0].descriptorType  = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags      = VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;

    /* binding 1: storage image (the rgen output target) */
    bindings[1].binding         = 1;
    bindings[1].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags      = VK_SHADER_STAGE_RAYGEN_BIT_KHR;

    /* binding 2: vertex storage buffer (rchit reads UVs etc.) */
    bindings[2].binding         = 2;
    bindings[2].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[2].descriptorCount = 1;
    bindings[2].stageFlags      = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;

    /* binding 3: index storage buffer */
    bindings[3].binding         = 3;
    bindings[3].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[3].descriptorCount = 1;
    bindings[3].stageFlags      = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;

    /* binding 4: sampled-image array (atlas), size 1 for now */
    bindings[4].binding         = 4;
    bindings[4].descriptorType  = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    bindings[4].descriptorCount = 1;
    bindings[4].stageFlags      = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;

    /* binding 5: sampler (one linear-clamp for the atlas) */
    bindings[5].binding         = 5;
    bindings[5].descriptorType  = VK_DESCRIPTOR_TYPE_SAMPLER;
    bindings[5].descriptorCount = 1;
    bindings[5].stageFlags      = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;

    VkDescriptorSetLayoutCreateInfo li{};
    li.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    li.bindingCount = 6;
    li.pBindings    = bindings;
    check(vkCreateDescriptorSetLayout(mEngine.device(), &li, nullptr, &mDescriptorSetLayout),
          "vkCreateDescriptorSetLayout");
}

void RayTracingPipeline::createDescriptorPool() {
    VkDescriptorPoolSize sizes[5]{};
    sizes[0] = {VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 1};
    sizes[1] = {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,              1};
    sizes[2] = {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,             2};
    sizes[3] = {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,              1};
    sizes[4] = {VK_DESCRIPTOR_TYPE_SAMPLER,                    1};

    VkDescriptorPoolCreateInfo pci{};
    pci.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pci.maxSets       = 1;
    pci.poolSizeCount = 5;
    pci.pPoolSizes    = sizes;
    check(vkCreateDescriptorPool(mEngine.device(), &pci, nullptr, &mDescriptorPool),
          "vkCreateDescriptorPool");
}

void RayTracingPipeline::createDescriptorSet() {
    VkDescriptorSetAllocateInfo ai{};
    ai.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    ai.descriptorPool     = mDescriptorPool;
    ai.descriptorSetCount = 1;
    ai.pSetLayouts        = &mDescriptorSetLayout;
    check(vkAllocateDescriptorSets(mEngine.device(), &ai, &mDescriptorSet),
          "vkAllocateDescriptorSets");
}

void RayTracingPipeline::createPipelineLayout() {
    /* Push constants — frame counter + RNG seed are useful for
     * dithering and TAA-style accumulation in future passes; for the
     * current scaffold the rgen reads a single u32 frame index. */
    VkPushConstantRange pc{};
    pc.stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;
    pc.offset = 0;
    pc.size = 16;  /* room for vec4 of misc params */

    VkPipelineLayoutCreateInfo plci{};
    plci.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    plci.setLayoutCount         = 1;
    plci.pSetLayouts            = &mDescriptorSetLayout;
    plci.pushConstantRangeCount = 1;
    plci.pPushConstantRanges    = &pc;
    check(vkCreatePipelineLayout(mEngine.device(), &plci, nullptr, &mPipelineLayout),
          "vkCreatePipelineLayout");
}

void RayTracingPipeline::createRayTracingPipeline(const std::string& shaderDir) {
    std::vector<char> rgenCode = loadSpv(shaderDir + "/raygen.rgen.spv");
    std::vector<char> rmissCode = loadSpv(shaderDir + "/miss.rmiss.spv");
    std::vector<char> rchitCode = loadSpv(shaderDir + "/closesthit.rchit.spv");

    auto makeModule = [this](const std::vector<char>& code) -> VkShaderModule {
        VkShaderModuleCreateInfo smci{};
        smci.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        smci.codeSize = code.size();
        smci.pCode    = reinterpret_cast<const uint32_t*>(code.data());
        VkShaderModule mod = VK_NULL_HANDLE;
        check(vkCreateShaderModule(mEngine.device(), &smci, nullptr, &mod),
              "vkCreateShaderModule");
        return mod;
    };
    VkShaderModule rgen = makeModule(rgenCode);
    VkShaderModule rmiss = makeModule(rmissCode);
    VkShaderModule rchit = makeModule(rchitCode);

    VkPipelineShaderStageCreateInfo stages[3]{};
    stages[0].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage  = VK_SHADER_STAGE_RAYGEN_BIT_KHR;
    stages[0].module = rgen;
    stages[0].pName  = "main";
    stages[1].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage  = VK_SHADER_STAGE_MISS_BIT_KHR;
    stages[1].module = rmiss;
    stages[1].pName  = "main";
    stages[2].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[2].stage  = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;
    stages[2].module = rchit;
    stages[2].pName  = "main";

    /* Three groups — rgen (general), miss (general), hit (triangles).
     * Identity-mapped to stage indices 0/1/2. */
    VkRayTracingShaderGroupCreateInfoKHR groups[3]{};
    for (auto& g : groups) {
        g.sType                            = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
        g.generalShader                    = VK_SHADER_UNUSED_KHR;
        g.closestHitShader                 = VK_SHADER_UNUSED_KHR;
        g.anyHitShader                     = VK_SHADER_UNUSED_KHR;
        g.intersectionShader               = VK_SHADER_UNUSED_KHR;
    }
    groups[0].type           = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
    groups[0].generalShader  = 0;
    groups[1].type           = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
    groups[1].generalShader  = 1;
    groups[2].type           = VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR;
    groups[2].closestHitShader = 2;

    VkRayTracingPipelineCreateInfoKHR pci{};
    pci.sType                        = VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR;
    pci.stageCount                   = 3;
    pci.pStages                      = stages;
    pci.groupCount                   = 3;
    pci.pGroups                      = groups;
    /* One-bounce minimum; the closest-hit shader does emissive
     * accumulation along a single trace, so depth=2 (primary +
     * shadow) is enough for the scaffold. */
    pci.maxPipelineRayRecursionDepth = 2;
    pci.layout                       = mPipelineLayout;
    check(pfnCreateRTPipelines(mEngine.device(), VK_NULL_HANDLE, VK_NULL_HANDLE,
                               1, &pci, nullptr, &mPipeline),
          "vkCreateRayTracingPipelinesKHR");

    /* Modules can be destroyed now that the pipeline holds them. */
    vkDestroyShaderModule(mEngine.device(), rgen, nullptr);
    vkDestroyShaderModule(mEngine.device(), rmiss, nullptr);
    vkDestroyShaderModule(mEngine.device(), rchit, nullptr);
}

void RayTracingPipeline::createShaderBindingTable() {
    const auto& props = mEngine.rtProperties();
    const uint32_t groupCount = 3;
    const uint32_t handleSize = props.shaderGroupHandleSize;
    const uint32_t handleAlign = props.shaderGroupHandleAlignment;
    const uint32_t baseAlign  = props.shaderGroupBaseAlignment;

    /* Per-record stride within a region must be aligned to
     * shaderGroupHandleAlignment AND ≥ handleSize. */
    const VkDeviceSize stride = alignUp(handleSize, handleAlign);

    /* Per-region START (deviceAddress) must be aligned to
     * shaderGroupBaseAlignment. With one record per region, the
     * simplest correct layout is: each region occupies a baseAlign-
     * sized slot in the SBT buffer, even though the data inside is
     * only `stride` bytes. Lay them out as
     *     offset 0           → rgen
     *     offset baseAlign   → miss
     *     offset 2*baseAlign → hit
     * and the region.stride/size both equal stride (one record). */
    const VkDeviceSize regionStride = alignUp(stride, baseAlign);
    const VkDeviceSize sbtBytes     = regionStride * groupCount;

    std::vector<uint8_t> handles(handleSize * groupCount);
    check(pfnGetShaderGroupHandles(mEngine.device(), mPipeline, 0, groupCount,
                                   handles.size(), handles.data()),
          "vkGetRayTracingShaderGroupHandlesKHR");

    /* Host-visible SBT — small (kilobytes), so non-DEVICE_LOCAL is
     * fine for the scaffold. Production code would stage and keep
     * the SBT device-local. */
    mEngine.allocateBuffer(sbtBytes,
        VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR |
        VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        &mSbtBuffer, &mSbtMemory);

    void* ptr = nullptr;
    check(vkMapMemory(mEngine.device(), mSbtMemory, 0, sbtBytes, 0, &ptr), "vkMapMemory (sbt)");
    /* Zero the whole buffer first so the padding past `handleSize`
     * inside each region is well-defined (Vulkan doesn't require it,
     * but it makes the SBT contents reproducible across runs). */
    std::memset(ptr, 0, (size_t)sbtBytes);
    uint8_t* dst = (uint8_t*)ptr;
    for (uint32_t i = 0; i < groupCount; ++i) {
        std::memcpy(dst + i * regionStride,
                    handles.data() + i * handleSize, handleSize);
    }
    vkUnmapMemory(mEngine.device(), mSbtMemory);

    VkBufferDeviceAddressInfo bda{};
    bda.sType  = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
    bda.buffer = mSbtBuffer;
    VkDeviceAddress base = vkGetBufferDeviceAddress(mEngine.device(), &bda);

    mRgenRegion.deviceAddress = base + 0 * regionStride;
    mRgenRegion.stride        = stride;
    /* For ray-gen specifically, size MUST equal stride. */
    mRgenRegion.size          = stride;
    mMissRegion.deviceAddress = base + 1 * regionStride;
    mMissRegion.stride        = stride;
    mMissRegion.size          = stride;
    mHitRegion.deviceAddress  = base + 2 * regionStride;
    mHitRegion.stride         = stride;
    mHitRegion.size           = stride;
    /* No callable shaders → empty region. */
    mCallRegion = VkStridedDeviceAddressRegionKHR{};
}

std::vector<char> RayTracingPipeline::loadSpv(const std::string& path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) throw Error("loadSpv: cannot open " + path);
    auto end = f.tellg();
    f.seekg(0);
    std::vector<char> data((size_t)end);
    f.read(data.data(), data.size());
    if ((size_t)f.gcount() != data.size()) {
        throw Error("loadSpv: short read on " + path);
    }
    return data;
}

void RayTracingPipeline::setAtlas(VkImageView atlasView, VkSampler atlasSampler) {
    mAtlasView    = atlasView;
    mAtlasSampler = atlasSampler;
    mAtlasBound   = true;
}

void RayTracingPipeline::destroyAS() {
    if (mTlas && pfnDestroyAS) pfnDestroyAS(mEngine.device(), mTlas, nullptr);
    if (mTlasBuffer) vkDestroyBuffer(mEngine.device(), mTlasBuffer, nullptr);
    if (mTlasMemory) vkFreeMemory(mEngine.device(), mTlasMemory, nullptr);
    if (mBlas && pfnDestroyAS) pfnDestroyAS(mEngine.device(), mBlas, nullptr);
    if (mBlasBuffer) vkDestroyBuffer(mEngine.device(), mBlasBuffer, nullptr);
    if (mBlasMemory) vkFreeMemory(mEngine.device(), mBlasMemory, nullptr);
    mTlas = mBlas = VK_NULL_HANDLE;
    mTlasBuffer = mBlasBuffer = VK_NULL_HANDLE;
    mTlasMemory = mBlasMemory = VK_NULL_HANDLE;
}

void RayTracingPipeline::rebuildAS(VkCommandBuffer cmd) {
    /* The AS handles + buffers from the previous frame may still be
     * in use by an in-flight command buffer. Serialise with the GPU
     * before destroying. Crude (kills frame-pacing parallelism) but
     * correct — proper code would defer destruction via a per-frame
     * delete queue gated on the frame's fence. */
    vkDeviceWaitIdle(mEngine.device());
    destroyAS();
    buildBLAS(cmd);

    /* Inter-AS-build barrier: TLAS build reads BLAS via its device
     * address, so we need an acceleration-structure read-after-write
     * sync. */
    VkMemoryBarrier mb{};
    mb.sType         = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    mb.srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
    mb.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR;
    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
        VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
        0, 1, &mb, 0, nullptr, 0, nullptr);

    buildTLAS(cmd);

    /* Final barrier — ray tracing pipeline reads the TLAS. */
    mb.srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
    mb.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR;
    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
        VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,
        0, 1, &mb, 0, nullptr, 0, nullptr);

    /* Now wire the freshly-built TLAS + storage image + buffers
     * into the descriptor set. */
    VkWriteDescriptorSetAccelerationStructureKHR asDescInfo{};
    asDescInfo.sType                      = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR;
    asDescInfo.accelerationStructureCount = 1;
    asDescInfo.pAccelerationStructures    = &mTlas;

    VkDescriptorImageInfo imgInfo{};
    imgInfo.imageView   = mEngine.storageImageView();
    imgInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

    VkDescriptorBufferInfo vBufInfo{};
    vBufInfo.buffer = mLayers.buffers().vertexBuffer;
    vBufInfo.range  = VK_WHOLE_SIZE;
    VkDescriptorBufferInfo iBufInfo{};
    iBufInfo.buffer = mLayers.buffers().indexBuffer;
    iBufInfo.range  = VK_WHOLE_SIZE;

    VkDescriptorImageInfo atlasInfo{};
    atlasInfo.imageView   = mAtlasView;
    atlasInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VkDescriptorImageInfo samplerInfo{};
    samplerInfo.sampler = mAtlasSampler;

    VkWriteDescriptorSet writes[6]{};
    writes[0].sType            = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[0].pNext            = &asDescInfo;
    writes[0].dstSet           = mDescriptorSet;
    writes[0].dstBinding       = 0;
    writes[0].descriptorCount  = 1;
    writes[0].descriptorType   = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;

    writes[1].sType            = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[1].dstSet           = mDescriptorSet;
    writes[1].dstBinding       = 1;
    writes[1].descriptorCount  = 1;
    writes[1].descriptorType   = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[1].pImageInfo       = &imgInfo;

    writes[2].sType            = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[2].dstSet           = mDescriptorSet;
    writes[2].dstBinding       = 2;
    writes[2].descriptorCount  = 1;
    writes[2].descriptorType   = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[2].pBufferInfo      = &vBufInfo;

    writes[3].sType            = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[3].dstSet           = mDescriptorSet;
    writes[3].dstBinding       = 3;
    writes[3].descriptorCount  = 1;
    writes[3].descriptorType   = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[3].pBufferInfo      = &iBufInfo;

    uint32_t writeCount = 4;
    if (mAtlasBound) {
        writes[4].sType            = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[4].dstSet           = mDescriptorSet;
        writes[4].dstBinding       = 4;
        writes[4].descriptorCount  = 1;
        writes[4].descriptorType   = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
        writes[4].pImageInfo       = &atlasInfo;

        writes[5].sType            = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[5].dstSet           = mDescriptorSet;
        writes[5].dstBinding       = 5;
        writes[5].descriptorCount  = 1;
        writes[5].descriptorType   = VK_DESCRIPTOR_TYPE_SAMPLER;
        writes[5].pImageInfo       = &samplerInfo;
        writeCount = 6;
    }
    vkUpdateDescriptorSets(mEngine.device(), writeCount, writes, 0, nullptr);
}

void RayTracingPipeline::buildBLAS(VkCommandBuffer cmd) {
    /* Geometry description: one triangle set, position-only at
     * offset 0 of each Vertex (we stride past UV+emissive). */
    VkAccelerationStructureGeometryTrianglesDataKHR tri{};
    tri.sType        = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
    tri.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
    tri.vertexData.deviceAddress = mLayers.buffers().vertexAddr;
    tri.vertexStride = RenderLayerManager::vertexStride();
    tri.maxVertex    = mLayers.buffers().vertexCount - 1;
    tri.indexType    = VK_INDEX_TYPE_UINT32;
    tri.indexData.deviceAddress  = mLayers.buffers().indexAddr;
    tri.transformData.deviceAddress = 0;  /* no per-geometry transform */

    VkAccelerationStructureGeometryKHR geom{};
    geom.sType         = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
    geom.geometryType  = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
    geom.geometry.triangles = tri;
    geom.flags         = VK_GEOMETRY_OPAQUE_BIT_KHR;

    const uint32_t primitiveCount = mLayers.buffers().indexCount / 3;

    VkAccelerationStructureBuildGeometryInfoKHR bgi{};
    bgi.sType         = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
    bgi.type          = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
    bgi.flags         = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
    bgi.mode          = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    bgi.geometryCount = 1;
    bgi.pGeometries   = &geom;

    VkAccelerationStructureBuildSizesInfoKHR sizes{};
    sizes.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
    pfnGetBuildSizes(mEngine.device(),
        VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
        &bgi, &primitiveCount, &sizes);

    /* Allocate BLAS backing buffer */
    mEngine.allocateBuffer(sizes.accelerationStructureSize,
        VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR |
        VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        &mBlasBuffer, &mBlasMemory);

    /* Allocate/grow scratch */
    if (sizes.buildScratchSize > mScratchSize) {
        if (mScratchBuffer) {
            vkDestroyBuffer(mEngine.device(), mScratchBuffer, nullptr);
            vkFreeMemory(mEngine.device(), mScratchMemory, nullptr);
        }
        mEngine.allocateBuffer(sizes.buildScratchSize,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            &mScratchBuffer, &mScratchMemory);
        mScratchSize = sizes.buildScratchSize;
    }
    VkBufferDeviceAddressInfo bda{};
    bda.sType  = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
    bda.buffer = mScratchBuffer;
    VkDeviceAddress scratchAddr = vkGetBufferDeviceAddress(mEngine.device(), &bda);

    /* Create the AS handle wrapping the BLAS buffer */
    VkAccelerationStructureCreateInfoKHR aci{};
    aci.sType  = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
    aci.buffer = mBlasBuffer;
    aci.size   = sizes.accelerationStructureSize;
    aci.type   = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
    check(pfnCreateAS(mEngine.device(), &aci, nullptr, &mBlas),
          "vkCreateAccelerationStructureKHR (BLAS)");

    bgi.dstAccelerationStructure  = mBlas;
    bgi.scratchData.deviceAddress = scratchAddr;

    VkAccelerationStructureBuildRangeInfoKHR range{};
    range.primitiveCount = primitiveCount;
    const VkAccelerationStructureBuildRangeInfoKHR* ranges = &range;

    pfnCmdBuildAS(cmd, 1, &bgi, &ranges);
}

void RayTracingPipeline::buildTLAS(VkCommandBuffer cmd) {
    /* One instance, identity transform pointing at the BLAS. */
    VkAccelerationStructureDeviceAddressInfoKHR addrInfo{};
    addrInfo.sType                 = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR;
    addrInfo.accelerationStructure = mBlas;
    VkDeviceAddress blasAddr = pfnGetASDeviceAddress(mEngine.device(), &addrInfo);

    VkAccelerationStructureInstanceKHR instance{};
    /* row-major 3x4 identity transform */
    instance.transform.matrix[0][0] = 1.0f;
    instance.transform.matrix[1][1] = 1.0f;
    instance.transform.matrix[2][2] = 1.0f;
    instance.instanceCustomIndex = 0;
    instance.mask                = 0xFF;
    instance.instanceShaderBindingTableRecordOffset = 0;
    instance.flags               = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
    instance.accelerationStructureReference = blasAddr;

    /* Upload the single instance to the instance buffer (host-visible). */
    if (!mInstancesBuffer) {
        mEngine.allocateBuffer(sizeof(VkAccelerationStructureInstanceKHR),
            VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
            VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            &mInstancesBuffer, &mInstancesMemory);
    }
    void* ptr = nullptr;
    check(vkMapMemory(mEngine.device(), mInstancesMemory, 0,
                      sizeof(VkAccelerationStructureInstanceKHR), 0, &ptr),
          "vkMapMemory (instances)");
    std::memcpy(ptr, &instance, sizeof(VkAccelerationStructureInstanceKHR));
    vkUnmapMemory(mEngine.device(), mInstancesMemory);

    VkBufferDeviceAddressInfo bda{};
    bda.sType  = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
    bda.buffer = mInstancesBuffer;
    VkDeviceAddress instancesAddr = vkGetBufferDeviceAddress(mEngine.device(), &bda);

    VkAccelerationStructureGeometryInstancesDataKHR inst{};
    inst.sType              = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
    inst.arrayOfPointers    = VK_FALSE;
    inst.data.deviceAddress = instancesAddr;

    VkAccelerationStructureGeometryKHR geom{};
    geom.sType        = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
    geom.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
    geom.geometry.instances = inst;

    VkAccelerationStructureBuildGeometryInfoKHR bgi{};
    bgi.sType         = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
    bgi.type          = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
    bgi.flags         = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
    bgi.mode          = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    bgi.geometryCount = 1;
    bgi.pGeometries   = &geom;

    uint32_t instanceCount = 1;
    VkAccelerationStructureBuildSizesInfoKHR sizes{};
    sizes.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
    pfnGetBuildSizes(mEngine.device(),
        VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
        &bgi, &instanceCount, &sizes);

    mEngine.allocateBuffer(sizes.accelerationStructureSize,
        VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR |
        VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        &mTlasBuffer, &mTlasMemory);

    if (sizes.buildScratchSize > mScratchSize) {
        if (mScratchBuffer) {
            vkDestroyBuffer(mEngine.device(), mScratchBuffer, nullptr);
            vkFreeMemory(mEngine.device(), mScratchMemory, nullptr);
        }
        mEngine.allocateBuffer(sizes.buildScratchSize,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            &mScratchBuffer, &mScratchMemory);
        mScratchSize = sizes.buildScratchSize;
    }
    VkBufferDeviceAddressInfo bdaS{};
    bdaS.sType  = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
    bdaS.buffer = mScratchBuffer;
    VkDeviceAddress scratchAddr = vkGetBufferDeviceAddress(mEngine.device(), &bdaS);

    VkAccelerationStructureCreateInfoKHR aci{};
    aci.sType  = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
    aci.buffer = mTlasBuffer;
    aci.size   = sizes.accelerationStructureSize;
    aci.type   = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
    check(pfnCreateAS(mEngine.device(), &aci, nullptr, &mTlas),
          "vkCreateAccelerationStructureKHR (TLAS)");

    bgi.dstAccelerationStructure  = mTlas;
    bgi.scratchData.deviceAddress = scratchAddr;

    VkAccelerationStructureBuildRangeInfoKHR range{};
    range.primitiveCount = 1;
    const VkAccelerationStructureBuildRangeInfoKHR* ranges = &range;
    pfnCmdBuildAS(cmd, 1, &bgi, &ranges);
}

void RayTracingPipeline::dispatchRays(VkCommandBuffer cmd, uint32_t width, uint32_t height) {
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, mPipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR,
                            mPipelineLayout, 0, 1, &mDescriptorSet, 0, nullptr);

    /* Push constants — rgen/rchit statically reference pc.params,
     * which means the driver REQUIRES vkCmdPushConstants to have been
     * called before vkCmdTraceRaysKHR, even if all values are zero.
     * Without this, validation flags the trace and NVIDIA's strict
     * path silently produces nothing. Default: (frameIndex=0, time=0,
     * exposure=1.0, reserved=0). */
    float pc[4] = {0.0f, 0.0f, 1.0f, 0.0f};
    vkCmdPushConstants(cmd, mPipelineLayout,
                       VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR,
                       0, sizeof(pc), pc);

    pfnCmdTraceRays(cmd, &mRgenRegion, &mMissRegion, &mHitRegion, &mCallRegion,
                    width, height, 1);
}

}  /* namespace tmc_vkrt */
