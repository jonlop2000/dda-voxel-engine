#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <glm/glm.hpp>
#include <vulkan/vulkan.h>

#include "engine/render/SunShadowSamplingPolicy.h"
#include "engine/render/gpu/UniqueAllocation.h"
#include "engine/render/gpu/UniqueHandle.h"

struct VulkanContext;
struct FrameContext;
class ShadowBuffer;
class BlueNoiseTexture;

/**
 * ShadowRayPass dispatches stochastic shadow rays using dda traversal
 * through all static voxel volumes.
 */
class ShadowRayPass
{
public:
    enum class TerrainShadowColumnMode : uint32_t
    {
        Disabled = 0,
        Enabled = 1,
        ValidateParity = 2,
    };

    struct TerrainShadowColumnParityTotals
    {
        uint64_t comparedRays = 0;
        uint64_t mismatchedRays = 0;
        uint64_t candidateOnlyRays = 0;
        uint64_t referenceOnlyRays = 0;
        bool hasFirstMismatch = false;
        bool firstCandidateOccluded = false;
        bool firstReferenceOccluded = false;
        uint32_t firstVolumeIndex = 0;
        glm::vec3 firstLocalOrigin{};
        glm::vec3 firstLocalDirection{};
        float firstMaxT = 0.0f;
        int32_t firstMaxSteps = 0;
    };

    struct PushConstants
    {
        glm::mat4 invViewProj;
        glm::vec4 sunDir;  // xyz = direction to sun (normalized), w = angular radius
        glm::vec4 camPos;
        glm::ivec2 resolution;
        uint32_t volumeCount;
        uint32_t frameIndex;
        float maxShadowDist;
        float normalBias;
        int32_t maxStepsPerVolume;
        int32_t shadowEnabled;
        uint32_t volumeIndex;  // for multi-dispatch if needed
        float cloudShadowStrength;
        uint32_t sunSampleCount;
        float foliageShadowOpacityScale;
        uint32_t terrainShadowColumnMode;
    };
    static_assert(offsetof(PushConstants, invViewProj) == 0);
    static_assert(offsetof(PushConstants, sunDir) == 64);
    static_assert(offsetof(PushConstants, camPos) == 80);
    static_assert(offsetof(PushConstants, resolution) == 96);
    static_assert(offsetof(PushConstants, volumeCount) == 104);
    static_assert(offsetof(PushConstants, frameIndex) == 108);
    static_assert(offsetof(PushConstants, maxShadowDist) == 112);
    static_assert(offsetof(PushConstants, normalBias) == 116);
    static_assert(offsetof(PushConstants, maxStepsPerVolume) == 120);
    static_assert(offsetof(PushConstants, shadowEnabled) == 124);
    static_assert(offsetof(PushConstants, volumeIndex) == 128);
    static_assert(offsetof(PushConstants, cloudShadowStrength) == 132);
    static_assert(offsetof(PushConstants, sunSampleCount) == 136);
    static_assert(offsetof(PushConstants, foliageShadowOpacityScale) == 140);
    static_assert(offsetof(PushConstants, terrainShadowColumnMode) == 144);
    static_assert(sizeof(PushConstants) == 148,
                  "ShadowRayPass push constants must match shadow_ray.comp");

    void setSamplingMode(engine::render::SunShadowSamplingMode mode)
    {
        samplingMode_ = mode;
    }
    [[nodiscard]] engine::render::SunShadowSamplingMode samplingMode() const
    {
        return samplingMode_;
    }
    [[nodiscard]] engine::render::SunShadowSamplingDecision samplingDecision(
        int authoredSampleCount) const
    {
        return engine::render::resolveSunShadowSampling(
            samplingMode_, authoredSampleCount);
    }
    [[nodiscard]] uint32_t lastDispatchedSunSampleCount() const
    {
        return lastDispatchedSunSampleCount_;
    }
    void configureTerrainShadowColumns(bool enabled, bool validateParity)
    {
        terrainShadowColumnMode_ = validateParity
                                       ? TerrainShadowColumnMode::ValidateParity
                                       : enabled
                                             ? TerrainShadowColumnMode::Enabled
                                             : TerrainShadowColumnMode::Disabled;
        terrainShadowColumnParityTotals_ = {};
        parityMetricsSubmitted_.fill(false);
    }
    [[nodiscard]] bool terrainShadowColumnsEnabled() const
    {
        return terrainShadowColumnMode_ == TerrainShadowColumnMode::Enabled;
    }
    [[nodiscard]] TerrainShadowColumnMode terrainShadowColumnMode() const
    {
        return terrainShadowColumnMode_;
    }
    [[nodiscard]] TerrainShadowColumnParityTotals
    terrainShadowColumnParityTotals() const
    {
        return terrainShadowColumnParityTotals_;
    }

    bool create(VulkanContext& ctx, VkDescriptorSetLayout voxelSetLayout);
    void destroy(VulkanContext& ctx);

    void prepareFrame(VulkanContext& ctx, VkImageView gDepth, VkSampler depthSampler,
                      VkImageView gNormal, VkSampler normalSampler,
                      BlueNoiseTexture& blueNoise, ShadowBuffer& shadowOut,
                      VkBuffer tileWorkBuffer, VkDeviceSize tileWorkBufferSize,
                      uint32_t frameIndex);
    void dispatch(VkCommandBuffer cmd, const PushConstants& pc, VkDescriptorSet voxelSet,
                  VkBuffer tileListBuffer, uint32_t frameIndex);
    void finishFrame(VkCommandBuffer cmd, uint32_t frameIndex);

private:
    bool createPipeline(VulkanContext& ctx);
    bool createParityMetricBuffers(VulkanContext& ctx);
    bool createDescriptorSets(VulkanContext& ctx);
    void collectParityMetrics(VulkanContext& ctx, uint32_t frameIndex);
    void updateDescriptorSet(VulkanContext& ctx, uint32_t frameIndex,
                              VkImageView gDepth, VkSampler depthSampler,
                              VkImageView gNormal, VkSampler normalSampler,
                              BlueNoiseTexture& blueNoise, ShadowBuffer& shadowOut,
                              VkBuffer tileWorkBuffer, VkDeviceSize tileWorkBufferSize);

    engine::render::UniquePipeline pipeline_{};
    engine::render::UniquePipelineLayout pipelineLayout_{};
    VkDescriptorSetLayout descriptorSetLayout_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout dummySetLayout_ = VK_NULL_HANDLE;  // empty layout for unused set 1
    VkDescriptorSetLayout voxelSetLayout_ = VK_NULL_HANDLE;

    static constexpr uint32_t kMaxFramesInFlight = 2;
    VkDescriptorSet descriptorSets_[kMaxFramesInFlight] = {};
    struct alignas(16) TerrainShadowColumnParityMetrics
    {
        uint32_t comparedRays = 0;
        uint32_t mismatchedRays = 0;
        uint32_t candidateOnlyRays = 0;
        uint32_t referenceOnlyRays = 0;
        uint32_t firstClaimed = 0;
        uint32_t firstCandidateOccluded = 0;
        uint32_t firstReferenceOccluded = 0;
        uint32_t firstVolumeIndex = 0;
        uint32_t firstLocalOriginX = 0;
        uint32_t firstLocalOriginY = 0;
        uint32_t firstLocalOriginZ = 0;
        uint32_t firstMaxT = 0;
        uint32_t firstLocalDirectionX = 0;
        uint32_t firstLocalDirectionY = 0;
        uint32_t firstLocalDirectionZ = 0;
        int32_t firstMaxSteps = 0;
    };
    std::array<engine::render::UniqueBuffer, kMaxFramesInFlight>
        parityMetricBuffers_{};
    std::array<bool, kMaxFramesInFlight> parityMetricsNeedReset_{};
    std::array<bool, kMaxFramesInFlight> parityMetricsSubmitted_{};

    std::string shaderPath_;
    engine::render::SunShadowSamplingMode samplingMode_ =
        engine::render::SunShadowSamplingMode::FullPerFrame;
    uint32_t lastDispatchedSunSampleCount_ = 0;
    TerrainShadowColumnMode terrainShadowColumnMode_ =
        TerrainShadowColumnMode::Disabled;
    TerrainShadowColumnParityTotals terrainShadowColumnParityTotals_{};
};
