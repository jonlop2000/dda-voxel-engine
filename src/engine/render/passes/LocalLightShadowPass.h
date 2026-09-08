#pragma once

#include <string>

#include <glm/glm.hpp>
#include <vulkan/vulkan.h>

#include "engine/render/gpu/UniqueHandle.h"

struct VulkanContext;
struct LightsBuffer;
class BlueNoiseTexture;
class LocalLightShadowBuffer;

class LocalLightShadowPass
{
public:
    struct PushConstants
    {
        glm::mat4 invViewProj;
        glm::vec4 camPos;
        glm::ivec2 resolution;
        uint32_t volumeCount;
        uint32_t frameIndex;
        float maxShadowDist;
        float normalBias;
        int32_t maxStepsPerVolume;
        int32_t shadowEnabled;
        uint32_t volumeIndex;
        uint32_t lightCount;
        uint32_t slotCount;
        uint32_t _pad0;
        uint32_t lightSlots[4];
    };

    bool create(VulkanContext& ctx, VkExtent2D extent, VkDescriptorSetLayout voxelSetLayout);
    void destroy(VulkanContext& ctx);
    void resize(VulkanContext& ctx, VkExtent2D extent);

    void prepareFrame(VulkanContext& ctx, VkImageView gDepth, VkSampler depthSampler,
                      VkImageView gNormal, VkSampler normalSampler,
                      BlueNoiseTexture& blueNoise, const LightsBuffer& lights,
                      LocalLightShadowBuffer& shadowOut, VkBuffer tileWorkBuffer,
                      VkDeviceSize tileWorkBufferSize, uint32_t frameIndex);
    void dispatch(VkCommandBuffer cmd, const PushConstants& pc, VkDescriptorSet voxelSet,
                  VkBuffer tileListBuffer, uint32_t frameIndex);

private:
    bool createPipeline(VulkanContext& ctx);
    bool createDescriptorSets(VulkanContext& ctx);
    void updateDescriptorSet(VulkanContext& ctx, uint32_t frameIndex,
                             VkImageView gDepth, VkSampler depthSampler,
                             VkImageView gNormal, VkSampler normalSampler,
                             BlueNoiseTexture& blueNoise, const LightsBuffer& lights,
                             LocalLightShadowBuffer& shadowOut, VkBuffer tileWorkBuffer,
                             VkDeviceSize tileWorkBufferSize);

    VkExtent2D extent_{};
    engine::render::UniquePipeline pipeline_{};
    engine::render::UniquePipelineLayout pipelineLayout_{};
    VkDescriptorSetLayout descriptorSetLayout_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout lightSetLayout_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout voxelSetLayout_ = VK_NULL_HANDLE;

    static constexpr uint32_t kMaxFramesInFlight = 2;
    VkDescriptorSet descriptorSets_[kMaxFramesInFlight] = {};
    VkDescriptorSet lightSets_[kMaxFramesInFlight] = {};

    std::string shaderPath_;
};
