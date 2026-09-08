#include "ShadowRayPass.h"
#include "BlueNoiseTexture.h"
#include "Core/Logger.h"
#include "engine/render/FrameSync.h"
#include "engine/render/VulkanContext.h"
#include "Resources/ShaderModule.h"
#include "ShadowBuffer.h"
#include "engine/render/Utils.h"

#include <bit>
#include <cstring>
#include <fstream>
#include <span>
#include <vector>

namespace
{
constexpr const char* kShadowRayPassSubsystem = "ShadowRayPass";

bool logShadowRayPassFailure(std::string_view message)
{
    logError(kShadowRayPassSubsystem, message);
    return false;
}

bool logShadowRayPassVkFailure(std::string_view action, VkResult result)
{
    return logShadowRayPassFailure(
        makeLogMessage(action, " failed with VkResult ", static_cast<int>(result), "."));
}
} // namespace

bool ShadowRayPass::create(VulkanContext& ctx, VkDescriptorSetLayout voxelSetLayout)
{
    voxelSetLayout_ = voxelSetLayout;
    shaderPath_ = resolveShaderPath(nullptr, "shadow_ray.comp.spv");

    if (!createPipeline(ctx))
    {
        destroy(ctx);
        return false;
    }

    if (!createParityMetricBuffers(ctx) || !createDescriptorSets(ctx))
    {
        destroy(ctx);
        return false;
    }

    return true;
}

void ShadowRayPass::destroy(VulkanContext& ctx)
{
    (void)ctx;
    pipeline_.reset();
    pipelineLayout_.reset();
    descriptorSetLayout_ = VK_NULL_HANDLE;
    dummySetLayout_ = VK_NULL_HANDLE;
    for (engine::render::UniqueBuffer& buffer : parityMetricBuffers_)
    {
        buffer.reset();
    }
    parityMetricsNeedReset_.fill(false);
    parityMetricsSubmitted_.fill(false);
    terrainShadowColumnParityTotals_ = {};
    for (VkDescriptorSet& descriptorSet : descriptorSets_)
    {
        descriptorSet = VK_NULL_HANDLE;
    }
}

bool ShadowRayPass::createPipeline(VulkanContext& ctx)
{
    // create descriptor set layout for set 0 (g-buffer inputs, blue noise, shadow output)
    VkDescriptorSetLayoutBinding bindings[6]{};

    // binding 0: g-buffer depth
    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    // binding 1: g-buffer normal
    bindings[1].binding = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    // binding 2: blue noise
    bindings[2].binding = 2;
    bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[2].descriptorCount = 1;
    bindings[2].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    // binding 3: shadow output (storage image)
    bindings[3].binding = 3;
    bindings[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    bindings[3].descriptorCount = 1;
    bindings[3].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    bindings[4].binding = 4;
    bindings[4].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[4].descriptorCount = 1;
    bindings[4].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    bindings[5].binding = 5;
    bindings[5].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[5].descriptorCount = 1;
    bindings[5].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 6;
    layoutInfo.pBindings = bindings;

    if (!ctx.descriptorLayoutCache.get(ctx.device, layoutInfo, descriptorSetLayout_))
    {
        return logShadowRayPassVkFailure("vkCreateDescriptorSetLayout for shadow ray inputs",
                                         VK_ERROR_INITIALIZATION_FAILED);
    }

    // create dummy descriptor set layout for set 1 (unused but required for pipeline layout)
    VkDescriptorSetLayoutCreateInfo dummyLayoutInfo{};
    dummyLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    dummyLayoutInfo.bindingCount = 0;
    dummyLayoutInfo.pBindings = nullptr;
    if (!ctx.descriptorLayoutCache.get(ctx.device, dummyLayoutInfo, dummySetLayout_))
    {
        return logShadowRayPassVkFailure("vkCreateDescriptorSetLayout for shadow ray dummy set",
                                         VK_ERROR_INITIALIZATION_FAILED);
    }

    // create pipeline layout with set 0 (ours), set 1 (dummy), and set 2 (voxel data)
    VkDescriptorSetLayout setLayouts[3] = {
        descriptorSetLayout_,  // set 0
        dummySetLayout_,       // set 1 (empty/unused)
        voxelSetLayout_        // set 2 (voxel volumes - matches VOXEL_SET=2 in shader)
    };

    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(PushConstants);

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 3;
    pipelineLayoutInfo.pSetLayouts = setLayouts;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkResult result =
        vkCreatePipelineLayout(ctx.device, &pipelineLayoutInfo, nullptr, &pipelineLayout);
    if (result != VK_SUCCESS)
    {
        return logShadowRayPassVkFailure("vkCreatePipelineLayout for shadow ray pass", result);
    }
    pipelineLayout_ = engine::render::UniquePipelineLayout(ctx.device, pipelineLayout);

    // load shader
    std::vector<char> shaderCode;
    std::string shaderError;
    if (!tryReadFile(shaderPath_.c_str(), shaderCode, &shaderError))
    {
        logError("Shader", std::string("ShadowRayPass failed to load shader '") + shaderPath_ +
                               "': " + shaderError);
        return false;
    }

    VkShaderModule shaderModule = tryCreateShaderModule(ctx.device, shaderCode, &shaderError);
    if (shaderModule == VK_NULL_HANDLE)
    {
        logError("Shader", std::string("ShadowRayPass failed to create shader module for '") +
                               shaderPath_ + "': " + shaderError);
        return false;
    }

    VkPipelineShaderStageCreateInfo shaderStageInfo{};
    shaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    shaderStageInfo.module = shaderModule;
    shaderStageInfo.pName = "main";

    VkComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.stage = shaderStageInfo;
    pipelineInfo.layout = pipelineLayout_.get();

    VkPipeline pipeline = VK_NULL_HANDLE;
    result =
        vkCreateComputePipelines(ctx.device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline);
    if (result != VK_SUCCESS)
    {
        vkDestroyShaderModule(ctx.device, shaderModule, nullptr);
        return logShadowRayPassVkFailure("vkCreateComputePipelines for shadow ray pass", result);
    }
    pipeline_ = engine::render::UniquePipeline(ctx.device, pipeline);

    vkDestroyShaderModule(ctx.device, shaderModule, nullptr);

    return true;
}

bool ShadowRayPass::createParityMetricBuffers(VulkanContext& ctx)
{
    static_assert(sizeof(TerrainShadowColumnParityMetrics) == 64);
    for (uint32_t frameIndex = 0; frameIndex < kMaxFramesInFlight;
         ++frameIndex)
    {
        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = sizeof(TerrainShadowColumnParityMetrics);
        bufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                           VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo allocationInfo{};
        allocationInfo.flags =
            VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT;
        allocationInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
        allocationInfo.requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
        if (engine::render::createUniqueBuffer(
                ctx.memoryAllocator, bufferInfo, allocationInfo,
                parityMetricBuffers_[frameIndex],
                "terrain_shadow_column_parity_metrics") != VK_SUCCESS)
        {
            return logShadowRayPassFailure(
                "Failed to allocate terrain shadow-column parity metrics.");
        }

        void* mapped = nullptr;
        if (vmaMapMemory(ctx.memoryAllocator,
                         parityMetricBuffers_[frameIndex].allocation(),
                         &mapped) != VK_SUCCESS)
        {
            return logShadowRayPassFailure(
                "Failed to map terrain shadow-column parity metrics.");
        }
        std::memset(mapped, 0, sizeof(TerrainShadowColumnParityMetrics));
        vmaFlushAllocation(ctx.memoryAllocator,
                           parityMetricBuffers_[frameIndex].allocation(), 0,
                           sizeof(TerrainShadowColumnParityMetrics));
        vmaUnmapMemory(ctx.memoryAllocator,
                       parityMetricBuffers_[frameIndex].allocation());
    }
    return true;
}

bool ShadowRayPass::createDescriptorSets(VulkanContext& ctx)
{
    VkDescriptorPoolSize poolSizes[3]{};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[0].descriptorCount = 3 * kMaxFramesInFlight;  // depth, normal, blue noise
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    poolSizes[1].descriptorCount = 1 * kMaxFramesInFlight;  // shadow output
    poolSizes[2].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    poolSizes[2].descriptorCount = 2 * kMaxFramesInFlight;

    VkDescriptorSetLayout layouts[kMaxFramesInFlight] = {descriptorSetLayout_,
                                                         descriptorSetLayout_};
    const std::span<const VkDescriptorPoolSize> poolSizesView(poolSizes, 3);
    if (!ctx.descriptorAllocator.allocate(ctx.device, poolSizesView, kMaxFramesInFlight,
                                          layouts, kMaxFramesInFlight, descriptorSets_))
    {
        return logShadowRayPassFailure("Failed to allocate shadow ray pass descriptors.");
    }

    return true;
}

void ShadowRayPass::updateDescriptorSet(VulkanContext& ctx, uint32_t frameIndex,
                                        VkImageView gDepth, VkSampler depthSampler,
                                        VkImageView gNormal, VkSampler normalSampler,
                                        BlueNoiseTexture& blueNoise, ShadowBuffer& shadowOut,
                                        VkBuffer tileWorkBuffer,
                                        VkDeviceSize tileWorkBufferSize)
{
    VkDescriptorSet descSet = descriptorSets_[frameIndex];

    // depth sampler info
    VkDescriptorImageInfo depthInfo{};
    depthInfo.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
    depthInfo.imageView = gDepth;
    depthInfo.sampler = depthSampler;

    // normal sampler info
    VkDescriptorImageInfo normalInfo{};
    normalInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    normalInfo.imageView = gNormal;
    normalInfo.sampler = normalSampler;

    // blue noise sampler info
    VkDescriptorImageInfo blueNoiseInfo{};
    blueNoiseInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    blueNoiseInfo.imageView = blueNoise.getImageView();
    blueNoiseInfo.sampler = blueNoise.getSampler();

    // shadow output storage image info
    VkDescriptorImageInfo shadowInfo{};
    shadowInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    shadowInfo.imageView = shadowOut.getCurrentStorageView();
    shadowInfo.sampler = VK_NULL_HANDLE;

    VkDescriptorBufferInfo tileWorkInfo{};
    tileWorkInfo.buffer = tileWorkBuffer;
    tileWorkInfo.offset = 0;
    tileWorkInfo.range = tileWorkBufferSize;

    VkDescriptorBufferInfo parityMetricsInfo{};
    parityMetricsInfo.buffer = parityMetricBuffers_[frameIndex].get();
    parityMetricsInfo.offset = 0;
    parityMetricsInfo.range = sizeof(TerrainShadowColumnParityMetrics);

    VkWriteDescriptorSet writes[6]{};

    // binding 0: depth
    writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[0].dstSet = descSet;
    writes[0].dstBinding = 0;
    writes[0].dstArrayElement = 0;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[0].descriptorCount = 1;
    writes[0].pImageInfo = &depthInfo;

    // binding 1: normal
    writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[1].dstSet = descSet;
    writes[1].dstBinding = 1;
    writes[1].dstArrayElement = 0;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[1].descriptorCount = 1;
    writes[1].pImageInfo = &normalInfo;

    // binding 2: blue noise
    writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[2].dstSet = descSet;
    writes[2].dstBinding = 2;
    writes[2].dstArrayElement = 0;
    writes[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[2].descriptorCount = 1;
    writes[2].pImageInfo = &blueNoiseInfo;

    // binding 3: shadow output
    writes[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[3].dstSet = descSet;
    writes[3].dstBinding = 3;
    writes[3].dstArrayElement = 0;
    writes[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[3].descriptorCount = 1;
    writes[3].pImageInfo = &shadowInfo;

    writes[4].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[4].dstSet = descSet;
    writes[4].dstBinding = 4;
    writes[4].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[4].descriptorCount = 1;
    writes[4].pBufferInfo = &tileWorkInfo;

    writes[5].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[5].dstSet = descSet;
    writes[5].dstBinding = 5;
    writes[5].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[5].descriptorCount = 1;
    writes[5].pBufferInfo = &parityMetricsInfo;

    vkUpdateDescriptorSets(ctx.device, 6, writes, 0, nullptr);
}

void ShadowRayPass::collectParityMetrics(VulkanContext& ctx,
                                         uint32_t frameIndex)
{
    if (terrainShadowColumnMode_ !=
            TerrainShadowColumnMode::ValidateParity ||
        frameIndex >= kMaxFramesInFlight ||
        !parityMetricsSubmitted_[frameIndex])
    {
        return;
    }

    void* mapped = nullptr;
    engine::render::UniqueBuffer& buffer = parityMetricBuffers_[frameIndex];
    if (vmaMapMemory(ctx.memoryAllocator, buffer.allocation(), &mapped) !=
        VK_SUCCESS)
    {
        return;
    }
    vmaInvalidateAllocation(ctx.memoryAllocator, buffer.allocation(), 0,
                            sizeof(TerrainShadowColumnParityMetrics));
    const auto* metrics =
        static_cast<const TerrainShadowColumnParityMetrics*>(mapped);
    terrainShadowColumnParityTotals_.comparedRays += metrics->comparedRays;
    terrainShadowColumnParityTotals_.mismatchedRays +=
        metrics->mismatchedRays;
    terrainShadowColumnParityTotals_.candidateOnlyRays +=
        metrics->candidateOnlyRays;
    terrainShadowColumnParityTotals_.referenceOnlyRays +=
        metrics->referenceOnlyRays;
    if (!terrainShadowColumnParityTotals_.hasFirstMismatch &&
        metrics->firstClaimed != 0)
    {
        terrainShadowColumnParityTotals_.hasFirstMismatch = true;
        terrainShadowColumnParityTotals_.firstCandidateOccluded =
            metrics->firstCandidateOccluded != 0;
        terrainShadowColumnParityTotals_.firstReferenceOccluded =
            metrics->firstReferenceOccluded != 0;
        terrainShadowColumnParityTotals_.firstVolumeIndex =
            metrics->firstVolumeIndex;
        terrainShadowColumnParityTotals_.firstLocalOrigin = glm::vec3(
            std::bit_cast<float>(metrics->firstLocalOriginX),
            std::bit_cast<float>(metrics->firstLocalOriginY),
            std::bit_cast<float>(metrics->firstLocalOriginZ));
        terrainShadowColumnParityTotals_.firstLocalDirection = glm::vec3(
            std::bit_cast<float>(metrics->firstLocalDirectionX),
            std::bit_cast<float>(metrics->firstLocalDirectionY),
            std::bit_cast<float>(metrics->firstLocalDirectionZ));
        terrainShadowColumnParityTotals_.firstMaxT =
            std::bit_cast<float>(metrics->firstMaxT);
        terrainShadowColumnParityTotals_.firstMaxSteps =
            metrics->firstMaxSteps;
    }
    vmaUnmapMemory(ctx.memoryAllocator, buffer.allocation());
    parityMetricsSubmitted_[frameIndex] = false;
}

void ShadowRayPass::prepareFrame(VulkanContext& ctx, VkImageView gDepth, VkSampler depthSampler,
                                 VkImageView gNormal, VkSampler normalSampler,
                                 BlueNoiseTexture& blueNoise, ShadowBuffer& shadowOut,
                                 VkBuffer tileWorkBuffer, VkDeviceSize tileWorkBufferSize,
                                 uint32_t frameIndex)
{
    lastDispatchedSunSampleCount_ = 0;
    collectParityMetrics(ctx, frameIndex);
    parityMetricsNeedReset_[frameIndex] = true;
    updateDescriptorSet(ctx, frameIndex, gDepth, depthSampler, gNormal, normalSampler, blueNoise,
                        shadowOut, tileWorkBuffer, tileWorkBufferSize);
}

void ShadowRayPass::dispatch(VkCommandBuffer cmd, const PushConstants& pc,
                             VkDescriptorSet voxelSet, VkBuffer tileListBuffer,
                             uint32_t frameIndex)
{
    if (!pc.shadowEnabled)
    {
        return;
    }
    lastDispatchedSunSampleCount_ = pc.sunSampleCount;

    if (terrainShadowColumnMode_ ==
            TerrainShadowColumnMode::ValidateParity &&
        parityMetricsNeedReset_[frameIndex])
    {
        vkCmdFillBuffer(cmd, parityMetricBuffers_[frameIndex].get(), 0,
                        sizeof(TerrainShadowColumnParityMetrics), 0u);
        VkBufferMemoryBarrier resetBarrier{};
        resetBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        resetBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        resetBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT |
                                     VK_ACCESS_SHADER_WRITE_BIT;
        resetBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        resetBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        resetBarrier.buffer = parityMetricBuffers_[frameIndex].get();
        resetBarrier.offset = 0;
        resetBarrier.size = sizeof(TerrainShadowColumnParityMetrics);
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0,
                             nullptr, 1, &resetBarrier, 0, nullptr);
        parityMetricsNeedReset_[frameIndex] = false;
    }

    // bind pipeline
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline_.get());

    // bind set 0 (our descriptors) and set 2 (voxel volumes)
    // we skip set 1 (dummy/unused) by binding sets separately at their indices
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout_.get(), 0, 1,
                            &descriptorSets_[frameIndex], 0, nullptr);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout_.get(), 2, 1,
                            &voxelSet, 0, nullptr);

    // the candidate stays opt-in until raw-shadow parity and M1 performance
    // gates pass. per-volume capability still fails closed in VolumeGpu.
    PushConstants resolvedPc = pc;
    resolvedPc.terrainShadowColumnMode =
        static_cast<uint32_t>(terrainShadowColumnMode_);

    // push constants
    vkCmdPushConstants(cmd, pipelineLayout_.get(), VK_SHADER_STAGE_COMPUTE_BIT, 0,
                       sizeof(PushConstants), &resolvedPc);

    vkCmdDispatchIndirect(cmd, tileListBuffer, 0);
    if (terrainShadowColumnMode_ ==
        TerrainShadowColumnMode::ValidateParity)
    {
        parityMetricsSubmitted_[frameIndex] = true;
    }
}

void ShadowRayPass::finishFrame(VkCommandBuffer cmd, uint32_t frameIndex)
{
    if (terrainShadowColumnMode_ !=
            TerrainShadowColumnMode::ValidateParity ||
        frameIndex >= kMaxFramesInFlight ||
        !parityMetricsSubmitted_[frameIndex])
    {
        return;
    }

    VkBufferMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.buffer = parityMetricBuffers_[frameIndex].get();
    barrier.offset = 0;
    barrier.size = sizeof(TerrainShadowColumnParityMetrics);
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_HOST_BIT, 0, 0, nullptr, 1,
                         &barrier, 0, nullptr);
}
