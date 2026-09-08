#include "engine/render/SceneAtmosphere.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

#include <glm/geometric.hpp>

namespace engine::render
{
namespace
{

// below this vertical component the analytic integral switches to its exact
// horizontal limit to stay numerically stable.
constexpr float kVerticalEpsilon = 1e-5f;

} // namespace

bool SceneAtmosphereSettings::enabled() const
{
    return density > 0.0f;
}

std::optional<SceneAtmospherePreset> sceneAtmospherePresetFromName(std::string_view name)
{
    if (name == "disabled")
    {
        return SceneAtmospherePreset::Disabled;
    }
    if (name == "outdoor-haze-v0")
    {
        return SceneAtmospherePreset::OutdoorHazeV0;
    }
    return std::nullopt;
}

std::string_view sceneAtmospherePresetName(SceneAtmospherePreset preset)
{
    switch (preset)
    {
    case SceneAtmospherePreset::Disabled:
        return "disabled";
    case SceneAtmospherePreset::OutdoorHazeV0:
        return "outdoor-haze-v0";
    }
    return "disabled";
}

SceneAtmosphereSettings makeSceneAtmosphereSettings(SceneAtmospherePreset preset)
{
    SceneAtmosphereSettings settings{};
    switch (preset)
    {
    case SceneAtmospherePreset::Disabled:
        break;
    case SceneAtmospherePreset::OutdoorHazeV0:
        // lookdev candidate, not an accepted value set: subtle haze at pond scale
        // (~20-30 m views), meaningful buildup toward beach-scale horizons.
        settings.density = 0.010f;
        settings.heightFalloff = 0.070f;
        settings.baseHeight = 0.0f;
        settings.sunPhaseStrength = 0.60f;
        settings.sunPhaseExponent = 5.0f;
        break;
    }
    return settings;
}

float sceneAtmosphereOpticalDepth(const SceneAtmosphereSettings& settings,
                                  float originHeight, float directionY,
                                  float lengthMeters)
{
    if (!settings.enabled() || lengthMeters <= 0.0f)
    {
        return 0.0f;
    }

    const float densityAtOrigin =
        settings.density *
        std::exp(-settings.heightFalloff * (originHeight - settings.baseHeight));
    const float verticalRate = settings.heightFalloff * directionY;
    if (std::fabs(verticalRate) < kVerticalEpsilon)
    {
        return densityAtOrigin * lengthMeters;
    }
    return densityAtOrigin * (1.0f - std::exp(-verticalRate * lengthMeters)) /
           verticalRate;
}

float sceneAtmosphereAirOpticalDepth(
    const SceneAtmosphereSettings& settings, const glm::vec3& rayOrigin,
    const glm::vec3& rayDirection, float lengthMeters,
    std::span<const SceneAtmosphereExcludedInterval> excludedIntervals)
{
    if (!settings.enabled() || lengthMeters <= 0.0f)
    {
        return 0.0f;
    }

    const glm::vec3 direction = glm::normalize(rayDirection);
    std::vector<SceneAtmosphereExcludedInterval> intervals;
    intervals.reserve(excludedIntervals.size());
    for (const SceneAtmosphereExcludedInterval interval : excludedIntervals)
    {
        const float start = std::clamp(interval.startDistance, 0.0f, lengthMeters);
        const float end = std::clamp(interval.endDistance, 0.0f, lengthMeters);
        if (end > start)
        {
            intervals.push_back({start, end});
        }
    }
    std::sort(intervals.begin(), intervals.end(),
              [](const SceneAtmosphereExcludedInterval& lhs,
                 const SceneAtmosphereExcludedInterval& rhs) {
                  return lhs.startDistance < rhs.startDistance;
              });

    float opticalDepth = 0.0f;
    float airStart = 0.0f;
    for (const SceneAtmosphereExcludedInterval& interval : intervals)
    {
        if (interval.startDistance > airStart)
        {
            const float segmentLength = interval.startDistance - airStart;
            const float segmentOriginHeight = rayOrigin.y + direction.y * airStart;
            opticalDepth += sceneAtmosphereOpticalDepth(
                settings, segmentOriginHeight, direction.y, segmentLength);
        }
        airStart = std::max(airStart, interval.endDistance);
    }
    if (airStart < lengthMeters)
    {
        const float segmentOriginHeight = rayOrigin.y + direction.y * airStart;
        opticalDepth += sceneAtmosphereOpticalDepth(
            settings, segmentOriginHeight, direction.y, lengthMeters - airStart);
    }
    return opticalDepth;
}

float sceneAtmosphereSkyOpticalDepth(const SceneAtmosphereSettings& settings,
                                     float originHeight, float directionY)
{
    if (!settings.enabled())
    {
        return 0.0f;
    }

    const float verticalRate = settings.heightFalloff * directionY;
    if (verticalRate <= kVerticalEpsilon)
    {
        return std::numeric_limits<float>::infinity();
    }
    const float densityAtOrigin =
        settings.density *
        std::exp(-settings.heightFalloff * (originHeight - settings.baseHeight));
    return densityAtOrigin / verticalRate;
}

glm::vec3 sceneAtmosphereInscatterTint(const SceneAtmosphereSettings& settings,
                                       const glm::vec3& viewDirection,
                                       const glm::vec3& sunDirection,
                                       const glm::vec3& skyTint, const glm::vec3& sunTint)
{
    const float cosine = glm::dot(glm::normalize(viewDirection),
                                  glm::normalize(sunDirection));
    const float lobe = std::pow(std::clamp((cosine + 1.0f) * 0.5f, 0.0f, 1.0f),
                                std::max(settings.sunPhaseExponent, 1.0f));
    const float blend = std::clamp(settings.sunPhaseStrength, 0.0f, 1.0f) * lobe;
    return skyTint * (1.0f - blend) + sunTint * blend;
}

SceneAtmosphereSample evaluateSceneAtmosphere(const SceneAtmosphereSettings& settings,
                                              const glm::vec3& segmentOrigin,
                                              const glm::vec3& viewDirection,
                                              float lengthMeters,
                                              const glm::vec3& sunDirection,
                                              const glm::vec3& skyTint,
                                              const glm::vec3& sunTint)
{
    if (!settings.enabled() || lengthMeters <= 0.0f)
    {
        return {};
    }
    const glm::vec3 direction = glm::normalize(viewDirection);
    const float opticalDepth = sceneAtmosphereOpticalDepth(
        settings, segmentOrigin.y, direction.y, lengthMeters);
    return evaluateSceneAtmosphereFromOpticalDepth(
        settings, opticalDepth, direction, sunDirection, skyTint, sunTint);
}

SceneAtmosphereSample evaluateSceneAtmosphereFromOpticalDepth(
    const SceneAtmosphereSettings& settings, float opticalDepth,
    const glm::vec3& viewDirection, const glm::vec3& sunDirection,
    const glm::vec3& skyTint, const glm::vec3& sunTint)
{
    SceneAtmosphereSample sample{};
    if (!settings.enabled() || opticalDepth <= 0.0f)
    {
        return sample;
    }

    sample.transmittance = std::exp(-opticalDepth);
    const glm::vec3 tint =
        sceneAtmosphereInscatterTint(settings, viewDirection, sunDirection, skyTint, sunTint);
    sample.inscatter = tint * (1.0f - sample.transmittance);
    return sample;
}

SceneAtmosphereSample evaluateSceneAtmosphereSky(const SceneAtmosphereSettings& settings,
                                                 const glm::vec3& cameraPosition,
                                                 const glm::vec3& viewDirection,
                                                 const glm::vec3& sunDirection,
                                                 const glm::vec3& skyTint,
                                                 const glm::vec3& sunTint)
{
    SceneAtmosphereSample sample{};
    if (!settings.enabled())
    {
        return sample;
    }

    const glm::vec3 direction = glm::normalize(viewDirection);
    const float opticalDepth = sceneAtmosphereSkyOpticalDepth(
        settings, cameraPosition.y, direction.y);
    return evaluateSceneAtmosphereFromOpticalDepth(
        settings, opticalDepth, direction, sunDirection, skyTint, sunTint);
}

glm::vec3 composeSceneAtmosphere(const glm::vec3& surfaceColor,
                                 const SceneAtmosphereSample& sample)
{
    return surfaceColor * sample.transmittance + sample.inscatter;
}

glm::vec3 attenuateSceneAtmosphereSurfaceContribution(
    const SceneAtmosphereSettings& settings, float opticalDepth,
    const glm::vec3& alreadyAtmospheredBase,
    const glm::vec3& surfaceCompositedColor)
{
    if (!settings.enabled() || opticalDepth <= 0.0f)
    {
        return surfaceCompositedColor;
    }

    const float transmittance = std::exp(-opticalDepth);
    return alreadyAtmospheredBase +
           (surfaceCompositedColor - alreadyAtmospheredBase) * transmittance;
}

glm::vec3 attenuateSceneAtmosphereSkyEmission(
    const SceneAtmosphereSettings& settings, float opticalDepth,
    const glm::vec3& emission)
{
    if (!settings.enabled() || opticalDepth <= 0.0f)
    {
        return emission;
    }

    return emission * std::exp(-opticalDepth);
}

} // namespace engine::render
