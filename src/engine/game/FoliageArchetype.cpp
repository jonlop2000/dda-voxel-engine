#include "engine/game/FoliageArchetype.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace engine::game
{
namespace
{
constexpr std::array<FoliageArchetype, 2> kArchetypes = {
    FoliageArchetype{FoliageForm::MeadowBlade, 0.035f,
                     {0.035f, 0.52f, 1.85f, 0.28f}},
    FoliageArchetype{FoliageForm::Reed, 0.045f,
                     {0.055f, 0.38f, 2.15f, 0.22f}},
};

constexpr std::array<FoliageMorphologyArchetype, 7> kMorphologyArchetypes = {
    FoliageMorphologyArchetype{FoliageMorphology::GrassTuft,
                               {0.026f, 0.46f, 1.90f, 0.30f}, 12u, 3072u},
    FoliageMorphologyArchetype{FoliageMorphology::BranchingShrub,
                               {0.014f, 0.27f, 2.30f, 0.56f}, 12u, 3072u},
    FoliageMorphologyArchetype{FoliageMorphology::DaisyFlower,
                               {0.022f, 0.43f, 2.00f, 0.38f}, 10u, 3072u},
    FoliageMorphologyArchetype{FoliageMorphology::SpikeFlower,
                               {0.026f, 0.35f, 2.15f, 0.32f}, 10u, 3072u},
    FoliageMorphologyArchetype{FoliageMorphology::Reed,
                               {0.038f, 0.34f, 2.20f, 0.24f}, 10u, 768u},
    FoliageMorphologyArchetype{FoliageMorphology::WaterLily,
                               {0.010f, 0.18f, 1.35f, 0.05f}, 3u, 512u},
    FoliageMorphologyArchetype{FoliageMorphology::TreeCanopySprig,
                               {0.026f, 1.15f, 1.20f, 0.05f}, 3u, 1024u},
};

uint32_t mixSeed(uint32_t seed)
{
    uint32_t value = seed;
    value ^= value >> 16u;
    value *= 0x7feb352du;
    value ^= value >> 15u;
    value *= 0x846ca68bu;
    value ^= value >> 16u;
    return value;
}

float seedUnit(uint32_t seed)
{
    return static_cast<float>(mixSeed(seed) & 0x00ffffffu) / 16777215.0f;
}
} // namespace

const FoliageArchetype& foliageArchetype(FoliageForm form)
{
    const size_t index = form == FoliageForm::Reed ? 1u : 0u;
    return kArchetypes[index];
}

const FoliageMorphologyArchetype& foliageMorphologyArchetype(
    FoliageMorphology morphology)
{
    const size_t requested = static_cast<size_t>(morphology);
    const size_t index = requested < kMorphologyArchetypes.size() ? requested : 0u;
    return kMorphologyArchetypes[index];
}

std::string_view foliageMorphologyName(FoliageMorphology morphology)
{
    switch (morphology)
    {
    case FoliageMorphology::GrassTuft:
        return "grass-tuft";
    case FoliageMorphology::BranchingShrub:
        return "branching-shrub";
    case FoliageMorphology::DaisyFlower:
        return "daisy-flower";
    case FoliageMorphology::SpikeFlower:
        return "spike-flower";
    case FoliageMorphology::Reed:
        return "reed";
    case FoliageMorphology::WaterLily:
        return "water-lily";
    case FoliageMorphology::TreeCanopySprig:
        return "tree-canopy-sprig";
    case FoliageMorphology::Count:
        break;
    }
    return "unknown";
}

FoliageMorphology foliageFlowerMorphology(uint32_t stableSeed)
{
    // keep both species common enough to read as intentional colonies while
    // avoiding any dependency on traversal order or platform rng behavior.
    return seedUnit(stableSeed ^ 0x6d2b79f5u) < 0.58f
               ? FoliageMorphology::DaisyFlower
               : FoliageMorphology::SpikeFlower;
}

FoliageMorphology foliageMorphologyForPatch(FoliagePatchKind kind,
                                             uint32_t stableSeed)
{
    switch (kind)
    {
    case FoliagePatchKind::GrassTuft:
        return FoliageMorphology::GrassTuft;
    case FoliagePatchKind::Shrub:
        return FoliageMorphology::BranchingShrub;
    case FoliagePatchKind::FlowerCluster:
        return foliageFlowerMorphology(stableSeed);
    case FoliagePatchKind::ReedCluster:
        return FoliageMorphology::Reed;
    case FoliagePatchKind::WaterLilyCluster:
        return FoliageMorphology::WaterLily;
    case FoliagePatchKind::TreeCanopyCluster:
        return FoliageMorphology::TreeCanopySprig;
    case FoliagePatchKind::Count:
        break;
    }
    return FoliageMorphology::Count;
}

float foliageMorphologyResolvedMaxTipDisplacementMeters(
    FoliageMorphology morphology, uint32_t stableSeed)
{
    const FoliageSwayProfile& sway = foliageMorphologyArchetype(morphology).sway;
    const float seedVariation = seedUnit(stableSeed ^ 0xa511e9b3u);
    return sway.maxTipDisplacementMeters * (0.86f + 0.14f * seedVariation);
}

FoliageForm foliageForm(const FoliageBladeInstance& instance)
{
    return (instance.flags & FoliageBladeReed) != 0u ? FoliageForm::Reed
                                                     : FoliageForm::MeadowBlade;
}

uint64_t foliageStableId(uint32_t worldSeed, int candidateX, int candidateZ)
{
    const uint64_t x = static_cast<uint16_t>(candidateX);
    const uint64_t z = static_cast<uint16_t>(candidateZ);
    return (static_cast<uint64_t>(worldSeed) << 32u) | (x << 16u) | z;
}

uint64_t foliageCarpetStableId(uint32_t worldSeed, int candidateX,
                               int candidateZ)
{
    constexpr int kCoordinateBias = 512;
    constexpr uint32_t kCoordinateMask = 0x7ffu;
    constexpr uint32_t kCarpetNamespace = 0x40000000u;
    const uint32_t x = static_cast<uint32_t>(candidateX + kCoordinateBias) &
                       kCoordinateMask;
    const uint32_t z = static_cast<uint32_t>(candidateZ + kCoordinateBias) &
                       kCoordinateMask;
    const uint32_t packed = kCarpetNamespace | (x << 11u) | z;
    return (static_cast<uint64_t>(worldSeed) << 32u) | packed;
}

uint64_t foliageAquaticStableId(uint32_t worldSeed, int candidateX,
                                int candidateZ)
{
    constexpr int kCoordinateBias = 512;
    constexpr uint32_t kCoordinateMask = 0x7ffu;
    constexpr uint32_t kAquaticNamespace = 0x80000000u;
    const uint32_t x = static_cast<uint32_t>(candidateX + kCoordinateBias) &
                       kCoordinateMask;
    const uint32_t z = static_cast<uint32_t>(candidateZ + kCoordinateBias) &
                       kCoordinateMask;
    const uint32_t packed = kAquaticNamespace | (x << 11u) | z;
    return (static_cast<uint64_t>(worldSeed) << 32u) | packed;
}

uint64_t foliageTreeCanopyStableId(uint32_t worldSeed,
                                   uint32_t lobeIndex,
                                   uint32_t sprigIndex)
{
    constexpr uint32_t kTreeCanopyNamespace = 0xc0000000u;
    const uint32_t packed = kTreeCanopyNamespace |
                            ((lobeIndex & 0xffu) << 16u) |
                            (sprigIndex & 0xffffu);
    return (static_cast<uint64_t>(worldSeed) << 32u) | packed;
}

uint64_t foliageTreeCanopyPatchStableId(uint32_t worldSeed,
                                        uint32_t lobeIndex)
{
    constexpr uint32_t kTreeCanopyPatchNamespace = 0xe0000000u;
    return (static_cast<uint64_t>(worldSeed) << 32u) |
           kTreeCanopyPatchNamespace | (lobeIndex & 0xffffu);
}

float foliageResolvedMaxTipDisplacementMeters(
    const FoliageBladeInstance& instance)
{
    const FoliageSwayProfile& sway = foliageArchetype(foliageForm(instance)).sway;
    const float seedVariation = seedUnit(instance.randomSeed ^ 0xa511e9b3u);
    return sway.maxTipDisplacementMeters * (0.84f + 0.16f * seedVariation);
}

glm::vec3 foliageDisplacement(const FoliageBladeInstance& instance,
                              float normalizedHeight, float timeSeconds,
                              const engine::scene::EnvironmentWindSettings& windSettings,
                              float swayStrength)
{
    const float t = std::clamp(normalizedHeight, 0.0f, 1.0f);
    if (t <= 0.0f)
    {
        return glm::vec3(0.0f);
    }

    const FoliageSwayProfile& sway = foliageArchetype(foliageForm(instance)).sway;
    const engine::scene::EnvironmentWindSettings wind =
        engine::scene::sanitizeEnvironmentWindSettings(windSettings);
    const engine::scene::EnvironmentWindWaveShape& field =
        engine::scene::defaultEnvironmentWindWaveShape();
    const float flexibleHeight = std::max(1.0f - sway.rootRigidity, 1e-5f);
    const float bendHeight = std::clamp(
        (t - sway.rootRigidity) / flexibleHeight, 0.0f, 1.0f);
    const float bend = std::pow(bendHeight, sway.bendExponent);
    const float gust = engine::scene::environmentWindGustMultiplierFromPhase(
        wind, instance.phaseRadians, timeSeconds);
    const float amplitude = foliageResolvedMaxTipDisplacementMeters(instance) *
                            std::max(swayStrength, 0.0f) * wind.strength * gust;
    const float speed = sway.angularSpeedRadiansPerSecond;
    const float scaledTime = timeSeconds * wind.speed;
    const float primary = std::sin(scaledTime * speed + instance.phaseRadians);
    const float secondary =
        field.secondaryWeight *
        std::sin(scaledTime * speed * field.secondaryTimeScale +
                 instance.phaseRadians * field.secondarySpatialScale +
                 field.secondaryPhaseOffsetRadians);
    const float wave =
        (primary + secondary) / (1.0f + std::abs(field.secondaryWeight));

    const glm::vec2 crossWind(-wind.direction.y, wind.direction.x);
    const float crossWave =
        engine::scene::environmentWindFoliageCrossWeight(
            wind, field.crossWeight) *
        std::sin(scaledTime * speed * field.crossTimeScale +
                 instance.phaseRadians * field.crossSpatialScale +
                 field.crossPhaseOffsetRadians);
    const glm::vec2 animated =
        (wind.direction * wave + crossWind * crossWave) * amplitude * bend;
    return glm::vec3(animated.x, 0.0f, animated.y);
}

float foliageConservativeDisplacement(const FoliageBladeInstance& instance,
                                      const engine::scene::EnvironmentWindSettings& wind,
                                      float swayStrength)
{
    return std::abs(instance.restLeanMeters) +
           foliageResolvedMaxTipDisplacementMeters(instance) *
               std::max(swayStrength, 0.0f) *
               engine::scene::environmentWindConservativeAmplitudeScale(wind);
}

} // namespace engine::game
