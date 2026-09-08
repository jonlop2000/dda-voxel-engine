#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include <glm/glm.hpp>

namespace engine
{

struct VoxColor
{
    uint8_t r = 0;
    uint8_t g = 0;
    uint8_t b = 0;
    uint8_t a = 0;
};

struct VoxPalette
{
    std::array<VoxColor, 256> colors{};
};

struct VoxModel
{
    glm::ivec3 size{0, 0, 0};
    std::vector<uint8_t> voxels; // dense voxel grid in engine layout.
};

struct VoxFile
{
    std::vector<VoxModel> models;
    VoxPalette palette{};
    bool hasPalette = false;
};

class VoxLoader
{
public:
    static bool load(const std::filesystem::path& path, VoxFile& outFile,
                     std::string* outError = nullptr);
};

} // namespace engine
