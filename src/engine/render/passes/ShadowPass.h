#pragma once

#include <array>
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
#include "engine/render/RenderObject.h"
#include "engine/render/gpu/UniqueHandle.h"

struct VulkanContext;
struct FrameContext;
struct PassCreateInfo;

struct ShadowMap
{
    VkExtent2D extent{2048, 2048};
    VkFormat format = VK_FORMAT_D32_SFLOAT;

    struct Cascade
    {
        VkImage image = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;

        // debug color attachment for visualizing depth
        VkImage debugImage = VK_NULL_HANDLE;
        VkDeviceMemory debugMemory = VK_NULL_HANDLE;
        VkImageView debugView = VK_NULL_HANDLE;

        VkFramebuffer framebuffer = VK_NULL_HANDLE;
    };

    std::array<Cascade, kShadowCascades> cascades{};

    VkSampler sampler = VK_NULL_HANDLE;
    VkSampler debugSampler = VK_NULL_HANDLE;

    VkFormat debugFormat = VK_FORMAT_R8G8B8A8_UNORM;
    VkSampler debugColorSampler = VK_NULL_HANDLE;

    VkRenderPass renderPass = VK_NULL_HANDLE;
};

struct ShadowPass
{
    struct FrameUbo
    {
        std::array<glm::mat4, kShadowCascades> lightViewProj{};
    };

    void setShaderPaths(const std::string& vertPath, const std::string& fragPath);

    void create(VulkanContext& ctx, const PassCreateInfo& ci);
    void destroy(VulkanContext& ctx);
    void record(VulkanContext& ctx, const FrameContext& fc, const FrameUbo& frameUbo,
                const std::vector<RenderObject>& objects);

    const ShadowMap& map() const;

private:
    struct PerFrameUbo
    {
        VkBuffer buffer = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        void* mapped = nullptr;
    };

    void createPipeline(VulkanContext& ctx);
    void destroyPipeline(VulkanContext& ctx);
    void createResources(VulkanContext& ctx, const PassCreateInfo& ci);
    void destroyResources(VulkanContext& ctx);
    void updateUbo(uint32_t frameIndex, const FrameUbo& frameUbo);

    ShadowMap shadow_{};

    VkDescriptorSetLayout setLayout_ = VK_NULL_HANDLE;
    engine::render::UniquePipelineLayout pipelineLayout_{};
    engine::render::UniquePipeline pipeline_{};
    std::vector<VkDescriptorSet> descSets_;
    std::vector<PerFrameUbo> ubos_;

    std::string vertPath_;
    std::string fragPath_;
};
