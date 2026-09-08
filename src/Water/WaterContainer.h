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

struct WaterContainer
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

struct WaterContainerGpu
{
    glm::vec4 boundsMin_fillHeight; // xyz = min, w = fill height
    glm::vec4 boundsMax_fogDensity; // xyz = max, w = fog density
    glm::vec4 absorption_shape;     // xyz = absorption, w = shape enum
    glm::vec4 deepColor_flags;      // xyz = deep color, w = flags
};

static_assert(sizeof(WaterContainerGpu) == 64,
              "WaterContainerGpu must stay std430-aligned");

class WaterContainerManager
{
public:
    static constexpr uint32_t kMaxWaterContainers = 8;

    bool init(VulkanContext& ctx);
    void shutdown(VulkanContext& ctx);

    void setContainers(const std::vector<WaterContainer>& containers);
    std::vector<WaterContainer>& containers() { return containers_; }
    const std::vector<WaterContainer>& containers() const { return containers_; }

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

    std::vector<WaterContainer> containers_{};
    bool dirty_ = false;
};

} // namespace engine
