#pragma once

struct VulkanContext;

namespace engine
{
class VoxelPalette;
class VoxelWorld;
} // namespace engine

class DenseSkipProbeScene
{
public:
    static bool init(VulkanContext& ctx, engine::VoxelWorld& world, engine::VoxelPalette& palette);
    static bool initNpot(VulkanContext& ctx, engine::VoxelWorld& world,
                         engine::VoxelPalette& palette);
    static bool initIrregular(VulkanContext& ctx, engine::VoxelWorld& world,
                              engine::VoxelPalette& palette);
};
