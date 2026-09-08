#include "engine/render/voxel/TerrainShadowColumnGpuBuffer.h"

#include <cstring>
#include <limits>
#include <utility>

#include "engine/render/VulkanContext.h"

namespace engine::render
{

bool TerrainShadowColumnGpuBuffer::upload(
    VulkanContext& context, std::span<const uint32_t> topYExclusive)
{
    if (topYExclusive.empty() ||
        topYExclusive.size() >
            std::numeric_limits<VkDeviceSize>::max() / sizeof(uint32_t))
    {
        return false;
    }

    const VkDeviceSize requiredBytes =
        static_cast<VkDeviceSize>(topYExclusive.size()) * sizeof(uint32_t);
    UniqueBuffer replacement{};
    UniqueBuffer* destination = &buffer_;
    if (!buffer_ || sizeBytes_ != requiredBytes)
    {
        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = requiredBytes;
        bufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo allocationInfo{};
        allocationInfo.flags =
            VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
        allocationInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
        allocationInfo.requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
        if (createUniqueBuffer(context.memoryAllocator, bufferInfo, allocationInfo,
                               replacement,
                               "terrain_shadow_column_heights") != VK_SUCCESS)
        {
            return false;
        }
        destination = &replacement;
    }

    void* mapped = nullptr;
    if (vmaMapMemory(context.memoryAllocator, destination->allocation(), &mapped) !=
        VK_SUCCESS)
    {
        return false;
    }
    std::memcpy(mapped, topYExclusive.data(),
                static_cast<size_t>(requiredBytes));
    const VkResult flushResult = vmaFlushAllocation(
        context.memoryAllocator, destination->allocation(), 0, requiredBytes);
    vmaUnmapMemory(context.memoryAllocator, destination->allocation());
    if (flushResult != VK_SUCCESS)
    {
        return false;
    }

    if (replacement)
    {
        buffer_ = std::move(replacement);
        sizeBytes_ = requiredBytes;
    }
    return true;
}

void TerrainShadowColumnGpuBuffer::reset()
{
    buffer_.reset();
    sizeBytes_ = 0;
}

}  // namespace engine::render
