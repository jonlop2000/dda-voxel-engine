#pragma once

#include <filesystem>
#include <vector>
#include <vulkan/vulkan.h>

#include "engine/render/gpu/UniqueAllocation.h"
#include "engine/render/gpu/UniqueHandle.h"

struct VulkanContext;

/**
 * 128x128 blue noise texture for stochastic sampling.
 * RG8_UNORM format - two channels of uncorrelated noise.
 */
class BlueNoiseTexture
{
public:
    static constexpr int kSize = 128;

    bool create(VulkanContext& ctx, const std::filesystem::path& filePath);
    void destroy(VulkanContext& ctx);

    VkImageView getImageView() const { return imageView_.get(); }
    VkSampler getSampler() const { return sampler_.get(); }

private:
    void generateFallbackNoise(uint8_t* data, int width, int height);
    bool loadFromFile(const std::filesystem::path& path, std::vector<uint8_t>& data);

    engine::render::UniqueImage image_{};
    engine::render::UniqueImageView imageView_{};
    engine::render::UniqueSampler sampler_{};
};
