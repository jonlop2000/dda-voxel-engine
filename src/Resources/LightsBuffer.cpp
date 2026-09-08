#include "Resources/LightsBuffer.h"

#include "engine/render/Utils.h"
#include "engine/render/VulkanContext.h"
#include "Resources/GpuBuffer.h"

void createLightsBuffer(VulkanContext& ctx, uint32_t maxLights, LightsBuffer& out)
{
    out.maxLights = maxLights;
    const VkDeviceSize size = sizeof(AreaLightGpu) * static_cast<VkDeviceSize>(maxLights);

    createBuffer(ctx, size, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                 out.buffer, out.memory);

    if (vkMapMemory(ctx.device, out.memory, 0, size, 0, &out.mapped) != VK_SUCCESS)
    {
        die("vkMapMemory (lights buffer) failed");
    }
}

void destroyLightsBuffer(VulkanContext& ctx, LightsBuffer& out)
{
    if (out.mapped != nullptr)
    {
        vkUnmapMemory(ctx.device, out.memory);
        out.mapped = nullptr;
    }
    if (out.buffer != VK_NULL_HANDLE)
    {
        vkDestroyBuffer(ctx.device, out.buffer, nullptr);
    }
    if (out.memory != VK_NULL_HANDLE)
    {
        vkFreeMemory(ctx.device, out.memory, nullptr);
    }
    out = LightsBuffer{};
}
