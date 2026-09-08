#include "engine/render/passes/ShadowDenoisePass.h"

#include <span>
#include <vector>

#include "Core/Logger.h"
#include "Resources/ShaderModule.h"
#include "engine/render/Utils.h"
#include "engine/render/VulkanContext.h"

namespace
{
constexpr uint32_t kBindingCount = 4;
constexpr const char* kShadowDenoiseSubsystem = "ShadowDenoisePass";

bool logShadowDenoiseFailure(std::string_view message)
{
    logError(kShadowDenoiseSubsystem, message);
    return false;
}

bool logShadowDenoiseVkFailure(std::string_view action, VkResult result)
{
    return logShadowDenoiseFailure(
        makeLogMessage(action, " failed with VkResult ", static_cast<int>(result), "."));
}
} // namespace

bool ShadowDenoisePass::create(VulkanContext& ctx, VkExtent2D extent)
{
    extent_ = extent;
    shaderPath_ = resolveShaderPath(nullptr, "shadow_denoise.comp.spv");

    if (!createOutputResources(ctx))
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

void ShadowDenoisePass::destroyOutputResources(VulkanContext& ctx)
{
    (void)ctx;
    if (sampler_ != VK_NULL_HANDLE)
    {
        sampler_ = VK_NULL_HANDLE;
    }
    outputView_.reset();
    outputImage_.reset();
}

void ShadowDenoisePass::destroy(VulkanContext& ctx)
{
    pipeline_.reset();
    pipelineLayout_.reset();
    descriptorSetLayout_ = VK_NULL_HANDLE;
    for (VkDescriptorSet& descriptorSet : descriptorSets_)
    {
        descriptorSet = VK_NULL_HANDLE;
    }

    destroyOutputResources(ctx);
}

bool ShadowDenoisePass::resize(VulkanContext& ctx, VkExtent2D extent)
{
    if (extent.width == extent_.width && extent.height == extent_.height)
    {
        return true;
    }

    destroyOutputResources(ctx);
    extent_ = extent;
    return createOutputResources(ctx);
}

bool ShadowDenoisePass::createOutputResources(VulkanContext& ctx)
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
    VkResult result = engine::render::createUniqueImage(
        ctx.memoryAllocator, imageInfo, allocationInfo, outputImage_, "shadow_denoise_output");
    if (result != VK_SUCCESS)
    {
        return logShadowDenoiseVkFailure("vmaCreateImage for shadow denoise output", result);
    }

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = outputImage_.get();
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_R16_SFLOAT;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;
    VkImageView rawOutputView = VK_NULL_HANDLE;
    result = vkCreateImageView(ctx.device, &viewInfo, nullptr, &rawOutputView);
    if (result != VK_SUCCESS)
    {
        return logShadowDenoiseVkFailure("vkCreateImageView for shadow denoise output", result);
    }
    outputView_ = engine::render::UniqueImageView(ctx.device, rawOutputView);

    if (!ctx.samplerCache.get(ctx.device, engine::render::SamplerPreset::ClampLinearNearestMip,
                              sampler_))
    {
        return logShadowDenoiseVkFailure("vkCreateSampler for shadow denoise output",
                                         VK_ERROR_INITIALIZATION_FAILED);
    }

    VkCommandBuffer cmd = VK_NULL_HANDLE;
    std::string commandError;
    if (!ctx.tryBeginSingleTimeCommands(&cmd, &commandError))
    {
        return logShadowDenoiseFailure(makeLogMessage(
            "Failed to begin single-time commands for shadow denoise transitions: ", commandError));
    }

    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = outputImage_.get();
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
        return logShadowDenoiseFailure(
            makeLogMessage("Failed to submit shadow denoise transitions: ", commandError));
    }

    return true;
}

bool ShadowDenoisePass::createPipeline(VulkanContext& ctx)
{
    VkDescriptorSetLayoutBinding bindings[kBindingCount]{};

    for (uint32_t i = 0; i < 3; ++i)
    {
        bindings[i].binding = i;
        bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[i].descriptorCount = 1;
        bindings[i].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    }

    bindings[3].binding = 3;
    bindings[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    bindings[3].descriptorCount = 1;
    bindings[3].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = kBindingCount;
    layoutInfo.pBindings = bindings;
    if (!ctx.descriptorLayoutCache.get(ctx.device, layoutInfo, descriptorSetLayout_))
    {
        return logShadowDenoiseVkFailure("vkCreateDescriptorSetLayout for shadow denoise",
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
        return logShadowDenoiseVkFailure("vkCreatePipelineLayout for shadow denoise", result);
    }
    pipelineLayout_ = engine::render::UniquePipelineLayout(ctx.device, pipelineLayout);

    std::vector<char> shaderCode;
    std::string shaderError;
    if (!tryReadFile(shaderPath_.c_str(), shaderCode, &shaderError))
    {
        logError("Shader", std::string("ShadowDenoisePass failed to load shader '") + shaderPath_ +
                               "': " + shaderError);
        return false;
    }

    VkShaderModule shaderModule = tryCreateShaderModule(ctx.device, shaderCode, &shaderError);
    if (shaderModule == VK_NULL_HANDLE)
    {
        logError("Shader", std::string("ShadowDenoisePass failed to create shader module for '") +
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
        return logShadowDenoiseVkFailure("vkCreateComputePipelines for shadow denoise", result);
    }
    pipeline_ = engine::render::UniquePipeline(ctx.device, pipeline);

    vkDestroyShaderModule(ctx.device, shaderModule, nullptr);
    return true;
}

bool ShadowDenoisePass::createDescriptorSets(VulkanContext& ctx)
{
    VkDescriptorPoolSize poolSizes[2]{};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[0].descriptorCount = 3 * kMaxFramesInFlight;
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    poolSizes[1].descriptorCount = 1 * kMaxFramesInFlight;

    VkDescriptorSetLayout layouts[kMaxFramesInFlight] = {descriptorSetLayout_,
                                                         descriptorSetLayout_};
    const std::span<const VkDescriptorPoolSize> poolSizesView(poolSizes, 2);
    if (!ctx.descriptorAllocator.allocate(ctx.device, poolSizesView, kMaxFramesInFlight, layouts,
                                          kMaxFramesInFlight, descriptorSets_))
    {
        return logShadowDenoiseFailure("Failed to allocate shadow denoise descriptors.");
    }

    return true;
}

void ShadowDenoisePass::updateDescriptorSet(VulkanContext& ctx, uint32_t frameIndex,
                                            VkImageView inputView, VkSampler inputSampler,
                                            VkImageView depthView, VkSampler depthSampler,
                                            VkImageView normalView, VkSampler normalSampler)
{
    VkDescriptorSet descSet = descriptorSets_[frameIndex];

    VkDescriptorImageInfo inputInfo{};
    inputInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    inputInfo.imageView = inputView;
    inputInfo.sampler = inputSampler;

    VkDescriptorImageInfo depthInfo{};
    depthInfo.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
    depthInfo.imageView = depthView;
    depthInfo.sampler = depthSampler;

    VkDescriptorImageInfo normalInfo{};
    normalInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    normalInfo.imageView = normalView;
    normalInfo.sampler = normalSampler;

    VkDescriptorImageInfo outputInfo{};
    outputInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    outputInfo.imageView = outputView_.get();
    outputInfo.sampler = VK_NULL_HANDLE;

    VkWriteDescriptorSet writes[kBindingCount]{};

    writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[0].dstSet = descSet;
    writes[0].dstBinding = 0;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[0].descriptorCount = 1;
    writes[0].pImageInfo = &inputInfo;

    writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[1].dstSet = descSet;
    writes[1].dstBinding = 1;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[1].descriptorCount = 1;
    writes[1].pImageInfo = &depthInfo;

    writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[2].dstSet = descSet;
    writes[2].dstBinding = 2;
    writes[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[2].descriptorCount = 1;
    writes[2].pImageInfo = &normalInfo;

    writes[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[3].dstSet = descSet;
    writes[3].dstBinding = 3;
    writes[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[3].descriptorCount = 1;
    writes[3].pImageInfo = &outputInfo;

    vkUpdateDescriptorSets(ctx.device, kBindingCount, writes, 0, nullptr);
}

void ShadowDenoisePass::dispatch(VkCommandBuffer cmd, VulkanContext& ctx, VkImageView inputView,
                                 VkSampler inputSampler, VkImageView depthView,
                                 VkSampler depthSampler, VkImageView normalView,
                                 VkSampler normalSampler, uint32_t radius, float depthSigma,
                                 float valueSigma, float normalPower, uint32_t frameIndex)
{
    updateDescriptorSet(ctx, frameIndex, inputView, inputSampler, depthView, depthSampler,
                        normalView, normalSampler);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline_.get());
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout_.get(), 0, 1,
                            &descriptorSets_[frameIndex], 0, nullptr);

    PushConstants pc{};
    pc.resolution = glm::ivec2(static_cast<int>(extent_.width), static_cast<int>(extent_.height));
    pc.depthSigma = depthSigma;
    pc.valueSigma = valueSigma;
    pc.normalPower = normalPower;
    pc.radius = radius;
    pc._pad[0] = 0.0f;
    pc._pad[1] = 0.0f;
    pc._pad[2] = 0.0f;

    vkCmdPushConstants(cmd, pipelineLayout_.get(), VK_SHADER_STAGE_COMPUTE_BIT, 0,
                       sizeof(PushConstants), &pc);

    const uint32_t groupsX = (extent_.width + 7u) / 8u;
    const uint32_t groupsY = (extent_.height + 7u) / 8u;
    vkCmdDispatch(cmd, groupsX, groupsY, 1);
}
