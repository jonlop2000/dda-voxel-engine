#pragma once

#include <vulkan/vulkan.h>

struct VulkanContext;
struct Commands;

void createBuffer(VulkanContext& ctx, VkDeviceSize size, VkBufferUsageFlags usage,
                  VkMemoryPropertyFlags properties, VkBuffer& buffer, VkDeviceMemory& memory);
void copyBuffer(VulkanContext& ctx, Commands& commands, VkBuffer src, VkBuffer dst,
                VkDeviceSize size);

