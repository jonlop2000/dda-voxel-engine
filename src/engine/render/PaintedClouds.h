#pragma once

#include <cstdint>
#include <string_view>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

namespace engine::render
{

// fixed visual-style constant shared conceptually with painted_clouds.glsl.
// it snaps only the analytic domain; it adds no samples or gpu resources.
inline constexpr float kPaintedCloudFacetsPerWorldScale = 32.0f;

// one bounded, world-anchored cloud layer evaluated only for sky pixels in the
// existing lighting pass. strength zero is the exact compatibility path.
struct PaintedCloudSettings
{
    float strength = 0.0f;
    float coverage = 0.46f;
    float opacity = 0.76f;
    float softness = 0.085f;
    float altitude = 90.0f;
    float worldScale = 105.0f;
    float detailStrength = 0.38f;
    glm::vec3 lightTint{1.08f, 1.06f, 1.00f};
    glm::vec3 shadowTint{0.64f, 0.72f, 0.82f};
    float silverLiningStrength = 0.16f;
    float horizonFadeStart = 0.035f;
    float horizonFadeEnd = 0.18f;

    [[nodiscard]] bool enabled() const { return strength > 0.0f; }
    bool operator==(const PaintedCloudSettings&) const = default;
};

enum class PaintedCloudPreset : uint8_t
{
    CompatibilityOff,
    SoftDayV0,
    SparseDayV0,
    OvercastV0,
};

[[nodiscard]] std::string_view paintedCloudPresetName(PaintedCloudPreset preset);
[[nodiscard]] PaintedCloudSettings makePaintedCloudSettings(
    PaintedCloudPreset preset);
[[nodiscard]] PaintedCloudSettings sanitizePaintedCloudSettings(
    const PaintedCloudSettings& settings);

// dynamic inputs remain outside presentation-profile ownership. the app routes
// the scene-owned wind-001 field and stable world seed here each frame.
struct PaintedCloudFrameInputs
{
    glm::vec2 windDirection{0.8f, 0.6f};
    float windSpeed = 1.0f;
    float windStrength = 1.0f;
    float animationTime = 0.0f;
    uint32_t worldSeed = 0u;
};

// std140-compatible packing consumed by LightingPass. cloud-001 has a hard
// ceiling of five vec4 values and owns no gpu resource beyond this ubo growth.
struct PaintedCloudGpuData
{
    // xyz=light tint, w=strength
    glm::vec4 cloud0{1.08f, 1.06f, 1.00f, 0.0f};
    // xyz=shadow tint, w=coverage
    glm::vec4 cloud1{0.64f, 0.72f, 0.82f, 0.46f};
    // x=altitude, y=world scale, z=opacity, w=softness
    glm::vec4 cloud2{90.0f, 105.0f, 0.76f, 0.085f};
    // x=detail, y=silver lining, z=horizon start, w=horizon end
    glm::vec4 cloud3{0.38f, 0.16f, 0.035f, 0.18f};
    // xy=wind-001 velocity, z=animation time, w=stable world seed
    glm::vec4 cloud4{0.0f};

    bool operator==(const PaintedCloudGpuData&) const = default;
};

[[nodiscard]] PaintedCloudGpuData packPaintedCloudGpuData(
    const PaintedCloudSettings& settings,
    const PaintedCloudFrameInputs& frameInputs);

struct PaintedCloudEvaluation
{
    glm::vec3 color{0.0f};
    float opacity = 0.0f;
};

// cpu reference for the fixed three-octave shader evaluator. the input color is
// the already-painted sky; atmosphere remains the next separate composition.
[[nodiscard]] PaintedCloudEvaluation evaluatePaintedClouds(
    const PaintedCloudSettings& settings,
    const PaintedCloudFrameInputs& frameInputs,
    const glm::vec3& skyColor,
    const glm::vec3& cameraPosition,
    const glm::vec3& viewDirection,
    const glm::vec3& directionToSun);

} // namespace engine::render
