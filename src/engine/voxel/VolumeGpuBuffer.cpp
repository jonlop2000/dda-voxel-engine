#include "engine/voxel/VolumeGpuBuffer.h"

#include <cstring>

#include "engine/render/Utils.h"
#include "engine/render/VulkanContext.h"
#include "Resources/GpuBuffer.h"
#include "engine/voxel/VoxelVolumeGpu.h"

bool VolumeGpuBuffer::create(VulkanContext& ctx, size_t maxVolumes)
{
    if (maxVolumes == 0)
    {
        return false;
    }

    m_capacityVolumes = maxVolumes;
    const VkDeviceSize bytes =
        static_cast<VkDeviceSize>(m_capacityVolumes) * static_cast<VkDeviceSize>(sizeof(VolumeGpu));
    m_capacityBytes = static_cast<size_t>(bytes);

    createBuffer(ctx, bytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                 m_buffer, m_memory);

    if (vkMapMemory(ctx.device, m_memory, 0, bytes, 0, &m_mapped) != VK_SUCCESS)
    {
        destroy(ctx);
        return false;
    }

    return true;
}

void VolumeGpuBuffer::destroy(VulkanContext& ctx)
{
    if (m_mapped != nullptr)
    {
        vkUnmapMemory(ctx.device, m_memory);
        m_mapped = nullptr;
    }
    if (m_buffer != VK_NULL_HANDLE)
    {
        vkDestroyBuffer(ctx.device, m_buffer, nullptr);
        m_buffer = VK_NULL_HANDLE;
    }
    if (m_memory != VK_NULL_HANDLE)
    {
        vkFreeMemory(ctx.device, m_memory, nullptr);
        m_memory = VK_NULL_HANDLE;
    }
    m_capacityVolumes = 0;
    m_capacityBytes = 0;
}

void VolumeGpuBuffer::upload(const void* data, size_t bytes)
{
    if (m_mapped == nullptr || data == nullptr || bytes == 0)
    {
        return;
    }
    if (bytes > m_capacityBytes)
    {
        return;
    }

    std::memcpy(m_mapped, data, bytes);
}
