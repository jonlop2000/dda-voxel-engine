#pragma once

#include <cstdint>

#include "engine/voxel/VoxelPalette.h"

namespace engine::scene
{

enum class ProceduralPaletteKind
{
    Default,
    Beach,
};

namespace ProceduralWorldMaterial
{
inline constexpr uint8_t Grass = 1;
inline constexpr uint8_t Dirt = 2;
inline constexpr uint8_t Stone = 3;
inline constexpr uint8_t Wood = 4;
inline constexpr uint8_t Leaves = 5;

inline constexpr uint8_t BeachDrySandBase = 16;
inline constexpr uint8_t BeachDrySandCount = 12;
inline constexpr uint8_t BeachWetSandBase = 28;
inline constexpr uint8_t BeachWetSandCount = 12;
inline constexpr uint8_t BeachShellBase = 40;
inline constexpr uint8_t BeachShellCount = 8;
inline constexpr uint8_t BeachDuneGrassBase = 48;
inline constexpr uint8_t BeachDuneGrassCount = 4;
inline constexpr uint8_t BeachRockBase = 56;
inline constexpr uint8_t BeachRockCount = 8;
inline constexpr uint8_t BeachDriftwoodBase = 64;
inline constexpr uint8_t BeachDriftwoodCount = 6;
} // namespace ProceduralWorldMaterial

VoxelMaterialCategory proceduralWorldMaterialCategory(ProceduralPaletteKind palette,
                                                      uint8_t voxelId);

} // namespace engine::scene
