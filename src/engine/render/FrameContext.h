#pragma once

#include <cstdint>

#include <vulkan/vulkan.h>

namespace engine::scene
{
struct WorldStateView;
}

struct FrameContext
{
    uint32_t frameIndex = 0;
    uint32_t swapImageIndex = 0;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    VkExtent2D extent{};
    const engine::scene::WorldStateView* worldState = nullptr;
};
