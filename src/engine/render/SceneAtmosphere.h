#pragma once

#include <optional>
#include <span>
#include <string_view>

#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

namespace engine::render
{

// transient controls for the air-atmosphere model: exponential distance
// haze with height falloff and sun-direction inscatter. density zero is the exact
// compatibility configuration. ScenePresentationProfile v1 owns persistent scene
// values. the model applies only to the in-air path segment; water
// absorption/fog continues to own the underwater segment.
struct SceneAtmosphereSettings
{
    // extinction at baseHeight, per meter of air path.
    float density = 0.0f;
    // exponential falloff of extinction with height above baseHeight, per meter.
    float heightFalloff = 0.0f;
    // world-space height where density is nominal.
    float baseHeight = 0.0f;
    // 0..1 blend authority of the sun-facing inscatter tint over the sky tint.
    float sunPhaseStrength = 0.0f;
    // sharpness of the sun-facing lobe; higher concentrates warmth around the sun.
    float sunPhaseExponent = 4.0f;

    [[nodiscard]] bool enabled() const;
    bool operator==(const SceneAtmosphereSettings&) const = default;
};

enum class SceneAtmospherePreset
{
    Disabled,
    OutdoorHazeV0,
};

[[nodiscard]] std::optional<SceneAtmospherePreset>
sceneAtmospherePresetFromName(std::string_view name);
[[nodiscard]] std::string_view sceneAtmospherePresetName(SceneAtmospherePreset preset);
[[nodiscard]] SceneAtmosphereSettings
makeSceneAtmosphereSettings(SceneAtmospherePreset preset);

// stable gpu packing shared by every atmosphere-aware pass. lightTravelDirection
// points from the light toward the scene; shaders consume the opposite direction
// (from the shaded point toward the sun).
[[nodiscard]] inline glm::vec4
packSceneAtmosphereParameters(const SceneAtmosphereSettings& settings)
{
    return glm::vec4(settings.density, settings.heightFalloff, settings.baseHeight,
                     settings.sunPhaseStrength);
}

[[nodiscard]] inline glm::vec4
packSceneAtmosphereSun(const SceneAtmosphereSettings& settings,
                       const glm::vec3& lightTravelDirection)
{
    return glm::vec4(settings.sunPhaseExponent, -lightTravelDirection.x,
                     -lightTravelDirection.y, -lightTravelDirection.z);
}

struct SceneAtmosphereSample
{
    // fraction of surface radiance surviving the air segment (1 = clear air).
    float transmittance = 1.0f;
    // radiance added by the air segment, already weighted by (1 - transmittance).
    glm::vec3 inscatter{0.0f};
};

// a camera-ray interval owned by another participating medium (currently water).
// distances are measured from the camera along the normalized view ray. the air
// resolver clips, sorts, and merges intervals, so callers may provide volume
// intersections in any order and overlapping volumes are excluded only once.
struct SceneAtmosphereExcludedInterval
{
    float startDistance = 0.0f;
    float endDistance = 0.0f;
};

// optical depth of an air segment starting at originHeight, traveling with
// vertical direction component directionY for lengthMeters. analytic integral of
// the exponential height profile; the directionY -> 0 limit is handled exactly.
[[nodiscard]] float sceneAtmosphereOpticalDepth(const SceneAtmosphereSettings& settings,
                                                float originHeight, float directionY,
                                                float lengthMeters);

// integrates only the air complement of excluded medium intervals over a finite
// camera ray. this is the cpu contract mirrored by LightingPass for mixed
// air/water rays, including underwater-to-air paths.
[[nodiscard]] float sceneAtmosphereAirOpticalDepth(
    const SceneAtmosphereSettings& settings, const glm::vec3& rayOrigin,
    const glm::vec3& rayDirection, float lengthMeters,
    std::span<const SceneAtmosphereExcludedInterval> excludedIntervals);

// infinite-length optical depth used by sky rays. upward rays have a finite
// limit under positive height falloff; horizontal/downward rays return infinity.
[[nodiscard]] float sceneAtmosphereSkyOpticalDepth(
    const SceneAtmosphereSettings& settings, float originHeight, float directionY);

// directional inscatter tint: sky tint away from the sun blending toward the sun
// tint inside the phase lobe. pure direction term with no distance dependence.
[[nodiscard]] glm::vec3 sceneAtmosphereInscatterTint(
    const SceneAtmosphereSettings& settings, const glm::vec3& viewDirection,
    const glm::vec3& sunDirection, const glm::vec3& skyTint, const glm::vec3& sunTint);

// cpu reference for one air segment. shader stages mirror this exactly; the
// composed result is surfaceColor * transmittance + inscatter.
[[nodiscard]] SceneAtmosphereSample evaluateSceneAtmosphere(
    const SceneAtmosphereSettings& settings, const glm::vec3& segmentOrigin,
    const glm::vec3& viewDirection, float lengthMeters, const glm::vec3& sunDirection,
    const glm::vec3& skyTint, const glm::vec3& sunTint);

// builds the shared transmittance/inscatter response from an optical depth that
// was resolved by stage-specific path ownership.
[[nodiscard]] SceneAtmosphereSample evaluateSceneAtmosphereFromOpticalDepth(
    const SceneAtmosphereSettings& settings, float opticalDepth,
    const glm::vec3& viewDirection, const glm::vec3& sunDirection,
    const glm::vec3& skyTint, const glm::vec3& sunTint);

// Sky/horizon variant: the lengthMeters -> infinity limit of the same model, so
// far geometry and the sky background converge on identical haze.
[[nodiscard]] SceneAtmosphereSample evaluateSceneAtmosphereSky(
    const SceneAtmosphereSettings& settings, const glm::vec3& cameraPosition,
    const glm::vec3& viewDirection, const glm::vec3& sunDirection,
    const glm::vec3& skyTint, const glm::vec3& sunTint);

[[nodiscard]] glm::vec3 composeSceneAtmosphere(const glm::vec3& surfaceColor,
                                               const SceneAtmosphereSample& sample);

// water's refraction source has already received camera-path atmosphere in the
// opaque lighting pass. preserve that base (including its inscatter) and attenuate
// only the terms added by the transparent water surface. this prevents both a
// second extinction of the refracted scene and duplicate inscatter.
[[nodiscard]] glm::vec3 attenuateSceneAtmosphereSurfaceContribution(
    const SceneAtmosphereSettings& settings, float opticalDepth,
    const glm::vec3& alreadyAtmospheredBase,
    const glm::vec3& surfaceCompositedColor);

// procedural stars are emitted after the lit sky has already received atmosphere.
// attenuate only their emission by the resolved sky-path transmittance; do not add
// a second inscatter term. Disabled atmosphere and a zero-length air path preserve
// the input emission exactly.
[[nodiscard]] glm::vec3 attenuateSceneAtmosphereSkyEmission(
    const SceneAtmosphereSettings& settings, float opticalDepth,
    const glm::vec3& emission);

} // namespace engine::render
