#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "Resources/Mesh.h"

struct CpuMesh
{
    std::vector<Vertex> vertices{};
    std::vector<uint32_t> indices{};
};

bool loadMeshOBJ(const std::filesystem::path& path, CpuMesh& out, std::string* error);
