#include "engine/scene/SunroofLivingWater.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

#include "Utils/SpatialHash.h"

namespace engine::scene
{
namespace
{
constexpr float kTau = 6.28318530718f;

[[nodiscard]] bool validBand(uint8_t base, uint8_t count) noexcept
{
    return count > 0 && static_cast<unsigned int>(base) + count <=
                            static_cast<unsigned int>(
                                std::numeric_limits<uint8_t>::max()) +
                                1u;
}

[[nodiscard]] float hashUnit(uint32_t seed) noexcept
{
    uint32_t value = seed;
    value ^= value >> 16u;
    value *= 0x7feb352du;
    value ^= value >> 15u;
    value *= 0x846ca68bu;
    value ^= value >> 16u;
    return static_cast<float>(value & 0x00ffffffu) / 16777215.0f;
}

[[nodiscard]] uint8_t bandId(uint8_t base, uint8_t count, uint32_t seed,
                             float lowerFraction = 0.0f) noexcept
{
    const uint32_t first = std::min<uint32_t>(
        count - 1u,
        static_cast<uint32_t>(std::ceil(lowerFraction * count)));
    const uint32_t usable = std::max<uint32_t>(1u, count - first);
    return static_cast<uint8_t>(base + first + seed % usable);
}

[[nodiscard]] float wrapUnit(float value) noexcept
{
    return value - std::floor(value);
}

} // namespace

std::vector<engine::game::FoliageBladeInstance>
buildSunroofLivingFoliage(const glm::ivec3& dims, const glm::vec3& position,
                          const glm::vec3& scale,
                          const SunroofLivingFoliagePalette& palette,
                          uint32_t seed)
{
    std::vector<engine::game::FoliageBladeInstance> result;
    if (dims.x < 24 || dims.z < 24 || scale.x <= 0.0f || scale.y <= 0.0f ||
        scale.z <= 0.0f || !validBand(palette.stemBase, palette.stemCount) ||
        !validBand(palette.secondaryBase, palette.secondaryCount) ||
        !validBand(palette.flowerBase, palette.flowerCount))
    {
        return result;
    }

    // authored normalized anchors keep the center/navigation lens readable and
    // form a few recognizable gardens instead of uniformly sprinkling motion.
    constexpr std::array<glm::vec2, 24> kAnchors{{
        {0.16f, 0.18f}, {0.23f, 0.23f}, {0.31f, 0.18f},
        {0.68f, 0.20f}, {0.77f, 0.24f}, {0.84f, 0.18f},
        {0.13f, 0.48f}, {0.21f, 0.55f}, {0.31f, 0.61f},
        {0.70f, 0.52f}, {0.79f, 0.59f}, {0.87f, 0.48f},
        {0.17f, 0.80f}, {0.27f, 0.76f}, {0.36f, 0.84f},
        {0.64f, 0.82f}, {0.74f, 0.76f}, {0.84f, 0.82f},
        {0.38f, 0.30f}, {0.61f, 0.33f}, {0.37f, 0.69f},
        {0.63f, 0.68f}, {0.10f, 0.68f}, {0.90f, 0.70f},
    }};

    result.reserve(kAnchors.size());
    const engine::scene::EnvironmentWindSettings& wind =
        engine::scene::defaultEnvironmentWindSettings();
    const engine::scene::EnvironmentWindWaveShape& windShape =
        engine::scene::defaultEnvironmentWindWaveShape();
    const glm::vec2 windDirection = wind.direction;
    const glm::vec2 crossWind(-windDirection.y, windDirection.x);

    for (uint32_t index = 0; index < kAnchors.size(); ++index)
    {
        const uint32_t patchSeed = SpatialHash::hash3D(
            static_cast<int>(seed & 0x7fffu) + static_cast<int>(index) * 17,
            43 + static_cast<int>((seed >> 11u) & 0x7fffu),
            static_cast<int>(index) * 29 - static_cast<int>((seed >> 22u) & 0x3ffu));
        const float jitterX = (hashUnit(patchSeed ^ 0x68bc21ebu) - 0.5f) * 0.018f;
        const float jitterZ = (hashUnit(patchSeed ^ 0x02e5be93u) - 0.5f) * 0.018f;
        const float normalizedX = std::clamp(kAnchors[index].x + jitterX, 0.06f, 0.94f);
        const float normalizedZ = std::clamp(kAnchors[index].y + jitterZ, 0.06f, 0.94f);
        const glm::vec3 root(
            position.x + normalizedX * static_cast<float>(dims.x) * scale.x,
            position.y,
            position.z + normalizedZ * static_cast<float>(dims.z) * scale.z);

        engine::game::FoliagePatchKind kind =
            engine::game::FoliagePatchKind::FlowerCluster;
        if (index % 6u == 4u)
        {
            kind = engine::game::FoliagePatchKind::ReedCluster;
        }
        else if (index % 6u == 5u)
        {
            kind = engine::game::FoliagePatchKind::Shrub;
        }

        engine::game::FoliageBladeInstance instance{};
        instance.stableId =
            (static_cast<uint64_t>(seed) << 32u) | 0x20000000u | (index + 1u);
        instance.stablePatchId = instance.stableId;
        instance.rootWorld = root;
        instance.patchRootWorld = root;
        instance.heightMeters = kind == engine::game::FoliagePatchKind::ReedCluster
                                    ? 1.18f
                                    : (kind == engine::game::FoliagePatchKind::FlowerCluster
                                           ? 1.02f
                                           : 0.72f);
        instance.cellSizeMeters = 0.18f;
        instance.patchRadiusMeters =
            kind == engine::game::FoliagePatchKind::ReedCluster ? 0.46f : 0.38f;
        instance.restLeanMeters = 0.04f;
        instance.yawRadians =
            static_cast<float>(patchSeed & 3u) * (kTau * 0.25f);
        const glm::vec2 rootXZ(root.x, root.z);
        instance.phaseRadians =
            glm::dot(rootXZ, windDirection) *
                windShape.alongPhaseRadiansPerMeter +
            glm::dot(rootXZ, crossWind) *
                windShape.crossPhaseRadiansPerMeter +
            (hashUnit(patchSeed ^ 0x9e3779b9u) - 0.5f) * 0.12f;
        instance.randomSeed = patchSeed ^ 0xa511e9b3u;
        instance.patchSeed = patchSeed;
        instance.stemMaterialId =
            bandId(palette.stemBase, palette.stemCount, patchSeed, 0.32f);
        instance.secondaryMaterialId = bandId(
            palette.secondaryBase, palette.secondaryCount,
            patchSeed ^ 0x4cf5ad43u, 0.28f);
        // the upper third contains coral, lavender, cream, gold, and warm-pink
        // accents in the aquarium palette; cycle the full tier deterministically.
        instance.tipMaterialId = bandId(
            palette.flowerBase, palette.flowerCount,
            index * 5u + (patchSeed >> 20u), 0.66f);
        instance.flags = kind == engine::game::FoliagePatchKind::ReedCluster
                             ? engine::game::FoliageBladeReed
                             : (kind == engine::game::FoliagePatchKind::FlowerCluster
                                    ? engine::game::FoliageBladeFlower
                                    : engine::game::FoliageBladeNone);
        instance.patchKind = kind;
        instance.morphology =
            engine::game::foliageMorphologyForPatch(kind, patchSeed);
        result.push_back(instance);
    }

    return result;
}

std::vector<SunroofBubbleMote> buildSunroofBubbleMotes(
    const glm::vec3& boundsMin, const glm::vec3& boundsMax,
    float surfaceHeight, float timeSeconds,
    const SunroofBubbleControls& controls, uint32_t seed)
{
    std::vector<SunroofBubbleMote> result;
    const glm::vec3 extent = boundsMax - boundsMin;
    const float top = std::min(boundsMax.y, surfaceHeight) - 0.30f;
    const float bottom = boundsMin.y + 0.45f;
    if (extent.x <= 1.0f || extent.z <= 1.0f || top <= bottom)
    {
        return result;
    }

    constexpr std::array<glm::vec2, 3> kStreamCenters{{
        {0.24f, 0.30f}, {0.70f, 0.42f}, {0.58f, 0.76f},
    }};
    constexpr uint32_t kMotesPerStream = 9u;
    const uint32_t activeMotesPerStream = static_cast<uint32_t>(std::lround(
        std::clamp(controls.amount, 0.0f, 1.0f) *
        static_cast<float>(kMotesPerStream)));
    if (activeMotesPerStream == 0u)
    {
        return result;
    }
    const float sizeScale = std::clamp(controls.size, 0.5f, 1.75f);
    const float riseScale = std::clamp(controls.riseSpeed, 0.0f, 2.0f);
    const float driftScale = std::clamp(controls.drift, 0.0f, 2.0f);
    result.reserve(kStreamCenters.size() * activeMotesPerStream);
    const float verticalSpan = top - bottom;

    for (uint32_t stream = 0; stream < kStreamCenters.size(); ++stream)
    {
        for (uint32_t mote = 0; mote < activeMotesPerStream; ++mote)
        {
            const uint32_t moteSeed = SpatialHash::hash3D(
                static_cast<int>(seed & 0xffffu) + static_cast<int>(stream) * 71,
                static_cast<int>(mote) * 37 + 19,
                static_cast<int>((seed >> 16u) & 0xffffu) -
                    static_cast<int>(mote) * 53);
            const float riseSpeed = 0.018f + hashUnit(moteSeed ^ 0x51c3a447u) * 0.014f;
            const float phase =
                static_cast<float>(mote) / static_cast<float>(kMotesPerStream) +
                hashUnit(moteSeed ^ 0xd1b54a35u) * 0.08f;
            const float rise = wrapUnit(phase + timeSeconds * riseSpeed * riseScale);
            const float driftPhase = timeSeconds * (0.20f + stream * 0.035f) +
                                     hashUnit(moteSeed) * kTau;
            const float driftRadius = 0.10f + hashUnit(moteSeed ^ 0x31f142ebu) * 0.18f;
            const glm::vec2 center(
                boundsMin.x + kStreamCenters[stream].x * extent.x,
                boundsMin.z + kStreamCenters[stream].y * extent.z);

            SunroofBubbleMote bubble{};
            bubble.center = glm::vec3(
                center.x + std::sin(driftPhase) * driftRadius * driftScale,
                bottom + rise * verticalSpan,
                center.y + std::cos(driftPhase * 0.83f) * driftRadius * driftScale);
            const float sizeVariation = hashUnit(moteSeed ^ 0x85ebca6bu);
            bubble.scale = (0.055f + sizeVariation * 0.075f) * sizeScale;
            result.push_back(bubble);
        }
    }
    return result;
}

} // namespace engine::scene
