#include "engine/render/FrameSync.h"

#include "engine/render/Swapchain.h"
#include "engine/render/Utils.h"
#include "engine/render/VulkanContext.h"

void createSyncObjects(VulkanContext& ctx, const Swapchain& swapchain, FrameSync& sync)
{
    sync.imagesInFlight.assign(swapchain.images.size(), VK_NULL_HANDLE);
    sync.renderFinishedPerImage.assign(swapchain.images.size(), VK_NULL_HANDLE);

    VkSemaphoreCreateInfo sem{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    VkFenceCreateInfo fence{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    fence.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (size_t i = 0; i < sync.renderFinishedPerImage.size(); i++)
    {
        if (vkCreateSemaphore(ctx.device, &sem, nullptr, &sync.renderFinishedPerImage[i]) !=
            VK_SUCCESS)
        {
            die("vkCreateSemaphore failed");
        }
    }

    for (size_t i = 0; i < kMaxFramesInFlight; i++)
    {
        if (vkCreateSemaphore(ctx.device, &sem, nullptr, &sync.imageAvailable[i]) != VK_SUCCESS)
        {
            die("vkCreateSemaphore failed");
        }
        if (vkCreateFence(ctx.device, &fence, nullptr, &sync.inFlightFences[i]) != VK_SUCCESS)
        {
            die("vkCreateFence failed");
        }
    }
}

void destroySyncObjects(VulkanContext& ctx, FrameSync& sync)
{
    for (VkSemaphore sem : sync.renderFinishedPerImage)
    {
        if (sem != VK_NULL_HANDLE)
        {
            vkDestroySemaphore(ctx.device, sem, nullptr);
        }
    }
    sync.renderFinishedPerImage.clear();

    for (size_t i = 0; i < kMaxFramesInFlight; i++)
    {
        if (sync.imageAvailable[i] != VK_NULL_HANDLE)
        {
            vkDestroySemaphore(ctx.device, sync.imageAvailable[i], nullptr);
            sync.imageAvailable[i] = VK_NULL_HANDLE;
        }
        if (sync.inFlightFences[i] != VK_NULL_HANDLE)
        {
            vkDestroyFence(ctx.device, sync.inFlightFences[i], nullptr);
            sync.inFlightFences[i] = VK_NULL_HANDLE;
        }
    }

    sync.imagesInFlight.clear();
}

