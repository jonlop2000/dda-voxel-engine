#include "ShadowTemporalPass.h"

#include "Core/Logger.h"
#include "engine/render/passes/ShadowBuffer.h"
#include "engine/render/Utils.h"
#include "engine/render/VulkanContext.h"
#include "Resources/ShaderModule.h"

#include <vector>

namespace
{
constexpr const char* kShadowTemporalPassSubsystem = "ShadowTemporalPass";

bool logShadowTemporalPassFailure(std::string_view message)
{
    logError(kShadowTemporalPassSubsystem, message);
    return false;
}

bool logShadowTemporalPassVkFailure(std::string_view action, VkResult result)
{
    return logShadowTemporalPassFailure(
        makeLogMessage(action, " failed with VkResult ", static_cast<int>(result), "."));
}
} // namespace

bool ShadowTemporalPass::create(VulkanContext& ctx)
{
    shaderPath_ = resolveShaderPath(nullptr, "shadow_temporal.comp.spv");

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

void ShadowTemporalPass::destroy(VulkanContext& ctx)
{
    if (descriptorPool_)
    {
        vkDestroyDescriptorPool(ctx.device, descriptorPool_, nullptr);
        descriptorPool_ = VK_NULL_HANDLE;
    }
    if (pipeline_)
    {
        vkDestroyPipeline(ctx.device, pipeline_, nullptr);
        pipeline_ = VK_NULL_HANDLE;
    }
    if (pipelineLayout_)
    {
        vkDestroyPipelineLayout(ctx.device, pipelineLayout_, nullptr);
        pipelineLayout_ = VK_NULL_HANDLE;
    }
    if (descriptorSetLayout_)
    {
        vkDestroyDescriptorSetLayout(ctx.device, descriptorSetLayout_, nullptr);
        descriptorSetLayout_ = VK_NULL_HANDLE;
    }
}

bool ShadowTemporalPass::createPipeline(VulkanContext& ctx)
{
    VkDescriptorSetLayoutBinding bindings[6]{};

    for (uint32_t i = 0; i < 5; ++i)
    {
        bindings[i].binding = i;
        bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[i].descriptorCount = 1;
        bindings[i].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    }

    bindings[5].binding = 5;
    bindings[5].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    bindings[5].descriptorCount = 1;
    bindings[5].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 6;
    layoutInfo.pBindings = bindings;

    VkResult result =
        vkCreateDescriptorSetLayout(ctx.device, &layoutInfo, nullptr, &descriptorSetLayout_);
    if (result != VK_SUCCESS)
    {
        return logShadowTemporalPassVkFailure(
            "vkCreateDescriptorSetLayout for shadow temporal resolve", result);
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

    result = vkCreatePipelineLayout(ctx.device, &pipelineLayoutInfo, nullptr, &pipelineLayout_);
    if (result != VK_SUCCESS)
    {
        return logShadowTemporalPassVkFailure("vkCreatePipelineLayout for shadow temporal resolve",
                                              result);
    }

    std::vector<char> shaderCode{};
    std::string shaderError;
    if (!tryReadFile(shaderPath_.c_str(), shaderCode, &shaderError))
    {
        logError("Shader",
                 std::string("ShadowTemporalPass failed to load shader '") + shaderPath_ +
                     "': " + shaderError);
        return false;
    }

    VkShaderModule shaderModule = tryCreateShaderModule(ctx.device, shaderCode, &shaderError);
    if (shaderModule == VK_NULL_HANDLE)
    {
        logError("Shader",
                 std::string("ShadowTemporalPass failed to create shader module for '") +
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
    pipelineInfo.layout = pipelineLayout_;

    result = vkCreateComputePipelines(ctx.device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr,
                                      &pipeline_);
    if (result != VK_SUCCESS)
    {
        vkDestroyShaderModule(ctx.device, shaderModule, nullptr);
        return logShadowTemporalPassVkFailure(
            "vkCreateComputePipelines for shadow temporal resolve", result);
    }

    vkDestroyShaderModule(ctx.device, shaderModule, nullptr);

    return true;
}

bool ShadowTemporalPass::createDescriptorSets(VulkanContext& ctx)
{
    VkDescriptorPoolSize poolSizes[2]{};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[0].descriptorCount = 5 * kMaxFramesInFlight;
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    poolSizes[1].descriptorCount = 1 * kMaxFramesInFlight;

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = 2;
    poolInfo.pPoolSizes = poolSizes;
    poolInfo.maxSets = kMaxFramesInFlight;

    VkResult result = vkCreateDescriptorPool(ctx.device, &poolInfo, nullptr, &descriptorPool_);
    if (result != VK_SUCCESS)
    {
        return logShadowTemporalPassVkFailure("vkCreateDescriptorPool for shadow temporal resolve",
                                              result);
    }

    VkDescriptorSetLayout layouts[kMaxFramesInFlight] = {descriptorSetLayout_,
                                                          descriptorSetLayout_};
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = descriptorPool_;
    allocInfo.descriptorSetCount = kMaxFramesInFlight;
    allocInfo.pSetLayouts = layouts;

    result = vkAllocateDescriptorSets(ctx.device, &allocInfo, descriptorSets_);
    if (result != VK_SUCCESS)
    {
        return logShadowTemporalPassVkFailure(
            "vkAllocateDescriptorSets for shadow temporal resolve", result);
    }

    return true;
}

void ShadowTemporalPass::updateDescriptorSet(VulkanContext& ctx, uint32_t frameIndex,
                                             ShadowBuffer& shadowBuffer,
                                             VkImageView velocityView, VkSampler velocitySampler,
                                             VkImageView depthView, VkSampler depthSampler,
                                             VkImageView depthHistoryView,
                                             VkSampler depthHistorySampler)
{
    VkDescriptorSet descSet = descriptorSets_[frameIndex];

    VkDescriptorImageInfo currentInfo{};
    currentInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    currentInfo.imageView = shadowBuffer.getCurrentImageView();
    currentInfo.sampler = shadowBuffer.getSampler();

    VkDescriptorImageInfo historyInfo{};
    historyInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    historyInfo.imageView = shadowBuffer.getHistoryReadView();
    historyInfo.sampler = shadowBuffer.getSampler();

    VkDescriptorImageInfo velocityInfo{};
    velocityInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    velocityInfo.imageView = velocityView;
    velocityInfo.sampler = velocitySampler;

    VkDescriptorImageInfo depthInfo{};
    depthInfo.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
    depthInfo.imageView = depthView;
    depthInfo.sampler = depthSampler;

    VkDescriptorImageInfo depthHistoryInfo{};
    depthHistoryInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    depthHistoryInfo.imageView = depthHistoryView;
    depthHistoryInfo.sampler = depthHistorySampler;

    VkDescriptorImageInfo outputInfo{};
    outputInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    outputInfo.imageView = shadowBuffer.getHistoryWriteView();
    outputInfo.sampler = VK_NULL_HANDLE;

    VkWriteDescriptorSet writes[6]{};

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
    writes[4].pImageInfo = &depthHistoryInfo;

    writes[5].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[5].dstSet = descSet;
    writes[5].dstBinding = 5;
    writes[5].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[5].descriptorCount = 1;
    writes[5].pImageInfo = &outputInfo;

    vkUpdateDescriptorSets(ctx.device, 6, writes, 0, nullptr);
}

void ShadowTemporalPass::dispatch(VkCommandBuffer cmd, VulkanContext& ctx, const PushConstants& pc,
                                  ShadowBuffer& shadowBuffer,
                                  VkImageView velocityView, VkSampler velocitySampler,
                                  VkImageView depthView, VkSampler depthSampler,
                                  VkImageView depthHistoryView, VkSampler depthHistorySampler,
                                  uint32_t frameIndex)
{
    updateDescriptorSet(ctx, frameIndex, shadowBuffer, velocityView, velocitySampler, depthView,
                        depthSampler, depthHistoryView, depthHistorySampler);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline_);

    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout_, 0, 1,
                            &descriptorSets_[frameIndex], 0, nullptr);

    vkCmdPushConstants(cmd, pipelineLayout_, VK_SHADER_STAGE_COMPUTE_BIT, 0,
                       sizeof(PushConstants), &pc);

    uint32_t groupsX = (pc.resolution.x + 7) / 8;
    uint32_t groupsY = (pc.resolution.y + 7) / 8;
    vkCmdDispatch(cmd, groupsX, groupsY, 1);
}
