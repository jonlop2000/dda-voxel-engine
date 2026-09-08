#pragma once

#include <array>
#include <vector>

#include <vulkan/vulkan.h>

#include "engine/render/RendererConfig.h"

struct VulkanContext;
struct Swapchain;

struct FrameSync
{
    uint32_t currentFrame = 0;
    std::array<VkSemaphore, kMaxFramesInFlight> imageAvailable{};
    std::array<VkFence, kMaxFramesInFlight> inFlightFences{};
    std::vector<VkFence> imagesInFlight;
    std::vector<VkSemaphore> renderFinishedPerImage;
};

void createSyncObjects(VulkanContext& ctx, const Swapchain& swapchain, FrameSync& sync);
void destroySyncObjects(VulkanContext& ctx, FrameSync& sync);

