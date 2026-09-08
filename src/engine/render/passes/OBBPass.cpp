#include "engine/render/passes/OBBPass.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <span>
#include <vector>

#include <glm/glm.hpp>

#include "Resources/ShaderModule.h"
#include "engine/render/RendererConfig.h"
#include "engine/render/Utils.h"
#include "engine/render/VulkanContext.h"

namespace engine
{
namespace
{
struct PushConstants
{
    uint32_t volumeIndex = 0;
    uint32_t debugMode = 0;
    uint32_t skipEnabled = 0;
    uint32_t skipMip = 0;
    uint32_t heatmapMode = 0; // 0 = iterations, 1 = skip jumps
    float heatmapMax = 128.0f;
    float heatmapGamma = 1.0f;
    uint32_t metricsEnabled = 0;      // day 8.5: enable metrics recording
    uint32_t metricsSampleStride = 4; // sample every nth pixel
    uint32_t metricsSampleOffsetX = 0;
    uint32_t metricsSampleOffsetY = 0;
    float normalEdgeSmoothing = 0.15f; // day 12D: edge smoothing (0 = off, 0.15 = recommended)
    float pixelEdgeShadowStrength = 0.35f;
    uint32_t waterVolumeCount = 0; // day 18: active water volumes
    float reflectionClipY = 0.0f;
    uint32_t reflectionClipEnabled = 0;
    float timeSeconds = 0.0f;
    float cellVariationStrength = 0.0f;
    float cellVariationGeneric = 0.0f;
    float cellVariationGravel = 0.0f;
    float cellVariationPlant = 0.0f;
    float cellVariationStone = 0.0f;
    float cellVariationWood = 0.0f;
    float cellVariationHueSpread = 0.018f;
    float cellVariationSaturationSpread = 0.12f;
    float cellVariationValueSpread = 0.10f;
    float cellVariationPaletteFamilyStrength = 0.0f;
    float cavityStrength = 1.0f;
    float paintedMaterialStrength = 0.0f;
    uint32_t secondaryVolumeIndex = 0;
};

static_assert(sizeof(PushConstants) == 120,
              "OBBPass push constants must match shaders/voxel/obb_dda.frag");

// vertex with position and normal for proper flat shading
struct CubeVertex
{
    glm::vec3 pos;
    glm::vec3 normal;
};

// 24 vertices: 4 per face, each with the face's normal
// positions are in [0,1] range, normals point outward (will be flipped for
// back-face rendering)
static const CubeVertex kCubeVerts[24] = {
    // -Z face (z=0)
    {{0, 0, 0}, {0, 0, -1}},
    {{1, 0, 0}, {0, 0, -1}},
    {{1, 1, 0}, {0, 0, -1}},
    {{0, 1, 0}, {0, 0, -1}},
    // +z face (z=1)
    {{0, 0, 1}, {0, 0, 1}},
    {{1, 0, 1}, {0, 0, 1}},
    {{1, 1, 1}, {0, 0, 1}},
    {{0, 1, 1}, {0, 0, 1}},
    // -X face (x=0)
    {{0, 0, 0}, {-1, 0, 0}},
    {{0, 0, 1}, {-1, 0, 0}},
    {{0, 1, 1}, {-1, 0, 0}},
    {{0, 1, 0}, {-1, 0, 0}},
    // +x face (x=1)
    {{1, 0, 0}, {1, 0, 0}},
    {{1, 1, 0}, {1, 0, 0}},
    {{1, 1, 1}, {1, 0, 0}},
    {{1, 0, 1}, {1, 0, 0}},
    // -Y face (y=0)
    {{0, 0, 0}, {0, -1, 0}},
    {{1, 0, 0}, {0, -1, 0}},
    {{1, 0, 1}, {0, -1, 0}},
    {{0, 0, 1}, {0, -1, 0}},
    // +y face (y=1)
    {{0, 1, 0}, {0, 1, 0}},
    {{0, 1, 1}, {0, 1, 0}},
    {{1, 1, 1}, {0, 1, 0}},
    {{1, 1, 0}, {0, 1, 0}},
};

// 36 indices, 2 triangles per face, ccw winding when viewed from outside
static const uint16_t kCubeIdx[36] = {
    // -Z face
    0,
    2,
    1,
    0,
    3,
    2,
    // +z face
    4,
    5,
    6,
    4,
    6,
    7,
    // -X face
    8,
    9,
    10,
    8,
    10,
    11,
    // +x face
    12,
    13,
    14,
    12,
    14,
    15,
    // -Y face
    16,
    17,
    18,
    16,
    18,
    19,
    // +y face
    20,
    21,
    22,
    20,
    22,
    23,
};

bool createBuffer(::VulkanContext& ctx, VkDeviceSize size, VkBufferUsageFlags usage,
                  VkMemoryPropertyFlags requiredProperties, render::UniqueBuffer& buffer,
                  const char* allocationName, VmaAllocationCreateFlags allocationFlags = 0)
{
    VkBufferCreateInfo ci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    ci.size = size;
    ci.usage = usage;
    ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocationInfo{};
    allocationInfo.flags = allocationFlags;
    allocationInfo.usage = (requiredProperties & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) != 0
                               ? VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE
                               : VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
    allocationInfo.requiredFlags = requiredProperties;
    return render::createUniqueBuffer(ctx.memoryAllocator, ci, allocationInfo, buffer,
                                      allocationName) == VK_SUCCESS;
}

bool uploadBuffer(::VulkanContext& ctx, VkBuffer dst, VkDeviceSize size, const void* data,
                  VkAccessFlags dstAccess, VkPipelineStageFlags dstStage)
{
    render::UniqueBuffer staging{};
    if (!createBuffer(ctx, size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                      staging, "obb_upload_staging",
                      VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT))
    {
        return false;
    }

    void* mapped = nullptr;
    if (vmaMapMemory(ctx.memoryAllocator, staging.allocation(), &mapped) != VK_SUCCESS)
    {
        return false;
    }
    std::memcpy(mapped, data, static_cast<size_t>(size));
    vmaFlushAllocation(ctx.memoryAllocator, staging.allocation(), 0, size);
    vmaUnmapMemory(ctx.memoryAllocator, staging.allocation());

    VkCommandBuffer cmd = ctx.beginSingleTimeCommands();

    VkBufferCopy copy{};
    copy.size = size;
    vkCmdCopyBuffer(cmd, staging.get(), dst, 1, &copy);

    VkBufferMemoryBarrier barrier{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = dstAccess;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.buffer = dst;
    barrier.offset = 0;
    barrier.size = size;

    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, dstStage, 0, 0, nullptr, 1, &barrier,
                         0, nullptr);

    ctx.endSingleTimeCommands(cmd);

    return true;
}

VkPipeline createPipeline(::VulkanContext& ctx, VkRenderPass renderPass,
                          VkPipelineLayout pipelineLayout, const std::string& vertPath,
                          const std::string& fragPath, VkCullModeFlags cullMode,
                          uint32_t colorAttachmentCount)
{
    const std::vector<char> vertCode = readFile(vertPath.c_str());
    const std::vector<char> fragCode = readFile(fragPath.c_str());

    VkShaderModule vertModule = createShaderModule(ctx.device, vertCode);
    VkShaderModule fragModule = createShaderModule(ctx.device, fragCode);

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
    binding.stride = sizeof(CubeVertex);
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    std::array<VkVertexInputAttributeDescription, 2> attrs{};
    // position at location 0
    attrs[0].binding = 0;
    attrs[0].location = 0;
    attrs[0].format = VK_FORMAT_R32G32B32_SFLOAT;
    attrs[0].offset = offsetof(CubeVertex, pos);
    // normal at location 1
    attrs[1].binding = 0;
    attrs[1].location = 1;
    attrs[1].format = VK_FORMAT_R32G32B32_SFLOAT;
    attrs[1].offset = offsetof(CubeVertex, normal);

    VkPipelineVertexInputStateCreateInfo vertexInput{
        VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    vertexInput.vertexBindingDescriptionCount = 1;
    vertexInput.pVertexBindingDescriptions = &binding;
    vertexInput.vertexAttributeDescriptionCount = static_cast<uint32_t>(attrs.size());
    vertexInput.pVertexAttributeDescriptions = attrs.data();

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
    raster.cullMode = cullMode;
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

    std::vector<VkPipelineColorBlendAttachmentState> blendAttachments(colorAttachmentCount);
    for (auto& attachment : blendAttachments)
    {
        attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                    VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        attachment.blendEnable = VK_FALSE;
    }

    VkPipelineColorBlendStateCreateInfo blend{
        VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    blend.logicOpEnable = VK_FALSE;
    blend.attachmentCount = colorAttachmentCount;
    blend.pAttachments = blendAttachments.data();

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
        vkDestroyShaderModule(ctx.device, fragModule, nullptr);
        vkDestroyShaderModule(ctx.device, vertModule, nullptr);
        return VK_NULL_HANDLE;
    }

    vkDestroyShaderModule(ctx.device, fragModule, nullptr);
    vkDestroyShaderModule(ctx.device, vertModule, nullptr);

    return pipeline;
}
} // namespace

void OBBPass::setShaderPaths(const std::string& vertPath,
                             const std::string& fullScreenVertPath,
                             const std::string& sharedAlignedVertPath,
                             const std::string& ddaHotFragPath,
                             const std::string& ddaOpaqueFragPath,
                             const std::string& ddaUnwrappedOpaqueFragPath,
                             const std::string& ddaNearClipSafeOpaqueFragPath,
                             const std::string& ddaSharedAlignedOpaqueFragPath,
                             const std::string& ddaSharedAlignedNearClipFragPath,
                             const std::string& ddaDiagnosticFragPath)
{
    m_vertPath = vertPath;
    m_fullScreenVertPath = fullScreenVertPath;
    m_sharedAlignedVertPath = sharedAlignedVertPath;
    m_ddaHotFragPath = ddaHotFragPath;
    m_ddaOpaqueFragPath = ddaOpaqueFragPath;
    m_ddaUnwrappedOpaqueFragPath = ddaUnwrappedOpaqueFragPath;
    m_ddaNearClipSafeOpaqueFragPath = ddaNearClipSafeOpaqueFragPath;
    m_ddaSharedAlignedOpaqueFragPath = ddaSharedAlignedOpaqueFragPath;
    m_ddaSharedAlignedNearClipFragPath = ddaSharedAlignedNearClipFragPath;
    m_ddaDiagnosticFragPath = ddaDiagnosticFragPath;
}

void OBBPass::setDebugMode(uint32_t mode)
{
    m_debugMode = mode;
}

void OBBPass::setSkipSettings(bool enabled, uint32_t mipLevel)
{
    m_skipEnabled = enabled ? 1u : 0u;
    m_skipMip = mipLevel;
}

void OBBPass::setHeatmapSettings(uint32_t mode, float maxValue, float gamma)
{
    m_heatmapMode = mode;
    m_heatmapMax = maxValue;
    m_heatmapGamma = gamma;
}

void OBBPass::setMetricsSettings(bool enabled, uint32_t sampleStride, uint32_t sampleOffsetX,
                                 uint32_t sampleOffsetY)
{
    m_metricsEnabled = enabled;
    m_metricsSampleStride = sampleStride;
    m_metricsSampleOffsetX = sampleOffsetX;
    m_metricsSampleOffsetY = sampleOffsetY;
}

void OBBPass::setNormalEdgeSmoothing(float smoothing)
{
    m_normalEdgeSmoothing = smoothing;
}

void OBBPass::setPixelEdgeShadowStrength(float strength)
{
    m_pixelEdgeShadowStrength = std::clamp(strength, 0.0f, 1.0f);
}

void OBBPass::setPaintedSurfaceSettings(float cavityStrength,
                                        float paintedMaterialStrength)
{
    m_cavityStrength = std::clamp(cavityStrength, 0.0f, 1.0f);
    m_paintedMaterialStrength = std::clamp(paintedMaterialStrength, 0.0f, 1.0f);
}

void OBBPass::setCellVariationSettings(const VoxelCellVariationSettings& settings)
{
    m_cellVariation.masterStrength = std::clamp(settings.masterStrength, 0.0f, 1.0f);
    m_cellVariation.genericAmplitude =
        std::clamp(settings.genericAmplitude, 0.0f, 1.0f);
    m_cellVariation.gravelAmplitude =
        std::clamp(settings.gravelAmplitude, 0.0f, 1.0f);
    m_cellVariation.plantAmplitude =
        std::clamp(settings.plantAmplitude, 0.0f, 1.0f);
    m_cellVariation.stoneAmplitude =
        std::clamp(settings.stoneAmplitude, 0.0f, 1.0f);
    m_cellVariation.woodAmplitude =
        std::clamp(settings.woodAmplitude, 0.0f, 1.0f);
    m_cellVariation.hueSpread = std::clamp(settings.hueSpread, 0.0f, 0.25f);
    m_cellVariation.saturationSpread =
        std::clamp(settings.saturationSpread, 0.0f, 1.0f);
    m_cellVariation.valueSpread = std::clamp(settings.valueSpread, 0.0f, 1.0f);
    m_cellVariation.paletteFamilyStrength =
        std::clamp(settings.paletteFamilyStrength, 0.0f, 1.0f);
}

void OBBPass::setWaterVolumeCount(uint32_t count)
{
    m_waterVolumeCount = count;
}

void OBBPass::setReflectionClip(float clipY, bool enabled)
{
    m_reflectionClipY = clipY;
    m_reflectionClipEnabled = enabled ? 1u : 0u;
}

void OBBPass::setTime(float timeSeconds)
{
    m_timeSeconds = timeSeconds;
}

void OBBPass::clearMetrics(VkCommandBuffer cmd)
{
    if (!m_metricsBuffer)
    {
        return;
    }
    vkCmdFillBuffer(cmd, m_metricsBuffer.get(), 0, sizeof(DDAMetrics), 0);

    // barrier to ensure fill completes before shader reads
    VkBufferMemoryBarrier barrier{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.buffer = m_metricsBuffer.get();
    barrier.offset = 0;
    barrier.size = sizeof(DDAMetrics);

    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         0, 0, nullptr, 1, &barrier, 0, nullptr);
}

void OBBPass::recordMetricsCopy(VkCommandBuffer cmd, uint32_t frameIndex)
{
    if (!m_metricsEnabled || !m_metricsBuffer)
    {
        return;
    }
    if (frameIndex >= m_metricsReadbackBuffers.size())
    {
        return;
    }

    VkBufferMemoryBarrier barrier{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.buffer = m_metricsBuffer.get();
    barrier.offset = 0;
    barrier.size = sizeof(DDAMetrics);

    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         0, 0, nullptr, 1, &barrier, 0, nullptr);

    VkBufferCopy copy{};
    copy.size = sizeof(DDAMetrics);
    vkCmdCopyBuffer(cmd, m_metricsBuffer.get(), m_metricsReadbackBuffers[frameIndex].get(), 1,
                    &copy);
}

DDAMetrics OBBPass::readMetrics(::VulkanContext& ctx, uint32_t frameIndex)
{
    DDAMetrics result{};
    if (frameIndex >= m_metricsReadbackBuffers.size())
    {
        return result;
    }

    void* mapped = nullptr;
    const auto& readback = m_metricsReadbackBuffers[frameIndex];
    if (vmaMapMemory(ctx.memoryAllocator, readback.allocation(), &mapped) == VK_SUCCESS)
    {
        vmaInvalidateAllocation(ctx.memoryAllocator, readback.allocation(), 0, sizeof(DDAMetrics));
        std::memcpy(&result, mapped, sizeof(DDAMetrics));
        vmaUnmapMemory(ctx.memoryAllocator, readback.allocation());
    }
    return result;
}

bool OBBPass::create(::VulkanContext& ctx, VkRenderPass gbufferRenderPass, VkExtent2D extent,
                     VkDescriptorSetLayout frameSetLayout, VkDescriptorSetLayout voxelSetLayout,
                     uint32_t gbufferColorAttachmentCount, VkBuffer waterBuffer)
{
    if (gbufferRenderPass == VK_NULL_HANDLE || frameSetLayout == VK_NULL_HANDLE ||
        voxelSetLayout == VK_NULL_HANDLE || gbufferColorAttachmentCount == 0 ||
        waterBuffer == VK_NULL_HANDLE)
    {
        return false;
    }

    VkPhysicalDeviceProperties deviceProperties{};
    vkGetPhysicalDeviceProperties(ctx.gpu, &deviceProperties);
    constexpr uint32_t kSharedAlignedDescriptorSetCount = 5u;
    const bool sharedAlignedLayoutSupported =
        deviceProperties.limits.maxBoundDescriptorSets >=
        kSharedAlignedDescriptorSetCount;

    m_extent = extent;
    m_indexCount = static_cast<uint32_t>(sizeof(kCubeIdx) / sizeof(kCubeIdx[0]));

    const VkDeviceSize vbSize = sizeof(kCubeVerts);
    const VkDeviceSize ibSize = sizeof(kCubeIdx);

    if (!createBuffer(ctx, vbSize,
                      VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, m_vb, "obb_vertices"))
    {
        return false;
    }

    if (!createBuffer(ctx, ibSize,
                      VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, m_ib, "obb_indices"))
    {
        destroy(ctx);
        return false;
    }

    if (!uploadBuffer(ctx, m_vb.get(), vbSize, kCubeVerts, VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT,
                      VK_PIPELINE_STAGE_VERTEX_INPUT_BIT))
    {
        destroy(ctx);
        return false;
    }

    if (!uploadBuffer(ctx, m_ib.get(), ibSize, kCubeIdx, VK_ACCESS_INDEX_READ_BIT,
                      VK_PIPELINE_STAGE_VERTEX_INPUT_BIT))
    {
        destroy(ctx);
        return false;
    }

    // day 8.5: create metrics ssbo for dda statistics (device local)
    if (!createBuffer(ctx, sizeof(DDAMetrics),
                      VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                          VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, m_metricsBuffer, "obb_dda_metrics"))
    {
        destroy(ctx);
        return false;
    }

    m_metricsReadbackBuffers.resize(kMaxFramesInFlight);
    constexpr const char* kReadbackNames[] = {"obb_dda_metrics_readback_0",
                                              "obb_dda_metrics_readback_1"};
    for (size_t i = 0; i < m_metricsReadbackBuffers.size(); ++i)
    {
        if (!createBuffer(ctx, sizeof(DDAMetrics), VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                              VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                          m_metricsReadbackBuffers[i], kReadbackNames[i],
                          VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT))
        {
            destroy(ctx);
            return false;
        }
    }

    // create metrics descriptor set layout (ssbo binding)
    VkDescriptorSetLayoutBinding metricsBinding{};
    metricsBinding.binding = 0;
    metricsBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    metricsBinding.descriptorCount = 1;
    metricsBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo metricsLayoutInfo{
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    metricsLayoutInfo.bindingCount = 1;
    metricsLayoutInfo.pBindings = &metricsBinding;
    if (!ctx.descriptorLayoutCache.get(ctx.device, metricsLayoutInfo, m_metricsSetLayout))
    {
        destroy(ctx);
        return false;
    }

    // allocate metrics descriptor set.
    VkDescriptorPoolSize metricsPoolSize{};
    metricsPoolSize.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    metricsPoolSize.descriptorCount = 1;

    VkDescriptorSetLayout metricsSetLayoutRaw = m_metricsSetLayout;
    const std::span<const VkDescriptorPoolSize> metricsPoolSizes(&metricsPoolSize, 1);
    if (!ctx.descriptorAllocator.allocate(ctx.device, metricsPoolSizes, 1, &metricsSetLayoutRaw, 1,
                                          &m_metricsDescSet))
    {
        destroy(ctx);
        return false;
    }

    // update metrics descriptor set
    VkDescriptorBufferInfo metricsBufferInfo{};
    metricsBufferInfo.buffer = m_metricsBuffer.get();
    metricsBufferInfo.offset = 0;
    metricsBufferInfo.range = sizeof(DDAMetrics);

    VkWriteDescriptorSet metricsWrite{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    metricsWrite.dstSet = m_metricsDescSet;
    metricsWrite.dstBinding = 0;
    metricsWrite.descriptorCount = 1;
    metricsWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    metricsWrite.pBufferInfo = &metricsBufferInfo;
    vkUpdateDescriptorSets(ctx.device, 1, &metricsWrite, 0, nullptr);

    // create water descriptor set layout (ssbo binding)
    VkDescriptorSetLayoutBinding waterBinding{};
    waterBinding.binding = 0;
    waterBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    waterBinding.descriptorCount = 1;
    waterBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo waterLayoutInfo{
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    waterLayoutInfo.bindingCount = 1;
    waterLayoutInfo.pBindings = &waterBinding;
    if (!ctx.descriptorLayoutCache.get(ctx.device, waterLayoutInfo, m_waterSetLayout))
    {
        destroy(ctx);
        return false;
    }

    VkDescriptorPoolSize waterPoolSize{};
    waterPoolSize.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    waterPoolSize.descriptorCount = 1;

    VkDescriptorSetLayout waterSetLayoutRaw = m_waterSetLayout;
    const std::span<const VkDescriptorPoolSize> waterPoolSizes(&waterPoolSize, 1);
    if (!ctx.descriptorAllocator.allocate(ctx.device, waterPoolSizes, 1, &waterSetLayoutRaw, 1,
                                          &m_waterDescSet))
    {
        destroy(ctx);
        return false;
    }

    VkDescriptorBufferInfo waterBufferInfo{};
    waterBufferInfo.buffer = waterBuffer;
    waterBufferInfo.offset = 0;
    waterBufferInfo.range = VK_WHOLE_SIZE;

    VkWriteDescriptorSet waterWrite{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    waterWrite.dstSet = m_waterDescSet;
    waterWrite.dstBinding = 0;
    waterWrite.descriptorCount = 1;
    waterWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    waterWrite.pBufferInfo = &waterBufferInfo;
    vkUpdateDescriptorSets(ctx.device, 1, &waterWrite, 0, nullptr);

    VkPushConstantRange pcr{};
    pcr.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pcr.offset = 0;
    pcr.size = sizeof(PushConstants);

    VkDescriptorSetLayout setLayouts[] = {frameSetLayout, voxelSetLayout, m_metricsSetLayout,
                                          m_waterSetLayout};
    VkPipelineLayoutCreateInfo plci{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    plci.setLayoutCount = static_cast<uint32_t>(sizeof(setLayouts) / sizeof(setLayouts[0]));
    plci.pSetLayouts = setLayouts;
    plci.pushConstantRangeCount = 1;
    plci.pPushConstantRanges = &pcr;

    VkPipelineLayout layout = VK_NULL_HANDLE;
    if (vkCreatePipelineLayout(ctx.device, &plci, nullptr, &layout) != VK_SUCCESS)
    {
        destroy(ctx);
        return false;
    }
    m_layout = render::UniquePipelineLayout(ctx.device, layout);

    if (sharedAlignedLayoutSupported)
    {
        VkDescriptorSetLayout sharedSetLayouts[] = {
            frameSetLayout, voxelSetLayout, m_metricsSetLayout,
            m_waterSetLayout, voxelSetLayout};
        plci.setLayoutCount = static_cast<uint32_t>(
            sizeof(sharedSetLayouts) / sizeof(sharedSetLayouts[0]));
        plci.pSetLayouts = sharedSetLayouts;
        VkPipelineLayout sharedLayout = VK_NULL_HANDLE;
        if (vkCreatePipelineLayout(ctx.device, &plci, nullptr, &sharedLayout) !=
            VK_SUCCESS)
        {
            destroy(ctx);
            return false;
        }
        m_sharedAlignedLayout =
            render::UniquePipelineLayout(ctx.device, sharedLayout);
    }

    VkPipeline ddaHotPipeline =
        createPipeline(ctx, gbufferRenderPass, m_layout.get(), m_vertPath, m_ddaHotFragPath,
                       VK_CULL_MODE_NONE, gbufferColorAttachmentCount);
    if (ddaHotPipeline == VK_NULL_HANDLE)
    {
        destroy(ctx);
        return false;
    }
    m_ddaHotPipeline = render::UniquePipeline(ctx.device, ddaHotPipeline);
    VkPipeline ddaHotFrontCullPipeline =
        createPipeline(ctx, gbufferRenderPass, m_layout.get(), m_vertPath, m_ddaHotFragPath,
                       VK_CULL_MODE_FRONT_BIT, gbufferColorAttachmentCount);
    VkPipeline ddaHotBackCullPipeline =
        createPipeline(ctx, gbufferRenderPass, m_layout.get(), m_vertPath, m_ddaHotFragPath,
                       VK_CULL_MODE_BACK_BIT, gbufferColorAttachmentCount);
    if (ddaHotFrontCullPipeline == VK_NULL_HANDLE ||
        ddaHotBackCullPipeline == VK_NULL_HANDLE)
    {
        if (ddaHotFrontCullPipeline != VK_NULL_HANDLE)
        {
            vkDestroyPipeline(ctx.device, ddaHotFrontCullPipeline, nullptr);
        }
        if (ddaHotBackCullPipeline != VK_NULL_HANDLE)
        {
            vkDestroyPipeline(ctx.device, ddaHotBackCullPipeline, nullptr);
        }
        destroy(ctx);
        return false;
    }
    m_ddaHotFrontCullPipeline =
        render::UniquePipeline(ctx.device, ddaHotFrontCullPipeline);
    m_ddaHotBackCullPipeline =
        render::UniquePipeline(ctx.device, ddaHotBackCullPipeline);

    VkPipeline ddaOpaquePipeline =
        createPipeline(ctx, gbufferRenderPass, m_layout.get(), m_vertPath,
                       m_ddaOpaqueFragPath, VK_CULL_MODE_NONE,
                       gbufferColorAttachmentCount);
    if (ddaOpaquePipeline == VK_NULL_HANDLE)
    {
        destroy(ctx);
        return false;
    }
    m_ddaOpaquePipeline = render::UniquePipeline(ctx.device, ddaOpaquePipeline);
    VkPipeline ddaOpaqueFrontCullPipeline =
        createPipeline(ctx, gbufferRenderPass, m_layout.get(), m_vertPath,
                       m_ddaOpaqueFragPath, VK_CULL_MODE_FRONT_BIT,
                       gbufferColorAttachmentCount);
    VkPipeline ddaOpaqueBackCullPipeline =
        createPipeline(ctx, gbufferRenderPass, m_layout.get(), m_vertPath,
                       m_ddaOpaqueFragPath, VK_CULL_MODE_BACK_BIT,
                       gbufferColorAttachmentCount);
    if (ddaOpaqueFrontCullPipeline == VK_NULL_HANDLE ||
        ddaOpaqueBackCullPipeline == VK_NULL_HANDLE)
    {
        if (ddaOpaqueFrontCullPipeline != VK_NULL_HANDLE)
        {
            vkDestroyPipeline(ctx.device, ddaOpaqueFrontCullPipeline, nullptr);
        }
        if (ddaOpaqueBackCullPipeline != VK_NULL_HANDLE)
        {
            vkDestroyPipeline(ctx.device, ddaOpaqueBackCullPipeline, nullptr);
        }
        destroy(ctx);
        return false;
    }
    m_ddaOpaqueFrontCullPipeline =
        render::UniquePipeline(ctx.device, ddaOpaqueFrontCullPipeline);
    m_ddaOpaqueBackCullPipeline =
        render::UniquePipeline(ctx.device, ddaOpaqueBackCullPipeline);

    VkPipeline ddaUnwrappedOpaquePipeline =
        createPipeline(ctx, gbufferRenderPass, m_layout.get(), m_vertPath,
                       m_ddaUnwrappedOpaqueFragPath, VK_CULL_MODE_NONE,
                       gbufferColorAttachmentCount);
    if (ddaUnwrappedOpaquePipeline == VK_NULL_HANDLE)
    {
        destroy(ctx);
        return false;
    }
    m_ddaUnwrappedOpaquePipeline =
        render::UniquePipeline(ctx.device, ddaUnwrappedOpaquePipeline);
    VkPipeline ddaUnwrappedOpaqueFrontCullPipeline =
        createPipeline(ctx, gbufferRenderPass, m_layout.get(), m_vertPath,
                       m_ddaUnwrappedOpaqueFragPath, VK_CULL_MODE_FRONT_BIT,
                       gbufferColorAttachmentCount);
    VkPipeline ddaUnwrappedOpaqueBackCullPipeline =
        createPipeline(ctx, gbufferRenderPass, m_layout.get(), m_vertPath,
                       m_ddaUnwrappedOpaqueFragPath, VK_CULL_MODE_BACK_BIT,
                       gbufferColorAttachmentCount);
    if (ddaUnwrappedOpaqueFrontCullPipeline == VK_NULL_HANDLE ||
        ddaUnwrappedOpaqueBackCullPipeline == VK_NULL_HANDLE)
    {
        if (ddaUnwrappedOpaqueFrontCullPipeline != VK_NULL_HANDLE)
        {
            vkDestroyPipeline(ctx.device, ddaUnwrappedOpaqueFrontCullPipeline, nullptr);
        }
        if (ddaUnwrappedOpaqueBackCullPipeline != VK_NULL_HANDLE)
        {
            vkDestroyPipeline(ctx.device, ddaUnwrappedOpaqueBackCullPipeline, nullptr);
        }
        destroy(ctx);
        return false;
    }
    m_ddaUnwrappedOpaqueFrontCullPipeline =
        render::UniquePipeline(ctx.device, ddaUnwrappedOpaqueFrontCullPipeline);
    m_ddaUnwrappedOpaqueBackCullPipeline =
        render::UniquePipeline(ctx.device, ddaUnwrappedOpaqueBackCullPipeline);

    VkPipeline ddaNearClipSafeOpaqueFullScreenPipeline =
        createPipeline(ctx, gbufferRenderPass, m_layout.get(), m_fullScreenVertPath,
                       m_ddaNearClipSafeOpaqueFragPath, VK_CULL_MODE_NONE,
                       gbufferColorAttachmentCount);
    if (ddaNearClipSafeOpaqueFullScreenPipeline == VK_NULL_HANDLE)
    {
        destroy(ctx);
        return false;
    }
    m_ddaNearClipSafeOpaqueFullScreenPipeline =
        render::UniquePipeline(ctx.device, ddaNearClipSafeOpaqueFullScreenPipeline);

    if (sharedAlignedLayoutSupported)
    {
        VkPipeline ddaSharedAlignedPipeline = createPipeline(
            ctx, gbufferRenderPass, m_sharedAlignedLayout.get(),
            m_sharedAlignedVertPath, m_ddaSharedAlignedOpaqueFragPath,
            VK_CULL_MODE_NONE, gbufferColorAttachmentCount);
        VkPipeline ddaSharedAlignedFrontCullPipeline = createPipeline(
            ctx, gbufferRenderPass, m_sharedAlignedLayout.get(),
            m_sharedAlignedVertPath, m_ddaSharedAlignedOpaqueFragPath,
            VK_CULL_MODE_FRONT_BIT, gbufferColorAttachmentCount);
        VkPipeline ddaSharedAlignedBackCullPipeline = createPipeline(
            ctx, gbufferRenderPass, m_sharedAlignedLayout.get(),
            m_sharedAlignedVertPath, m_ddaSharedAlignedOpaqueFragPath,
            VK_CULL_MODE_BACK_BIT, gbufferColorAttachmentCount);
        VkPipeline ddaSharedAlignedFullScreenPipeline = createPipeline(
            ctx, gbufferRenderPass, m_sharedAlignedLayout.get(),
            m_fullScreenVertPath, m_ddaSharedAlignedNearClipFragPath,
            VK_CULL_MODE_NONE, gbufferColorAttachmentCount);
        if (ddaSharedAlignedPipeline == VK_NULL_HANDLE ||
            ddaSharedAlignedFrontCullPipeline == VK_NULL_HANDLE ||
            ddaSharedAlignedBackCullPipeline == VK_NULL_HANDLE ||
            ddaSharedAlignedFullScreenPipeline == VK_NULL_HANDLE)
        {
            if (ddaSharedAlignedPipeline != VK_NULL_HANDLE)
            {
                vkDestroyPipeline(ctx.device, ddaSharedAlignedPipeline,
                                  nullptr);
            }
            if (ddaSharedAlignedFrontCullPipeline != VK_NULL_HANDLE)
            {
                vkDestroyPipeline(ctx.device,
                                  ddaSharedAlignedFrontCullPipeline, nullptr);
            }
            if (ddaSharedAlignedBackCullPipeline != VK_NULL_HANDLE)
            {
                vkDestroyPipeline(ctx.device,
                                  ddaSharedAlignedBackCullPipeline, nullptr);
            }
            if (ddaSharedAlignedFullScreenPipeline != VK_NULL_HANDLE)
            {
                vkDestroyPipeline(ctx.device,
                                  ddaSharedAlignedFullScreenPipeline, nullptr);
            }
            destroy(ctx);
            return false;
        }
        m_ddaSharedAlignedPipeline =
            render::UniquePipeline(ctx.device, ddaSharedAlignedPipeline);
        m_ddaSharedAlignedFrontCullPipeline = render::UniquePipeline(
            ctx.device, ddaSharedAlignedFrontCullPipeline);
        m_ddaSharedAlignedBackCullPipeline = render::UniquePipeline(
            ctx.device, ddaSharedAlignedBackCullPipeline);
        m_ddaSharedAlignedFullScreenPipeline = render::UniquePipeline(
            ctx.device, ddaSharedAlignedFullScreenPipeline);
    }

    VkPipeline ddaDiagnosticPipeline =
        createPipeline(ctx, gbufferRenderPass, m_layout.get(), m_vertPath,
                       m_ddaDiagnosticFragPath, VK_CULL_MODE_NONE,
                       gbufferColorAttachmentCount);
    if (ddaDiagnosticPipeline == VK_NULL_HANDLE)
    {
        destroy(ctx);
        return false;
    }
    m_ddaDiagnosticPipeline = render::UniquePipeline(ctx.device, ddaDiagnosticPipeline);
    VkPipeline ddaDiagnosticFullScreenPipeline =
        createPipeline(ctx, gbufferRenderPass, m_layout.get(), m_fullScreenVertPath,
                       m_ddaDiagnosticFragPath, VK_CULL_MODE_NONE,
                       gbufferColorAttachmentCount);
    if (ddaDiagnosticFullScreenPipeline == VK_NULL_HANDLE)
    {
        destroy(ctx);
        return false;
    }
    m_ddaDiagnosticFullScreenPipeline =
        render::UniquePipeline(ctx.device, ddaDiagnosticFullScreenPipeline);
    VkPipeline ddaDiagnosticFrontCullPipeline =
        createPipeline(ctx, gbufferRenderPass, m_layout.get(), m_vertPath,
                       m_ddaDiagnosticFragPath, VK_CULL_MODE_FRONT_BIT,
                       gbufferColorAttachmentCount);
    VkPipeline ddaDiagnosticBackCullPipeline =
        createPipeline(ctx, gbufferRenderPass, m_layout.get(), m_vertPath,
                       m_ddaDiagnosticFragPath, VK_CULL_MODE_BACK_BIT,
                       gbufferColorAttachmentCount);
    if (ddaDiagnosticFrontCullPipeline == VK_NULL_HANDLE ||
        ddaDiagnosticBackCullPipeline == VK_NULL_HANDLE)
    {
        if (ddaDiagnosticFrontCullPipeline != VK_NULL_HANDLE)
        {
            vkDestroyPipeline(ctx.device, ddaDiagnosticFrontCullPipeline, nullptr);
        }
        if (ddaDiagnosticBackCullPipeline != VK_NULL_HANDLE)
        {
            vkDestroyPipeline(ctx.device, ddaDiagnosticBackCullPipeline, nullptr);
        }
        destroy(ctx);
        return false;
    }
    m_ddaDiagnosticFrontCullPipeline =
        render::UniquePipeline(ctx.device, ddaDiagnosticFrontCullPipeline);
    m_ddaDiagnosticBackCullPipeline =
        render::UniquePipeline(ctx.device, ddaDiagnosticBackCullPipeline);

    return true;
}

void OBBPass::destroy(::VulkanContext& ctx)
{
    (void)ctx;
    m_ddaDiagnosticBackCullPipeline.reset();
    m_ddaDiagnosticFrontCullPipeline.reset();
    m_ddaDiagnosticFullScreenPipeline.reset();
    m_ddaDiagnosticPipeline.reset();
    m_ddaSharedAlignedFullScreenPipeline.reset();
    m_ddaSharedAlignedBackCullPipeline.reset();
    m_ddaSharedAlignedFrontCullPipeline.reset();
    m_ddaSharedAlignedPipeline.reset();
    m_ddaNearClipSafeOpaqueFullScreenPipeline.reset();
    m_ddaUnwrappedOpaqueBackCullPipeline.reset();
    m_ddaUnwrappedOpaqueFrontCullPipeline.reset();
    m_ddaUnwrappedOpaquePipeline.reset();
    m_ddaOpaqueBackCullPipeline.reset();
    m_ddaOpaqueFrontCullPipeline.reset();
    m_ddaOpaquePipeline.reset();
    m_ddaHotBackCullPipeline.reset();
    m_ddaHotFrontCullPipeline.reset();
    m_ddaHotPipeline.reset();
    m_sharedAlignedLayout.reset();
    m_layout.reset();

    // clean up metrics resources
    m_metricsDescSet = VK_NULL_HANDLE;
    m_metricsSetLayout = VK_NULL_HANDLE;
    m_waterDescSet = VK_NULL_HANDLE;
    m_waterSetLayout = VK_NULL_HANDLE;
    m_metricsReadbackBuffers.clear();
    m_metricsBuffer.reset();
    m_vb.reset();
    m_ib.reset();

    m_indexCount = 0;
    m_waterVolumeCount = 0;
}

void OBBPass::draw(VkCommandBuffer cmd, VkDescriptorSet frameSet, VkDescriptorSet voxelSet,
                   uint32_t volumeIndex, bool opaqueOnlyDda, bool unwrappedOpaqueDda,
                   bool fullScreenCoverageRequired,
                   bool cameraInsideRasterBounds, bool faceCullingSafe,
                   bool frontFaceWindingReversed)
{
    drawInternal(cmd, frameSet, voxelSet, volumeIndex, opaqueOnlyDda,
                 unwrappedOpaqueDda, fullScreenCoverageRequired,
                 cameraInsideRasterBounds, faceCullingSafe,
                 frontFaceWindingReversed, VK_NULL_HANDLE, 0u);
}

void OBBPass::drawSharedAligned(
    VkCommandBuffer cmd, VkDescriptorSet frameSet,
    VkDescriptorSet firstVoxelSet, VkDescriptorSet secondVoxelSet,
    uint32_t firstVolumeIndex, uint32_t secondVolumeIndex,
    bool fullScreenCoverageRequired, bool cameraInsideRasterBounds,
    bool faceCullingSafe, bool frontFaceWindingReversed)
{
    drawInternal(cmd, frameSet, firstVoxelSet, firstVolumeIndex, true, true,
                 fullScreenCoverageRequired, cameraInsideRasterBounds,
                 faceCullingSafe, frontFaceWindingReversed, secondVoxelSet,
                 secondVolumeIndex);
}

bool OBBPass::supportsSharedAlignedTraversal() const
{
    return m_debugMode == 0u && !m_metricsEnabled &&
           m_sharedAlignedLayout.get() != VK_NULL_HANDLE &&
           m_ddaSharedAlignedPipeline.get() != VK_NULL_HANDLE &&
           m_ddaSharedAlignedFullScreenPipeline.get() != VK_NULL_HANDLE;
}

void OBBPass::drawInternal(
    VkCommandBuffer cmd, VkDescriptorSet frameSet, VkDescriptorSet voxelSet,
    uint32_t volumeIndex, bool opaqueOnlyDda, bool unwrappedOpaqueDda,
    bool fullScreenCoverageRequired, bool cameraInsideRasterBounds,
    bool faceCullingSafe, bool frontFaceWindingReversed,
    VkDescriptorSet secondaryVoxelSet, uint32_t secondaryVolumeIndex)
{
    const bool sharedAligned = secondaryVoxelSet != VK_NULL_HANDLE;
    const bool normalRendering = m_debugMode == 0u && !m_metricsEnabled;
    const bool drawFullScreen =
        unwrappedOpaqueDda && fullScreenCoverageRequired;
    VkPipeline ddaPipeline = m_ddaDiagnosticPipeline.get();
    VkPipeline ddaFrontCullPipeline = m_ddaDiagnosticFrontCullPipeline.get();
    VkPipeline ddaBackCullPipeline = m_ddaDiagnosticBackCullPipeline.get();
    if (sharedAligned)
    {
        if (!normalRendering)
        {
            return;
        }
        ddaPipeline = drawFullScreen
                          ? m_ddaSharedAlignedFullScreenPipeline.get()
                          : m_ddaSharedAlignedPipeline.get();
        ddaFrontCullPipeline = m_ddaSharedAlignedFrontCullPipeline.get();
        ddaBackCullPipeline = m_ddaSharedAlignedBackCullPipeline.get();
    }
    else if (normalRendering)
    {
        if (drawFullScreen)
        {
            ddaPipeline = m_ddaNearClipSafeOpaqueFullScreenPipeline.get();
        }
        else if (unwrappedOpaqueDda)
        {
            ddaPipeline = m_ddaUnwrappedOpaquePipeline.get();
            ddaFrontCullPipeline = m_ddaUnwrappedOpaqueFrontCullPipeline.get();
            ddaBackCullPipeline = m_ddaUnwrappedOpaqueBackCullPipeline.get();
        }
        else if (opaqueOnlyDda)
        {
            ddaPipeline = m_ddaOpaquePipeline.get();
            ddaFrontCullPipeline = m_ddaOpaqueFrontCullPipeline.get();
            ddaBackCullPipeline = m_ddaOpaqueBackCullPipeline.get();
        }
        else
        {
            ddaPipeline = m_ddaHotPipeline.get();
            ddaFrontCullPipeline = m_ddaHotFrontCullPipeline.get();
            ddaBackCullPipeline = m_ddaHotBackCullPipeline.get();
        }
    }
    else if (drawFullScreen)
    {
        ddaPipeline = m_ddaDiagnosticFullScreenPipeline.get();
    }
    if (faceCullingSafe && !drawFullScreen)
    {
        const bool cullFrontFace =
            cameraInsideRasterBounds != frontFaceWindingReversed;
        ddaPipeline = cullFrontFace ? ddaFrontCullPipeline : ddaBackCullPipeline;
    }
    const VkPipelineLayout activeLayout =
        sharedAligned ? m_sharedAlignedLayout.get() : m_layout.get();
    if (ddaPipeline == VK_NULL_HANDLE || activeLayout == VK_NULL_HANDLE)
    {
        return;
    }
    if (!m_vb || !m_ib || m_indexCount == 0)
    {
        return;
    }
    if (voxelSet == VK_NULL_HANDLE)
    {
        return;
    }
    if (m_waterDescSet == VK_NULL_HANDLE || m_metricsDescSet == VK_NULL_HANDLE)
    {
        return;
    }

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, ddaPipeline);

    VkViewport vp{};
    vp.x = 0.0f;
    vp.y = 0.0f;
    vp.width = static_cast<float>(m_extent.width);
    vp.height = static_cast<float>(m_extent.height);
    vp.minDepth = 0.0f;
    vp.maxDepth = 1.0f;
    VkRect2D scissor{{0, 0}, m_extent};
    vkCmdSetViewport(cmd, 0, 1, &vp);
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    VkBuffer vertexBuffers[] = {m_vb.get()};
    VkDeviceSize offsets[] = {0};
    vkCmdBindVertexBuffers(cmd, 0, 1, vertexBuffers, offsets);
    vkCmdBindIndexBuffer(cmd, m_ib.get(), 0, VK_INDEX_TYPE_UINT16);

    VkDescriptorSet sets[] = {frameSet, voxelSet, m_metricsDescSet,
                              m_waterDescSet, secondaryVoxelSet};
    const uint32_t setCount = sharedAligned ? 5u : 4u;
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, activeLayout,
                            0, setCount, sets, 0, nullptr);

    PushConstants pc{};
    pc.volumeIndex = volumeIndex;
    pc.debugMode = m_debugMode;
    pc.skipEnabled = m_skipEnabled;
    pc.skipMip = m_skipMip;
    pc.heatmapMode = m_heatmapMode;
    pc.heatmapMax = m_heatmapMax;
    pc.heatmapGamma = m_heatmapGamma;
    pc.metricsEnabled = m_metricsEnabled ? 1u : 0u;
    pc.metricsSampleStride = m_metricsSampleStride;
    pc.metricsSampleOffsetX = m_metricsSampleOffsetX;
    pc.metricsSampleOffsetY = m_metricsSampleOffsetY;
    pc.normalEdgeSmoothing = m_normalEdgeSmoothing;
    pc.pixelEdgeShadowStrength = m_pixelEdgeShadowStrength;
    pc.waterVolumeCount = m_waterVolumeCount;
    pc.reflectionClipY = m_reflectionClipY;
    pc.reflectionClipEnabled = m_reflectionClipEnabled;
    pc.timeSeconds = m_timeSeconds;
    pc.cellVariationStrength = m_cellVariation.masterStrength;
    pc.cellVariationGeneric = m_cellVariation.genericAmplitude;
    pc.cellVariationGravel = m_cellVariation.gravelAmplitude;
    pc.cellVariationPlant = m_cellVariation.plantAmplitude;
    pc.cellVariationStone = m_cellVariation.stoneAmplitude;
    pc.cellVariationWood = m_cellVariation.woodAmplitude;
    pc.cellVariationHueSpread = m_cellVariation.hueSpread;
    pc.cellVariationSaturationSpread = m_cellVariation.saturationSpread;
    pc.cellVariationValueSpread = m_cellVariation.valueSpread;
    pc.cellVariationPaletteFamilyStrength =
        m_cellVariation.paletteFamilyStrength;
    pc.cavityStrength = m_cavityStrength;
    pc.paintedMaterialStrength = m_paintedMaterialStrength;
    pc.secondaryVolumeIndex = secondaryVolumeIndex;

    vkCmdPushConstants(cmd, activeLayout,
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc),
                       &pc);
    if (drawFullScreen)
    {
        vkCmdDraw(cmd, 3, 1, 0, 0);
    }
    else
    {
        vkCmdDrawIndexed(cmd, m_indexCount, 1, 0, 0, 0);
    }
}

} // namespace engine
