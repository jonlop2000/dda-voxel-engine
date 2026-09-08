#pragma once

#include <string>

#include <glm/glm.hpp>
#include <vulkan/vulkan.h>

#include "engine/render/gpu/UniqueHandle.h"

struct VulkanContext;

class LocalShadowBlur
{
public:
    bool create(VulkanContext& ctx, VkExtent2D extent);
    void destroy(VulkanContext& ctx);
    void resize(VulkanContext& ctx, VkExtent2D extent);

    void dispatch(VkCommandBuffer cmd, VulkanContext& ctx,
                  VkImageView inputView, VkSampler inputSampler,
                  VkImageView outputStorageView,
                  VkExtent2D activeExtent, float sigma, uint32_t frameIndex);

private:
    struct PushConstants
    {
        glm::ivec2 resolution;
        float sigma;
        float _pad;
    };

    bool createPipeline(VulkanContext& ctx);
    bool createDescriptorSets(VulkanContext& ctx);
    void updateDescriptorSet(VulkanContext& ctx, uint32_t frameIndex,
                             VkImageView inputView, VkSampler inputSampler,
                             VkImageView outputStorageView);

    VkExtent2D extent_{};
    engine::render::UniquePipeline pipeline_{};
    engine::render::UniquePipelineLayout pipelineLayout_{};
    VkDescriptorSetLayout descriptorSetLayout_ = VK_NULL_HANDLE;

    static constexpr uint32_t kMaxFramesInFlight = 2;
    VkDescriptorSet descriptorSets_[kMaxFramesInFlight] = {};

    std::string shaderPath_;
};
