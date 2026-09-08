#include "engine/render/passes/LightingPass.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <span>

#include "engine/render/passes/GBufferPass.h"
#include "engine/render/passes/PassCreateInfo.h"
#include "engine/render/passes/ShadowBuffer.h"
#include "engine/render/passes/ShadowPass.h"
#include "engine/render/Commands.h"
#include "engine/render/FrameContext.h"
#include "engine/render/Utils.h"
#include "engine/render/VulkanContext.h"
#include "Resources/LightsBuffer.h"
#include "Resources/GpuBuffer.h"
#include "Resources/GpuImage.h"
#include "Resources/ShaderModule.h"

namespace
{
struct LightingPushConstants
{
    int lightCount;
    int debugMode;
    int useDDAShadows;      // day 11
    int debugShadowMode;    // day 11
    int csmDitherEnabled;   // csm dither when taa is active
    int csmEnabled;         // csm shadow toggle
    int aoEnabled;          // ao contribution toggle
    float aoContribution;   // hemisphere ambient strength (legacy field name)
    float terminatorSoftness; // day 12C
    int terminatorMode;       // 0 = off, 1 = wrap, 2 = smoothstep
    int waterVolumeCount;     // day 18
    int localShadowSlotCount; // day 22
    int localShadowLightIndices[4]; // day 22: slot -> light index map (-1 unused)
    float skyColorR;          // Sky/ambient color rgb
    float skyColorG;
    float skyColorB;
    float pixelShadowStyleStrength;
    int voxelCellVariationEnabled;
    float hemisphereAmbientStrength;
    float hemisphereSkyTintR;
    float hemisphereSkyTintG;
    float hemisphereSkyTintB;
    float hemisphereGroundTintR;
    float hemisphereGroundTintG;
    float hemisphereGroundTintB;
    float projectionJitterUvX;
    float projectionJitterUvY;
};
static_assert(sizeof(LightingPushConstants) == 120);

constexpr const char* kLightingPassSubsystem = "LightingPass";
constexpr const char* kLightingPassRationale =
    "LightingPass is required for the main HDR lighting buffer used by reflection, water, "
    "glass, bloom, TAA, and final composite.";

[[noreturn]] void failLightingPass(std::string_view detail)
{
    logAndExit(kLightingPassSubsystem,
               std::string("Required pass failure: ") + std::string(detail) + " " +
                   kLightingPassRationale);
}

std::vector<char> loadLightingShaderOrFatal(const char* path, std::string_view stage)
{
    std::vector<char> code;
    std::string error;
    if (!tryReadFile(path, code, &error))
    {
        failLightingPass(std::string("Failed to read ") + std::string(stage) + " shader '" +
                         path + "': " + error);
    }
    return code;
}

VkShaderModule createLightingShaderModuleOrFatal(VkDevice device, const std::vector<char>& code,
                                                 const char* path, std::string_view stage)
{
    std::string error;
    VkShaderModule module = tryCreateShaderModule(device, code, &error);
    if (module == VK_NULL_HANDLE)
    {
        failLightingPass(std::string("Failed to create ") + std::string(stage) +
                         " shader module for '" + path + "': " +
                         (error.empty() ? "vkCreateShaderModule failed." : error));
    }
    return module;
}

VkRenderPass createLightingRenderPass(VkDevice device, VkFormat colorFormat)
{
    VkAttachmentDescription color{};
    color.format = colorFormat;
    color.samples = VK_SAMPLE_COUNT_1_BIT;
    color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    color.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    // keep output shader-readable for composite.
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
    dep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.srcAccessMask = 0;
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
        failLightingPass("vkCreateRenderPass failed for the HDR lighting render pass.");
    }
    return rp;
}

VkPipeline createFullscreenPipeline(VulkanContext& ctx, VkRenderPass renderPass,
                                    VkPipelineLayout pipelineLayout, const char* vertPath,
                                    const char* fragPath)
{
    const std::vector<char> vertCode = loadLightingShaderOrFatal(vertPath, "vertex");
    const std::vector<char> fragCode = loadLightingShaderOrFatal(fragPath, "fragment");

    VkShaderModule vertModule =
        createLightingShaderModuleOrFatal(ctx.device, vertCode, vertPath, "vertex");
    VkShaderModule fragModule =
        createLightingShaderModuleOrFatal(ctx.device, fragCode, fragPath, "fragment");

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
    pci.pColorBlendState = &blend;
    pci.pDynamicState = &dynamic;
    pci.layout = pipelineLayout;
    pci.renderPass = renderPass;
    pci.subpass = 0;

    VkPipeline pipeline = VK_NULL_HANDLE;
    if (vkCreateGraphicsPipelines(ctx.device, VK_NULL_HANDLE, 1, &pci, nullptr, &pipeline) !=
        VK_SUCCESS)
    {
        failLightingPass("vkCreateGraphicsPipelines failed for the fullscreen lighting pipeline.");
    }

    vkDestroyShaderModule(ctx.device, fragModule, nullptr);
    vkDestroyShaderModule(ctx.device, vertModule, nullptr);

    return pipeline;
}
} // namespace

void LightingPass::setShaderPaths(const std::string& fullscreenVert, const std::string& fragPath)
{
    fullscreenVertPath_ = fullscreenVert;
    fragPath_ = fragPath;
}

void LightingPass::setLightParams(uint32_t lightCount, int debugMode)
{
    lightCount_ = static_cast<int>(lightCount);
    debugMode_ = debugMode;
}

void LightingPass::setWaterVolumes(VkBuffer buffer, uint32_t count)
{
    waterVolumeBuffer_ = buffer;
    waterVolumeCount_ = count;
}

void LightingPass::setWaterVolumeCount(uint32_t count)
{
    waterVolumeCount_ = count;
}

void LightingPass::create(VulkanContext& ctx, const PassCreateInfo& ci, const GBufferPass& gbuffer,
                          const LightsBuffer& lights, const ShadowMap& shadowMap,
                          ShadowBuffer& shadowBuffer, VkImageView shadowResolvedView,
                          VkSampler shadowResolvedSampler, VkImageView aoResolvedView,
                          VkSampler aoResolvedSampler, VkImageView aoRawView,
                          VkSampler aoRawSampler, VkImageView localShadowResolvedView,
                          VkSampler localShadowResolvedSampler)
{
    if (ci.commands == nullptr)
    {
        failLightingPass(
            "PassCreateInfo.commands is required for swapchain-dependent target transitions.");
    }

    extent_ = ci.extent;
    lights_ = &lights;
    shadowMap_ = &shadowMap;
    shadowBuffer_ = &shadowBuffer;
    if (waterVolumeBuffer_ == VK_NULL_HANDLE)
    {
        failLightingPass("A valid water volume buffer is required for lighting descriptor setup.");
    }

    // set 0: g-buffer + depth textures + shadow buffers (day 12) + water distance (day 18).
    constexpr uint32_t kGBufferBindings = 4 + kShadowCascades + 4 + 1 + 1;
    VkDescriptorSetLayoutBinding gbindings[kGBufferBindings]{};
    for (uint32_t i = 0; i < kGBufferBindings; i++)
    {
        gbindings[i].binding = i;
        gbindings[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        gbindings[i].descriptorCount = 1;
        gbindings[i].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    }
    for (uint32_t cascade = 0; cascade < kShadowCascades; ++cascade)
    {
        gbindings[4 + cascade].pImmutableSamplers = &shadowMap.sampler;
    }

    VkDescriptorSetLayoutCreateInfo glci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    glci.bindingCount = kGBufferBindings;
    glci.pBindings = gbindings;
    if (!ctx.descriptorLayoutCache.get(ctx.device, glci, gbufferSetLayout_))
    {
        failLightingPass("vkCreateDescriptorSetLayout failed for lighting G-buffer bindings.");
    }

    // set 1: lighting ubo + light list + water volumes ssbo.
    VkDescriptorSetLayoutBinding set1Bindings[3]{};
    set1Bindings[0].binding = 0;
    set1Bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    set1Bindings[0].descriptorCount = 1;
    set1Bindings[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    set1Bindings[1].binding = 1;
    set1Bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    set1Bindings[1].descriptorCount = 1;
    set1Bindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    set1Bindings[2].binding = 2;
    set1Bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    set1Bindings[2].descriptorCount = 1;
    set1Bindings[2].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo ulci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    ulci.bindingCount = 3;
    ulci.pBindings = set1Bindings;
    if (!ctx.descriptorLayoutCache.get(ctx.device, ulci, uboSetLayout_))
    {
        failLightingPass("vkCreateDescriptorSetLayout failed for lighting frame/light bindings.");
    }

    VkDescriptorSetLayout setLayouts[] = {gbufferSetLayout_, uboSetLayout_};
    VkPipelineLayoutCreateInfo plci{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    plci.setLayoutCount = 2;
    plci.pSetLayouts = setLayouts;
    // push constants for light count + debug mode + shadow params.
    VkPushConstantRange pcr{};
    pcr.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    pcr.offset = 0;
    pcr.size = sizeof(LightingPushConstants);
    plci.pushConstantRangeCount = 1;
    plci.pPushConstantRanges = &pcr;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    if (vkCreatePipelineLayout(ctx.device, &plci, nullptr, &pipelineLayout) != VK_SUCCESS)
    {
        failLightingPass("vkCreatePipelineLayout failed for lighting pipelines.");
    }
    pipelineLayout_ = engine::render::UniquePipelineLayout(ctx.device, pipelineLayout);

    VkDescriptorPoolSize poolSizes[3]{};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[0].descriptorCount = kMaxFramesInFlight * kGBufferBindings;
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSizes[1].descriptorCount = kMaxFramesInFlight;
    poolSizes[2].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    poolSizes[2].descriptorCount = kMaxFramesInFlight * 2;

    gbufferSets_.resize(kMaxFramesInFlight);
    lightSets_.resize(kMaxFramesInFlight);

    const std::span<const VkDescriptorPoolSize> poolSizesView(poolSizes, 3);
    constexpr uint32_t kMaxDescriptorSets = kMaxFramesInFlight * 2;

    std::vector<VkDescriptorSetLayout> gLayouts(kMaxFramesInFlight, gbufferSetLayout_);
    if (!ctx.descriptorAllocator.allocate(ctx.device, poolSizesView, kMaxDescriptorSets,
                                          gLayouts.data(), kMaxFramesInFlight,
                                          gbufferSets_.data()))
    {
        failLightingPass("Failed to allocate lighting G-buffer descriptors.");
    }

    std::vector<VkDescriptorSetLayout> uLayouts(kMaxFramesInFlight, uboSetLayout_);
    if (!ctx.descriptorAllocator.allocate(ctx.device, poolSizesView, kMaxDescriptorSets,
                                          uLayouts.data(), kMaxFramesInFlight, lightSets_.data()))
    {
        failLightingPass("Failed to allocate lighting frame/light descriptors.");
    }

    ubos_.resize(kMaxFramesInFlight);
    for (size_t i = 0; i < kMaxFramesInFlight; i++)
    {
        createBuffer(ctx, sizeof(FrameUbo), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                     ubos_[i].buffer, ubos_[i].memory);

        if (vkMapMemory(ctx.device, ubos_[i].memory, 0, sizeof(FrameUbo), 0,
                        &ubos_[i].mapped) != VK_SUCCESS)
        {
            failLightingPass("vkMapMemory failed for lighting frame uniform buffers.");
        }

        VkDescriptorBufferInfo bi{};
        bi.buffer = ubos_[i].buffer;
        bi.offset = 0;
        bi.range = sizeof(FrameUbo);

        VkDescriptorBufferInfo sbo{};
        sbo.buffer = lights.buffer;
        sbo.offset = 0;
        sbo.range = sizeof(AreaLightGpu) * lights.maxLights;

        VkDescriptorBufferInfo waterSbo{};
        waterSbo.buffer = waterVolumeBuffer_;
        waterSbo.offset = 0;
        waterSbo.range = VK_WHOLE_SIZE;

        VkWriteDescriptorSet writes[3]{};
        VkWriteDescriptorSet w0{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        w0.dstSet = lightSets_[i];
        w0.dstBinding = 0;
        w0.descriptorCount = 1;
        w0.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        w0.pBufferInfo = &bi;
        writes[0] = w0;

        VkWriteDescriptorSet w1{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        w1.dstSet = lightSets_[i];
        w1.dstBinding = 1;
        w1.descriptorCount = 1;
        w1.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        w1.pBufferInfo = &sbo;
        writes[1] = w1;

        VkWriteDescriptorSet w2{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        w2.dstSet = lightSets_[i];
        w2.dstBinding = 2;
        w2.descriptorCount = 1;
        w2.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        w2.pBufferInfo = &waterSbo;
        writes[2] = w2;

        vkUpdateDescriptorSets(ctx.device, 3, writes, 0, nullptr);
    }

    createTargets(ctx, ci);
    updateDescriptorSets(ctx, gbuffer, lights, shadowMap, shadowBuffer, shadowResolvedView,
                         shadowResolvedSampler, aoResolvedView, aoResolvedSampler, aoRawView,
                         aoRawSampler, localShadowResolvedView, localShadowResolvedSampler);
    createPipeline(ctx);
}

void LightingPass::destroy(VulkanContext& ctx)
{
    destroyPipeline(ctx);
    destroyTargets(ctx);

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
        }
        if (ubo.memory != VK_NULL_HANDLE)
        {
            vkFreeMemory(ctx.device, ubo.memory, nullptr);
        }
    }
    ubos_.clear();

    gbufferSets_.clear();
    lightSets_.clear();
    gbufferSetLayout_ = VK_NULL_HANDLE;
    uboSetLayout_ = VK_NULL_HANDLE;
    pipelineLayout_.reset();
    lights_ = nullptr;
    shadowMap_ = nullptr;
}

void LightingPass::onResize(VulkanContext& ctx, const PassCreateInfo& ci,
                            const GBufferPass& gbuffer, const ShadowMap& shadowMap,
                            ShadowBuffer& shadowBuffer, VkImageView shadowResolvedView,
                            VkSampler shadowResolvedSampler, VkImageView aoResolvedView,
                            VkSampler aoResolvedSampler, VkImageView aoRawView,
                            VkSampler aoRawSampler, VkImageView localShadowResolvedView,
                            VkSampler localShadowResolvedSampler)
{
    extent_ = ci.extent;
    destroyPipeline(ctx);
    destroyTargets(ctx);
    createTargets(ctx, ci);
    shadowMap_ = &shadowMap;
    shadowBuffer_ = &shadowBuffer;
    if (lights_ != nullptr)
    {
        updateDescriptorSets(ctx, gbuffer, *lights_, shadowMap, shadowBuffer, shadowResolvedView,
                             shadowResolvedSampler, aoResolvedView, aoResolvedSampler, aoRawView,
                             aoRawSampler, localShadowResolvedView,
                             localShadowResolvedSampler);
    }
    createPipeline(ctx);
}

void LightingPass::record(VulkanContext& ctx, const FrameContext& fc, const FrameUbo& frameUbo,
                          int useDDAShadows, int debugShadowMode, int csmDitherEnabled,
                          int csmEnabled, int aoEnabled, float aoContribution,
                          float terminatorSoftness, int terminatorMode, int localShadowSlotCount,
                          const std::array<int32_t, 4>& localShadowLightIndices,
                          float pixelShadowStyleStrength,
                          int voxelCellVariationEnabled,
                          const engine::render::HemisphereAmbientSettings& hemisphereAmbient,
                          const glm::vec3& skyColor,
                          const glm::vec2& projectionJitterUv)
{
    (void)ctx;
    if (fc.frameIndex >= targets_.size())
    {
        return;
    }

    updateUbo(fc.frameIndex, frameUbo);

    VkClearValue litClear{};
    litClear.color = {{0.0f, 0.0f, 0.0f, 1.0f}};

    const LitTarget& lit = targets_[fc.frameIndex];
    VkRenderPassBeginInfo litRp{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    litRp.renderPass = lit.renderPass;
    litRp.framebuffer = lit.framebuffer;
    litRp.renderArea.offset = {0, 0};
    litRp.renderArea.extent = extent_;
    litRp.clearValueCount = 1;
    litRp.pClearValues = &litClear;

    vkCmdBeginRenderPass(fc.cmd, &litRp, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(fc.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_.get());
    // bind g-buffer + lighting inputs.
    VkDescriptorSet lightingSets[] = {gbufferSets_[fc.frameIndex], lightSets_[fc.frameIndex]};
    vkCmdBindDescriptorSets(fc.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout_.get(), 0, 2,
                            lightingSets, 0, nullptr);
    LightingPushConstants pc{lightCount_, debugMode_, useDDAShadows, debugShadowMode,
                             csmDitherEnabled, csmEnabled, aoEnabled, aoContribution,
                             terminatorSoftness, terminatorMode,
                             static_cast<int>(waterVolumeCount_), localShadowSlotCount,
                             {localShadowLightIndices[0], localShadowLightIndices[1],
                              localShadowLightIndices[2], localShadowLightIndices[3]},
                             skyColor.r, skyColor.g, skyColor.b, pixelShadowStyleStrength,
                             voxelCellVariationEnabled,
                             hemisphereAmbient.strength,
                             hemisphereAmbient.skyTint.r,
                             hemisphereAmbient.skyTint.g,
                             hemisphereAmbient.skyTint.b,
                             hemisphereAmbient.groundTint.r,
                             hemisphereAmbient.groundTint.g,
                             hemisphereAmbient.groundTint.b,
                             projectionJitterUv.x,
                             projectionJitterUv.y};
    vkCmdPushConstants(fc.cmd, pipelineLayout_.get(), VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc),
                       &pc);

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
    // full-screen triangle.
    vkCmdDraw(fc.cmd, 3, 1, 0, 0);
    vkCmdEndRenderPass(fc.cmd);
}

void LightingPass::updateShadowBindings(VulkanContext& ctx, uint32_t frameIndex,
                                        ShadowBuffer& shadowBuffer, VkImageView shadowResolvedView,
                                        VkSampler shadowResolvedSampler, VkImageView aoResolvedView,
                                        VkSampler aoResolvedSampler, VkImageView aoRawView,
                                        VkSampler aoRawSampler,
                                        VkImageView localShadowResolvedView,
                                        VkSampler localShadowResolvedSampler)
{
    if (frameIndex >= gbufferSets_.size())
    {
        return;
    }

    constexpr uint32_t shadowResolvedBinding = 4 + kShadowCascades;
    constexpr uint32_t shadowRawBinding = shadowResolvedBinding + 1;
    constexpr uint32_t aoResolvedBinding = shadowRawBinding + 1;
    constexpr uint32_t aoRawBinding = aoResolvedBinding + 1;
    constexpr uint32_t localShadowResolvedBinding = aoRawBinding + 2;

    VkDescriptorImageInfo resolvedInfo{};
    resolvedInfo.sampler = shadowResolvedSampler;
    resolvedInfo.imageView = shadowResolvedView;
    resolvedInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

    VkDescriptorImageInfo rawInfo{};
    rawInfo.sampler = shadowBuffer.getSampler();
    rawInfo.imageView = shadowBuffer.getCurrentImageView();
    rawInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

    VkDescriptorImageInfo aoResolvedInfo{};
    aoResolvedInfo.sampler = aoResolvedSampler;
    aoResolvedInfo.imageView = aoResolvedView;
    aoResolvedInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

    VkDescriptorImageInfo aoRawInfo{};
    aoRawInfo.sampler = aoRawSampler;
    aoRawInfo.imageView = aoRawView;
    aoRawInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

    VkDescriptorImageInfo localShadowResolvedInfo{};
    localShadowResolvedInfo.sampler = localShadowResolvedSampler;
    localShadowResolvedInfo.imageView = localShadowResolvedView;
    localShadowResolvedInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

    VkWriteDescriptorSet writes[5]{};
    VkWriteDescriptorSet w0{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    w0.dstSet = gbufferSets_[frameIndex];
    w0.dstBinding = shadowResolvedBinding;
    w0.descriptorCount = 1;
    w0.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    w0.pImageInfo = &resolvedInfo;
    writes[0] = w0;

    VkWriteDescriptorSet w1{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    w1.dstSet = gbufferSets_[frameIndex];
    w1.dstBinding = shadowRawBinding;
    w1.descriptorCount = 1;
    w1.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    w1.pImageInfo = &rawInfo;
    writes[1] = w1;

    VkWriteDescriptorSet w2{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    w2.dstSet = gbufferSets_[frameIndex];
    w2.dstBinding = aoResolvedBinding;
    w2.descriptorCount = 1;
    w2.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    w2.pImageInfo = &aoResolvedInfo;
    writes[2] = w2;

    VkWriteDescriptorSet w3{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    w3.dstSet = gbufferSets_[frameIndex];
    w3.dstBinding = aoRawBinding;
    w3.descriptorCount = 1;
    w3.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    w3.pImageInfo = &aoRawInfo;
    writes[3] = w3;

    VkWriteDescriptorSet w4{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    w4.dstSet = gbufferSets_[frameIndex];
    w4.dstBinding = localShadowResolvedBinding;
    w4.descriptorCount = 1;
    w4.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    w4.pImageInfo = &localShadowResolvedInfo;
    writes[4] = w4;

    vkUpdateDescriptorSets(ctx.device, 5, writes, 0, nullptr);
}

const LightingPass::LitTarget& LightingPass::lit(uint32_t frameIndex) const
{
    return targets_.at(frameIndex);
}

VkRenderPass LightingPass::renderPass() const
{
    if (targets_.empty())
    {
        return VK_NULL_HANDLE;
    }
    return targets_[0].renderPass;
}

VkExtent2D LightingPass::extent() const
{
    return extent_;
}

void LightingPass::createPipeline(VulkanContext& ctx)
{
    if (targets_.empty())
    {
        failLightingPass("Lighting targets must exist before creating the fullscreen pipeline.");
    }
    VkPipeline pipeline = createFullscreenPipeline(ctx, targets_[0].renderPass,
                                                   pipelineLayout_.get(),
                                                   fullscreenVertPath_.c_str(), fragPath_.c_str());
    pipeline_ = engine::render::UniquePipeline(ctx.device, pipeline);
}

void LightingPass::destroyPipeline(VulkanContext& ctx)
{
    (void)ctx;
    pipeline_.reset();
}

void LightingPass::createTargets(VulkanContext& ctx, const PassCreateInfo& ci)
{
    if (ci.commands == nullptr)
    {
        failLightingPass(
            "PassCreateInfo.commands is required for lighting target layout transitions.");
    }

    // per-frame hdr lighting targets.
    targets_.resize(kMaxFramesInFlight);
    for (auto& t : targets_)
    {
        createImage(ctx, extent_.width, extent_.height, t.format,
                    VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                        VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                    t.image,
                    t.memory);
        t.view = createImageView(ctx.device, t.image, t.format, VK_IMAGE_ASPECT_COLOR_BIT);

        if (!ctx.samplerCache.get(ctx.device, engine::render::SamplerPreset::ClampLinear,
                                  t.sampler))
        {
            failLightingPass("vkCreateSampler failed for lighting output sampling.");
        }

        transitionImageLayout(ctx, *ci.commands, t.image, VK_IMAGE_ASPECT_COLOR_BIT,
                              VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

        t.renderPass = createLightingRenderPass(ctx.device, t.format);

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
            failLightingPass("vkCreateFramebuffer failed for lighting output targets.");
        }
    }
}

void LightingPass::destroyTargets(VulkanContext& ctx)
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
        t = LitTarget{};
    }
    targets_.clear();
}

void LightingPass::updateUbo(uint32_t frameIndex, const FrameUbo& frameUbo)
{
    if (frameIndex >= ubos_.size())
    {
        return;
    }
    if (ubos_[frameIndex].mapped == nullptr)
    {
        return;
    }

    std::memcpy(ubos_[frameIndex].mapped, &frameUbo, sizeof(frameUbo));
}

void LightingPass::updateDescriptorSets(VulkanContext& ctx, const GBufferPass& gbuffer,
                                        const LightsBuffer& lights, const ShadowMap& shadowMap,
                                        ShadowBuffer& shadowBuffer,
                                        VkImageView shadowResolvedView, VkSampler shadowResolvedSampler,
                                        VkImageView aoResolvedView, VkSampler aoResolvedSampler,
                                        VkImageView aoRawView, VkSampler aoRawSampler,
                                        VkImageView localShadowResolvedView,
                                        VkSampler localShadowResolvedSampler)
{
    const size_t count = std::min(gbufferSets_.size(), static_cast<size_t>(kMaxFramesInFlight));
    for (size_t i = 0; i < count; i++)
    {
        // set 0: sampled g-buffer + depth + shadow buffers + water distance (day 18).
        constexpr uint32_t kGBufferBindings = 4 + kShadowCascades + 4 + 1 + 1;
        VkDescriptorImageInfo infos[kGBufferBindings]{};
        infos[0].sampler = gbuffer.color(static_cast<uint32_t>(i), GBufferPass::Slot::Albedo)
                               .sampler;
        infos[0].imageView = gbuffer.color(static_cast<uint32_t>(i), GBufferPass::Slot::Albedo).view;
        infos[0].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        infos[1].sampler = gbuffer.color(static_cast<uint32_t>(i), GBufferPass::Slot::Normal).sampler;
        infos[1].imageView = gbuffer.color(static_cast<uint32_t>(i), GBufferPass::Slot::Normal).view;
        infos[1].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        infos[2].sampler =
            gbuffer.color(static_cast<uint32_t>(i), GBufferPass::Slot::Material).sampler;
        infos[2].imageView =
            gbuffer.color(static_cast<uint32_t>(i), GBufferPass::Slot::Material).view;
        infos[2].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        infos[3].sampler = gbuffer.depth(static_cast<uint32_t>(i)).sampler;
        infos[3].imageView = gbuffer.depth(static_cast<uint32_t>(i)).view;
        infos[3].imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;

        for (uint32_t c = 0; c < kShadowCascades; ++c)
        {
            const uint32_t binding = 4 + c;
            // the comparison sampler is immutable in the descriptor layout;
            // the sampler member is ignored and must remain null for
            // portability implementations without mutable comparison samplers.
            infos[binding].sampler = VK_NULL_HANDLE;
            infos[binding].imageView = shadowMap.cascades[c].view;
            infos[binding].imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
        }

        // binding 7: resolved shadow (day 12)
        const uint32_t shadowResolvedBinding = 4 + kShadowCascades;
        infos[shadowResolvedBinding].sampler = shadowResolvedSampler;
        infos[shadowResolvedBinding].imageView = shadowResolvedView;
        infos[shadowResolvedBinding].imageLayout = VK_IMAGE_LAYOUT_GENERAL;

        // binding 8: raw shadow (day 11 debug)
        const uint32_t shadowRawBinding = shadowResolvedBinding + 1;
        infos[shadowRawBinding].sampler = shadowBuffer.getSampler();
        infos[shadowRawBinding].imageView = shadowBuffer.getCurrentImageView();
        infos[shadowRawBinding].imageLayout = VK_IMAGE_LAYOUT_GENERAL;

        // binding 9: resolved ao (day 15)
        const uint32_t aoResolvedBinding = shadowRawBinding + 1;
        infos[aoResolvedBinding].sampler = aoResolvedSampler;
        infos[aoResolvedBinding].imageView = aoResolvedView;
        infos[aoResolvedBinding].imageLayout = VK_IMAGE_LAYOUT_GENERAL;

        // binding 10: raw ao (day 15 debug)
        const uint32_t aoRawBinding = aoResolvedBinding + 1;
        infos[aoRawBinding].sampler = aoRawSampler;
        infos[aoRawBinding].imageView = aoRawView;
        infos[aoRawBinding].imageLayout = VK_IMAGE_LAYOUT_GENERAL;

        // binding 11: water distance (day 18)
        const uint32_t waterDistBinding = aoRawBinding + 1;
        infos[waterDistBinding].sampler =
            gbuffer.color(static_cast<uint32_t>(i), GBufferPass::Slot::WaterDist).sampler;
        infos[waterDistBinding].imageView =
            gbuffer.color(static_cast<uint32_t>(i), GBufferPass::Slot::WaterDist).view;
        infos[waterDistBinding].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        // binding 12: local light shadows (RGBA16F, channels 0..3)
        const uint32_t localShadowResolvedBinding = waterDistBinding + 1;
        infos[localShadowResolvedBinding].sampler = localShadowResolvedSampler;
        infos[localShadowResolvedBinding].imageView = localShadowResolvedView;
        infos[localShadowResolvedBinding].imageLayout = VK_IMAGE_LAYOUT_GENERAL;

        VkWriteDescriptorSet writes[kGBufferBindings]{};
        for (uint32_t b = 0; b < kGBufferBindings; b++)
        {
            VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
            w.dstSet = gbufferSets_[i];
            w.dstBinding = b;
            w.descriptorCount = 1;
            w.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            w.pImageInfo = &infos[b];
            writes[b] = w;
        }

        vkUpdateDescriptorSets(ctx.device, kGBufferBindings, writes, 0, nullptr);

        VkDescriptorBufferInfo bi{};
        bi.buffer = ubos_[i].buffer;
        bi.offset = 0;
        bi.range = sizeof(FrameUbo);

        VkDescriptorBufferInfo sbo{};
        sbo.buffer = lights.buffer;
        sbo.offset = 0;
        sbo.range = sizeof(AreaLightGpu) * lights.maxLights;

        VkDescriptorBufferInfo waterSbo{};
        waterSbo.buffer = waterVolumeBuffer_;
        waterSbo.offset = 0;
        waterSbo.range = VK_WHOLE_SIZE;

        VkWriteDescriptorSet lightWrites[3]{};
        VkWriteDescriptorSet lw0{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        lw0.dstSet = lightSets_[i];
        lw0.dstBinding = 0;
        lw0.descriptorCount = 1;
        lw0.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        lw0.pBufferInfo = &bi;
        lightWrites[0] = lw0;

        VkWriteDescriptorSet lw1{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        lw1.dstSet = lightSets_[i];
        lw1.dstBinding = 1;
        lw1.descriptorCount = 1;
        lw1.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        lw1.pBufferInfo = &sbo;
        lightWrites[1] = lw1;

        VkWriteDescriptorSet lw2{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        lw2.dstSet = lightSets_[i];
        lw2.dstBinding = 2;
        lw2.descriptorCount = 1;
        lw2.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        lw2.pBufferInfo = &waterSbo;
        lightWrites[2] = lw2;

        vkUpdateDescriptorSets(ctx.device, 3, lightWrites, 0, nullptr);
    }
}
