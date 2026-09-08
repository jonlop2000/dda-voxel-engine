#pragma once

#include <vulkan/vulkan.h>

#include "engine/render/gpu/UniqueAllocation.h"
#include "engine/render/gpu/UniqueHandle.h"

struct VulkanContext;

class LocalLightShadowBuffer
{
  public:
    bool create(VulkanContext& ctx, uint32_t width, uint32_t height);
    void destroy(VulkanContext& ctx);
    void resize(VulkanContext& ctx, uint32_t width, uint32_t height);

    VkImage getCurrentImage() const
    {
        return currentImage_.get();
    }
    VkImageView getCurrentImageView() const
    {
        return currentImageView_.get();
    }
    VkImageView getCurrentStorageView() const
    {
        return currentStorageView_.get();
    }
    VkSampler getSampler() const
    {
        return sampler_.get();
    }

    VkImage getBlurredImage() const
    {
        return blurredImage_.get();
    }
    VkImageView getBlurredImageView() const
    {
        return blurredImageView_.get();
    }
    VkImageView getBlurredStorageView() const
    {
        return blurredStorageView_.get();
    }

    uint32_t width() const
    {
        return width_;
    }
    uint32_t height() const
    {
        return height_;
    }

  private:
    uint32_t width_ = 0;
    uint32_t height_ = 0;

    engine::render::UniqueImage currentImage_{};
    engine::render::UniqueImageView currentImageView_{};
    engine::render::UniqueImageView currentStorageView_{};

    engine::render::UniqueImage blurredImage_{};
    engine::render::UniqueImageView blurredImageView_{};
    engine::render::UniqueImageView blurredStorageView_{};

    engine::render::UniqueSampler sampler_{};
};
