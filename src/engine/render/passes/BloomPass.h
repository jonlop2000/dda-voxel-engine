#pragma once

#include <string>
#include <vector>

#include <vulkan/vulkan.h>

#include "engine/render/RendererConfig.h"
#include "engine/render/gpu/UniqueHandle.h"

struct VulkanContext;
struct FrameContext;
struct PassCreateInfo;
struct LightingPass;

struct BloomPass
{
    struct Target
    {
        VkFormat format = VK_FORMAT_R16G16B16A16_SFLOAT;
        // image + backing memory stay raw (image-owner category for the future
        // vma-backed bundled owner); the device-child handles are raii.
        VkImage image = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        engine::render::UniqueImageView view{};
        VkSampler sampler = VK_NULL_HANDLE;
        engine::render::UniqueRenderPass renderPass{};
        engine::render::UniqueFramebuffer framebuffer{};
    };

    struct Settings
    {
        float threshold = 1.2f;
        float knee = 0.5f;
        float blurSigma = 2.0f;
    };

    void setShaderPaths(const std::string& fullscreenVert, const std::string& extractFrag,
                        const std::string& blurFrag);

    void create(VulkanContext& ctx, const PassCreateInfo& ci, const LightingPass& lighting);
    void destroy(VulkanContext& ctx);
    void onResize(VulkanContext& ctx, const PassCreateInfo& ci, const LightingPass& lighting);
    void updateDescriptorSets(VulkanContext& ctx, const LightingPass& lighting);
    void record(VulkanContext& ctx, const FrameContext& fc, const Settings& settings, bool run);

    const Target& extract(uint32_t frameIndex) const;
    const Target& blur(uint32_t frameIndex) const;
    VkExtent2D extent() const;
    VkExtent2D halfExtent() const;

private:
    void createPipeline(VulkanContext& ctx);
    void destroyPipeline(VulkanContext& ctx);
    void createTargets(VulkanContext& ctx, const PassCreateInfo& ci);
    void destroyTargets(VulkanContext& ctx);
    void recordExtract(const FrameContext& fc, const Settings& settings);
    void recordBlur(const FrameContext& fc, float dirX, float dirY, float sigma, VkDescriptorSet set,
                    const Target& target);
    void recordBarrier(VkCommandBuffer cmd, VkImage image);

    std::vector<Target> extractTargets_{};
    std::vector<Target> pingTargets_{};
    std::vector<Target> pongTargets_{};
    VkExtent2D extent_{};
    VkExtent2D halfExtent_{};

    VkDescriptorSetLayout texSetLayout_ = VK_NULL_HANDLE;
    engine::render::UniquePipelineLayout extractPipelineLayout_{};
    engine::render::UniquePipelineLayout blurPipelineLayout_{};
    engine::render::UniquePipeline extractPipeline_{};
    engine::render::UniquePipeline blurPipeline_{};
    std::vector<VkDescriptorSet> extractSets_{};
    std::vector<VkDescriptorSet> blurHSets_{};
    std::vector<VkDescriptorSet> blurVSets_{};

    std::string fullscreenVertPath_{};
    std::string extractFragPath_{};
    std::string blurFragPath_{};
};
