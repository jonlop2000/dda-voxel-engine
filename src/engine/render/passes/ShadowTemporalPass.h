#pragma once

#include <string>
#include <glm/glm.hpp>
#include <vulkan/vulkan.h>

struct VulkanContext;
class ShadowBuffer;

/**
 * ShadowTemporalPass resolves noisy shadow rays into smooth output
 * via temporal accumulation with motion-vector reprojection.
 */
class ShadowTemporalPass
{
public:
    struct PushConstants
    {
        glm::ivec2 resolution;
        float blendAlpha;
        float depthRejectThresh;
        int32_t resetHistory;
        int32_t debugMode;
        float _pad0;
        float _pad1;
    };

    bool create(VulkanContext& ctx);
    void destroy(VulkanContext& ctx);

    void dispatch(VkCommandBuffer cmd, VulkanContext& ctx, const PushConstants& pc,
                  ShadowBuffer& shadowBuffer,
                  VkImageView velocityView, VkSampler velocitySampler,
                  VkImageView depthView, VkSampler depthSampler,
                  VkImageView depthHistoryView, VkSampler depthHistorySampler,
                  uint32_t frameIndex);

private:
    bool createPipeline(VulkanContext& ctx);
    bool createDescriptorSets(VulkanContext& ctx);
    void updateDescriptorSet(VulkanContext& ctx, uint32_t frameIndex,
                             ShadowBuffer& shadowBuffer,
                             VkImageView velocityView, VkSampler velocitySampler,
                             VkImageView depthView, VkSampler depthSampler,
                             VkImageView depthHistoryView, VkSampler depthHistorySampler);

    VkPipeline pipeline_ = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout descriptorSetLayout_ = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool_ = VK_NULL_HANDLE;

    static constexpr uint32_t kMaxFramesInFlight = 2;
    VkDescriptorSet descriptorSets_[kMaxFramesInFlight] = {};

    std::string shaderPath_;
};
