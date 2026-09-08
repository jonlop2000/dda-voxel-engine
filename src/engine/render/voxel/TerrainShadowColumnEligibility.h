#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include <glm/glm.hpp>

#include "engine/voxel/VoxelVolume.h"

namespace engine::render
{

// a height-column shadow path is valid only when each X/Z column describes one
// solid interval starting at the volume base. the classifier is deliberately
// fail-closed: a rejected result carries no partial height data, so callers keep
// using the established 3D dda path.
enum class TerrainShadowColumnRejection : uint8_t
{
    None = 0,
    InvalidDimensions,
    VoxelCountMismatch,
    DynamicVolume,
    MissingStaticFlag,
    WrappedVolume,
    NonBinaryOpaque,
    UnsupportedMaterialFlags,
    EmptyColumn,
    NonContiguousColumn,
};

struct TerrainShadowColumnInput
{
    glm::ivec3 dimensions{0};
    uint32_t volumeFlags = 0;
    engine::VoxelVolume::LightingOcclusionMode lightingOcclusionMode =
        engine::VoxelVolume::LightingOcclusionMode::None;
    std::span<const uint8_t> voxels{};
};

struct TerrainShadowColumnEligibility
{
    TerrainShadowColumnRejection rejection =
        TerrainShadowColumnRejection::InvalidDimensions;

    // x-major within each z row: index = x + dimensions.x * z. each value is
    // the exclusive y coordinate of the solid column [0, topYExclusive).
    std::vector<uint32_t> topYExclusive{};

    [[nodiscard]] bool eligible() const noexcept
    {
        return rejection == TerrainShadowColumnRejection::None;
    }
};

[[nodiscard]] const char* terrainShadowColumnRejectionId(
    TerrainShadowColumnRejection rejection) noexcept;

[[nodiscard]] TerrainShadowColumnEligibility classifyTerrainShadowColumns(
    const TerrainShadowColumnInput& input);

}  // namespace engine::render
