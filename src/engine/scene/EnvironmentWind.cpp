#include "engine/scene/EnvironmentWind.h"

#include <algorithm>
#include <cmath>

#include <glm/geometric.hpp>

namespace engine::scene
{
namespace
{
constexpr float kTwoPi = 6.28318530717958647692f;
constexpr glm::vec2 kDefaultDirection{0.8f, 0.6f};
constexpr glm::vec2 kGustSpatialFrequency{0.09125f, 0.05125f};
constexpr glm::vec2 kTurbulenceSpatialFrequency{-0.07875f, 0.11875f};
constexpr float kTurbulenceTimeScale = 0.73f;
constexpr float kTurbulenceDirectionScale = 0.35f;
constexpr float kFoliageTurbulenceWeightScale = 0.20f;

constexpr EnvironmentWindSettings kDefaultSettings{};
constexpr EnvironmentWindWaveShape kDefaultWaveShape{};

float finiteOr(float value, float fallback)
{
    return std::isfinite(value) ? value : fallback;
}

float finiteTime(float timeSeconds)
{
    return std::isfinite(timeSeconds) ? timeSeconds : 0.0f;
}
} // namespace

const EnvironmentWindSettings& defaultEnvironmentWindSettings()
{
    return kDefaultSettings;
}

const EnvironmentWindWaveShape& defaultEnvironmentWindWaveShape()
{
    return kDefaultWaveShape;
}

EnvironmentWindSettings sanitizeEnvironmentWindSettings(
    const EnvironmentWindSettings& settings)
{
    EnvironmentWindSettings result = settings;
    result.direction.x = finiteOr(result.direction.x, kDefaultDirection.x);
    result.direction.y = finiteOr(result.direction.y, kDefaultDirection.y);
    const float directionScale =
        std::max(std::abs(result.direction.x), std::abs(result.direction.y));
    if (directionScale > 1e-5f)
    {
        const glm::vec2 scaledDirection = result.direction / directionScale;
        const float scaledLength =
            std::hypot(scaledDirection.x, scaledDirection.y);
        result.direction = scaledDirection / scaledLength;
    }
    else
    {
        result.direction = kDefaultDirection;
    }
    result.speed = std::clamp(finiteOr(result.speed, 1.0f), 0.0f, 8.0f);
    result.strength = std::clamp(finiteOr(result.strength, 1.0f), 0.0f, 2.0f);
    result.gustStrength =
        std::clamp(finiteOr(result.gustStrength, 0.0f), 0.0f, 1.0f);
    result.gustFrequencyHz =
        std::clamp(finiteOr(result.gustFrequencyHz, 0.12f), 0.01f, 2.0f);
    result.turbulenceStrength =
        std::clamp(finiteOr(result.turbulenceStrength, 0.0f), 0.0f, 1.0f);
    result.verticalLift =
        std::clamp(finiteOr(result.verticalLift, 0.0f), -2.0f, 2.0f);
    return result;
}

bool environmentWindSettingsEquivalent(const EnvironmentWindSettings& lhs,
                                       const EnvironmentWindSettings& rhs,
                                       float epsilon)
{
    const float tolerance = std::max(epsilon, 0.0f);
    const auto close = [tolerance](float a, float b) {
        return std::abs(a - b) <= tolerance;
    };
    return close(lhs.direction.x, rhs.direction.x) &&
           close(lhs.direction.y, rhs.direction.y) &&
           close(lhs.speed, rhs.speed) && close(lhs.strength, rhs.strength) &&
           close(lhs.gustStrength, rhs.gustStrength) &&
           close(lhs.gustFrequencyHz, rhs.gustFrequencyHz) &&
           close(lhs.turbulenceStrength, rhs.turbulenceStrength) &&
           close(lhs.verticalLift, rhs.verticalLift);
}

float environmentWindGustMultiplier(const EnvironmentWindSettings& settings,
                                    const glm::vec2& worldXZ,
                                    float timeSeconds)
{
    const glm::vec2 finiteWorldXZ(finiteOr(worldXZ.x, 0.0f),
                                  finiteOr(worldXZ.y, 0.0f));
    return environmentWindGustMultiplierFromPhase(
        settings, glm::dot(finiteWorldXZ, kGustSpatialFrequency), timeSeconds);
}

float environmentWindGustMultiplierFromPhase(
    const EnvironmentWindSettings& settings,
    float spatialPhase,
    float timeSeconds)
{
    const EnvironmentWindSettings wind =
        sanitizeEnvironmentWindSettings(settings);
    const float phase = finiteTime(timeSeconds) * kTwoPi * wind.gustFrequencyHz +
                        finiteOr(spatialPhase, 0.0f);
    return std::max(0.0f, 1.0f + wind.gustStrength * std::sin(phase));
}

float environmentWindTurbulenceSignal(const EnvironmentWindSettings& settings,
                                      const glm::vec2& worldXZ,
                                      float timeSeconds)
{
    const EnvironmentWindSettings wind =
        sanitizeEnvironmentWindSettings(settings);
    const glm::vec2 finiteWorldXZ(finiteOr(worldXZ.x, 0.0f),
                                  finiteOr(worldXZ.y, 0.0f));
    const float phase = finiteTime(timeSeconds) * kTwoPi *
                            wind.gustFrequencyHz * kTurbulenceTimeScale +
                        glm::dot(finiteWorldXZ, kTurbulenceSpatialFrequency) +
                        1.7f;
    return wind.turbulenceStrength * std::sin(phase);
}

EnvironmentWindSample sampleEnvironmentWind(
    const EnvironmentWindSettings& settings,
    const glm::vec3& worldPosition,
    float timeSeconds)
{
    const EnvironmentWindSettings wind =
        sanitizeEnvironmentWindSettings(settings);
    const glm::vec2 worldXZ(finiteOr(worldPosition.x, 0.0f),
                            finiteOr(worldPosition.z, 0.0f));
    const float gust = environmentWindGustMultiplier(wind, worldXZ, timeSeconds);
    const float turbulence =
        environmentWindTurbulenceSignal(wind, worldXZ, timeSeconds);
    const glm::vec2 cross(-wind.direction.y, wind.direction.x);
    const glm::vec2 perturbed =
        wind.direction + cross * (turbulence * kTurbulenceDirectionScale);
    const float perturbedLength = glm::length(perturbed);
    const glm::vec2 horizontalDirection =
        perturbedLength > 1e-5f ? perturbed / perturbedLength : wind.direction;

    EnvironmentWindSample sample{};
    sample.gustMultiplier = gust;
    sample.turbulenceSignal = turbulence;
    sample.intensity = wind.strength * gust;
    sample.velocity = glm::vec3(horizontalDirection.x * wind.speed * sample.intensity,
                                wind.verticalLift * sample.intensity,
                                horizontalDirection.y * wind.speed * sample.intensity);
    return sample;
}

float environmentWindFoliageCrossWeight(
    const EnvironmentWindSettings& settings,
    float baseCrossWeight)
{
    const EnvironmentWindSettings wind =
        sanitizeEnvironmentWindSettings(settings);
    return std::max(baseCrossWeight, 0.0f) +
           wind.turbulenceStrength * kFoliageTurbulenceWeightScale;
}

float environmentWindConservativeAmplitudeScale(
    const EnvironmentWindSettings& settings)
{
    const EnvironmentWindSettings wind =
        sanitizeEnvironmentWindSettings(settings);
    return wind.strength * (1.0f + wind.gustStrength) *
           (1.02f + wind.turbulenceStrength * kFoliageTurbulenceWeightScale);
}

} // namespace engine::scene
