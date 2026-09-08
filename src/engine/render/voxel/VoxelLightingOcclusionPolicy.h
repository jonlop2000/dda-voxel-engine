#pragma once

#include <span>

#include "engine/voxel/VoxelVolume.h"

namespace engine::render
{

struct EditableAreaLight;

// volume-level routing contract for the voxel renderer's primary and auxiliary dda
// passes. proxy volumes are deliberately scene-visible but color-invisible: they
// supply coarse binary mass to AO/local-light tracing without replacing the real
// canopy's translucent sun-shadow path.
struct VoxelLightingOcclusionParticipation
{
    bool mainDda = true;
    bool planarReflection = false;
    bool sunShadow = false;
    bool localLighting = false;
};

VoxelLightingOcclusionParticipation voxelLightingOcclusionParticipation(
    engine::VoxelVolume::LightingOcclusionMode mode, bool isStaticVolume);

// the meadow's sparse fallback volume is deliberately lighter than a hero canopy
// when both consume the profile-owned foliage opacity control.
float voxelSunShadowFoliageOpacityScale(
    engine::VoxelVolume::LightingOcclusionMode mode);

// the specialized primary-DDA module is valid only when the volume declaration
// conservatively excludes every transmissive material class.
bool voxelVolumeQualifiesForOpaqueOnlyDda(uint32_t volumeFlags);

// the narrower module is valid only for opaque volumes that also exclude every
// wrapped/cloud traversal declaration.
bool voxelVolumeQualifiesForUnwrappedOpaqueDda(uint32_t volumeFlags);

struct LocalShadowLightInfluence
{
    glm::vec3 center{0.0f};
    float conservativeRadius = 0.0f;
};

// the local-shadow shader only evaluates receivers inside influenceRadius, then
// traces toward a sampled point on the light. both endpoints therefore lie in
// this conservative sphere, as does the full shadow segment because a sphere is
// convex. the implementation also includes normal-bias and numeric padding.
LocalShadowLightInfluence localShadowLightInfluence(
    const EditableAreaLight& light, float receiverOriginBias);

bool worldBoundsMayOccludeLocalShadows(
    const glm::vec3& boundsMin, const glm::vec3& boundsMax,
    std::span<const LocalShadowLightInfluence> lights);

// returns false only when the volume's conservative occupied world bounds are
// disjoint from every selected light's complete receiver/sample region.
bool voxelVolumeMayOccludeLocalShadows(
    const engine::VoxelVolume& volume,
    std::span<const LocalShadowLightInfluence> lights);

} // namespace engine::render
