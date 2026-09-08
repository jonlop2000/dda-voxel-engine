#pragma once

#include <string>
#include <vector>

#ifndef GLM_FORCE_RADIANS
#define GLM_FORCE_RADIANS
#endif
#ifndef GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#endif

#include <glm/glm.hpp>
#include <vulkan/vulkan.h>

#include "engine/render/RendererConfig.h"
#include "engine/render/gpu/UniqueHandle.h"

struct VulkanContext;
struct FrameContext;
struct PassCreateInfo;
struct GBufferPass;

struct WaterVolumePrepass
{
    struct FrameUbo
    {
        glm::mat4 invViewProj{1.0f};
        glm::vec4 camPos{0.0f, 0.0f, 0.0f, 1.0f};
        glm::vec4 params{0.0f}; // x = container count
    };

    void setShaderPaths(const std::string& fullscreenVert, const std::string& frag);
    void setContainerBuffer(VkBuffer buffer);

    void create(VulkanContext& ctx, const PassCreateInfo& ci, const GBufferPass& gbuffer);
    void destroy(VulkanContext& ctx);
    void onResize(VulkanContext& ctx, const PassCreateInfo& ci, const GBufferPass& gbuffer);

    void recordLegacyCopy(VkCommandBuffer cmd, uint32_t frameIndex,
                          const GBufferPass& gbuffer);
    void record(VulkanContext& ctx, const FrameContext& fc, const FrameUbo& frameUbo);
    VkImageView legacyWaterDistView(uint32_t frameIndex) const;
    VkSampler legacyWaterDistSampler(uint32_t frameIndex) const;

private:
    struct FrameResources
    {
        VkFramebuffer framebuffer = VK_NULL_HANDLE;
    };

    struct PerFrameUbo
    {
        VkBuffer buffer = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        void* mapped = nullptr;
    };

    struct DebugTarget
    {
        VkFormat format = VK_FORMAT_UNDEFINED;
        VkImage image = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
        VkSampler sampler = VK_NULL_HANDLE;
    };

    void createRenderPass(VulkanContext& ctx, VkFormat waterDistFormat);
    void destroyRenderPass(VulkanContext& ctx);
    void createPipeline(VulkanContext& ctx);
    void destroyPipeline(VulkanContext& ctx);
    void createFramebuffers(VulkanContext& ctx, const GBufferPass& gbuffer);
    void destroyFramebuffers(VulkanContext& ctx);
    void createLegacyTargets(VulkanContext& ctx, const PassCreateInfo& ci);
    void destroyLegacyTargets(VulkanContext& ctx);
    void updateDescriptorSets(VulkanContext& ctx, const GBufferPass& gbuffer);
    void updateUbo(uint32_t frameIndex, const FrameUbo& frameUbo);

    std::vector<FrameResources> frames_{};
    std::vector<PerFrameUbo> ubos_{};
    std::vector<VkDescriptorSet> descSets_{};
    std::vector<DebugTarget> legacyWaterDist_{};

    VkExtent2D extent_{};
    VkFormat waterDistFormat_ = VK_FORMAT_UNDEFINED;
    VkBuffer containerBuffer_ = VK_NULL_HANDLE;

    VkRenderPass renderPass_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout descSetLayout_ = VK_NULL_HANDLE;
    engine::render::UniquePipelineLayout pipelineLayout_{};
    engine::render::UniquePipeline pipeline_{};

    std::string fullscreenVertPath_{};
    std::string fragPath_{};
};
