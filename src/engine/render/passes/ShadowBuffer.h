#pragma once

#include <glm/glm.hpp>
#include <vulkan/vulkan.h>

#include "engine/render/gpu/UniqueAllocation.h"
#include "engine/render/gpu/UniqueHandle.h"

struct VulkanContext;

/**
 * ShadowBuffer stores per-pixel shadow factors from stochastic ray tracing.
 * current: R8_UNORM (0.0 = fully shadowed, 1.0 = fully lit).
 * history: R16_SFLOAT for temporal accumulation (ping-pong).
 */
class ShadowBuffer
{
  public:
    bool create(VulkanContext& ctx, uint32_t width, uint32_t height);
    void destroy(VulkanContext& ctx);
    void resize(VulkanContext& ctx, uint32_t width, uint32_t height);

    void swapHistory()
    {
        historyIndex_ = 1 - historyIndex_;
    }

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

    VkImage getHistoryWriteImage() const
    {
        return historyImage_[historyIndex_].get();
    }
    VkImageView getHistoryReadView() const
    {
        return historyImageView_[1 - historyIndex_].get();
    }
    VkImageView getHistoryWriteView() const
    {
        return historyStorageView_[historyIndex_].get();
    }
    VkImageView getResolvedView() const
    {
        return historyImageView_[historyIndex_].get();
    }

    VkSampler getSampler() const
    {
        return sampler_.get();
    }

    uint32_t getWidth() const
    {
        return width_;
    }
    uint32_t getHeight() const
    {
        return height_;
    }

  private:
    bool createHistoryImage(VulkanContext& ctx, engine::render::UniqueImage& image,
                            engine::render::UniqueImageView& view,
                            engine::render::UniqueImageView& storageView, const char* name);

    uint32_t width_ = 0;
    uint32_t height_ = 0;
    int historyIndex_ = 0;

    engine::render::UniqueImage currentImage_{};
    engine::render::UniqueImageView currentImageView_{};   // for sampling (raw debug)
    engine::render::UniqueImageView currentStorageView_{}; // for shadow ray writes

    engine::render::UniqueImage historyImage_[2]{};
    engine::render::UniqueImageView historyImageView_[2]{};
    engine::render::UniqueImageView historyStorageView_[2]{};

    engine::render::UniqueSampler sampler_{};
};
