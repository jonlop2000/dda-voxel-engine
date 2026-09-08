#include "engine/render/passes/WaterPass.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <span>
#include <vector>

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
uint32_t hashNoiseU32(uint32_t v)
{
    v ^= v >> 16;
    v *= 0x7feb352du;
    v ^= v >> 15;
    v *= 0x846ca68bu;
    v ^= v >> 16;
    return v;
}

constexpr const char* kWaterPassSubsystem = "WaterPass";
constexpr const char* kWaterPassRationale =
    "WaterPass is required because refraction, planar reflection composition, and downstream "
    "glass shading all depend on its scene-color targets.";

[[noreturn]] void failWaterPass(std::string_view detail)
{
    logAndExit(kWaterPassSubsystem,
               std::string("Required pass failure: ") + std::string(detail) + " " +
                   kWaterPassRationale);
}

std::vector<char> loadWaterShaderOrFatal(const char* path, std::string_view stage)
{
    std::vector<char> code;
    std::string error;
    if (!tryReadFile(path, code, &error))
    {
        failWaterPass(std::string("Failed to read ") + std::string(stage) + " shader '" + path +
                      "': " + error);
    }
    return code;
}

VkShaderModule createWaterShaderModuleOrFatal(VkDevice device, const std::vector<char>& code,
                                              const char* path, std::string_view stage)
{
    std::string error;
    VkShaderModule module = tryCreateShaderModule(device, code, &error);
    if (module == VK_NULL_HANDLE)
    {
        failWaterPass(std::string("Failed to create ") + std::string(stage) +
                      " shader module for '" + path + "': " +
                      (error.empty() ? "vkCreateShaderModule failed." : error));
    }
    return module;
}

VkRenderPass createWaterRenderPass(VkDevice device, VkFormat colorFormat, VkFormat depthFormat)
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

    VkAttachmentDescription depth{};
    depth.format = depthFormat;
    depth.samples = VK_SAMPLE_COUNT_1_BIT;
    depth.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    depth.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    depth.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depth.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth.initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
    depth.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;

    std::array<VkAttachmentDescription, 2> attachments = {color, depth};

    VkAttachmentReference colorRef{};
    colorRef.attachment = 0;
    colorRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentReference depthRef{};
    depthRef.attachment = 1;
    depthRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorRef;
    subpass.pDepthStencilAttachment = &depthRef;

    VkSubpassDependency dep{};
    dep.srcSubpass = VK_SUBPASS_EXTERNAL;
    dep.dstSubpass = 0;
    dep.srcStageMask =
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dep.dstStageMask =
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dep.srcAccessMask = 0;
    dep.dstAccessMask =
        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;

    VkRenderPassCreateInfo rpci{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    rpci.attachmentCount = static_cast<uint32_t>(attachments.size());
    rpci.pAttachments = attachments.data();
    rpci.subpassCount = 1;
    rpci.pSubpasses = &subpass;
    rpci.dependencyCount = 1;
    rpci.pDependencies = &dep;

    VkRenderPass rp = VK_NULL_HANDLE;
    if (vkCreateRenderPass(device, &rpci, nullptr, &rp) != VK_SUCCESS)
    {
        failWaterPass("vkCreateRenderPass failed for water scene-color targets.");
    }
    return rp;
}

VkPipeline createFullscreenPipeline(VulkanContext& ctx, VkRenderPass renderPass,
                                    VkPipelineLayout pipelineLayout, const char* vertPath,
                                    const char* fragPath)
{
    const std::vector<char> vertCode = loadWaterShaderOrFatal(vertPath, "vertex");
    const std::vector<char> fragCode = loadWaterShaderOrFatal(fragPath, "fragment");

    VkShaderModule vertModule =
        createWaterShaderModuleOrFatal(ctx.device, vertCode, vertPath, "vertex");
    VkShaderModule fragModule =
        createWaterShaderModuleOrFatal(ctx.device, fragCode, fragPath, "fragment");

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
        failWaterPass("vkCreateGraphicsPipelines failed for the fullscreen water copy pipeline.");
    }

    vkDestroyShaderModule(ctx.device, fragModule, nullptr);
    vkDestroyShaderModule(ctx.device, vertModule, nullptr);

    return pipeline;
}

VkPipeline createWaterPipeline(VulkanContext& ctx, VkRenderPass renderPass,
                               VkPipelineLayout pipelineLayout, const char* vertPath,
                               const char* fragPath)
{
    const std::vector<char> vertCode = loadWaterShaderOrFatal(vertPath, "vertex");
    const std::vector<char> fragCode = loadWaterShaderOrFatal(fragPath, "fragment");

    VkShaderModule vertModule =
        createWaterShaderModuleOrFatal(ctx.device, vertCode, vertPath, "vertex");
    VkShaderModule fragModule =
        createWaterShaderModuleOrFatal(ctx.device, fragCode, fragPath, "fragment");

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

    VkVertexInputAttributeDescription attributes[3]{};
    attributes[0].binding = 0;
    attributes[0].location = 0;
    attributes[0].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributes[0].offset = static_cast<uint32_t>(offsetof(Vertex, pos));
    attributes[1].binding = 0;
    attributes[1].location = 1;
    attributes[1].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributes[1].offset = static_cast<uint32_t>(offsetof(Vertex, normal));
    attributes[2].binding = 0;
    attributes[2].location = 2;
    attributes[2].format = VK_FORMAT_R32G32_SFLOAT;
    attributes[2].offset = static_cast<uint32_t>(offsetof(Vertex, uv));

    VkPipelineVertexInputStateCreateInfo vertexInput{
        VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    vertexInput.vertexBindingDescriptionCount = 1;
    vertexInput.pVertexBindingDescriptions = &binding;
    vertexInput.vertexAttributeDescriptionCount = 3;
    vertexInput.pVertexAttributeDescriptions = attributes;

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
    depth.depthWriteEnable = VK_FALSE;
    depth.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    depth.depthBoundsTestEnable = VK_FALSE;
    depth.stencilTestEnable = VK_FALSE;

    VkPipelineColorBlendAttachmentState blendAttachment{};
    blendAttachment.colorWriteMask =
        VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT;
    blendAttachment.blendEnable = VK_TRUE;
    blendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    blendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
    blendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    blendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;

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
        failWaterPass("vkCreateGraphicsPipelines failed for the mesh water shading pipeline.");
    }

    vkDestroyShaderModule(ctx.device, fragModule, nullptr);
    vkDestroyShaderModule(ctx.device, vertModule, nullptr);

    return pipeline;
}
} // namespace

void WaterPass::setShaderPaths(const std::string& fullscreenVert, const std::string& copyFrag,
                               const std::string& waterVert, const std::string& waterFrag)
{
    fullscreenVertPath_ = fullscreenVert;
    copyFragPath_ = copyFrag;
    waterVertPath_ = waterVert;
    waterFragPath_ = waterFrag;
}

void WaterPass::setWaterVolumeBuffer(VkBuffer buffer)
{
    waterVolumeBuffer_ = buffer;
}

void WaterPass::create(VulkanContext& ctx, const PassCreateInfo& ci, const GBufferPass& gbuffer,
                       const LightingPass& lighting,
                       const LightingPass* planarReflection)
{
    if (ci.commands == nullptr)
    {
        failWaterPass("PassCreateInfo.commands is required for water target transitions.");
    }

    extent_ = ci.extent;
    depthFormat_ = gbuffer.depthFormat();

    if (waterVolumeBuffer_ == VK_NULL_HANDLE)
    {
        failWaterPass("A valid water volume buffer is required for water shading descriptors.");
    }

    VkDescriptorSetLayoutBinding texBindings[5]{};
    texBindings[0].binding = 0;
    texBindings[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    texBindings[0].descriptorCount = 1;
    texBindings[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    texBindings[1].binding = 1;
    texBindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    texBindings[1].descriptorCount = 1;
    texBindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    texBindings[2].binding = 2;
    texBindings[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    texBindings[2].descriptorCount = 1;
    texBindings[2].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    texBindings[3].binding = 3;
    texBindings[3].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    texBindings[3].descriptorCount = 1;
    texBindings[3].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    texBindings[4].binding = 4;
    texBindings[4].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    texBindings[4].descriptorCount = 1;
    texBindings[4].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo tlci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    tlci.bindingCount = 5;
    tlci.pBindings = texBindings;
    if (!ctx.descriptorLayoutCache.get(ctx.device, tlci, texSetLayout_))
    {
        failWaterPass("vkCreateDescriptorSetLayout failed for water texture bindings.");
    }

    VkDescriptorSetLayoutBinding uboBinding{};
    uboBinding.binding = 0;
    uboBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    uboBinding.descriptorCount = 1;
    uboBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo ulci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    ulci.bindingCount = 1;
    ulci.pBindings = &uboBinding;
    if (!ctx.descriptorLayoutCache.get(ctx.device, ulci, uboSetLayout_))
    {
        failWaterPass("vkCreateDescriptorSetLayout failed for water frame uniforms.");
    }

    VkDescriptorSetLayoutBinding waterBinding{};
    waterBinding.binding = 0;
    waterBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    waterBinding.descriptorCount = 1;
    waterBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo wlci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    wlci.bindingCount = 1;
    wlci.pBindings = &waterBinding;
    if (!ctx.descriptorLayoutCache.get(ctx.device, wlci, waterSetLayout_))
    {
        failWaterPass("vkCreateDescriptorSetLayout failed for water volume bindings.");
    }

    VkDescriptorSetLayout setLayouts[] = {texSetLayout_, uboSetLayout_, waterSetLayout_};
    VkPipelineLayoutCreateInfo plci{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    plci.setLayoutCount = 3;
    plci.pSetLayouts = setLayouts;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    if (vkCreatePipelineLayout(ctx.device, &plci, nullptr, &pipelineLayout) != VK_SUCCESS)
    {
        failWaterPass("vkCreatePipelineLayout failed for water pipelines.");
    }
    pipelineLayout_ = engine::render::UniquePipelineLayout(ctx.device, pipelineLayout);

    VkDescriptorPoolSize poolSizes[3]{};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[0].descriptorCount = kMaxFramesInFlight * 5;
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSizes[1].descriptorCount = kMaxFramesInFlight;
    poolSizes[2].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    poolSizes[2].descriptorCount = 1;

    texSets_.resize(kMaxFramesInFlight);
    uboSets_.resize(kMaxFramesInFlight);

    const std::span<const VkDescriptorPoolSize> poolSizesView(poolSizes, 3);
    constexpr uint32_t kMaxDescriptorSets = kMaxFramesInFlight * 2 + 1;

    std::vector<VkDescriptorSetLayout> tLayouts(kMaxFramesInFlight, texSetLayout_);
    if (!ctx.descriptorAllocator.allocate(ctx.device, poolSizesView, kMaxDescriptorSets,
                                          tLayouts.data(), kMaxFramesInFlight,
                                          texSets_.data()))
    {
        failWaterPass("Failed to allocate water texture descriptors.");
    }

    std::vector<VkDescriptorSetLayout> uLayouts(kMaxFramesInFlight, uboSetLayout_);
    if (!ctx.descriptorAllocator.allocate(ctx.device, poolSizesView, kMaxDescriptorSets,
                                          uLayouts.data(), kMaxFramesInFlight,
                                          uboSets_.data()))
    {
        failWaterPass("Failed to allocate water frame uniform descriptors.");
    }

    VkDescriptorSetLayout waterSetLayoutRaw = waterSetLayout_;
    if (!ctx.descriptorAllocator.allocate(ctx.device, poolSizesView, kMaxDescriptorSets,
                                          &waterSetLayoutRaw, 1, &waterSet_))
    {
        failWaterPass("Failed to allocate water volume descriptors.");
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
            failWaterPass("vkMapMemory failed for water frame uniform buffers.");
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

    createRefractionSources(ctx, ci, lighting);
    createNoiseTexture(ctx, ci);
    createTargets(ctx, ci, gbuffer);
    updateDescriptorSets(ctx, gbuffer, lighting, planarReflection);
    updateWaterDescriptor(ctx);
    createPipeline(ctx);
}

void WaterPass::destroy(VulkanContext& ctx)
{
    destroyPipeline(ctx);
    destroyTargets(ctx);
    destroyRefractionSources(ctx);
    destroyNoiseTexture(ctx);

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

    texSetLayout_ = VK_NULL_HANDLE;
    uboSetLayout_ = VK_NULL_HANDLE;
    waterSetLayout_ = VK_NULL_HANDLE;
    pipelineLayout_.reset();
    texSets_.clear();
    uboSets_.clear();
    waterSet_ = VK_NULL_HANDLE;
    waterVolumeBuffer_ = VK_NULL_HANDLE;
}

void WaterPass::onResize(VulkanContext& ctx, const PassCreateInfo& ci, const GBufferPass& gbuffer,
                         const LightingPass& lighting,
                         const LightingPass* planarReflection)
{
    extent_ = ci.extent;
    depthFormat_ = gbuffer.depthFormat();
    destroyPipeline(ctx);
    destroyTargets(ctx);
    destroyRefractionSources(ctx);
    createTargets(ctx, ci, gbuffer);
    createRefractionSources(ctx, ci, lighting);
    updateDescriptorSets(ctx, gbuffer, lighting, planarReflection);
    updateWaterDescriptor(ctx);
    createPipeline(ctx);
}

void WaterPass::recordSceneColorCopy(const FrameContext& fc, const LightingPass& lighting)
{
    recordRefractionCopy(fc, lighting);
}

void WaterPass::record(VulkanContext& ctx, const FrameContext& fc, const FrameUbo& frameUbo,
                       const LightingPass& lighting, const LightingPass* planarReflection,
                       bool usePlanarReflection, const MeshGpu& mesh, bool drawWater)
{
    if (fc.frameIndex >= targets_.size())
    {
        return;
    }

    updateUbo(fc.frameIndex, frameUbo);
    recordRefractionCopy(fc, lighting);

    VkDescriptorImageInfo planarInfo{};
    if (usePlanarReflection && planarReflection != nullptr)
    {
        const auto& planarTarget = planarReflection->lit(fc.frameIndex);
        planarInfo.sampler = planarTarget.sampler;
        planarInfo.imageView = planarTarget.view;
        planarInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    }
    else
    {
        planarInfo.sampler = refractionSources_[fc.frameIndex].sampler;
        planarInfo.imageView = refractionSources_[fc.frameIndex].view;
        planarInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    }

    VkWriteDescriptorSet planarWrite{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    planarWrite.dstSet = texSets_[fc.frameIndex];
    planarWrite.dstBinding = 4;
    planarWrite.descriptorCount = 1;
    planarWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    planarWrite.pImageInfo = &planarInfo;
    vkUpdateDescriptorSets(ctx.device, 1, &planarWrite, 0, nullptr);

    VkClearValue clears[2]{};
    clears[0].color = {{0.0f, 0.0f, 0.0f, 1.0f}};
    clears[1].depthStencil = {1.0f, 0};

    const WaterTarget& target = targets_[fc.frameIndex];
    VkRenderPassBeginInfo rp{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    rp.renderPass = target.renderPass;
    rp.framebuffer = target.framebuffer;
    rp.renderArea.offset = {0, 0};
    rp.renderArea.extent = extent_;
    rp.clearValueCount = 2;
    rp.pClearValues = clears;

    vkCmdBeginRenderPass(fc.cmd, &rp, VK_SUBPASS_CONTENTS_INLINE);

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

    VkDescriptorSet sets[] = {texSets_[fc.frameIndex], uboSets_[fc.frameIndex], waterSet_};

    vkCmdBindPipeline(fc.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, copyPipeline_.get());
    vkCmdBindDescriptorSets(fc.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout_.get(), 0, 3,
                            sets, 0, nullptr);
    vkCmdDraw(fc.cmd, 3, 1, 0, 0);

    if (drawWater && mesh.vbo != VK_NULL_HANDLE && mesh.vertexCount > 0)
    {
        vkCmdBindPipeline(fc.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, waterPipeline_.get());
        vkCmdBindDescriptorSets(fc.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout_.get(), 0,
                                3, sets, 0, nullptr);

        VkBuffer vertexBuffers[] = {mesh.vbo};
        VkDeviceSize offsets[] = {0};
        vkCmdBindVertexBuffers(fc.cmd, 0, 1, vertexBuffers, offsets);

        if (mesh.useIndex && mesh.ibo != VK_NULL_HANDLE)
        {
            vkCmdBindIndexBuffer(fc.cmd, mesh.ibo, 0, VK_INDEX_TYPE_UINT32);
            vkCmdDrawIndexed(fc.cmd, mesh.indexCount, 1, 0, 0, 0);
        }
        else
        {
            vkCmdDraw(fc.cmd, mesh.vertexCount, 1, 0, 0);
        }
    }

    vkCmdEndRenderPass(fc.cmd);
}

const WaterPass::WaterTarget& WaterPass::target(uint32_t frameIndex) const
{
    return targets_.at(frameIndex);
}

const WaterPass::RefractionSource& WaterPass::refractionSource(uint32_t frameIndex) const
{
    return refractionSources_.at(frameIndex);
}

VkExtent2D WaterPass::extent() const
{
    return extent_;
}

void WaterPass::createPipeline(VulkanContext& ctx)
{
    if (targets_.empty())
    {
        failWaterPass("Water targets must exist before creating the water pipelines.");
    }
    VkPipeline copyPipeline = createFullscreenPipeline(ctx, targets_[0].renderPass,
                                                       pipelineLayout_.get(),
                                                       fullscreenVertPath_.c_str(),
                                                       copyFragPath_.c_str());
    copyPipeline_ = engine::render::UniquePipeline(ctx.device, copyPipeline);
    VkPipeline waterPipeline = createWaterPipeline(ctx, targets_[0].renderPass,
                                                   pipelineLayout_.get(),
                                                   waterVertPath_.c_str(),
                                                   waterFragPath_.c_str());
    waterPipeline_ = engine::render::UniquePipeline(ctx.device, waterPipeline);
}

void WaterPass::destroyPipeline(VulkanContext& ctx)
{
    (void)ctx;
    copyPipeline_.reset();
    waterPipeline_.reset();
}

void WaterPass::createTargets(VulkanContext& ctx, const PassCreateInfo& ci,
                              const GBufferPass& gbuffer)
{
    if (ci.commands == nullptr)
    {
        failWaterPass("PassCreateInfo.commands is required for water target layout transitions.");
    }

    targets_.resize(kMaxFramesInFlight);
    for (size_t i = 0; i < kMaxFramesInFlight; ++i)
    {
        auto& t = targets_[i];
        createImage(ctx, extent_.width, extent_.height, t.format,
                    VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, t.image,
                    t.memory);
        t.view = createImageView(ctx.device, t.image, t.format, VK_IMAGE_ASPECT_COLOR_BIT);

        if (!ctx.samplerCache.get(ctx.device, engine::render::SamplerPreset::ClampLinear,
                                  t.sampler))
        {
            failWaterPass("vkCreateSampler failed for water scene-color targets.");
        }

        transitionImageLayout(ctx, *ci.commands, t.image, VK_IMAGE_ASPECT_COLOR_BIT,
                              VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

        t.renderPass = createWaterRenderPass(ctx.device, t.format, depthFormat_);

        VkImageView attachments[] = {t.view, gbuffer.depth(static_cast<uint32_t>(i)).view};
        VkFramebufferCreateInfo fbci{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
        fbci.renderPass = t.renderPass;
        fbci.attachmentCount = 2;
        fbci.pAttachments = attachments;
        fbci.width = extent_.width;
        fbci.height = extent_.height;
        fbci.layers = 1;
        if (vkCreateFramebuffer(ctx.device, &fbci, nullptr, &t.framebuffer) != VK_SUCCESS)
        {
            failWaterPass("vkCreateFramebuffer failed for water scene-color targets.");
        }
    }
}

void WaterPass::destroyTargets(VulkanContext& ctx)
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
        t = WaterTarget{};
    }
    targets_.clear();
}

void WaterPass::createRefractionSources(VulkanContext& ctx, const PassCreateInfo& ci,
                                        const LightingPass& lighting)
{
    if (ci.commands == nullptr)
    {
        failWaterPass(
            "PassCreateInfo.commands is required for water refraction source transitions.");
    }

    refractionSources_.resize(kMaxFramesInFlight);
    for (size_t i = 0; i < kMaxFramesInFlight; ++i)
    {
        auto& src = refractionSources_[i];
        src.format = lighting.lit(static_cast<uint32_t>(i)).format;
        createImage(ctx, extent_.width, extent_.height, src.format,
                    VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, src.image,
                    src.memory);
        src.view = createImageView(ctx.device, src.image, src.format, VK_IMAGE_ASPECT_COLOR_BIT);

        if (!ctx.samplerCache.get(ctx.device, engine::render::SamplerPreset::ClampLinear,
                                  src.sampler))
        {
            failWaterPass("vkCreateSampler failed for water refraction sources.");
        }

        transitionImageLayout(ctx, *ci.commands, src.image, VK_IMAGE_ASPECT_COLOR_BIT,
                              VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        src.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    }
}

void WaterPass::destroyRefractionSources(VulkanContext& ctx)
{
    for (auto& src : refractionSources_)
    {
        src.sampler = VK_NULL_HANDLE;
        if (src.view != VK_NULL_HANDLE)
        {
            vkDestroyImageView(ctx.device, src.view, nullptr);
        }
        if (src.image != VK_NULL_HANDLE)
        {
            vkDestroyImage(ctx.device, src.image, nullptr);
        }
        if (src.memory != VK_NULL_HANDLE)
        {
            vkFreeMemory(ctx.device, src.memory, nullptr);
        }
        src = RefractionSource{};
    }
    refractionSources_.clear();
}

void WaterPass::createNoiseTexture(VulkanContext& ctx, const PassCreateInfo& ci)
{
    if (ci.commands == nullptr)
    {
        failWaterPass("PassCreateInfo.commands is required for water noise texture upload.");
    }

    createImage(ctx, noiseTexture_.width, noiseTexture_.height, noiseTexture_.format,
                VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, noiseTexture_.image,
                noiseTexture_.memory);
    noiseTexture_.view =
        createImageView(ctx.device, noiseTexture_.image, noiseTexture_.format,
                        VK_IMAGE_ASPECT_COLOR_BIT);

    VkSamplerCreateInfo sci{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    sci.magFilter = VK_FILTER_LINEAR;
    sci.minFilter = VK_FILTER_LINEAR;
    sci.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    sci.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sci.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sci.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sci.anisotropyEnable = VK_FALSE;
    sci.maxAnisotropy = 1.0f;
    if (vkCreateSampler(ctx.device, &sci, nullptr, &noiseTexture_.sampler) != VK_SUCCESS)
    {
        failWaterPass("vkCreateSampler failed for the water noise texture.");
    }

    std::vector<uint8_t> noise(static_cast<size_t>(noiseTexture_.width) * noiseTexture_.height);
    for (uint32_t y = 0; y < noiseTexture_.height; ++y)
    {
        for (uint32_t x = 0; x < noiseTexture_.width; ++x)
        {
            const uint32_t h0 = hashNoiseU32((x * 73856093u) ^ (y * 19349663u) ^ 0x9e3779b9u);
            const uint32_t h1 =
                hashNoiseU32(((x + 17u) * 83492791u) ^ ((y + 31u) * 1234567u) ^ 0x85ebca6bu);
            const uint32_t mixed = (h0 & 0xFFu) + ((h1 >> 8) & 0xFFu);
            noise[static_cast<size_t>(y) * noiseTexture_.width + x] =
                static_cast<uint8_t>(mixed >> 1u);
        }
    }

    VkBuffer staging = VK_NULL_HANDLE;
    VkDeviceMemory stagingMem = VK_NULL_HANDLE;
    const VkDeviceSize noiseSize = static_cast<VkDeviceSize>(noise.size());
    createBuffer(ctx, noiseSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                 staging, stagingMem);

    void* mapped = nullptr;
    if (vkMapMemory(ctx.device, stagingMem, 0, noiseSize, 0, &mapped) != VK_SUCCESS)
    {
        failWaterPass("vkMapMemory failed for water noise staging uploads.");
    }
    std::memcpy(mapped, noise.data(), noise.size());
    vkUnmapMemory(ctx.device, stagingMem);

    transitionImageLayout(ctx, *ci.commands, noiseTexture_.image, VK_IMAGE_ASPECT_COLOR_BIT,
                          VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    copyBufferToImage(ctx, *ci.commands, staging, noiseTexture_.image, noiseTexture_.width,
                      noiseTexture_.height);
    transitionImageLayout(ctx, *ci.commands, noiseTexture_.image, VK_IMAGE_ASPECT_COLOR_BIT,
                          VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                          VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    vkDestroyBuffer(ctx.device, staging, nullptr);
    vkFreeMemory(ctx.device, stagingMem, nullptr);
}

void WaterPass::destroyNoiseTexture(VulkanContext& ctx)
{
    if (noiseTexture_.sampler != VK_NULL_HANDLE)
    {
        vkDestroySampler(ctx.device, noiseTexture_.sampler, nullptr);
        noiseTexture_.sampler = VK_NULL_HANDLE;
    }
    if (noiseTexture_.view != VK_NULL_HANDLE)
    {
        vkDestroyImageView(ctx.device, noiseTexture_.view, nullptr);
        noiseTexture_.view = VK_NULL_HANDLE;
    }
    if (noiseTexture_.image != VK_NULL_HANDLE)
    {
        vkDestroyImage(ctx.device, noiseTexture_.image, nullptr);
        noiseTexture_.image = VK_NULL_HANDLE;
    }
    if (noiseTexture_.memory != VK_NULL_HANDLE)
    {
        vkFreeMemory(ctx.device, noiseTexture_.memory, nullptr);
        noiseTexture_.memory = VK_NULL_HANDLE;
    }
}

void WaterPass::recordRefractionCopy(const FrameContext& fc, const LightingPass& lighting)
{
    if (fc.frameIndex >= refractionSources_.size())
    {
        return;
    }

    const auto& lit = lighting.lit(fc.frameIndex);
    auto& refr = refractionSources_[fc.frameIndex];

    std::array<VkImageMemoryBarrier, 2> pre{};
    pre[0].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    pre[0].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    pre[0].dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    pre[0].oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    pre[0].newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    pre[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    pre[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    pre[0].image = lit.image;
    pre[0].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    pre[0].subresourceRange.baseMipLevel = 0;
    pre[0].subresourceRange.levelCount = 1;
    pre[0].subresourceRange.baseArrayLayer = 0;
    pre[0].subresourceRange.layerCount = 1;

    pre[1].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    pre[1].srcAccessMask =
        (refr.layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) ? VK_ACCESS_SHADER_READ_BIT : 0;
    pre[1].dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    pre[1].oldLayout = refr.layout;
    pre[1].newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    pre[1].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    pre[1].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    pre[1].image = refr.image;
    pre[1].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    pre[1].subresourceRange.baseMipLevel = 0;
    pre[1].subresourceRange.levelCount = 1;
    pre[1].subresourceRange.baseArrayLayer = 0;
    pre[1].subresourceRange.layerCount = 1;

    VkPipelineStageFlags preSrcStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    if (refr.layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
    {
        preSrcStage |= VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    }

    vkCmdPipelineBarrier(fc.cmd, preSrcStage, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0,
                         nullptr, static_cast<uint32_t>(pre.size()), pre.data());

    VkImageCopy copy{};
    copy.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copy.srcSubresource.mipLevel = 0;
    copy.srcSubresource.baseArrayLayer = 0;
    copy.srcSubresource.layerCount = 1;
    copy.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copy.dstSubresource.mipLevel = 0;
    copy.dstSubresource.baseArrayLayer = 0;
    copy.dstSubresource.layerCount = 1;
    copy.extent = {extent_.width, extent_.height, 1};

    vkCmdCopyImage(fc.cmd, lit.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, refr.image,
                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);

    std::array<VkImageMemoryBarrier, 2> post{};
    post[0].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    post[0].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    post[0].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    post[0].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    post[0].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    post[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    post[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    post[0].image = refr.image;
    post[0].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    post[0].subresourceRange.baseMipLevel = 0;
    post[0].subresourceRange.levelCount = 1;
    post[0].subresourceRange.baseArrayLayer = 0;
    post[0].subresourceRange.layerCount = 1;

    post[1].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    post[1].srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    post[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    post[1].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    post[1].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    post[1].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    post[1].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    post[1].image = lit.image;
    post[1].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    post[1].subresourceRange.baseMipLevel = 0;
    post[1].subresourceRange.levelCount = 1;
    post[1].subresourceRange.baseArrayLayer = 0;
    post[1].subresourceRange.layerCount = 1;

    vkCmdPipelineBarrier(fc.cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr,
                         static_cast<uint32_t>(post.size()), post.data());

    refr.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
}

void WaterPass::updateUbo(uint32_t frameIndex, const FrameUbo& frameUbo)
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

void WaterPass::updateDescriptorSets(VulkanContext& ctx, const GBufferPass& gbuffer,
                                     const LightingPass& lighting,
                                     const LightingPass* planarReflection)
{
    (void)lighting;
    const size_t count = std::min(texSets_.size(), static_cast<size_t>(kMaxFramesInFlight));
    for (size_t i = 0; i < count; i++)
    {
        VkDescriptorImageInfo infos[5]{};
        infos[0].sampler = refractionSources_[i].sampler;
        infos[0].imageView = refractionSources_[i].view;
        infos[0].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        infos[1].sampler = gbuffer.depth(static_cast<uint32_t>(i)).sampler;
        infos[1].imageView = gbuffer.depth(static_cast<uint32_t>(i)).view;
        infos[1].imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;

        infos[2].sampler =
            gbuffer.color(static_cast<uint32_t>(i), GBufferPass::Slot::WaterDist).sampler;
        infos[2].imageView =
            gbuffer.color(static_cast<uint32_t>(i), GBufferPass::Slot::WaterDist).view;
        infos[2].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        infos[3].sampler = noiseTexture_.sampler;
        infos[3].imageView = noiseTexture_.view;
        infos[3].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        if (planarReflection != nullptr)
        {
            const auto& planarTarget = planarReflection->lit(static_cast<uint32_t>(i));
            infos[4].sampler = planarTarget.sampler;
            infos[4].imageView = planarTarget.view;
            infos[4].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        }
        else
        {
            infos[4] = infos[0];
        }

        VkWriteDescriptorSet writes[5]{};
        VkWriteDescriptorSet w0{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        w0.dstSet = texSets_[i];
        w0.dstBinding = 0;
        w0.descriptorCount = 1;
        w0.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        w0.pImageInfo = &infos[0];
        writes[0] = w0;

        VkWriteDescriptorSet w1{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        w1.dstSet = texSets_[i];
        w1.dstBinding = 1;
        w1.descriptorCount = 1;
        w1.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        w1.pImageInfo = &infos[1];
        writes[1] = w1;

        VkWriteDescriptorSet w2{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        w2.dstSet = texSets_[i];
        w2.dstBinding = 2;
        w2.descriptorCount = 1;
        w2.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        w2.pImageInfo = &infos[2];
        writes[2] = w2;

        VkWriteDescriptorSet w3{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        w3.dstSet = texSets_[i];
        w3.dstBinding = 3;
        w3.descriptorCount = 1;
        w3.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        w3.pImageInfo = &infos[3];
        writes[3] = w3;

        VkWriteDescriptorSet w4{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        w4.dstSet = texSets_[i];
        w4.dstBinding = 4;
        w4.descriptorCount = 1;
        w4.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        w4.pImageInfo = &infos[4];
        writes[4] = w4;

        vkUpdateDescriptorSets(ctx.device, 5, writes, 0, nullptr);
    }
}

void WaterPass::updateWaterDescriptor(VulkanContext& ctx)
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
