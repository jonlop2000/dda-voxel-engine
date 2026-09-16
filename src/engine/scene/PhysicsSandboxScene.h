#pragma once

struct VulkanContext;

namespace engine
{
    class VoxelPalette;
    class VoxelWorld;
}

class PhysicsSandboxScene
{
    public:
        static bool init(VulkanContext& ctx, engine::VoxelWorld&, engine::VoxelPalette& palette);
};