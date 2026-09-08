#include "engine/render/voxel/VoxelLightingOcclusionPolicy.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

#include "engine/render/RenderSettings.h"

namespace engine::render
{
namespace
{

bool finiteVec3(const glm::vec3& value)
{
    return std::isfinite(value.x) && std::isfinite(value.y) &&
           std::isfinite(value.z);
}

float rectangleSampleRadius(const EditableAreaLight& light)
{
    if (!finiteVec3(light.edge1) || !finiteVec3(light.edge2))
    {
        return std::numeric_limits<float>::infinity();
    }

    const std::array<glm::vec3, 4> corners{
        light.edge1 + light.edge2,
        light.edge1 - light.edge2,
        -light.edge1 + light.edge2,
        -light.edge1 - light.edge2,
    };
    float radius = 0.0f;
    for (const glm::vec3& corner : corners)
    {
        radius = std::max(radius, glm::length(corner));
    }
    return radius;
}

void conservativeOccupiedWorldAabb(const engine::VoxelVolume& volume,
                                   glm::vec3& outMin, glm::vec3& outMax)
{
    glm::vec3 localMin = glm::vec3(volume.occupiedLocalMin());
    glm::vec3 localMax = glm::vec3(volume.occupiedLocalMaxExclusive());
    if ((volume.flags() & engine::VoxelVolume::FLAG_WRAP_XZ) != 0u)
    {
        localMin = volume.localMin();
        localMax = volume.localMax();
    }

    const std::array<glm::vec3, 8> corners{
        glm::vec3{localMin.x, localMin.y, localMin.z},
        glm::vec3{localMax.x, localMin.y, localMin.z},
        glm::vec3{localMin.x, localMax.y, localMin.z},
        glm::vec3{localMax.x, localMax.y, localMin.z},
        glm::vec3{localMin.x, localMin.y, localMax.z},
        glm::vec3{localMax.x, localMin.y, localMax.z},
        glm::vec3{localMin.x, localMax.y, localMax.z},
        glm::vec3{localMax.x, localMax.y, localMax.z},
    };

    outMin = glm::vec3(std::numeric_limits<float>::max());
    outMax = glm::vec3(std::numeric_limits<float>::lowest());
    for (const glm::vec3& corner : corners)
    {
        const glm::vec3 worldCorner =
            glm::vec3(volume.worldFromLocal() * glm::vec4(corner, 1.0f));
        outMin = glm::min(outMin, worldCorner);
        outMax = glm::max(outMax, worldCorner);
    }
}

bool sphereIntersectsAabb(const LocalShadowLightInfluence& light,
                          const glm::vec3& boundsMin,
                          const glm::vec3& boundsMax)
{
    if (!finiteVec3(light.center) ||
        !std::isfinite(light.conservativeRadius))
    {
        return true;
    }

    const float radius = std::max(light.conservativeRadius, 0.0f);
    const glm::vec3 closest = glm::clamp(light.center, boundsMin, boundsMax);
    const glm::vec3 delta = closest - light.center;
    return glm::dot(delta, delta) <= radius * radius;
}

} // namespace

VoxelLightingOcclusionParticipation voxelLightingOcclusionParticipation(
    engine::VoxelVolume::LightingOcclusionMode mode, bool isStaticVolume)
{
    VoxelLightingOcclusionParticipation result{};
    result.planarReflection = isStaticVolume;

    switch (mode)
    {
    case engine::VoxelVolume::LightingOcclusionMode::BinaryOpaque:
        result.sunShadow = isStaticVolume;
        result.localLighting = isStaticVolume;
        break;
    case engine::VoxelVolume::LightingOcclusionMode::None:
        break;
    case engine::VoxelVolume::LightingOcclusionMode::TranslucentFoliage:
        // animated hero foliage still needs its plant-only transmittance path.
        result.sunShadow = true;
        break;
    case engine::VoxelVolume::LightingOcclusionMode::TranslucentMeadow:
        // the visible voxel fallback can render normally. when instanced foliage
        // replaces its color path, app admits it only to the sun-shadow plan.
        result.sunShadow = true;
        break;
    case engine::VoxelVolume::LightingOcclusionMode::Proxy:
        result.mainDda = false;
        result.planarReflection = false;
        result.localLighting = isStaticVolume;
        break;
    }

    return result;
}

float voxelSunShadowFoliageOpacityScale(
    engine::VoxelVolume::LightingOcclusionMode mode)
{
    // repeated sparse meadow cells accumulate naturally. keeping each at 45% of
    // canopy strength grounds patches without stamping hard blade silhouettes.
    return mode == engine::VoxelVolume::LightingOcclusionMode::TranslucentMeadow
               ? 0.45f
               : 1.0f;
}

bool voxelVolumeQualifiesForOpaqueOnlyDda(uint32_t volumeFlags)
{
    return (volumeFlags &
            (engine::VoxelVolume::FLAG_GLASS | engine::VoxelVolume::FLAG_WATER)) == 0u;
}

bool voxelVolumeQualifiesForUnwrappedOpaqueDda(uint32_t volumeFlags)
{
    constexpr uint32_t excludedFlags =
        engine::VoxelVolume::FLAG_GLASS | engine::VoxelVolume::FLAG_WATER |
        engine::VoxelVolume::FLAG_WRAP_XZ | engine::VoxelVolume::FLAG_CLOUD;
    return (volumeFlags & excludedFlags) == 0u;
}

LocalShadowLightInfluence localShadowLightInfluence(
    const EditableAreaLight& light, float receiverOriginBias)
{
    LocalShadowLightInfluence result{};
    result.center = light.position;

    float sampleRadius = 0.0f;
    switch (light.shape)
    {
    case LightShape::Rectangle:
        sampleRadius = rectangleSampleRadius(light);
        break;
    case LightShape::Disc:
    case LightShape::Sphere:
        sampleRadius = std::max(light.sourceRadius, 0.0f);
        break;
    case LightShape::Capsule:
        result.center = (light.capsuleEndA + light.capsuleEndB) * 0.5f;
        sampleRadius =
            std::max(glm::length(light.capsuleEndA - result.center),
                     glm::length(light.capsuleEndB - result.center)) +
            std::max(light.sourceRadius, 0.0f);
        break;
    case LightShape::Point:
        break;
    }

    if (!finiteVec3(result.center) || !std::isfinite(sampleRadius) ||
        !std::isfinite(light.influenceRadius) ||
        !std::isfinite(receiverOriginBias))
    {
        result.conservativeRadius = std::numeric_limits<float>::infinity();
    }
    else
    {
        const float exactRadius =
            std::max(std::max(light.influenceRadius, 0.001f) +
                         std::abs(receiverOriginBias),
                     sampleRadius);
        result.conservativeRadius =
            exactRadius + std::max(0.0001f, exactRadius * 0.00001f);
    }
    return result;
}

bool voxelVolumeMayOccludeLocalShadows(
    const engine::VoxelVolume& volume,
    std::span<const LocalShadowLightInfluence> lights)
{
    if (!volume.hasOccupiedVoxels() || lights.empty())
    {
        return false;
    }

    glm::vec3 boundsMin{};
    glm::vec3 boundsMax{};
    conservativeOccupiedWorldAabb(volume, boundsMin, boundsMax);
    return worldBoundsMayOccludeLocalShadows(boundsMin, boundsMax, lights);
}

bool worldBoundsMayOccludeLocalShadows(
    const glm::vec3& boundsMin, const glm::vec3& boundsMax,
    std::span<const LocalShadowLightInfluence> lights)
{
    if (!lights.empty() &&
        (!finiteVec3(boundsMin) || !finiteVec3(boundsMax) ||
         glm::any(glm::greaterThan(boundsMin, boundsMax))))
    {
        return true;
    }

    for (const LocalShadowLightInfluence& light : lights)
    {
        if (sphereIntersectsAabb(light, boundsMin, boundsMax))
        {
            return true;
        }
    }
    return false;
}

} // namespace engine::render
