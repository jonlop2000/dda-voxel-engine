#include "engine/render/passes/SpatialUpscalePass.h"

#include <algorithm>
#include <array>
#include <span>
#include <string_view>

#include <glm/vec2.hpp>

#include "Core/Logger.h"
#include "Resources/GpuImage.h"
#include "Resources/ShaderModule.h"
#include "engine/render/FrameContext.h"
#include "engine/render/Utils.h"
#include "engine/render/VulkanContext.h"
#include "engine/render/gpu/SamplerCache.h"
#include "engine/render/passes/PassCreateInfo.h"

namespace engine::render
{
namespace
{

struct SpatialUpscalePushConstants
{
    glm::vec2 inputTexelSize{1.0f};
    float sharpness = 0.0f;
    float edgeStrength = 0.0f;
    int mode = 0;
};

constexpr const char* kSubsystem = "SpatialUpscalePass";
constexpr const char* kRationale =
    "The spatial upscaler is required to present reduced-resolution scene targets while "
    "keeping the swapchain and UI at native resolution.";

[[noreturn]] void fail(std::string_view detail)
{
    logAndExit(kSubsystem, std::string("Required pass failure: ") + std::string(detail) + " " +
                               kRationale);
}

std::vector<char> loadShader(const std::string& path, std::string_view stage)
{
    std::vector<char> code;
    std::string error;
    if (!tryReadFile(path.c_str(), code, &error))
    {
        fail(std::string("Failed to read ") + std::string(stage) + " shader '" + path +
             "': " + error);
    }
    return code;
}

VkShaderModule createShaderModule(VkDevice device, const std::vector<char>& code,
                                  const std::string& path, std::string_view stage)
{
    std::string error;
    VkShaderModule module = tryCreateShaderModule(device, code, &error);
    if (module == VK_NULL_HANDLE)
    {
        fail(std::string("Failed to create ") + std::string(stage) + " shader module for '" +
             path + "': " +
             (error.empty() ? std::string("vkCreateShaderModule failed.") : error));
    }
    return module;
}

UniqueRenderPass createRenderPass(VkDevice device, VkFormat format)
{
    VkAttachmentDescription color{};
    color.format = format;
    color.samples = VK_SAMPLE_COUNT_1_BIT;
    color.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    color.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    color.initialLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    color.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkAttachmentReference colorReference{};
    colorReference.attachment = 0;
    colorReference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorReference;

    std::array<VkSubpassDependency, 2> dependencies{};
    dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[0].dstSubpass = 0;
    dependencies[0].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependencies[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dependencies[1].srcSubpass = 0;
    dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependencies[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dependencies[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    VkRenderPassCreateInfo createInfo{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    createInfo.attachmentCount = 1;
    createInfo.pAttachments = &color;
    createInfo.subpassCount = 1;
    createInfo.pSubpasses = &subpass;
    createInfo.dependencyCount = static_cast<uint32_t>(dependencies.size());
    createInfo.pDependencies = dependencies.data();

    VkRenderPass renderPass = VK_NULL_HANDLE;
    if (vkCreateRenderPass(device, &createInfo, nullptr, &renderPass) != VK_SUCCESS)
    {
        fail("vkCreateRenderPass failed for native-resolution upscale targets.");
    }
    return UniqueRenderPass(device, renderPass);
}

}  // namespace

void SpatialUpscalePass::setShaderPaths(const std::string& fullscreenVert,
                                        const std::string& fragment)
{
    fullscreenVertPath_ = fullscreenVert;
    fragmentPath_ = fragment;
}

void SpatialUpscalePass::create(VulkanContext& context, const PassCreateInfo& createInfo,
                                VkExtent2D presentationExtent)
{
    if (createInfo.commands == nullptr || createInfo.extent.width == 0 ||
        createInfo.extent.height == 0 || presentationExtent.width == 0 ||
        presentationExtent.height == 0)
    {
        fail("Creation requires commands plus valid internal and presentation extents.");
    }

    inputExtent_ = createInfo.extent;
    outputExtent_ = presentationExtent;
    if (!active())
    {
        return;
    }

    VkDescriptorSetLayoutBinding inputBinding{};
    inputBinding.binding = 0;
    inputBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    inputBinding.descriptorCount = 1;
    inputBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo setLayoutInfo{
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    setLayoutInfo.bindingCount = 1;
    setLayoutInfo.pBindings = &inputBinding;
    if (!context.descriptorLayoutCache.get(context.device, setLayoutInfo,
                                           descriptorSetLayout_))
    {
        fail("Failed to create the sampled-input descriptor layout.");
    }

    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    pushConstantRange.size = sizeof(SpatialUpscalePushConstants);

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{
        VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &descriptorSetLayout_;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    if (vkCreatePipelineLayout(context.device, &pipelineLayoutInfo, nullptr,
                               &pipelineLayout) != VK_SUCCESS)
    {
        fail("vkCreatePipelineLayout failed.");
    }
    pipelineLayout_ = UniquePipelineLayout(context.device, pipelineLayout);

    const VkDescriptorPoolSize poolSize{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                        kMaxFramesInFlight};
    std::array<VkDescriptorSetLayout, kMaxFramesInFlight> layouts{};
    layouts.fill(descriptorSetLayout_);
    descriptorSets_.resize(kMaxFramesInFlight);
    if (!context.descriptorAllocator.allocate(
            context.device, std::span<const VkDescriptorPoolSize>(&poolSize, 1),
            kMaxFramesInFlight, layouts.data(), kMaxFramesInFlight,
            descriptorSets_.data()))
    {
        fail("Failed to allocate sampled-input descriptors.");
    }

    if (!context.samplerCache.get(context.device, SamplerPreset::ClampLinear, sampler_))
    {
        fail("Failed to acquire the clamp-linear sampler.");
    }

    renderPass_ = createRenderPass(context.device, format_);
    createTargets(context, createInfo);
    createPipeline(context);
}

void SpatialUpscalePass::destroy(VulkanContext& context)
{
    (void)context;
    pipeline_.reset();
    for (Target& target : targets_)
    {
        target.framebuffer.reset();
    }
    renderPass_.reset();
    for (Target& target : targets_)
    {
        target.view.reset();
        target.image.reset();
    }
    descriptorSets_.clear();
    pipelineLayout_.reset();
    descriptorSetLayout_ = VK_NULL_HANDLE;
    sampler_ = VK_NULL_HANDLE;
    inputExtent_ = {};
    outputExtent_ = {};
}

void SpatialUpscalePass::updateInput(VulkanContext& context, uint32_t frameIndex,
                                     VkImageView inputView)
{
    if (frameIndex >= descriptorSets_.size() || inputView == VK_NULL_HANDLE ||
        sampler_ == VK_NULL_HANDLE)
    {
        return;
    }

    VkDescriptorImageInfo imageInfo{};
    imageInfo.sampler = sampler_;
    imageInfo.imageView = inputView;
    imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    write.dstSet = descriptorSets_[frameIndex];
    write.dstBinding = 0;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo = &imageInfo;
    vkUpdateDescriptorSets(context.device, 1, &write, 0, nullptr);
}

void SpatialUpscalePass::record(VulkanContext& context, const FrameContext& frame,
                                const RenderResolutionSettings& settings)
{
    (void)context;
    if (!active() || frame.frameIndex >= descriptorSets_.size() ||
        frame.frameIndex >= targets_.size())
    {
        return;
    }

    const RenderResolutionSettings sanitized =
        sanitizeRenderResolutionSettings(settings);
    const Target& target = targets_[frame.frameIndex];

    VkRenderPassBeginInfo renderPassInfo{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    renderPassInfo.renderPass = renderPass_.get();
    renderPassInfo.framebuffer = target.framebuffer.get();
    renderPassInfo.renderArea.extent = outputExtent_;
    vkCmdBeginRenderPass(frame.cmd, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(frame.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_.get());
    vkCmdBindDescriptorSets(frame.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            pipelineLayout_.get(), 0, 1,
                            &descriptorSets_[frame.frameIndex], 0, nullptr);

    SpatialUpscalePushConstants pushConstants{};
    pushConstants.inputTexelSize =
        glm::vec2(1.0f / static_cast<float>(inputExtent_.width),
                  1.0f / static_cast<float>(inputExtent_.height));
    pushConstants.sharpness = sanitized.sharpness;
    pushConstants.edgeStrength = sanitized.edgeStrength;
    pushConstants.mode = static_cast<int>(sanitized.upscaleMode);
    vkCmdPushConstants(frame.cmd, pipelineLayout_.get(), VK_SHADER_STAGE_FRAGMENT_BIT,
                       0, sizeof(pushConstants), &pushConstants);

    VkViewport viewport{};
    viewport.width = static_cast<float>(outputExtent_.width);
    viewport.height = static_cast<float>(outputExtent_.height);
    viewport.maxDepth = 1.0f;
    const VkRect2D scissor{{0, 0}, outputExtent_};
    vkCmdSetViewport(frame.cmd, 0, 1, &viewport);
    vkCmdSetScissor(frame.cmd, 0, 1, &scissor);
    vkCmdDraw(frame.cmd, 3, 1, 0, 0);
    vkCmdEndRenderPass(frame.cmd);
}

VkImageView SpatialUpscalePass::outputView(uint32_t frameIndex) const
{
    return frameIndex < targets_.size() ? targets_[frameIndex].view.get()
                                        : VK_NULL_HANDLE;
}

void SpatialUpscalePass::createTargets(VulkanContext& context,
                                       const PassCreateInfo& createInfo)
{
    for (size_t index = 0; index < targets_.size(); ++index)
    {
        Target& target = targets_[index];
        VkImageCreateInfo imageInfo{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.format = format_;
        imageInfo.extent = {outputExtent_.width, outputExtent_.height, 1};
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = 1;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

        VmaAllocationCreateInfo allocationInfo{};
        allocationInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
        const std::string allocationName =
            "spatial_upscale_output_" + std::to_string(index);
        if (createUniqueImage(context.memoryAllocator, imageInfo, allocationInfo,
                              target.image, allocationName.c_str()) != VK_SUCCESS)
        {
            fail("Failed to allocate a native-resolution upscale target.");
        }
        target.view = UniqueImageView(
            context.device,
            createImageView(context.device, target.image.get(), format_,
                            VK_IMAGE_ASPECT_COLOR_BIT));

        transitionImageLayout(context, *createInfo.commands, target.image.get(),
                              VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                              VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

        VkImageView attachment = target.view.get();
        VkFramebufferCreateInfo framebufferInfo{
            VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
        framebufferInfo.renderPass = renderPass_.get();
        framebufferInfo.attachmentCount = 1;
        framebufferInfo.pAttachments = &attachment;
        framebufferInfo.width = outputExtent_.width;
        framebufferInfo.height = outputExtent_.height;
        framebufferInfo.layers = 1;
        VkFramebuffer framebuffer = VK_NULL_HANDLE;
        if (vkCreateFramebuffer(context.device, &framebufferInfo, nullptr,
                                &framebuffer) != VK_SUCCESS)
        {
            fail("vkCreateFramebuffer failed for an upscale target.");
        }
        target.framebuffer = UniqueFramebuffer(context.device, framebuffer);
    }
}

void SpatialUpscalePass::createPipeline(VulkanContext& context)
{
    const std::vector<char> vertexCode = loadShader(fullscreenVertPath_, "vertex");
    const std::vector<char> fragmentCode = loadShader(fragmentPath_, "fragment");
    const VkShaderModule vertexModule = createShaderModule(
        context.device, vertexCode, fullscreenVertPath_, "vertex");
    const VkShaderModule fragmentModule = createShaderModule(
        context.device, fragmentCode, fragmentPath_, "fragment");

    std::array<VkPipelineShaderStageCreateInfo, 2> stages{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vertexModule;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fragmentModule;
    stages[1].pName = "main";

    VkPipelineVertexInputStateCreateInfo vertexInput{
        VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    VkPipelineInputAssemblyStateCreateInfo inputAssembly{
        VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo viewportState{
        VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo rasterization{
        VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rasterization.polygonMode = VK_POLYGON_MODE_FILL;
    rasterization.cullMode = VK_CULL_MODE_NONE;
    rasterization.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterization.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo multisample{
        VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineColorBlendAttachmentState blendAttachment{};
    blendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT |
                                     VK_COLOR_COMPONENT_G_BIT |
                                     VK_COLOR_COMPONENT_B_BIT |
                                     VK_COLOR_COMPONENT_A_BIT;
    VkPipelineColorBlendStateCreateInfo blend{
        VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    blend.attachmentCount = 1;
    blend.pAttachments = &blendAttachment;
    const std::array<VkDynamicState, 2> dynamicStates = {
        VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic{
        VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dynamic.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamic.pDynamicStates = dynamicStates.data();

    VkGraphicsPipelineCreateInfo pipelineInfo{
        VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    pipelineInfo.stageCount = static_cast<uint32_t>(stages.size());
    pipelineInfo.pStages = stages.data();
    pipelineInfo.pVertexInputState = &vertexInput;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterization;
    pipelineInfo.pMultisampleState = &multisample;
    pipelineInfo.pColorBlendState = &blend;
    pipelineInfo.pDynamicState = &dynamic;
    pipelineInfo.layout = pipelineLayout_.get();
    pipelineInfo.renderPass = renderPass_.get();

    VkPipeline pipeline = VK_NULL_HANDLE;
    if (vkCreateGraphicsPipelines(context.device, VK_NULL_HANDLE, 1, &pipelineInfo,
                                  nullptr, &pipeline) != VK_SUCCESS)
    {
        vkDestroyShaderModule(context.device, fragmentModule, nullptr);
        vkDestroyShaderModule(context.device, vertexModule, nullptr);
        fail("vkCreateGraphicsPipelines failed.");
    }
    pipeline_ = UniquePipeline(context.device, pipeline);

    vkDestroyShaderModule(context.device, fragmentModule, nullptr);
    vkDestroyShaderModule(context.device, vertexModule, nullptr);
}

}  // namespace engine::render
