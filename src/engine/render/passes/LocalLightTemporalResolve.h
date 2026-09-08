#pragma once

#include <array>
#include <cstdint>
#include <string>

#include <glm/glm.hpp>
#include <vulkan/vulkan.h>

#include "engine/render/gpu/UniqueAllocation.h"
#include "engine/render/gpu/UniqueHandle.h"

struct VulkanContext;

struct LocalLightTemporalResolveConfig
{
    float blendAlpha = 0.1f;
    float blendAlphaMax = 1.0f;
    float depthRejectThreshold = 0.02f;
    bool useNormalReject = false;
    float normalRejectDot = 0.85f;
    bool useNeighborhoodClamp = true;
    float clampSharpness = 1.0f;
};

class LocalLightTemporalResolve
{
  public:
    bool create(VulkanContext& ctx, VkExtent2D extent,
                const LocalLightTemporalResolveConfig& config);
    void destroy(VulkanContext& ctx);
    bool resize(VulkanContext& ctx, VkExtent2D extent);

    void setConfig(const LocalLightTemporalResolveConfig& config)
    {
        config_ = config;
    }
    const LocalLightTemporalResolveConfig& config() const
    {
        return config_;
    }

    void resolve(VkCommandBuffer cmd, VulkanContext& ctx, VkImageView currentView,
                 VkSampler currentSampler, VkExtent2D currentExtent,
                 VkImageView velocityView, VkSampler velocitySampler,
                 VkImageView depthView, VkSampler depthSampler, VkImageView normalView,
                 VkSampler normalSampler, VkImageView depthHistoryView,
                 VkSampler depthHistorySampler, bool resetHistory, int debugMode,
                 uint32_t frameIndex);

    VkImageView resolvedView() const
    {
        return historyViews_[historyIndex_].get();
    }
    VkImage resolvedImage() const
    {
        return historyImages_[historyIndex_].get();
    }
    VkSampler sampler() const
    {
        return sampler_;
    }

  private:
    struct PushConstants
    {
        glm::ivec2 resolution;
        glm::ivec2 currentResolution;
        float blendAlpha;
        float blendAlphaMax;
        float depthRejectThreshold;
        float normalRejectDot;
        float clampSharpness;
        uint32_t flags;
        uint32_t debugMode;
        float _pad0;
        float _pad1;
    };
    static_assert(sizeof(PushConstants) == 52,
                  "LocalLightTemporalResolve push constants must match GLSL");

    bool createPipeline(VulkanContext& ctx);
    bool createDescriptorSets(VulkanContext& ctx);
    void destroyResources(VulkanContext& ctx);
    bool createHistoryImages(VulkanContext& ctx);
    void updateDescriptorSet(VulkanContext& ctx, uint32_t frameIndex, VkImageView currentView,
                             VkSampler currentSampler, VkImageView velocityView,
                             VkSampler velocitySampler, VkImageView depthView,
                             VkSampler depthSampler, VkImageView normalView,
                             VkSampler normalSampler, VkImageView depthHistoryView,
                             VkSampler depthHistorySampler, VkImageView outputView);

    LocalLightTemporalResolveConfig config_{};
    VkExtent2D extent_{};
    int historyIndex_ = 0;

    engine::render::UniquePipeline pipeline_{};
    engine::render::UniquePipelineLayout pipelineLayout_{};
    VkDescriptorSetLayout descriptorSetLayout_ = VK_NULL_HANDLE;

    static constexpr uint32_t kMaxFramesInFlight = 2;
    VkDescriptorSet descriptorSets_[kMaxFramesInFlight] = {};

    std::array<engine::render::UniqueImage, 2> historyImages_{};
    std::array<engine::render::UniqueImageView, 2> historyViews_{};
    VkSampler sampler_ = VK_NULL_HANDLE;

    std::string shaderPath_;
};
