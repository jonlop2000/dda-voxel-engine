#pragma once

struct VulkanContext;

namespace engine
{
class VoxelPalette;
class VoxelWorld;
} // namespace engine

// simple glass voxel test scene for aaa glass refraction development.
// creates various glass structures without water or room enclosures.
class GlassTestScene
{
public:
    static bool init(VulkanContext& ctx, engine::VoxelWorld& world, engine::VoxelPalette& palette);
};
