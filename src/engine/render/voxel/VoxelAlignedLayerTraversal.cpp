#include "engine/render/voxel/VoxelAlignedLayerTraversal.h"

#include <algorithm>
#include <cmath>

#include "engine/voxel/VoxelVolume.h"

namespace engine::render
{
namespace
{

bool validOccupiedBounds(const VoxelAlignedLayerInfo& layer)
{
    return glm::all(glm::greaterThan(layer.dimensions, glm::ivec3(0))) &&
           glm::all(glm::greaterThanEqual(layer.occupiedMin, glm::ivec3(0))) &&
           glm::all(glm::lessThanEqual(layer.occupiedMaxExclusive,
                                      layer.dimensions)) &&
           glm::all(glm::greaterThan(layer.occupiedMaxExclusive,
                                    layer.occupiedMin));
}

bool matricesMatch(const glm::mat4& first, const glm::mat4& second,
                   float epsilon)
{
    for (int column = 0; column < 4; ++column)
    {
        for (int row = 0; row < 4; ++row)
        {
            if (!std::isfinite(first[column][row]) ||
                !std::isfinite(second[column][row]) ||
                std::abs(first[column][row] - second[column][row]) > epsilon)
            {
                return false;
            }
        }
    }
    return true;
}

}  // namespace

VoxelAlignedLayerTraversal classifyVoxelAlignedLayerTraversal(
    const VoxelAlignedLayerInfo& first, const VoxelAlignedLayerInfo& second,
    float transformEpsilon)
{
    VoxelAlignedLayerTraversal result{};
    if (!std::isfinite(transformEpsilon) || transformEpsilon < 0.0f ||
        !validOccupiedBounds(first) || !validOccupiedBounds(second) ||
        first.dimensions != second.dimensions || !first.opaqueOnlyDda ||
        !second.opaqueOnlyDda || !first.unwrappedOpaqueDda ||
        !second.unwrappedOpaqueDda)
    {
        return result;
    }

    constexpr uint32_t kExcludedFlags =
        engine::VoxelVolume::FLAG_GLASS | engine::VoxelVolume::FLAG_WATER |
        engine::VoxelVolume::FLAG_WRAP_XZ | engine::VoxelVolume::FLAG_CLOUD;
    if ((first.flags & kExcludedFlags) != 0u ||
        (second.flags & kExcludedFlags) != 0u ||
        !matricesMatch(first.worldFromLocal, second.worldFromLocal,
                       transformEpsilon) ||
        !matricesMatch(first.localFromWorld, second.localFromWorld,
                       transformEpsilon))
    {
        return result;
    }

    result.compatible = true;
    result.occupiedMin = glm::min(first.occupiedMin, second.occupiedMin);
    result.occupiedMaxExclusive =
        glm::max(first.occupiedMaxExclusive, second.occupiedMaxExclusive);
    return result;
}

}  // namespace engine::render
