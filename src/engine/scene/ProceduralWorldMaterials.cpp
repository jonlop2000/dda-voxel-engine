#include "engine/scene/ProceduralWorldMaterials.h"

namespace
{
bool isInBand(uint8_t voxelId, uint8_t base, uint8_t count)
{
    const uint16_t id = voxelId;
    return id >= base && id < static_cast<uint16_t>(base) + count;
}
} // namespace

namespace engine::scene
{

VoxelMaterialCategory proceduralWorldMaterialCategory(ProceduralPaletteKind palette,
                                                      uint8_t voxelId)
{
    using namespace ProceduralWorldMaterial;
    switch (voxelId)
    {
    case Grass:
    case Leaves:
        return VoxelMaterialCategory::Plant;
    case Dirt:
        return VoxelMaterialCategory::Gravel;
    case Stone:
        return VoxelMaterialCategory::Stone;
    case Wood:
        return VoxelMaterialCategory::Wood;
    default:
        break;
    }

    if (palette != ProceduralPaletteKind::Beach)
    {
        return VoxelMaterialCategory::Generic;
    }
    if (isInBand(voxelId, BeachDrySandBase, BeachDrySandCount) ||
        isInBand(voxelId, BeachWetSandBase, BeachWetSandCount))
    {
        return VoxelMaterialCategory::Gravel;
    }
    if (isInBand(voxelId, BeachShellBase, BeachShellCount) ||
        isInBand(voxelId, BeachRockBase, BeachRockCount))
    {
        return VoxelMaterialCategory::Stone;
    }
    if (isInBand(voxelId, BeachDuneGrassBase, BeachDuneGrassCount))
    {
        return VoxelMaterialCategory::Plant;
    }
    if (isInBand(voxelId, BeachDriftwoodBase, BeachDriftwoodCount))
    {
        return VoxelMaterialCategory::Wood;
    }
    return VoxelMaterialCategory::Generic;
}

} // namespace engine::scene
