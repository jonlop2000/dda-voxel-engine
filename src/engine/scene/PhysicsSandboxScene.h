#pragma once

#include <vector>

#include "engine/physics/BallPhysics.h"

struct VulkanContext;

namespace engine
{
    class VoxelPalette;
    class VoxelWorld;
}

class PhysicsSandboxScene
{
    public:
        // Read starting positions without copying or modifying the simulation's balls.
        static bool init(VulkanContext& ctx, engine::VoxelWorld& world,
                         engine::VoxelPalette& palette,
                         const std::vector<engine::physics::Ball>& balls);
};
