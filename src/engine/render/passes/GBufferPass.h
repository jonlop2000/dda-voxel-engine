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
struct Commands;

struct GBufferPass
{
    // g-buffer slots: albedo, world-space normal, material params, velocity, water/material meta.
    enum class Slot : uint32_t
    {
        Albedo = 0,
        Normal = 1,
        Material = 2,
        Velocity = 3,
        WaterDist = 4
    };

    static constexpr size_t kGBufferCount = 5;

    struct FrameUbo
    {
        // camera matrices for the current frame.
        glm::mat4 view{1.0f};
        glm::mat4 proj{1.0f};
        glm::mat4 viewProj{1.0f};
        glm::mat4 viewProjUnjittered{1.0f};
        glm::mat4 prevViewProjUnjittered{1.0f};
        glm::mat4 prevViewProj{1.0f};
        glm::mat4 invViewProjUnjittered{1.0f};
        glm::vec4 cameraWorld{0.0f, 0.0f, 0.0f, 1.0f};
        glm::vec2 renderSize{0.0f};
        glm::vec2 invRenderSize{0.0f};
    };

    struct FrameUboInputs
    {
        glm::mat4 view{1.0f};
        glm::mat4 proj{1.0f};
        glm::mat4 viewProj{1.0f};
        glm::mat4 viewProjUnjittered{1.0f};
        glm::mat4 prevViewProjUnjittered{1.0f};
        glm::mat4 prevViewProj{1.0f};
        glm::mat4 invViewProjUnjittered{1.0f};
        glm::vec3 cameraWorld{0.0f};
        VkExtent2D renderExtent{};
    };

    [[nodiscard]] static FrameUbo buildFrameUbo(const FrameUboInputs& in)
    {
        FrameUbo ubo{};
        ubo.view = in.view;
        ubo.proj = in.proj;
        ubo.viewProj = in.viewProj;
        ubo.viewProjUnjittered = in.viewProjUnjittered;
        ubo.prevViewProjUnjittered = in.prevViewProjUnjittered;
        ubo.prevViewProj = in.prevViewProj;
        ubo.invViewProjUnjittered = in.invViewProjUnjittered;
        ubo.cameraWorld = glm::vec4(in.cameraWorld, 1.0f);
        ubo.renderSize =
            glm::vec2(static_cast<float>(in.renderExtent.width),
                      static_cast<float>(in.renderExtent.height));
        ubo.invRenderSize =
            glm::vec2(in.renderExtent.width > 0
                          ? 1.0f / static_cast<float>(in.renderExtent.width)
                          : 1.0f,
                      in.renderExtent.height > 0
                          ? 1.0f / static_cast<float>(in.renderExtent.height)
                          : 1.0f);
        return ubo;
    }

    struct ColorAttachment
    {
        // per-frame color target used as a sampled g-buffer input.
        VkFormat format = VK_FORMAT_R8G8B8A8_UNORM;
        VkImage image = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
        VkSampler sampler = VK_NULL_HANDLE;
    };

    struct DepthTarget
    {
        // per-frame depth target (sampled in the lighting pass).
        VkFormat format = VK_FORMAT_UNDEFINED;
        VkImage image = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
        VkSampler sampler = VK_NULL_HANDLE;
    };

    void setShaderPaths(const std::string& vertPath, const std::string& fragPath);
    void setMaterialLayout(VkDescriptorSetLayout layout);

    struct ExtraDrawCall
    {
        void (*fn)(VkCommandBuffer cmd, VkDescriptorSet frameSet, void* user);
        void* user;
    };

    void create(VulkanContext& ctx, const PassCreateInfo& ci);
    void destroy(VulkanContext& ctx);
    void onResize(VulkanContext& ctx, const PassCreateInfo& ci);
    void updateFrameUbo(uint32_t frameIndex, const FrameUbo& frameUbo);
    void record(VulkanContext& ctx, const FrameContext& fc, const FrameUbo& frameUbo,
                const std::vector<RenderObject>& objects,
                const ExtraDrawCall& extraDraw = ExtraDrawCall{});

    const ColorAttachment& color(uint32_t frameIndex, Slot slot) const;
    const DepthTarget& depth(uint32_t frameIndex) const;
    VkRenderPass renderPass() const;
    VkExtent2D extent() const;
    VkFormat depthFormat() const;
    VkDescriptorSetLayout frameSetLayout() const;
    VkDescriptorSet frameSet(uint32_t frameIndex) const;

private:
    struct PerFrameUbo
    {
        VkBuffer buffer = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        void* mapped = nullptr;
    };

    struct FrameResources
    {
        std::array<ColorAttachment, kGBufferCount> g{};
        DepthTarget depth{};
        VkFramebuffer framebuffer = VK_NULL_HANDLE;
    };

    void createPipeline(VulkanContext& ctx);
    void destroyPipeline(VulkanContext& ctx);
    void createFrameResources(VulkanContext& ctx, const PassCreateInfo& ci);
    void destroyFrameResources(VulkanContext& ctx);
    void updateUbo(uint32_t frameIndex, const FrameUbo& frameUbo);

    std::vector<FrameResources> frames_;
    VkRenderPass renderPass_ = VK_NULL_HANDLE;
    VkExtent2D extent_{};
    VkFormat depthFormat_ = VK_FORMAT_UNDEFINED;

    // set 0: per-frame ubo; set 1: material textures.
    VkDescriptorSetLayout setLayout_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout materialSetLayout_ = VK_NULL_HANDLE;
    engine::render::UniquePipelineLayout pipelineLayout_{};
    engine::render::UniquePipeline pipeline_{};
    std::vector<VkDescriptorSet> descSets_;
    std::vector<PerFrameUbo> ubos_;

    std::string vertPath_;
    std::string fragPath_;
};
