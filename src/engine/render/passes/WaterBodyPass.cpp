#include "engine/render/passes/WaterBodyPass.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <span>
#include <string_view>

#include "engine/render/passes/GBufferPass.h"
#include "engine/render/passes/LightingPass.h"
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
constexpr const char* kWaterBodyPassSubsystem = "WaterBodyPass";
constexpr const char* kWaterBodyPassRationale =
    "WaterBodyPass is required for the first Water V2 fullscreen medium composite, "
    "which samples lighting, depth, WaterDist, and authored water-volume bounds.";

[[noreturn]] void failWaterBodyPass(std::string_view detail)
{
    logAndExit(kWaterBodyPassSubsystem,
               std::string("Required pass failure: ") + std::string(detail) + " " +
                   kWaterBodyPassRationale);
}

std::vector<char> loadWaterBodyShaderOrFatal(const char* path, std::string_view stage)
{
    std::vector<char> code;
    std::string error;
    if (!tryReadFile(path, code, &error))
    {
        failWaterBodyPass(std::string("Failed to read ") + std::string(stage) + " shader '" +
                          path + "': " + error);
    }
    return code;
}

VkShaderModule createWaterBodyShaderModuleOrFatal(VkDevice device,
                                                  const std::vector<char>& code,
                                                  const char* path,
                                                  std::string_view stage)
{
    std::string error;
    VkShaderModule module = tryCreateShaderModule(device, code, &error);
    if (module == VK_NULL_HANDLE)
    {
        failWaterBodyPass(std::string("Failed to create ") + std::string(stage) +
                          " shader module for '" + path + "': " +
                          (error.empty() ? "vkCreateShaderModule failed." : error));
    }
    return module;
}

VkRenderPass createWaterBodyRenderPass(VkDevice device, VkFormat colorFormat)
{
    VkAttachmentDescription color{};
    color.format = colorFormat;
    color.samples = VK_SAMPLE_COUNT_1_BIT;
    color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
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

    VkSubpassDependency dep{};
    dep.srcSubpass = VK_SUBPASS_EXTERNAL;
    dep.dstSubpass = 0;
    dep.srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    dep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo rpci{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    rpci.attachmentCount = 1;
    rpci.pAttachments = &color;
    rpci.subpassCount = 1;
    rpci.pSubpasses = &subpass;
    rpci.dependencyCount = 1;
    rpci.pDependencies = &dep;

    VkRenderPass rp = VK_NULL_HANDLE;
    if (vkCreateRenderPass(device, &rpci, nullptr, &rp) != VK_SUCCESS)
    {
        failWaterBodyPass("vkCreateRenderPass failed for water-body targets.");
    }
    return rp;
}

VkPipeline createFullscreenPipeline(VulkanContext& ctx, VkRenderPass renderPass,
                                    VkPipelineLayout pipelineLayout, const char* vertPath,
                                    const char* fragPath)
{
    const std::vector<char> vertCode = loadWaterBodyShaderOrFatal(vertPath, "vertex");
    const std::vector<char> fragCode = loadWaterBodyShaderOrFatal(fragPath, "fragment");

    VkShaderModule vertModule =
        createWaterBodyShaderModuleOrFatal(ctx.device, vertCode, vertPath, "vertex");
    VkShaderModule fragModule =
        createWaterBodyShaderModuleOrFatal(ctx.device, fragCode, fragPath, "fragment");

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

    VkPipelineViewportStateCreateInfo viewportState{
        VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo raster{
        VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.lineWidth = 1.0f;
    raster.cullMode = VK_CULL_MODE_NONE;
    raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

    VkPipelineMultisampleStateCreateInfo ms{
        VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState blendAttachment{};
    blendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                     VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

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
        failWaterBodyPass("vkCreateGraphicsPipelines failed for the water-body pipeline.");
    }

    vkDestroyShaderModule(ctx.device, fragModule, nullptr);
    vkDestroyShaderModule(ctx.device, vertModule, nullptr);

    return pipeline;
}
} // namespace

void WaterBodyPass::setShaderPaths(const std::string& fullscreenVert,
                                   const std::string& fragPath)
{
    fullscreenVertPath_ = fullscreenVert;
    fragPath_ = fragPath;
}

void WaterBodyPass::setWaterVolumeBuffer(VkBuffer buffer)
{
    waterVolumeBuffer_ = buffer;
}

void WaterBodyPass::create(VulkanContext& ctx, const PassCreateInfo& ci,
                           const GBufferPass& gbuffer, const LightingPass& lighting)
{
    if (ci.commands == nullptr)
    {
        failWaterBodyPass("PassCreateInfo.commands is required.");
    }
    if (waterVolumeBuffer_ == VK_NULL_HANDLE)
    {
        failWaterBodyPass("A valid water volume buffer is required.");
    }

    extent_ = ci.extent;

    constexpr uint32_t kTexBindings = 3;
    VkDescriptorSetLayoutBinding texBindings[kTexBindings]{};
    for (uint32_t i = 0; i < kTexBindings; ++i)
    {
        texBindings[i].binding = i;
        texBindings[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        texBindings[i].descriptorCount = 1;
        texBindings[i].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    }

    VkDescriptorSetLayoutCreateInfo texLci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    texLci.bindingCount = kTexBindings;
    texLci.pBindings = texBindings;
    if (!ctx.descriptorLayoutCache.get(ctx.device, texLci, texSetLayout_))
    {
        failWaterBodyPass("vkCreateDescriptorSetLayout failed for sampled inputs.");
    }

    VkDescriptorSetLayoutBinding uboBinding{};
    uboBinding.binding = 0;
    uboBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    uboBinding.descriptorCount = 1;
    uboBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo uboLci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    uboLci.bindingCount = 1;
    uboLci.pBindings = &uboBinding;
    if (!ctx.descriptorLayoutCache.get(ctx.device, uboLci, uboSetLayout_))
    {
        failWaterBodyPass("vkCreateDescriptorSetLayout failed for frame UBO.");
    }

    VkDescriptorSetLayoutBinding waterBinding{};
    waterBinding.binding = 0;
    waterBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    waterBinding.descriptorCount = 1;
    waterBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo waterLci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    waterLci.bindingCount = 1;
    waterLci.pBindings = &waterBinding;
    if (!ctx.descriptorLayoutCache.get(ctx.device, waterLci, waterSetLayout_))
    {
        failWaterBodyPass("vkCreateDescriptorSetLayout failed for water volume bindings.");
    }

    VkDescriptorSetLayout setLayouts[] = {texSetLayout_, uboSetLayout_, waterSetLayout_};
    VkPipelineLayoutCreateInfo plci{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    plci.setLayoutCount = 3;
    plci.pSetLayouts = setLayouts;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    if (vkCreatePipelineLayout(ctx.device, &plci, nullptr, &pipelineLayout) != VK_SUCCESS)
    {
        failWaterBodyPass("vkCreatePipelineLayout failed.");
    }
    pipelineLayout_ = engine::render::UniquePipelineLayout(ctx.device, pipelineLayout);

    VkDescriptorPoolSize poolSizes[3]{};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[0].descriptorCount = kMaxFramesInFlight * kTexBindings;
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSizes[1].descriptorCount = kMaxFramesInFlight;
    poolSizes[2].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    poolSizes[2].descriptorCount = 1;

    texSets_.resize(kMaxFramesInFlight);
    uboSets_.resize(kMaxFramesInFlight);

    const std::span<const VkDescriptorPoolSize> poolSizesView(poolSizes, 3);
    constexpr uint32_t kMaxDescriptorSets = kMaxFramesInFlight * 2 + 1;

    std::vector<VkDescriptorSetLayout> texLayouts(kMaxFramesInFlight, texSetLayout_);
    if (!ctx.descriptorAllocator.allocate(ctx.device, poolSizesView, kMaxDescriptorSets,
                                          texLayouts.data(), kMaxFramesInFlight,
                                          texSets_.data()))
    {
        failWaterBodyPass("Failed to allocate water-body sampled input descriptors.");
    }

    std::vector<VkDescriptorSetLayout> uboLayouts(kMaxFramesInFlight, uboSetLayout_);
    if (!ctx.descriptorAllocator.allocate(ctx.device, poolSizesView, kMaxDescriptorSets,
                                          uboLayouts.data(), kMaxFramesInFlight,
                                          uboSets_.data()))
    {
        failWaterBodyPass("Failed to allocate water-body frame UBO descriptors.");
    }

    VkDescriptorSetLayout waterSetLayoutRaw = waterSetLayout_;
    if (!ctx.descriptorAllocator.allocate(ctx.device, poolSizesView, kMaxDescriptorSets,
                                          &waterSetLayoutRaw, 1, &waterSet_))
    {
        failWaterBodyPass("Failed to allocate water-body volume descriptors.");
    }

    ubos_.resize(kMaxFramesInFlight);
    for (size_t i = 0; i < kMaxFramesInFlight; ++i)
    {
        createBuffer(ctx, sizeof(FrameUbo), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                     ubos_[i].buffer, ubos_[i].memory);

        if (vkMapMemory(ctx.device, ubos_[i].memory, 0, sizeof(FrameUbo), 0,
                        &ubos_[i].mapped) != VK_SUCCESS)
        {
            failWaterBodyPass("vkMapMemory failed for frame UBO.");
        }

        VkDescriptorBufferInfo bi{};
        bi.buffer = ubos_[i].buffer;
        bi.offset = 0;
        bi.range = sizeof(FrameUbo);

        VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        write.dstSet = uboSets_[i];
        write.dstBinding = 0;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        write.pBufferInfo = &bi;
        vkUpdateDescriptorSets(ctx.device, 1, &write, 0, nullptr);
    }

    createTargets(ctx, ci);
    updateDescriptorSets(ctx, gbuffer, lighting);
    updateWaterDescriptor(ctx);
    createPipeline(ctx);
}

void WaterBodyPass::destroy(VulkanContext& ctx)
{
    destroyPipeline(ctx);
    destroyTargets(ctx);

    for (auto& ubo : ubos_)
    {
        if (ubo.mapped != nullptr)
        {
            vkUnmapMemory(ctx.device, ubo.memory);
        }
        if (ubo.buffer != VK_NULL_HANDLE)
        {
            vkDestroyBuffer(ctx.device, ubo.buffer, nullptr);
        }
        if (ubo.memory != VK_NULL_HANDLE)
        {
            vkFreeMemory(ctx.device, ubo.memory, nullptr);
        }
        ubo = PerFrameUbo{};
    }
    ubos_.clear();

    texSetLayout_ = VK_NULL_HANDLE;
    uboSetLayout_ = VK_NULL_HANDLE;
    waterSetLayout_ = VK_NULL_HANDLE;
    pipelineLayout_.reset();

    texSets_.clear();
    uboSets_.clear();
    waterSet_ = VK_NULL_HANDLE;
}

void WaterBodyPass::onResize(VulkanContext& ctx, const PassCreateInfo& ci,
                             const GBufferPass& gbuffer, const LightingPass& lighting)
{
    extent_ = ci.extent;
    destroyPipeline(ctx);
    destroyTargets(ctx);
    createTargets(ctx, ci);
    updateDescriptorSets(ctx, gbuffer, lighting);
    createPipeline(ctx);
}

void WaterBodyPass::record(VulkanContext& ctx, const FrameContext& fc, const FrameUbo& ubo)
{
    (void)ctx;
    if (fc.frameIndex >= targets_.size())
    {
        return;
    }

    updateUbo(fc.frameIndex, ubo);

    VkClearValue clear{};
    clear.color = {{0.0f, 0.0f, 0.0f, 1.0f}};

    const Target& tgt = targets_[fc.frameIndex];
    VkRenderPassBeginInfo rp{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    rp.renderPass = tgt.renderPass;
    rp.framebuffer = tgt.framebuffer;
    rp.renderArea.offset = {0, 0};
    rp.renderArea.extent = extent_;
    rp.clearValueCount = 1;
    rp.pClearValues = &clear;

    vkCmdBeginRenderPass(fc.cmd, &rp, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(fc.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_.get());

    VkDescriptorSet sets[] = {texSets_[fc.frameIndex], uboSets_[fc.frameIndex], waterSet_};
    vkCmdBindDescriptorSets(fc.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout_.get(), 0, 3,
                            sets, 0, nullptr);

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

void WaterBodyPass::copyToLighting(VkCommandBuffer cmd, uint32_t frameIndex,
                                   const LightingPass& lighting) const
{
    if (frameIndex >= targets_.size())
    {
        return;
    }

    const Target& src = targets_[frameIndex];
    const auto& dst = lighting.lit(frameIndex);

    std::array<VkImageMemoryBarrier, 2> pre{};
    pre[0].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    pre[0].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    pre[0].dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    pre[0].oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    pre[0].newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    pre[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    pre[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    pre[0].image = src.image;
    pre[0].subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

    pre[1].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    pre[1].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    pre[1].dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    pre[1].oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    pre[1].newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    pre[1].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    pre[1].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    pre[1].image = dst.image;
    pre[1].subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

    vkCmdPipelineBarrier(cmd,
                         VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                             VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr,
                         static_cast<uint32_t>(pre.size()), pre.data());

    VkImageCopy copy{};
    copy.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    copy.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    copy.extent = {extent_.width, extent_.height, 1};
    vkCmdCopyImage(cmd, src.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, dst.image,
                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);

    std::array<VkImageMemoryBarrier, 2> post{};
    post[0].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    post[0].srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    post[0].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    post[0].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    post[0].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    post[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    post[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    post[0].image = src.image;
    post[0].subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

    post[1].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    post[1].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    post[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    post[1].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    post[1].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    post[1].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    post[1].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    post[1].image = dst.image;
    post[1].subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr,
                         static_cast<uint32_t>(post.size()), post.data());
}

const WaterBodyPass::Target& WaterBodyPass::target(uint32_t frameIndex) const
{
    return targets_.at(frameIndex);
}

VkExtent2D WaterBodyPass::extent() const
{
    return extent_;
}

void WaterBodyPass::createPipeline(VulkanContext& ctx)
{
    if (targets_.empty())
    {
        failWaterBodyPass("Targets must exist before pipeline creation.");
    }
    VkPipeline pipeline = createFullscreenPipeline(ctx, targets_[0].renderPass,
                                                   pipelineLayout_.get(),
                                                   fullscreenVertPath_.c_str(),
                                                   fragPath_.c_str());
    pipeline_ = engine::render::UniquePipeline(ctx.device, pipeline);
}

void WaterBodyPass::destroyPipeline(VulkanContext& ctx)
{
    (void)ctx;
    pipeline_.reset();
}

void WaterBodyPass::createTargets(VulkanContext& ctx, const PassCreateInfo& ci)
{
    if (ci.commands == nullptr)
    {
        failWaterBodyPass("PassCreateInfo.commands is required for target transitions.");
    }

    targets_.resize(kMaxFramesInFlight);
    for (auto& t : targets_)
    {
        createImage(ctx, extent_.width, extent_.height, t.format,
                    VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                        VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                    t.image, t.memory);
        t.view = createImageView(ctx.device, t.image, t.format, VK_IMAGE_ASPECT_COLOR_BIT);

        if (!ctx.samplerCache.get(ctx.device, engine::render::SamplerPreset::ClampLinear,
                                  t.sampler))
        {
            failWaterBodyPass("vkCreateSampler failed for a water-body target.");
        }

        transitionImageLayout(ctx, *ci.commands, t.image, VK_IMAGE_ASPECT_COLOR_BIT,
                              VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

        t.renderPass = createWaterBodyRenderPass(ctx.device, t.format);

        VkImageView attachments[] = {t.view};
        VkFramebufferCreateInfo fbci{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
        fbci.renderPass = t.renderPass;
        fbci.attachmentCount = 1;
        fbci.pAttachments = attachments;
        fbci.width = extent_.width;
        fbci.height = extent_.height;
        fbci.layers = 1;
        if (vkCreateFramebuffer(ctx.device, &fbci, nullptr, &t.framebuffer) != VK_SUCCESS)
        {
            failWaterBodyPass("vkCreateFramebuffer failed for a water-body target.");
        }
    }
}

void WaterBodyPass::destroyTargets(VulkanContext& ctx)
{
    for (auto& t : targets_)
    {
        if (t.framebuffer != VK_NULL_HANDLE)
        {
            vkDestroyFramebuffer(ctx.device, t.framebuffer, nullptr);
        }
        if (t.renderPass != VK_NULL_HANDLE)
        {
            vkDestroyRenderPass(ctx.device, t.renderPass, nullptr);
        }
        t.sampler = VK_NULL_HANDLE;
        if (t.view != VK_NULL_HANDLE)
        {
            vkDestroyImageView(ctx.device, t.view, nullptr);
        }
        if (t.image != VK_NULL_HANDLE)
        {
            vkDestroyImage(ctx.device, t.image, nullptr);
        }
        if (t.memory != VK_NULL_HANDLE)
        {
            vkFreeMemory(ctx.device, t.memory, nullptr);
        }
        t = Target{};
    }
    targets_.clear();
}

void WaterBodyPass::updateDescriptorSets(VulkanContext& ctx, const GBufferPass& gbuffer,
                                         const LightingPass& lighting)
{
    const size_t count = std::min(texSets_.size(), static_cast<size_t>(kMaxFramesInFlight));
    for (size_t i = 0; i < count; ++i)
    {
        VkDescriptorImageInfo infos[3]{};
        infos[0].sampler = lighting.lit(static_cast<uint32_t>(i)).sampler;
        infos[0].imageView = lighting.lit(static_cast<uint32_t>(i)).view;
        infos[0].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        infos[1].sampler = gbuffer.depth(static_cast<uint32_t>(i)).sampler;
        infos[1].imageView = gbuffer.depth(static_cast<uint32_t>(i)).view;
        infos[1].imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;

        infos[2].sampler =
            gbuffer.color(static_cast<uint32_t>(i), GBufferPass::Slot::WaterDist).sampler;
        infos[2].imageView =
            gbuffer.color(static_cast<uint32_t>(i), GBufferPass::Slot::WaterDist).view;
        infos[2].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkWriteDescriptorSet writes[3]{};
        for (uint32_t binding = 0; binding < 3; ++binding)
        {
            writes[binding].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[binding].dstSet = texSets_[i];
            writes[binding].dstBinding = binding;
            writes[binding].descriptorCount = 1;
            writes[binding].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            writes[binding].pImageInfo = &infos[binding];
        }

        vkUpdateDescriptorSets(ctx.device, 3, writes, 0, nullptr);
    }
}

void WaterBodyPass::updateWaterDescriptor(VulkanContext& ctx)
{
    if (waterSet_ == VK_NULL_HANDLE || waterVolumeBuffer_ == VK_NULL_HANDLE)
    {
        return;
    }

    VkDescriptorBufferInfo waterInfo{};
    waterInfo.buffer = waterVolumeBuffer_;
    waterInfo.offset = 0;
    waterInfo.range = VK_WHOLE_SIZE;

    VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    write.dstSet = waterSet_;
    write.dstBinding = 0;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    write.pBufferInfo = &waterInfo;
    vkUpdateDescriptorSets(ctx.device, 1, &write, 0, nullptr);
}

void WaterBodyPass::updateUbo(uint32_t frameIndex, const FrameUbo& ubo)
{
    if (frameIndex < ubos_.size() && ubos_[frameIndex].mapped != nullptr)
    {
        std::memcpy(ubos_[frameIndex].mapped, &ubo, sizeof(FrameUbo));
    }
}
