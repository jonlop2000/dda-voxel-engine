#pragma once

#include <filesystem>

#include <vulkan/vulkan.h>

#include "engine/render/gpu/UniqueAllocation.h"
#include "engine/render/gpu/UniqueHandle.h"

struct VulkanContext;

namespace engine
{

class VoxelMaterialAtlas
{
public:
    bool create(VulkanContext& ctx, const std::filesystem::path& assetPath = {});
    void destroy(VulkanContext& ctx);

    bool isValid() const
    {
        return image_ && imageView_.get() != VK_NULL_HANDLE &&
               sampler_.get() != VK_NULL_HANDLE;
    }

    VkDescriptorImageInfo descriptorInfo() const
    {
        VkDescriptorImageInfo info{};
        info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        info.imageView = imageView_.get();
        info.sampler = sampler_.get();
        return info;
    }

private:
    engine::render::UniqueImage image_{};
    engine::render::UniqueImageView imageView_{};
    engine::render::UniqueSampler sampler_{};
};

} // namespace engine
