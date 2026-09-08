#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include <glm/glm.hpp>
#include <vulkan/vulkan.h>

#include "Water/WaterVolumeShape.h"

struct VulkanContext;

namespace engine
{

struct WaterVolume
{
    glm::vec3 boundsMin{0.0f};
    glm::vec3 boundsMax{0.0f};
    float surfaceHeight{0.0f};
    glm::vec3 absorptionCoeff{0.45f, 0.08f, 0.04f};
    glm::vec3 deepColor{0.02f, 0.12f, 0.22f};
    float fogDensity{0.06f};
    uint32_t shape = waterVolumeShapeValue(WaterVolumeShape::Box);
    uint32_t flags = 0;
};

struct WaterVolumeGpu
{
    glm::vec4 boundsMin;       // xyz = min, w = surfaceHeight
    glm::vec4 boundsMax;       // xyz = max, w = fogDensity
    glm::vec4 absorptionCoeff; // xyz = absorption, w = shape enum
    glm::vec4 deepColor;       // xyz = deep color, w = flags
};

static_assert(sizeof(WaterVolumeGpu) == 64, "WaterVolumeGpu must stay std430-aligned");

class WaterVolumeManager
{
public:
    static constexpr uint32_t kMaxWaterVolumes = 8;

    bool init(VulkanContext& ctx);
    void shutdown(VulkanContext& ctx);

    void setVolumes(const std::vector<WaterVolume>& volumes);
    std::vector<WaterVolume>& volumes() { return volumes_; }
    const std::vector<WaterVolume>& volumes() const { return volumes_; }

    void upload();

    bool dirty() const { return dirty_; }
    void markDirty() { dirty_ = true; }

    uint32_t count() const;
    VkBuffer buffer() const { return buffer_; }
    VkDeviceSize bufferSize() const { return static_cast<VkDeviceSize>(capacityBytes_); }

private:
    VkBuffer buffer_ = VK_NULL_HANDLE;
    VkDeviceMemory memory_ = VK_NULL_HANDLE;
    void* mapped_ = nullptr;
    size_t capacityBytes_ = 0;

    std::vector<WaterVolume> volumes_{};
    bool dirty_ = false;
};

} // namespace engine
