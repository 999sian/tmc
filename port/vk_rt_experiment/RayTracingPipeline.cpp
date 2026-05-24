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
    /* Nine bindings for the PTGI pipeline:
     *   0 TLAS, 1 outputImage, 2 verts, 3 indices,
     *   4 diffuse atlas[], 5 sampler,
     *   6 accumImage (HDR running mean for path-trace progressive),
     *   7 emissive atlas[], 8 normal atlas[]. */
    VkDescriptorSetLayoutBinding bindings[9]{};

    bindings[0].binding         = 0;
    bindings[0].descriptorType  = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags      = VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;

    bindings[1].binding         = 1;
    bindings[1].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags      = VK_SHADER_STAGE_RAYGEN_BIT_KHR;

    bindings[2].binding         = 2;
    bindings[2].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[2].descriptorCount = 1;
    bindings[2].stageFlags      = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;

    bindings[3].binding         = 3;
    bindings[3].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[3].descriptorCount = 1;
    bindings[3].stageFlags      = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;

    bindings[4].binding         = 4;
    bindings[4].descriptorType  = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    bindings[4].descriptorCount = 1;
    bindings[4].stageFlags      = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;

    bindings[5].binding         = 5;
    bindings[5].descriptorType  = VK_DESCRIPTOR_TYPE_SAMPLER;
    bindings[5].descriptorCount = 1;
    bindings[5].stageFlags      = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;

    bindings[6].binding         = 6;
    bindings[6].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    bindings[6].descriptorCount = 1;
    bindings[6].stageFlags      = VK_SHADER_STAGE_RAYGEN_BIT_KHR;

    bindings[7].binding         = 7;
    bindings[7].descriptorType  = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    bindings[7].descriptorCount = 1;
    bindings[7].stageFlags      = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;

    bindings[8].binding         = 8;
    bindings[8].descriptorType  = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    bindings[8].descriptorCount = 1;
    bindings[8].stageFlags      = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;

    VkDescriptorSetLayoutCreateInfo li{};
    li.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    li.bindingCount = 9;
    li.pBindings    = bindings;
    check(vkCreateDescriptorSetLayout(mEngine.device(), &li, nullptr, &mDescriptorSetLayout),
          "vkCreateDescriptorSetLayout");
}

void RayTracingPipeline::createDescriptorPool() {
    VkDescriptorPoolSize sizes[5]{};
    sizes[0] = {VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 1};
    sizes[1] = {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,              2}; /* outputImage + accumImage */
    sizes[2] = {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,             2}; /* verts + indices */
    sizes[3] = {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,              3}; /* diffuse + emissive + normal */
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
    /* Path-trace pipeline: rgen + 1 miss + rchit (3 stages, 3 groups).
     * No shadow rays — the path tracer accumulates emissive
     * contributions along bounce chains instead. */
    std::vector<char> rgenCode      = loadSpv(shaderDir + "/path_trace.rgen.spv");
    std::vector<char> rmissCode     = loadSpv(shaderDir + "/path_trace_miss.rmiss.spv");
    std::vector<char> shadowMissCode;  /* unused */
    std::vector<char> rchitCode     = loadSpv(shaderDir + "/path_trace.rchit.spv");

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
    (void)shadowMissCode;
    VkShaderModule rgen  = makeModule(rgenCode);
    VkShaderModule rmiss = makeModule(rmissCode);
    VkShaderModule rchit = makeModule(rchitCode);

    /* Three stages: rgen=0, miss=1, rchit=2. */
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

    /* Three shader groups: rgen general, miss general, hit
     * triangles. Identity-mapped to stages 0/1/2. The path tracer's
     * trace calls all use missIndex=0 (no shadow rays), so a single
     * miss record in the SBT is enough. */
    VkRayTracingShaderGroupCreateInfoKHR groups[3]{};
    for (auto& g : groups) {
        g.sType              = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
        g.generalShader      = VK_SHADER_UNUSED_KHR;
        g.closestHitShader   = VK_SHADER_UNUSED_KHR;
        g.anyHitShader       = VK_SHADER_UNUSED_KHR;
        g.intersectionShader = VK_SHADER_UNUSED_KHR;
    }
    groups[0].type             = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
    groups[0].generalShader    = 0;
    groups[1].type             = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
    groups[1].generalShader    = 1;
    groups[2].type             = VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR;
    groups[2].closestHitShader = 2;

    VkRayTracingPipelineCreateInfoKHR pci{};
    pci.sType                        = VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR;
    pci.stageCount                   = 3;
    pci.pStages                      = stages;
    pci.groupCount                   = 3;
    pci.pGroups                      = groups;
    /* PT uses an in-shader bounce loop (not real recursion in
     * Vulkan terms), so depth=1 suffices. */
    pci.maxPipelineRayRecursionDepth = 1;
    pci.layout                       = mPipelineLayout;
    check(pfnCreateRTPipelines(mEngine.device(), VK_NULL_HANDLE, VK_NULL_HANDLE,
                               1, &pci, nullptr, &mPipeline),
          "vkCreateRayTracingPipelinesKHR");

    vkDestroyShaderModule(mEngine.device(), rgen, nullptr);
    vkDestroyShaderModule(mEngine.device(), rmiss, nullptr);
    vkDestroyShaderModule(mEngine.device(), rchit, nullptr);
}

void RayTracingPipeline::createShaderBindingTable() {
    const auto& props = mEngine.rtProperties();
    const uint32_t groupCount = 3;       /* rgen + 1 miss + rchit */
    const uint32_t missCount  = 1;       /* path-tracer only needs primary miss */
    const uint32_t handleSize = props.shaderGroupHandleSize;
    const uint32_t handleAlign = props.shaderGroupHandleAlignment;
    const uint32_t baseAlign  = props.shaderGroupBaseAlignment;

    /* Per-record stride: handleSize rounded up to handleAlignment. */
    const VkDeviceSize stride = alignUp(handleSize, handleAlign);

    /* Per-region layout: each region's deviceAddress must be
     * baseAlignment-aligned. Records inside a region are at multiples
     * of stride.
     *
     *   rgen region  : 1 record at offset 0
     *   miss region  : missCount records starting at offset miss0
     *   hit region   : 1 record starting at offset hit0
     *
     * Offsets sized so each region starts baseAlign-aligned and
     * regions don't overlap. */
    const VkDeviceSize rgenOff   = 0;
    const VkDeviceSize rgenSize  = stride;
    const VkDeviceSize missOff   = alignUp(rgenOff + rgenSize, baseAlign);
    const VkDeviceSize missSize  = stride * missCount;
    const VkDeviceSize hitOff    = alignUp(missOff + missSize, baseAlign);
    const VkDeviceSize hitSize   = stride;
    const VkDeviceSize sbtBytes  = alignUp(hitOff + hitSize, baseAlign);

    /* Fetch all group handles in one call. */
    std::vector<uint8_t> handles(handleSize * groupCount);
    check(pfnGetShaderGroupHandles(mEngine.device(), mPipeline, 0, groupCount,
                                   handles.size(), handles.data()),
          "vkGetRayTracingShaderGroupHandlesKHR");

    mEngine.allocateBuffer(sbtBytes,
        VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR |
        VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        &mSbtBuffer, &mSbtMemory);

    void* ptr = nullptr;
    check(vkMapMemory(mEngine.device(), mSbtMemory, 0, sbtBytes, 0, &ptr), "vkMapMemory (sbt)");
    std::memset(ptr, 0, (size_t)sbtBytes);
    uint8_t* dst = (uint8_t*)ptr;
    /* group indices: 0=rgen, 1=miss, 2=hit. */
    std::memcpy(dst + rgenOff, handles.data() + 0 * handleSize, handleSize);
    std::memcpy(dst + missOff, handles.data() + 1 * handleSize, handleSize);
    std::memcpy(dst + hitOff,  handles.data() + 2 * handleSize, handleSize);
    vkUnmapMemory(mEngine.device(), mSbtMemory);

    VkBufferDeviceAddressInfo bda{};
    bda.sType  = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
    bda.buffer = mSbtBuffer;
    VkDeviceAddress base = vkGetBufferDeviceAddress(mEngine.device(), &bda);

    mRgenRegion.deviceAddress = base + rgenOff;
    mRgenRegion.stride        = stride;
    mRgenRegion.size          = stride;          /* rgen: size == stride */
    mMissRegion.deviceAddress = base + missOff;
    mMissRegion.stride        = stride;
    mMissRegion.size          = stride * missCount;
    mHitRegion.deviceAddress  = base + hitOff;
    mHitRegion.stride         = stride;
    mHitRegion.size           = stride;
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

void RayTracingPipeline::setAtlas(VkImageView diffuseView, VkSampler atlasSampler,
                                  VkImageView emissiveView, VkImageView normalView) {
    mAtlasView      = diffuseView;
    mAtlasSampler   = atlasSampler;
    /* Fall back to the diffuse view when no emissive/normal map is
     * supplied. The shaders treat absent emissive/normal sensibly:
     * the emissive sample only matters when the material flag is
     * set (so for non-emissive material codes the binding's content
     * is irrelevant), and the normal map's "grey = no perturbation"
     * convention plus our 0.05-tolerance length check means random
     * diffuse data won't visibly perturb the geometric normal in
     * practice. */
    mEmissiveView   = (emissiveView != VK_NULL_HANDLE) ? emissiveView : diffuseView;
    mNormalView     = (normalView   != VK_NULL_HANDLE) ? normalView   : diffuseView;
    mAtlasBound     = true;
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

    VkDescriptorImageInfo accumInfo{};
    accumInfo.imageView   = mEngine.accumImageView();
    accumInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    VkDescriptorImageInfo emissiveInfo{};
    emissiveInfo.imageView   = mEmissiveView;
    emissiveInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VkDescriptorImageInfo normalInfo{};
    normalInfo.imageView   = mNormalView;
    normalInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkWriteDescriptorSet writes[9]{};
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

        /* Path-tracer extras: accum image + emissive + normal atlas. */
        writes[6].sType            = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[6].dstSet           = mDescriptorSet;
        writes[6].dstBinding       = 6;
        writes[6].descriptorCount  = 1;
        writes[6].descriptorType   = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        writes[6].pImageInfo       = &accumInfo;

        writes[7].sType            = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[7].dstSet           = mDescriptorSet;
        writes[7].dstBinding       = 7;
        writes[7].descriptorCount  = 1;
        writes[7].descriptorType   = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
        writes[7].pImageInfo       = &emissiveInfo;

        writes[8].sType            = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[8].dstSet           = mDescriptorSet;
        writes[8].dstBinding       = 8;
        writes[8].descriptorCount  = 1;
        writes[8].descriptorType   = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
        writes[8].pImageInfo       = &normalInfo;
        writeCount = 9;
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

void RayTracingPipeline::dispatchRays(VkCommandBuffer cmd, uint32_t width, uint32_t height,
                                      uint32_t frameIndex, float time) {
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, mPipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR,
                            mPipelineLayout, 0, 1, &mDescriptorSet, 0, nullptr);

    /* Push constants — pc.params is used by the rgen (exposure at .z)
     * and the rchit (frameIndex at .x, time at .y for light animation).
     * NVIDIA's strict path requires the call before vkCmdTraceRaysKHR
     * even when values would all be defaults; production code must
     * always supply them. */
    float pc[4] = {
        static_cast<float>(frameIndex),  /* .x = frame index */
        time,                            /* .y = seconds (animates lights) */
        1.0f,                            /* .z = exposure */
        0.0f                             /* .w = reserved */
    };
    vkCmdPushConstants(cmd, mPipelineLayout,
                       VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR,
                       0, sizeof(pc), pc);

    pfnCmdTraceRays(cmd, &mRgenRegion, &mMissRegion, &mHitRegion, &mCallRegion,
                    width, height, 1);
}

}  /* namespace tmc_vkrt */
