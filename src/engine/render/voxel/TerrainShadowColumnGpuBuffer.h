#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include <vulkan/vulkan.h>

#include "engine/render/gpu/UniqueAllocation.h"

struct VulkanContext;

namespace engine::render
{

// compact, host-updatable storage for one eligible volume's x-major terrain
// column tops. rejected volumes bind the VoxelWorld-owned one-element fallback
// buffer and never set the matching VolumeGpu capability bit.
class TerrainShadowColumnGpuBuffer
{
public:
    [[nodiscard]] bool upload(VulkanContext& context,
                              std::span<const uint32_t> topYExclusive);
    void reset();

    [[nodiscard]] bool valid() const noexcept { return static_cast<bool>(buffer_); }
    [[nodiscard]] VkBuffer buffer() const noexcept { return buffer_.get(); }
    [[nodiscard]] VkDeviceSize sizeBytes() const noexcept { return sizeBytes_; }

private:
    UniqueBuffer buffer_{};
    VkDeviceSize sizeBytes_ = 0;
};

}  // namespace engine::render
