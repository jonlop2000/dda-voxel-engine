#include "engine/render/passes/ShadowPass.h"

#include <array>
#include <cstring>
#include <span>

#include "engine/render/passes/PassCreateInfo.h"
#include "engine/render/Commands.h"
#include "engine/render/FrameContext.h"
#include "engine/render/Utils.h"
#include "engine/render/VulkanContext.h"
#include "Resources/GpuBuffer.h"
#include "Resources/GpuImage.h"
#include "Resources/Mesh.h"
#include "Resources/ShaderModule.h"

namespace
{
struct ObjectPushConstants
{
    glm::mat4 model{1.0f};
    glm::ivec4 cascadeIndex{0, 0, 0, 0};
};

constexpr const char* kShadowPassSubsystem = "ShadowPass";
constexpr const char* kShadowPassRationale =
    "ShadowPass is required because the primary cascaded shadow map path feeds the main lighting "
    "pass and shadow debug visualization.";

[[noreturn]] void failShadowPass(std::string_view detail)
{
    logAndExit(kShadowPassSubsystem,
               std::string("Required pass failure: ") + std::string(detail) + " " +
                   kShadowPassRationale);
}

std::vector<char> loadShadowShaderOrFatal(const char* path, std::string_view stage)
{
    std::vector<char> code;
    std::string error;
    if (!tryReadFile(path, code, &error))
    {
        failShadowPass(std::string("Failed to read ") + std::string(stage) + " shader '" + path +
                       "': " + error);
    }
    return code;
}

VkShaderModule createShadowShaderModuleOrFatal(VkDevice device, const std::vector<char>& code,
                                               const char* path, std::string_view stage)
{
    std::string error;
    VkShaderModule module = tryCreateShaderModule(device, code, &error);
    if (module == VK_NULL_HANDLE)
    {
        failShadowPass(std::string("Failed to create ") + std::string(stage) +
                       " shader module for '" + path + "': " +
                       (error.empty() ? "vkCreateShaderModule failed." : error));
    }
    return module;
}

VkRenderPass createShadowRenderPass(VkDevice device, VkFormat depthFormat, VkFormat colorFormat)
{
    // attachment 0: debug color (R32_SFLOAT for depth visualization)
    VkAttachmentDescription color{};
    color.format = colorFormat;
    color.samples = VK_SAMPLE_COUNT_1_BIT;
    color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    color.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    color.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    color.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    // attachment 1: depth
    VkAttachmentDescription depth{};
    depth.format = depthFormat;
    depth.samples = VK_SAMPLE_COUNT_1_BIT;
    depth.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depth.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    depth.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depth.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depth.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;

    VkAttachmentDescription attachments[] = {color, depth};

    VkAttachmentReference colorRef{};
    colorRef.attachment = 0;
    colorRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentReference depthRef{};
    depthRef.attachment = 1;
    depthRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorRef;
    subpass.pDepthStencilAttachment = &depthRef;

    VkSubpassDependency deps[2]{};

    // incoming dependency: wait for previous fragment shader reads before writing
    deps[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    deps[0].dstSubpass = 0;
    deps[0].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    deps[0].dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    deps[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    deps[0].dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    // outgoing dependency: ensure writes complete and layout transitions before next pass reads
    deps[1].srcSubpass = 0;
    deps[1].dstSubpass = VK_SUBPASS_EXTERNAL;
    deps[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    deps[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    deps[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    deps[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    VkRenderPassCreateInfo rpci{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    rpci.attachmentCount = 2;
    rpci.pAttachments = attachments;
    rpci.subpassCount = 1;
    rpci.pSubpasses = &subpass;
    rpci.dependencyCount = 2;
    rpci.pDependencies = deps;

    VkRenderPass rp = VK_NULL_HANDLE;
    if (vkCreateRenderPass(device, &rpci, nullptr, &rp) != VK_SUCCESS)
    {
        failShadowPass("vkCreateRenderPass failed for cascaded shadow map targets.");
    }
    return rp;
}

VkPipeline createShadowPipeline(VulkanContext& ctx, VkRenderPass renderPass,
                                VkPipelineLayout pipelineLayout, const char* vertPath,
                                const char* fragPath)
{
    const std::vector<char> vertCode = loadShadowShaderOrFatal(vertPath, "vertex");
    const std::vector<char> fragCode = loadShadowShaderOrFatal(fragPath, "fragment");

    VkShaderModule vertModule =
        createShadowShaderModuleOrFatal(ctx.device, vertCode, vertPath, "vertex");
    VkShaderModule fragModule =
        createShadowShaderModuleOrFatal(ctx.device, fragCode, fragPath, "fragment");

    VkPipelineShaderStageCreateInfo vertStage{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
    vertStage.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertStage.module = vertModule;
    vertStage.pName = "main";

    VkPipelineShaderStageCreateInfo fragStage{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
    fragStage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragStage.module = fragModule;
    fragStage.pName = "main";

    VkPipelineShaderStageCreateInfo stages[] = {vertStage, fragStage};

    VkVertexInputBindingDescription binding{};
    binding.binding = 0;
    binding.stride = sizeof(Vertex);
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputAttributeDescription attribute{};
    attribute.binding = 0;
    attribute.location = 0;
    attribute.format = VK_FORMAT_R32G32B32_SFLOAT;
    attribute.offset = static_cast<uint32_t>(offsetof(Vertex, pos));

    VkPipelineVertexInputStateCreateInfo vertexInput{
        VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    vertexInput.vertexBindingDescriptionCount = 1;
    vertexInput.pVertexBindingDescriptions = &binding;
    vertexInput.vertexAttributeDescriptionCount = 1;
    vertexInput.pVertexAttributeDescriptions = &attribute;

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
    depth.depthTestEnable = VK_TRUE;
    depth.depthWriteEnable = VK_TRUE;
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
        failShadowPass("vkCreateGraphicsPipelines failed for the cascaded shadow pipeline.");
    }

    vkDestroyShaderModule(ctx.device, fragModule, nullptr);
    vkDestroyShaderModule(ctx.device, vertModule, nullptr);

    return pipeline;
}
} // namespace

void ShadowPass::setShaderPaths(const std::string& vertPath, const std::string& fragPath)
{
    vertPath_ = vertPath;
    fragPath_ = fragPath;
}

void ShadowPass::create(VulkanContext& ctx, const PassCreateInfo& ci)
{
    if (ci.commands == nullptr)
    {
        failShadowPass("PassCreateInfo.commands is required for ShadowPass.");
    }

    shadow_.format = findSupportedDepthFormat(ctx);

    VkDescriptorSetLayoutBinding uboBinding{};
    uboBinding.binding = 0;
    uboBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    uboBinding.descriptorCount = 1;
    uboBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    VkDescriptorSetLayoutCreateInfo dlci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    dlci.bindingCount = 1;
    dlci.pBindings = &uboBinding;
    if (!ctx.descriptorLayoutCache.get(ctx.device, dlci, setLayout_))
    {
        failShadowPass("vkCreateDescriptorSetLayout failed for the shadow frame UBO set.");
    }

    VkPipelineLayoutCreateInfo plci{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    plci.setLayoutCount = 1;
    VkDescriptorSetLayout setLayouts[] = {setLayout_};
    plci.pSetLayouts = setLayouts;
    VkPushConstantRange pcr{};
    pcr.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pcr.offset = 0;
    pcr.size = sizeof(ObjectPushConstants);
    plci.pushConstantRangeCount = 1;
    plci.pPushConstantRanges = &pcr;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    if (vkCreatePipelineLayout(ctx.device, &plci, nullptr, &pipelineLayout) != VK_SUCCESS)
    {
        failShadowPass("vkCreatePipelineLayout failed for the shadow pipeline.");
    }
    pipelineLayout_ = engine::render::UniquePipelineLayout(ctx.device, pipelineLayout);

    VkDescriptorPoolSize poolSize{};
    poolSize.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSize.descriptorCount = kMaxFramesInFlight;

    std::vector<VkDescriptorSetLayout> layouts(kMaxFramesInFlight, setLayout_);
    descSets_.resize(kMaxFramesInFlight);
    const std::span<const VkDescriptorPoolSize> poolSizesView(&poolSize, 1);
    if (!ctx.descriptorAllocator.allocate(ctx.device, poolSizesView, kMaxFramesInFlight,
                                          layouts.data(), kMaxFramesInFlight, descSets_.data()))
    {
        failShadowPass("Failed to allocate shadow frame descriptors.");
    }

    ubos_.resize(kMaxFramesInFlight);
    for (size_t i = 0; i < kMaxFramesInFlight; i++)
    {
        createBuffer(ctx, sizeof(FrameUbo), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                     ubos_[i].buffer, ubos_[i].memory);

        if (vkMapMemory(ctx.device, ubos_[i].memory, 0, sizeof(FrameUbo), 0, &ubos_[i].mapped) !=
            VK_SUCCESS)
        {
            failShadowPass("vkMapMemory failed for the shadow frame UBO.");
        }

        VkDescriptorBufferInfo bi{};
        bi.buffer = ubos_[i].buffer;
        bi.offset = 0;
        bi.range = sizeof(FrameUbo);

        VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        w.dstSet = descSets_[i];
        w.dstBinding = 0;
        w.descriptorCount = 1;
        w.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        w.pBufferInfo = &bi;

        vkUpdateDescriptorSets(ctx.device, 1, &w, 0, nullptr);
    }

    createResources(ctx, ci);
    createPipeline(ctx);
}

void ShadowPass::destroy(VulkanContext& ctx)
{
    destroyPipeline(ctx);
    destroyResources(ctx);

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

    descSets_.clear();
    setLayout_ = VK_NULL_HANDLE;
    pipelineLayout_.reset();
}

void ShadowPass::record(VulkanContext& ctx, const FrameContext& fc, const FrameUbo& frameUbo,
                        const std::vector<RenderObject>& objects)
{
    (void)ctx;
    if (fc.frameIndex >= descSets_.size())
    {
        return;
    }
    updateUbo(fc.frameIndex, frameUbo);

    if (shadow_.renderPass == VK_NULL_HANDLE)
    {
        return;
    }

    // clear values: [0] = color (debug depth), [1] = depth
    // clear to 1.0 (far plane) - same as depth clear, so background = dark after inversion
    VkClearValue clears[2]{};
    clears[0].color = {{1.0f, 1.0f, 1.0f, 1.0f}};
    clears[1].depthStencil = {1.0f, 0};

    uint32_t drawnCount = 0;
    for (uint32_t cascadeIndex = 0; cascadeIndex < kShadowCascades; ++cascadeIndex)
    {
        const auto& cascade = shadow_.cascades[cascadeIndex];
        if (cascade.framebuffer == VK_NULL_HANDLE)
        {
            continue;
        }

        VkRenderPassBeginInfo rp{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
        rp.renderPass = shadow_.renderPass;
        rp.framebuffer = cascade.framebuffer;
        rp.renderArea.offset = {0, 0};
        rp.renderArea.extent = shadow_.extent;
        rp.clearValueCount = 2;
        rp.pClearValues = clears;

        vkCmdBeginRenderPass(fc.cmd, &rp, VK_SUBPASS_CONTENTS_INLINE);
        vkCmdBindPipeline(fc.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_.get());
        vkCmdBindDescriptorSets(fc.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout_.get(), 0, 1,
                                &descSets_[fc.frameIndex], 0, nullptr);

        VkViewport vp{};
        vp.x = 0.0f;
        vp.y = 0.0f;
        vp.width = static_cast<float>(shadow_.extent.width);
        vp.height = static_cast<float>(shadow_.extent.height);
        vp.minDepth = 0.0f;
        vp.maxDepth = 1.0f;
        VkRect2D scissor{{0, 0}, shadow_.extent};
        vkCmdSetViewport(fc.cmd, 0, 1, &vp);
        vkCmdSetScissor(fc.cmd, 0, 1, &scissor);

        if (!objects.empty())
        {
            for (const auto& obj : objects)
            {
                if (obj.mesh == nullptr)
                {
                    continue;
                }
                if (obj.mesh->vbo == VK_NULL_HANDLE)
                {
                    continue;
                }
                if (obj.mesh->useIndex && obj.mesh->ibo == VK_NULL_HANDLE)
                {
                    continue;
                }

                VkBuffer vertexBuffers[] = {obj.mesh->vbo};
                VkDeviceSize offsets[] = {0};
                vkCmdBindVertexBuffers(fc.cmd, 0, 1, vertexBuffers, offsets);

                if (obj.mesh->useIndex)
                {
                    vkCmdBindIndexBuffer(fc.cmd, obj.mesh->ibo, 0, VK_INDEX_TYPE_UINT32);
                }

                ObjectPushConstants pc{};
                pc.model = obj.model;
                pc.cascadeIndex = glm::ivec4(static_cast<int>(cascadeIndex), 0, 0, 0);
                vkCmdPushConstants(fc.cmd, pipelineLayout_.get(), VK_SHADER_STAGE_VERTEX_BIT, 0,
                                   sizeof(pc), &pc);

                if (obj.mesh->useIndex)
                {
                    vkCmdDrawIndexed(fc.cmd, obj.mesh->indexCount, 1, 0, 0, 0);
                }
                else
                {
                    vkCmdDraw(fc.cmd, obj.mesh->vertexCount, 1, 0, 0);
                }
                drawnCount++;
            }
        }

        vkCmdEndRenderPass(fc.cmd);
    }

    if (drawnCount == 0)
    {
        static bool warnedOnce = false;
        if (!warnedOnce)
        {
            warnedOnce = true;
            logWarning(kShadowPassSubsystem,
                       "Recorded 0 draw calls for all cascades; all objects were skipped.");
        }
    }
}

const ShadowMap& ShadowPass::map() const
{
    return shadow_;
}

void ShadowPass::createPipeline(VulkanContext& ctx)
{
    if (shadow_.renderPass == VK_NULL_HANDLE)
    {
        failShadowPass("ShadowPass render pass must exist before pipeline creation.");
    }
    VkPipeline pipeline = createShadowPipeline(ctx, shadow_.renderPass, pipelineLayout_.get(),
                                               vertPath_.c_str(), fragPath_.c_str());
    pipeline_ = engine::render::UniquePipeline(ctx.device, pipeline);
}

void ShadowPass::destroyPipeline(VulkanContext& ctx)
{
    (void)ctx;
    pipeline_.reset();
}

void ShadowPass::createResources(VulkanContext& ctx, const PassCreateInfo& ci)
{
    if (ci.commands == nullptr)
    {
        failShadowPass("PassCreateInfo.commands is required for ShadowPass resources.");
    }

    if (shadow_.renderPass == VK_NULL_HANDLE)
    {
        shadow_.renderPass =
            createShadowRenderPass(ctx.device, shadow_.format, shadow_.debugFormat);
    }

    // the lighting layouts bind this as an immutable comparison sampler.  in
    // addition to centralizing ownership, that is required on portability
    // devices that do not expose mutableComparisonSamplers (including
    // MoltenVK on apple silicon).
    if (!ctx.samplerCache.get(ctx.device,
                              engine::render::SamplerPreset::ShadowCompareNearest,
                              shadow_.sampler))
    {
        failShadowPass("vkCreateSampler failed for the shadow comparison sampler.");
    }

    // sampler for debug depth reading (no comparison)
    VkSamplerCreateInfo sci{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    sci.magFilter = VK_FILTER_NEAREST;
    sci.minFilter = VK_FILTER_NEAREST;
    sci.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    sci.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    sci.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    sci.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    sci.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
    sci.compareEnable = VK_FALSE;
    sci.minFilter = VK_FILTER_NEAREST;
    sci.magFilter = VK_FILTER_NEAREST;
    sci.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    if (vkCreateSampler(ctx.device, &sci, nullptr, &shadow_.debugSampler) != VK_SUCCESS)
    {
        failShadowPass("vkCreateSampler failed for the shadow debug depth sampler.");
    }

    // sampler for debug color texture (standard linear filtering)
    VkSamplerCreateInfo colorSci{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    colorSci.magFilter = VK_FILTER_LINEAR;
    colorSci.minFilter = VK_FILTER_LINEAR;
    colorSci.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    colorSci.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    colorSci.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    colorSci.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    if (vkCreateSampler(ctx.device, &colorSci, nullptr, &shadow_.debugColorSampler) != VK_SUCCESS)
    {
        failShadowPass("vkCreateSampler failed for the shadow debug color sampler.");
    }

    for (auto& cascade : shadow_.cascades)
    {
        // create depth image
        createImage(ctx, shadow_.extent.width, shadow_.extent.height, shadow_.format,
                    VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                    cascade.image, cascade.memory);
        cascade.view =
            createImageView(ctx.device, cascade.image, shadow_.format, VK_IMAGE_ASPECT_DEPTH_BIT);

        // create debug color image (R32_SFLOAT to store depth as color)
        createImage(ctx, shadow_.extent.width, shadow_.extent.height, shadow_.debugFormat,
                    VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                    cascade.debugImage, cascade.debugMemory);
        cascade.debugView = createImageView(ctx.device, cascade.debugImage, shadow_.debugFormat,
                                            VK_IMAGE_ASPECT_COLOR_BIT);

        // no initial layout transitions needed - render pass handles them via initialLayout=undefined
        // the images will be cleared and transitioned on first use

        // framebuffer with both color and depth attachments
        VkImageView attachments[] = {cascade.debugView, cascade.view};
        VkFramebufferCreateInfo fbci{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
        fbci.renderPass = shadow_.renderPass;
        fbci.attachmentCount = 2;
        fbci.pAttachments = attachments;
        fbci.width = shadow_.extent.width;
        fbci.height = shadow_.extent.height;
        fbci.layers = 1;
        if (vkCreateFramebuffer(ctx.device, &fbci, nullptr, &cascade.framebuffer) != VK_SUCCESS)
        {
            failShadowPass("vkCreateFramebuffer failed for a cascaded shadow framebuffer.");
        }
    }
}

void ShadowPass::destroyResources(VulkanContext& ctx)
{
    for (auto& cascade : shadow_.cascades)
    {
        if (cascade.framebuffer != VK_NULL_HANDLE)
        {
            vkDestroyFramebuffer(ctx.device, cascade.framebuffer, nullptr);
            cascade.framebuffer = VK_NULL_HANDLE;
        }
    }
    if (shadow_.renderPass != VK_NULL_HANDLE)
    {
        vkDestroyRenderPass(ctx.device, shadow_.renderPass, nullptr);
        shadow_.renderPass = VK_NULL_HANDLE;
    }
    if (shadow_.sampler != VK_NULL_HANDLE)
    {
        // owned by VulkanContext::samplerCache.
        shadow_.sampler = VK_NULL_HANDLE;
    }
    if (shadow_.debugSampler != VK_NULL_HANDLE)
    {
        vkDestroySampler(ctx.device, shadow_.debugSampler, nullptr);
        shadow_.debugSampler = VK_NULL_HANDLE;
    }
    if (shadow_.debugColorSampler != VK_NULL_HANDLE)
    {
        vkDestroySampler(ctx.device, shadow_.debugColorSampler, nullptr);
        shadow_.debugColorSampler = VK_NULL_HANDLE;
    }
    for (auto& cascade : shadow_.cascades)
    {
        if (cascade.view != VK_NULL_HANDLE)
        {
            vkDestroyImageView(ctx.device, cascade.view, nullptr);
            cascade.view = VK_NULL_HANDLE;
        }
        if (cascade.debugView != VK_NULL_HANDLE)
        {
            vkDestroyImageView(ctx.device, cascade.debugView, nullptr);
            cascade.debugView = VK_NULL_HANDLE;
        }
        if (cascade.image != VK_NULL_HANDLE)
        {
            vkDestroyImage(ctx.device, cascade.image, nullptr);
            cascade.image = VK_NULL_HANDLE;
        }
        if (cascade.debugImage != VK_NULL_HANDLE)
        {
            vkDestroyImage(ctx.device, cascade.debugImage, nullptr);
            cascade.debugImage = VK_NULL_HANDLE;
        }
        if (cascade.memory != VK_NULL_HANDLE)
        {
            vkFreeMemory(ctx.device, cascade.memory, nullptr);
            cascade.memory = VK_NULL_HANDLE;
        }
        if (cascade.debugMemory != VK_NULL_HANDLE)
        {
            vkFreeMemory(ctx.device, cascade.debugMemory, nullptr);
            cascade.debugMemory = VK_NULL_HANDLE;
        }
    }
}

void ShadowPass::updateUbo(uint32_t frameIndex, const FrameUbo& frameUbo)
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
