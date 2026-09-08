#pragma once

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

namespace engine::render
{

struct VoxelRasterFaceSelection
{
    bool cameraInside = false;
    bool safe = false;
    bool fullScreenCoverageRequired = true;
    bool windingReversed = false;
};

VoxelRasterFaceSelection classifyVoxelRasterFaces(
    const glm::mat4& localFromWorld,
    const glm::mat4& worldFromLocal,
    const glm::vec3& boundsMin,
    const glm::vec3& boundsMax,
    const glm::vec3& cameraWorld,
    float nearClipGuardRadiusWorld,
    float boundaryEpsilon = 1e-3f);

}  // namespace engine::render
