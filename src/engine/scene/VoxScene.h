#pragma once

#include <filesystem>

struct VulkanContext;

namespace engine
{
class VoxelWorld;
class VoxelPalette;
} // namespace engine

class VoxScene
{
public:
    static bool init(VulkanContext& ctx, engine::VoxelWorld& world,
                     engine::VoxelPalette& palette,
                     const std::filesystem::path& voxPath);
};
