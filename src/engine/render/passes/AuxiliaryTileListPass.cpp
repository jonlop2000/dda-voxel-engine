#include "engine/render/passes/AuxiliaryTileListPass.h"

#include "Core/Logger.h"
#include "Resources/ShaderModule.h"
#include "engine/render/Utils.h"
#include "engine/render/VulkanContext.h"

#include <span>
#include <vector>

namespace
{
constexpr const char* kSubsystem = "AuxiliaryTileListPass";
constexpr VkDeviceSize kIndirectHeaderSize = 16;

bool logFailure(std::string_view message)
{
    logError(kSubsystem, message);
    return false;
}

bool logVkFailure(std::string_view action, VkResult result)
{
    return logFailure(
        makeLogMessage(action, " failed with VkResult ", static_cast<int>(result), "."));
}
} // namespace

bool AuxiliaryTileListPass::create(VulkanContext& ctx, VkExtent2D extent)
{
    extent_ = extent;
    shaderPath_ = resolveShaderPath(nullptr, "auxiliary_tile_list.comp.spv");

    if (!createBuffers(ctx) || !createPipeline(ctx) || !createDescriptorSets(ctx))
    {
        destroy(ctx);
        return false;
    }
    return true;
}

void AuxiliaryTileListPass::destroyBuffers()
{
    for (engine::render::UniqueBuffer& buffer : buffers_)
    {
        buffer.reset();
    }
    bufferSize_ = 0;
}

void AuxiliaryTileListPass::destroy(VulkanContext& ctx)
{
    (void)ctx;
    pipeline_.reset();
    pipelineLayout_.reset();
    descriptorSetLayout_ = VK_NULL_HANDLE;
    for (VkDescriptorSet& descriptorSet : descriptorSets_)
    {
        descriptorSet = VK_NULL_HANDLE;
    }
    destroyBuffers();
}

bool AuxiliaryTileListPass::resize(VulkanContext& ctx, VkExtent2D extent)
{
    if (extent.width == extent_.width && extent.height == extent_.height)
    {
        return true;
    }

    destroyBuffers();
    extent_ = extent;
    return createBuffers(ctx);
}

bool AuxiliaryTileListPass::createBuffers(VulkanContext& ctx)
{
    constexpr VkDeviceSize kTileCoordinateSize = sizeof(uint32_t) * 2;
    const uint64_t groupsX = (static_cast<uint64_t>(extent_.width) + 7u) / 8u;
    const uint64_t groupsY = (static_cast<uint64_t>(extent_.height) + 7u) / 8u;
    const uint64_t tileCount = groupsX * groupsY;
    bufferSize_ =
        kIndirectHeaderSize + static_cast<VkDeviceSize>(tileCount) * kTileCoordinateSize;

    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = bufferSize_;
    bufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                       VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT |
                       VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocationInfo{};
    allocationInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    constexpr const char* kAllocationNames[kMaxFramesInFlight] = {
        "auxiliary_tile_list_0",
        "auxiliary_tile_list_1",
    };
    for (uint32_t i = 0; i < kMaxFramesInFlight; ++i)
    {
        const VkResult result =
            engine::render::createUniqueBuffer(ctx.memoryAllocator, bufferInfo,
                                               allocationInfo, buffers_[i],
                                               kAllocationNames[i]);
        if (result != VK_SUCCESS)
        {
            return logVkFailure("VMA allocation for auxiliary tile-list buffer", result);
        }
    }
    return true;
}

bool AuxiliaryTileListPass::createPipeline(VulkanContext& ctx)
{
    VkDescriptorSetLayoutBinding bindings[2]{};
    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    bindings[1].binding = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    VkDescriptorSetLayoutCreateInfo descriptorLayoutInfo{};
    descriptorLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    descriptorLayoutInfo.bindingCount = 2;
    descriptorLayoutInfo.pBindings = bindings;
    if (!ctx.descriptorLayoutCache.get(ctx.device, descriptorLayoutInfo,
                                       descriptorSetLayout_))
    {
        return logVkFailure("vkCreateDescriptorSetLayout for auxiliary tile list",
                            VK_ERROR_INITIALIZATION_FAILED);
    }

    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(PushConstants);

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &descriptorSetLayout_;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkResult result =
        vkCreatePipelineLayout(ctx.device, &pipelineLayoutInfo, nullptr, &pipelineLayout);
    if (result != VK_SUCCESS)
    {
        return logVkFailure("vkCreatePipelineLayout for auxiliary tile list", result);
    }
    pipelineLayout_ = engine::render::UniquePipelineLayout(ctx.device, pipelineLayout);

    std::vector<char> shaderCode{};
    std::string shaderError;
    if (!tryReadFile(shaderPath_.c_str(), shaderCode, &shaderError))
    {
        return logFailure(
            makeLogMessage("Failed to load shader '", shaderPath_, "': ", shaderError));
    }

    VkShaderModule shaderModule =
        tryCreateShaderModule(ctx.device, shaderCode, &shaderError);
    if (shaderModule == VK_NULL_HANDLE)
    {
        return logFailure(makeLogMessage("Failed to create shader module for '", shaderPath_,
                                         "': ", shaderError));
    }

    VkPipelineShaderStageCreateInfo stageInfo{};
    stageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    stageInfo.module = shaderModule;
    stageInfo.pName = "main";

    VkComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.stage = stageInfo;
    pipelineInfo.layout = pipelineLayout_.get();

    VkPipeline pipeline = VK_NULL_HANDLE;
    result =
        vkCreateComputePipelines(ctx.device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr,
                                 &pipeline);
    vkDestroyShaderModule(ctx.device, shaderModule, nullptr);
    if (result != VK_SUCCESS)
    {
        return logVkFailure("vkCreateComputePipelines for auxiliary tile list", result);
    }
    pipeline_ = engine::render::UniquePipeline(ctx.device, pipeline);
    return true;
}

bool AuxiliaryTileListPass::createDescriptorSets(VulkanContext& ctx)
{
    VkDescriptorPoolSize poolSizes[2]{};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[0].descriptorCount = kDescriptorSetCount;
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    poolSizes[1].descriptorCount = kDescriptorSetCount;

    VkDescriptorSetLayout layouts[kDescriptorSetCount]{};
    for (VkDescriptorSetLayout& layout : layouts)
    {
        layout = descriptorSetLayout_;
    }
    const std::span<const VkDescriptorPoolSize> poolSizesView(poolSizes, 2);
    if (!ctx.descriptorAllocator.allocate(ctx.device, poolSizesView,
                                          kDescriptorSetCount, layouts,
                                          kDescriptorSetCount,
                                          descriptorSets_))
    {
        return logFailure("Failed to allocate auxiliary tile-list descriptors.");
    }
    return true;
}

void AuxiliaryTileListPass::prepareFrame(VulkanContext& ctx, VkImageView depthView,
                                         VkSampler depthSampler, Consumer consumer,
                                         uint32_t frameIndex)
{
    if (frameIndex >= kMaxFramesInFlight || consumer == Consumer::Count)
    {
        return;
    }

    VkDescriptorImageInfo depthInfo{};
    depthInfo.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
    depthInfo.imageView = depthView;
    depthInfo.sampler = depthSampler;

    VkDescriptorBufferInfo bufferInfo{};
    bufferInfo.buffer = buffers_[frameIndex].get();
    bufferInfo.offset = 0;
    bufferInfo.range = bufferSize_;

    VkWriteDescriptorSet writes[2]{};
    writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[0].dstSet = descriptorSets_[descriptorIndex(consumer, frameIndex)];
    writes[0].dstBinding = 0;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[0].descriptorCount = 1;
    writes[0].pImageInfo = &depthInfo;
    writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[1].dstSet = descriptorSets_[descriptorIndex(consumer, frameIndex)];
    writes[1].dstBinding = 1;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[1].descriptorCount = 1;
    writes[1].pBufferInfo = &bufferInfo;
    vkUpdateDescriptorSets(ctx.device, 2, writes, 0, nullptr);
}

void AuxiliaryTileListPass::dispatch(VkCommandBuffer cmd, const glm::ivec2& resolution,
                                     Consumer consumer, uint32_t frameIndex)
{
    if (frameIndex >= kMaxFramesInFlight || consumer == Consumer::Count)
    {
        return;
    }

    const VkBuffer tileBuffer = buffers_[frameIndex].get();
    VkBufferMemoryBarrier recycleBarrier{};
    recycleBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    recycleBarrier.srcAccessMask =
        VK_ACCESS_INDIRECT_COMMAND_READ_BIT | VK_ACCESS_SHADER_READ_BIT;
    recycleBarrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    recycleBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    recycleBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    recycleBarrier.buffer = tileBuffer;
    recycleBarrier.offset = 0;
    recycleBarrier.size = bufferSize_;
    vkCmdPipelineBarrier(cmd,
                         VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT |
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 1,
                         &recycleBarrier, 0, nullptr);

    vkCmdFillBuffer(cmd, tileBuffer, 0, kIndirectHeaderSize, 0u);

    VkBufferMemoryBarrier clearBarrier{};
    clearBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    clearBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    clearBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    clearBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    clearBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    clearBarrier.buffer = tileBuffer;
    clearBarrier.offset = 0;
    clearBarrier.size = bufferSize_;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 1,
                         &clearBarrier, 0, nullptr);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline_.get());
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                            pipelineLayout_.get(), 0, 1,
                            &descriptorSets_[descriptorIndex(consumer, frameIndex)], 0,
                            nullptr);

    PushConstants pc{};
    pc.resolution = resolution;
    vkCmdPushConstants(cmd, pipelineLayout_.get(), VK_SHADER_STAGE_COMPUTE_BIT, 0,
                       sizeof(pc), &pc);

    const uint32_t groupsX = (static_cast<uint32_t>(resolution.x) + 7u) / 8u;
    const uint32_t groupsY = (static_cast<uint32_t>(resolution.y) + 7u) / 8u;
    vkCmdDispatch(cmd, groupsX, groupsY, 1);

    VkBufferMemoryBarrier buildBarrier{};
    buildBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    buildBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    buildBarrier.dstAccessMask =
        VK_ACCESS_INDIRECT_COMMAND_READ_BIT | VK_ACCESS_SHADER_READ_BIT;
    buildBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    buildBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    buildBarrier.buffer = tileBuffer;
    buildBarrier.offset = 0;
    buildBarrier.size = bufferSize_;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT |
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         0, 0, nullptr, 1, &buildBarrier, 0, nullptr);
}
