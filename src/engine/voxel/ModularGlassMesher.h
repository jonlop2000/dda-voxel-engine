#pragma once

#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

#include "Resources/Mesh.h"

namespace engine
{

struct ModularGlassMeshInput
{
    glm::ivec3 dimensions{0, 0, 0};
    const std::vector<uint8_t>* voxels = nullptr;
    std::vector<uint8_t> glassMaterialIds{3};
};

struct ModularGlassMesh
{
    std::vector<Vertex> vertices{};
    std::vector<uint32_t> indices{};
    uint32_t occupiedVoxelCount = 0;
    uint32_t quadCount = 0;
};

// builds a connected exterior mesh from placed glass voxels.
// adjacent glass voxels hide their shared face, and coplanar exterior faces with the
// same material are greedily merged into larger quads.
ModularGlassMesh buildModularGlassMesh(const ModularGlassMeshInput& input);

} // namespace engine
