#pragma once

#include <cstdint>
#include <filesystem>

#include <glm/glm.hpp>

struct VulkanContext;

namespace engine
{
class VoxelPalette;
class VoxelWorld;
} // namespace engine

struct VoxelImportSettings
{
    std::filesystem::path meshPath{};
    int resolution = 64;
    bool splitIntoChunks = false;
};

struct VoxelImportStats
{
    uint32_t triangleCount = 0;
    uint32_t filledVoxels = 0;
    double voxelizeMs = 0.0;
};

class VoxelImportScene
{
public:
    static bool init(VulkanContext& ctx, engine::VoxelWorld& world, engine::VoxelPalette& palette,
                     const VoxelImportSettings& settings, VoxelImportStats* outStats);
};
