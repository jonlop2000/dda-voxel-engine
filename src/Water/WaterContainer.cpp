#include "Water/WaterContainer.h"

#include <algorithm>
#include <array>
#include <cstring>

#include "engine/render/VulkanContext.h"
#include "Resources/GpuBuffer.h"

namespace engine
{

bool WaterContainerManager::init(VulkanContext& ctx)
{
    if (buffer_ != VK_NULL_HANDLE)
    {
        return true;
    }

    capacityBytes_ =
        sizeof(WaterContainerGpu) * static_cast<size_t>(kMaxWaterContainers);
    createBuffer(ctx, static_cast<VkDeviceSize>(capacityBytes_),
                 VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                     VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
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

void WaterContainerManager::shutdown(VulkanContext& ctx)
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
    containers_.clear();
    dirty_ = false;
}

void WaterContainerManager::setContainers(const std::vector<WaterContainer>& containers)
{
    containers_ = containers;
    if (containers_.size() > kMaxWaterContainers)
    {
        containers_.resize(kMaxWaterContainers);
    }
    dirty_ = true;
}

void WaterContainerManager::upload()
{
    if (!dirty_ || mapped_ == nullptr || capacityBytes_ == 0)
    {
        return;
    }

    std::array<WaterContainerGpu, kMaxWaterContainers> gpu{};
    gpu.fill(WaterContainerGpu{});

    const size_t countLocal =
        std::min(containers_.size(), static_cast<size_t>(kMaxWaterContainers));
    for (size_t i = 0; i < countLocal; ++i)
    {
        const WaterContainer& src = containers_[i];
        WaterContainerGpu& dst = gpu[i];
        dst.boundsMin_fillHeight = glm::vec4(src.boundsMin, src.surfaceHeight);
        dst.boundsMax_fogDensity = glm::vec4(src.boundsMax, src.fogDensity);
        dst.absorption_shape =
            glm::vec4(src.absorptionCoeff, static_cast<float>(src.shape));
        dst.deepColor_flags = glm::vec4(src.deepColor, static_cast<float>(src.flags));
    }

    const size_t bytes = sizeof(WaterContainerGpu) * gpu.size();
    std::memcpy(mapped_, gpu.data(), std::min(bytes, capacityBytes_));
    dirty_ = false;
}

uint32_t WaterContainerManager::count() const
{
    return static_cast<uint32_t>(
        std::min(containers_.size(), static_cast<size_t>(kMaxWaterContainers)));
}

} // namespace engine
