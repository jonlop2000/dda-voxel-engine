#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <vulkan/vulkan.h>

#include "Assets/Texture.h"
#include "engine/render/gpu/UniqueHandle.h"
#include "engine/render/RendererConfig.h"

struct Commands;
struct FrameContext;
struct VulkanContext;

namespace ui
{
struct UiDrawList;
}

struct UiOverlayDrawStats
{
    uint32_t commandCount = 0;
    uint32_t vertexCount = 0;
    uint32_t indexCount = 0;
};

class UiOverlayPass
{
public:
    UiOverlayPass() = default;
    UiOverlayPass(const UiOverlayPass&) = delete;
    UiOverlayPass& operator=(const UiOverlayPass&) = delete;

    void setShaderPaths(const std::string& vertPath, const std::string& fragPath);
    void setFontAtlasImage(CpuImage image, std::string label);
    void create(VulkanContext& ctx, Commands& commands, VkRenderPass renderPass);
    void destroy(VulkanContext& ctx);
    UiOverlayDrawStats record(VulkanContext& ctx, const FrameContext& fc,
                              const ui::UiDrawList& drawList);

private:
    struct FrameBuffers
    {
        VkBuffer vertexBuffer = VK_NULL_HANDLE;
        VkDeviceMemory vertexMemory = VK_NULL_HANDLE;
        VkDeviceSize vertexCapacityBytes = 0;
        VkBuffer indexBuffer = VK_NULL_HANDLE;
        VkDeviceMemory indexMemory = VK_NULL_HANDLE;
        VkDeviceSize indexCapacityBytes = 0;
    };

    void createAtlasResources(VulkanContext& ctx, Commands& commands);
    void destroyAtlasResources(VulkanContext& ctx);
    void createPipeline(VulkanContext& ctx, VkRenderPass renderPass);
    void destroyPipeline(VulkanContext& ctx);
    void ensureFrameBuffers(VulkanContext& ctx, uint32_t frameIndex, VkDeviceSize vertexBytes,
                            VkDeviceSize indexBytes);
    void destroyFrameBuffers(VulkanContext& ctx, FrameBuffers& buffers);

    std::string vertPath_{};
    std::string fragPath_{};
    Texture2D whiteAtlas_{};
    Texture2D fontAtlas_{};
    CpuImage fontAtlasImage_{};
    std::string fontAtlasLabel_{};
    VkDescriptorSetLayout atlasSetLayout_ = VK_NULL_HANDLE;
    std::array<VkDescriptorSet, 2> atlasSets_{};
    engine::render::UniquePipelineLayout pipelineLayout_{};
    engine::render::UniquePipeline pipeline_{};
    std::vector<FrameBuffers> frames_{kMaxFramesInFlight};
};
