#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include <glm/glm.hpp>
#include <vulkan/vulkan.h>

#include "engine/render/gpu/UniqueAllocation.h"
#include "engine/render/gpu/UniqueHandle.h"

struct VulkanContext;
class BlueNoiseTexture;

class AOPass
{
  public:
    struct PushConstants
    {
        glm::mat4 invViewProj;
        glm::vec4 camPos;
        glm::vec4 aoParams; // x=maxDist, y=stepSize, z=intensity, w=bias
        glm::ivec2 resolution;
        uint32_t volumeCount;
        uint32_t frameIndex;
        uint32_t rayCount;
        uint32_t volumeIndex;
        float projectedDistanceScale;
        float projectedMinDistance;
    };
    static_assert(sizeof(PushConstants) == 128,
                  "AOPass::PushConstants must preserve the 128-byte layout");
    static_assert(offsetof(PushConstants, invViewProj) == 0);
    static_assert(offsetof(PushConstants, camPos) == 64);
    static_assert(offsetof(PushConstants, aoParams) == 80);
    static_assert(offsetof(PushConstants, resolution) == 96);
    static_assert(offsetof(PushConstants, volumeCount) == 104);
    static_assert(offsetof(PushConstants, frameIndex) == 108);
    static_assert(offsetof(PushConstants, rayCount) == 112);
    static_assert(offsetof(PushConstants, volumeIndex) == 116);
    static_assert(offsetof(PushConstants, projectedDistanceScale) == 120);
    static_assert(offsetof(PushConstants, projectedMinDistance) == 124);

    struct FrameInputs
    {
        glm::mat4 invViewProj{1.0f};
        glm::vec3 cameraPosition{0.0f};
        float maxDistance = 0.0f;
        float stepSize = 0.0f;
        float intensity = 0.0f;
        float bias = 0.0f;
        VkExtent2D extent{};
        uint32_t volumeCount = 0;
        uint32_t frameIndex = 0;
        uint32_t rayCount = 1;
        float projectedDistanceScale = 0.0f;
        float projectedMinDistance = 0.0f;
    };

    [[nodiscard]] static PushConstants buildPushConstants(
        const FrameInputs& inputs) noexcept
    {
        PushConstants result{};
        result.invViewProj = inputs.invViewProj;
        result.camPos = glm::vec4(inputs.cameraPosition, 1.0f);
        result.aoParams = glm::vec4(inputs.maxDistance, inputs.stepSize,
                                    inputs.intensity, inputs.bias);
        result.resolution = glm::ivec2(static_cast<int>(inputs.extent.width),
                                       static_cast<int>(inputs.extent.height));
        result.volumeCount = inputs.volumeCount;
        result.frameIndex = inputs.frameIndex;
        result.rayCount = inputs.rayCount;
        result.projectedDistanceScale = inputs.projectedDistanceScale;
        result.projectedMinDistance = inputs.projectedMinDistance;
        return result;
    }

    bool create(VulkanContext& ctx, VkExtent2D extent, VkDescriptorSetLayout voxelSetLayout);
    void destroy(VulkanContext& ctx);
    bool resize(VulkanContext& ctx, VkExtent2D extent);

    void prepareFrame(VulkanContext& ctx, VkImageView gDepth, VkSampler depthSampler,
                      VkImageView gNormal, VkSampler normalSampler, BlueNoiseTexture& blueNoise,
                      VkBuffer tileListBuffer, VkDeviceSize tileListBufferSize,
                      uint32_t frameIndex);
    void dispatch(VkCommandBuffer cmd, const PushConstants& pc, VkDescriptorSet voxelSet,
                  VkBuffer tileListBuffer, uint32_t frameIndex);

    VkImage aoImage() const
    {
        return aoImage_.get();
    }
    VkImageView aoView() const
    {
        return aoView_.get();
    }
    VkImageView aoStorageView() const
    {
        return aoStorageView_.get();
    }
    VkSampler aoSampler() const
    {
        return aoSampler_;
    }
    VkExtent2D extent() const
    {
        return extent_;
    }
  private:
    bool createPipeline(VulkanContext& ctx);
    bool createDescriptorSets(VulkanContext& ctx);
    bool createAOImage(VulkanContext& ctx);
    void destroyResources(VulkanContext& ctx);
    void updateDescriptorSet(VulkanContext& ctx, uint32_t frameIndex, VkImageView gDepth,
                             VkSampler depthSampler, VkImageView gNormal, VkSampler normalSampler,
                             BlueNoiseTexture& blueNoise, VkBuffer tileListBuffer,
                             VkDeviceSize tileListBufferSize);

    VkExtent2D extent_{};

    engine::render::UniqueImage aoImage_{};
    engine::render::UniqueImageView aoView_{};
    engine::render::UniqueImageView aoStorageView_{};
    VkSampler aoSampler_ = VK_NULL_HANDLE;

    engine::render::UniquePipeline pipeline_{};
    engine::render::UniquePipelineLayout pipelineLayout_{};
    VkDescriptorSetLayout descriptorSetLayout_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout dummySetLayout_ = VK_NULL_HANDLE;
    // borrowed (owned by the caller), so it stays a raw handle.
    VkDescriptorSetLayout voxelSetLayout_ = VK_NULL_HANDLE;

    static constexpr uint32_t kMaxFramesInFlight = 2;
    VkDescriptorSet descriptorSets_[kMaxFramesInFlight] = {};

    std::string shaderPath_;
};
