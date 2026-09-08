#include "engine/render/Commands.h"

#include "Core/Logger.h"
#include "engine/render/RendererConfig.h"
#include "engine/render/Utils.h"
#include "engine/render/VulkanContext.h"

void Commands::create(VulkanContext& ctx)
{
    VkCommandPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = ctx.queueFamilies.graphics.value();

    if (vkCreateCommandPool(ctx.device, &poolInfo, nullptr, &this->pool) != VK_SUCCESS)
    {
        logAndExit("Vulkan", "vkCreateCommandPool failed");
    }

    buffers.resize(kMaxFramesInFlight);
    VkCommandBufferAllocateInfo alloc{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    alloc.commandPool = this->pool;
    alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc.commandBufferCount = static_cast<uint32_t>(buffers.size());

    if (vkAllocateCommandBuffers(ctx.device, &alloc, buffers.data()) != VK_SUCCESS)
    {
        logAndExit("Vulkan", "vkAllocateCommandBuffers failed");
    }
}

void Commands::destroy(VulkanContext& ctx)
{
    if (pool != VK_NULL_HANDLE)
    {
        vkDestroyCommandPool(ctx.device, pool, nullptr);
        pool = VK_NULL_HANDLE;
    }
    buffers.clear();
}

VkCommandBuffer Commands::beginSingleTimeCommands(VulkanContext& ctx)
{
    VkCommandBufferAllocateInfo alloc{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc.commandPool = pool;
    alloc.commandBufferCount = 1;

    VkCommandBuffer cmd = VK_NULL_HANDLE;
    if (vkAllocateCommandBuffers(ctx.device, &alloc, &cmd) != VK_SUCCESS)
    {
        logAndExit("Vulkan", "vkAllocateCommandBuffers failed");
    }

    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (vkBeginCommandBuffer(cmd, &begin) != VK_SUCCESS)
    {
        logAndExit("Vulkan", "vkBeginCommandBuffer failed");
    }

    return cmd;
}

void Commands::endSingleTimeCommands(VulkanContext& ctx, VkCommandBuffer cmd)
{
    if (vkEndCommandBuffer(cmd) != VK_SUCCESS)
    {
        logAndExit("Vulkan", "vkEndCommandBuffer failed");
    }

    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &cmd;

    if (vkQueueSubmit(ctx.graphicsQueue, 1, &submit, VK_NULL_HANDLE) != VK_SUCCESS)
    {
        logAndExit("Vulkan", "vkQueueSubmit failed");
    }
    vkQueueWaitIdle(ctx.graphicsQueue);

    vkFreeCommandBuffers(ctx.device, pool, 1, &cmd);
}
