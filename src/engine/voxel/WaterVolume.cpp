#include "engine/voxel/WaterVolume.h"

#include <algorithm>
#include <array>
#include <cstring>

#include "engine/render/VulkanContext.h"
#include "Resources/GpuBuffer.h"

namespace engine
{

bool WaterVolumeManager::init(VulkanContext& ctx)
{
    if (buffer_ != VK_NULL_HANDLE)
    {
        return true;
    }

    capacityBytes_ = sizeof(WaterVolumeGpu) * static_cast<size_t>(kMaxWaterVolumes);
    createBuffer(ctx, static_cast<VkDeviceSize>(capacityBytes_), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                 buffer_, memory_);

    if (vkMapMemory(ctx.device, memory_, 0, static_cast<VkDeviceSize>(capacityBytes_), 0,
                    &mapped_) != VK_SUCCESS)
    {
        shutdown(ctx);
        return false;
    }

    std::memset(mapped_, 0, capacityBytes_);
    dirty_ = false;
    return true;
}

void WaterVolumeManager::shutdown(VulkanContext& ctx)
{
    if (mapped_ != nullptr)
    {
        vkUnmapMemory(ctx.device, memory_);
        mapped_ = nullptr;
    }
    if (buffer_ != VK_NULL_HANDLE)
    {
        vkDestroyBuffer(ctx.device, buffer_, nullptr);
        buffer_ = VK_NULL_HANDLE;
    }
    if (memory_ != VK_NULL_HANDLE)
    {
        vkFreeMemory(ctx.device, memory_, nullptr);
        memory_ = VK_NULL_HANDLE;
    }
    capacityBytes_ = 0;
    volumes_.clear();
    dirty_ = false;
}

void WaterVolumeManager::setVolumes(const std::vector<WaterVolume>& volumes)
{
    volumes_ = volumes;
    if (volumes_.size() > kMaxWaterVolumes)
    {
        volumes_.resize(kMaxWaterVolumes);
    }
    dirty_ = true;
}

void WaterVolumeManager::upload()
{
    if (!dirty_ || mapped_ == nullptr || capacityBytes_ == 0)
    {
        return;
    }

    std::array<WaterVolumeGpu, kMaxWaterVolumes> gpu{};
    gpu.fill(WaterVolumeGpu{});

    const size_t countLocal = std::min(volumes_.size(), static_cast<size_t>(kMaxWaterVolumes));
    for (size_t i = 0; i < countLocal; ++i)
    {
        const WaterVolume& src = volumes_[i];
        WaterVolumeGpu& dst = gpu[i];
        dst.boundsMin = glm::vec4(src.boundsMin, src.surfaceHeight);
        dst.boundsMax = glm::vec4(src.boundsMax, src.fogDensity);
        dst.absorptionCoeff =
            glm::vec4(src.absorptionCoeff, static_cast<float>(src.shape));
        dst.deepColor = glm::vec4(src.deepColor, static_cast<float>(src.flags));
    }

    const size_t bytes = sizeof(WaterVolumeGpu) * gpu.size();
    std::memcpy(mapped_, gpu.data(), std::min(bytes, capacityBytes_));
    dirty_ = false;
}

uint32_t WaterVolumeManager::count() const
{
    return static_cast<uint32_t>(
        std::min(volumes_.size(), static_cast<size_t>(kMaxWaterVolumes)));
}

} // namespace engine
