#pragma once

#include <cstdint>
#include <string_view>

#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

namespace engine::render
{

// bounded, texture-free sky presentation layered over the authored scene sky
// color. strength zero is the exact compatibility path.
struct PaintedSkySettings
{
    float strength = 0.0f;
    glm::vec3 horizonTint{1.0f};
    glm::vec3 zenithTint{1.0f};
    glm::vec3 lowerHemisphereTint{1.0f};
    float gradientExponent = 1.0f;
    float horizonBandStrength = 0.0f;
    float horizonBandExponent = 4.0f;
    float sunDiscAngularRadius = 0.0105f;
    float sunDiscSoftness = 0.0020f;
    float sunDiscIntensity = 0.0f;
    float sunHaloIntensity = 0.0f;
    float sunHaloExponent = 48.0f;

    [[nodiscard]] bool enabled() const { return strength > 0.0f; }
    bool operator==(const PaintedSkySettings&) const = default;
};

enum class PaintedSkyPreset : uint8_t
{
    CompatibilityOff,
    SoftDayV0,
    WarmSunsetV0,
    NightV0,
    DawnV0,
};

[[nodiscard]] std::string_view paintedSkyPresetName(PaintedSkyPreset preset);
[[nodiscard]] PaintedSkySettings makePaintedSkySettings(PaintedSkyPreset preset);
[[nodiscard]] PaintedSkySettings sanitizePaintedSkySettings(
    const PaintedSkySettings& settings);

// std140-compatible packing consumed by LightingPass. keeping the sky data in
// the existing per-frame ubo avoids another pass, descriptor, texture, or draw.
struct PaintedSkyGpuData
{
    // xyz=horizon tint, w=strength
    glm::vec4 sky0{1.0f, 1.0f, 1.0f, 0.0f};
    // xyz=zenith tint, w=gradient exponent
    glm::vec4 sky1{1.0f, 1.0f, 1.0f, 1.0f};
    // xyz=lower-hemisphere tint, w=horizon-band strength
    glm::vec4 sky2{1.0f, 1.0f, 1.0f, 0.0f};
    // x=disc radius, y=disc softness, z=disc intensity, w=halo intensity
    glm::vec4 sky3{0.0105f, 0.0020f, 0.0f, 0.0f};
    // x=halo exponent, y=horizon-band exponent
    glm::vec4 sky4{48.0f, 4.0f, 0.0f, 0.0f};

    bool operator==(const PaintedSkyGpuData&) const = default;
};

[[nodiscard]] PaintedSkyGpuData packPaintedSkyGpuData(
    const PaintedSkySettings& settings);

// cpu reference for the shader evaluator. atmosphere remains a separate layer
// and is intentionally not included here.
[[nodiscard]] glm::vec3 evaluatePaintedSky(
    const PaintedSkySettings& settings, const glm::vec3& authoredSkyColor,
    const glm::vec3& viewDirection, const glm::vec3& directionToSun,
    const glm::vec3& sunColor);

} // namespace engine::render
