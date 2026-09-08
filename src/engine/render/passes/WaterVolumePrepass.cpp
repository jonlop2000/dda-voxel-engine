#include "engine/render/passes/WaterVolumePrepass.h"

#include <array>
#include <cstring>
#include <iterator>
#include <span>
#include <string_view>

#include "engine/render/passes/GBufferPass.h"
#include "engine/render/passes/PassCreateInfo.h"
#include "engine/render/Commands.h"
#include "engine/render/FrameContext.h"
#include "engine/render/Utils.h"
#include "engine/render/VulkanContext.h"
#include "Resources/GpuBuffer.h"
#include "Resources/GpuImage.h"
#include "Resources/ShaderModule.h"

namespace
{
constexpr const char* kWaterVolumePrepassSubsystem = "WaterVolumePrepass";
constexpr const char* kWaterVolumePrepassRationale =
    "WaterVolumePrepass is required for Water V2 analytic water-distance production.";

[[noreturn]] void failWaterVolumePrepass(std::string_view detail)
{
    logAndExit(kWaterVolumePrepassSubsystem,
               std::string("Required pass failure: ") + std::string(detail) + " " +
                   kWaterVolumePrepassRationale);
}

std::vector<char> loadPrepassShaderOrFatal(const char* path, std::string_view stage)
{
    std::vector<char> code;
    std::string error;
    if (!tryReadFile(path, code, &error))
    {
        failWaterVolumePrepass(std::string("Failed to read ") + std::string(stage) +
                               " shader '" + path + "': " + error);
    }
    return code;
}

VkShaderModule createPrepassShaderModuleOrFatal(VkDevice device,
                                                const std::vector<char>& code,
                                                const char* path,
                                                std::string_view stage)
{
    std::string error;
    VkShaderModule module = tryCreateShaderModule(device, code, &error);
    if (module == VK_NULL_HANDLE)
    {
        failWaterVolumePrepass(std::string("Failed to create ") + std::string(stage) +
                               " shader module for '" + path + "': " +
                               (error.empty() ? "vkCreateShaderModule failed." : error));
    }
    return module;
}

VkPipeline createFullscreenPipeline(VulkanContext& ctx, VkRenderPass renderPass,
                                    VkPipelineLayout pipelineLayout,
                                    const char* vertPath, const char* fragPath)
{
    const std::vector<char> vertCode = loadPrepassShaderOrFatal(vertPath, "vertex");
    const std::vector<char> fragCode = loadPrepassShaderOrFatal(fragPath, "fragment");

    VkShaderModule vertModule =
        createPrepassShaderModuleOrFatal(ctx.device, vertCode, vertPath, "vertex");
    VkShaderModule fragModule =
        createPrepassShaderModuleOrFatal(ctx.device, fragCode, fragPath, "fragment");

    VkPipelineShaderStageCreateInfo vertStage{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
    vertStage.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertStage.module = vertModule;
    vertStage.pName = "main";

    VkPipelineShaderStageCreateInfo fragStage{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
    fragStage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragStage.module = fragModule;
    fragStage.pName = "main";

    VkPipelineShaderStageCreateInfo stages[] = {vertStage, fragStage};

    VkPipelineVertexInputStateCreateInfo vertexInput{
        VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};

    VkPipelineInputAssemblyStateCreateInfo inputAsm{
        VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    inputAsm.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAsm.primitiveRestartEnable = VK_FALSE;

    VkPipelineViewportStateCreateInfo viewportState{
        VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo raster{
        VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    raster.depthClampEnable = VK_FALSE;
    raster.rasterizerDiscardEnable = VK_FALSE;
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.lineWidth = 1.0f;
    raster.cullMode = VK_CULL_MODE_NONE;
    raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    raster.depthBiasEnable = VK_FALSE;

    VkPipelineMultisampleStateCreateInfo ms{
        VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState blendAttachment{};
    blendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT;
    blendAttachment.blendEnable = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo blend{
        VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    blend.attachmentCount = 1;
    blend.pAttachments = &blendAttachment;

    VkDynamicState dynStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dynamic.dynamicStateCount = 2;
    dynamic.pDynamicStates = dynStates;

    VkGraphicsPipelineCreateInfo pci{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    pci.stageCount = 2;
    pci.pStages = stages;
    pci.pVertexInputState = &vertexInput;
    pci.pInputAssemblyState = &inputAsm;
    pci.pViewportState = &viewportState;
    pci.pRasterizationState = &raster;
    pci.pMultisampleState = &ms;
    pci.pColorBlendState = &blend;
    pci.pDynamicState = &dynamic;
    pci.layout = pipelineLayout;
    pci.renderPass = renderPass;
    pci.subpass = 0;

    VkPipeline pipeline = VK_NULL_HANDLE;
    if (vkCreateGraphicsPipelines(ctx.device, VK_NULL_HANDLE, 1, &pci, nullptr, &pipeline) !=
        VK_SUCCESS)
    {
        failWaterVolumePrepass("vkCreateGraphicsPipelines failed.");
    }

    vkDestroyShaderModule(ctx.device, fragModule, nullptr);
    vkDestroyShaderModule(ctx.device, vertModule, nullptr);

    return pipeline;
}
} // namespace

void WaterVolumePrepass::setShaderPaths(const std::string& fullscreenVert,
                                        const std::string& frag)
{
    fullscreenVertPath_ = fullscreenVert;
    fragPath_ = frag;
}

void WaterVolumePrepass::setContainerBuffer(VkBuffer buffer)
{
    containerBuffer_ = buffer;
}

void WaterVolumePrepass::create(VulkanContext& ctx, const PassCreateInfo& ci,
                                const GBufferPass& gbuffer)
{
    if (ci.commands == nullptr)
    {
        failWaterVolumePrepass("PassCreateInfo.commands is required.");
    }
    if (containerBuffer_ == VK_NULL_HANDLE)
    {
        failWaterVolumePrepass("A valid water container buffer is required.");
    }

    extent_ = ci.extent;
    waterDistFormat_ = gbuffer.color(0, GBufferPass::Slot::WaterDist).format;

    VkDescriptorSetLayoutBinding uboBinding{};
    uboBinding.binding = 0;
    uboBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    uboBinding.descriptorCount = 1;
    uboBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutBinding depthBinding{};
    depthBinding.binding = 1;
    depthBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    depthBinding.descriptorCount = 1;
    depthBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutBinding containerBinding{};
    containerBinding.binding = 2;
    containerBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    containerBinding.descriptorCount = 1;
    containerBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutBinding bindings[] = {uboBinding, depthBinding, containerBinding};
    VkDescriptorSetLayoutCreateInfo dlci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    dlci.bindingCount = static_cast<uint32_t>(std::size(bindings));
    dlci.pBindings = bindings;
    if (!ctx.descriptorLayoutCache.get(ctx.device, dlci, descSetLayout_))
    {
        failWaterVolumePrepass("vkCreateDescriptorSetLayout failed.");
    }

    VkPipelineLayoutCreateInfo plci{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    plci.setLayoutCount = 1;
    VkDescriptorSetLayout descSetLayoutRaw = descSetLayout_;
    plci.pSetLayouts = &descSetLayoutRaw;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    if (vkCreatePipelineLayout(ctx.device, &plci, nullptr, &pipelineLayout) != VK_SUCCESS)
    {
        failWaterVolumePrepass("vkCreatePipelineLayout failed.");
    }
    pipelineLayout_ = engine::render::UniquePipelineLayout(ctx.device, pipelineLayout);

    std::array<VkDescriptorPoolSize, 3> poolSizes{};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSizes[0].descriptorCount = kMaxFramesInFlight;
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[1].descriptorCount = kMaxFramesInFlight;
    poolSizes[2].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    poolSizes[2].descriptorCount = kMaxFramesInFlight;

    std::vector<VkDescriptorSetLayout> layouts(kMaxFramesInFlight, descSetLayout_);
    descSets_.resize(kMaxFramesInFlight);
    const std::span<const VkDescriptorPoolSize> poolSizesView(poolSizes.data(), poolSizes.size());
    if (!ctx.descriptorAllocator.allocate(ctx.device, poolSizesView, kMaxFramesInFlight,
                                          layouts.data(), kMaxFramesInFlight,
                                          descSets_.data()))
    {
        failWaterVolumePrepass("Failed to allocate water-volume descriptor sets.");
    }

    ubos_.resize(kMaxFramesInFlight);
    for (auto& ubo : ubos_)
    {
        createBuffer(ctx, sizeof(FrameUbo), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                         VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                     ubo.buffer, ubo.memory);
        if (vkMapMemory(ctx.device, ubo.memory, 0, sizeof(FrameUbo), 0, &ubo.mapped) !=
            VK_SUCCESS)
        {
            failWaterVolumePrepass("vkMapMemory failed for frame UBO.");
        }
    }

    createRenderPass(ctx, waterDistFormat_);
    createFramebuffers(ctx, gbuffer);
    createLegacyTargets(ctx, ci);
    updateDescriptorSets(ctx, gbuffer);
    createPipeline(ctx);
}

void WaterVolumePrepass::destroy(VulkanContext& ctx)
{
    destroyPipeline(ctx);
    destroyFramebuffers(ctx);
    destroyLegacyTargets(ctx);
    destroyRenderPass(ctx);

    for (auto& ubo : ubos_)
    {
        if (ubo.mapped != nullptr)
        {
            vkUnmapMemory(ctx.device, ubo.memory);
            ubo.mapped = nullptr;
        }
        if (ubo.buffer != VK_NULL_HANDLE)
        {
            vkDestroyBuffer(ctx.device, ubo.buffer, nullptr);
            ubo.buffer = VK_NULL_HANDLE;
        }
        if (ubo.memory != VK_NULL_HANDLE)
        {
            vkFreeMemory(ctx.device, ubo.memory, nullptr);
            ubo.memory = VK_NULL_HANDLE;
        }
    }
    ubos_.clear();
    descSets_.clear();

    descSetLayout_ = VK_NULL_HANDLE;
    pipelineLayout_.reset();

    extent_ = {};
    waterDistFormat_ = VK_FORMAT_UNDEFINED;
}

void WaterVolumePrepass::onResize(VulkanContext& ctx, const PassCreateInfo& ci,
                                  const GBufferPass& gbuffer)
{
    extent_ = ci.extent;
    waterDistFormat_ = gbuffer.color(0, GBufferPass::Slot::WaterDist).format;
    destroyPipeline(ctx);
    destroyFramebuffers(ctx);
    destroyLegacyTargets(ctx);
    destroyRenderPass(ctx);
    createRenderPass(ctx, waterDistFormat_);
    createFramebuffers(ctx, gbuffer);
    createLegacyTargets(ctx, ci);
    updateDescriptorSets(ctx, gbuffer);
    createPipeline(ctx);
}

void WaterVolumePrepass::recordLegacyCopy(VkCommandBuffer cmd, uint32_t frameIndex,
                                          const GBufferPass& gbuffer)
{
    if (frameIndex >= legacyWaterDist_.size())
    {
        return;
    }
    const DebugTarget& dst = legacyWaterDist_[frameIndex];
    if (dst.image == VK_NULL_HANDLE)
    {
        return;
    }

    const auto& src = gbuffer.color(frameIndex, GBufferPass::Slot::WaterDist);

    std::array<VkImageMemoryBarrier, 2> toTransfer{};
    toTransfer[0].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    toTransfer[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    toTransfer[0].dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    toTransfer[0].oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    toTransfer[0].newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    toTransfer[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toTransfer[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toTransfer[0].image = src.image;
    toTransfer[0].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    toTransfer[0].subresourceRange.baseMipLevel = 0;
    toTransfer[0].subresourceRange.levelCount = 1;
    toTransfer[0].subresourceRange.baseArrayLayer = 0;
    toTransfer[0].subresourceRange.layerCount = 1;

    toTransfer[1].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    toTransfer[1].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    toTransfer[1].dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    toTransfer[1].oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    toTransfer[1].newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toTransfer[1].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toTransfer[1].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toTransfer[1].image = dst.image;
    toTransfer[1].subresourceRange = toTransfer[0].subresourceRange;

    vkCmdPipelineBarrier(cmd,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr,
                         static_cast<uint32_t>(toTransfer.size()), toTransfer.data());

    VkImageCopy copy{};
    copy.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copy.srcSubresource.mipLevel = 0;
    copy.srcSubresource.baseArrayLayer = 0;
    copy.srcSubresource.layerCount = 1;
    copy.dstSubresource = copy.srcSubresource;
    copy.extent = {extent_.width, extent_.height, 1};

    vkCmdCopyImage(cmd, src.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, dst.image,
                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);

    std::array<VkImageMemoryBarrier, 2> toRead{};
    toRead[0].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    toRead[0].srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    toRead[0].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    toRead[0].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    toRead[0].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    toRead[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toRead[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toRead[0].image = src.image;
    toRead[0].subresourceRange = toTransfer[0].subresourceRange;

    toRead[1].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    toRead[1].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    toRead[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    toRead[1].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toRead[1].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    toRead[1].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toRead[1].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toRead[1].image = dst.image;
    toRead[1].subresourceRange = toTransfer[1].subresourceRange;

    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         0, 0, nullptr, 0, nullptr, static_cast<uint32_t>(toRead.size()),
                         toRead.data());
}

void WaterVolumePrepass::record(VulkanContext& ctx, const FrameContext& fc,
                                const FrameUbo& frameUbo)
{
    (void)ctx;
    if (renderPass_ == VK_NULL_HANDLE || pipeline_.get() == VK_NULL_HANDLE)
    {
        return;
    }
    if (fc.frameIndex >= frames_.size() || fc.frameIndex >= descSets_.size())
    {
        return;
    }

    updateUbo(fc.frameIndex, frameUbo);

    VkRenderPassBeginInfo rp{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    rp.renderPass = renderPass_;
    rp.framebuffer = frames_[fc.frameIndex].framebuffer;
    rp.renderArea.offset = {0, 0};
    rp.renderArea.extent = extent_;
    rp.clearValueCount = 0;
    rp.pClearValues = nullptr;

    vkCmdBeginRenderPass(fc.cmd, &rp, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(fc.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_.get());
    vkCmdBindDescriptorSets(fc.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout_.get(), 0, 1,
                            &descSets_[fc.frameIndex], 0, nullptr);

    VkViewport vp{};
    vp.x = 0.0f;
    vp.y = 0.0f;
    vp.width = static_cast<float>(extent_.width);
    vp.height = static_cast<float>(extent_.height);
    vp.minDepth = 0.0f;
    vp.maxDepth = 1.0f;
    VkRect2D scissor{{0, 0}, extent_};
    vkCmdSetViewport(fc.cmd, 0, 1, &vp);
    vkCmdSetScissor(fc.cmd, 0, 1, &scissor);

    vkCmdDraw(fc.cmd, 3, 1, 0, 0);
    vkCmdEndRenderPass(fc.cmd);
}

void WaterVolumePrepass::createRenderPass(VulkanContext& ctx, VkFormat waterDistFormat)
{
    if (renderPass_ != VK_NULL_HANDLE)
    {
        return;
    }

    VkAttachmentDescription color{};
    color.format = waterDistFormat;
    color.samples = VK_SAMPLE_COUNT_1_BIT;
    // preserve WaterDist.g, which carries material category metadata from the g-buffer pass.
    // this pass only updates WaterDist.r through the color write mask.
    color.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    color.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    color.initialLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    color.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkAttachmentReference colorRef{};
    colorRef.attachment = 0;
    colorRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorRef;

    std::array<VkSubpassDependency, 2> deps{};
    deps[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    deps[0].dstSubpass = 0;
    deps[0].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    deps[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    deps[0].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    deps[0].dstAccessMask =
        VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    deps[1].srcSubpass = 0;
    deps[1].dstSubpass = VK_SUBPASS_EXTERNAL;
    deps[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    deps[1].dstStageMask =
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
    deps[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    deps[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    VkRenderPassCreateInfo rpci{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    rpci.attachmentCount = 1;
    rpci.pAttachments = &color;
    rpci.subpassCount = 1;
    rpci.pSubpasses = &subpass;
    rpci.dependencyCount = static_cast<uint32_t>(deps.size());
    rpci.pDependencies = deps.data();

    if (vkCreateRenderPass(ctx.device, &rpci, nullptr, &renderPass_) != VK_SUCCESS)
    {
        failWaterVolumePrepass("vkCreateRenderPass failed.");
    }
}

void WaterVolumePrepass::destroyRenderPass(VulkanContext& ctx)
{
    if (renderPass_ != VK_NULL_HANDLE)
    {
        vkDestroyRenderPass(ctx.device, renderPass_, nullptr);
        renderPass_ = VK_NULL_HANDLE;
    }
}

void WaterVolumePrepass::createPipeline(VulkanContext& ctx)
{
    if (renderPass_ == VK_NULL_HANDLE || pipelineLayout_.get() == VK_NULL_HANDLE)
    {
        failWaterVolumePrepass("Render pass and pipeline layout must exist first.");
    }
    VkPipeline pipeline = createFullscreenPipeline(ctx, renderPass_, pipelineLayout_.get(),
                                                   fullscreenVertPath_.c_str(),
                                                   fragPath_.c_str());
    pipeline_ = engine::render::UniquePipeline(ctx.device, pipeline);
}

void WaterVolumePrepass::destroyPipeline(VulkanContext& ctx)
{
    (void)ctx;
    pipeline_.reset();
}

void WaterVolumePrepass::createFramebuffers(VulkanContext& ctx, const GBufferPass& gbuffer)
{
    frames_.resize(kMaxFramesInFlight);
    for (uint32_t i = 0; i < kMaxFramesInFlight; ++i)
    {
        VkImageView attachment = gbuffer.color(i, GBufferPass::Slot::WaterDist).view;
        VkFramebufferCreateInfo fbci{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
        fbci.renderPass = renderPass_;
        fbci.attachmentCount = 1;
        fbci.pAttachments = &attachment;
        fbci.width = extent_.width;
        fbci.height = extent_.height;
        fbci.layers = 1;
        if (vkCreateFramebuffer(ctx.device, &fbci, nullptr, &frames_[i].framebuffer) !=
            VK_SUCCESS)
        {
            failWaterVolumePrepass("vkCreateFramebuffer failed.");
        }
    }
}

void WaterVolumePrepass::destroyFramebuffers(VulkanContext& ctx)
{
    for (auto& frame : frames_)
    {
        if (frame.framebuffer != VK_NULL_HANDLE)
        {
            vkDestroyFramebuffer(ctx.device, frame.framebuffer, nullptr);
            frame.framebuffer = VK_NULL_HANDLE;
        }
    }
    frames_.clear();
}

void WaterVolumePrepass::createLegacyTargets(VulkanContext& ctx, const PassCreateInfo& ci)
{
    if (ci.commands == nullptr)
    {
        failWaterVolumePrepass("PassCreateInfo.commands is required for legacy debug targets.");
    }

    legacyWaterDist_.resize(kMaxFramesInFlight);
    for (auto& target : legacyWaterDist_)
    {
        target.format = waterDistFormat_;
        createImage(ctx, extent_.width, extent_.height, target.format,
                    VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                    target.image, target.memory);
        target.view = createImageView(ctx.device, target.image, target.format,
                                      VK_IMAGE_ASPECT_COLOR_BIT);

        if (!ctx.samplerCache.get(ctx.device, engine::render::SamplerPreset::ClampNearest,
                                  target.sampler))
        {
            failWaterVolumePrepass("vkCreateSampler failed for legacy WaterDist target.");
        }

        transitionImageLayout(ctx, *ci.commands, target.image, VK_IMAGE_ASPECT_COLOR_BIT,
                              VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

        VkCommandBuffer clearCmd = ci.commands->beginSingleTimeCommands(ctx);
        VkClearColorValue zero{};
        VkImageSubresourceRange range{};
        range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        range.baseMipLevel = 0;
        range.levelCount = 1;
        range.baseArrayLayer = 0;
        range.layerCount = 1;
        vkCmdClearColorImage(clearCmd, target.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                             &zero, 1, &range);
        ci.commands->endSingleTimeCommands(ctx, clearCmd);

        transitionImageLayout(ctx, *ci.commands, target.image, VK_IMAGE_ASPECT_COLOR_BIT,
                              VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                              VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }
}

void WaterVolumePrepass::destroyLegacyTargets(VulkanContext& ctx)
{
    for (auto& target : legacyWaterDist_)
    {
        target.sampler = VK_NULL_HANDLE;
        if (target.view != VK_NULL_HANDLE)
        {
            vkDestroyImageView(ctx.device, target.view, nullptr);
            target.view = VK_NULL_HANDLE;
        }
        if (target.image != VK_NULL_HANDLE)
        {
            vkDestroyImage(ctx.device, target.image, nullptr);
            target.image = VK_NULL_HANDLE;
        }
        if (target.memory != VK_NULL_HANDLE)
        {
            vkFreeMemory(ctx.device, target.memory, nullptr);
            target.memory = VK_NULL_HANDLE;
        }
        target.format = VK_FORMAT_UNDEFINED;
    }
    legacyWaterDist_.clear();
}

VkImageView WaterVolumePrepass::legacyWaterDistView(uint32_t frameIndex) const
{
    if (frameIndex >= legacyWaterDist_.size())
    {
        return VK_NULL_HANDLE;
    }
    return legacyWaterDist_[frameIndex].view;
}

VkSampler WaterVolumePrepass::legacyWaterDistSampler(uint32_t frameIndex) const
{
    if (frameIndex >= legacyWaterDist_.size())
    {
        return VK_NULL_HANDLE;
    }
    return legacyWaterDist_[frameIndex].sampler;
}

void WaterVolumePrepass::updateDescriptorSets(VulkanContext& ctx, const GBufferPass& gbuffer)
{
    for (uint32_t i = 0; i < kMaxFramesInFlight; ++i)
    {
        VkDescriptorBufferInfo uboInfo{};
        uboInfo.buffer = ubos_[i].buffer;
        uboInfo.offset = 0;
        uboInfo.range = sizeof(FrameUbo);

        VkDescriptorImageInfo depthInfo{};
        depthInfo.sampler = gbuffer.depth(i).sampler;
        depthInfo.imageView = gbuffer.depth(i).view;
        depthInfo.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;

        VkDescriptorBufferInfo containerInfo{};
        containerInfo.buffer = containerBuffer_;
        containerInfo.offset = 0;
        containerInfo.range = VK_WHOLE_SIZE;

        std::array<VkWriteDescriptorSet, 3> writes{};
        writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[0].dstSet = descSets_[i];
        writes[0].dstBinding = 0;
        writes[0].descriptorCount = 1;
        writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        writes[0].pBufferInfo = &uboInfo;

        writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[1].dstSet = descSets_[i];
        writes[1].dstBinding = 1;
        writes[1].descriptorCount = 1;
        writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[1].pImageInfo = &depthInfo;

        writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[2].dstSet = descSets_[i];
        writes[2].dstBinding = 2;
        writes[2].descriptorCount = 1;
        writes[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[2].pBufferInfo = &containerInfo;

        vkUpdateDescriptorSets(ctx.device, static_cast<uint32_t>(writes.size()),
                               writes.data(), 0, nullptr);
    }
}

void WaterVolumePrepass::updateUbo(uint32_t frameIndex, const FrameUbo& frameUbo)
{
    if (frameIndex >= ubos_.size() || ubos_[frameIndex].mapped == nullptr)
    {
        return;
    }
    std::memcpy(ubos_[frameIndex].mapped, &frameUbo, sizeof(frameUbo));
}
