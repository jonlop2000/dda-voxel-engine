#pragma once

struct VulkanContext;

namespace engine
{
class VoxelPalette;
class VoxelWorld;
} // namespace engine

class ObbTestScene
{
public:
    static bool init(VulkanContext& ctx, engine::VoxelWorld& world, engine::VoxelPalette& palette);
};
