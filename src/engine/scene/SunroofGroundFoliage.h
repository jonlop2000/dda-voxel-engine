#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include <glm/vec3.hpp>

namespace engine::scene
{

struct SunroofGroundFoliagePalette
{
    uint8_t grassBase = 0;
    uint8_t grassCount = 0;
    uint8_t leafBase = 0;
    uint8_t leafCount = 0;
    uint8_t reedBase = 0;
    uint8_t reedCount = 0;
    uint8_t accentBase = 0;
    uint8_t accentCount = 0;
    uint8_t flowerBase = 0;
    uint8_t flowerCount = 0;
};

struct SunroofGroundSurfacePalette
{
    uint8_t grassBase = 0;
    uint8_t grassCount = 0;
    uint8_t algaeBase = 0;
    uint8_t algaeCount = 0;
    uint8_t gravelBase = 0;
    uint8_t gravelCount = 0;
};

struct SunroofGroundFoliageStats
{
    size_t occupiedVoxelCount = 0;
    size_t grassVoxelCount = 0;
    size_t leafVoxelCount = 0;
    size_t reedVoxelCount = 0;
    size_t accentVoxelCount = 0;
    size_t flowerVoxelCount = 0;
    uint32_t turfPatchCount = 0;
    uint32_t ribbonTuftCount = 0;
    uint32_t broadLeafCount = 0;
    uint32_t reedFanCount = 0;
    uint32_t flowerColonyCount = 0;
};

struct SunroofGroundFoliageBuild
{
    std::vector<uint8_t> voxels;
    SunroofGroundFoliageStats stats;
};

// returns a deterministic 0-255 contact-grounding weight for the planted floor.
// the field shares the garden's broad habitat shapes, so dense beds receive
// darker roots without adding the fine-foliage volume to ao or shadow ray
// traversal.
[[nodiscard]] uint8_t sunroofGroundFoliageGrounding(const glm::ivec3& dims, int x, int z,
                                                    uint32_t seed = 0x53554e46u) noexcept;

// selects a deterministic, low-frequency planted-surface material for the
// full-size blocks beneath the fine garden. grass remains dominant while
// coherent algae and gravel islands break up the old checkerboard. zero is
// returned for an invalid palette.
[[nodiscard]] uint8_t sunroofGroundBlockMaterial(const glm::ivec3& dims, int x, int z,
                                                 const SunroofGroundSurfacePalette& palette,
                                                 uint8_t grounding = 0,
                                                 uint32_t seed = 0x47524153u) noexcept;

// builds a deterministic layered underwater garden for the sunroof chamber. low
// turf crosses the focal zone while ribbon tufts, broad leaves, reeds, and
// floral colonies form irregular depth layers around a protected, curved
// navigation channel.
[[nodiscard]] SunroofGroundFoliageBuild buildSunroofGroundFoliage(
    const glm::ivec3& dims, const SunroofGroundFoliagePalette& palette,
    uint32_t seed = 0x53554e46u);

[[nodiscard]] bool isSunroofGroundFoliageWalkway(const glm::ivec3& dims, int x, int z) noexcept;

} // namespace engine::scene
