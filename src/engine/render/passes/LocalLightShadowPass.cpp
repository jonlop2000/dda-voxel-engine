#include "engine/render/passes/LocalLightShadowPass.h"

#include <span>
#include <vector>

#include "Core/Logger.h"
#include "engine/render/passes/BlueNoiseTexture.h"
#include "engine/render/passes/LocalLightShadowBuffer.h"
#include "engine/render/Utils.h"
#include "engine/render/VulkanContext.h"
#include "Resources/LightsBuffer.h"
#include "Resources/ShaderModule.h"

namespace
{
constexpr const char* kLocalLightShadowPassSubsystem = "LocalLightShadowPass";

bool logLocalLightShadowPassFailure(std::string_view message)
{
    logError(kLocalLightShadowPassSubsystem, message);
    return false;
}

bool logLocalLightShadowPassVkFailure(std::string_view action, VkResult result)
{
    return logLocalLightShadowPassFailure(
        makeLogMessage(action, " failed with VkResult ", static_cast<int>(result), "."));
}
} // namespace

bool LocalLightShadowPass::create(VulkanContext& ctx, VkExtent2D extent,
                                  VkDescriptorSetLayout voxelSetLayout)
{
    extent_ = extent;
    voxelSetLayout_ = voxelSetLayout;
    shaderPath_ = resolveShaderPath(nullptr, "local_light_shadow_ray.comp.spv");

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

void LocalLightShadowPass::destroy(VulkanContext& ctx)
{
    (void)ctx;
    pipeline_.reset();
    pipelineLayout_.reset();
    descriptorSetLayout_ = VK_NULL_HANDLE;
    lightSetLayout_ = VK_NULL_HANDLE;
    for (VkDescriptorSet& descriptorSet : descriptorSets_)
    {
        descriptorSet = VK_NULL_HANDLE;
    }
    for (VkDescriptorSet& lightSet : lightSets_)
    {
        lightSet = VK_NULL_HANDLE;
    }
}

void LocalLightShadowPass::resize(VulkanContext& ctx, VkExtent2D extent)
{
    (void)ctx;
    extent_ = extent;
}

bool LocalLightShadowPass::createPipeline(VulkanContext& ctx)
{
    VkDescriptorSetLayoutBinding bindings[5]{};

    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    bindings[1].binding = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    bindings[2].binding = 2;
    bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[2].descriptorCount = 1;
    bindings[2].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    bindings[3].binding = 3;
    bindings[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    bindings[3].descriptorCount = 1;
    bindings[3].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

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
        return logLocalLightShadowPassVkFailure(
            "vkCreateDescriptorSetLayout for local-light shadow inputs",
            VK_ERROR_INITIALIZATION_FAILED);
    }

    VkDescriptorSetLayoutBinding lightBinding{};
    lightBinding.binding = 0;
    lightBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    lightBinding.descriptorCount = 1;
    lightBinding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    VkDescriptorSetLayoutCreateInfo lightLayoutInfo{};
    lightLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    lightLayoutInfo.bindingCount = 1;
    lightLayoutInfo.pBindings = &lightBinding;
    if (!ctx.descriptorLayoutCache.get(ctx.device, lightLayoutInfo, lightSetLayout_))
    {
        return logLocalLightShadowPassVkFailure(
            "vkCreateDescriptorSetLayout for local-light shadow light buffer",
            VK_ERROR_INITIALIZATION_FAILED);
    }

    VkDescriptorSetLayout setLayouts[3] = {
        descriptorSetLayout_,  // set 0
        lightSetLayout_,       // set 1
        voxelSetLayout_        // set 2
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
        return logLocalLightShadowPassVkFailure(
            "vkCreatePipelineLayout for local-light shadow tracing", result);
    }
    pipelineLayout_ = engine::render::UniquePipelineLayout(ctx.device, pipelineLayout);

    std::vector<char> shaderCode;
    std::string shaderError;
    if (!tryReadFile(shaderPath_.c_str(), shaderCode, &shaderError))
    {
        logError("Shader",
                 std::string("LocalLightShadowPass failed to load shader '") + shaderPath_ +
                     "': " + shaderError);
        return false;
    }

    VkShaderModule shaderModule = tryCreateShaderModule(ctx.device, shaderCode, &shaderError);
    if (shaderModule == VK_NULL_HANDLE)
    {
        logError("Shader", std::string("LocalLightShadowPass failed to create shader module for '") +
                               shaderPath_ + "': " + shaderError);
        return false;
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
        vkCreateComputePipelines(ctx.device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline);
    if (result != VK_SUCCESS)
    {
        vkDestroyShaderModule(ctx.device, shaderModule, nullptr);
        return logLocalLightShadowPassVkFailure(
            "vkCreateComputePipelines for local-light shadow tracing", result);
    }
    pipeline_ = engine::render::UniquePipeline(ctx.device, pipeline);

    vkDestroyShaderModule(ctx.device, shaderModule, nullptr);
    return true;
}

bool LocalLightShadowPass::createDescriptorSets(VulkanContext& ctx)
{
    VkDescriptorPoolSize poolSizes[3]{};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[0].descriptorCount = 3 * kMaxFramesInFlight;
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    poolSizes[1].descriptorCount = 1 * kMaxFramesInFlight;
    poolSizes[2].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    poolSizes[2].descriptorCount = 2 * kMaxFramesInFlight;

    VkDescriptorSetLayout set0Layouts[kMaxFramesInFlight] = {descriptorSetLayout_,
                                                             descriptorSetLayout_};
    const std::span<const VkDescriptorPoolSize> poolSizesView(poolSizes, 3);
    constexpr uint32_t kMaxDescriptorSets = kMaxFramesInFlight * 2;
    if (!ctx.descriptorAllocator.allocate(ctx.device, poolSizesView, kMaxDescriptorSets,
                                          set0Layouts, kMaxFramesInFlight, descriptorSets_))
    {
        return logLocalLightShadowPassFailure(
            "Failed to allocate local-light shadow input descriptors.");
    }

    VkDescriptorSetLayout set1Layouts[kMaxFramesInFlight] = {lightSetLayout_,
                                                             lightSetLayout_};
    if (!ctx.descriptorAllocator.allocate(ctx.device, poolSizesView, kMaxDescriptorSets,
                                          set1Layouts, kMaxFramesInFlight, lightSets_))
    {
        return logLocalLightShadowPassFailure(
            "Failed to allocate local-light shadow light-buffer descriptors.");
    }

    return true;
}

void LocalLightShadowPass::updateDescriptorSet(VulkanContext& ctx, uint32_t frameIndex,
                                               VkImageView gDepth, VkSampler depthSampler,
                                               VkImageView gNormal, VkSampler normalSampler,
                                               BlueNoiseTexture& blueNoise,
                                               const LightsBuffer& lights,
                                               LocalLightShadowBuffer& shadowOut,
                                               VkBuffer tileWorkBuffer,
                                               VkDeviceSize tileWorkBufferSize)
{
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

    VkDescriptorImageInfo outInfo{};
    outInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    outInfo.imageView = shadowOut.getCurrentStorageView();
    outInfo.sampler = VK_NULL_HANDLE;

    VkDescriptorBufferInfo tileWorkInfo{};
    tileWorkInfo.buffer = tileWorkBuffer;
    tileWorkInfo.offset = 0;
    tileWorkInfo.range = tileWorkBufferSize;

    VkWriteDescriptorSet writes[5]{};

    writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[0].dstSet = descriptorSets_[frameIndex];
    writes[0].dstBinding = 0;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[0].descriptorCount = 1;
    writes[0].pImageInfo = &depthInfo;

    writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[1].dstSet = descriptorSets_[frameIndex];
    writes[1].dstBinding = 1;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[1].descriptorCount = 1;
    writes[1].pImageInfo = &normalInfo;

    writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[2].dstSet = descriptorSets_[frameIndex];
    writes[2].dstBinding = 2;
    writes[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[2].descriptorCount = 1;
    writes[2].pImageInfo = &blueNoiseInfo;

    writes[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[3].dstSet = descriptorSets_[frameIndex];
    writes[3].dstBinding = 3;
    writes[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[3].descriptorCount = 1;
    writes[3].pImageInfo = &outInfo;

    writes[4].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[4].dstSet = descriptorSets_[frameIndex];
    writes[4].dstBinding = 4;
    writes[4].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[4].descriptorCount = 1;
    writes[4].pBufferInfo = &tileWorkInfo;

    vkUpdateDescriptorSets(ctx.device, 5, writes, 0, nullptr);

    VkDescriptorBufferInfo lightInfo{};
    lightInfo.buffer = lights.buffer;
    lightInfo.offset = 0;
    lightInfo.range = sizeof(AreaLightGpu) * lights.maxLights;

    VkWriteDescriptorSet lightWrite{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    lightWrite.dstSet = lightSets_[frameIndex];
    lightWrite.dstBinding = 0;
    lightWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    lightWrite.descriptorCount = 1;
    lightWrite.pBufferInfo = &lightInfo;
    vkUpdateDescriptorSets(ctx.device, 1, &lightWrite, 0, nullptr);
}

void LocalLightShadowPass::prepareFrame(VulkanContext& ctx, VkImageView gDepth,
                                        VkSampler depthSampler, VkImageView gNormal,
                                        VkSampler normalSampler, BlueNoiseTexture& blueNoise,
                                        const LightsBuffer& lights,
                                        LocalLightShadowBuffer& shadowOut,
                                        VkBuffer tileWorkBuffer,
                                        VkDeviceSize tileWorkBufferSize,
                                        uint32_t frameIndex)
{
    updateDescriptorSet(ctx, frameIndex, gDepth, depthSampler, gNormal, normalSampler, blueNoise,
                        lights, shadowOut, tileWorkBuffer, tileWorkBufferSize);
}

void LocalLightShadowPass::dispatch(VkCommandBuffer cmd, const PushConstants& pc,
                                    VkDescriptorSet voxelSet, VkBuffer tileListBuffer,
                                    uint32_t frameIndex)
{
    if (!pc.shadowEnabled)
    {
        return;
    }

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline_.get());
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout_.get(), 0, 1,
                            &descriptorSets_[frameIndex], 0, nullptr);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout_.get(), 1, 1,
                            &lightSets_[frameIndex], 0, nullptr);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout_.get(), 2, 1,
                            &voxelSet, 0, nullptr);

    vkCmdPushConstants(cmd, pipelineLayout_.get(), VK_SHADER_STAGE_COMPUTE_BIT, 0,
                       sizeof(PushConstants), &pc);

    vkCmdDispatchIndirect(cmd, tileListBuffer, 0);
}
