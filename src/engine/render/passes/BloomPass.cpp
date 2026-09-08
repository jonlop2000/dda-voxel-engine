#include "engine/render/passes/BloomPass.h"

#include <algorithm>
#include <array>
#include <span>

#include "engine/render/passes/LightingPass.h"
#include "engine/render/passes/PassCreateInfo.h"
#include "engine/render/Commands.h"
#include "engine/render/FrameContext.h"
#include "engine/render/Utils.h"
#include "engine/render/VulkanContext.h"
#include "Resources/GpuImage.h"
#include "Resources/ShaderModule.h"
#include "engine/render/gpu/PipelineBuilder.h"

namespace
{
struct BloomExtractPushConstants
{
    float threshold = 1.2f;
    float knee = 0.5f;
    float padding0 = 0.0f;
    float padding1 = 0.0f;
};

struct BloomBlurPushConstants
{
    float dirX = 1.0f;
    float dirY = 0.0f;
    float sigma = 2.0f;
    float padding = 0.0f;
};

constexpr const char* kBloomPassSubsystem = "BloomPass";
constexpr const char* kBloomPassRationale =
    "BloomPass is required for the current post-processing chain consumed by TAA and final "
    "composite.";

[[noreturn]] void failBloomPass(std::string_view detail)
{
    logAndExit(kBloomPassSubsystem,
               std::string("Required pass failure: ") + std::string(detail) + " " +
                   kBloomPassRationale);
}

std::vector<char> loadBloomShaderOrFatal(const char* path, std::string_view stage)
{
    std::vector<char> code;
    std::string error;
    if (!tryReadFile(path, code, &error))
    {
        failBloomPass(std::string("Failed to read ") + std::string(stage) + " shader '" + path +
                      "': " + error);
    }
    return code;
}

VkShaderModule createBloomShaderModuleOrFatal(VkDevice device, const std::vector<char>& code,
                                              const char* path, std::string_view stage)
{
    std::string error;
    VkShaderModule module = tryCreateShaderModule(device, code, &error);
    if (module == VK_NULL_HANDLE)
    {
        failBloomPass(std::string("Failed to create ") + std::string(stage) +
                      " shader module for '" + path + "': " +
                      (error.empty() ? "vkCreateShaderModule failed." : error));
    }
    return module;
}

VkRenderPass createBloomRenderPass(VkDevice device, VkFormat colorFormat)
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
        failBloomPass("vkCreateRenderPass failed for bloom extract/blur targets.");
    }
    return rp;
}

engine::render::UniquePipeline createFullscreenPipeline(VulkanContext& ctx,
                                                        VkRenderPass renderPass,
                                                        VkPipelineLayout pipelineLayout,
                                                        const char* vertPath,
                                                        const char* fragPath)
{
    const std::vector<char> vertCode = loadBloomShaderOrFatal(vertPath, "vertex");
    const std::vector<char> fragCode = loadBloomShaderOrFatal(fragPath, "fragment");

    VkShaderModule vertModule =
        createBloomShaderModuleOrFatal(ctx.device, vertCode, vertPath, "vertex");
    VkShaderModule fragModule =
        createBloomShaderModuleOrFatal(ctx.device, fragCode, fragPath, "fragment");

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

    engine::render::UniquePipeline pipeline =
        engine::render::PipelineBuilder{}.graphics(pci).createGraphics(ctx.device);
    if (!pipeline)
    {
        failBloomPass("vkCreateGraphicsPipelines failed for bloom fullscreen pipelines.");
    }

    vkDestroyShaderModule(ctx.device, fragModule, nullptr);
    vkDestroyShaderModule(ctx.device, vertModule, nullptr);

    return pipeline;
}
} // namespace

void BloomPass::setShaderPaths(const std::string& fullscreenVert, const std::string& extractFrag,
                               const std::string& blurFrag)
{
    fullscreenVertPath_ = fullscreenVert;
    extractFragPath_ = extractFrag;
    blurFragPath_ = blurFrag;
}

void BloomPass::create(VulkanContext& ctx, const PassCreateInfo& ci, const LightingPass& lighting)
{
    if (ci.commands == nullptr)
    {
        failBloomPass("PassCreateInfo.commands is required for bloom target transitions.");
    }

    extent_ = ci.extent;
    halfExtent_.width = std::max(1u, extent_.width / 2);
    halfExtent_.height = std::max(1u, extent_.height / 2);

    VkDescriptorSetLayoutBinding texBinding{};
    texBinding.binding = 0;
    texBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    texBinding.descriptorCount = 1;
    texBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo dlci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    dlci.bindingCount = 1;
    dlci.pBindings = &texBinding;
    if (!ctx.descriptorLayoutCache.get(ctx.device, dlci, texSetLayout_))
    {
        failBloomPass("vkCreateDescriptorSetLayout failed for bloom texture bindings.");
    }

    VkPipelineLayoutCreateInfo plci{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    plci.setLayoutCount = 1;
    plci.pSetLayouts = &texSetLayout_;

    VkPushConstantRange extractRange{};
    extractRange.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    extractRange.offset = 0;
    extractRange.size = sizeof(BloomExtractPushConstants);
    plci.pushConstantRangeCount = 1;
    plci.pPushConstantRanges = &extractRange;
    VkPipelineLayout extractLayout = VK_NULL_HANDLE;
    if (vkCreatePipelineLayout(ctx.device, &plci, nullptr, &extractLayout) != VK_SUCCESS)
    {
        failBloomPass("vkCreatePipelineLayout failed for bloom extract.");
    }
    extractPipelineLayout_ = engine::render::UniquePipelineLayout(ctx.device, extractLayout);

    VkPushConstantRange blurRange{};
    blurRange.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    blurRange.offset = 0;
    blurRange.size = sizeof(BloomBlurPushConstants);
    plci.pushConstantRangeCount = 1;
    plci.pPushConstantRanges = &blurRange;
    VkPipelineLayout blurLayout = VK_NULL_HANDLE;
    if (vkCreatePipelineLayout(ctx.device, &plci, nullptr, &blurLayout) != VK_SUCCESS)
    {
        failBloomPass("vkCreatePipelineLayout failed for bloom blur.");
    }
    blurPipelineLayout_ = engine::render::UniquePipelineLayout(ctx.device, blurLayout);

    std::vector<VkDescriptorSetLayout> layouts(kMaxFramesInFlight, texSetLayout_);

    extractSets_.resize(kMaxFramesInFlight);
    blurHSets_.resize(kMaxFramesInFlight);
    blurVSets_.resize(kMaxFramesInFlight);

    std::array<VkDescriptorPoolSize, 1> poolSizes{};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[0].descriptorCount = kMaxFramesInFlight * 3;
    const std::span<const VkDescriptorPoolSize> poolSizesView(poolSizes.data(),
                                                             poolSizes.size());
    constexpr uint32_t kBloomDescriptorSetCount = kMaxFramesInFlight * 3;

    const auto allocateBloomSets = [&](std::vector<VkDescriptorSet>& sets,
                                       std::string_view label) {
        if (!ctx.descriptorAllocator.allocate(ctx.device, poolSizesView,
                                              kBloomDescriptorSetCount, layouts.data(),
                                              kMaxFramesInFlight, sets.data()))
        {
            failBloomPass(std::string("Failed to allocate bloom ") +
                          std::string(label) + " descriptors.");
        }
    };
    allocateBloomSets(extractSets_, "extract");
    allocateBloomSets(blurHSets_, "horizontal blur");
    allocateBloomSets(blurVSets_, "vertical blur");

    createTargets(ctx, ci);
    updateDescriptorSets(ctx, lighting);
    createPipeline(ctx);
}

void BloomPass::destroy(VulkanContext& ctx)
{
    destroyPipeline(ctx);
    destroyTargets(ctx);

    extractPipelineLayout_.reset();
    blurPipelineLayout_.reset();
    texSetLayout_ = VK_NULL_HANDLE;

    extractSets_.clear();
    blurHSets_.clear();
    blurVSets_.clear();
}

void BloomPass::onResize(VulkanContext& ctx, const PassCreateInfo& ci, const LightingPass& lighting)
{
    extent_ = ci.extent;
    halfExtent_.width = std::max(1u, extent_.width / 2);
    halfExtent_.height = std::max(1u, extent_.height / 2);

    destroyPipeline(ctx);
    destroyTargets(ctx);
    createTargets(ctx, ci);
    updateDescriptorSets(ctx, lighting);
    createPipeline(ctx);
}

void BloomPass::updateDescriptorSets(VulkanContext& ctx, const LightingPass& lighting)
{
    const size_t count = std::min(extractSets_.size(), static_cast<size_t>(kMaxFramesInFlight));
    for (size_t i = 0; i < count; i++)
    {
        VkDescriptorImageInfo sceneInfo{};
        sceneInfo.sampler = lighting.lit(static_cast<uint32_t>(i)).sampler;
        sceneInfo.imageView = lighting.lit(static_cast<uint32_t>(i)).view;
        sceneInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkWriteDescriptorSet wScene{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        wScene.dstSet = extractSets_[i];
        wScene.dstBinding = 0;
        wScene.descriptorCount = 1;
        wScene.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        wScene.pImageInfo = &sceneInfo;

        VkDescriptorImageInfo extractInfo{};
        extractInfo.sampler = extractTargets_[i].sampler;
        extractInfo.imageView = extractTargets_[i].view.get();
        extractInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkWriteDescriptorSet wExtract{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        wExtract.dstSet = blurHSets_[i];
        wExtract.dstBinding = 0;
        wExtract.descriptorCount = 1;
        wExtract.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        wExtract.pImageInfo = &extractInfo;

        VkDescriptorImageInfo pingInfo{};
        pingInfo.sampler = pingTargets_[i].sampler;
        pingInfo.imageView = pingTargets_[i].view.get();
        pingInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkWriteDescriptorSet wPing{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        wPing.dstSet = blurVSets_[i];
        wPing.dstBinding = 0;
        wPing.descriptorCount = 1;
        wPing.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        wPing.pImageInfo = &pingInfo;

        VkWriteDescriptorSet writes[] = {wScene, wExtract, wPing};
        vkUpdateDescriptorSets(ctx.device,
                               static_cast<uint32_t>(sizeof(writes) / sizeof(writes[0])), writes,
                               0, nullptr);
    }
}

void BloomPass::record(VulkanContext& ctx, const FrameContext& fc, const Settings& settings,
                       bool run)
{
    (void)ctx;
    if (!run || fc.frameIndex >= extractTargets_.size())
    {
        return;
    }

    recordExtract(fc, settings);
    recordBarrier(fc.cmd, extractTargets_[fc.frameIndex].image);

    const float sigma = std::max(0.1f, settings.blurSigma);
    recordBlur(fc, 1.0f, 0.0f, sigma, blurHSets_[fc.frameIndex], pingTargets_[fc.frameIndex]);
    recordBarrier(fc.cmd, pingTargets_[fc.frameIndex].image);

    recordBlur(fc, 0.0f, 1.0f, sigma, blurVSets_[fc.frameIndex], pongTargets_[fc.frameIndex]);
    recordBarrier(fc.cmd, pongTargets_[fc.frameIndex].image);
}

const BloomPass::Target& BloomPass::extract(uint32_t frameIndex) const
{
    return extractTargets_.at(frameIndex);
}

const BloomPass::Target& BloomPass::blur(uint32_t frameIndex) const
{
    return pongTargets_.at(frameIndex);
}

VkExtent2D BloomPass::extent() const
{
    return extent_;
}

VkExtent2D BloomPass::halfExtent() const
{
    return halfExtent_;
}

void BloomPass::createPipeline(VulkanContext& ctx)
{
    if (extractTargets_.empty())
    {
        failBloomPass("Bloom targets must exist before creating extract and blur pipelines.");
    }

    extractPipeline_ = createFullscreenPipeline(ctx, extractTargets_[0].renderPass.get(),
                                                extractPipelineLayout_.get(),
                                                fullscreenVertPath_.c_str(),
                                                extractFragPath_.c_str());
    blurPipeline_ = createFullscreenPipeline(ctx, pingTargets_[0].renderPass.get(),
                                             blurPipelineLayout_.get(),
                                             fullscreenVertPath_.c_str(), blurFragPath_.c_str());
}

void BloomPass::destroyPipeline(VulkanContext& ctx)
{
    (void)ctx;
    extractPipeline_.reset();
    blurPipeline_.reset();
}

void BloomPass::createTargets(VulkanContext& ctx, const PassCreateInfo& ci)
{
    if (ci.commands == nullptr)
    {
        failBloomPass("PassCreateInfo.commands is required for bloom image layout transitions.");
    }

    auto createTargetSet = [&](std::vector<Target>& targets)
    {
        targets.resize(kMaxFramesInFlight);
        for (size_t i = 0; i < kMaxFramesInFlight; ++i)
        {
            auto& t = targets[i];

            createImage(ctx, halfExtent_.width, halfExtent_.height, t.format,
                        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, t.image,
                        t.memory);
            t.view = engine::render::UniqueImageView(
                ctx.device,
                createImageView(ctx.device, t.image, t.format, VK_IMAGE_ASPECT_COLOR_BIT));

            if (!ctx.samplerCache.get(ctx.device, engine::render::SamplerPreset::ClampLinear,
                                      t.sampler))
            {
                failBloomPass("vkCreateSampler failed for bloom targets.");
            }

            transitionImageLayout(ctx, *ci.commands, t.image, VK_IMAGE_ASPECT_COLOR_BIT,
                                  VK_IMAGE_LAYOUT_UNDEFINED,
                                  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

            t.renderPass = engine::render::UniqueRenderPass(
                ctx.device, createBloomRenderPass(ctx.device, t.format));

            VkImageView attachments[] = {t.view.get()};
            VkFramebufferCreateInfo fbci{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
            fbci.renderPass = t.renderPass.get();
            fbci.attachmentCount = 1;
            fbci.pAttachments = attachments;
            fbci.width = halfExtent_.width;
            fbci.height = halfExtent_.height;
            fbci.layers = 1;
            VkFramebuffer framebuffer = VK_NULL_HANDLE;
            if (vkCreateFramebuffer(ctx.device, &fbci, nullptr, &framebuffer) != VK_SUCCESS)
            {
                failBloomPass("vkCreateFramebuffer failed for bloom targets.");
            }
            t.framebuffer = engine::render::UniqueFramebuffer(ctx.device, framebuffer);
        }
    };

    createTargetSet(extractTargets_);
    createTargetSet(pingTargets_);
    createTargetSet(pongTargets_);
}

void BloomPass::destroyTargets(VulkanContext& ctx)
{
    auto destroyTargetSet = [&](std::vector<Target>& targets)
    {
        for (auto& t : targets)
        {
            // same order as before, except cached samplers are borrowed.
            t.framebuffer.reset();
            t.renderPass.reset();
            t.sampler = VK_NULL_HANDLE;
            t.view.reset();
            if (t.image != VK_NULL_HANDLE)
            {
                vkDestroyImage(ctx.device, t.image, nullptr);
                t.image = VK_NULL_HANDLE;
            }
            if (t.memory != VK_NULL_HANDLE)
            {
                vkFreeMemory(ctx.device, t.memory, nullptr);
                t.memory = VK_NULL_HANDLE;
            }
        }
        targets.clear();
    };

    destroyTargetSet(extractTargets_);
    destroyTargetSet(pingTargets_);
    destroyTargetSet(pongTargets_);
}

void BloomPass::recordExtract(const FrameContext& fc, const Settings& settings)
{
    VkClearValue clear{};
    clear.color = {{0.0f, 0.0f, 0.0f, 1.0f}};

    const Target& target = extractTargets_[fc.frameIndex];
    VkRenderPassBeginInfo rp{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    rp.renderPass = target.renderPass.get();
    rp.framebuffer = target.framebuffer.get();
    rp.renderArea.offset = {0, 0};
    rp.renderArea.extent = halfExtent_;
    rp.clearValueCount = 1;
    rp.pClearValues = &clear;

    vkCmdBeginRenderPass(fc.cmd, &rp, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(fc.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, extractPipeline_.get());
    vkCmdBindDescriptorSets(fc.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, extractPipelineLayout_.get(), 0,
                            1, &extractSets_[fc.frameIndex], 0, nullptr);

    BloomExtractPushConstants pc{};
    pc.threshold = std::max(0.0f, settings.threshold);
    pc.knee = std::max(0.0f, settings.knee);
    vkCmdPushConstants(fc.cmd, extractPipelineLayout_.get(), VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                       sizeof(pc), &pc);

    VkViewport vp{};
    vp.x = 0.0f;
    vp.y = 0.0f;
    vp.width = static_cast<float>(halfExtent_.width);
    vp.height = static_cast<float>(halfExtent_.height);
    vp.minDepth = 0.0f;
    vp.maxDepth = 1.0f;
    VkRect2D scissor{{0, 0}, halfExtent_};
    vkCmdSetViewport(fc.cmd, 0, 1, &vp);
    vkCmdSetScissor(fc.cmd, 0, 1, &scissor);
    vkCmdDraw(fc.cmd, 3, 1, 0, 0);
    vkCmdEndRenderPass(fc.cmd);
}

void BloomPass::recordBlur(const FrameContext& fc, float dirX, float dirY, float sigma,
                           VkDescriptorSet set, const Target& target)
{
    VkClearValue clear{};
    clear.color = {{0.0f, 0.0f, 0.0f, 1.0f}};

    VkRenderPassBeginInfo rp{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    rp.renderPass = target.renderPass.get();
    rp.framebuffer = target.framebuffer.get();
    rp.renderArea.offset = {0, 0};
    rp.renderArea.extent = halfExtent_;
    rp.clearValueCount = 1;
    rp.pClearValues = &clear;

    vkCmdBeginRenderPass(fc.cmd, &rp, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(fc.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, blurPipeline_.get());
    vkCmdBindDescriptorSets(fc.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, blurPipelineLayout_.get(), 0, 1,
                            &set, 0, nullptr);

    BloomBlurPushConstants pc{};
    pc.dirX = dirX;
    pc.dirY = dirY;
    pc.sigma = sigma;
    vkCmdPushConstants(fc.cmd, blurPipelineLayout_.get(), VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                       sizeof(pc), &pc);

    VkViewport vp{};
    vp.x = 0.0f;
    vp.y = 0.0f;
    vp.width = static_cast<float>(halfExtent_.width);
    vp.height = static_cast<float>(halfExtent_.height);
    vp.minDepth = 0.0f;
    vp.maxDepth = 1.0f;
    VkRect2D scissor{{0, 0}, halfExtent_};
    vkCmdSetViewport(fc.cmd, 0, 1, &vp);
    vkCmdSetScissor(fc.cmd, 0, 1, &scissor);
    vkCmdDraw(fc.cmd, 3, 1, 0, 0);
    vkCmdEndRenderPass(fc.cmd);
}

void BloomPass::recordBarrier(VkCommandBuffer cmd, VkImage image)
{
    VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    barrier.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;

    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1,
                         &barrier);
}
