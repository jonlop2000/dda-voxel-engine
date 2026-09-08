#include "engine/render/WindborneParticleVoxelGeometry.h"

#include <algorithm>
#include <cmath>

namespace engine::render
{
namespace
{
bool finiteCandidate(
    const engine::scene::WindborneParticleCandidate& candidate) noexcept
{
    return std::isfinite(candidate.anchorWorld.x) &&
           std::isfinite(candidate.anchorWorld.y) &&
           std::isfinite(candidate.anchorWorld.z) &&
           std::isfinite(candidate.phaseRadians) &&
           std::isfinite(candidate.selectionRank) &&
           std::isfinite(candidate.kindRank) &&
           std::isfinite(candidate.travelAmplitude) &&
           std::isfinite(candidate.motionSpeed) &&
           std::isfinite(candidate.flutter) && candidate.leafMaterialId < 256u &&
           candidate.moteMaterialId < 256u;
}
} // namespace

std::vector<FoliageGpuPrimitive> buildWindborneParticleVoxelGeometry(
    std::span<const engine::scene::WindborneParticleCandidate> candidates)
{
    if (candidates.size() > engine::scene::kMaxWindborneParticleCount)
    {
        return {};
    }

    std::vector<FoliageGpuPrimitive> result;
    result.reserve(candidates.size());
    for (size_t index = 0u; index < candidates.size(); ++index)
    {
        const engine::scene::WindborneParticleCandidate& candidate =
            candidates[index];
        const float expectedRank =
            (static_cast<float>(index) + 0.5f) /
            static_cast<float>(engine::scene::kMaxWindborneParticleCount);
        if (!finiteCandidate(candidate) ||
            std::abs(candidate.selectionRank - expectedRank) > 1e-6f)
        {
            return {};
        }

        FoliageGpuPrimitive primitive{};
        primitive.centerMotionT =
            glm::vec4(candidate.anchorWorld,
                      std::clamp(candidate.selectionRank, 0.0f, 1.0f));
        // runtime kind/scale choose the final leaf slab or mote cube dimensions.
        primitive.halfExtentPhase = glm::vec4(
            0.08f, 0.022f, 0.13f, candidate.phaseRadians);
        const uint32_t yawOctant = (candidate.randomSeed >> 8u) & 7u;
        const uint32_t encodedSeed =
            (candidate.randomSeed & kFoliageVoxelRandomSeedMask) |
            kFoliageVoxelOrientedSlabFlag |
            (yawOctant << kFoliageVoxelYawOctantShift);
        primitive.materialSeedFlags = glm::uvec4(
            (candidate.leafMaterialId & 0xffu) |
                ((candidate.moteMaterialId & 0xffu) << 8u),
            kWindborneParticlePrimitiveFlag, encodedSeed,
            static_cast<uint32_t>(FoliageVoxelPrimitiveRole::Leaf));
        primitive.swayProfile = glm::vec4(
            std::clamp(candidate.travelAmplitude, 0.0f, 2.0f),
            std::clamp(candidate.motionSpeed, 0.0f, 4.0f),
            std::clamp(candidate.flutter, 0.0f, 2.0f),
            std::clamp(candidate.kindRank, 0.0f, 1.0f));
        result.push_back(primitive);
    }
    return result;
}

} // namespace engine::render
