#include "engine/render/passes/LocalShadowBlur.h"

#include <algorithm>
#include <span>
#include <vector>

#include "Core/Logger.h"
#include "engine/render/Utils.h"
#include "engine/render/VulkanContext.h"
#include "Resources/ShaderModule.h"

namespace
{
constexpr const char* kLocalShadowBlurSubsystem = "LocalShadowBlur";

bool logLocalShadowBlurFailure(std::string_view message)
{
    logError(kLocalShadowBlurSubsystem, message);
    return false;
}

bool logLocalShadowBlurVkFailure(std::string_view action, VkResult result)
{
    return logLocalShadowBlurFailure(
        makeLogMessage(action, " failed with VkResult ", static_cast<int>(result), "."));
}
} // namespace

bool LocalShadowBlur::create(VulkanContext& ctx, VkExtent2D extent)
{
    extent_ = extent;
    shaderPath_ = resolveShaderPath(nullptr, "local_shadow_blur.comp.spv");

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

void LocalShadowBlur::destroy(VulkanContext& ctx)
{
    (void)ctx;
    pipeline_.reset();
    pipelineLayout_.reset();
    descriptorSetLayout_ = VK_NULL_HANDLE;
    for (VkDescriptorSet& descriptorSet : descriptorSets_)
    {
        descriptorSet = VK_NULL_HANDLE;
    }
}

void LocalShadowBlur::resize(VulkanContext& ctx, VkExtent2D extent)
{
    (void)ctx;
    extent_ = extent;
}

bool LocalShadowBlur::createPipeline(VulkanContext& ctx)
{
    VkDescriptorSetLayoutBinding bindings[2]{};

    // binding 0: input sampler
    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    // binding 1: output storage image
    bindings[1].binding = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 2;
    layoutInfo.pBindings = bindings;
    if (!ctx.descriptorLayoutCache.get(ctx.device, layoutInfo, descriptorSetLayout_))
    {
        return logLocalShadowBlurVkFailure("vkCreateDescriptorSetLayout for local shadow blur",
                                           VK_ERROR_INITIALIZATION_FAILED);
    }

    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(PushConstants);

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    VkDescriptorSetLayout setLayouts[] = {descriptorSetLayout_};
    pipelineLayoutInfo.pSetLayouts = setLayouts;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkResult result =
        vkCreatePipelineLayout(ctx.device, &pipelineLayoutInfo, nullptr, &pipelineLayout);
    if (result != VK_SUCCESS)
    {
        return logLocalShadowBlurVkFailure("vkCreatePipelineLayout for local shadow blur",
                                           result);
    }
    pipelineLayout_ = engine::render::UniquePipelineLayout(ctx.device, pipelineLayout);

    std::vector<char> shaderCode;
    std::string shaderError;
    if (!tryReadFile(shaderPath_.c_str(), shaderCode, &shaderError))
    {
        logError("Shader", std::string("LocalShadowBlur failed to load shader '") + shaderPath_ +
                               "': " + shaderError);
        return false;
    }

    VkShaderModule shaderModule = tryCreateShaderModule(ctx.device, shaderCode, &shaderError);
    if (shaderModule == VK_NULL_HANDLE)
    {
        logError("Shader", std::string("LocalShadowBlur failed to create shader module for '") +
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
        return logLocalShadowBlurVkFailure("vkCreateComputePipelines for local shadow blur",
                                           result);
    }
    pipeline_ = engine::render::UniquePipeline(ctx.device, pipeline);

    vkDestroyShaderModule(ctx.device, shaderModule, nullptr);
    return true;
}

bool LocalShadowBlur::createDescriptorSets(VulkanContext& ctx)
{
    VkDescriptorPoolSize poolSizes[2]{};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[0].descriptorCount = 1 * kMaxFramesInFlight;
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    poolSizes[1].descriptorCount = 1 * kMaxFramesInFlight;

    VkDescriptorSetLayout layouts[kMaxFramesInFlight] = {descriptorSetLayout_,
                                                         descriptorSetLayout_};
    const std::span<const VkDescriptorPoolSize> poolSizesView(poolSizes, 2);
    if (!ctx.descriptorAllocator.allocate(ctx.device, poolSizesView, kMaxFramesInFlight,
                                          layouts, kMaxFramesInFlight, descriptorSets_))
    {
        return logLocalShadowBlurFailure("Failed to allocate local shadow blur descriptors.");
    }

    return true;
}

void LocalShadowBlur::updateDescriptorSet(VulkanContext& ctx, uint32_t frameIndex,
                                          VkImageView inputView, VkSampler inputSampler,
                                          VkImageView outputStorageView)
{
    VkDescriptorSet descSet = descriptorSets_[frameIndex];

    VkDescriptorImageInfo inputInfo{};
    inputInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    inputInfo.imageView = inputView;
    inputInfo.sampler = inputSampler;

    VkDescriptorImageInfo outputInfo{};
    outputInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    outputInfo.imageView = outputStorageView;
    outputInfo.sampler = VK_NULL_HANDLE;

    VkWriteDescriptorSet writes[2]{};

    writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[0].dstSet = descSet;
    writes[0].dstBinding = 0;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[0].descriptorCount = 1;
    writes[0].pImageInfo = &inputInfo;

    writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[1].dstSet = descSet;
    writes[1].dstBinding = 1;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[1].descriptorCount = 1;
    writes[1].pImageInfo = &outputInfo;

    vkUpdateDescriptorSets(ctx.device, 2, writes, 0, nullptr);
}

void LocalShadowBlur::dispatch(VkCommandBuffer cmd, VulkanContext& ctx,
                               VkImageView inputView, VkSampler inputSampler,
                               VkImageView outputStorageView,
                               VkExtent2D activeExtent, float sigma,
                               uint32_t frameIndex)
{
    updateDescriptorSet(ctx, frameIndex, inputView, inputSampler, outputStorageView);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline_.get());
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout_.get(), 0, 1,
                            &descriptorSets_[frameIndex], 0, nullptr);

    PushConstants pc{};
    const VkExtent2D dispatchExtent =
        activeExtent.width > 0u && activeExtent.height > 0u
            ? VkExtent2D{std::min(activeExtent.width, extent_.width),
                         std::min(activeExtent.height, extent_.height)}
            : extent_;
    pc.resolution = glm::ivec2(static_cast<int>(dispatchExtent.width),
                               static_cast<int>(dispatchExtent.height));
    pc.sigma = sigma;
    pc._pad = 0.0f;

    vkCmdPushConstants(cmd, pipelineLayout_.get(), VK_SHADER_STAGE_COMPUTE_BIT, 0,
                       sizeof(PushConstants), &pc);

    uint32_t groupsX = (dispatchExtent.width + 7u) / 8u;
    uint32_t groupsY = (dispatchExtent.height + 7u) / 8u;
    vkCmdDispatch(cmd, groupsX, groupsY, 1);
}
