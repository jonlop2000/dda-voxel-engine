#pragma once

#include <array>
#include <string>

#include <glm/glm.hpp>
#include <vulkan/vulkan.h>

#include "engine/render/gpu/UniqueAllocation.h"
#include "engine/render/gpu/UniqueHandle.h"

struct VulkanContext;

class AuxiliaryTileListPass
{
  public:
    enum class Consumer : uint32_t
    {
        SunShadow = 0,
        LocalShadow,
        AmbientOcclusion,
        Count,
    };

    struct PushConstants
    {
        glm::ivec2 resolution;
        uint32_t _pad[2];
    };
    static_assert(sizeof(PushConstants) == 16,
                  "AuxiliaryTileListPass::PushConstants must be 16 bytes");

    bool create(VulkanContext& ctx, VkExtent2D extent);
    void destroy(VulkanContext& ctx);
    bool resize(VulkanContext& ctx, VkExtent2D extent);

    void prepareFrame(VulkanContext& ctx, VkImageView depthView, VkSampler depthSampler,
                      Consumer consumer, uint32_t frameIndex);
    void dispatch(VkCommandBuffer cmd, const glm::ivec2& resolution,
                  Consumer consumer, uint32_t frameIndex);

    [[nodiscard]] VkBuffer buffer(uint32_t frameIndex) const
    {
        return frameIndex < kMaxFramesInFlight ? buffers_[frameIndex].get()
                                               : VK_NULL_HANDLE;
    }
    [[nodiscard]] VkDeviceSize bufferSize() const
    {
        return bufferSize_;
    }

  private:
    bool createBuffers(VulkanContext& ctx);
    bool createPipeline(VulkanContext& ctx);
    bool createDescriptorSets(VulkanContext& ctx);
    void destroyBuffers();

    VkExtent2D extent_{};
    engine::render::UniquePipeline pipeline_{};
    engine::render::UniquePipelineLayout pipelineLayout_{};
    VkDescriptorSetLayout descriptorSetLayout_ = VK_NULL_HANDLE;

    static constexpr uint32_t kMaxFramesInFlight = 2;
    static constexpr uint32_t kConsumerCount =
        static_cast<uint32_t>(Consumer::Count);
    static constexpr uint32_t kDescriptorSetCount =
        kMaxFramesInFlight * kConsumerCount;
    VkDescriptorSet descriptorSets_[kDescriptorSetCount] = {};
    std::array<engine::render::UniqueBuffer, kMaxFramesInFlight> buffers_{};
    VkDeviceSize bufferSize_ = 0;

    std::string shaderPath_;

    [[nodiscard]] static constexpr uint32_t descriptorIndex(
        Consumer consumer, uint32_t frameIndex)
    {
        return frameIndex * kConsumerCount + static_cast<uint32_t>(consumer);
    }
};
