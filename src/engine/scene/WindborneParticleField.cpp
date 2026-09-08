#include "engine/scene/WindborneParticleField.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numeric>

#include <glm/common.hpp>
#include <glm/geometric.hpp>

namespace engine::scene
{
namespace
{
constexpr float kTwoPi = 6.28318530717958647692f;

uint32_t mixBits(uint32_t value) noexcept
{
    value ^= value >> 16u;
    value *= 0x7feb352du;
    value ^= value >> 15u;
    value *= 0x846ca68bu;
    return value ^ (value >> 16u);
}

uint32_t candidateBits(uint32_t worldSeed, uint32_t stableId,
                       uint32_t stream) noexcept
{
    return mixBits(worldSeed ^ (stableId * 0x9e3779b9u) ^
                   (stream * 0x85ebca6bu));
}

float unitFloat(uint32_t bits) noexcept
{
    return static_cast<float>(bits >> 8u) * (1.0f / 16777216.0f);
}

bool finiteVec3(const glm::vec3& value) noexcept
{
    return std::isfinite(value.x) && std::isfinite(value.y) &&
           std::isfinite(value.z);
}

float finiteTime(float value) noexcept
{
    return std::isfinite(value) ? value : 0.0f;
}
} // namespace

bool validWindborneParticleDomain(
    const WindborneParticleDomain& domain) noexcept
{
    return finiteVec3(domain.minimum) && finiteVec3(domain.maximum) &&
           domain.minimum.x < domain.maximum.x &&
           domain.minimum.y < domain.maximum.y &&
           domain.minimum.z < domain.maximum.z &&
           domain.leafMaterialCount > 0u && domain.moteMaterialCount > 0u &&
           domain.leafMaterialBase < 256u && domain.moteMaterialBase < 256u &&
           domain.leafMaterialCount <= 256u - domain.leafMaterialBase &&
           domain.moteMaterialCount <= 256u - domain.moteMaterialBase;
}

std::vector<WindborneParticleCandidate> buildWindborneParticleField(
    const WindborneParticleDomain& domain, uint32_t worldSeed)
{
    if (!validWindborneParticleDomain(domain))
    {
        return {};
    }

    constexpr uint32_t kGridWidth = 8u;
    static_assert(kGridWidth * kGridWidth == kMaxWindborneParticleCount);
    const glm::vec3 extent = domain.maximum - domain.minimum;
    std::vector<WindborneParticleCandidate> result;
    result.reserve(kMaxWindborneParticleCount);

    for (uint32_t stableId = 0u; stableId < kMaxWindborneParticleCount;
         ++stableId)
    {
        const uint32_t cellX = stableId % kGridWidth;
        const uint32_t cellZ = stableId / kGridWidth;
        const float jitterX = unitFloat(candidateBits(worldSeed, stableId, 0u));
        const float jitterZ = unitFloat(candidateBits(worldSeed, stableId, 1u));
        const float height = unitFloat(candidateBits(worldSeed, stableId, 2u));

        WindborneParticleCandidate candidate{};
        candidate.stableId = stableId;
        candidate.anchorWorld = glm::vec3(
            domain.minimum.x +
                extent.x * (static_cast<float>(cellX) + 0.15f + jitterX * 0.70f) /
                    static_cast<float>(kGridWidth),
            domain.minimum.y + extent.y * (0.10f + height * 0.80f),
            domain.minimum.z +
                extent.z * (static_cast<float>(cellZ) + 0.15f + jitterZ * 0.70f) /
                    static_cast<float>(kGridWidth));
        candidate.phaseRadians =
            unitFloat(candidateBits(worldSeed, stableId, 3u)) * kTwoPi;
        candidate.selectionRank =
            unitFloat(candidateBits(worldSeed, stableId, 4u));
        candidate.kindRank = unitFloat(candidateBits(worldSeed, stableId, 5u));
        candidate.travelAmplitude =
            0.28f + unitFloat(candidateBits(worldSeed, stableId, 6u)) * 0.52f;
        candidate.motionSpeed =
            0.42f + unitFloat(candidateBits(worldSeed, stableId, 7u)) * 0.76f;
        candidate.flutter =
            0.45f + unitFloat(candidateBits(worldSeed, stableId, 8u)) * 0.55f;
        candidate.randomSeed = candidateBits(worldSeed, stableId, 9u);
        candidate.leafMaterialId = domain.leafMaterialBase +
            candidateBits(worldSeed, stableId, 10u) % domain.leafMaterialCount;
        candidate.moteMaterialId = domain.moteMaterialBase +
            candidateBits(worldSeed, stableId, 11u) % domain.moteMaterialCount;
        result.push_back(candidate);
    }

    std::sort(result.begin(), result.end(),
              [](const WindborneParticleCandidate& lhs,
                 const WindborneParticleCandidate& rhs) {
                  if (lhs.selectionRank != rhs.selectionRank)
                  {
                      return lhs.selectionRank < rhs.selectionRank;
                  }
                  return lhs.stableId < rhs.stableId;
              });
    for (uint32_t rank = 0u; rank < result.size(); ++rank)
    {
        result[rank].selectionRank =
            (static_cast<float>(rank) + 0.5f) /
            static_cast<float>(kMaxWindborneParticleCount);
    }
    return result;
}

uint32_t activeWindborneParticleCount(
    const WindborneParticleSettings& settings,
    uint32_t residentCandidateCount) noexcept
{
    const WindborneParticleSettings sanitized =
        sanitizeWindborneParticleSettings(settings);
    if (!sanitized.enabled || residentCandidateCount == 0u)
    {
        return 0u;
    }
    const uint32_t boundedResident =
        std::min(residentCandidateCount, kMaxWindborneParticleCount);
    uint32_t activeCount = 0u;
    for (uint32_t rank = 0u; rank < boundedResident; ++rank)
    {
        const float selectionRank =
            (static_cast<float>(rank) + 0.5f) /
            static_cast<float>(kMaxWindborneParticleCount);
        if (!(selectionRank < sanitized.amount))
        {
            break;
        }
        ++activeCount;
    }
    return activeCount;
}

WindborneParticleSample sampleWindborneParticle(
    const WindborneParticleCandidate& candidate,
    const WindborneParticleSettings& settings,
    const EnvironmentWindSettings& windSettings,
    float timeSeconds) noexcept
{
    const WindborneParticleSettings settingsValue =
        sanitizeWindborneParticleSettings(settings);
    const EnvironmentWindSettings wind =
        sanitizeEnvironmentWindSettings(windSettings);
    WindborneParticleSample sample{};
    sample.worldPosition = candidate.anchorWorld;
    sample.active = settingsValue.enabled &&
                    candidate.selectionRank < settingsValue.amount;
    sample.leaf = candidate.kindRank < settingsValue.leafFraction;
    if (!sample.active || !finiteVec3(candidate.anchorWorld))
    {
        return sample;
    }

    const float animationTime = finiteTime(timeSeconds) * wind.speed;
    const float phase = candidate.phaseRadians;
    const float cycle = animationTime * candidate.motionSpeed + phase;
    const float gust = environmentWindGustMultiplier(
        wind, glm::vec2(candidate.anchorWorld.x, candidate.anchorWorld.z),
        finiteTime(timeSeconds));
    const float amplitude = candidate.travelAmplitude * wind.strength * gust;
    const glm::vec2 direction = wind.direction;
    const glm::vec2 cross(-direction.y, direction.x);
    const float turbulence = environmentWindTurbulenceSignal(
        wind, glm::vec2(candidate.anchorWorld.x, candidate.anchorWorld.z),
        finiteTime(timeSeconds));
    const float along = std::sin(cycle) * amplitude;
    const float across =
        std::cos(cycle * 0.83f + 1.7f) * amplitude *
        (0.28f + std::abs(turbulence) * 0.25f);
    const float vertical =
        std::sin(cycle * (1.31f + candidate.flutter * 0.22f) + 2.4f) *
            amplitude * (0.22f + candidate.flutter * 0.16f) +
        std::sin(cycle * 0.47f + phase) * amplitude * wind.verticalLift * 0.18f;
    sample.worldPosition +=
        glm::vec3(direction.x * along + cross.x * across, vertical,
                  direction.y * along + cross.y * across);
    return sample;
}

bool windborneParticleFieldEquivalent(
    std::span<const WindborneParticleCandidate> lhs,
    std::span<const WindborneParticleCandidate> rhs, float epsilon) noexcept
{
    if (lhs.size() != rhs.size())
    {
        return false;
    }
    const float tolerance = std::max(epsilon, 0.0f);
    const auto close = [tolerance](float a, float b) {
        return std::abs(a - b) <= tolerance;
    };
    for (size_t index = 0; index < lhs.size(); ++index)
    {
        const WindborneParticleCandidate& a = lhs[index];
        const WindborneParticleCandidate& b = rhs[index];
        if (a.stableId != b.stableId || a.randomSeed != b.randomSeed ||
            a.leafMaterialId != b.leafMaterialId ||
            a.moteMaterialId != b.moteMaterialId ||
            !close(a.anchorWorld.x, b.anchorWorld.x) ||
            !close(a.anchorWorld.y, b.anchorWorld.y) ||
            !close(a.anchorWorld.z, b.anchorWorld.z) ||
            !close(a.phaseRadians, b.phaseRadians) ||
            !close(a.selectionRank, b.selectionRank) ||
            !close(a.kindRank, b.kindRank) ||
            !close(a.travelAmplitude, b.travelAmplitude) ||
            !close(a.motionSpeed, b.motionSpeed) || !close(a.flutter, b.flutter))
        {
            return false;
        }
    }
    return true;
}

} // namespace engine::scene
