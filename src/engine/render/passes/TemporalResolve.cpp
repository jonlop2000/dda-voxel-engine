#include "engine/render/passes/TemporalResolve.h"

#include "Core/Logger.h"
#include "Resources/ShaderModule.h"
#include "engine/render/Utils.h"
#include "engine/render/VulkanContext.h"

#include <span>
#include <vector>

namespace
{
constexpr uint32_t kBindingCount = 7;
constexpr uint32_t kFlagUseNormalReject = 1u;
constexpr uint32_t kFlagUseNeighborhood = 2u;
constexpr uint32_t kFlagResetHistory = 4u;
constexpr uint32_t kFlagUseSpatialFilter = 8u;
constexpr uint32_t kFlagReconstructCurrent = 16u;
constexpr uint32_t kFlagFilterReconstructedCurrent = 32u;

constexpr const char* kTemporalResolveSubsystem = "TemporalResolve";

bool logTemporalResolveFailure(std::string_view message)
{
    logError(kTemporalResolveSubsystem, message);
    return false;
}

bool logTemporalResolveVkFailure(std::string_view action, VkResult result)
{
    return logTemporalResolveFailure(
        makeLogMessage(action, " failed with VkResult ", static_cast<int>(result), "."));
}
} // namespace

bool TemporalResolve::create(VulkanContext& ctx, VkExtent2D extent,
                             const TemporalResolveConfig& config)
{
    config_ = config;
    extent_ = extent;
    historyIndex_ = 0;

    shaderPath_ = resolveShaderPath(nullptr, "temporal_resolve.comp.spv");

    if (!createHistoryImages(ctx))
    {
        destroy(ctx);
        return false;
    }

    if (!createPipeline(ctx))
    {
        destroy(ctx);
        return false;
    }

    if (!createDescriptorSets(ctx))
    {
        destroy(ctx);
        return false;
    }

    return true;
}

void TemporalResolve::destroyResources(VulkanContext& ctx)
{
    (void)ctx;
    if (sampler_ != VK_NULL_HANDLE)
    {
        sampler_ = VK_NULL_HANDLE;
    }

    for (size_t i = 0; i < historyViews_.size(); ++i)
    {
        historyViews_[i].reset();
        historyImages_[i].reset();
    }
}

void TemporalResolve::destroy(VulkanContext& ctx)
{
    pipeline_.reset();
    pipelineLayout_.reset();
    descriptorSetLayout_ = VK_NULL_HANDLE;
    for (VkDescriptorSet& descriptorSet : descriptorSets_)
    {
        descriptorSet = VK_NULL_HANDLE;
    }

    destroyResources(ctx);
}

bool TemporalResolve::resize(VulkanContext& ctx, VkExtent2D extent)
{
    if (extent.width == extent_.width && extent.height == extent_.height)
    {
        return true;
    }

    destroyResources(ctx);
    extent_ = extent;
    historyIndex_ = 0;
    if (!createHistoryImages(ctx))
    {
        destroy(ctx);
        return false;
    }
    return true;
}

bool TemporalResolve::createHistoryImages(VulkanContext& ctx)
{
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = config_.format;
    imageInfo.extent = {extent_.width, extent_.height, 1};
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage =
        VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = config_.format;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    for (size_t i = 0; i < historyImages_.size(); ++i)
    {
        VmaAllocationCreateInfo allocationInfo{};
        allocationInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
        constexpr const char* kHistoryNames[] = {"temporal_resolve_history_0",
                                                 "temporal_resolve_history_1"};
        VkResult result = engine::render::createUniqueImage(
            ctx.memoryAllocator, imageInfo, allocationInfo, historyImages_[i], kHistoryNames[i]);
        if (result != VK_SUCCESS)
        {
            return logTemporalResolveVkFailure("vmaCreateImage for temporal resolve history",
                                               result);
        }

        viewInfo.image = historyImages_[i].get();
        VkImageView rawHistoryView = VK_NULL_HANDLE;
        result = vkCreateImageView(ctx.device, &viewInfo, nullptr, &rawHistoryView);
        if (result != VK_SUCCESS)
        {
            return logTemporalResolveVkFailure("vkCreateImageView for temporal resolve history",
                                               result);
        }
        historyViews_[i] = engine::render::UniqueImageView(ctx.device, rawHistoryView);
    }

    if (!ctx.samplerCache.get(ctx.device, engine::render::SamplerPreset::ClampLinearNearestMip,
                              sampler_))
    {
        return logTemporalResolveVkFailure("vkCreateSampler for temporal resolve history",
                                           VK_ERROR_INITIALIZATION_FAILED);
    }

    VkCommandBuffer cmd = VK_NULL_HANDLE;
    std::string commandError;
    if (!ctx.tryBeginSingleTimeCommands(&cmd, &commandError))
    {
        return logTemporalResolveFailure(
            makeLogMessage("Failed to begin single-time commands for temporal "
                           "resolve history transitions: ",
                           commandError));
    }

    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT;

    for (size_t i = 0; i < historyImages_.size(); ++i)
    {
        barrier.image = historyImages_[i].get();
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1,
                             &barrier);
    }

    if (!ctx.tryEndSingleTimeCommands(cmd, &commandError))
    {
        return logTemporalResolveFailure(makeLogMessage(
            "Failed to submit temporal resolve history transitions: ", commandError));
    }
    return true;
}

bool TemporalResolve::createPipeline(VulkanContext& ctx)
{
    VkDescriptorSetLayoutBinding bindings[kBindingCount]{};

    for (uint32_t i = 0; i < 6; ++i)
    {
        bindings[i].binding = i;
        bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[i].descriptorCount = 1;
        bindings[i].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    }

    bindings[6].binding = 6;
    bindings[6].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    bindings[6].descriptorCount = 1;
    bindings[6].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = kBindingCount;
    layoutInfo.pBindings = bindings;

    if (!ctx.descriptorLayoutCache.get(ctx.device, layoutInfo, descriptorSetLayout_))
    {
        return logTemporalResolveVkFailure("vkCreateDescriptorSetLayout for temporal resolve",
                                           VK_ERROR_INITIALIZATION_FAILED);
    }

    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(PushConstants);

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    VkDescriptorSetLayout descriptorSetLayoutRaw = descriptorSetLayout_;
    pipelineLayoutInfo.pSetLayouts = &descriptorSetLayoutRaw;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkResult result =
        vkCreatePipelineLayout(ctx.device, &pipelineLayoutInfo, nullptr, &pipelineLayout);
    if (result != VK_SUCCESS)
    {
        return logTemporalResolveVkFailure("vkCreatePipelineLayout for temporal resolve", result);
    }
    pipelineLayout_ = engine::render::UniquePipelineLayout(ctx.device, pipelineLayout);

    std::vector<char> shaderCode{};
    std::string shaderError;
    if (!tryReadFile(shaderPath_.c_str(), shaderCode, &shaderError))
    {
        logError("Shader", std::string("TemporalResolve failed to load shader '") + shaderPath_ +
                               "': " + shaderError);
        return false;
    }

    VkShaderModule shaderModule = tryCreateShaderModule(ctx.device, shaderCode, &shaderError);
    if (shaderModule == VK_NULL_HANDLE)
    {
        logError("Shader", std::string("TemporalResolve failed to create shader module for '") +
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
        return logTemporalResolveVkFailure("vkCreateComputePipelines for temporal resolve", result);
    }
    pipeline_ = engine::render::UniquePipeline(ctx.device, pipeline);

    vkDestroyShaderModule(ctx.device, shaderModule, nullptr);

    return true;
}

bool TemporalResolve::createDescriptorSets(VulkanContext& ctx)
{
    VkDescriptorPoolSize poolSizes[2]{};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[0].descriptorCount = 6 * kMaxFramesInFlight;
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    poolSizes[1].descriptorCount = 1 * kMaxFramesInFlight;

    VkDescriptorSetLayout layouts[kMaxFramesInFlight] = {descriptorSetLayout_,
                                                         descriptorSetLayout_};
    const std::span<const VkDescriptorPoolSize> poolSizesView(poolSizes, 2);
    if (!ctx.descriptorAllocator.allocate(ctx.device, poolSizesView, kMaxFramesInFlight, layouts,
                                          kMaxFramesInFlight, descriptorSets_))
    {
        return logTemporalResolveFailure("Failed to allocate temporal resolve descriptors.");
    }

    return true;
}

void TemporalResolve::updateDescriptorSet(VulkanContext& ctx, uint32_t frameIndex,
                                          VkImageView currentView, VkSampler currentSampler,
                                          VkImageView velocityView, VkSampler velocitySampler,
                                          VkImageView depthView, VkSampler depthSampler,
                                          VkImageView normalView, VkSampler normalSampler,
                                          VkImageView depthHistoryView,
                                          VkSampler depthHistorySampler, VkImageView outputView)
{
    VkDescriptorSet descSet = descriptorSets_[frameIndex];

    VkDescriptorImageInfo currentInfo{};
    currentInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    currentInfo.imageView = currentView;
    currentInfo.sampler = currentSampler;

    VkDescriptorImageInfo historyInfo{};
    historyInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    historyInfo.imageView = historyViews_[historyIndex_].get();
    historyInfo.sampler = sampler_;

    VkDescriptorImageInfo velocityInfo{};
    velocityInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    velocityInfo.imageView = velocityView;
    velocityInfo.sampler = velocitySampler;

    VkDescriptorImageInfo depthInfo{};
    depthInfo.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
    depthInfo.imageView = depthView;
    depthInfo.sampler = depthSampler;

    VkDescriptorImageInfo normalInfo{};
    normalInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    normalInfo.imageView = normalView;
    normalInfo.sampler = normalSampler;

    VkDescriptorImageInfo depthHistoryInfo{};
    depthHistoryInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    depthHistoryInfo.imageView = depthHistoryView;
    depthHistoryInfo.sampler = depthHistorySampler;

    VkDescriptorImageInfo outputInfo{};
    outputInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    outputInfo.imageView = outputView;
    outputInfo.sampler = VK_NULL_HANDLE;

    VkWriteDescriptorSet writes[kBindingCount]{};

    writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[0].dstSet = descSet;
    writes[0].dstBinding = 0;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[0].descriptorCount = 1;
    writes[0].pImageInfo = &currentInfo;

    writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[1].dstSet = descSet;
    writes[1].dstBinding = 1;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[1].descriptorCount = 1;
    writes[1].pImageInfo = &historyInfo;

    writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[2].dstSet = descSet;
    writes[2].dstBinding = 2;
    writes[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[2].descriptorCount = 1;
    writes[2].pImageInfo = &velocityInfo;

    writes[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[3].dstSet = descSet;
    writes[3].dstBinding = 3;
    writes[3].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[3].descriptorCount = 1;
    writes[3].pImageInfo = &depthInfo;

    writes[4].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[4].dstSet = descSet;
    writes[4].dstBinding = 4;
    writes[4].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[4].descriptorCount = 1;
    writes[4].pImageInfo = &normalInfo;

    writes[5].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[5].dstSet = descSet;
    writes[5].dstBinding = 5;
    writes[5].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[5].descriptorCount = 1;
    writes[5].pImageInfo = &depthHistoryInfo;

    writes[6].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[6].dstSet = descSet;
    writes[6].dstBinding = 6;
    writes[6].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[6].descriptorCount = 1;
    writes[6].pImageInfo = &outputInfo;

    vkUpdateDescriptorSets(ctx.device, kBindingCount, writes, 0, nullptr);
}

void TemporalResolve::resolve(VkCommandBuffer cmd, VulkanContext& ctx, VkImageView currentView,
                              VkSampler currentSampler, VkExtent2D currentExtent,
                              VkImageView velocityView,
                              VkSampler velocitySampler, VkImageView depthView,
                              VkSampler depthSampler, VkImageView normalView,
                              VkSampler normalSampler, VkImageView depthHistoryView,
                              VkSampler depthHistorySampler, bool resetHistory, int debugMode,
                              uint32_t frameIndex)
{
    const uint32_t readIndex = static_cast<uint32_t>(historyIndex_);
    const uint32_t writeIndex = 1u - readIndex;

    updateDescriptorSet(ctx, frameIndex, currentView, currentSampler, velocityView, velocitySampler,
                        depthView, depthSampler, normalView, normalSampler, depthHistoryView,
                        depthHistorySampler, historyViews_[writeIndex].get());

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline_.get());
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout_.get(), 0, 1,
                            &descriptorSets_[frameIndex], 0, nullptr);

    PushConstants pc{};
    pc.resolution = glm::ivec2(static_cast<int>(extent_.width), static_cast<int>(extent_.height));
    const VkExtent2D resolvedCurrentExtent =
        currentExtent.width > 0 && currentExtent.height > 0 ? currentExtent : extent_;
    pc.currentResolution =
        glm::ivec2(static_cast<int>(resolvedCurrentExtent.width),
                   static_cast<int>(resolvedCurrentExtent.height));
    pc.blendAlpha = config_.blendAlpha;
    pc.blendAlphaMax = config_.blendAlphaMax;
    pc.depthRejectThreshold = config_.depthRejectThreshold;
    pc.normalRejectDot = config_.normalRejectDot;
    pc.clampSharpness = config_.clampSharpness;
    pc.spatialDepthSigma = config_.spatialDepthSigma;
    pc.spatialValueSigma = config_.spatialValueSigma;
    pc.spatialNormalPower = config_.spatialNormalPower;
    pc.flags = 0;
    if (config_.useNormalReject)
    {
        pc.flags |= kFlagUseNormalReject;
    }
    if (config_.useNeighborhoodClamp)
    {
        pc.flags |= kFlagUseNeighborhood;
    }
    if (config_.useSpatialFilter && config_.spatialFilterRadius > 0)
    {
        pc.flags |= kFlagUseSpatialFilter;
        if (config_.spatialFilterReconstructedCurrent)
        {
            pc.flags |= kFlagFilterReconstructedCurrent;
        }
    }
    if (resolvedCurrentExtent.width != extent_.width ||
        resolvedCurrentExtent.height != extent_.height)
    {
        pc.flags |= kFlagReconstructCurrent;
    }
    if (resetHistory)
    {
        pc.flags |= kFlagResetHistory;
    }
    pc.debugMode = static_cast<uint32_t>(debugMode);
    pc.spatialRadius = config_.spatialFilterRadius;
    pc._pad0 = 0.0f;

    vkCmdPushConstants(cmd, pipelineLayout_.get(), VK_SHADER_STAGE_COMPUTE_BIT, 0,
                       sizeof(PushConstants), &pc);

    uint32_t groupsX = (extent_.width + 7) / 8;
    uint32_t groupsY = (extent_.height + 7) / 8;
    vkCmdDispatch(cmd, groupsX, groupsY, 1);

    historyIndex_ = static_cast<int>(writeIndex);
}
