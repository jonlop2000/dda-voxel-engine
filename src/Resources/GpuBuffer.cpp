#include "Resources/GpuBuffer.h"

#include "engine/render/Commands.h"
#include "engine/render/Utils.h"
#include "engine/render/VulkanContext.h"

static uint32_t findMemoryType(VkPhysicalDevice gpu, uint32_t typeFilter,
                               VkMemoryPropertyFlags properties)
{
    VkPhysicalDeviceMemoryProperties memProps{};
    vkGetPhysicalDeviceMemoryProperties(gpu, &memProps);

    for (uint32_t i = 0; i < memProps.memoryTypeCount; i++)
    {
        if ((typeFilter & (1u << i)) != 0 &&
            (memProps.memoryTypes[i].propertyFlags & properties) == properties)
        {
            return i;
        }
    }

    die("Failed to find suitable memory type.");
    return 0;
}

void createBuffer(VulkanContext& ctx, VkDeviceSize size, VkBufferUsageFlags usage,
                  VkMemoryPropertyFlags properties, VkBuffer& buffer, VkDeviceMemory& memory)
{
    VkBufferCreateInfo ci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    ci.size = size;
    ci.usage = usage;
    ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(ctx.device, &ci, nullptr, &buffer) != VK_SUCCESS)
    {
        die("vkCreateBuffer failed");
    }

    VkMemoryRequirements memReq{};
    vkGetBufferMemoryRequirements(ctx.device, buffer, &memReq);

    VkMemoryAllocateInfo alloc{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    alloc.allocationSize = memReq.size;
    alloc.memoryTypeIndex = findMemoryType(ctx.gpu, memReq.memoryTypeBits, properties);

    if (vkAllocateMemory(ctx.device, &alloc, nullptr, &memory) != VK_SUCCESS)
    {
        die("vkAllocateMemory failed");
    }

    if (vkBindBufferMemory(ctx.device, buffer, memory, 0) != VK_SUCCESS)
    {
        die("vkBindBufferMemory failed");
    }
}

void copyBuffer(VulkanContext& ctx, Commands& commands, VkBuffer src, VkBuffer dst,
                VkDeviceSize size)
{
    VkCommandBuffer cmd = commands.beginSingleTimeCommands(ctx);
    VkBufferCopy copy{};
    copy.size = size;
    vkCmdCopyBuffer(cmd, src, dst, 1, &copy);
    commands.endSingleTimeCommands(ctx, cmd);
}

