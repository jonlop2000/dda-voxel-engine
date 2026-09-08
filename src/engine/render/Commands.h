#pragma once

#include <vector>

#include <vulkan/vulkan.h>

struct VulkanContext;

struct Commands
{
    VkCommandPool pool = VK_NULL_HANDLE;
    std::vector<VkCommandBuffer> buffers;

    void create(VulkanContext& ctx);
    void destroy(VulkanContext& ctx);

    VkCommandBuffer beginSingleTimeCommands(VulkanContext& ctx);
    void endSingleTimeCommands(VulkanContext& ctx, VkCommandBuffer cmd);
};

