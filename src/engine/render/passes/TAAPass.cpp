#include "engine/render/passes/TAAPass.h"

#include <algorithm>
#include <array>
#include <span>

#include "Resources/GpuImage.h"
#include "Resources/ShaderModule.h"
#include "engine/render/FrameContext.h"
#include "engine/render/Utils.h"
#include "engine/render/VulkanContext.h"
#include "engine/render/passes/BloomPass.h"
#include "engine/render/passes/GBufferPass.h"
#include "engine/render/passes/LightingPass.h"
#include "engine/render/passes/PassCreateInfo.h"

namespace
{
struct TAAPushConstants
{
    glm::vec2 resolution{0.0f};
    glm::vec2 texelSize{1.0f};
    glm::vec2 jitter{0.0f};
    glm::vec2 prevJitter{0.0f};
    float similarityThreshold = 0.1f;
    float velocityScale = 10.0f;
    float blendFactorMin = 0.05f;
    float blendFactorMax = 0.2f;
    float bloomIntensity = 0.0f;
    int32_t enabled = 1;
    int32_t debugMode = 0;
    float depthEdgeThreshold = 0.1f;        // day 12E: depth edge rejection
    float nearPlane = 0.1f;                 // for depth linearization
    float crossFrameDepthThreshold = 0.02f; // cross-frame depth rejection
    float farPlane = 1000.0f;               // for depth linearization
    float colorVarianceThreshold = 0.15f;   // local color variance rejection
    float softEdgeStrength = 0.0f;          // bounded full-scene edge reconstruction
};

constexpr const char* kTAAPassSubsystem = "TAAPass";
constexpr const char* kTAAPassRationale =
    "TAAPass is required because the current present path and sharpen/post "
    "chain sample its "
    "history-resolved output.";

[[noreturn]] void failTAAPass(std::string_view detail)
{
    logAndExit(kTAAPassSubsystem, std::string("Required pass failure: ") + std::string(detail) +
                                      " " + kTAAPassRationale);
}

std::vector<char> loadTAAShaderOrFatal(const char* path, std::string_view stage)
{
    std::vector<char> code;
    std::string error;
    if (!tryReadFile(path, code, &error))
    {
        failTAAPass(std::string("Failed to read ") + std::string(stage) + " shader '" + path +
                    "': " + error);
    }
    return code;
}

VkShaderModule createTAAShaderModuleOrFatal(VkDevice device, const std::vector<char>& code,
                                            const char* path, std::string_view stage)
{
    std::string error;
    VkShaderModule module = tryCreateShaderModule(device, code, &error);
    if (module == VK_NULL_HANDLE)
    {
        failTAAPass(std::string("Failed to create ") + std::string(stage) + " shader module for '" +
                    path + "': " + (error.empty() ? "vkCreateShaderModule failed." : error));
    }
    return module;
}

VkRenderPass createTaaRenderPass(VkDevice device, VkFormat format)
{
    VkAttachmentDescription color{};
    color.format = format;
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

    VkSubpassDependency deps[2]{};
    deps[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    deps[0].dstSubpass = 0;
    deps[0].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    deps[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    deps[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    deps[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    deps[1].srcSubpass = 0;
    deps[1].dstSubpass = VK_SUBPASS_EXTERNAL;
    deps[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    deps[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    deps[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    deps[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    VkRenderPassCreateInfo rpci{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    rpci.attachmentCount = 1;
    rpci.pAttachments = &color;
    rpci.subpassCount = 1;
    rpci.pSubpasses = &subpass;
    rpci.dependencyCount = 2;
    rpci.pDependencies = deps;

    VkRenderPass rp = VK_NULL_HANDLE;
    if (vkCreateRenderPass(device, &rpci, nullptr, &rp) != VK_SUCCESS)
    {
        failTAAPass("vkCreateRenderPass failed for TAA history targets.");
    }
    return rp;
}

VkPipeline createFullscreenPipeline(VulkanContext& ctx, VkRenderPass renderPass,
                                    VkPipelineLayout pipelineLayout, const char* vertPath,
                                    const char* fragPath)
{
    const std::vector<char> vertCode = loadTAAShaderOrFatal(vertPath, "vertex");
    const std::vector<char> fragCode = loadTAAShaderOrFatal(fragPath, "fragment");

    VkShaderModule vertModule =
        createTAAShaderModuleOrFatal(ctx.device, vertCode, vertPath, "vertex");
    VkShaderModule fragModule =
        createTAAShaderModuleOrFatal(ctx.device, fragCode, fragPath, "fragment");

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
    ms.sampleShadingEnable = VK_FALSE;
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depth{
        VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    depth.depthTestEnable = VK_FALSE;
    depth.depthWriteEnable = VK_FALSE;
    depth.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    depth.depthBoundsTestEnable = VK_FALSE;
    depth.stencilTestEnable = VK_FALSE;

    VkPipelineColorBlendAttachmentState blendAttachment{};
    blendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                     VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    blendAttachment.blendEnable = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo blend{
        VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    blend.logicOpEnable = VK_FALSE;
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
    pci.pDepthStencilState = &depth;
    pci.pColorBlendState = &blend;
    pci.pDynamicState = &dynamic;
    pci.layout = pipelineLayout;
    pci.renderPass = renderPass;
    pci.subpass = 0;

    VkPipeline pipeline = VK_NULL_HANDLE;
    if (vkCreateGraphicsPipelines(ctx.device, VK_NULL_HANDLE, 1, &pci, nullptr, &pipeline) !=
        VK_SUCCESS)
    {
        failTAAPass("vkCreateGraphicsPipelines failed for the temporal resolve pipeline.");
    }

    vkDestroyShaderModule(ctx.device, fragModule, nullptr);
    vkDestroyShaderModule(ctx.device, vertModule, nullptr);

    return pipeline;
}
} // namespace

void TAAPass::setShaderPaths(const std::string& fullscreenVert, const std::string& fragPath)
{
    fullscreenVertPath_ = fullscreenVert;
    fragPath_ = fragPath;
}

void TAAPass::create(VulkanContext& ctx, const PassCreateInfo& ci, const GBufferPass& gbuffer,
                     const LightingPass& lighting, const BloomPass& bloom)
{
    if (ci.commands == nullptr)
    {
        failTAAPass("PassCreateInfo.commands is required for TAAPass.");
    }

    extent_ = ci.extent;
    depthHistoryFormat_ = gbuffer.depthFormat(); // use same format as g-buffer depth
    createRenderPass(ctx);
    createSamplers(ctx);
    createHistoryBuffers(ctx, ci);
    createDepthHistoryBuffers(ctx, ci);
    createDescriptorSets(ctx);
    updateDescriptorSets(ctx, gbuffer, lighting, bloom);
    createPipeline(ctx);

    historyRead_ = 0;
    outputHistory_ = 0;
    firstFrame_ = true;
}

void TAAPass::destroy(VulkanContext& ctx)
{
    destroyPipeline(ctx);
    destroyHistoryBuffers(ctx);
    destroyDepthHistoryBuffers(ctx);
    destroyDescriptorSets(ctx);
    destroySamplers(ctx);

    pipelineLayout_.reset();
    if (renderPass_ != VK_NULL_HANDLE)
    {
        vkDestroyRenderPass(ctx.device, renderPass_, nullptr);
        renderPass_ = VK_NULL_HANDLE;
    }
}

void TAAPass::onResize(VulkanContext& ctx, const PassCreateInfo& ci, const GBufferPass& gbuffer,
                       const LightingPass& lighting, const BloomPass& bloom)
{
    extent_ = ci.extent;
    depthHistoryFormat_ = gbuffer.depthFormat(); // use same format as g-buffer depth
    destroyPipeline(ctx);
    destroyHistoryBuffers(ctx);
    destroyDepthHistoryBuffers(ctx);
    createRenderPass(ctx);
    createHistoryBuffers(ctx, ci);
    createDepthHistoryBuffers(ctx, ci);
    updateDescriptorSets(ctx, gbuffer, lighting, bloom);
    createPipeline(ctx);

    historyRead_ = 0;
    outputHistory_ = 0;
    firstFrame_ = true;
}

void TAAPass::record(VulkanContext& ctx, const FrameContext& fc, const glm::vec2& jitterOffset,
                     const glm::vec2& prevJitterOffset, float similarityThreshold,
                     float velocityScale, float blendFactorMin, float blendFactorMax,
                     float bloomIntensity, bool resetHistory, int debugMode,
                     float depthEdgeThreshold, float nearPlane, float crossFrameDepthThreshold,
                     float farPlane, float colorVarianceThreshold, float softEdgeStrength)
{
    (void)ctx;
    if (fc.frameIndex >= kMaxFramesInFlight || extent_.width == 0 || extent_.height == 0)
    {
        return;
    }

    const uint32_t srcHistory = historyRead_;
    const uint32_t dstHistory = 1 - historyRead_;

    const size_t setIndex = static_cast<size_t>(fc.frameIndex * 2 + srcHistory);
    if (setIndex >= descSets_.size())
    {
        return;
    }

    VkClearValue clear{};
    clear.color = {{0.0f, 0.0f, 0.0f, 1.0f}};

    VkRenderPassBeginInfo rp{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    rp.renderPass = renderPass_;
    rp.framebuffer = historyFramebuffers_[dstHistory].get();
    rp.renderArea.offset = {0, 0};
    rp.renderArea.extent = extent_;
    rp.clearValueCount = 1;
    rp.pClearValues = &clear;

    vkCmdBeginRenderPass(fc.cmd, &rp, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(fc.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_.get());
    vkCmdBindDescriptorSets(fc.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout_.get(), 0, 1,
                            &descSets_[setIndex], 0, nullptr);

    TAAPushConstants pc{};
    const float invWidth = extent_.width > 0 ? 1.0f / static_cast<float>(extent_.width) : 0.0f;
    const float invHeight = extent_.height > 0 ? 1.0f / static_cast<float>(extent_.height) : 0.0f;
    pc.resolution =
        glm::vec2(static_cast<float>(extent_.width), static_cast<float>(extent_.height));
    pc.texelSize = glm::vec2(invWidth, invHeight);
    pc.jitter = jitterOffset;
    pc.prevJitter = prevJitterOffset;
    pc.similarityThreshold = std::max(0.0001f, similarityThreshold);
    pc.velocityScale = std::max(0.0f, velocityScale);
    float clampedMin = std::clamp(blendFactorMin, 0.0f, 1.0f);
    float clampedMax = std::clamp(blendFactorMax, 0.0f, 1.0f);
    if (clampedMax < clampedMin)
    {
        std::swap(clampedMin, clampedMax);
    }
    pc.blendFactorMin = clampedMin;
    pc.blendFactorMax = clampedMax;
    pc.bloomIntensity = std::max(0.0f, bloomIntensity);
    pc.enabled = (resetHistory || firstFrame_ || !enabled_) ? 0 : 1;
    pc.debugMode = debugMode;
    pc.depthEdgeThreshold = std::max(0.0f, depthEdgeThreshold); // day 12E
    pc.nearPlane = std::max(0.01f, nearPlane);                  // day 12E
    pc.crossFrameDepthThreshold = std::max(0.0f, crossFrameDepthThreshold);
    pc.farPlane = std::max(1.0f, farPlane);
    pc.colorVarianceThreshold = std::max(0.0f, colorVarianceThreshold);
    pc.softEdgeStrength = std::clamp(softEdgeStrength, 0.0f, 1.0f);

    vkCmdPushConstants(fc.cmd, pipelineLayout_.get(), VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                       sizeof(TAAPushConstants), &pc);

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

    outputHistory_ = dstHistory;
    if (debugMode == 0)
    {
        historyRead_ = dstHistory;
    }
    firstFrame_ = false;
}

void TAAPass::createHistoryBuffers(VulkanContext& ctx, const PassCreateInfo& ci)
{
    if (ci.commands == nullptr)
    {
        failTAAPass("PassCreateInfo.commands is required for TAAPass history buffers.");
    }

    for (size_t i = 0; i < historyImages_.size(); ++i)
    {
        VkImageCreateInfo imageInfo{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.format = format_;
        imageInfo.extent = {extent_.width, extent_.height, 1};
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = 1;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo allocationInfo{};
        allocationInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
        constexpr const char* kHistoryNames[] = {"taa_color_history_0", "taa_color_history_1"};
        if (engine::render::createUniqueImage(ctx.memoryAllocator, imageInfo, allocationInfo,
                                              historyImages_[i], kHistoryNames[i]) != VK_SUCCESS)
        {
            failTAAPass("vmaCreateImage failed for a TAA history target.");
        }
        historyViews_[i] = engine::render::UniqueImageView(
            ctx.device, createImageView(ctx.device, historyImages_[i].get(), format_,
                                        VK_IMAGE_ASPECT_COLOR_BIT));

        transitionImageLayout(ctx, *ci.commands, historyImages_[i].get(), VK_IMAGE_ASPECT_COLOR_BIT,
                              VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

        VkImageView attachments[] = {historyViews_[i].get()};
        VkFramebufferCreateInfo fbci{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
        fbci.renderPass = renderPass_;
        fbci.attachmentCount = 1;
        fbci.pAttachments = attachments;
        fbci.width = extent_.width;
        fbci.height = extent_.height;
        fbci.layers = 1;
        VkFramebuffer framebuffer = VK_NULL_HANDLE;
        if (vkCreateFramebuffer(ctx.device, &fbci, nullptr, &framebuffer) != VK_SUCCESS)
        {
            failTAAPass("vkCreateFramebuffer failed for a TAA history target.");
        }
        historyFramebuffers_[i] = engine::render::UniqueFramebuffer(ctx.device, framebuffer);
    }
}

void TAAPass::destroyHistoryBuffers(VulkanContext& ctx)
{
    (void)ctx;
    for (size_t i = 0; i < historyImages_.size(); ++i)
    {
        historyFramebuffers_[i].reset();
        historyViews_[i].reset();
        historyImages_[i].reset();
    }
}

void TAAPass::createRenderPass(VulkanContext& ctx)
{
    if (renderPass_ != VK_NULL_HANDLE)
    {
        vkDestroyRenderPass(ctx.device, renderPass_, nullptr);
        renderPass_ = VK_NULL_HANDLE;
    }
    renderPass_ = createTaaRenderPass(ctx.device, format_);
}

void TAAPass::createPipeline(VulkanContext& ctx)
{
    if (renderPass_ == VK_NULL_HANDLE)
    {
        failTAAPass("TAAPass render pass must exist before pipeline creation.");
    }
    VkPipeline pipeline = createFullscreenPipeline(ctx, renderPass_, pipelineLayout_.get(),
                                                   fullscreenVertPath_.c_str(), fragPath_.c_str());
    pipeline_ = engine::render::UniquePipeline(ctx.device, pipeline);
}

void TAAPass::destroyPipeline(VulkanContext& ctx)
{
    (void)ctx;
    pipeline_.reset();
}

void TAAPass::createDescriptorSets(VulkanContext& ctx)
{
    constexpr uint32_t kBindingCount = 7; // added depth history for cross-frame rejection

    if (setLayout_ == VK_NULL_HANDLE)
    {
        VkDescriptorSetLayoutBinding bindings[kBindingCount]{};
        for (uint32_t i = 0; i < kBindingCount; ++i)
        {
            bindings[i].binding = i;
            bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            bindings[i].descriptorCount = 1;
            bindings[i].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        }

        VkDescriptorSetLayoutCreateInfo dlci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        dlci.bindingCount = kBindingCount;
        dlci.pBindings = bindings;
        if (!ctx.descriptorLayoutCache.get(ctx.device, dlci, setLayout_))
        {
            failTAAPass("vkCreateDescriptorSetLayout failed for TAA sampled inputs.");
        }

        VkPipelineLayoutCreateInfo plci{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        plci.setLayoutCount = 1;
        plci.pSetLayouts = &setLayout_;
        VkPushConstantRange pcr{};
        pcr.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        pcr.offset = 0;
        pcr.size = sizeof(TAAPushConstants);
        plci.pushConstantRangeCount = 1;
        plci.pPushConstantRanges = &pcr;
        VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
        if (vkCreatePipelineLayout(ctx.device, &plci, nullptr, &pipelineLayout) != VK_SUCCESS)
        {
            failTAAPass("vkCreatePipelineLayout failed for the TAA resolve pipeline.");
        }
        pipelineLayout_ = engine::render::UniquePipelineLayout(ctx.device, pipelineLayout);
    }

    const uint32_t setCount = kMaxFramesInFlight * 2;

    VkDescriptorPoolSize poolSize{};
    poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSize.descriptorCount = setCount * kBindingCount;

    std::vector<VkDescriptorSetLayout> layouts(setCount, setLayout_);
    descSets_.resize(setCount);
    const std::span<const VkDescriptorPoolSize> poolSizesView(&poolSize, 1);
    if (!ctx.descriptorAllocator.allocate(ctx.device, poolSizesView, setCount, layouts.data(),
                                          setCount, descSets_.data()))
    {
        failTAAPass("Failed to allocate TAA descriptor sets.");
    }
}

void TAAPass::destroyDescriptorSets(VulkanContext& ctx)
{
    (void)ctx;
    setLayout_ = VK_NULL_HANDLE;
    descSets_.clear();
}

void TAAPass::updateDescriptorSets(VulkanContext& ctx, const GBufferPass& gbuffer,
                                   const LightingPass& lighting, const BloomPass& bloom)
{
    if (descSets_.empty())
    {
        return;
    }

    for (uint32_t frameIndex = 0; frameIndex < kMaxFramesInFlight; ++frameIndex)
    {
        for (uint32_t historyIndex = 0; historyIndex < 2; ++historyIndex)
        {
            const size_t setIndex = static_cast<size_t>(frameIndex * 2 + historyIndex);
            if (setIndex >= descSets_.size())
            {
                continue;
            }

            VkDescriptorImageInfo infos[7]{};
            infos[0].sampler = linearSampler_;
            infos[0].imageView = lighting.lit(frameIndex).view;
            infos[0].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

            infos[1].sampler = linearSampler_;
            infos[1].imageView = historyViews_[historyIndex].get();
            infos[1].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

            infos[2].sampler = nearestSampler_;
            infos[2].imageView = gbuffer.color(frameIndex, GBufferPass::Slot::Velocity).view;
            infos[2].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

            infos[3].sampler = nearestSampler_;
            infos[3].imageView = gbuffer.depth(frameIndex).view;
            infos[3].imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;

            infos[4].sampler = linearSampler_;
            infos[4].imageView = bloom.blur(frameIndex).view.get();
            infos[4].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

            infos[5].sampler = nearestSampler_;
            infos[5].imageView = gbuffer.color(frameIndex, GBufferPass::Slot::Normal).view;
            infos[5].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

            // binding 6: depth history (previous frame's depth for cross-frame
            // rejection)
            infos[6].sampler = nearestSampler_;
            infos[6].imageView = depthHistoryViews_[historyIndex].get();
            infos[6].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

            VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
            w.dstSet = descSets_[setIndex];
            w.dstBinding = 0;
            w.descriptorCount = 7;
            w.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            w.pImageInfo = infos;

            vkUpdateDescriptorSets(ctx.device, 1, &w, 0, nullptr);
        }
    }
}

void TAAPass::createSamplers(VulkanContext& ctx)
{
    if (linearSampler_ != VK_NULL_HANDLE || nearestSampler_ != VK_NULL_HANDLE)
    {
        return;
    }

    if (!ctx.samplerCache.get(ctx.device, engine::render::SamplerPreset::ClampLinear,
                              linearSampler_))
    {
        failTAAPass("vkCreateSampler failed for the TAA linear sampler.");
    }

    if (!ctx.samplerCache.get(ctx.device, engine::render::SamplerPreset::ClampNearest,
                              nearestSampler_))
    {
        failTAAPass("vkCreateSampler failed for the TAA nearest sampler.");
    }
}

void TAAPass::destroySamplers(VulkanContext& ctx)
{
    (void)ctx;
    linearSampler_ = VK_NULL_HANDLE;
    nearestSampler_ = VK_NULL_HANDLE;
}

void TAAPass::createDepthHistoryBuffers(VulkanContext& ctx, const PassCreateInfo& ci)
{
    if (ci.commands == nullptr)
    {
        failTAAPass("PassCreateInfo.commands is required for TAA depth history buffers.");
    }

    for (size_t i = 0; i < depthHistoryImages_.size(); ++i)
    {
        // use same depth format as g-buffer for direct copy compatibility
        VkImageCreateInfo ici{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        ici.imageType = VK_IMAGE_TYPE_2D;
        ici.format = depthHistoryFormat_;
        ici.extent = {extent_.width, extent_.height, 1};
        ici.mipLevels = 1;
        ici.arrayLayers = 1;
        ici.samples = VK_SAMPLE_COUNT_1_BIT;
        ici.tiling = VK_IMAGE_TILING_OPTIMAL;
        ici.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        ici.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

        VmaAllocationCreateInfo allocationInfo{};
        allocationInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
        constexpr const char* kDepthHistoryNames[] = {"taa_depth_history_0", "taa_depth_history_1"};
        if (engine::render::createUniqueImage(ctx.memoryAllocator, ici, allocationInfo,
                                              depthHistoryImages_[i],
                                              kDepthHistoryNames[i]) != VK_SUCCESS)
        {
            failTAAPass("vmaCreateImage failed for a TAA depth history image.");
        }

        depthHistoryViews_[i] = engine::render::UniqueImageView(
            ctx.device, createImageView(ctx.device, depthHistoryImages_[i].get(),
                                        depthHistoryFormat_, VK_IMAGE_ASPECT_DEPTH_BIT));

        transitionImageLayout(ctx, *ci.commands, depthHistoryImages_[i].get(),
                              VK_IMAGE_ASPECT_DEPTH_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                              VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }
}

void TAAPass::destroyDepthHistoryBuffers(VulkanContext& ctx)
{
    (void)ctx;
    for (size_t i = 0; i < depthHistoryImages_.size(); ++i)
    {
        depthHistoryViews_[i].reset();
        depthHistoryImages_[i].reset();
    }
}
