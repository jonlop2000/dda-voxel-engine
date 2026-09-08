#pragma once

#include <cstdint>

namespace engine
{

// matches the material-category constants in shaders/voxel/palette.glsl.
enum class VoxelMaterialCategory : uint32_t
{
    Generic = 0,
    Frame = 1,
    Gravel = 2,
    Plant = 3,
    Fish = 4,
    Stone = 5,
    Wood = 6,
    Coral = 7,
    Glass = 8,
    Water = 9,
    Room = 10,
};

} // namespace engine
