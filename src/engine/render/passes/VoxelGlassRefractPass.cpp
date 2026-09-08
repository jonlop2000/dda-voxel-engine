#include "engine/render/passes/VoxelGlassRefractPass.h"

#include <array>
#include <cstring>
#include <span>

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
constexpr const char* kVoxelGlassRefractPassSubsystem = "VoxelGlassRefractPass";
constexpr const char* kVoxelGlassRefractPassRationale =
    "VoxelGlassRefractPass is required for the current voxel-glass refraction and debug path, "
    "which samples lit scene color and G-buffer inputs before compositing back into lighting.";

[[noreturn]] void failVoxelGlassRefractPass(std::string_view detail)
{
    logAndExit(kVoxelGlassRefractPassSubsystem,
               std::string("Required pass failure: ") + std::string(detail) + " " +
                   kVoxelGlassRefractPassRationale);
}

std::vector<char> loadVoxelGlassShaderOrFatal(const char* path, std::string_view stage)
{
    std::vector<char> code;
    std::string error;
    if (!tryReadFile(path, code, &error))
    {
        failVoxelGlassRefractPass(std::string("Failed to read ") + std::string(stage) +
                                  " shader '" + path + "': " + error);
    }
    return code;
}

VkShaderModule createVoxelGlassShaderModuleOrFatal(VkDevice device, const std::vector<char>& code,
                                                   const char* path, std::string_view stage)
{
    std::string error;
    VkShaderModule module = tryCreateShaderModule(device, code, &error);
    if (module == VK_NULL_HANDLE)
    {
        failVoxelGlassRefractPass(std::string("Failed to create ") + std::string(stage) +
                                  " shader module for '" + path + "': " +
                                  (error.empty() ? "vkCreateShaderModule failed." : error));
    }
    return module;
}

VkRenderPass createVoxelGlassRenderPass(VkDevice device, VkFormat colorFormat)
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
        failVoxelGlassRefractPass("vkCreateRenderPass failed for voxel-glass refraction targets.");
    }
    return rp;
}

VkPipeline createFullscreenPipeline(VulkanContext& ctx, VkRenderPass renderPass,
                                    VkPipelineLayout pipelineLayout, const char* vertPath,
                                    const char* fragPath)
{
    const std::vector<char> vertCode = loadVoxelGlassShaderOrFatal(vertPath, "vertex");
    const std::vector<char> fragCode = loadVoxelGlassShaderOrFatal(fragPath, "fragment");

    VkShaderModule vertModule =
        createVoxelGlassShaderModuleOrFatal(ctx.device, vertCode, vertPath, "vertex");
    VkShaderModule fragModule =
        createVoxelGlassShaderModuleOrFatal(ctx.device, fragCode, fragPath, "fragment");

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
        failVoxelGlassRefractPass(
            "vkCreateGraphicsPipelines failed for the voxel-glass refraction pipeline.");
    }

    vkDestroyShaderModule(ctx.device, fragModule, nullptr);
    vkDestroyShaderModule(ctx.device, vertModule, nullptr);

    return pipeline;
}
} // namespace

void VoxelGlassRefractPass::setShaderPaths(const std::string& fullscreenVert,
                                            const std::string& fragPath)
{
    fullscreenVertPath_ = fullscreenVert;
    fragPath_ = fragPath;
}

void VoxelGlassRefractPass::setWaterVolumeBuffer(VkBuffer buffer)
{
    waterVolumeBuffer_ = buffer;
}

void VoxelGlassRefractPass::create(VulkanContext& ctx, const PassCreateInfo& ci,
                                    const GBufferPass& gbuffer, const LightingPass& lighting)
{
    if (ci.commands == nullptr)
    {
        failVoxelGlassRefractPass("PassCreateInfo.commands is required for VoxelGlassRefractPass.");
    }

    extent_ = ci.extent;
    gbuffer_ = &gbuffer;
    lighting_ = &lighting;
    if (waterVolumeBuffer_ == VK_NULL_HANDLE)
    {
        failVoxelGlassRefractPass(
            "A valid water volume buffer is required for voxel-glass atmosphere descriptors.");
    }

    // set 0: g-buffer textures (albedo, normal, material, depth, velocity) + lit scene.
    constexpr uint32_t kTexBindings = 6;  // 5 g-buffer + 1 lit scene
    VkDescriptorSetLayoutBinding texBindings[kTexBindings]{};
    for (uint32_t i = 0; i < kTexBindings; i++)
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
        failVoxelGlassRefractPass(
            "vkCreateDescriptorSetLayout failed for voxel-glass sampled inputs.");
    }

    // set 1: ubo.
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
        failVoxelGlassRefractPass(
            "vkCreateDescriptorSetLayout failed for the voxel-glass frame UBO.");
    }

    VkDescriptorSetLayoutBinding waterBinding{};
    waterBinding.binding = 0;
    waterBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    waterBinding.descriptorCount = 1;
    waterBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo waterLci{
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    waterLci.bindingCount = 1;
    waterLci.pBindings = &waterBinding;
    if (!ctx.descriptorLayoutCache.get(ctx.device, waterLci, waterSetLayout_))
    {
        failVoxelGlassRefractPass(
            "vkCreateDescriptorSetLayout failed for voxel-glass water-volume bindings.");
    }

    VkDescriptorSetLayout setLayouts[] = {texSetLayout_, uboSetLayout_, waterSetLayout_};
    VkPipelineLayoutCreateInfo plci{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    plci.setLayoutCount = 3;
    plci.pSetLayouts = setLayouts;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    if (vkCreatePipelineLayout(ctx.device, &plci, nullptr, &pipelineLayout) != VK_SUCCESS)
    {
        failVoxelGlassRefractPass("vkCreatePipelineLayout failed for the voxel-glass pipeline.");
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
        failVoxelGlassRefractPass(
            "Failed to allocate voxel-glass texture binding descriptors.");
    }

    std::vector<VkDescriptorSetLayout> uboLayouts(kMaxFramesInFlight, uboSetLayout_);
    if (!ctx.descriptorAllocator.allocate(ctx.device, poolSizesView, kMaxDescriptorSets,
                                          uboLayouts.data(), kMaxFramesInFlight,
                                          uboSets_.data()))
    {
        failVoxelGlassRefractPass("Failed to allocate voxel-glass frame UBO descriptors.");
    }

    VkDescriptorSetLayout waterLayout = waterSetLayout_;
    if (!ctx.descriptorAllocator.allocate(ctx.device, poolSizesView, kMaxDescriptorSets,
                                          &waterLayout, 1, &waterSet_))
    {
        failVoxelGlassRefractPass(
            "Failed to allocate voxel-glass water-volume descriptors.");
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
            failVoxelGlassRefractPass("vkMapMemory failed for the voxel-glass frame UBO.");
        }

        VkDescriptorBufferInfo bi{};
        bi.buffer = ubos_[i].buffer;
        bi.offset = 0;
        bi.range = sizeof(FrameUbo);

        VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        w.dstSet = uboSets_[i];
        w.dstBinding = 0;
        w.descriptorCount = 1;
        w.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        w.pBufferInfo = &bi;

        vkUpdateDescriptorSets(ctx.device, 1, &w, 0, nullptr);
    }

    updateWaterDescriptor(ctx);

    createTargets(ctx, ci);
    updateDescriptorSets(ctx, gbuffer, lighting);
    createPipeline(ctx);
}

void VoxelGlassRefractPass::destroy(VulkanContext& ctx)
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

    texSets_.clear();
    uboSets_.clear();
    texSetLayout_ = VK_NULL_HANDLE;
    uboSetLayout_ = VK_NULL_HANDLE;
    waterSetLayout_ = VK_NULL_HANDLE;
    pipelineLayout_.reset();
    gbuffer_ = nullptr;
    lighting_ = nullptr;
    waterSet_ = VK_NULL_HANDLE;
    waterVolumeBuffer_ = VK_NULL_HANDLE;
}

void VoxelGlassRefractPass::onResize(VulkanContext& ctx, const PassCreateInfo& ci,
                                      const GBufferPass& gbuffer, const LightingPass& lighting)
{
    extent_ = ci.extent;
    destroyPipeline(ctx);
    destroyTargets(ctx);
    createTargets(ctx, ci);
    gbuffer_ = &gbuffer;
    lighting_ = &lighting;
    updateDescriptorSets(ctx, gbuffer, lighting);
    createPipeline(ctx);
}

void VoxelGlassRefractPass::record(VulkanContext& ctx, const FrameContext& fc, const FrameUbo& ubo)
{
    (void)ctx;
    if (fc.frameIndex >= targets_.size())
    {
        return;
    }

    updateUbo(fc.frameIndex, ubo);

    VkClearValue clearVal{};
    clearVal.color = {{0.0f, 0.0f, 0.0f, 1.0f}};

    const Target& tgt = targets_[fc.frameIndex];
    VkRenderPassBeginInfo rpbi{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    rpbi.renderPass = tgt.renderPass;
    rpbi.framebuffer = tgt.framebuffer;
    rpbi.renderArea.offset = {0, 0};
    rpbi.renderArea.extent = extent_;
    rpbi.clearValueCount = 1;
    rpbi.pClearValues = &clearVal;

    vkCmdBeginRenderPass(fc.cmd, &rpbi, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(fc.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_.get());

    VkDescriptorSet sets[] = {
        texSets_[fc.frameIndex], uboSets_[fc.frameIndex], waterSet_};
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

    // full-screen triangle.
    vkCmdDraw(fc.cmd, 3, 1, 0, 0);
    vkCmdEndRenderPass(fc.cmd);
}

void VoxelGlassRefractPass::updateDescriptorSets(VulkanContext& ctx, const GBufferPass& gbuffer,
                                                  const LightingPass& lighting)
{
    for (uint32_t i = 0; i < kMaxFramesInFlight; i++)
    {
        const auto& albedo = gbuffer.color(i, GBufferPass::Slot::Albedo);
        const auto& normal = gbuffer.color(i, GBufferPass::Slot::Normal);
        const auto& material = gbuffer.color(i, GBufferPass::Slot::Material);
        const auto& depth = gbuffer.depth(i);
        const auto& velocity = gbuffer.color(i, GBufferPass::Slot::Velocity);
        const auto& lit = lighting.lit(i);

        VkDescriptorImageInfo infos[6]{};
        infos[0].sampler = albedo.sampler;
        infos[0].imageView = albedo.view;
        infos[0].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        infos[1].sampler = normal.sampler;
        infos[1].imageView = normal.view;
        infos[1].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        infos[2].sampler = material.sampler;
        infos[2].imageView = material.view;
        infos[2].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        infos[3].sampler = depth.sampler;
        infos[3].imageView = depth.view;
        infos[3].imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;

        infos[4].sampler = velocity.sampler;
        infos[4].imageView = velocity.view;
        infos[4].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        infos[5].sampler = lit.sampler;
        infos[5].imageView = lit.view;
        infos[5].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkWriteDescriptorSet writes[6]{};
        for (uint32_t j = 0; j < 6; j++)
        {
            writes[j].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[j].dstSet = texSets_[i];
            writes[j].dstBinding = j;
            writes[j].descriptorCount = 1;
            writes[j].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            writes[j].pImageInfo = &infos[j];
        }

        vkUpdateDescriptorSets(ctx.device, 6, writes, 0, nullptr);
    }
}

void VoxelGlassRefractPass::updateWaterDescriptor(VulkanContext& ctx)
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

const VoxelGlassRefractPass::Target& VoxelGlassRefractPass::target(uint32_t frameIndex) const
{
    return targets_.at(frameIndex);
}

VkRenderPass VoxelGlassRefractPass::renderPass() const
{
    if (targets_.empty())
    {
        return VK_NULL_HANDLE;
    }
    return targets_[0].renderPass;
}

VkExtent2D VoxelGlassRefractPass::extent() const
{
    return extent_;
}

void VoxelGlassRefractPass::createPipeline(VulkanContext& ctx)
{
    if (targets_.empty())
    {
        failVoxelGlassRefractPass(
            "VoxelGlassRefractPass targets must exist before pipeline creation.");
    }
    VkPipeline pipeline = createFullscreenPipeline(ctx, targets_[0].renderPass,
                                                   pipelineLayout_.get(),
                                                   fullscreenVertPath_.c_str(), fragPath_.c_str());
    pipeline_ = engine::render::UniquePipeline(ctx.device, pipeline);
}

void VoxelGlassRefractPass::destroyPipeline(VulkanContext& ctx)
{
    (void)ctx;
    pipeline_.reset();
}

void VoxelGlassRefractPass::createTargets(VulkanContext& ctx, const PassCreateInfo& ci)
{
    if (ci.commands == nullptr)
    {
        failVoxelGlassRefractPass(
            "PassCreateInfo.commands is required for VoxelGlassRefractPass targets.");
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
            failVoxelGlassRefractPass("vkCreateSampler failed for a voxel-glass target.");
        }

        transitionImageLayout(ctx, *ci.commands, t.image, VK_IMAGE_ASPECT_COLOR_BIT,
                              VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

        t.renderPass = createVoxelGlassRenderPass(ctx.device, t.format);

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
            failVoxelGlassRefractPass(
                "vkCreateFramebuffer failed for a voxel-glass refraction target.");
        }
    }
}

void VoxelGlassRefractPass::destroyTargets(VulkanContext& ctx)
{
    for (auto& t : targets_)
    {
        if (t.framebuffer != VK_NULL_HANDLE)
        {
            vkDestroyFramebuffer(ctx.device, t.framebuffer, nullptr);
            t.framebuffer = VK_NULL_HANDLE;
        }
        if (t.renderPass != VK_NULL_HANDLE)
        {
            vkDestroyRenderPass(ctx.device, t.renderPass, nullptr);
            t.renderPass = VK_NULL_HANDLE;
        }
        t.sampler = VK_NULL_HANDLE;
        if (t.view != VK_NULL_HANDLE)
        {
            vkDestroyImageView(ctx.device, t.view, nullptr);
            t.view = VK_NULL_HANDLE;
        }
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
    targets_.clear();
}

void VoxelGlassRefractPass::updateUbo(uint32_t frameIndex, const FrameUbo& ubo)
{
    if (frameIndex < ubos_.size() && ubos_[frameIndex].mapped != nullptr)
    {
        std::memcpy(ubos_[frameIndex].mapped, &ubo, sizeof(FrameUbo));
    }
}
