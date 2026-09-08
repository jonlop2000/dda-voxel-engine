#include "engine/render/voxel/TerrainShadowColumnEligibility.h"

#include <limits>
#include <optional>

namespace engine::render
{
namespace
{

std::optional<size_t> checkedVoxelCount(const glm::ivec3& dimensions)
{
    if (glm::any(glm::lessThanEqual(dimensions, glm::ivec3(0))))
    {
        return std::nullopt;
    }

    const size_t width = static_cast<size_t>(dimensions.x);
    const size_t height = static_cast<size_t>(dimensions.y);
    const size_t depth = static_cast<size_t>(dimensions.z);
    constexpr size_t maximum = std::numeric_limits<size_t>::max();
    if (width > maximum / height)
    {
        return std::nullopt;
    }
    const size_t sliceSize = width * height;
    if (sliceSize > maximum / depth)
    {
        return std::nullopt;
    }
    return sliceSize * depth;
}

size_t voxelIndex(const glm::ivec3& dimensions, int x, int y, int z)
{
    return static_cast<size_t>(x) +
           static_cast<size_t>(dimensions.x) *
               (static_cast<size_t>(y) +
                static_cast<size_t>(dimensions.y) * static_cast<size_t>(z));
}

TerrainShadowColumnEligibility reject(TerrainShadowColumnRejection reason)
{
    TerrainShadowColumnEligibility result{};
    result.rejection = reason;
    return result;
}

}  // namespace

const char* terrainShadowColumnRejectionId(
    TerrainShadowColumnRejection rejection) noexcept
{
    switch (rejection)
    {
    case TerrainShadowColumnRejection::None:
        return "eligible";
    case TerrainShadowColumnRejection::InvalidDimensions:
        return "invalid-dimensions";
    case TerrainShadowColumnRejection::VoxelCountMismatch:
        return "voxel-count-mismatch";
    case TerrainShadowColumnRejection::DynamicVolume:
        return "dynamic-volume";
    case TerrainShadowColumnRejection::MissingStaticFlag:
        return "missing-static-flag";
    case TerrainShadowColumnRejection::WrappedVolume:
        return "wrapped-volume";
    case TerrainShadowColumnRejection::NonBinaryOpaque:
        return "non-binary-opaque";
    case TerrainShadowColumnRejection::UnsupportedMaterialFlags:
        return "unsupported-material-flags";
    case TerrainShadowColumnRejection::EmptyColumn:
        return "empty-column";
    case TerrainShadowColumnRejection::NonContiguousColumn:
        return "non-contiguous-column";
    }
    return "unknown";
}

TerrainShadowColumnEligibility classifyTerrainShadowColumns(
    const TerrainShadowColumnInput& input)
{
    const std::optional<size_t> expectedVoxelCount =
        checkedVoxelCount(input.dimensions);
    if (!expectedVoxelCount.has_value())
    {
        return reject(TerrainShadowColumnRejection::InvalidDimensions);
    }
    if (input.voxels.size() != *expectedVoxelCount)
    {
        return reject(TerrainShadowColumnRejection::VoxelCountMismatch);
    }

    if ((input.volumeFlags & engine::VoxelVolume::FLAG_DYNAMIC) != 0u)
    {
        return reject(TerrainShadowColumnRejection::DynamicVolume);
    }
    if ((input.volumeFlags & engine::VoxelVolume::FLAG_STATIC) == 0u)
    {
        return reject(TerrainShadowColumnRejection::MissingStaticFlag);
    }
    if ((input.volumeFlags & engine::VoxelVolume::FLAG_WRAP_XZ) != 0u)
    {
        return reject(TerrainShadowColumnRejection::WrappedVolume);
    }
    if (input.lightingOcclusionMode !=
        engine::VoxelVolume::LightingOcclusionMode::BinaryOpaque)
    {
        return reject(TerrainShadowColumnRejection::NonBinaryOpaque);
    }

    constexpr uint32_t unsupportedFlags =
        engine::VoxelVolume::FLAG_GLASS | engine::VoxelVolume::FLAG_WATER |
        engine::VoxelVolume::FLAG_CLOUD;
    if ((input.volumeFlags & unsupportedFlags) != 0u)
    {
        return reject(TerrainShadowColumnRejection::UnsupportedMaterialFlags);
    }

    TerrainShadowColumnEligibility result{};
    result.rejection = TerrainShadowColumnRejection::None;
    result.topYExclusive.resize(
        static_cast<size_t>(input.dimensions.x) *
        static_cast<size_t>(input.dimensions.z));

    for (int z = 0; z < input.dimensions.z; ++z)
    {
        for (int x = 0; x < input.dimensions.x; ++x)
        {
            int topYExclusive = 0;
            while (topYExclusive < input.dimensions.y &&
                   input.voxels[voxelIndex(
                       input.dimensions, x, topYExclusive, z)] != 0u)
            {
                ++topYExclusive;
            }

            if (topYExclusive == 0)
            {
                return reject(TerrainShadowColumnRejection::EmptyColumn);
            }

            for (int y = topYExclusive; y < input.dimensions.y; ++y)
            {
                if (input.voxels[voxelIndex(input.dimensions, x, y, z)] != 0u)
                {
                    return reject(
                        TerrainShadowColumnRejection::NonContiguousColumn);
                }
            }

            const size_t columnIndex =
                static_cast<size_t>(x) +
                static_cast<size_t>(input.dimensions.x) *
                    static_cast<size_t>(z);
            result.topYExclusive[columnIndex] =
                static_cast<uint32_t>(topYExclusive);
        }
    }

    return result;
}

}  // namespace engine::render
