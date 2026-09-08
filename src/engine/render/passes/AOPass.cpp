#include "engine/render/passes/AOPass.h"

#include "Core/Logger.h"
#include "Resources/ShaderModule.h"
#include "engine/render/FrameSync.h"
#include "engine/render/Utils.h"
#include "engine/render/VulkanContext.h"
#include "engine/render/passes/BlueNoiseTexture.h"

#include <span>
#include <vector>

namespace
{
constexpr const char* kAOPassSubsystem = "AOPass";

bool logAOPassFailure(std::string_view message)
{
    logError(kAOPassSubsystem, message);
    return false;
}

bool logAOPassVkFailure(std::string_view action, VkResult result)
{
    return logAOPassFailure(
        makeLogMessage(action, " failed with VkResult ", static_cast<int>(result), "."));
}
} // namespace

bool AOPass::create(VulkanContext& ctx, VkExtent2D extent, VkDescriptorSetLayout voxelSetLayout)
{
    extent_ = extent;
    voxelSetLayout_ = voxelSetLayout;
    shaderPath_ = resolveShaderPath(nullptr, "ao_ray.comp.spv");

    if (!createAOImage(ctx))
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

void AOPass::destroyResources(VulkanContext& ctx)
{
    (void)ctx;
    aoSampler_ = VK_NULL_HANDLE;
    aoView_.reset();
    aoStorageView_.reset();
    aoImage_.reset();
}

void AOPass::destroy(VulkanContext& ctx)
{
    pipeline_.reset();
    pipelineLayout_.reset();
    descriptorSetLayout_ = VK_NULL_HANDLE;
    dummySetLayout_ = VK_NULL_HANDLE;
    for (VkDescriptorSet& descriptorSet : descriptorSets_)
    {
        descriptorSet = VK_NULL_HANDLE;
    }

    destroyResources(ctx);
}

bool AOPass::resize(VulkanContext& ctx, VkExtent2D extent)
{
    if (extent.width == extent_.width && extent.height == extent_.height)
    {
        return true;
    }

    destroyResources(ctx);
    extent_ = extent;
    if (!createAOImage(ctx))
    {
        destroy(ctx);
        return false;
    }
    return true;
}

bool AOPass::createAOImage(VulkanContext& ctx)
{
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = VK_FORMAT_R16_SFLOAT;
    imageInfo.extent = {extent_.width, extent_.height, 1};
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage =
        VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VmaAllocationCreateInfo allocationInfo{};
    allocationInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    VkResult result = engine::render::createUniqueImage(ctx.memoryAllocator, imageInfo,
                                                        allocationInfo, aoImage_, "ao_target");
    if (result != VK_SUCCESS)
    {
        return logAOPassVkFailure("VMA image allocation for AO target", result);
    }

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = aoImage_.get();
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_R16_SFLOAT;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    VkImageView sampledView = VK_NULL_HANDLE;
    result = vkCreateImageView(ctx.device, &viewInfo, nullptr, &sampledView);
    if (result != VK_SUCCESS)
    {
        return logAOPassVkFailure("vkCreateImageView for AO sampled view", result);
    }
    aoView_ = engine::render::UniqueImageView(ctx.device, sampledView);

    VkImageView storageView = VK_NULL_HANDLE;
    result = vkCreateImageView(ctx.device, &viewInfo, nullptr, &storageView);
    if (result != VK_SUCCESS)
    {
        return logAOPassVkFailure("vkCreateImageView for AO storage view", result);
    }
    aoStorageView_ = engine::render::UniqueImageView(ctx.device, storageView);

    if (!ctx.samplerCache.get(ctx.device, engine::render::SamplerPreset::ClampLinearNearestMip,
                              aoSampler_))
    {
        return logAOPassVkFailure("vkCreateSampler for AO target", VK_ERROR_INITIALIZATION_FAILED);
    }

    VkCommandBuffer cmd = VK_NULL_HANDLE;
    std::string commandError;
    if (!ctx.tryBeginSingleTimeCommands(&cmd, &commandError))
    {
        return logAOPassFailure(makeLogMessage(
            "Failed to begin single-time commands for AO image transition: ", commandError));
    }

    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = aoImage_.get();
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT;

    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1,
                         &barrier);

    if (!ctx.tryEndSingleTimeCommands(cmd, &commandError))
    {
        return logAOPassFailure(
            makeLogMessage("Failed to submit AO image transition commands: ", commandError));
    }
    return true;
}

bool AOPass::createPipeline(VulkanContext& ctx)
{
    VkDescriptorSetLayoutBinding bindings[5]{};

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

    // binding 3: ao output (storage image)
    bindings[3].binding = 3;
    bindings[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    bindings[3].descriptorCount = 1;
    bindings[3].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    // binding 4: indirect dispatch header + compact non-sky tile coordinates.
    bindings[4].binding = 4;
    bindings[4].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[4].descriptorCount = 1;
    bindings[4].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 5;
    layoutInfo.pBindings = bindings;

    if (!ctx.descriptorLayoutCache.get(ctx.device, layoutInfo, descriptorSetLayout_))
    {
        return logAOPassVkFailure("vkCreateDescriptorSetLayout for AO inputs",
                                  VK_ERROR_INITIALIZATION_FAILED);
    }

    VkDescriptorSetLayoutCreateInfo dummyLayoutInfo{};
    dummyLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    dummyLayoutInfo.bindingCount = 0;
    dummyLayoutInfo.pBindings = nullptr;
    if (!ctx.descriptorLayoutCache.get(ctx.device, dummyLayoutInfo, dummySetLayout_))
    {
        return logAOPassVkFailure("vkCreateDescriptorSetLayout for AO dummy set",
                                  VK_ERROR_INITIALIZATION_FAILED);
    }

    VkDescriptorSetLayout setLayouts[3] = {
        descriptorSetLayout_, // set 0
        dummySetLayout_,      // set 1
        voxelSetLayout_       // set 2 (voxel volumes)
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
        return logAOPassVkFailure("vkCreatePipelineLayout for AO compute pass", result);
    }
    pipelineLayout_ = engine::render::UniquePipelineLayout(ctx.device, pipelineLayout);

    std::vector<char> shaderCode{};
    std::string shaderError;
    if (!tryReadFile(shaderPath_.c_str(), shaderCode, &shaderError))
    {
        logError("Shader",
                 std::string("AOPass failed to load shader '") + shaderPath_ + "': " + shaderError);
        return false;
    }

    VkShaderModule shaderModule = tryCreateShaderModule(ctx.device, shaderCode, &shaderError);
    if (shaderModule == VK_NULL_HANDLE)
    {
        logError("Shader", std::string("AOPass failed to create shader module for '") +
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
        return logAOPassVkFailure("vkCreateComputePipelines for AO compute pass", result);
    }
    pipeline_ = engine::render::UniquePipeline(ctx.device, pipeline);
    vkDestroyShaderModule(ctx.device, shaderModule, nullptr);

    return true;
}

bool AOPass::createDescriptorSets(VulkanContext& ctx)
{
    VkDescriptorPoolSize poolSizes[3]{};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[0].descriptorCount = 3 * kMaxFramesInFlight;
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    poolSizes[1].descriptorCount = 1 * kMaxFramesInFlight;
    poolSizes[2].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    poolSizes[2].descriptorCount = 1 * kMaxFramesInFlight;

    VkDescriptorSetLayout layouts[kMaxFramesInFlight] = {descriptorSetLayout_,
                                                         descriptorSetLayout_};
    const std::span<const VkDescriptorPoolSize> poolSizesView(poolSizes, 3);
    if (!ctx.descriptorAllocator.allocate(ctx.device, poolSizesView, kMaxFramesInFlight, layouts,
                                          kMaxFramesInFlight, descriptorSets_))
    {
        return logAOPassFailure("Failed to allocate AO descriptors.");
    }

    return true;
}

void AOPass::updateDescriptorSet(VulkanContext& ctx, uint32_t frameIndex, VkImageView gDepth,
                                 VkSampler depthSampler, VkImageView gNormal,
                                 VkSampler normalSampler, BlueNoiseTexture& blueNoise,
                                 VkBuffer tileListBuffer,
                                 VkDeviceSize tileListBufferSize)
{
    VkDescriptorSet descSet = descriptorSets_[frameIndex];

    VkDescriptorImageInfo depthInfo{};
    depthInfo.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
    depthInfo.imageView = gDepth;
    depthInfo.sampler = depthSampler;

    VkDescriptorImageInfo normalInfo{};
    normalInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    normalInfo.imageView = gNormal;
    normalInfo.sampler = normalSampler;

    VkDescriptorImageInfo blueNoiseInfo{};
    blueNoiseInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    blueNoiseInfo.imageView = blueNoise.getImageView();
    blueNoiseInfo.sampler = blueNoise.getSampler();

    VkDescriptorImageInfo aoInfo{};
    aoInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    aoInfo.imageView = aoStorageView_.get();
    aoInfo.sampler = VK_NULL_HANDLE;

    VkDescriptorBufferInfo tileListInfo{};
    tileListInfo.buffer = tileListBuffer;
    tileListInfo.offset = 0;
    tileListInfo.range = tileListBufferSize;

    VkWriteDescriptorSet writes[5]{};

    writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[0].dstSet = descSet;
    writes[0].dstBinding = 0;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[0].descriptorCount = 1;
    writes[0].pImageInfo = &depthInfo;

    writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[1].dstSet = descSet;
    writes[1].dstBinding = 1;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[1].descriptorCount = 1;
    writes[1].pImageInfo = &normalInfo;

    writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[2].dstSet = descSet;
    writes[2].dstBinding = 2;
    writes[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[2].descriptorCount = 1;
    writes[2].pImageInfo = &blueNoiseInfo;

    writes[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[3].dstSet = descSet;
    writes[3].dstBinding = 3;
    writes[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[3].descriptorCount = 1;
    writes[3].pImageInfo = &aoInfo;

    writes[4].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[4].dstSet = descSet;
    writes[4].dstBinding = 4;
    writes[4].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[4].descriptorCount = 1;
    writes[4].pBufferInfo = &tileListInfo;

    vkUpdateDescriptorSets(ctx.device, 5, writes, 0, nullptr);
}

void AOPass::prepareFrame(VulkanContext& ctx, VkImageView gDepth, VkSampler depthSampler,
                          VkImageView gNormal, VkSampler normalSampler,
                          BlueNoiseTexture& blueNoise, VkBuffer tileListBuffer,
                          VkDeviceSize tileListBufferSize, uint32_t frameIndex)
{
    updateDescriptorSet(ctx, frameIndex, gDepth, depthSampler, gNormal, normalSampler,
                        blueNoise, tileListBuffer, tileListBufferSize);
}

void AOPass::dispatch(VkCommandBuffer cmd, const PushConstants& pc, VkDescriptorSet voxelSet,
                      VkBuffer tileListBuffer, uint32_t frameIndex)
{
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline_.get());

    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout_.get(), 0, 1,
                            &descriptorSets_[frameIndex], 0, nullptr);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout_.get(), 2, 1,
                            &voxelSet, 0, nullptr);

    vkCmdPushConstants(cmd, pipelineLayout_.get(), VK_SHADER_STAGE_COMPUTE_BIT, 0,
                       sizeof(PushConstants), &pc);

    vkCmdDispatchIndirect(cmd, tileListBuffer, 0);
}
