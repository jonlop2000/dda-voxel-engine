#pragma once

#include <string>

#include <glm/glm.hpp>
#include <vulkan/vulkan.h>

#include "engine/render/gpu/UniqueAllocation.h"
#include "engine/render/gpu/UniqueHandle.h"

struct VulkanContext;

class ShadowDenoisePass
{
  public:
    bool create(VulkanContext& ctx, VkExtent2D extent);
    void destroy(VulkanContext& ctx);
    bool resize(VulkanContext& ctx, VkExtent2D extent);

    void dispatch(VkCommandBuffer cmd, VulkanContext& ctx, VkImageView inputView,
                  VkSampler inputSampler, VkImageView depthView, VkSampler depthSampler,
                  VkImageView normalView, VkSampler normalSampler, uint32_t radius,
                  float depthSigma, float valueSigma, float normalPower, uint32_t frameIndex);

    VkImageView outputView() const
    {
        return outputView_.get();
    }
    VkImage outputImage() const
    {
        return outputImage_.get();
    }
    VkSampler sampler() const
    {
        return sampler_;
    }

  private:
    struct PushConstants
    {
        glm::ivec2 resolution;
        float depthSigma;
        float valueSigma;
        float normalPower;
        uint32_t radius;
        float _pad[3];
    };

    bool createPipeline(VulkanContext& ctx);
    bool createDescriptorSets(VulkanContext& ctx);
    bool createOutputResources(VulkanContext& ctx);
    void destroyOutputResources(VulkanContext& ctx);
    void updateDescriptorSet(VulkanContext& ctx, uint32_t frameIndex, VkImageView inputView,
                             VkSampler inputSampler, VkImageView depthView, VkSampler depthSampler,
                             VkImageView normalView, VkSampler normalSampler);

    VkExtent2D extent_{};
    engine::render::UniquePipeline pipeline_{};
    engine::render::UniquePipelineLayout pipelineLayout_{};
    VkDescriptorSetLayout descriptorSetLayout_ = VK_NULL_HANDLE;

    static constexpr uint32_t kMaxFramesInFlight = 2;
    VkDescriptorSet descriptorSets_[kMaxFramesInFlight] = {};

    engine::render::UniqueImage outputImage_{};
    engine::render::UniqueImageView outputView_{};
    VkSampler sampler_ = VK_NULL_HANDLE;

    std::string shaderPath_;
};
