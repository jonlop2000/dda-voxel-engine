#pragma once

#include <cstdint>

#include <glm/glm.hpp>

inline constexpr uint32_t kVolumeGpuTerrainShadowColumns = 0x01u;

struct alignas(16) VolumeGpu
{
    glm::mat4 worldFromLocal{1.0f};
    glm::mat4 localFromWorld{1.0f};
    glm::mat4 normalFromLocal{1.0f};
    glm::vec4 worldAabbMin{0.0f};
    glm::vec4 worldAabbMax{0.0f};
    glm::uvec4 dims_palette_flags{0u};
    // x=volume flags, y=neighbor mask, z=lighting occlusion, w=gpu feature bits.
    glm::uvec4 misc{0u};
    glm::vec4 wrapOffset{0.0f};  // xyz = wrap offset for infinite scrolling
    glm::uvec4 occupiedMin{0u};          // xyz = first occupied voxel index
    glm::uvec4 occupiedMaxExclusive{0u}; // xyz = one-past-last occupied voxel index
};

static_assert(sizeof(VolumeGpu) % 16 == 0, "VolumeGpu must be 16-byte aligned");
