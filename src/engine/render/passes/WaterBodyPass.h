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
struct LightingPass;

struct WaterBodyPass
{
    struct Target
    {
        VkFormat format = VK_FORMAT_R16G16B16A16_SFLOAT;
        VkImage image = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
        VkSampler sampler = VK_NULL_HANDLE;

        VkRenderPass renderPass = VK_NULL_HANDLE;
        VkFramebuffer framebuffer = VK_NULL_HANDLE;
    };

    struct FrameUbo
    {
        glm::mat4 invViewProj{1.0f};
        glm::vec4 camPos{0.0f, 0.0f, 0.0f, 1.0f};
        glm::vec4 sunDirToSun{0.0f, 1.0f, 0.0f, 0.0f};
        glm::vec4 waterTint{0.02f, 0.12f, 0.18f, 1.0f};
        // x=time, y=debugMode, z=waterVolumeCount, w=authored mote intensity
        glm::vec4 params0{0.0f, 0.0f, 0.0f, 1.0f};
        // x=bodyStrength, y=pathRate, z=scatterStrength, w=noiseStrength
        glm::vec4 params1{0.56f, 0.44f, 0.145f, 0.34f};
        // x=shaftStrength, y=bandContrast, z=apertureHalfExtent, w=warmth
        glm::vec4 params2{1.0f, 0.0f, 0.0f, 0.0f};
    };

    void setShaderPaths(const std::string& fullscreenVert, const std::string& fragPath);
    void setWaterVolumeBuffer(VkBuffer buffer);

    void create(VulkanContext& ctx, const PassCreateInfo& ci, const GBufferPass& gbuffer,
                const LightingPass& lighting);
    void destroy(VulkanContext& ctx);
    void onResize(VulkanContext& ctx, const PassCreateInfo& ci, const GBufferPass& gbuffer,
                  const LightingPass& lighting);
    void record(VulkanContext& ctx, const FrameContext& fc, const FrameUbo& ubo);
    void copyToLighting(VkCommandBuffer cmd, uint32_t frameIndex,
                        const LightingPass& lighting) const;

    const Target& target(uint32_t frameIndex) const;
    VkExtent2D extent() const;

private:
    struct PerFrameUbo
    {
        VkBuffer buffer = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        void* mapped = nullptr;
    };

    void createPipeline(VulkanContext& ctx);
    void destroyPipeline(VulkanContext& ctx);
    void createTargets(VulkanContext& ctx, const PassCreateInfo& ci);
    void destroyTargets(VulkanContext& ctx);
    void updateDescriptorSets(VulkanContext& ctx, const GBufferPass& gbuffer,
                              const LightingPass& lighting);
    void updateWaterDescriptor(VulkanContext& ctx);
    void updateUbo(uint32_t frameIndex, const FrameUbo& ubo);

    std::vector<Target> targets_{};
    VkExtent2D extent_{};

    VkDescriptorSetLayout texSetLayout_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout uboSetLayout_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout waterSetLayout_ = VK_NULL_HANDLE;
    engine::render::UniquePipelineLayout pipelineLayout_{};
    engine::render::UniquePipeline pipeline_{};
    std::vector<VkDescriptorSet> texSets_{};
    std::vector<VkDescriptorSet> uboSets_{};
    VkDescriptorSet waterSet_ = VK_NULL_HANDLE;
    std::vector<PerFrameUbo> ubos_{};

    VkBuffer waterVolumeBuffer_ = VK_NULL_HANDLE;
    std::string fullscreenVertPath_{};
    std::string fragPath_{};
};
