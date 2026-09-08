#pragma once

#include <vector>

#include <vulkan/vulkan.h>

struct VulkanContext;
struct Commands;

VkFormat findSupportedDepthFormat(VulkanContext& ctx);
void createImage(VulkanContext& ctx, uint32_t width, uint32_t height, VkFormat format,
                 VkImageUsageFlags usage, VkImage& image, VkDeviceMemory& memory);
VkImageView createImageView(VkDevice device, VkImage image, VkFormat format,
                            VkImageAspectFlags aspectMask);
void transitionImageLayout(VulkanContext& ctx, Commands& commands, VkImage image,
                           VkImageAspectFlags aspectMask, VkImageLayout oldLayout,
                           VkImageLayout newLayout);
void copyBufferToImage(VulkanContext& ctx, Commands& commands, VkBuffer buffer, VkImage image,
                       uint32_t width, uint32_t height);
