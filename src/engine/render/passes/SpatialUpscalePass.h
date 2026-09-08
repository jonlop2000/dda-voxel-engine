#pragma once

#include <array>
#include <string>
#include <vector>

#include <vulkan/vulkan.h>

#include "engine/render/RenderResolution.h"
#include "engine/render/RendererConfig.h"
#include "engine/render/gpu/UniqueAllocation.h"
#include "engine/render/gpu/UniqueHandle.h"

struct FrameContext;
struct PassCreateInfo;
struct VulkanContext;

namespace engine::render
{

class SpatialUpscalePass
{
public:
    void setShaderPaths(const std::string& fullscreenVert, const std::string& fragment);
    void create(VulkanContext& context, const PassCreateInfo& createInfo,
                VkExtent2D presentationExtent);
    void destroy(VulkanContext& context);

    void updateInput(VulkanContext& context, uint32_t frameIndex, VkImageView inputView);
    void record(VulkanContext& context, const FrameContext& frame,
                const RenderResolutionSettings& settings);

    [[nodiscard]] VkImageView outputView(uint32_t frameIndex) const;
    [[nodiscard]] VkSampler sampler() const { return sampler_; }
    [[nodiscard]] VkExtent2D inputExtent() const { return inputExtent_; }
    [[nodiscard]] VkExtent2D outputExtent() const { return outputExtent_; }
    [[nodiscard]] bool active() const
    {
        return requiresSpatialUpscale(inputExtent_, outputExtent_);
    }

private:
    struct Target
    {
        UniqueImage image{};
        UniqueImageView view{};
        UniqueFramebuffer framebuffer{};
    };

    void createTargets(VulkanContext& context, const PassCreateInfo& createInfo);
    void createPipeline(VulkanContext& context);

    VkFormat format_ = VK_FORMAT_R16G16B16A16_SFLOAT;
    VkExtent2D inputExtent_{};
    VkExtent2D outputExtent_{};
    UniqueRenderPass renderPass_{};
    VkDescriptorSetLayout descriptorSetLayout_ = VK_NULL_HANDLE;
    UniquePipelineLayout pipelineLayout_{};
    UniquePipeline pipeline_{};
    std::vector<VkDescriptorSet> descriptorSets_{};
    std::array<Target, kMaxFramesInFlight> targets_{};
    VkSampler sampler_ = VK_NULL_HANDLE;
    std::string fullscreenVertPath_{};
    std::string fragmentPath_{};
};

}  // namespace engine::render
