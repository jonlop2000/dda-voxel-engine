#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string_view>

namespace engine::render
{

enum class AmbientOcclusionDistanceMode : uint8_t
{
    AuthoredWorldDistance = 0,
    ProjectedScreenRadius = 1,
};

inline constexpr std::string_view ambientOcclusionDistanceModeName(
    AmbientOcclusionDistanceMode mode)
{
    switch (mode)
    {
        case AmbientOcclusionDistanceMode::ProjectedScreenRadius:
            return "projected-screen-radius";
        case AmbientOcclusionDistanceMode::AuthoredWorldDistance:
        default:
            return "authored-world-distance";
    }
}

inline constexpr std::optional<AmbientOcclusionDistanceMode>
ambientOcclusionDistanceModeFromName(std::string_view name)
{
    if (name == "authored-world-distance")
    {
        return AmbientOcclusionDistanceMode::AuthoredWorldDistance;
    }
    if (name == "projected-screen-radius")
    {
        return AmbientOcclusionDistanceMode::ProjectedScreenRadius;
    }
    return std::nullopt;
}

struct ProjectedAoDistanceTier
{
    float radiusPixels = 0.0f;
    float minimumWorldDistance = 2.0f;

    [[nodiscard]] bool enabled() const
    {
        return std::isfinite(radiusPixels) && radiusPixels > 0.0f &&
               std::isfinite(minimumWorldDistance) && minimumWorldDistance >= 0.0f;
    }
};

inline ProjectedAoDistanceTier projectedAoDistanceTier(
    AmbientOcclusionDistanceMode mode,
    float radiusPixels,
    float minimumWorldDistance)
{
    if (mode != AmbientOcclusionDistanceMode::ProjectedScreenRadius)
    {
        return {};
    }
    return {radiusPixels, minimumWorldDistance};
}

inline ProjectedAoDistanceTier resolveProjectedAoDistanceTier(
    const ProjectedAoDistanceTier& configuredTier,
    const ProjectedAoDistanceTier& automationOverride)
{
    return automationOverride.enabled() ? automationOverride : configuredTier;
}

inline float projectedAoDistanceScale(const ProjectedAoDistanceTier& tier,
                                      float verticalFovRadians,
                                      float framebufferHeight)
{
    if (!tier.enabled() || !std::isfinite(verticalFovRadians) ||
        verticalFovRadians <= 0.0f || verticalFovRadians >= 3.14159265f ||
        !std::isfinite(framebufferHeight) || framebufferHeight <= 0.0f)
    {
        return 0.0f;
    }
    return 2.0f * std::tan(verticalFovRadians * 0.5f) * tier.radiusPixels /
           framebufferHeight;
}

inline float projectedAoTraceDistance(float authoredMaxDistance,
                                      float cameraDistance,
                                      float projectedDistanceScale,
                                      float minimumWorldDistance)
{
    if (!std::isfinite(authoredMaxDistance) || authoredMaxDistance <= 0.0f)
    {
        return 0.0f;
    }
    if (!std::isfinite(cameraDistance) || cameraDistance < 0.0f ||
        !std::isfinite(projectedDistanceScale) || projectedDistanceScale <= 0.0f)
    {
        return authoredMaxDistance;
    }
    const float safeMinimum =
        std::isfinite(minimumWorldDistance) ?
            std::clamp(minimumWorldDistance, 0.0f, authoredMaxDistance) :
            0.0f;
    return std::clamp(cameraDistance * projectedDistanceScale,
                      safeMinimum, authoredMaxDistance);
}

// spatial ao is a local contact effect. a quadratic falloff keeps near hits
// readable while allowing the trace-reach control to change the visible ao
// radius instead of making every hit inside a large radius nearly equivalent.
// the same contract is mirrored in shaders/ao/ao_ray.comp.
inline float ambientOcclusionDistanceFalloff(float hitDistance,
                                             float effectiveTraceDistance)
{
    if (!std::isfinite(hitDistance) || hitDistance < 0.0f ||
        !std::isfinite(effectiveTraceDistance) || effectiveTraceDistance <= 0.0f)
    {
        return 0.0f;
    }
    const float remaining =
        1.0f - std::clamp(hitDistance / effectiveTraceDistance, 0.0f, 1.0f);
    return remaining * remaining;
}

} // namespace engine::render
