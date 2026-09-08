#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>
#include <string>
#include <vector>

#include "engine/render/gpu/UniqueAllocation.h"
#include "engine/render/gpu/UniqueHandle.h"
#include "engine/voxel/VoxelCellVariation.h"

struct VulkanContext;

namespace engine
{

// dda metrics data structure (gpu readable/writable)
struct DDAMetrics
{
    uint32_t sampleCount = 0;
    uint32_t sumIters = 0;
    uint32_t sumSkipJumps = 0;
    uint32_t hitCount = 0;
    uint32_t missCount = 0;
    uint32_t sumHierarchyDescents = 0;
    uint32_t sumFineIters = 0;
    uint32_t sumHierarchyProbes = 0;
    uint32_t hitSignatureXor = 0;
    uint32_t hitSignatureSum = 0;
    uint32_t surfaceSignatureXor = 0;
    uint32_t surfaceSignatureSum = 0;
};
static_assert(sizeof(DDAMetrics) == 48, "DDAMetrics must match the shader SSBO");

class OBBPass
{
  public:
    void setShaderPaths(const std::string& vertPath,
                        const std::string& fullScreenVertPath,
                        const std::string& sharedAlignedVertPath,
                        const std::string& ddaHotFragPath,
                        const std::string& ddaOpaqueFragPath,
                        const std::string& ddaUnwrappedOpaqueFragPath,
                        const std::string& ddaNearClipSafeOpaqueFragPath,
                        const std::string& ddaSharedAlignedOpaqueFragPath,
                        const std::string& ddaSharedAlignedNearClipFragPath,
                        const std::string& ddaDiagnosticFragPath);

    bool create(::VulkanContext& ctx, VkRenderPass gbufferRenderPass, VkExtent2D extent,
                VkDescriptorSetLayout frameSetLayout, VkDescriptorSetLayout voxelSetLayout,
                uint32_t gbufferColorAttachmentCount, VkBuffer waterBuffer);
    void destroy(::VulkanContext& ctx);

    void draw(VkCommandBuffer cmd, VkDescriptorSet frameSet, VkDescriptorSet voxelSet,
              uint32_t volumeIndex, bool opaqueOnlyDda, bool unwrappedOpaqueDda,
              bool fullScreenCoverageRequired,
              bool cameraInsideRasterBounds, bool faceCullingSafe,
              bool frontFaceWindingReversed);
    void drawSharedAligned(VkCommandBuffer cmd, VkDescriptorSet frameSet,
                           VkDescriptorSet firstVoxelSet,
                           VkDescriptorSet secondVoxelSet,
                           uint32_t firstVolumeIndex,
                           uint32_t secondVolumeIndex,
                           bool fullScreenCoverageRequired,
                           bool cameraInsideRasterBounds,
                           bool faceCullingSafe,
                           bool frontFaceWindingReversed);
    bool supportsSharedAlignedTraversal() const;
    void setDebugMode(uint32_t mode);
    void setSkipSettings(bool enabled, uint32_t mipLevel);
    void setHeatmapSettings(uint32_t mode, float maxValue, float gamma);
    void setMetricsSettings(bool enabled, uint32_t sampleStride, uint32_t sampleOffsetX,
                            uint32_t sampleOffsetY);
    void setNormalEdgeSmoothing(float smoothing);
    void setPixelEdgeShadowStrength(float strength);
    void setPaintedSurfaceSettings(float cavityStrength, float paintedMaterialStrength);
    void setCellVariationSettings(const VoxelCellVariationSettings& settings);
    void setWaterVolumeCount(uint32_t count);
    void setReflectionClip(float clipY, bool enabled);
    void setTime(float timeSeconds);

    // metrics management
    void clearMetrics(VkCommandBuffer cmd);
    void recordMetricsCopy(VkCommandBuffer cmd, uint32_t frameIndex);
    DDAMetrics readMetrics(::VulkanContext& ctx, uint32_t frameIndex);
    bool metricsEnabled() const
    {
        return m_metricsEnabled;
    }

  private:
    void drawInternal(VkCommandBuffer cmd, VkDescriptorSet frameSet,
                      VkDescriptorSet voxelSet, uint32_t volumeIndex,
                      bool opaqueOnlyDda, bool unwrappedOpaqueDda,
                      bool fullScreenCoverageRequired,
                      bool cameraInsideRasterBounds, bool faceCullingSafe,
                      bool frontFaceWindingReversed,
                      VkDescriptorSet secondaryVoxelSet,
                      uint32_t secondaryVolumeIndex);

    render::UniquePipelineLayout m_layout{};
    render::UniquePipelineLayout m_sharedAlignedLayout{};
    render::UniquePipeline m_ddaHotPipeline{};
    render::UniquePipeline m_ddaHotFrontCullPipeline{};
    render::UniquePipeline m_ddaHotBackCullPipeline{};
    render::UniquePipeline m_ddaOpaquePipeline{};
    render::UniquePipeline m_ddaOpaqueFrontCullPipeline{};
    render::UniquePipeline m_ddaOpaqueBackCullPipeline{};
    render::UniquePipeline m_ddaUnwrappedOpaquePipeline{};
    render::UniquePipeline m_ddaUnwrappedOpaqueFrontCullPipeline{};
    render::UniquePipeline m_ddaUnwrappedOpaqueBackCullPipeline{};
    render::UniquePipeline m_ddaNearClipSafeOpaqueFullScreenPipeline{};
    render::UniquePipeline m_ddaSharedAlignedPipeline{};
    render::UniquePipeline m_ddaSharedAlignedFrontCullPipeline{};
    render::UniquePipeline m_ddaSharedAlignedBackCullPipeline{};
    render::UniquePipeline m_ddaSharedAlignedFullScreenPipeline{};
    render::UniquePipeline m_ddaDiagnosticPipeline{};
    render::UniquePipeline m_ddaDiagnosticFullScreenPipeline{};
    render::UniquePipeline m_ddaDiagnosticFrontCullPipeline{};
    render::UniquePipeline m_ddaDiagnosticBackCullPipeline{};

    render::UniqueBuffer m_vb{};
    render::UniqueBuffer m_ib{};

    uint32_t m_indexCount = 0;
    VkExtent2D m_extent{};

    std::string m_vertPath;
    std::string m_fullScreenVertPath;
    std::string m_sharedAlignedVertPath;
    std::string m_ddaHotFragPath;
    std::string m_ddaOpaqueFragPath;
    std::string m_ddaUnwrappedOpaqueFragPath;
    std::string m_ddaNearClipSafeOpaqueFragPath;
    std::string m_ddaSharedAlignedOpaqueFragPath;
    std::string m_ddaSharedAlignedNearClipFragPath;
    std::string m_ddaDiagnosticFragPath;

    uint32_t m_debugMode = 0;
    uint32_t m_skipEnabled = 0;
    uint32_t m_skipMip = 0;
    uint32_t m_heatmapMode = 0; // 0 = iterations, 1 = skip jumps
    float m_heatmapMax = 128.0f;
    float m_heatmapGamma = 1.0f;
    float m_normalEdgeSmoothing = 0.15f; // day 12D: edge smoothing (0 = off, 0.15 = recommended)
    float m_pixelEdgeShadowStrength = 0.35f;
    float m_cavityStrength = 1.0f;
    float m_paintedMaterialStrength = 0.0f;
    VoxelCellVariationSettings m_cellVariation{};

    // metrics ssbo for day 8.5
    bool m_metricsEnabled = false;
    uint32_t m_metricsSampleStride = 4;
    uint32_t m_metricsSampleOffsetX = 0;
    uint32_t m_metricsSampleOffsetY = 0;
    render::UniqueBuffer m_metricsBuffer{};
    std::vector<render::UniqueBuffer> m_metricsReadbackBuffers{};
    VkDescriptorSetLayout m_metricsSetLayout = VK_NULL_HANDLE;
    VkDescriptorSet m_metricsDescSet = VK_NULL_HANDLE;

    // water volume ssbo descriptor set (day 18)
    VkDescriptorSetLayout m_waterSetLayout = VK_NULL_HANDLE;
    VkDescriptorSet m_waterDescSet = VK_NULL_HANDLE;
    uint32_t m_waterVolumeCount = 0;
    float m_reflectionClipY = 0.0f;
    uint32_t m_reflectionClipEnabled = 0;
    float m_timeSeconds = 0.0f;
};

} // namespace engine
