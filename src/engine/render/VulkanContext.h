#pragma once

#include <optional>
#include <cstdint>
#include <string>
#include <vector>

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>

#include "engine/render/gpu/DescriptorAllocator.h"
#include "engine/render/gpu/DescriptorLayoutCache.h"
#include "engine/render/gpu/SamplerCache.h"

struct GLFWwindow;

struct QueueFamilyIndices
{
    std::optional<uint32_t> graphics;
    std::optional<uint32_t> present;
    bool complete() const { return graphics.has_value() && present.has_value(); }
};

struct SwapchainSupportDetails
{
    VkSurfaceCapabilitiesKHR capabilities{};
    std::vector<VkSurfaceFormatKHR> formats;
    std::vector<VkPresentModeKHR> presentModes;
};

struct VulkanContext
{
    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice gpu = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VkQueue graphicsQueue = VK_NULL_HANDLE;
    VkQueue presentQueue = VK_NULL_HANDLE;
    VmaAllocator memoryAllocator = VK_NULL_HANDLE;
    QueueFamilyIndices queueFamilies{};
    engine::render::DescriptorAllocator descriptorAllocator{};
    engine::render::DescriptorLayoutCache descriptorLayoutCache{};
    engine::render::SamplerCache samplerCache{};

    void create(GLFWwindow* window);
    void destroy();

    SwapchainSupportDetails querySwapchainSupport(VkPhysicalDevice device) const;

    // helper methods for single-time commands
    bool tryBeginSingleTimeCommands(VkCommandBuffer* outCmd, std::string* outError = nullptr);
    bool tryEndSingleTimeCommands(VkCommandBuffer cmd, std::string* outError = nullptr);
    VkCommandBuffer beginSingleTimeCommands();
    void endSingleTimeCommands(VkCommandBuffer cmd);
    uint32_t findMemoryType(uint32_t typeBits, VkMemoryPropertyFlags props) const;
    void setDebugName(uint64_t handle, VkObjectType type, const char* name) const;

private:
    VkCommandPool singleTimePool_ = VK_NULL_HANDLE;
};
