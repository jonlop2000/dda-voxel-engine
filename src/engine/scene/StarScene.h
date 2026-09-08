#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <glm/glm.hpp>

struct VulkanContext;

namespace engine
{
class VoxelPalette;
class VoxelWorld;
} // namespace engine

// star generation parameters
struct StarSettings
{
    uint32_t seed = 123;

    // volume dimensions (large sparse field)
    glm::ivec3 dims{256, 8, 256};

    // world position (very high in sky, above clouds)
    glm::vec3 position{-128.0f, 150.0f, -128.0f};

    // star density (0.01-0.1, very sparse)
    float density = 0.02f;
};

class StarScene
{
public:
    // initialize star volume in VoxelWorld.
    // returns the volume index of the created star field, or -1 on failure.
    static int init(VulkanContext& ctx, engine::VoxelWorld& world,
                    engine::VoxelPalette& palette, const StarSettings& settings);

private:
    // build star voxel data
    static std::vector<uint8_t> buildStarVolume(const glm::ivec3& dims,
                                                 const StarSettings& settings);
};
