#pragma once

#include <array>
#include <string>
#include <vector>

#include <vulkan/vulkan.h>

#ifndef GLM_FORCE_RADIANS
#define GLM_FORCE_RADIANS
#endif
#ifndef GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#endif

#include <glm/glm.hpp>

#include "engine/render/RendererConfig.h"
#include "engine/render/gpu/UniqueAllocation.h"
#include "engine/render/gpu/UniqueHandle.h"

struct VulkanContext;
struct FrameContext;
struct PassCreateInfo;
struct GBufferPass;
struct LightingPass;
struct BloomPass;

struct TAAPass
{
    void setShaderPaths(const std::string& fullscreenVert, const std::string& fragPath);

    void create(VulkanContext& ctx, const PassCreateInfo& ci, const GBufferPass& gbuffer,
                const LightingPass& lighting, const BloomPass& bloom);
    void destroy(VulkanContext& ctx);
    void onResize(VulkanContext& ctx, const PassCreateInfo& ci, const GBufferPass& gbuffer,
                  const LightingPass& lighting, const BloomPass& bloom);

    void record(VulkanContext& ctx, const FrameContext& fc, const glm::vec2& jitterOffset,
                const glm::vec2& prevJitterOffset, float similarityThreshold, float velocityScale,
                float blendFactorMin, float blendFactorMax, float bloomIntensity, bool resetHistory,
                int debugMode, float depthEdgeThreshold = 0.1f, float nearPlane = 0.1f,
                float crossFrameDepthThreshold = 0.02f, float farPlane = 1000.0f,
                float colorVarianceThreshold = 0.15f, float softEdgeStrength = 0.0f);

    VkImageView outputView() const
    {
        return historyViews_[outputHistory_].get();
    }
    VkSampler sampler() const
    {
        return linearSampler_;
    }

    void setEnabled(bool enabled)
    {
        enabled_ = enabled;
    }
    bool isEnabled() const
    {
        return enabled_;
    }

    // debug accessors
    uint32_t historyReadIndex() const
    {
        return historyRead_;
    }
    uint32_t outputHistoryIndex() const
    {
        return outputHistory_;
    }

    // depth history accessors for cross-frame rejection
    VkImage depthHistoryImage(uint32_t index) const
    {
        return depthHistoryImages_[index].get();
    }
    VkImageView depthHistoryView(uint32_t index) const
    {
        return depthHistoryViews_[index].get();
    }

  private:
    void createHistoryBuffers(VulkanContext& ctx, const PassCreateInfo& ci);
    void destroyHistoryBuffers(VulkanContext& ctx);
    void createRenderPass(VulkanContext& ctx);
    void createPipeline(VulkanContext& ctx);
    void destroyPipeline(VulkanContext& ctx);
    void createDescriptorSets(VulkanContext& ctx);
    void destroyDescriptorSets(VulkanContext& ctx);
    void updateDescriptorSets(VulkanContext& ctx, const GBufferPass& gbuffer,
                              const LightingPass& lighting, const BloomPass& bloom);
    void createSamplers(VulkanContext& ctx);
    void destroySamplers(VulkanContext& ctx);
    void createDepthHistoryBuffers(VulkanContext& ctx, const PassCreateInfo& ci);
    void destroyDepthHistoryBuffers(VulkanContext& ctx);

    VkFormat format_ = VK_FORMAT_R16G16B16A16_SFLOAT;
    VkExtent2D extent_{};

    VkRenderPass renderPass_ = VK_NULL_HANDLE;
    engine::render::UniquePipelineLayout pipelineLayout_{};
    engine::render::UniquePipeline pipeline_{};

    std::array<engine::render::UniqueImage, 2> historyImages_{};
    std::array<engine::render::UniqueImageView, 2> historyViews_{};
    std::array<engine::render::UniqueFramebuffer, 2> historyFramebuffers_{};

    VkDescriptorSetLayout setLayout_ = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> descSets_{};

    VkSampler linearSampler_ = VK_NULL_HANDLE;
    VkSampler nearestSampler_ = VK_NULL_HANDLE;

    // depth history for cross-frame rejection (ping-pong like color history)
    std::array<engine::render::UniqueImage, 2> depthHistoryImages_{};
    std::array<engine::render::UniqueImageView, 2> depthHistoryViews_{};
    VkFormat depthHistoryFormat_ = VK_FORMAT_R32_SFLOAT;

    uint32_t historyRead_ = 0;
    uint32_t outputHistory_ = 0;
    bool enabled_ = true;
    bool firstFrame_ = true;

    std::string fullscreenVertPath_{};
    std::string fragPath_{};
};
