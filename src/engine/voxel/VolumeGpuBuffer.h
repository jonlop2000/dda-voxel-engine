#pragma once

#include <cstddef>

#include <vulkan/vulkan.h>

struct VulkanContext;

class VolumeGpuBuffer
{
public:
    bool create(VulkanContext& ctx, size_t maxVolumes);
    void destroy(VulkanContext& ctx);

    void upload(const void* data, size_t bytes);

    VkBuffer buffer() const { return m_buffer; }
    VkDeviceSize sizeBytes() const { return static_cast<VkDeviceSize>(m_capacityBytes); }
    size_t maxVolumes() const { return m_capacityVolumes; }
    void* mapped() const { return m_mapped; }

private:
    VkBuffer m_buffer = VK_NULL_HANDLE;
    VkDeviceMemory m_memory = VK_NULL_HANDLE;
    void* m_mapped = nullptr;
    size_t m_capacityVolumes = 0;
    size_t m_capacityBytes = 0;
};
