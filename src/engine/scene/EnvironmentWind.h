#pragma once

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

namespace engine::scene
{

// scene-owned production wind shared by rooted foliage and future airborne
// effects. defaults intentionally reproduce the established foliage motion:
// gusts, additional turbulence, and vertical lift are opt-in.
struct EnvironmentWindSettings
{
    glm::vec2 direction{0.8f, 0.6f};
    float speed = 1.0f;
    float strength = 1.0f;
    float gustStrength = 0.0f;
    float gustFrequencyHz = 0.12f;
    float turbulenceStrength = 0.0f;
    float verticalLift = 0.0f;
};

// coherent secondary-wave shape shared by the cpu foliage reference and its
// gpu implementation. these are implementation constants rather than scene
// look controls, so all wind consumers retain one stable motion language.
struct EnvironmentWindWaveShape
{
    float alongPhaseRadiansPerMeter = 0.42f;
    float crossPhaseRadiansPerMeter = 0.16f;
    float secondaryTimeScale = 0.55f;
    float secondarySpatialScale = 0.67f;
    float secondaryWeight = 0.16f;
    float secondaryPhaseOffsetRadians = 1.7f;
    float crossTimeScale = 0.72f;
    float crossSpatialScale = 0.43f;
    float crossWeight = 0.10f;
    float crossPhaseOffsetRadians = 2.4f;
};

struct EnvironmentWindSample
{
    glm::vec3 velocity{0.0f};
    float intensity = 0.0f;
    float gustMultiplier = 1.0f;
    float turbulenceSignal = 0.0f;
};

[[nodiscard]] const EnvironmentWindSettings& defaultEnvironmentWindSettings();
[[nodiscard]] const EnvironmentWindWaveShape& defaultEnvironmentWindWaveShape();
[[nodiscard]] EnvironmentWindSettings sanitizeEnvironmentWindSettings(
    const EnvironmentWindSettings& settings);
[[nodiscard]] bool environmentWindSettingsEquivalent(
    const EnvironmentWindSettings& lhs,
    const EnvironmentWindSettings& rhs,
    float epsilon = 1e-5f);

// pure deterministic field queries. the same world position and time always
// return the same result; no frame index or mutable random state is involved.
[[nodiscard]] float environmentWindGustMultiplier(
    const EnvironmentWindSettings& settings,
    const glm::vec2& worldXZ,
    float timeSeconds);
// foliage geometry transports one stable phase per authored patch. this query
// keeps every expanded primitive in that patch on the same gust envelope.
[[nodiscard]] float environmentWindGustMultiplierFromPhase(
    const EnvironmentWindSettings& settings,
    float spatialPhase,
    float timeSeconds);
[[nodiscard]] float environmentWindTurbulenceSignal(
    const EnvironmentWindSettings& settings,
    const glm::vec2& worldXZ,
    float timeSeconds);
[[nodiscard]] EnvironmentWindSample sampleEnvironmentWind(
    const EnvironmentWindSettings& settings,
    const glm::vec3& worldPosition,
    float timeSeconds);

// rooted foliage ignores vertical lift but consumes every horizontal control.
[[nodiscard]] float environmentWindFoliageCrossWeight(
    const EnvironmentWindSettings& settings,
    float baseCrossWeight);
[[nodiscard]] float environmentWindConservativeAmplitudeScale(
    const EnvironmentWindSettings& settings);

} // namespace engine::scene
