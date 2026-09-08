#pragma once

#include <cstdint>

#include <glm/glm.hpp>

struct VulkanContext;

namespace engine
{
class VoxelPalette;
class VoxelWorld;
} // namespace engine

enum class ProceduralWorldTerrainStyle
{
    Default,
    Beach,
};

struct ProceduralWorldSettings
{
    uint32_t seed = 1337;
    glm::ivec3 gridDims{4, 4, 1};
    glm::ivec3 chunkDims{32, 32, 32};
    ProceduralWorldTerrainStyle terrainStyle = ProceduralWorldTerrainStyle::Default;

    // terrain
    float noiseScale = 32.0f;
    float baseHeight = 10.0f;
    float heightAmplitude = 16.0f;
    int dirtDepth = 4;

    // caves
    bool enableCaves = true;
    float caveNoiseScale = 24.0f;
    float caveThreshold = 0.65f;
    int caveMinY = 0;
    int caveMaxY = 1024;

    // trees
    bool enableTrees = true;
    int treesPerChunk = 2;
    int trunkMinH = 4;
    int trunkMaxH = 7;
    int leafRadius = 3;

    // rocks
    bool enableRocks = true;
    int rocksPerChunk = 1;
    int rockMinR = 2;
    int rockMaxR = 4;
};

class ProceduralWorldScene
{
public:
    static bool init(VulkanContext& ctx, engine::VoxelWorld& world, engine::VoxelPalette& palette,
                     const ProceduralWorldSettings& settings);
    static bool regenerate(VulkanContext& ctx, engine::VoxelWorld& world, engine::VoxelPalette& palette,
                           const ProceduralWorldSettings& settings, bool reuseVolumes);
};
