#include "engine/render/FoliageVoxelGeometry.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <tuple>
#include <utility>
#include <vector>

namespace engine::render
{
namespace
{
constexpr float kPi = 3.14159265358979323846f;
constexpr double kCenterQuantizationPerMeter = 10000.0; // 0.1 mm

struct QuantizedCenter
{
    int64_t x = 0;
    int64_t y = 0;
    int64_t z = 0;
};

[[nodiscard]] bool operator==(const QuantizedCenter& lhs,
                              const QuantizedCenter& rhs) noexcept
{
    return lhs.x == rhs.x && lhs.y == rhs.y && lhs.z == rhs.z;
}

[[nodiscard]] bool quantizedCenterLess(const QuantizedCenter& lhs,
                                       const QuantizedCenter& rhs) noexcept
{
    return std::tie(lhs.x, lhs.y, lhs.z) < std::tie(rhs.x, rhs.y, rhs.z);
}

struct PrimitiveCandidate
{
    QuantizedCenter key{};
    uint64_t stableId = 0;
    FoliageGpuPrimitive primitive{};
};

[[nodiscard]] uint32_t mixSeed(uint32_t value) noexcept
{
    value ^= value >> 16u;
    value *= 0x7feb352du;
    value ^= value >> 15u;
    value *= 0x846ca68bu;
    value ^= value >> 16u;
    return value;
}

[[nodiscard]] float seedUnit(uint32_t seed) noexcept
{
    return static_cast<float>(mixSeed(seed) & 0x00ffffffu) / 16777215.0f;
}

[[nodiscard]] bool finiteVec3(const glm::vec3& value) noexcept
{
    return std::isfinite(value.x) && std::isfinite(value.y) &&
           std::isfinite(value.z);
}

[[nodiscard]] bool finiteVec4(const glm::vec4& value) noexcept
{
    return std::isfinite(value.x) && std::isfinite(value.y) &&
           std::isfinite(value.z) && std::isfinite(value.w);
}

[[nodiscard]] std::optional<QuantizedCenter> quantizeCenter(
    const glm::vec3& center) noexcept
{
    if (!finiteVec3(center))
    {
        return std::nullopt;
    }

    std::array<int64_t, 3> quantized{};
    const std::array<float, 3> components = {center.x, center.y, center.z};
    constexpr double kMinimum =
        static_cast<double>(std::numeric_limits<int64_t>::min()) + 1.0;
    constexpr double kMaximum =
        static_cast<double>(std::numeric_limits<int64_t>::max()) - 1.0;
    for (size_t index = 0; index < components.size(); ++index)
    {
        const double scaled =
            static_cast<double>(components[index]) * kCenterQuantizationPerMeter;
        if (!std::isfinite(scaled) || scaled < kMinimum || scaled > kMaximum)
        {
            return std::nullopt;
        }
        quantized[index] = static_cast<int64_t>(std::llround(scaled));
    }
    return QuantizedCenter{quantized[0], quantized[1], quantized[2]};
}

[[nodiscard]] uint32_t cardinalIndex(float yawRadians) noexcept
{
    const double wrappedYaw = std::remainder(
        static_cast<double>(yawRadians), static_cast<double>(kPi) * 2.0);
    const long long unwrapped = std::llround(
        wrappedYaw / static_cast<double>(kPi * 0.5f));
    const long long wrapped = ((unwrapped % 4ll) + 4ll) % 4ll;
    return static_cast<uint32_t>(wrapped);
}

[[nodiscard]] uint32_t octagonalIndex(float yawRadians) noexcept
{
    const double wrappedYaw = std::remainder(
        static_cast<double>(yawRadians), static_cast<double>(kPi) * 2.0);
    const long long unwrapped = std::llround(
        wrappedYaw / static_cast<double>(kPi * 0.25f));
    const long long wrapped = ((unwrapped % 8ll) + 8ll) % 8ll;
    return static_cast<uint32_t>(wrapped);
}

[[nodiscard]] glm::ivec2 rotateCardinal(const glm::ivec2& value,
                                        uint32_t cardinal) noexcept
{
    switch (cardinal & 3u)
    {
    case 1u:
        return glm::ivec2(-value.y, value.x);
    case 2u:
        return -value;
    case 3u:
        return glm::ivec2(value.y, -value.x);
    default:
        return value;
    }
}

[[nodiscard]] glm::vec2 rotateCardinal(const glm::vec2& value,
                                       uint32_t cardinal) noexcept
{
    switch (cardinal & 3u)
    {
    case 1u:
        return glm::vec2(-value.y, value.x);
    case 2u:
        return -value;
    case 3u:
        return glm::vec2(value.y, -value.x);
    default:
        return value;
    }
}

[[nodiscard]] glm::ivec2 cardinalDirection(uint32_t cardinal) noexcept
{
    static constexpr std::array<glm::ivec2, 4> kDirections = {
        glm::ivec2(1, 0), glm::ivec2(0, 1), glm::ivec2(-1, 0),
        glm::ivec2(0, -1)};
    return kDirections[cardinal & 3u];
}

[[nodiscard]] glm::ivec2 scaledPatchOffset(
    const glm::vec2& normalizedOffset, float radiusMeters, float cellMeters,
    uint32_t orientation) noexcept
{
    const int radiusCells = std::max(
        1, static_cast<int>(std::lround(
               std::max(radiusMeters - cellMeters * 0.5f, cellMeters) /
               cellMeters)));
    const glm::ivec2 offset(
        static_cast<int>(std::lround(normalizedOffset.x * radiusCells)),
        static_cast<int>(std::lround(normalizedOffset.y * radiusCells)));
    return rotateCardinal(offset, orientation);
}

[[nodiscard]] engine::game::FoliageMorphology resolvedMorphology(
    const engine::game::FoliagePatchInstance& patch) noexcept
{
    return patch.morphology;
}

[[nodiscard]] glm::vec4 resolvedSwayProfile(
    const engine::game::FoliagePatchInstance& patch) noexcept
{
    const engine::game::FoliageMorphology morphology =
        resolvedMorphology(patch);
    const engine::game::FoliageSwayProfile& sway =
        engine::game::foliageMorphologyArchetype(morphology).sway;
    return glm::vec4(
        engine::game::foliageMorphologyResolvedMaxTipDisplacementMeters(
            morphology, patch.randomSeed),
        sway.angularSpeedRadiansPerSecond, sway.bendExponent,
        sway.rootRigidity);
}

[[nodiscard]] float motionHeight(float rootWorldY, float authoredHeightMeters,
                                 float centerWorldY,
                                 float halfHeight) noexcept
{
    const float localCenterY = centerWorldY - rootWorldY;
    if (localCenterY - halfHeight <= 1e-5f)
    {
        return 0.0f;
    }
    return std::clamp(localCenterY /
                          std::max(authoredHeightMeters, 1e-4f),
                      0.0f, 1.0f);
}

[[nodiscard]] FoliageGpuPrimitive makePrimitive(
    const engine::game::FoliagePatchInstance& patch,
    const glm::vec3& center, const glm::vec3& halfExtent, float motionT,
    uint32_t materialId, FoliageVoxelPrimitiveRole role,
    const glm::vec4& swayProfile, uint32_t primitiveSeed,
    uint32_t yawOctant = 8u) noexcept
{
    FoliageGpuPrimitive primitive{};
    primitive.centerMotionT =
        glm::vec4(center, std::clamp(motionT, 0.0f, 1.0f));
    primitive.halfExtentPhase = glm::vec4(halfExtent, patch.phaseRadians);
    uint32_t encodedSeed = primitiveSeed & kFoliageVoxelRandomSeedMask;
    if (yawOctant < 8u)
    {
        encodedSeed |= kFoliageVoxelOrientedSlabFlag |
                       ((yawOctant & 7u) <<
                        kFoliageVoxelYawOctantShift);
    }
    primitive.materialSeedFlags =
        glm::uvec4(materialId, static_cast<uint32_t>(resolvedMorphology(patch)),
                   encodedSeed, static_cast<uint32_t>(role));
    primitive.swayProfile = swayProfile;
    return primitive;
}

[[nodiscard]] bool validPrimitive(const FoliageGpuPrimitive& primitive) noexcept
{
    return finiteVec4(primitive.centerMotionT) &&
           finiteVec4(primitive.halfExtentPhase) &&
           finiteVec4(primitive.swayProfile) &&
           primitive.halfExtentPhase.x > 0.0f &&
           primitive.halfExtentPhase.y > 0.0f &&
           primitive.halfExtentPhase.z > 0.0f;
}

[[nodiscard]] bool appendCandidate(std::vector<PrimitiveCandidate>& candidates,
                                   uint64_t stableId,
                                   const FoliageGpuPrimitive& primitive)
{
    if (!validPrimitive(primitive))
    {
        return false;
    }
    const std::optional<QuantizedCenter> key =
        quantizeCenter(glm::vec3(primitive.centerMotionT));
    if (!key.has_value())
    {
        return false;
    }
    candidates.push_back(PrimitiveCandidate{*key, stableId, primitive});
    return true;
}

[[nodiscard]] bool appendBlock(
    std::vector<PrimitiveCandidate>& candidates,
    const engine::game::FoliagePatchInstance& patch,
    const glm::vec3& center, const glm::vec3& halfExtent,
    uint32_t materialId, FoliageVoxelPrimitiveRole role,
    uint32_t primitiveSeed, const glm::vec4& swayProfile,
    float rootWorldY, float authoredHeightMeters,
    uint32_t yawOctant = 8u)
{
    return appendCandidate(
        candidates, patch.stableId,
        makePrimitive(patch, center, halfExtent,
                      motionHeight(rootWorldY, authoredHeightMeters,
                                   center.y, halfExtent.y),
                      materialId, role,
                      swayProfile, primitiveSeed, yawOctant));
}

[[nodiscard]] bool appendBlock(
    std::vector<PrimitiveCandidate>& candidates,
    const engine::game::FoliagePatchInstance& patch,
    const glm::vec3& center, const glm::vec3& halfExtent,
    uint32_t materialId, FoliageVoxelPrimitiveRole role,
    uint32_t primitiveSeed, const glm::vec4& swayProfile,
    uint32_t yawOctant = 8u)
{
    return appendBlock(candidates, patch, center, halfExtent, materialId,
                       role, primitiveSeed, swayProfile, patch.rootWorld.y,
                       patch.heightMeters, yawOctant);
}

[[nodiscard]] bool primitiveTieBreakLess(const FoliageGpuPrimitive& lhs,
                                         const FoliageGpuPrimitive& rhs) noexcept
{
    return std::tie(lhs.materialSeedFlags.x, lhs.materialSeedFlags.y,
                    lhs.materialSeedFlags.z, lhs.materialSeedFlags.w,
                    lhs.centerMotionT.x, lhs.centerMotionT.y,
                    lhs.centerMotionT.z, lhs.centerMotionT.w,
                    lhs.halfExtentPhase.x, lhs.halfExtentPhase.y,
                    lhs.halfExtentPhase.z, lhs.halfExtentPhase.w,
                    lhs.swayProfile.x, lhs.swayProfile.y,
                    lhs.swayProfile.z, lhs.swayProfile.w) <
           std::tie(rhs.materialSeedFlags.x, rhs.materialSeedFlags.y,
                    rhs.materialSeedFlags.z, rhs.materialSeedFlags.w,
                    rhs.centerMotionT.x, rhs.centerMotionT.y,
                    rhs.centerMotionT.z, rhs.centerMotionT.w,
                    rhs.halfExtentPhase.x, rhs.halfExtentPhase.y,
                    rhs.halfExtentPhase.z, rhs.halfExtentPhase.w,
                    rhs.swayProfile.x, rhs.swayProfile.y,
                    rhs.swayProfile.z, rhs.swayProfile.w);
}

[[nodiscard]] bool candidateLess(const PrimitiveCandidate& lhs,
                                 const PrimitiveCandidate& rhs) noexcept
{
    if (quantizedCenterLess(lhs.key, rhs.key))
    {
        return true;
    }
    if (quantizedCenterLess(rhs.key, lhs.key))
    {
        return false;
    }

    // accents win exact overlaps, then leafy silhouettes, then low cover and
    // support stems. the numeric role is an abi, not an overlap priority.
    const uint32_t lhsRole = lhs.primitive.materialSeedFlags.w;
    const uint32_t rhsRole = rhs.primitive.materialSeedFlags.w;
    const auto rolePriority = [](uint32_t role) {
        if (role == static_cast<uint32_t>(FoliageVoxelPrimitiveRole::Flower))
            return 3u;
        if (role == static_cast<uint32_t>(FoliageVoxelPrimitiveRole::Leaf))
            return 2u;
        if (role ==
            static_cast<uint32_t>(FoliageVoxelPrimitiveRole::GroundCover))
            return 1u;
        return 0u;
    };
    const uint32_t lhsPriority = rolePriority(lhsRole);
    const uint32_t rhsPriority = rolePriority(rhsRole);
    if (lhsPriority != rhsPriority)
    {
        return lhsPriority > rhsPriority;
    }
    if (lhs.stableId != rhs.stableId)
    {
        return lhs.stableId < rhs.stableId;
    }
    return primitiveTieBreakLess(lhs.primitive, rhs.primitive);
}

[[nodiscard]] uint32_t patchMaterial(
    const engine::game::FoliagePatchInstance& patch, uint32_t seed) noexcept
{
    return (mixSeed(seed) & 1u) == 0u || patch.secondaryMaterialId == 0u
               ? patch.primaryMaterialId
               : patch.secondaryMaterialId;
}

[[nodiscard]] uint32_t plantMaterial(
    const engine::game::FoliageBladeInstance& plant, uint32_t seed,
    bool preferSecondary = false) noexcept
{
    if (plant.secondaryMaterialId == 0u)
    {
        return plant.stemMaterialId;
    }
    if (preferSecondary || (mixSeed(seed) % 5u) >= 3u)
    {
        return plant.secondaryMaterialId;
    }
    return plant.stemMaterialId;
}

[[nodiscard]] glm::vec2 cardinalLean(uint32_t seed, float magnitude) noexcept
{
    const glm::ivec2 direction = cardinalDirection((seed >> 5u) & 3u);
    return glm::vec2(direction) * magnitude;
}

[[nodiscard]] glm::vec2 octagonalLean(uint32_t seed,
                                      float magnitude) noexcept
{
    constexpr float kDiagonal = 0.70710678118f;
    static constexpr std::array<glm::vec2, 8> kDirections = {
        glm::vec2(1.0f, 0.0f), glm::vec2(kDiagonal, kDiagonal),
        glm::vec2(0.0f, 1.0f), glm::vec2(-kDiagonal, kDiagonal),
        glm::vec2(-1.0f, 0.0f), glm::vec2(-kDiagonal, -kDiagonal),
        glm::vec2(0.0f, -1.0f), glm::vec2(kDiagonal, -kDiagonal)};
    return kDirections[(seed >> 5u) & 7u] * magnitude;
}

[[nodiscard]] bool groundCoverOnly(
    const engine::game::FoliageBladeInstance& plant) noexcept
{
    return (plant.flags & engine::game::FoliageBladeGroundCoverOnly) != 0u;
}

[[nodiscard]] bool aquaticPlant(
    const engine::game::FoliageBladeInstance& plant) noexcept
{
    return (plant.flags & engine::game::FoliageBladeAquatic) != 0u;
}

[[nodiscard]] bool appendSegmentedBlade(
    std::vector<PrimitiveCandidate>& candidates,
    const engine::game::FoliagePatchInstance& patch,
    const engine::game::FoliageBladeInstance& plant,
    const glm::vec3& rootWorld, float heightMeters, float halfWidthMeters,
    const glm::vec2& tipLeanMeters, uint32_t seed,
    FoliageVoxelPrimitiveRole upperRole, const glm::vec4& swayProfile,
    uint32_t minimumSegments = 2u)
{
    const float cell = std::max(plant.cellSizeMeters, 0.02f);
    const uint32_t segmentCount = std::clamp(
        static_cast<uint32_t>(std::ceil(
            heightMeters / std::max(cell * 1.10f, 0.02f))),
        minimumSegments, 10u);
    const float segmentHeight = heightMeters /
                                static_cast<float>(segmentCount);
    const float overlap = std::min(segmentHeight * 0.08f, 0.008f);
    for (uint32_t segment = 0; segment < segmentCount; ++segment)
    {
        const float t0 = static_cast<float>(segment) /
                         static_cast<float>(segmentCount);
        const float t1 = static_cast<float>(segment + 1u) /
                         static_cast<float>(segmentCount);
        const float centerT = (t0 + t1) * 0.5f;
        const glm::vec2 lean = tipLeanMeters * centerT * centerT;
        const glm::vec3 center =
            rootWorld + glm::vec3(lean.x, heightMeters * centerT, lean.y);
        const FoliageVoxelPrimitiveRole role =
            segment == 0u ? FoliageVoxelPrimitiveRole::Stem : upperRole;
        const uint32_t blockSeed = mixSeed(seed ^
            (0x9e3779b9u * (segment + 1u)));
        if (!appendBlock(
                candidates, patch, center,
                glm::vec3(halfWidthMeters,
                          segmentHeight * 0.5f + overlap,
                          halfWidthMeters),
                plantMaterial(plant, blockSeed, segment + 1u == segmentCount),
                role, blockSeed, swayProfile, rootWorld.y, heightMeters))
        {
            return false;
        }
    }
    return true;
}

// meadow blades need to read as overlapping leaves rather than vertical wires.
// keep the closed-cube primitive abi, but vary the footprint per segment so the
// lower silhouette is broad and the tip visibly tapers. the encoded octagonal
// yaw rotates these thin local slabs in the vertex shader without growing the
// primitive abi or splitting the foliage batch.
[[nodiscard]] bool appendTaperedMeadowBlade(
    std::vector<PrimitiveCandidate>& candidates,
    const engine::game::FoliagePatchInstance& patch,
    const engine::game::FoliageBladeInstance& plant,
    const glm::vec3& rootWorld, float heightMeters,
    float baseHalfWidthMeters, float baseHalfThicknessMeters,
    const glm::vec2& tipLeanMeters, uint32_t segmentCount, uint32_t seed,
    FoliageVoxelPrimitiveRole upperRole, const glm::vec4& swayProfile,
    uint32_t yawOctant)
{
    segmentCount = std::clamp(segmentCount, 2u, 6u);
    const float maximumHalfHeight =
        std::max(plant.cellSizeMeters * 0.75f, 0.015f);
    while (segmentCount < 6u)
    {
        const float candidateHeight =
            heightMeters / static_cast<float>(segmentCount);
        const float candidateOverlap =
            std::min(candidateHeight * 0.10f, 0.010f);
        if (candidateHeight * 0.5f + candidateOverlap <=
            maximumHalfHeight)
        {
            break;
        }
        ++segmentCount;
    }
    const float segmentHeight =
        heightMeters / static_cast<float>(segmentCount);
    const float overlap = std::min(segmentHeight * 0.10f, 0.010f);
    yawOctant &= 7u;
    for (uint32_t segment = 0; segment < segmentCount; ++segment)
    {
        const float t0 = static_cast<float>(segment) /
                         static_cast<float>(segmentCount);
        const float t1 = static_cast<float>(segment + 1u) /
                         static_cast<float>(segmentCount);
        const float centerT = (t0 + t1) * 0.5f;
        const glm::vec2 lean = tipLeanMeters * centerT * centerT;
        const glm::vec3 center =
            rootWorld + glm::vec3(lean.x, heightMeters * centerT, lean.y);
        const float widthTaper = 1.0f - 0.48f * centerT;
        const float thicknessTaper = 1.0f - 0.38f * centerT;
        const float halfWidth =
            std::max(baseHalfWidthMeters * widthTaper, 0.009f);
        const float halfThickness =
            std::max(baseHalfThicknessMeters * thicknessTaper, 0.007f);
        const glm::vec3 halfExtent(
            halfWidth, segmentHeight * 0.5f + overlap, halfThickness);
        const FoliageVoxelPrimitiveRole role =
            segment == 0u ? FoliageVoxelPrimitiveRole::Stem : upperRole;
        const uint32_t blockSeed = mixSeed(
            seed ^ (0x9e3779b9u * (segment + 1u)));
        if (!appendBlock(
                candidates, patch, center, halfExtent,
                plantMaterial(plant, blockSeed,
                              segment + 1u == segmentCount),
                role, blockSeed, swayProfile, rootWorld.y, heightMeters,
                yawOctant))
        {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool appendGrassTuft(
    std::vector<PrimitiveCandidate>& candidates,
    const engine::game::FoliagePatchInstance& patch,
    std::span<const engine::game::FoliageBladeInstance> plants,
    const glm::vec4& swayProfile);

[[nodiscard]] bool appendGroundCoverLayer(
    std::vector<PrimitiveCandidate>& candidates,
    const engine::game::FoliagePatchInstance& patch,
    std::span<const engine::game::FoliageBladeInstance> plants,
    const glm::vec4& swayProfile)
{
    static constexpr std::array<glm::vec2, 8> kRootOffsets = {
        glm::vec2(0.00f, 0.00f), glm::vec2(0.72f, 0.18f),
        glm::vec2(-0.58f, 0.54f), glm::vec2(0.20f, -0.76f),
        glm::vec2(0.68f, -0.55f), glm::vec2(-0.80f, -0.22f),
        glm::vec2(-0.16f, 0.82f), glm::vec2(0.42f, 0.70f)};
    for (const engine::game::FoliageBladeInstance& plant : plants)
    {
        const uint32_t seed = mixSeed(plant.randomSeed ^ 0xa24baed5u);
        uint32_t bladeCount = 4u + seed % 2u;
        uint32_t rootLeafCount = 2u;
        const uint32_t segmentCount = 2u;
        float minimumHeight = 0.15f;
        float heightRange = 0.12f;
        float maximumSpread = 0.12f;
        float minimumHalfWidth = 0.016f;
        float halfWidthRange = 0.007f;
        float minimumHalfThickness = 0.010f;
        float halfThicknessRange = 0.005f;
        float minimumLean = 0.020f;
        float leanRange = 0.032f;
        const bool carpet = groundCoverOnly(plant);
        if (carpet)
        {
            const bool referenceTier = plant.cellSizeMeters <= 0.11f;
            const bool balancedTier = !referenceTier &&
                                      plant.cellSizeMeters <= 0.21f;
            if (plant.cellSizeMeters <= 0.11f)
            {
                bladeCount = 5u + seed % 2u;
                rootLeafCount = 3u;
            }
            else if (plant.cellSizeMeters <= 0.21f)
            {
                bladeCount = 4u;
                rootLeafCount = 3u;
            }
            else
            {
                bladeCount = 3u;
                rootLeafCount = 2u;
            }
            const uint32_t clumpRoll =
                mixSeed(seed ^ 0x3c6ef372u) % 100u;
            if (clumpRoll < 22u)
            {
                bladeCount = referenceTier ? 4u
                                           : balancedTier ? 3u : 2u;
                minimumHeight = 0.080f;
                heightRange = 0.075f;
                maximumSpread = 0.105f;
                minimumLean = 0.032f;
                leanRange = 0.038f;
            }
            else if (clumpRoll < 74u)
            {
                bladeCount = referenceTier ? 4u + (seed & 1u)
                                           : balancedTier ? 3u + (seed & 1u)
                                                          : 3u;
                minimumHeight = 0.13f;
                heightRange = 0.11f;
                maximumSpread = 0.125f;
                minimumLean = 0.052f;
                leanRange = 0.058f;
            }
            else
            {
                bladeCount = referenceTier ? 5u + (seed & 1u)
                                           : balancedTier ? 4u : 3u;
                minimumHeight = 0.19f;
                heightRange = 0.15f;
                maximumSpread = 0.140f;
                minimumLean = 0.078f;
                leanRange = 0.072f;
            }
            minimumHalfWidth = 0.030f;
            halfWidthRange = 0.018f;
            minimumHalfThickness = 0.010f;
            halfThicknessRange = 0.007f;
        }
        else if (patch.kind == engine::game::FoliagePatchKind::GrassTuft)
        {
            bladeCount = 6u + seed % 2u;
            rootLeafCount = 3u;
            minimumHeight = 0.17f;
            heightRange = 0.14f;
            maximumSpread = 0.14f;
            minimumHalfWidth = 0.026f;
            halfWidthRange = 0.014f;
            minimumLean = 0.060f;
            leanRange = 0.065f;
        }
        else if (patch.kind == engine::game::FoliagePatchKind::FlowerCluster)
        {
            bladeCount = 5u + seed % 2u;
            rootLeafCount = 3u;
            maximumSpread = 0.13f;
            minimumHalfWidth = 0.024f;
            halfWidthRange = 0.013f;
            minimumLean = 0.052f;
            leanRange = 0.060f;
        }
        else if (patch.kind == engine::game::FoliagePatchKind::Shrub)
        {
            bladeCount = 5u;
            rootLeafCount = 3u;
            maximumSpread = 0.13f;
            minimumHalfWidth = 0.025f;
            halfWidthRange = 0.013f;
            minimumLean = 0.050f;
            leanRange = 0.058f;
        }
        else if (patch.kind == engine::game::FoliagePatchKind::ReedCluster)
        {
            bladeCount = 3u;
            rootLeafCount = 1u;
            minimumHeight = 0.20f;
            heightRange = 0.15f;
        }

        const uint32_t orientation = cardinalIndex(
            patch.yawRadians + 0.5f * kPi * static_cast<float>(seed & 3u));

        // a low crossed rosette visually joins neighboring anchors into a
        // continuous meadow floor. its members stay shallow and root-rigid,
        // while their long axis alternates so the layer does not become a grid.
        for (uint32_t leaf = 0; leaf < rootLeafCount; ++leaf)
        {
            const uint32_t leafSeed = mixSeed(
                seed ^ (0x68bc21ebu * (leaf + 1u)));
            const uint32_t directionCardinal =
                (orientation + ((seed >> 7u) & 3u) + leaf) & 3u;
            const uint32_t yawOctant = directionCardinal * 2u;
            const glm::ivec2 direction =
                cardinalDirection(directionCardinal);
            const float detailScale = std::clamp(
                plant.cellSizeMeters / 0.10f, 1.0f, 1.45f);
            const float radialOffset =
                (carpet ? 0.045f : 0.032f) +
                0.018f * seedUnit(leafSeed ^ 0x51c3a447u);
            const float halfLength =
                (carpet ? 0.082f * detailScale : 0.062f) +
                (carpet ? 0.020f : 0.016f) *
                    seedUnit(leafSeed ^ 0x4cf5ad43u);
            const float halfBreadth =
                (carpet ? 0.030f : 0.023f) +
                0.012f * seedUnit(leafSeed ^ 0x31f142ebu);
            const float halfHeight =
                0.014f + 0.006f * seedUnit(leafSeed ^ 0xd1b54a35u);
            const glm::vec3 center =
                plant.rootWorld +
                glm::vec3(static_cast<float>(direction.x) * radialOffset,
                          halfHeight,
                          static_cast<float>(direction.y) * radialOffset);
            const glm::vec3 halfExtent(
                halfLength, halfHeight, halfBreadth);
            if (!appendBlock(
                    candidates, patch, center, halfExtent,
                    plantMaterial(plant, leafSeed, leaf > 0u),
                    FoliageVoxelPrimitiveRole::GroundCover, leafSeed,
                    swayProfile, plant.rootWorld.y,
                    std::max(plant.heightMeters, minimumHeight),
                    yawOctant))
            {
                return false;
            }
        }

        const float spread =
            std::min(plant.cellSizeMeters * 1.45f, maximumSpread);
        for (uint32_t blade = 0; blade < bladeCount; ++blade)
        {
            const uint32_t bladeSeed = mixSeed(
                seed ^ (0x9e3779b9u * (blade + 1u)));
            const glm::vec2 rootOffset =
                rotateCardinal(kRootOffsets[blade], orientation) * spread;
            const glm::vec3 root =
                plant.rootWorld + glm::vec3(rootOffset.x, 0.0f, rootOffset.y);
            const float height = minimumHeight + heightRange *
                seedUnit(bladeSeed ^ 0x51c3a447u);
            const float halfWidth =
                minimumHalfWidth + halfWidthRange *
                    seedUnit(bladeSeed ^ 0x4cf5ad43u);
            const float halfThickness =
                minimumHalfThickness + halfThicknessRange *
                    seedUnit(bladeSeed ^ 0xd1b54a35u);
            const glm::vec2 lean = octagonalLean(
                bladeSeed, minimumLean + leanRange *
                    seedUnit(bladeSeed ^ 0x68bc21ebu));
            const uint32_t yawOctant =
                (orientation * 2u + blade * 3u +
                 ((bladeSeed >> 17u) & 1u)) &
                7u;
            if (!appendTaperedMeadowBlade(
                    candidates, patch, plant, root, height, halfWidth,
                    halfThickness, lean, segmentCount, bladeSeed,
                    FoliageVoxelPrimitiveRole::GroundCover, swayProfile,
                    yawOctant))
            {
                return false;
            }
        }
    }
    return true;
}

[[nodiscard]] bool appendGrassTuft(
    std::vector<PrimitiveCandidate>& candidates,
    const engine::game::FoliagePatchInstance& patch,
    std::span<const engine::game::FoliageBladeInstance> plants,
    const glm::vec4& swayProfile)
{
    static constexpr std::array<glm::vec2, 4> kBladeOffsets = {
        glm::vec2(0.0f), glm::vec2(0.34f, 0.18f),
        glm::vec2(-0.28f, 0.30f), glm::vec2(0.16f, -0.34f)};
    for (const engine::game::FoliageBladeInstance& plant : plants)
    {
        if (groundCoverOnly(plant))
        {
            continue;
        }
        const uint32_t seed = mixSeed(plant.randomSeed ^ 0x7a143589u);
        const uint32_t bladeCount = 3u + seed % 2u;
        const uint32_t orientation = cardinalIndex(
            patch.yawRadians + 0.5f * kPi * static_cast<float>(seed & 3u));
        for (uint32_t blade = 0; blade < bladeCount; ++blade)
        {
            const uint32_t bladeSeed = mixSeed(
                seed ^ (0x85ebca6bu * (blade + 1u)));
            const glm::vec2 offset = rotateCardinal(
                kBladeOffsets[blade], orientation) * plant.cellSizeMeters;
            const glm::vec3 root =
                plant.rootWorld + glm::vec3(offset.x, 0.0f, offset.y);
            const float height =
                0.24f + 0.15f * seedUnit(bladeSeed ^ 0x51c3a447u) +
                0.12f * std::clamp(plant.heightMeters, 0.0f, 0.60f);
            const float halfWidth = 0.026f + 0.014f *
                seedUnit(bladeSeed ^ 0x4cf5ad43u);
            const float halfThickness = 0.009f + 0.005f *
                seedUnit(bladeSeed ^ 0xd1b54a35u);
            const glm::vec2 lean = octagonalLean(
                bladeSeed, 0.065f + 0.065f *
                    seedUnit(bladeSeed ^ 0x31f142ebu));
            const uint32_t yawOctant =
                (orientation * 2u + blade * 3u +
                 ((bladeSeed >> 17u) & 1u)) &
                7u;
            if (!appendTaperedMeadowBlade(
                    candidates, patch, plant, root, height, halfWidth,
                    halfThickness, lean, 3u, bladeSeed,
                    FoliageVoxelPrimitiveRole::Leaf, swayProfile,
                    yawOctant))
            {
                return false;
            }
        }
    }
    return true;
}

[[nodiscard]] bool appendShrub(
    std::vector<PrimitiveCandidate>& candidates,
    const engine::game::FoliagePatchInstance& patch,
    std::span<const engine::game::FoliageBladeInstance> plants,
    const glm::vec4& swayProfile)
{
    static constexpr std::array<glm::vec3, 9> kLeafOffsets = {
        glm::vec3(0.00f, 0.78f, 0.00f),
        glm::vec3(0.78f, 0.52f, 0.08f),
        glm::vec3(-0.72f, 0.50f, 0.12f),
        glm::vec3(0.10f, 0.48f, 0.76f),
        glm::vec3(-0.08f, 0.45f, -0.74f),
        glm::vec3(0.56f, 0.68f, 0.54f),
        glm::vec3(-0.52f, 0.64f, 0.58f),
        glm::vec3(0.50f, 0.60f, -0.56f),
        glm::vec3(-0.54f, 0.57f, -0.50f)};
    for (const engine::game::FoliageBladeInstance& plant : plants)
    {
        if (groundCoverOnly(plant))
        {
            continue;
        }
        const uint32_t seed = mixSeed(plant.randomSeed ^ 0x91e10da5u);
        const float height = 0.25f + 0.13f *
            seedUnit(seed ^ 0x51c3a447u);
        const float radius = 0.09f + 0.035f *
            seedUnit(seed ^ 0x4cf5ad43u);
        const uint32_t orientation = cardinalIndex(
            patch.yawRadians + 0.5f * kPi * static_cast<float>(seed & 3u));
        if (!appendSegmentedBlade(
                candidates, patch, plant, plant.rootWorld, height * 0.72f,
                0.016f, cardinalLean(seed, radius * 0.18f), seed,
                FoliageVoxelPrimitiveRole::Leaf, swayProfile, 2u))
        {
            return false;
        }

        const uint32_t leafCount = 7u + (seed & 1u);
        for (uint32_t leaf = 0; leaf < leafCount; ++leaf)
        {
            const uint32_t leafSeed = mixSeed(
                seed ^ (0x68bc21ebu * (leaf + 1u)));
            const glm::vec2 rotated = rotateCardinal(
                glm::vec2(kLeafOffsets[leaf].x, kLeafOffsets[leaf].z),
                orientation);
            const glm::vec3 center =
                plant.rootWorld +
                glm::vec3(rotated.x * radius,
                          kLeafOffsets[leaf].y * height,
                          rotated.y * radius);
            const float leafRadius = 0.034f + 0.014f *
                seedUnit(leafSeed ^ 0x31f142ebu);
            if (!appendBlock(
                    candidates, patch, center,
                    glm::vec3(leafRadius, leafRadius * 0.72f,
                              leafRadius),
                    plantMaterial(plant, leafSeed, leaf >= 4u),
                    FoliageVoxelPrimitiveRole::Leaf, leafSeed, swayProfile,
                    plant.rootWorld.y, height))
            {
                return false;
            }
        }
    }
    return true;
}

[[nodiscard]] bool appendTreeCanopySprigs(
    std::vector<PrimitiveCandidate>& candidates,
    const engine::game::FoliagePatchInstance& patch,
    std::span<const engine::game::FoliageBladeInstance> plants,
    const glm::vec4& swayProfile)
{
    static constexpr std::array<int32_t, 3> kYawOffsets = {0, -1, 1};
    static constexpr std::array<float, 3> kMotionHeights = {
        0.0f, 0.78f, 1.0f};
    static constexpr std::array<int32_t, 2> kFlowerYawOffsets = {-2, 2};
    static constexpr std::array<float, 2> kFlowerMotionHeights = {0.90f, 1.0f};
    for (const engine::game::FoliageBladeInstance& plant : plants)
    {
        if ((plant.flags & engine::game::FoliageBladeTreeCanopy) == 0u)
        {
            return false;
        }
        const uint32_t seed = mixSeed(plant.randomSeed ^ 0xb5297a4du);
        glm::vec2 outward(plant.rootWorld.x - patch.rootWorld.x,
                          plant.rootWorld.z - patch.rootWorld.z);
        const float outwardLength = glm::length(outward);
        if (outwardLength > 1e-5f)
        {
            outward /= outwardLength;
        }
        else
        {
            outward = glm::vec2(std::cos(patch.yawRadians),
                                std::sin(patch.yawRadians));
        }
        const glm::vec2 tangent(-outward.y, outward.x);
        const uint32_t baseYaw =
            octagonalIndex(std::atan2(outward.y, outward.x));

        for (uint32_t leaf = 0; leaf < kYawOffsets.size(); ++leaf)
        {
            const uint32_t leafSeed = mixSeed(
                seed ^ (0x68bc21ebu * (leaf + 1u)));
            const float forwardOffset =
                0.010f + 0.010f * static_cast<float>(leaf) +
                0.008f * seedUnit(leafSeed ^ 0x51c3a447u);
            const float tangentSign = leaf == 1u ? -1.0f : 1.0f;
            const float tangentOffset = leaf == 0u
                ? 0.0f
                : tangentSign * (0.010f + 0.010f *
                    seedUnit(leafSeed ^ 0x4cf5ad43u));
            const float verticalOffset =
                0.005f + 0.012f * static_cast<float>(leaf) +
                0.006f * seedUnit(leafSeed ^ 0xd1b54a35u);
            const glm::vec2 horizontal =
                outward * forwardOffset + tangent * tangentOffset;
            const glm::vec3 center =
                plant.rootWorld +
                glm::vec3(horizontal.x, verticalOffset, horizontal.y);
            const float halfLength =
                0.055f + 0.018f * seedUnit(leafSeed ^ 0x31f142ebu);
            const float halfBreadth =
                0.035f + 0.012f * seedUnit(leafSeed ^ 0xa24baed5u);
            const float halfThickness =
                0.026f + 0.009f * seedUnit(leafSeed ^ 0x9e3779b9u);
            const uint32_t yawOctant = static_cast<uint32_t>(
                (static_cast<int32_t>(baseYaw) + kYawOffsets[leaf] + 8) & 7);
            FoliageGpuPrimitive primitive = makePrimitive(
                patch, center,
                glm::vec3(halfLength, halfThickness, halfBreadth),
                kMotionHeights[leaf],
                plantMaterial(plant, leafSeed, leaf >= 2u),
                FoliageVoxelPrimitiveRole::Leaf, swayProfile, leafSeed,
                yawOctant);
            const float phaseJitter =
                -0.12f + 0.24f * seedUnit(leafSeed ^ 0x7a143589u);
            primitive.halfExtentPhase.w += phaseJitter;
            if (!appendCandidate(candidates, patch.stableId, primitive))
            {
                return false;
            }
        }

        if (plant.tipMaterialId == 0u)
        {
            continue;
        }

        for (uint32_t petal = 0u; petal < kFlowerYawOffsets.size(); ++petal)
        {
            const uint32_t petalSeed = mixSeed(
                seed ^ (0xf1357ae1u * (petal + 1u)));
            const float tangentSign = petal == 0u ? -1.0f : 1.0f;
            const float forwardOffset =
                0.082f + 0.010f * seedUnit(petalSeed ^ 0x51c3a447u);
            const float tangentOffset = tangentSign *
                (0.018f + 0.007f * seedUnit(petalSeed ^ 0x4cf5ad43u));
            const float verticalOffset =
                0.030f + 0.008f * static_cast<float>(petal) +
                0.005f * seedUnit(petalSeed ^ 0xd1b54a35u);
            const glm::vec2 horizontal =
                outward * forwardOffset + tangent * tangentOffset;
            const glm::vec3 center =
                plant.rootWorld +
                glm::vec3(horizontal.x, verticalOffset, horizontal.y);
            const glm::vec3 halfExtent(
                0.030f + 0.006f * seedUnit(petalSeed ^ 0x31f142ebu),
                0.012f + 0.003f * seedUnit(petalSeed ^ 0x9e3779b9u),
                0.021f + 0.005f * seedUnit(petalSeed ^ 0xa24baed5u));
            const uint32_t yawOctant = static_cast<uint32_t>(
                (static_cast<int32_t>(baseYaw) + kFlowerYawOffsets[petal] + 8) &
                7);
            FoliageGpuPrimitive primitive = makePrimitive(
                patch, center, halfExtent, kFlowerMotionHeights[petal],
                plant.tipMaterialId, FoliageVoxelPrimitiveRole::Flower,
                swayProfile, petalSeed, yawOctant);
            const float phaseJitter =
                -0.08f + 0.16f * seedUnit(petalSeed ^ 0x7a143589u);
            primitive.halfExtentPhase.w += phaseJitter;
            if (!appendCandidate(candidates, patch.stableId, primitive))
            {
                return false;
            }
        }
    }
    return true;
}

[[nodiscard]] bool appendFlowerRosette(
    std::vector<PrimitiveCandidate>& candidates,
    const engine::game::FoliagePatchInstance& patch,
    const glm::vec4& swayProfile, uint32_t orientation)
{
    static constexpr std::array<glm::ivec2, 5> kRosette = {
        glm::ivec2(0, 0), glm::ivec2(1, 0), glm::ivec2(-1, 0),
        glm::ivec2(0, 1), glm::ivec2(0, -1)};
    const float cell = patch.cellSizeMeters;
    for (uint32_t leaf = 0; leaf < kRosette.size(); ++leaf)
    {
        const glm::ivec2 offset = rotateCardinal(kRosette[leaf], orientation);
        const uint32_t seed =
            mixSeed(patch.randomSeed ^ (0x68bc21ebu * (leaf + 1u)));
        const glm::vec3 center =
            patch.rootWorld +
            glm::vec3(static_cast<float>(offset.x) * cell, cell * 0.5f,
                      static_cast<float>(offset.y) * cell);
        if (!appendBlock(candidates, patch, center,
                         glm::vec3(cell * 0.49f, cell * 0.515f,
                                   cell * 0.49f),
                         patchMaterial(patch, seed),
                         FoliageVoxelPrimitiveRole::Leaf, seed, swayProfile))
        {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool appendDaisyFlowers(
    std::vector<PrimitiveCandidate>& candidates,
    const engine::game::FoliagePatchInstance& patch,
    const glm::vec4& swayProfile)
{
    static constexpr std::array<glm::vec2, 6> kStemRoots = {
        glm::vec2(0.00f, 0.00f), glm::vec2(0.52f, 0.08f),
        glm::vec2(-0.48f, 0.16f), glm::vec2(0.12f, 0.55f),
        glm::vec2(-0.10f, -0.52f), glm::vec2(0.46f, -0.45f)};
    static constexpr std::array<glm::ivec2, 4> kPetalDirections = {
        glm::ivec2(1, 0), glm::ivec2(-1, 0), glm::ivec2(0, 1),
        glm::ivec2(0, -1)};
    const float cell = patch.cellSizeMeters;
    const uint32_t orientation = cardinalIndex(patch.yawRadians);
    if (!appendFlowerRosette(candidates, patch, swayProfile, orientation))
    {
        return false;
    }

    const uint32_t stemCount =
        4u + mixSeed(patch.randomSeed ^ 0x31f142ebu) % 3u;
    const int authoredHeight = std::clamp(
        static_cast<int>(std::lround(patch.heightMeters / cell)), 3, 6);

    for (uint32_t stem = 0; stem < stemCount; ++stem)
    {
        const uint32_t seed = mixSeed(
            patch.randomSeed ^ (0x85ebca6bu * (stem + 1u)));
        const glm::ivec2 base = scaledPatchOffset(
            kStemRoots[stem], patch.radiusMeters, cell, orientation);
        const glm::ivec2 lean = cardinalDirection(
            orientation + ((seed >> 6u) & 3u));
        const int height = std::clamp(
            authoredHeight - 1 + static_cast<int>((seed >> 11u) % 3u),
            3, 6);
        for (int layer = 0; layer < height; ++layer)
        {
            const int lateralStep = layer + 1 == height ? 1 : 0;
            const glm::ivec2 offset = base + lean * lateralStep;
            const glm::vec3 center =
                patch.rootWorld +
                glm::vec3(static_cast<float>(offset.x) * cell,
                          (static_cast<float>(layer) + 0.5f) * cell,
                          static_cast<float>(offset.y) * cell);
            const float width = cell * (layer == 0 ? 0.29f : 0.25f);
            if (!appendBlock(candidates, patch, center,
                             glm::vec3(width, cell * 0.515f, width),
                             patchMaterial(patch, seed + layer),
                             FoliageVoxelPrimitiveRole::Stem,
                             seed + static_cast<uint32_t>(layer), swayProfile))
            {
                return false;
            }
        }

        // one low side leaf breaks the pole silhouette before the radial bloom.
        const glm::ivec2 side =
            base + cardinalDirection(orientation + stem + 1u);
        const glm::vec3 leafCenter =
            patch.rootWorld +
            glm::vec3(static_cast<float>(side.x) * cell,
                      (1.5f + static_cast<float>(stem & 1u)) * cell,
                      static_cast<float>(side.y) * cell);
        if (!appendBlock(candidates, patch, leafCenter,
                         glm::vec3(cell * 0.44f, cell * 0.25f,
                                   cell * 0.30f),
                         patchMaterial(patch, seed ^ 0x4cf5ad43u),
                         FoliageVoxelPrimitiveRole::Leaf,
                         seed ^ 0x4cf5ad43u, swayProfile))
        {
            return false;
        }

        const glm::ivec2 blossomOffset = base + lean;
        const glm::vec3 blossomCenter =
            patch.rootWorld +
            glm::vec3(static_cast<float>(blossomOffset.x) * cell,
                      (static_cast<float>(height) + 0.30f) * cell,
                      static_cast<float>(blossomOffset.y) * cell);
        const uint32_t accent = patch.accentMaterialId != 0u
                                    ? patch.accentMaterialId
                                    : patch.primaryMaterialId;
        if (!appendBlock(candidates, patch, blossomCenter,
                         glm::vec3(cell * 0.34f, cell * 0.25f,
                                   cell * 0.34f), accent,
                         FoliageVoxelPrimitiveRole::Flower,
                         seed ^ 0xf1357ae1u, swayProfile))
        {
            return false;
        }
        for (uint32_t petal = 0; petal < kPetalDirections.size(); ++petal)
        {
            const glm::ivec2 direction =
                rotateCardinal(kPetalDirections[petal], orientation);
            const glm::vec3 petalCenter =
                blossomCenter +
                glm::vec3(static_cast<float>(direction.x) * cell * 0.62f,
                          0.0f,
                          static_cast<float>(direction.y) * cell * 0.62f);
            const glm::vec3 petalExtent =
                direction.x != 0
                    ? glm::vec3(cell * 0.34f, cell * 0.20f,
                                cell * 0.25f)
                    : glm::vec3(cell * 0.25f, cell * 0.20f,
                                cell * 0.34f);
            if (!appendBlock(candidates, patch, petalCenter, petalExtent,
                             accent, FoliageVoxelPrimitiveRole::Flower,
                             seed ^ (0x9e3779b9u * (petal + 1u)),
                             swayProfile))
            {
                return false;
            }
        }
    }
    return true;
}

[[nodiscard]] bool appendSpikeFlowers(
    std::vector<PrimitiveCandidate>& candidates,
    const engine::game::FoliagePatchInstance& patch,
    const glm::vec4& swayProfile)
{
    static constexpr std::array<glm::vec2, 5> kStalkRoots = {
        glm::vec2(0.00f, 0.00f), glm::vec2(0.50f, 0.10f),
        glm::vec2(-0.47f, 0.15f), glm::vec2(0.08f, 0.54f),
        glm::vec2(-0.12f, -0.50f)};
    const float cell = patch.cellSizeMeters;
    const uint32_t orientation = cardinalIndex(patch.yawRadians);
    if (!appendFlowerRosette(candidates, patch, swayProfile, orientation))
    {
        return false;
    }

    const uint32_t stalkCount =
        3u + mixSeed(patch.randomSeed ^ 0xc2b2ae35u) % 3u;
    const int authoredHeight = std::clamp(
        static_cast<int>(std::lround(patch.heightMeters / cell)) + 1, 6, 10);
    const uint32_t accent = patch.accentMaterialId != 0u
                                ? patch.accentMaterialId
                                : patch.primaryMaterialId;
    for (uint32_t stalk = 0; stalk < stalkCount; ++stalk)
    {
        const uint32_t seed = mixSeed(
            patch.randomSeed ^ (0x27d4eb2du * (stalk + 1u)));
        const glm::ivec2 base = scaledPatchOffset(
            kStalkRoots[stalk], patch.radiusMeters, cell, orientation);
        const glm::ivec2 lean = cardinalDirection(
            orientation + ((seed >> 5u) & 3u));
        const int height = std::clamp(
            authoredHeight - 1 + static_cast<int>((seed >> 10u) % 3u),
            6, 10);
        const int flowerStart = height - 4;
        for (int layer = 0; layer < flowerStart; ++layer)
        {
            const int lateralStep = layer + 1 == flowerStart ? 1 : 0;
            const glm::ivec2 offset = base + lean * lateralStep;
            const glm::vec3 center =
                patch.rootWorld +
                glm::vec3(static_cast<float>(offset.x) * cell,
                          (static_cast<float>(layer) + 0.5f) * cell,
                          static_cast<float>(offset.y) * cell);
            const uint32_t blockSeed = seed + static_cast<uint32_t>(layer);
            if (!appendBlock(candidates, patch, center,
                             glm::vec3(cell * 0.25f, cell * 0.515f,
                                       cell * 0.25f),
                             patchMaterial(patch, blockSeed),
                             FoliageVoxelPrimitiveRole::Stem, blockSeed,
                             swayProfile))
            {
                return false;
            }
        }

        const glm::ivec2 sideDirection = cardinalDirection(
            orientation + stalk + 1u);
        for (uint32_t leaf = 0; leaf < 2u; ++leaf)
        {
            const glm::ivec2 offset =
                base + sideDirection * static_cast<int>(leaf + 1u);
            const glm::vec3 center =
                patch.rootWorld +
                glm::vec3(static_cast<float>(offset.x) * cell,
                          (1.35f + static_cast<float>(leaf)) * cell,
                          static_cast<float>(offset.y) * cell);
            const glm::vec3 extent =
                sideDirection.x != 0
                    ? glm::vec3(cell * 0.49f, cell * 0.22f,
                                cell * 0.28f)
                    : glm::vec3(cell * 0.28f, cell * 0.22f,
                                cell * 0.49f);
            if (!appendBlock(candidates, patch, center, extent,
                             patchMaterial(patch, seed ^ (leaf + 17u)),
                             FoliageVoxelPrimitiveRole::Leaf,
                             seed ^ (0x85ebca6bu * (leaf + 1u)),
                             swayProfile))
            {
                return false;
            }
        }

        // alternating paired florets make a vertical color mass rather than a
        // single cap, clearly separating spikes from radial daisies.
        for (int flowerLayer = 0; flowerLayer < 4; ++flowerLayer)
        {
            const int layer = flowerStart + flowerLayer;
            const glm::ivec2 stalkOffset = base + lean;
            const glm::vec3 center =
                patch.rootWorld +
                glm::vec3(static_cast<float>(stalkOffset.x) * cell,
                          (static_cast<float>(layer) + 0.5f) * cell,
                          static_cast<float>(stalkOffset.y) * cell);
            if (!appendBlock(candidates, patch, center,
                             glm::vec3(cell * 0.34f, cell * 0.46f,
                                       cell * 0.34f),
                             accent, FoliageVoxelPrimitiveRole::Flower,
                             seed ^ static_cast<uint32_t>(0xf1357ae1u + layer),
                             swayProfile))
            {
                return false;
            }
            const glm::ivec2 floretDirection = cardinalDirection(
                orientation + static_cast<uint32_t>(flowerLayer & 1));
            const glm::vec3 floretCenter =
                center +
                glm::vec3(static_cast<float>(floretDirection.x) * cell * 0.52f,
                          cell * 0.08f,
                          static_cast<float>(floretDirection.y) * cell * 0.52f);
            if (!appendBlock(candidates, patch, floretCenter,
                             glm::vec3(cell * 0.28f), accent,
                             FoliageVoxelPrimitiveRole::Flower,
                             seed ^ static_cast<uint32_t>(0x7f4a7c15u + layer),
                             swayProfile))
            {
                return false;
            }
        }
    }
    return true;
}

[[maybe_unused]] [[nodiscard]] bool appendFlowerCluster(
    std::vector<PrimitiveCandidate>& candidates,
    const engine::game::FoliagePatchInstance& patch,
    const glm::vec4& swayProfile)
{
    return resolvedMorphology(patch) ==
                   engine::game::FoliageMorphology::DaisyFlower
               ? appendDaisyFlowers(candidates, patch, swayProfile)
               : appendSpikeFlowers(candidates, patch, swayProfile);
}

[[nodiscard]] bool appendFlowerCluster(
    std::vector<PrimitiveCandidate>& candidates,
    const engine::game::FoliagePatchInstance& patch,
    std::span<const engine::game::FoliageBladeInstance> plants,
    const glm::vec4& swayProfile)
{
    static constexpr std::array<glm::vec2, 4> kPetalOffsets = {
        glm::vec2(1.0f, 0.0f), glm::vec2(-1.0f, 0.0f),
        glm::vec2(0.0f, 1.0f), glm::vec2(0.0f, -1.0f)};
    const bool daisy = resolvedMorphology(patch) ==
                       engine::game::FoliageMorphology::DaisyFlower;
    for (const engine::game::FoliageBladeInstance& plant : plants)
    {
        if (groundCoverOnly(plant))
        {
            continue;
        }
        const uint32_t seed = mixSeed(plant.randomSeed ^ 0x31f142ebu);
        const float height =
            daisy
                ? 0.24f + 0.15f * seedUnit(seed ^ 0x51c3a447u)
                : 0.35f + 0.20f * seedUnit(seed ^ 0x51c3a447u);
        const glm::vec2 lean = cardinalLean(
            seed, 0.014f + 0.020f * seedUnit(seed ^ 0x4cf5ad43u));
        const float stemWidth = 0.009f + 0.003f *
            seedUnit(seed ^ 0x68bc21ebu);
        if (!appendSegmentedBlade(
                candidates, patch, plant, plant.rootWorld, height,
                stemWidth, lean, seed, FoliageVoxelPrimitiveRole::Leaf,
                swayProfile, daisy ? 3u : 4u))
        {
            return false;
        }

        const uint32_t accent = plant.tipMaterialId != 0u
                                    ? plant.tipMaterialId
                                    : plant.secondaryMaterialId;
        const glm::vec3 tip = plant.rootWorld +
            glm::vec3(lean.x, height, lean.y);
        const glm::vec2 sideDirection =
            glm::vec2(cardinalDirection((seed >> 8u) & 3u));
        const glm::vec3 sideLeafCenter =
            plant.rootWorld +
            glm::vec3(sideDirection.x * 0.035f, height * 0.42f,
                      sideDirection.y * 0.035f);
        if (!appendBlock(
                candidates, patch, sideLeafCenter,
                sideDirection.x != 0.0f
                    ? glm::vec3(0.034f, 0.016f, 0.014f)
                    : glm::vec3(0.014f, 0.016f, 0.034f),
                plantMaterial(plant, seed ^ 0x85ebca6bu, true),
                FoliageVoxelPrimitiveRole::Leaf, seed ^ 0x85ebca6bu,
                swayProfile, plant.rootWorld.y, height))
        {
            return false;
        }

        if (daisy)
        {
            if (!appendBlock(
                    candidates, patch, tip,
                    glm::vec3(0.024f, 0.018f, 0.024f), accent,
                    FoliageVoxelPrimitiveRole::Flower,
                    seed ^ 0xf1357ae1u, swayProfile, plant.rootWorld.y,
                    height))
            {
                return false;
            }
            const uint32_t orientation = cardinalIndex(
                patch.yawRadians +
                0.5f * kPi * static_cast<float>((seed >> 12u) & 3u));
            for (uint32_t petal = 0; petal < kPetalOffsets.size(); ++petal)
            {
                const glm::vec2 offset =
                    rotateCardinal(kPetalOffsets[petal], orientation);
                const glm::vec3 center =
                    tip + glm::vec3(offset.x * 0.041f, 0.0f,
                                    offset.y * 0.041f);
                const glm::vec3 extent =
                    offset.x != 0.0f
                        ? glm::vec3(0.024f, 0.014f, 0.016f)
                        : glm::vec3(0.016f, 0.014f, 0.024f);
                if (!appendBlock(
                        candidates, patch, center, extent, accent,
                        FoliageVoxelPrimitiveRole::Flower,
                        seed ^ (0x9e3779b9u * (petal + 1u)), swayProfile,
                        plant.rootWorld.y, height))
                {
                    return false;
                }
            }
        }
        else
        {
            const glm::vec2 across(-lean.y, lean.x);
            const float acrossLength = glm::length(across);
            const glm::vec2 floretDirection =
                acrossLength > 1e-5f ? across / acrossLength
                                     : glm::vec2(1.0f, 0.0f);
            for (uint32_t floret = 0; floret < 3u; ++floret)
            {
                const float yOffset =
                    (static_cast<float>(floret) - 1.0f) * 0.042f;
                const float side = floret == 1u
                                       ? 0.0f
                                       : (floret == 0u ? -0.018f : 0.018f);
                const glm::vec3 center =
                    tip + glm::vec3(floretDirection.x * side, yOffset,
                                    floretDirection.y * side);
                if (!appendBlock(
                        candidates, patch, center,
                        glm::vec3(0.023f, 0.026f, 0.023f), accent,
                        FoliageVoxelPrimitiveRole::Flower,
                        seed ^ (0x7f4a7c15u * (floret + 1u)), swayProfile,
                        plant.rootWorld.y, height))
                {
                    return false;
                }
            }
        }
    }
    return true;
}

[[nodiscard]] bool appendWaterLilyCluster(
    std::vector<PrimitiveCandidate>& candidates,
    const engine::game::FoliagePatchInstance& patch,
    std::span<const engine::game::FoliageBladeInstance> plants,
    const glm::vec4& swayProfile)
{
    for (const engine::game::FoliageBladeInstance& plant : plants)
    {
        if (!aquaticPlant(plant))
        {
            return false;
        }

        const uint32_t seed = mixSeed(plant.randomSeed ^ 0x48f2a39bu);
        const uint32_t orientation = (seed >> 5u) & 7u;
        const float halfHeight =
            0.012f + 0.005f * seedUnit(seed ^ 0xd1b54a35u);
        const float radius =
            0.105f + 0.025f * seedUnit(seed ^ 0x51c3a447u);
        const glm::vec2 axis = octagonalLean(seed, 1.0f);
        const glm::vec2 across(-axis.y, axis.x);

        // three shallow, offset slabs approximate a notched round pad while
        // retaining the existing closed-cube primitive abi. their low profile
        // is intentionally distinct from all upright meadow morphologies.
        const std::array<glm::vec2, 3> offsets = {
            glm::vec2(0.0f), across * radius * 0.22f,
            -across * radius * 0.22f};
        const std::array<glm::vec3, 3> extents = {
            glm::vec3(radius, halfHeight, radius * 0.50f),
            glm::vec3(radius * 0.78f, halfHeight * 0.92f,
                      radius * 0.48f),
            glm::vec3(radius * 0.72f, halfHeight * 0.88f,
                      radius * 0.44f)};
        for (uint32_t padPart = 0u; padPart < offsets.size(); ++padPart)
        {
            const uint32_t partSeed = mixSeed(
                seed ^ (0x9e3779b9u * (padPart + 1u)));
            const glm::vec3 center =
                plant.rootWorld +
                glm::vec3(offsets[padPart].x, halfHeight,
                          offsets[padPart].y);
            if (!appendBlock(
                    candidates, patch, center, extents[padPart],
                    plantMaterial(plant, partSeed, padPart > 0u),
                    FoliageVoxelPrimitiveRole::GroundCover, partSeed,
                    swayProfile, plant.rootWorld.y,
                    std::max(plant.heightMeters, halfHeight * 2.0f),
                    (orientation + padPart * 2u) & 7u))
            {
                return false;
            }
        }

        if ((plant.flags & engine::game::FoliageBladeFlower) == 0u)
        {
            continue;
        }

        const uint32_t accent = plant.tipMaterialId != 0u
                                    ? plant.tipMaterialId
                                    : plant.secondaryMaterialId;
        const float blossomY = halfHeight * 2.0f + 0.020f;
        const glm::vec3 blossomCenter =
            plant.rootWorld + glm::vec3(axis.x * radius * 0.10f,
                                        blossomY, axis.y * radius * 0.10f);
        if (!appendBlock(
                candidates, patch, blossomCenter,
                glm::vec3(0.022f, 0.020f, 0.022f), accent,
                FoliageVoxelPrimitiveRole::Flower, seed ^ 0xf1357ae1u,
                swayProfile, plant.rootWorld.y,
                std::max(plant.heightMeters, blossomY + 0.020f)))
        {
            return false;
        }

        static constexpr std::array<glm::vec2, 4> kPetalOffsets = {
            glm::vec2(1.0f, 0.0f), glm::vec2(-1.0f, 0.0f),
            glm::vec2(0.0f, 1.0f), glm::vec2(0.0f, -1.0f)};
        for (uint32_t petal = 0u; petal < kPetalOffsets.size(); ++petal)
        {
            const glm::vec2 offset =
                rotateCardinal(kPetalOffsets[petal], orientation >> 1u);
            const glm::vec3 center =
                blossomCenter +
                glm::vec3(offset.x * 0.038f, -0.004f,
                          offset.y * 0.038f);
            const glm::vec3 extent =
                offset.x != 0.0f
                    ? glm::vec3(0.026f, 0.012f, 0.018f)
                    : glm::vec3(0.018f, 0.012f, 0.026f);
            if (!appendBlock(
                    candidates, patch, center, extent, accent,
                    FoliageVoxelPrimitiveRole::Flower,
                    seed ^ (0x7f4a7c15u * (petal + 1u)), swayProfile,
                    plant.rootWorld.y,
                    std::max(plant.heightMeters, blossomY + 0.020f)))
            {
                return false;
            }
        }
    }
    return true;
}

[[maybe_unused]] [[nodiscard]] bool appendReedCluster(
    std::vector<PrimitiveCandidate>& candidates,
    const engine::game::FoliagePatchInstance& patch,
    const glm::vec4& swayProfile)
{
    static constexpr std::array<glm::vec2, 8> kStemRoots = {
        glm::vec2(0.00f, 0.00f), glm::vec2(0.55f, 0.04f),
        glm::vec2(-0.52f, 0.10f), glm::vec2(0.08f, 0.58f),
        glm::vec2(-0.06f, -0.56f), glm::vec2(0.50f, 0.48f),
        glm::vec2(-0.47f, 0.52f), glm::vec2(0.48f, -0.50f)};
    const float cell = patch.cellSizeMeters;
    const uint32_t orientation = cardinalIndex(patch.yawRadians);
    const uint32_t stemCount = std::clamp(
        4u + mixSeed(patch.randomSeed ^ 0xf17a23b9u) % 4u, 4u, 7u);
    const int authoredHeight = std::clamp(
        static_cast<int>(std::lround(patch.heightMeters / cell)), 6,
        static_cast<int>(kFoliageVoxelMaximumCellsPerAxis));

    for (uint32_t stem = 0; stem < stemCount; ++stem)
    {
        const uint32_t seed = mixSeed(
            patch.randomSeed ^ (0x27d4eb2du * (stem + 1u)));
        const glm::ivec2 base = scaledPatchOffset(
            kStemRoots[stem], patch.radiusMeters, cell, orientation);
        const glm::ivec2 lean = cardinalDirection(
            orientation + ((seed >> 5u) & 3u));
        const int height = std::clamp(
            authoredHeight - 2 + static_cast<int>((seed >> 10u) % 5u), 6,
            static_cast<int>(kFoliageVoxelMaximumCellsPerAxis));
        const float width = cell *
                            (0.24f + 0.05f * seedUnit(seed ^ 0x51c3a447u));
        for (int layer = 0; layer < height; ++layer)
        {
            const int lateralStep = layer * 5 >= height * 4 ? 1 : 0;
            const glm::ivec2 offset = base + lean * lateralStep;
            const glm::vec3 center =
                patch.rootWorld +
                glm::vec3(static_cast<float>(offset.x) * cell,
                          (static_cast<float>(layer) + 0.5f) * cell,
                          static_cast<float>(offset.y) * cell);
            const FoliageVoxelPrimitiveRole role =
                layer <= 1 ? FoliageVoxelPrimitiveRole::Stem
                           : FoliageVoxelPrimitiveRole::Leaf;
            if (!appendBlock(candidates, patch, center,
                             glm::vec3(width, cell * 0.515f, width),
                             patchMaterial(patch, seed + layer), role,
                             seed + static_cast<uint32_t>(layer), swayProfile))
            {
                return false;
            }
        }

        // long lower blades plus a compact terminal seed head distinguish reeds
        // from the shorter grass fan without introducing another draw path.
        const glm::ivec2 leafDirection = cardinalDirection(
            orientation + stem + 1u);
        for (uint32_t leaf = 0; leaf < 2u; ++leaf)
        {
            const glm::ivec2 direction =
                leaf == 0u ? leafDirection : -leafDirection;
            const glm::vec3 center =
                patch.rootWorld +
                glm::vec3(static_cast<float>(base.x + direction.x) * cell,
                          (2.0f + static_cast<float>(leaf) * 1.4f) * cell,
                          static_cast<float>(base.y + direction.y) * cell);
            const glm::vec3 extent =
                direction.x != 0
                    ? glm::vec3(cell * 0.70f, cell * 0.20f,
                                cell * 0.24f)
                    : glm::vec3(cell * 0.24f, cell * 0.20f,
                                cell * 0.70f);
            const uint32_t leafSeed =
                seed ^ (0x85ebca6bu * (leaf + 1u));
            if (!appendBlock(candidates, patch, center, extent,
                             patchMaterial(patch, leafSeed),
                             FoliageVoxelPrimitiveRole::Leaf, leafSeed,
                             swayProfile))
            {
                return false;
            }
        }

        const glm::ivec2 tipOffset = base + lean;
        const glm::vec3 tipCenter =
            patch.rootWorld +
            glm::vec3(static_cast<float>(tipOffset.x) * cell,
                      (static_cast<float>(height) + 0.15f) * cell,
                      static_cast<float>(tipOffset.y) * cell);
        if (!appendBlock(candidates, patch, tipCenter,
                         glm::vec3(width * 1.65f, cell * 0.65f,
                                   width * 1.65f),
                         patch.secondaryMaterialId != 0u
                             ? patch.secondaryMaterialId
                             : patch.primaryMaterialId,
                         FoliageVoxelPrimitiveRole::Leaf,
                         seed ^ 0xd1b54a35u, swayProfile))
        {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool appendReedCluster(
    std::vector<PrimitiveCandidate>& candidates,
    const engine::game::FoliagePatchInstance& patch,
    std::span<const engine::game::FoliageBladeInstance> plants,
    const glm::vec4& swayProfile)
{
    static constexpr std::array<glm::vec2, 3> kStemOffsets = {
        glm::vec2(0.0f), glm::vec2(0.45f, 0.18f),
        glm::vec2(-0.36f, 0.34f)};
    for (const engine::game::FoliageBladeInstance& plant : plants)
    {
        if (groundCoverOnly(plant))
        {
            continue;
        }
        const uint32_t seed = mixSeed(plant.randomSeed ^ 0xf17a23b9u);
        const uint32_t stemCount = 2u + (seed & 1u);
        const uint32_t orientation = cardinalIndex(
            patch.yawRadians + 0.5f * kPi * static_cast<float>(seed & 3u));
        for (uint32_t stem = 0; stem < stemCount; ++stem)
        {
            const uint32_t stemSeed = mixSeed(
                seed ^ (0x27d4eb2du * (stem + 1u)));
            const glm::vec2 offset = rotateCardinal(
                kStemOffsets[stem], orientation) * plant.cellSizeMeters;
            const glm::vec3 root =
                plant.rootWorld + glm::vec3(offset.x, 0.0f, offset.y);
            const float height = 0.50f + 0.27f *
                seedUnit(stemSeed ^ 0x51c3a447u);
            const float width = 0.010f + 0.004f *
                seedUnit(stemSeed ^ 0x4cf5ad43u);
            const glm::vec2 lean = cardinalLean(
                stemSeed, 0.025f + 0.035f *
                    seedUnit(stemSeed ^ 0x68bc21ebu));
            if (!appendSegmentedBlade(
                    candidates, patch, plant, root, height, width, lean,
                    stemSeed, FoliageVoxelPrimitiveRole::Leaf, swayProfile,
                    5u))
            {
                return false;
            }

            const glm::vec2 leafDirection =
                glm::vec2(cardinalDirection(orientation + stem + 1u));
            const glm::vec3 leafCenter =
                root + glm::vec3(leafDirection.x * 0.040f, height * 0.36f,
                                 leafDirection.y * 0.040f);
            const glm::vec3 leafExtent =
                leafDirection.x != 0.0f
                    ? glm::vec3(0.043f, 0.014f, 0.012f)
                    : glm::vec3(0.012f, 0.014f, 0.043f);
            if (!appendBlock(
                    candidates, patch, leafCenter, leafExtent,
                    plantMaterial(plant, stemSeed ^ 0x85ebca6bu, true),
                    FoliageVoxelPrimitiveRole::Leaf,
                    stemSeed ^ 0x85ebca6bu, swayProfile, root.y, height))
            {
                return false;
            }

            const glm::vec3 tip = root +
                glm::vec3(lean.x, height + 0.012f, lean.y);
            if (!appendBlock(
                    candidates, patch, tip,
                    glm::vec3(width * 1.35f, 0.032f, width * 1.35f),
                    plantMaterial(plant, stemSeed ^ 0xd1b54a35u, true),
                    FoliageVoxelPrimitiveRole::Leaf,
                    stemSeed ^ 0xd1b54a35u, swayProfile, root.y, height))
            {
                return false;
            }
        }
    }
    return true;
}
} // namespace

FoliageUnitCubeVertex decodeFoliageUnitCubeVertex(uint32_t vertexIndex) noexcept
{
    if (vertexIndex >= kFoliageVoxelVerticesPerPrimitive)
    {
        return {};
    }

    static const std::array<uint32_t, 6> kTriangleCorners = {
        0u, 1u, 2u, 0u, 2u, 3u};
    static const std::array<std::array<glm::vec3, 4>, 6> kFaceCorners = {{
        {{{1.0f, -1.0f, -1.0f}, {1.0f, 1.0f, -1.0f},
          {1.0f, 1.0f, 1.0f}, {1.0f, -1.0f, 1.0f}}},
        {{{-1.0f, -1.0f, 1.0f}, {-1.0f, 1.0f, 1.0f},
          {-1.0f, 1.0f, -1.0f}, {-1.0f, -1.0f, -1.0f}}},
        {{{-1.0f, 1.0f, 1.0f}, {1.0f, 1.0f, 1.0f},
          {1.0f, 1.0f, -1.0f}, {-1.0f, 1.0f, -1.0f}}},
        {{{-1.0f, -1.0f, -1.0f}, {1.0f, -1.0f, -1.0f},
          {1.0f, -1.0f, 1.0f}, {-1.0f, -1.0f, 1.0f}}},
        {{{-1.0f, -1.0f, 1.0f}, {1.0f, -1.0f, 1.0f},
          {1.0f, 1.0f, 1.0f}, {-1.0f, 1.0f, 1.0f}}},
        {{{1.0f, -1.0f, -1.0f}, {-1.0f, -1.0f, -1.0f},
          {-1.0f, 1.0f, -1.0f}, {1.0f, 1.0f, -1.0f}}},
    }};
    static const std::array<glm::vec3, 6> kFaceNormals = {
        glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(-1.0f, 0.0f, 0.0f),
        glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(0.0f, -1.0f, 0.0f),
        glm::vec3(0.0f, 0.0f, 1.0f), glm::vec3(0.0f, 0.0f, -1.0f)};

    const uint32_t face = vertexIndex / 6u;
    const uint32_t corner = kTriangleCorners[vertexIndex % 6u];
    return FoliageUnitCubeVertex{kFaceCorners[face][corner],
                                 kFaceNormals[face]};
}

FoliageVoxelGeometry buildFoliageVoxelGeometry(
    std::span<const engine::game::FoliageBladeInstance> plants)
{
    FoliageVoxelGeometry geometry{};
    geometry.semanticInstanceCount = plants.size();

    const engine::game::FoliagePatchLayout layout =
        engine::game::buildFoliagePatchLayout(plants);
    if (!layout.valid)
    {
        return geometry;
    }
    geometry.patchCount = layout.patches.size();
    geometry.patchKindCounts = layout.kindCounts;

    std::vector<PrimitiveCandidate> candidates;
    constexpr size_t kEstimatedPrimitivesPerPatch = 1024u;
    if (layout.patches.size() <=
        candidates.max_size() / kEstimatedPrimitivesPerPatch)
    {
        candidates.reserve(layout.patches.size() *
                           kEstimatedPrimitivesPerPatch);
    }

    for (const engine::game::FoliagePatchInstance& patch : layout.patches)
    {
        if (!finiteVec3(patch.rootWorld) ||
            !std::isfinite(patch.radiusMeters) || patch.radiusMeters <= 0.0f ||
            !std::isfinite(patch.heightMeters) || patch.heightMeters <= 0.0f ||
            !std::isfinite(patch.cellSizeMeters) ||
            patch.cellSizeMeters <= 0.0f ||
            !std::isfinite(patch.phaseRadians))
        {
            geometry.patchCount = 0u;
            geometry.patchKindCounts = {};
            geometry.morphologyPatchCounts = {};
            geometry.primitives.clear();
            return geometry;
        }

        const engine::game::FoliageMorphology morphology =
            resolvedMorphology(patch);
        if (morphology == engine::game::FoliageMorphology::Count ||
            patch.sourcePlantCount == 0u ||
            patch.sourcePlantOffset > layout.sourcePlants.size() ||
            static_cast<size_t>(patch.sourcePlantCount) >
                layout.sourcePlants.size() - patch.sourcePlantOffset)
        {
            geometry.patchCount = 0u;
            geometry.patchKindCounts = {};
            geometry.morphologyPatchCounts = {};
            geometry.primitives.clear();
            return geometry;
        }
        const engine::game::FoliageMorphologyArchetype& archetype =
            engine::game::foliageMorphologyArchetype(morphology);
        const std::span<const engine::game::FoliageBladeInstance> patchPlants(
            layout.sourcePlants.data() + patch.sourcePlantOffset,
            patch.sourcePlantCount);
        const size_t primitiveBegin = candidates.size();
        const glm::vec4 swayProfile = resolvedSwayProfile(patch);
        const bool hasSpeciesAccent = std::any_of(
            patchPlants.begin(), patchPlants.end(),
            [](const engine::game::FoliageBladeInstance& plant) {
                return !groundCoverOnly(plant);
            });
        const bool waterLily =
            patch.kind == engine::game::FoliagePatchKind::WaterLilyCluster;
        const bool treeCanopy =
            patch.kind == engine::game::FoliagePatchKind::TreeCanopyCluster;
        bool appended = treeCanopy
                            ? true
                        : waterLily
                            ? appendWaterLilyCluster(
                                  candidates, patch, patchPlants, swayProfile)
                            : appendGroundCoverLayer(
                                  candidates, patch, patchPlants, swayProfile);
        if (!appended)
        {
            geometry.patchCount = 0u;
            geometry.patchKindCounts = {};
            geometry.morphologyPatchCounts = {};
            geometry.primitives.clear();
            return geometry;
        }
        switch (patch.kind)
        {
        case engine::game::FoliagePatchKind::GrassTuft:
            appended = appendGrassTuft(
                candidates, patch, patchPlants, swayProfile);
            break;
        case engine::game::FoliagePatchKind::Shrub:
            appended = appendShrub(
                candidates, patch, patchPlants, swayProfile);
            break;
        case engine::game::FoliagePatchKind::FlowerCluster:
            appended = appendFlowerCluster(
                candidates, patch, patchPlants, swayProfile);
            break;
        case engine::game::FoliagePatchKind::ReedCluster:
            appended = appendReedCluster(
                candidates, patch, patchPlants, swayProfile);
            break;
        case engine::game::FoliagePatchKind::WaterLilyCluster:
            // dedicated pad/blossom topology was emitted above; do not layer
            // terrain-grounded grass underneath floating plants.
            break;
        case engine::game::FoliagePatchKind::TreeCanopyCluster:
            appended = appendTreeCanopySprigs(
                candidates, patch, patchPlants, swayProfile);
            break;
        case engine::game::FoliagePatchKind::Count:
            appended = false;
            break;
        }
        const size_t emittedPrimitiveCount = candidates.size() - primitiveBegin;
        const size_t minimumPrimitiveCount =
            hasSpeciesAccent ? archetype.minimumPrimitivesPerPatch : 1u;
        if (!appended ||
            emittedPrimitiveCount < minimumPrimitiveCount ||
            emittedPrimitiveCount > archetype.maximumPrimitivesPerPatch)
        {
            geometry.patchCount = 0u;
            geometry.patchKindCounts = {};
            geometry.morphologyPatchCounts = {};
            geometry.primitives.clear();
            return geometry;
        }
        ++geometry.morphologyPatchCounts[static_cast<size_t>(morphology)];
    }

    std::sort(candidates.begin(), candidates.end(), candidateLess);
    geometry.primitives.reserve(candidates.size());
    std::optional<QuantizedCenter> previousKey;
    for (const PrimitiveCandidate& candidate : candidates)
    {
        if (previousKey.has_value() && candidate.key == *previousKey)
        {
            continue;
        }
        geometry.primitives.push_back(candidate.primitive);
        previousKey = candidate.key;
    }
    return geometry;
}

} // namespace engine::render
