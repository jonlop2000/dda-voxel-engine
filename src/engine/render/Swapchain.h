#pragma once

#include <vector>

#include <vulkan/vulkan.h>

struct VulkanContext;
struct GLFWwindow;

enum class SwapPresentMode
{
    Fifo = 0,
    Mailbox = 1,
    Immediate = 2,
};

struct Swapchain
{
    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    VkFormat imageFormat = VK_FORMAT_UNDEFINED;
    VkExtent2D extent{};
    VkPresentModeKHR presentMode = VK_PRESENT_MODE_FIFO_KHR;
    bool supportsTransferSrc = false;
    std::vector<VkImage> images;
    std::vector<VkImageView> imageViews;
    std::vector<VkFramebuffer> framebuffers;
    VkRenderPass renderPass = VK_NULL_HANDLE;

    void create(VulkanContext& ctx, GLFWwindow* window, SwapPresentMode preferredPresentMode);
    void destroy(VulkanContext& ctx);
    void recreate(VulkanContext& ctx, GLFWwindow* window, SwapPresentMode preferredPresentMode);
};
