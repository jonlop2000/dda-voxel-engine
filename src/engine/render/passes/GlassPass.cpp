#include "engine/render/passes/GlassPass.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <span>

#include "engine/render/passes/GBufferPass.h"
#include "engine/render/passes/LightingPass.h"
#include "engine/render/passes/PassCreateInfo.h"
#include "engine/render/passes/WaterPass.h"
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
};

constexpr const char* kGlassPassSubsystem = "GlassPass";
constexpr const char* kGlassPassRationale =
    "GlassPass is required because glass back-depth and shading composite into the main lit "
    "buffer and sample water/scene-color inputs.";

[[noreturn]] void failGlassPass(std::string_view detail)
{
    logAndExit(kGlassPassSubsystem,
               std::string("Required pass failure: ") + std::string(detail) + " " +
                   kGlassPassRationale);
}

std::vector<char> loadGlassShaderOrFatal(const char* path, std::string_view stage)
{
    std::vector<char> code;
    std::string error;
    if (!tryReadFile(path, code, &error))
    {
        failGlassPass(std::string("Failed to read ") + std::string(stage) + " shader '" + path +
                      "': " + error);
    }
    return code;
}

VkShaderModule createGlassShaderModuleOrFatal(VkDevice device, const std::vector<char>& code,
                                              const char* path, std::string_view stage)
{
    std::string error;
    VkShaderModule module = tryCreateShaderModule(device, code, &error);
    if (module == VK_NULL_HANDLE)
    {
        failGlassPass(std::string("Failed to create ") + std::string(stage) +
                      " shader module for '" + path + "': " +
                      (error.empty() ? "vkCreateShaderModule failed." : error));
    }
    return module;
}

VkRenderPass createBackDepthRenderPass(VkDevice device, VkFormat colorFormat, VkFormat depthFormat)
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
    depth.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depth.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    depth.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depth.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth.initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    depth.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    std::array<VkAttachmentDescription, 2> attachments = {color, depth};

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
    deps[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    deps[0].dstSubpass = 0;
    deps[0].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    deps[0].dstStageMask =
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    deps[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    deps[0].dstAccessMask =
        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    deps[1].srcSubpass = 0;
    deps[1].dstSubpass = VK_SUBPASS_EXTERNAL;
    deps[1].srcStageMask =
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    deps[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    deps[1].srcAccessMask =
        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    deps[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    VkRenderPassCreateInfo rpci{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    rpci.attachmentCount = static_cast<uint32_t>(attachments.size());
    rpci.pAttachments = attachments.data();
    rpci.subpassCount = 1;
    rpci.pSubpasses = &subpass;
    rpci.dependencyCount = 2;
    rpci.pDependencies = deps;

    VkRenderPass rp = VK_NULL_HANDLE;
    if (vkCreateRenderPass(device, &rpci, nullptr, &rp) != VK_SUCCESS)
    {
        failGlassPass("vkCreateRenderPass failed for glass back-depth targets.");
    }
    return rp;
}

VkRenderPass createShadeRenderPass(VkDevice device, VkFormat colorFormat, VkFormat depthFormat)
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

    VkSubpassDependency deps[2]{};
    deps[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    deps[0].dstSubpass = 0;
    deps[0].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    deps[0].dstStageMask =
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    deps[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    deps[0].dstAccessMask =
        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;

    deps[1].srcSubpass = 0;
    deps[1].dstSubpass = VK_SUBPASS_EXTERNAL;
    deps[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    deps[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    deps[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    deps[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    VkRenderPassCreateInfo rpci{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    rpci.attachmentCount = static_cast<uint32_t>(attachments.size());
    rpci.pAttachments = attachments.data();
    rpci.subpassCount = 1;
    rpci.pSubpasses = &subpass;
    rpci.dependencyCount = 2;
    rpci.pDependencies = deps;

    VkRenderPass rp = VK_NULL_HANDLE;
    if (vkCreateRenderPass(device, &rpci, nullptr, &rp) != VK_SUCCESS)
    {
        failGlassPass("vkCreateRenderPass failed for glass shading targets.");
    }
    return rp;
}

VkPipeline createFullscreenPipeline(VulkanContext& ctx, VkRenderPass renderPass,
                                    VkPipelineLayout pipelineLayout, const char* vertPath,
                                    const char* fragPath)
{
    const std::vector<char> vertCode = loadGlassShaderOrFatal(vertPath, "vertex");
    const std::vector<char> fragCode = loadGlassShaderOrFatal(fragPath, "fragment");

    VkShaderModule vertModule =
        createGlassShaderModuleOrFatal(ctx.device, vertCode, vertPath, "vertex");
    VkShaderModule fragModule =
        createGlassShaderModuleOrFatal(ctx.device, fragCode, fragPath, "fragment");

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
        failGlassPass("vkCreateGraphicsPipelines failed for the fullscreen glass copy pipeline.");
    }

    vkDestroyShaderModule(ctx.device, fragModule, nullptr);
    vkDestroyShaderModule(ctx.device, vertModule, nullptr);

    return pipeline;
}

VkPipeline createGlassBackDepthPipeline(VulkanContext& ctx, VkRenderPass renderPass,
                                        VkPipelineLayout pipelineLayout, const char* vertPath,
                                        const char* fragPath)
{
    const std::vector<char> vertCode = loadGlassShaderOrFatal(vertPath, "vertex");
    const std::vector<char> fragCode = loadGlassShaderOrFatal(fragPath, "fragment");

    VkShaderModule vertModule =
        createGlassShaderModuleOrFatal(ctx.device, vertCode, vertPath, "vertex");
    VkShaderModule fragModule =
        createGlassShaderModuleOrFatal(ctx.device, fragCode, fragPath, "fragment");

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
    raster.cullMode = VK_CULL_MODE_FRONT_BIT;
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
        failGlassPass(
            "vkCreateGraphicsPipelines failed for the glass back-depth mesh pipeline.");
    }

    vkDestroyShaderModule(ctx.device, fragModule, nullptr);
    vkDestroyShaderModule(ctx.device, vertModule, nullptr);

    return pipeline;
}

VkPipeline createGlassShadePipeline(VulkanContext& ctx, VkRenderPass renderPass,
                                    VkPipelineLayout pipelineLayout, const char* vertPath,
                                    const char* fragPath)
{
    const std::vector<char> vertCode = loadGlassShaderOrFatal(vertPath, "vertex");
    const std::vector<char> fragCode = loadGlassShaderOrFatal(fragPath, "fragment");

    VkShaderModule vertModule =
        createGlassShaderModuleOrFatal(ctx.device, vertCode, vertPath, "vertex");
    VkShaderModule fragModule =
        createGlassShaderModuleOrFatal(ctx.device, fragCode, fragPath, "fragment");

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
    raster.cullMode = VK_CULL_MODE_BACK_BIT;
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
        failGlassPass("vkCreateGraphicsPipelines failed for the glass shading mesh pipeline.");
    }

    vkDestroyShaderModule(ctx.device, fragModule, nullptr);
    vkDestroyShaderModule(ctx.device, vertModule, nullptr);

    return pipeline;
}
} // namespace

void GlassPass::setShaderPaths(const std::string& backDepthVert, const std::string& backDepthFrag,
                               const std::string& fullscreenVert, const std::string& copyFrag,
                               const std::string& shadeVert, const std::string& shadeFrag)
{
    backVertPath_ = backDepthVert;
    backFragPath_ = backDepthFrag;
    fullscreenVertPath_ = fullscreenVert;
    copyFragPath_ = copyFrag;
    shadeVertPath_ = shadeVert;
    shadeFragPath_ = shadeFrag;
}

void GlassPass::setWaterVolumeBuffer(VkBuffer buffer)
{
    waterVolumeBuffer_ = buffer;
}

void GlassPass::create(VulkanContext& ctx, const PassCreateInfo& ci, const GBufferPass& gbuffer,
                       const LightingPass& lighting, const WaterPass& water)
{
    if (ci.commands == nullptr)
    {
        failGlassPass("PassCreateInfo.commands is required for glass target transitions.");
    }

    extent_ = ci.extent;
    if (waterVolumeBuffer_ == VK_NULL_HANDLE)
    {
        failGlassPass("A valid water volume buffer is required for glass atmosphere descriptors.");
    }

    VkDescriptorSetLayoutBinding backUboBinding{};
    backUboBinding.binding = 0;
    backUboBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    backUboBinding.descriptorCount = 1;
    backUboBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo backUboLayoutInfo{
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    backUboLayoutInfo.bindingCount = 1;
    backUboLayoutInfo.pBindings = &backUboBinding;
    if (!ctx.descriptorLayoutCache.get(ctx.device, backUboLayoutInfo, backUboLayout_))
    {
        failGlassPass("vkCreateDescriptorSetLayout failed for glass back-depth UBO bindings.");
    }

    VkDescriptorSetLayoutBinding texBindings[3]{};
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

    VkDescriptorSetLayoutCreateInfo texLayoutInfo{
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    texLayoutInfo.bindingCount = 3;
    texLayoutInfo.pBindings = texBindings;
    if (!ctx.descriptorLayoutCache.get(ctx.device, texLayoutInfo, texSetLayout_))
    {
        failGlassPass("vkCreateDescriptorSetLayout failed for glass texture bindings.");
    }

    VkDescriptorSetLayoutBinding shadeUboBinding{};
    shadeUboBinding.binding = 0;
    shadeUboBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    shadeUboBinding.descriptorCount = 1;
    shadeUboBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo shadeUboLayoutInfo{
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    shadeUboLayoutInfo.bindingCount = 1;
    shadeUboLayoutInfo.pBindings = &shadeUboBinding;
    if (!ctx.descriptorLayoutCache.get(ctx.device, shadeUboLayoutInfo, shadeUboLayout_))
    {
        failGlassPass("vkCreateDescriptorSetLayout failed for glass shade UBO bindings.");
    }

    VkDescriptorSetLayoutBinding waterBinding{};
    waterBinding.binding = 0;
    waterBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    waterBinding.descriptorCount = 1;
    waterBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo waterLayoutInfo{
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    waterLayoutInfo.bindingCount = 1;
    waterLayoutInfo.pBindings = &waterBinding;
    if (!ctx.descriptorLayoutCache.get(ctx.device, waterLayoutInfo, waterSetLayout_))
    {
        failGlassPass("vkCreateDescriptorSetLayout failed for glass water-volume bindings.");
    }

    VkPipelineLayoutCreateInfo backPlci{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    backPlci.setLayoutCount = 1;
    VkDescriptorSetLayout backUboLayoutRaw = backUboLayout_;
    backPlci.pSetLayouts = &backUboLayoutRaw;
    VkPushConstantRange backPcr{};
    backPcr.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    backPcr.offset = 0;
    backPcr.size = sizeof(ObjectPushConstants);
    backPlci.pushConstantRangeCount = 1;
    backPlci.pPushConstantRanges = &backPcr;
    VkPipelineLayout backPipelineLayout = VK_NULL_HANDLE;
    if (vkCreatePipelineLayout(ctx.device, &backPlci, nullptr, &backPipelineLayout) !=
        VK_SUCCESS)
    {
        failGlassPass("vkCreatePipelineLayout failed for glass back-depth rendering.");
    }
    backPipelineLayout_ = engine::render::UniquePipelineLayout(ctx.device, backPipelineLayout);

    VkDescriptorSetLayout shadeSetLayouts[] = {
        texSetLayout_, shadeUboLayout_, waterSetLayout_};
    VkPipelineLayoutCreateInfo shadePlci{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    shadePlci.setLayoutCount = 3;
    shadePlci.pSetLayouts = shadeSetLayouts;
    VkPushConstantRange shadePcr{};
    shadePcr.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    shadePcr.offset = 0;
    shadePcr.size = sizeof(ObjectPushConstants);
    shadePlci.pushConstantRangeCount = 1;
    shadePlci.pPushConstantRanges = &shadePcr;
    VkPipelineLayout shadePipelineLayout = VK_NULL_HANDLE;
    if (vkCreatePipelineLayout(ctx.device, &shadePlci, nullptr, &shadePipelineLayout) !=
        VK_SUCCESS)
    {
        failGlassPass("vkCreatePipelineLayout failed for glass shading.");
    }
    shadePipelineLayout_ = engine::render::UniquePipelineLayout(ctx.device, shadePipelineLayout);

    VkDescriptorPoolSize poolSizes[3]{};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[0].descriptorCount = kMaxFramesInFlight * 3;
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSizes[1].descriptorCount = kMaxFramesInFlight * 2;
    poolSizes[2].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    poolSizes[2].descriptorCount = 1;

    backUboSets_.resize(kMaxFramesInFlight);
    shadeUboSets_.resize(kMaxFramesInFlight);
    texSets_.resize(kMaxFramesInFlight);

    const std::span<const VkDescriptorPoolSize> poolSizesView(poolSizes, 3);
    constexpr uint32_t kMaxDescriptorSets = kMaxFramesInFlight * 3 + 1;

    std::vector<VkDescriptorSetLayout> backLayouts(kMaxFramesInFlight, backUboLayout_);
    if (!ctx.descriptorAllocator.allocate(ctx.device, poolSizesView, kMaxDescriptorSets,
                                          backLayouts.data(), kMaxFramesInFlight,
                                          backUboSets_.data()))
    {
        failGlassPass("Failed to allocate glass back-depth UBO descriptors.");
    }

    std::vector<VkDescriptorSetLayout> shadeLayouts(kMaxFramesInFlight, shadeUboLayout_);
    if (!ctx.descriptorAllocator.allocate(ctx.device, poolSizesView, kMaxDescriptorSets,
                                          shadeLayouts.data(), kMaxFramesInFlight,
                                          shadeUboSets_.data()))
    {
        failGlassPass("Failed to allocate glass shade UBO descriptors.");
    }

    std::vector<VkDescriptorSetLayout> texLayouts(kMaxFramesInFlight, texSetLayout_);
    if (!ctx.descriptorAllocator.allocate(ctx.device, poolSizesView, kMaxDescriptorSets,
                                          texLayouts.data(), kMaxFramesInFlight,
                                          texSets_.data()))
    {
        failGlassPass("Failed to allocate glass texture descriptors.");
    }

    VkDescriptorSetLayout waterLayout = waterSetLayout_;
    if (!ctx.descriptorAllocator.allocate(ctx.device, poolSizesView, kMaxDescriptorSets,
                                          &waterLayout, 1, &waterSet_))
    {
        failGlassPass("Failed to allocate glass water-volume descriptors.");
    }

    backUbos_.resize(kMaxFramesInFlight);
    shadeUbos_.resize(kMaxFramesInFlight);
    for (size_t i = 0; i < kMaxFramesInFlight; i++)
    {
        createBuffer(ctx, sizeof(FrameUbo), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                     backUbos_[i].buffer, backUbos_[i].memory);

        if (vkMapMemory(ctx.device, backUbos_[i].memory, 0, sizeof(FrameUbo), 0,
                        &backUbos_[i].mapped) != VK_SUCCESS)
        {
            failGlassPass("vkMapMemory failed for glass back-depth uniform buffers.");
        }

        VkDescriptorBufferInfo backInfo{};
        backInfo.buffer = backUbos_[i].buffer;
        backInfo.offset = 0;
        backInfo.range = sizeof(FrameUbo);

        VkWriteDescriptorSet backWrite{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        backWrite.dstSet = backUboSets_[i];
        backWrite.dstBinding = 0;
        backWrite.descriptorCount = 1;
        backWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        backWrite.pBufferInfo = &backInfo;
        vkUpdateDescriptorSets(ctx.device, 1, &backWrite, 0, nullptr);

        createBuffer(ctx, sizeof(FrameUbo), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                     shadeUbos_[i].buffer, shadeUbos_[i].memory);

        if (vkMapMemory(ctx.device, shadeUbos_[i].memory, 0, sizeof(FrameUbo), 0,
                        &shadeUbos_[i].mapped) != VK_SUCCESS)
        {
            failGlassPass("vkMapMemory failed for glass shade uniform buffers.");
        }

        VkDescriptorBufferInfo shadeInfo{};
        shadeInfo.buffer = shadeUbos_[i].buffer;
        shadeInfo.offset = 0;
        shadeInfo.range = sizeof(FrameUbo);

        VkWriteDescriptorSet shadeWrite{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        shadeWrite.dstSet = shadeUboSets_[i];
        shadeWrite.dstBinding = 0;
        shadeWrite.descriptorCount = 1;
        shadeWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        shadeWrite.pBufferInfo = &shadeInfo;
        vkUpdateDescriptorSets(ctx.device, 1, &shadeWrite, 0, nullptr);
    }

    updateWaterDescriptor(ctx);

    createBackDepthTargets(ctx, ci);
    createShadeTargets(ctx, gbuffer, lighting);
    updateDescriptorSets(ctx, gbuffer, water);
    createBackDepthPipeline(ctx);
    createShadePipeline(ctx);
}

void GlassPass::destroy(VulkanContext& ctx)
{
    destroyBackDepthPipeline(ctx);
    destroyShadePipeline(ctx);
    destroyShadeTargets(ctx);
    destroyBackDepthTargets(ctx);

    for (auto& ubo : backUbos_)
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
    backUbos_.clear();

    for (auto& ubo : shadeUbos_)
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
    shadeUbos_.clear();

    backUboLayout_ = VK_NULL_HANDLE;
    texSetLayout_ = VK_NULL_HANDLE;
    shadeUboLayout_ = VK_NULL_HANDLE;
    waterSetLayout_ = VK_NULL_HANDLE;
    backPipelineLayout_.reset();
    shadePipelineLayout_.reset();

    backUboSets_.clear();
    shadeUboSets_.clear();
    texSets_.clear();
    waterSet_ = VK_NULL_HANDLE;
    waterVolumeBuffer_ = VK_NULL_HANDLE;
}

void GlassPass::onResize(VulkanContext& ctx, const PassCreateInfo& ci, const GBufferPass& gbuffer,
                         const LightingPass& lighting, const WaterPass& water)
{
    extent_ = ci.extent;
    destroyBackDepthPipeline(ctx);
    destroyShadePipeline(ctx);
    destroyShadeTargets(ctx);
    destroyBackDepthTargets(ctx);
    createBackDepthTargets(ctx, ci);
    createShadeTargets(ctx, gbuffer, lighting);
    updateDescriptorSets(ctx, gbuffer, water);
    createBackDepthPipeline(ctx);
    createShadePipeline(ctx);
}

void GlassPass::updateSceneColorSource(VulkanContext& ctx, uint32_t frameIndex,
                                       VkImageView sceneColorView,
                                       VkSampler sceneColorSampler)
{
    if (frameIndex >= texSets_.size())
    {
        return;
    }

    VkDescriptorImageInfo sceneInfo{};
    sceneInfo.sampler = sceneColorSampler;
    sceneInfo.imageView = sceneColorView;
    sceneInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    write.dstSet = texSets_[frameIndex];
    write.dstBinding = 0;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo = &sceneInfo;
    vkUpdateDescriptorSets(ctx.device, 1, &write, 0, nullptr);
}

void GlassPass::recordBackDepth(VulkanContext& ctx, const FrameContext& fc,
                                const FrameUbo& frameUbo,
                                const std::vector<RenderObject>& objects)
{
    (void)ctx;
    if (fc.frameIndex >= backDepthTargets_.size())
    {
        return;
    }

    updateBackUbo(fc.frameIndex, frameUbo);

    VkClearValue clears[2]{};
    clears[0].color = {{0.0f, 0.0f, 0.0f, 0.0f}};
    clears[1].depthStencil = {1.0f, 0};

    const BackDepthTarget& target = backDepthTargets_[fc.frameIndex];
    VkRenderPassBeginInfo rp{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    rp.renderPass = target.renderPass;
    rp.framebuffer = target.framebuffer;
    rp.renderArea.offset = {0, 0};
    rp.renderArea.extent = extent_;
    rp.clearValueCount = 2;
    rp.pClearValues = clears;

    vkCmdBeginRenderPass(fc.cmd, &rp, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(fc.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, backPipeline_.get());
    vkCmdBindDescriptorSets(fc.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, backPipelineLayout_.get(), 0,
                            1, &backUboSets_[fc.frameIndex], 0, nullptr);

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

    if (!objects.empty())
    {
        for (const auto& obj : objects)
        {
            if (obj.mesh == nullptr || obj.mesh->vbo == VK_NULL_HANDLE)
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
            vkCmdPushConstants(fc.cmd, backPipelineLayout_.get(), VK_SHADER_STAGE_VERTEX_BIT, 0,
                               sizeof(pc), &pc);

            if (obj.mesh->useIndex)
            {
                vkCmdDrawIndexed(fc.cmd, obj.mesh->indexCount, 1, 0, 0, 0);
            }
            else
            {
                vkCmdDraw(fc.cmd, obj.mesh->vertexCount, 1, 0, 0);
            }
        }
    }

    vkCmdEndRenderPass(fc.cmd);
}

void GlassPass::recordShade(VulkanContext& ctx, const FrameContext& fc, const FrameUbo& frameUbo,
                            const std::vector<RenderObject>& objects, bool drawGlass)
{
    (void)ctx;
    if (fc.frameIndex >= shadeTargets_.size())
    {
        return;
    }

    updateShadeUbo(fc.frameIndex, frameUbo);

    VkClearValue clears[2]{};
    clears[0].color = {{0.0f, 0.0f, 0.0f, 1.0f}};
    clears[1].depthStencil = {1.0f, 0};

    const ShadeTarget& target = shadeTargets_[fc.frameIndex];
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

    VkDescriptorSet sets[] = {
        texSets_[fc.frameIndex], shadeUboSets_[fc.frameIndex], waterSet_};

    const int debugMode = static_cast<int>(frameUbo.params0.w + 0.5f);
    const bool debugSceneColor = (debugMode == 4);
    if (!debugSceneColor)
    {
        vkCmdBindPipeline(fc.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, copyPipeline_.get());
        vkCmdBindDescriptorSets(fc.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                shadePipelineLayout_.get(), 0, 3, sets, 0, nullptr);
        vkCmdDraw(fc.cmd, 3, 1, 0, 0);
    }

    if (drawGlass && !objects.empty())
    {
        vkCmdBindPipeline(fc.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, shadePipeline_.get());
        vkCmdBindDescriptorSets(fc.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                shadePipelineLayout_.get(), 0, 3, sets, 0, nullptr);

        for (const auto& obj : objects)
        {
            if (obj.mesh == nullptr || obj.mesh->vbo == VK_NULL_HANDLE)
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
            vkCmdPushConstants(fc.cmd, shadePipelineLayout_.get(), VK_SHADER_STAGE_VERTEX_BIT, 0,
                               sizeof(pc), &pc);

            if (obj.mesh->useIndex)
            {
                vkCmdDrawIndexed(fc.cmd, obj.mesh->indexCount, 1, 0, 0, 0);
            }
            else
            {
                vkCmdDraw(fc.cmd, obj.mesh->vertexCount, 1, 0, 0);
            }
        }
    }

    vkCmdEndRenderPass(fc.cmd);
}

const GlassPass::BackDepthTarget& GlassPass::backDepth(uint32_t frameIndex) const
{
    return backDepthTargets_.at(frameIndex);
}

VkExtent2D GlassPass::extent() const
{
    return extent_;
}

void GlassPass::createBackDepthPipeline(VulkanContext& ctx)
{
    if (backDepthTargets_.empty())
    {
        failGlassPass("Back-depth targets must exist before creating glass back-depth pipelines.");
    }

    VkPipeline backPipeline = createGlassBackDepthPipeline(
        ctx, backDepthTargets_[0].renderPass, backPipelineLayout_.get(), backVertPath_.c_str(),
        backFragPath_.c_str());
    backPipeline_ = engine::render::UniquePipeline(ctx.device, backPipeline);
}

void GlassPass::destroyBackDepthPipeline(VulkanContext& ctx)
{
    (void)ctx;
    backPipeline_.reset();
}

void GlassPass::createShadePipeline(VulkanContext& ctx)
{
    if (shadeTargets_.empty())
    {
        failGlassPass("Shade targets must exist before creating glass shading pipelines.");
    }

    VkPipeline copyPipeline = createFullscreenPipeline(ctx, shadeTargets_[0].renderPass,
                                                       shadePipelineLayout_.get(),
                                                       fullscreenVertPath_.c_str(),
                                                       copyFragPath_.c_str());
    copyPipeline_ = engine::render::UniquePipeline(ctx.device, copyPipeline);
    VkPipeline shadePipeline = createGlassShadePipeline(ctx, shadeTargets_[0].renderPass,
                                                        shadePipelineLayout_.get(),
                                                        shadeVertPath_.c_str(),
                                                        shadeFragPath_.c_str());
    shadePipeline_ = engine::render::UniquePipeline(ctx.device, shadePipeline);
}

void GlassPass::destroyShadePipeline(VulkanContext& ctx)
{
    (void)ctx;
    copyPipeline_.reset();
    shadePipeline_.reset();
}

void GlassPass::createBackDepthTargets(VulkanContext& ctx, const PassCreateInfo& ci)
{
    if (ci.commands == nullptr)
    {
        failGlassPass("PassCreateInfo.commands is required for glass back-depth transitions.");
    }

    backDepthTargets_.resize(kMaxFramesInFlight);
    for (size_t i = 0; i < kMaxFramesInFlight; ++i)
    {
        auto& t = backDepthTargets_[i];

        createImage(ctx, extent_.width, extent_.height, t.colorFormat,
                    VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, t.colorImage,
                    t.colorMemory);
        t.colorView = createImageView(ctx.device, t.colorImage, t.colorFormat,
                                      VK_IMAGE_ASPECT_COLOR_BIT);

        if (!ctx.samplerCache.get(ctx.device, engine::render::SamplerPreset::ClampNearest,
                                  t.colorSampler))
        {
            failGlassPass("vkCreateSampler failed for glass back-depth targets.");
        }

        transitionImageLayout(ctx, *ci.commands, t.colorImage, VK_IMAGE_ASPECT_COLOR_BIT,
                              VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

        createImage(ctx, extent_.width, extent_.height, t.depthFormat,
                    VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, t.depthImage, t.depthMemory);
        t.depthView =
            createImageView(ctx.device, t.depthImage, t.depthFormat, VK_IMAGE_ASPECT_DEPTH_BIT);

        transitionImageLayout(ctx, *ci.commands, t.depthImage, VK_IMAGE_ASPECT_DEPTH_BIT,
                              VK_IMAGE_LAYOUT_UNDEFINED,
                              VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);

        t.renderPass = createBackDepthRenderPass(ctx.device, t.colorFormat, t.depthFormat);

        VkImageView attachments[] = {t.colorView, t.depthView};
        VkFramebufferCreateInfo fbci{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
        fbci.renderPass = t.renderPass;
        fbci.attachmentCount = 2;
        fbci.pAttachments = attachments;
        fbci.width = extent_.width;
        fbci.height = extent_.height;
        fbci.layers = 1;
        if (vkCreateFramebuffer(ctx.device, &fbci, nullptr, &t.framebuffer) != VK_SUCCESS)
        {
            failGlassPass("vkCreateFramebuffer failed for glass back-depth targets.");
        }
    }
}

void GlassPass::destroyBackDepthTargets(VulkanContext& ctx)
{
    for (auto& t : backDepthTargets_)
    {
        if (t.framebuffer != VK_NULL_HANDLE)
        {
            vkDestroyFramebuffer(ctx.device, t.framebuffer, nullptr);
        }
        if (t.renderPass != VK_NULL_HANDLE)
        {
            vkDestroyRenderPass(ctx.device, t.renderPass, nullptr);
        }
        t.colorSampler = VK_NULL_HANDLE;
        if (t.colorView != VK_NULL_HANDLE)
        {
            vkDestroyImageView(ctx.device, t.colorView, nullptr);
        }
        if (t.colorImage != VK_NULL_HANDLE)
        {
            vkDestroyImage(ctx.device, t.colorImage, nullptr);
        }
        if (t.colorMemory != VK_NULL_HANDLE)
        {
            vkFreeMemory(ctx.device, t.colorMemory, nullptr);
        }
        if (t.depthView != VK_NULL_HANDLE)
        {
            vkDestroyImageView(ctx.device, t.depthView, nullptr);
        }
        if (t.depthImage != VK_NULL_HANDLE)
        {
            vkDestroyImage(ctx.device, t.depthImage, nullptr);
        }
        if (t.depthMemory != VK_NULL_HANDLE)
        {
            vkFreeMemory(ctx.device, t.depthMemory, nullptr);
        }
        t = BackDepthTarget{};
    }
    backDepthTargets_.clear();
}

void GlassPass::createShadeTargets(VulkanContext& ctx, const GBufferPass& gbuffer,
                                   const LightingPass& lighting)
{
    shadeTargets_.resize(kMaxFramesInFlight);
    for (size_t i = 0; i < kMaxFramesInFlight; ++i)
    {
        auto& t = shadeTargets_[i];
        t.renderPass = createShadeRenderPass(ctx.device, lighting.lit(static_cast<uint32_t>(i)).format,
                                             gbuffer.depth(static_cast<uint32_t>(i)).format);

        VkImageView attachments[] = {lighting.lit(static_cast<uint32_t>(i)).view,
                                     gbuffer.depth(static_cast<uint32_t>(i)).view};
        VkFramebufferCreateInfo fbci{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
        fbci.renderPass = t.renderPass;
        fbci.attachmentCount = 2;
        fbci.pAttachments = attachments;
        fbci.width = extent_.width;
        fbci.height = extent_.height;
        fbci.layers = 1;
        if (vkCreateFramebuffer(ctx.device, &fbci, nullptr, &t.framebuffer) != VK_SUCCESS)
        {
            failGlassPass("vkCreateFramebuffer failed for glass shading targets.");
        }
    }
}

void GlassPass::destroyShadeTargets(VulkanContext& ctx)
{
    for (auto& t : shadeTargets_)
    {
        if (t.framebuffer != VK_NULL_HANDLE)
        {
            vkDestroyFramebuffer(ctx.device, t.framebuffer, nullptr);
        }
        if (t.renderPass != VK_NULL_HANDLE)
        {
            vkDestroyRenderPass(ctx.device, t.renderPass, nullptr);
        }
        t = ShadeTarget{};
    }
    shadeTargets_.clear();
}

void GlassPass::updateBackUbo(uint32_t frameIndex, const FrameUbo& frameUbo)
{
    if (frameIndex >= backUbos_.size())
    {
        return;
    }
    if (backUbos_[frameIndex].mapped == nullptr)
    {
        return;
    }

    std::memcpy(backUbos_[frameIndex].mapped, &frameUbo, sizeof(frameUbo));
}

void GlassPass::updateShadeUbo(uint32_t frameIndex, const FrameUbo& frameUbo)
{
    if (frameIndex >= shadeUbos_.size())
    {
        return;
    }
    if (shadeUbos_[frameIndex].mapped == nullptr)
    {
        return;
    }

    std::memcpy(shadeUbos_[frameIndex].mapped, &frameUbo, sizeof(frameUbo));
}

void GlassPass::updateDescriptorSets(VulkanContext& ctx, const GBufferPass& gbuffer,
                                     const WaterPass& water)
{
    const size_t count = std::min(texSets_.size(), static_cast<size_t>(kMaxFramesInFlight));
    for (size_t i = 0; i < count; i++)
    {
        VkDescriptorImageInfo infos[3]{};
        infos[0].sampler = water.target(static_cast<uint32_t>(i)).sampler;
        infos[0].imageView = water.target(static_cast<uint32_t>(i)).view;
        infos[0].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        infos[1].sampler = backDepthTargets_[i].colorSampler;
        infos[1].imageView = backDepthTargets_[i].colorView;
        infos[1].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        infos[2].sampler = gbuffer.depth(static_cast<uint32_t>(i)).sampler;
        infos[2].imageView = gbuffer.depth(static_cast<uint32_t>(i)).view;
        infos[2].imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;

        VkWriteDescriptorSet writes[3]{};
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

        vkUpdateDescriptorSets(ctx.device, 3, writes, 0, nullptr);
    }
}

void GlassPass::updateWaterDescriptor(VulkanContext& ctx)
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
