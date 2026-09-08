#pragma once

#include <glm/glm.hpp>

#include "engine/voxel/ChunkGrid.h"

struct RayHit
{
    bool hit = false;
    glm::ivec3 voxel{0};
    glm::ivec3 prevVoxel{0};
    float t = 0.0f;
};

RayHit voxelRaycast(const ChunkGrid& grid, glm::vec3 rayOrigin, glm::vec3 rayDir,
                    float maxDist);
