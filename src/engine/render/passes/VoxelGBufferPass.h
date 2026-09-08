#pragma once

#include <array>
#include <vector>

#include <vulkan/vulkan.h>

#include "engine/render/gpu/UniqueHandle.h"
#include "engine/render/passes/GBufferPass.h"

struct VulkanContext;
struct FrameContext;
struct PassCreateInfo;

struct VoxelGBufferPass
{
    struct ExtraDrawCall
    {
        void (*fn)(VkCommandBuffer cmd, VkDescriptorSet frameSet, void* user) = nullptr;
        void* user = nullptr;
    };

    void create(VulkanContext& ctx, const PassCreateInfo& ci, const GBufferPass& gbuffer);
    void destroy(VulkanContext& ctx);
    void onResize(VulkanContext& ctx, const PassCreateInfo& ci, const GBufferPass& gbuffer);

    void record(VulkanContext& ctx, const FrameContext& fc, VkDescriptorSet frameSet,
                const ExtraDrawCall& extraDraw);
    VkRenderPass renderPass() const { return renderPass_.get(); }

private:
    struct FrameResources
    {
        engine::render::UniqueFramebuffer framebuffer{};
    };

    void createRenderPass(VulkanContext& ctx, VkFormat depthFormat,
                          const std::array<VkFormat, GBufferPass::kGBufferCount>& colorFormats);
    void destroyRenderPass(VulkanContext& ctx);
    void createFramebuffers(VulkanContext& ctx, const GBufferPass& gbuffer);
    void destroyFramebuffers(VulkanContext& ctx);

    std::vector<FrameResources> frames_{};
    engine::render::UniqueRenderPass renderPass_{};
    VkExtent2D extent_{};
};
