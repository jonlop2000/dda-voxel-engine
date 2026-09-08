#include "engine/game/FoliagePatch.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <unordered_set>
#include <vector>

namespace engine::game
{
namespace
{
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

[[nodiscard]] size_t kindIndex(FoliagePatchKind kind) noexcept
{
    return std::min(static_cast<size_t>(kind),
                    static_cast<size_t>(FoliagePatchKind::Count) - 1u);
}

[[nodiscard]] bool finiteVec3(const glm::vec3& value) noexcept
{
    return std::isfinite(value.x) && std::isfinite(value.y) &&
           std::isfinite(value.z);
}

[[nodiscard]] bool finitePlant(const FoliageBladeInstance& plant) noexcept
{
    return plant.stablePatchId != 0u && finiteVec3(plant.rootWorld) &&
           finiteVec3(plant.patchRootWorld) &&
           std::isfinite(plant.heightMeters) && plant.heightMeters > 0.0f &&
           std::isfinite(plant.cellSizeMeters) && plant.cellSizeMeters > 0.0f &&
           std::isfinite(plant.patchRadiusMeters) &&
           plant.patchRadiusMeters > 0.0f &&
           std::isfinite(plant.yawRadians) &&
           std::isfinite(plant.phaseRadians) &&
           plant.patchKind != FoliagePatchKind::Count &&
           plant.morphology != FoliageMorphology::Count &&
           plant.morphology ==
               foliageMorphologyForPatch(plant.patchKind, plant.patchSeed);
}

[[nodiscard]] bool sharedPatchMetadataMatches(
    const FoliageBladeInstance& lhs,
    const FoliageBladeInstance& rhs) noexcept
{
    return lhs.stablePatchId == rhs.stablePatchId &&
           lhs.patchRootWorld == rhs.patchRootWorld &&
           lhs.cellSizeMeters == rhs.cellSizeMeters &&
           lhs.patchRadiusMeters == rhs.patchRadiusMeters &&
           lhs.yawRadians == rhs.yawRadians &&
           lhs.phaseRadians == rhs.phaseRadians &&
           lhs.patchSeed == rhs.patchSeed &&
           lhs.stemMaterialId == rhs.stemMaterialId &&
           lhs.secondaryMaterialId == rhs.secondaryMaterialId &&
           lhs.tipMaterialId == rhs.tipMaterialId &&
           lhs.patchKind == rhs.patchKind &&
           lhs.morphology == rhs.morphology;
}

[[nodiscard]] float resolvedPatchHeightMeters(
    FoliageMorphology morphology, uint32_t patchSeed) noexcept
{
    const float variation = seedUnit(patchSeed ^ 0x51c3a447u);
    switch (morphology)
    {
    case FoliageMorphology::GrassTuft:
        return 0.46f + 0.20f * variation;
    case FoliageMorphology::BranchingShrub:
        return 0.58f + 0.24f * variation;
    case FoliageMorphology::DaisyFlower:
        return 0.48f + 0.18f * variation;
    case FoliageMorphology::SpikeFlower:
        return 0.72f + 0.28f * variation;
    case FoliageMorphology::Reed:
        return 0.94f + 0.34f * variation;
    case FoliageMorphology::WaterLily:
        return 0.10f + 0.05f * variation;
    case FoliageMorphology::TreeCanopySprig:
        return 0.32f + 0.08f * variation;
    case FoliageMorphology::Count:
        break;
    }
    return 0.5f;
}
} // namespace

std::string_view foliagePatchKindName(FoliagePatchKind kind)
{
    switch (kind)
    {
    case FoliagePatchKind::GrassTuft:
        return "grass-tuft";
    case FoliagePatchKind::Shrub:
        return "shrub";
    case FoliagePatchKind::FlowerCluster:
        return "flower-cluster";
    case FoliagePatchKind::ReedCluster:
        return "reed-cluster";
    case FoliagePatchKind::WaterLilyCluster:
        return "water-lily-cluster";
    case FoliagePatchKind::TreeCanopyCluster:
        return "tree-canopy-cluster";
    case FoliagePatchKind::Count:
        break;
    }
    return "unknown";
}

float foliagePatchResolvedMaxTipDisplacementMeters(
    const FoliagePatchInstance& patch)
{
    return foliageMorphologyResolvedMaxTipDisplacementMeters(
        patch.morphology, patch.randomSeed);
}

glm::vec3 foliagePatchDisplacement(const FoliagePatchInstance& patch,
                                   float normalizedHeight, float timeSeconds,
                                   const engine::scene::EnvironmentWindSettings& windSettings,
                                   float swayStrength)
{
    const float t = std::clamp(normalizedHeight, 0.0f, 1.0f);
    if (t <= 0.0f)
    {
        return glm::vec3(0.0f);
    }

    const FoliageSwayProfile& sway =
        foliageMorphologyArchetype(patch.morphology).sway;
    const engine::scene::EnvironmentWindSettings wind =
        engine::scene::sanitizeEnvironmentWindSettings(windSettings);
    const engine::scene::EnvironmentWindWaveShape& field =
        engine::scene::defaultEnvironmentWindWaveShape();
    const float flexibleHeight = std::max(1.0f - sway.rootRigidity, 1e-5f);
    const float bendHeight = std::clamp(
        (t - sway.rootRigidity) / flexibleHeight, 0.0f, 1.0f);
    const float bend = std::pow(bendHeight, sway.bendExponent);
    const float gust = engine::scene::environmentWindGustMultiplierFromPhase(
        wind, patch.phaseRadians, timeSeconds);
    const float amplitude = foliagePatchResolvedMaxTipDisplacementMeters(patch) *
                            std::max(swayStrength, 0.0f) * wind.strength * gust;
    const float scaledTime = timeSeconds * wind.speed;
    const float primary =
        std::sin(scaledTime * sway.angularSpeedRadiansPerSecond +
                 patch.phaseRadians);
    const float secondary =
        field.secondaryWeight *
        std::sin(scaledTime * sway.angularSpeedRadiansPerSecond *
                     field.secondaryTimeScale +
                 patch.phaseRadians * field.secondarySpatialScale +
                 field.secondaryPhaseOffsetRadians);
    const float alongWave =
        (primary + secondary) / (1.0f + std::abs(field.secondaryWeight));

    const glm::vec2 crossWind(-wind.direction.y, wind.direction.x);
    const float crossWave =
        engine::scene::environmentWindFoliageCrossWeight(
            wind, field.crossWeight) *
        std::sin(scaledTime * sway.angularSpeedRadiansPerSecond *
                     field.crossTimeScale +
                 patch.phaseRadians * field.crossSpatialScale +
                 field.crossPhaseOffsetRadians);
    const glm::vec2 animated =
        (wind.direction * alongWave + crossWind * crossWave) * amplitude * bend;
    return glm::vec3(animated.x, 0.0f, animated.y);
}

float foliagePatchConservativeDisplacement(const FoliagePatchInstance& patch,
                                           const engine::scene::EnvironmentWindSettings& wind,
                                           float swayStrength)
{
    return foliagePatchResolvedMaxTipDisplacementMeters(patch) *
           std::max(swayStrength, 0.0f) *
           engine::scene::environmentWindConservativeAmplitudeScale(wind);
}

FoliagePatchLayout buildFoliagePatchLayout(
    std::span<const FoliageBladeInstance> plants)
{
    FoliagePatchLayout result{};
    result.sourcePlantCount = plants.size();
    if (plants.empty())
    {
        return result;
    }

    std::map<uint64_t, std::vector<const FoliageBladeInstance*>> groups;
    std::unordered_set<uint64_t> stablePlantIds;
    stablePlantIds.reserve(plants.size());
    for (const FoliageBladeInstance& plant : plants)
    {
        if (!finitePlant(plant) ||
            !stablePlantIds.insert(plant.stableId).second)
        {
            result.valid = false;
            result.sourcePlants.clear();
            result.patches.clear();
            result.kindCounts = {};
            return result;
        }
        groups[plant.stablePatchId].push_back(&plant);
    }

    result.sourcePlants.reserve(plants.size());
    result.patches.reserve(groups.size());
    for (auto& [stablePatchId, members] : groups)
    {
        std::sort(members.begin(), members.end(),
                  [](const FoliageBladeInstance* lhs,
                     const FoliageBladeInstance* rhs) {
                      return lhs->stableId < rhs->stableId;
                  });
        const FoliageBladeInstance& authored = *members.front();
        if (std::any_of(members.begin() + 1, members.end(),
                        [&authored](const FoliageBladeInstance* member) {
                            return !sharedPatchMetadataMatches(authored, *member);
                        }))
        {
            result.valid = false;
            result.sourcePlants.clear();
            result.patches.clear();
            result.kindCounts = {};
            return result;
        }

        FoliagePatchInstance patch{};
        patch.stableId = stablePatchId;
        patch.rootWorld = authored.patchRootWorld;
        patch.radiusMeters = authored.patchRadiusMeters;
        patch.heightMeters =
            resolvedPatchHeightMeters(authored.morphology, authored.patchSeed);
        patch.cellSizeMeters = authored.cellSizeMeters;
        patch.yawRadians = authored.yawRadians;
        patch.phaseRadians = authored.phaseRadians;
        patch.randomSeed = authored.patchSeed;
        patch.primaryMaterialId = authored.stemMaterialId;
        patch.secondaryMaterialId = authored.secondaryMaterialId;
        patch.accentMaterialId = authored.tipMaterialId;
        patch.sourcePlantOffset = result.sourcePlants.size();
        patch.sourcePlantCount = static_cast<uint32_t>(std::min<size_t>(
            members.size(), std::numeric_limits<uint32_t>::max()));
        patch.kind = authored.patchKind;
        patch.morphology = authored.morphology;

        for (const FoliageBladeInstance* member : members)
        {
            result.sourcePlants.push_back(*member);
        }
        result.kindCounts[kindIndex(patch.kind)] += 1u;
        result.patches.push_back(patch);
    }
    return result;
}

} // namespace engine::game
