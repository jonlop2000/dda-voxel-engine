#include "engine/render/Swapchain.h"

#include <algorithm>
#include <limits>

#include <GLFW/glfw3.h>

#include "engine/render/Utils.h"
#include "engine/render/VulkanContext.h"

static VkSurfaceFormatKHR chooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& formats)
{
    for (const auto& available : formats)
    {
        if (available.format == VK_FORMAT_B8G8R8A8_SRGB &&
            available.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
        {
            return available;
        }
    }

    return formats[0];
}

static VkPresentModeKHR toVkPresentMode(SwapPresentMode mode)
{
    switch (mode)
    {
    case SwapPresentMode::Mailbox:
        return VK_PRESENT_MODE_MAILBOX_KHR;
    case SwapPresentMode::Immediate:
        return VK_PRESENT_MODE_IMMEDIATE_KHR;
    case SwapPresentMode::Fifo:
    default:
        return VK_PRESENT_MODE_FIFO_KHR;
    }
}

static VkPresentModeKHR chooseSwapPresentMode(const std::vector<VkPresentModeKHR>& presentModes,
                                              SwapPresentMode preferredPresentMode)
{
    const VkPresentModeKHR requestedMode = toVkPresentMode(preferredPresentMode);
    for (const auto& mode : presentModes)
    {
        if (mode == requestedMode)
        {
            return mode;
        }
    }

    for (const auto& mode : presentModes)
    {
        if (mode == VK_PRESENT_MODE_FIFO_KHR)
        {
            return mode;
        }
    }

    return presentModes.empty() ? VK_PRESENT_MODE_FIFO_KHR : presentModes.front();
}

static VkExtent2D chooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities, GLFWwindow* window)
{
    if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max())
    {
        return capabilities.currentExtent;
    }

    int width = 0;
    int height = 0;
    glfwGetFramebufferSize(window, &width, &height);

    VkExtent2D actual{
        static_cast<uint32_t>(width),
        static_cast<uint32_t>(height),
    };

    actual.width = std::clamp(actual.width, capabilities.minImageExtent.width,
                              capabilities.maxImageExtent.width);
    actual.height = std::clamp(actual.height, capabilities.minImageExtent.height,
                               capabilities.maxImageExtent.height);

    return actual;
}

static void createRenderPass(VulkanContext& ctx, Swapchain& swapchain)
{
    VkAttachmentDescription colorAttachment{};
    colorAttachment.format = swapchain.imageFormat;
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference colorRef{};
    colorRef.attachment = 0;
    colorRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorRef;

    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.srcAccessMask = 0;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo rp{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    rp.attachmentCount = 1;
    rp.pAttachments = &colorAttachment;
    rp.subpassCount = 1;
    rp.pSubpasses = &subpass;
    rp.dependencyCount = 1;
    rp.pDependencies = &dependency;

    if (vkCreateRenderPass(ctx.device, &rp, nullptr, &swapchain.renderPass) != VK_SUCCESS)
    {
        die("vkCreateRenderPass failed");
    }
}

static void createFramebuffers(VulkanContext& ctx, Swapchain& swapchain)
{
    swapchain.framebuffers.resize(swapchain.imageViews.size());
    for (size_t i = 0; i < swapchain.imageViews.size(); i++)
    {
        VkImageView attachments[] = {swapchain.imageViews[i]};

        VkFramebufferCreateInfo fb{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
        fb.renderPass = swapchain.renderPass;
        fb.attachmentCount = 1;
        fb.pAttachments = attachments;
        fb.width = swapchain.extent.width;
        fb.height = swapchain.extent.height;
        fb.layers = 1;

        if (vkCreateFramebuffer(ctx.device, &fb, nullptr, &swapchain.framebuffers[i]) !=
            VK_SUCCESS)
        {
            die("vkCreateFramebuffer failed");
        }
    }
}

void Swapchain::create(VulkanContext& ctx, GLFWwindow* window,
                       SwapPresentMode preferredPresentMode)
{
    SwapchainSupportDetails support = ctx.querySwapchainSupport(ctx.gpu);
    VkSurfaceFormatKHR surfaceFormat = chooseSwapSurfaceFormat(support.formats);
    VkPresentModeKHR swapPresentMode =
        chooseSwapPresentMode(support.presentModes, preferredPresentMode);
    VkExtent2D swapExtent = chooseSwapExtent(support.capabilities, window);

    uint32_t imageCount = support.capabilities.minImageCount + 1;
    if (support.capabilities.maxImageCount > 0 && imageCount > support.capabilities.maxImageCount)
    {
        imageCount = support.capabilities.maxImageCount;
    }

    VkSwapchainCreateInfoKHR ci{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
    ci.surface = ctx.surface;
    ci.minImageCount = imageCount;
    ci.imageFormat = surfaceFormat.format;
    ci.imageColorSpace = surfaceFormat.colorSpace;
    ci.imageExtent = swapExtent;
    ci.imageArrayLayers = 1;
    supportsTransferSrc =
        (support.capabilities.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) != 0;
    ci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    if (supportsTransferSrc)
    {
        ci.imageUsage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    }

    uint32_t queueFamilyIndices[] = {
        ctx.queueFamilies.graphics.value(),
        ctx.queueFamilies.present.value(),
    };

    if (ctx.queueFamilies.graphics.value() == ctx.queueFamilies.present.value())
    {
        ci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }
    else
    {
        ci.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        ci.queueFamilyIndexCount = 2;
        ci.pQueueFamilyIndices = queueFamilyIndices;
    }

    ci.preTransform = support.capabilities.currentTransform;
    ci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    ci.presentMode = swapPresentMode;
    ci.clipped = VK_TRUE;
    ci.oldSwapchain = VK_NULL_HANDLE;

    if (vkCreateSwapchainKHR(ctx.device, &ci, nullptr, &swapchain) != VK_SUCCESS)
    {
        die("vkCreateSwapchainKHR failed");
    }

    imageFormat = surfaceFormat.format;
    extent = swapExtent;
    presentMode = swapPresentMode;

    uint32_t actualCount = 0;
    vkGetSwapchainImagesKHR(ctx.device, swapchain, &actualCount, nullptr);
    images.resize(actualCount);
    vkGetSwapchainImagesKHR(ctx.device, swapchain, &actualCount, images.data());

    imageViews.resize(images.size());
    for (size_t i = 0; i < images.size(); i++)
    {
        VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        view.image = images[i];
        view.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view.format = imageFormat;
        view.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
        view.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
        view.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
        view.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
        view.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        view.subresourceRange.baseMipLevel = 0;
        view.subresourceRange.levelCount = 1;
        view.subresourceRange.baseArrayLayer = 0;
        view.subresourceRange.layerCount = 1;

        if (vkCreateImageView(ctx.device, &view, nullptr, &imageViews[i]) != VK_SUCCESS)
        {
            die("vkCreateImageView failed");
        }
    }

    createRenderPass(ctx, *this);
    createFramebuffers(ctx, *this);
}

void Swapchain::destroy(VulkanContext& ctx)
{
    for (VkFramebuffer fb : framebuffers)
    {
        vkDestroyFramebuffer(ctx.device, fb, nullptr);
    }
    framebuffers.clear();

    if (renderPass != VK_NULL_HANDLE)
    {
        vkDestroyRenderPass(ctx.device, renderPass, nullptr);
        renderPass = VK_NULL_HANDLE;
    }

    for (VkImageView view : imageViews)
    {
        vkDestroyImageView(ctx.device, view, nullptr);
    }
    imageViews.clear();
    images.clear();

    if (swapchain != VK_NULL_HANDLE)
    {
        vkDestroySwapchainKHR(ctx.device, swapchain, nullptr);
        swapchain = VK_NULL_HANDLE;
    }
    supportsTransferSrc = false;
}

void Swapchain::recreate(VulkanContext& ctx, GLFWwindow* window,
                         SwapPresentMode preferredPresentMode)
{
    destroy(ctx);
    create(ctx, window, preferredPresentMode);
}
