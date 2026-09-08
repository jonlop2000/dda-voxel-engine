#pragma once

#include <cstdint>

#include <glm/glm.hpp>

namespace engine::render
{

struct VoxelAlignedLayerInfo
{
    glm::ivec3 dimensions{0};
    glm::mat4 worldFromLocal{1.0f};
    glm::mat4 localFromWorld{1.0f};
    glm::ivec3 occupiedMin{0};
    glm::ivec3 occupiedMaxExclusive{0};
    uint32_t flags = 0;
    bool opaqueOnlyDda = false;
    bool unwrappedOpaqueDda = false;
};

struct VoxelAlignedLayerTraversal
{
    bool compatible = false;
    glm::ivec3 occupiedMin{0};
    glm::ivec3 occupiedMaxExclusive{0};
};

// returns a shared-traversal contract only when both layers use the exact same
// local lattice and transform and can use the opaque, non-wrapped dda path.
// source textures stay separate; the union here is raster/trace coverage only.
VoxelAlignedLayerTraversal classifyVoxelAlignedLayerTraversal(
    const VoxelAlignedLayerInfo& first,
    const VoxelAlignedLayerInfo& second,
    float transformEpsilon = 0.0f);

}  // namespace engine::render
