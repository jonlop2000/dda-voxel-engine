#include "engine/voxel/Raycast.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include "engine/voxel/VoxelTypes.h"

namespace
{
glm::ivec3 floorToVoxel(const glm::vec3& p)
{
    return glm::ivec3(static_cast<int>(std::floor(p.x)), static_cast<int>(std::floor(p.y)),
                      static_cast<int>(std::floor(p.z)));
}
} // namespace

RayHit voxelRaycast(const ChunkGrid& grid, glm::vec3 rayOrigin, glm::vec3 rayDir,
                    float maxDist)
{
    RayHit hit{};
    const float dirLen2 = glm::dot(rayDir, rayDir);
    if (dirLen2 < 1e-6f)
    {
        return hit;
    }

    rayDir = glm::normalize(rayDir);
    glm::ivec3 voxel = floorToVoxel(rayOrigin);
    glm::ivec3 prev = voxel;

    glm::ivec3 step(0);
    glm::vec3 tMax(0.0f);
    glm::vec3 tDelta(0.0f);

    const float inf = std::numeric_limits<float>::infinity();

    for (int axis = 0; axis < 3; ++axis)
    {
        const float dir = rayDir[axis];
        if (dir > 0.0f)
        {
            step[axis] = 1;
            const float nextBoundary = static_cast<float>(voxel[axis] + 1);
            tMax[axis] = (nextBoundary - rayOrigin[axis]) / dir;
            tDelta[axis] = 1.0f / dir;
        }
        else if (dir < 0.0f)
        {
            step[axis] = -1;
            const float nextBoundary = static_cast<float>(voxel[axis]);
            tMax[axis] = (nextBoundary - rayOrigin[axis]) / dir;
            tDelta[axis] = -1.0f / dir;
        }
        else
        {
            step[axis] = 0;
            tMax[axis] = inf;
            tDelta[axis] = inf;
        }
    }

    float t = 0.0f;
    const int maxSteps = std::max(1, static_cast<int>(maxDist * 3.0f) + 8);

    for (int stepIndex = 0; stepIndex < maxSteps && t <= maxDist; ++stepIndex)
    {
        if (grid.getVoxelWorld(voxel.x, voxel.y, voxel.z) != BLOCK_AIR)
        {
            hit.hit = true;
            hit.voxel = voxel;
            hit.prevVoxel = prev;
            hit.t = t;
            return hit;
        }

        prev = voxel;

        if (tMax.x < tMax.y)
        {
            if (tMax.x < tMax.z)
            {
                voxel.x += step.x;
                t = tMax.x;
                tMax.x += tDelta.x;
            }
            else
            {
                voxel.z += step.z;
                t = tMax.z;
                tMax.z += tDelta.z;
            }
        }
        else
        {
            if (tMax.y < tMax.z)
            {
                voxel.y += step.y;
                t = tMax.y;
                tMax.y += tDelta.y;
            }
            else
            {
                voxel.z += step.z;
                t = tMax.z;
                tMax.z += tDelta.z;
            }
        }
    }

    return hit;
}
