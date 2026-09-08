#include "engine/render/voxel/VoxelRasterFaceSelection.h"

#include <algorithm>
#include <cmath>

#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/matrix.hpp>

namespace engine::render
{

VoxelRasterFaceSelection classifyVoxelRasterFaces(
    const glm::mat4& localFromWorld,
    const glm::mat4& worldFromLocal,
    const glm::vec3& boundsMin,
    const glm::vec3& boundsMax,
    const glm::vec3& cameraWorld,
    float nearClipGuardRadiusWorld,
    float boundaryEpsilon)
{
    VoxelRasterFaceSelection selection{};
    const auto finiteVec3 = [](const glm::vec3& value) {
        return std::isfinite(value.x) && std::isfinite(value.y) &&
               std::isfinite(value.z);
    };
    if (!finiteVec3(boundsMin) || !finiteVec3(boundsMax) ||
        !finiteVec3(cameraWorld) || !std::isfinite(nearClipGuardRadiusWorld) ||
        nearClipGuardRadiusWorld < 0.0f || !std::isfinite(boundaryEpsilon) ||
        glm::any(glm::lessThanEqual(boundsMax, boundsMin)))
    {
        return selection;
    }

    const glm::vec3 cameraLocal =
        glm::vec3(localFromWorld * glm::vec4(cameraWorld, 1.0f));
    const float determinant = glm::determinant(glm::mat3(worldFromLocal));
    if (!finiteVec3(cameraLocal) || !std::isfinite(determinant) ||
        std::abs(determinant) <= 1e-8f)
    {
        return selection;
    }

    const float epsilon = std::max(boundaryEpsilon, 0.0f);
    const bool nearBoundary =
        glm::any(glm::lessThanEqual(glm::abs(cameraLocal - boundsMin),
                                    glm::vec3(epsilon))) ||
        glm::any(glm::lessThanEqual(glm::abs(cameraLocal - boundsMax),
                                    glm::vec3(epsilon)));
    selection.cameraInside =
        glm::all(glm::greaterThan(cameraLocal, boundsMin)) &&
        glm::all(glm::lessThan(cameraLocal, boundsMax));
    const glm::vec3 closestLocal = glm::clamp(cameraLocal, boundsMin, boundsMax);
    const glm::vec3 closestWorld =
        glm::vec3(worldFromLocal * glm::vec4(closestLocal, 1.0f));
    if (!finiteVec3(closestWorld))
    {
        return selection;
    }
    const float cameraDistance = glm::length(closestWorld - cameraWorld);
    const bool nearClipMayIntersect =
        !std::isfinite(cameraDistance) || cameraDistance <= nearClipGuardRadiusWorld;

    // a camera inside a sparse volume, or close enough for its finite near-clip
    // rectangle to intersect the proxy, needs coverage independent of the clipped
    // box faces. cameras clear of that guard retain the measured face-selection
    // single-face path; boundary-only ambiguity still fails closed to no-cull.
    selection.safe =
        !selection.cameraInside && !nearBoundary && !nearClipMayIntersect;
    selection.fullScreenCoverageRequired =
        selection.cameraInside || nearClipMayIntersect;
    selection.windingReversed = determinant < 0.0f;
    return selection;
}

}  // namespace engine::render
