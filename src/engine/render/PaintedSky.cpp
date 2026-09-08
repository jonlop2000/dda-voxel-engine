#include "engine/render/PaintedSky.h"

#include <algorithm>
#include <cmath>

#include <glm/common.hpp>
#include <glm/geometric.hpp>

namespace engine::render
{
namespace
{

float finiteOr(float value, float fallback)
{
    return std::isfinite(value) ? value : fallback;
}

glm::vec3 sanitizeTint(const glm::vec3& tint)
{
    return glm::clamp(glm::vec3(finiteOr(tint.x, 1.0f), finiteOr(tint.y, 1.0f),
                                finiteOr(tint.z, 1.0f)),
                      glm::vec3(0.0f), glm::vec3(4.0f));
}

glm::vec3 safeDirection(const glm::vec3& direction, const glm::vec3& fallback)
{
    const float lengthSquared = glm::dot(direction, direction);
    if (!std::isfinite(lengthSquared) || lengthSquared <= 1e-10f)
    {
        return fallback;
    }
    return direction / std::sqrt(lengthSquared);
}

float smoothstep(float edge0, float edge1, float value)
{
    if (edge0 == edge1)
    {
        return value < edge0 ? 0.0f : 1.0f;
    }
    const float t = std::clamp((value - edge0) / (edge1 - edge0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

} // namespace

std::string_view paintedSkyPresetName(PaintedSkyPreset preset)
{
    switch (preset)
    {
    case PaintedSkyPreset::CompatibilityOff:
        return "compatibility-off";
    case PaintedSkyPreset::SoftDayV0:
        return "soft-day-v0";
    case PaintedSkyPreset::WarmSunsetV0:
        return "warm-sunset-v0";
    case PaintedSkyPreset::NightV0:
        return "night-v0";
    case PaintedSkyPreset::DawnV0:
        return "dawn-v0";
    }
    return "compatibility-off";
}

PaintedSkySettings makePaintedSkySettings(PaintedSkyPreset preset)
{
    PaintedSkySettings settings{};
    switch (preset)
    {
    case PaintedSkyPreset::CompatibilityOff:
        break;
    case PaintedSkyPreset::SoftDayV0:
        settings.strength = 1.0f;
        settings.horizonTint = {1.16f, 1.08f, 0.92f};
        settings.zenithTint = {0.72f, 0.90f, 1.13f};
        settings.lowerHemisphereTint = {0.84f, 0.92f, 1.02f};
        settings.gradientExponent = 0.62f;
        settings.horizonBandStrength = 0.20f;
        settings.horizonBandExponent = 5.0f;
        settings.sunDiscIntensity = 1.15f;
        settings.sunHaloIntensity = 0.24f;
        settings.sunHaloExponent = 42.0f;
        break;
    case PaintedSkyPreset::WarmSunsetV0:
        settings.strength = 1.0f;
        settings.horizonTint = {1.32f, 0.83f, 0.58f};
        settings.zenithTint = {0.58f, 0.64f, 1.02f};
        settings.lowerHemisphereTint = {0.80f, 0.58f, 0.70f};
        settings.gradientExponent = 0.74f;
        settings.horizonBandStrength = 0.42f;
        settings.horizonBandExponent = 4.0f;
        settings.sunDiscAngularRadius = 0.0130f;
        settings.sunDiscSoftness = 0.0028f;
        settings.sunDiscIntensity = 1.55f;
        settings.sunHaloIntensity = 0.52f;
        settings.sunHaloExponent = 28.0f;
        break;
    case PaintedSkyPreset::NightV0:
        settings.strength = 1.0f;
        settings.horizonTint = {0.86f, 0.88f, 1.08f};
        settings.zenithTint = {0.44f, 0.56f, 0.92f};
        settings.lowerHemisphereTint = {0.50f, 0.55f, 0.78f};
        settings.gradientExponent = 0.76f;
        settings.horizonBandStrength = 0.08f;
        settings.horizonBandExponent = 5.0f;
        settings.sunDiscIntensity = 0.04f;
        settings.sunHaloIntensity = 0.05f;
        settings.sunHaloExponent = 52.0f;
        break;
    case PaintedSkyPreset::DawnV0:
        settings.strength = 1.0f;
        settings.horizonTint = {1.24f, 0.92f, 0.78f};
        settings.zenithTint = {0.62f, 0.76f, 1.08f};
        settings.lowerHemisphereTint = {0.76f, 0.68f, 0.86f};
        settings.gradientExponent = 0.70f;
        settings.horizonBandStrength = 0.34f;
        settings.horizonBandExponent = 4.5f;
        settings.sunDiscAngularRadius = 0.0120f;
        settings.sunDiscSoftness = 0.0025f;
        settings.sunDiscIntensity = 1.30f;
        settings.sunHaloIntensity = 0.40f;
        settings.sunHaloExponent = 32.0f;
        break;
    }
    return settings;
}

PaintedSkySettings sanitizePaintedSkySettings(const PaintedSkySettings& input)
{
    PaintedSkySettings settings = input;
    settings.strength = std::clamp(finiteOr(settings.strength, 0.0f), 0.0f, 1.0f);
    settings.horizonTint = sanitizeTint(settings.horizonTint);
    settings.zenithTint = sanitizeTint(settings.zenithTint);
    settings.lowerHemisphereTint = sanitizeTint(settings.lowerHemisphereTint);
    settings.gradientExponent =
        std::clamp(finiteOr(settings.gradientExponent, 1.0f), 0.10f, 8.0f);
    settings.horizonBandStrength =
        std::clamp(finiteOr(settings.horizonBandStrength, 0.0f), 0.0f, 2.0f);
    settings.horizonBandExponent =
        std::clamp(finiteOr(settings.horizonBandExponent, 4.0f), 0.10f, 16.0f);
    settings.sunDiscAngularRadius =
        std::clamp(finiteOr(settings.sunDiscAngularRadius, 0.0105f), 0.001f, 0.25f);
    settings.sunDiscSoftness =
        std::clamp(finiteOr(settings.sunDiscSoftness, 0.0020f), 0.0f,
                   settings.sunDiscAngularRadius * 0.95f);
    settings.sunDiscIntensity =
        std::clamp(finiteOr(settings.sunDiscIntensity, 0.0f), 0.0f, 16.0f);
    settings.sunHaloIntensity =
        std::clamp(finiteOr(settings.sunHaloIntensity, 0.0f), 0.0f, 8.0f);
    settings.sunHaloExponent =
        std::clamp(finiteOr(settings.sunHaloExponent, 48.0f), 1.0f, 1024.0f);
    return settings;
}

PaintedSkyGpuData packPaintedSkyGpuData(const PaintedSkySettings& input)
{
    const PaintedSkySettings settings = sanitizePaintedSkySettings(input);
    PaintedSkyGpuData gpu{};
    gpu.sky0 = glm::vec4(settings.horizonTint, settings.strength);
    gpu.sky1 = glm::vec4(settings.zenithTint, settings.gradientExponent);
    gpu.sky2 = glm::vec4(settings.lowerHemisphereTint,
                         settings.horizonBandStrength);
    gpu.sky3 = glm::vec4(settings.sunDiscAngularRadius,
                         settings.sunDiscSoftness, settings.sunDiscIntensity,
                         settings.sunHaloIntensity);
    gpu.sky4 = glm::vec4(settings.sunHaloExponent,
                         settings.horizonBandExponent, 0.0f, 0.0f);
    return gpu;
}

glm::vec3 evaluatePaintedSky(const PaintedSkySettings& input,
                             const glm::vec3& authoredSkyColor,
                             const glm::vec3& viewDirection,
                             const glm::vec3& directionToSun,
                             const glm::vec3& sunColor)
{
    const PaintedSkySettings settings = sanitizePaintedSkySettings(input);
    if (!settings.enabled())
    {
        return authoredSkyColor;
    }

    const glm::vec3 base = glm::max(authoredSkyColor, glm::vec3(0.0f));
    const glm::vec3 view = safeDirection(viewDirection, glm::vec3(0.0f, 1.0f, 0.0f));
    const glm::vec3 sun = safeDirection(directionToSun, glm::vec3(0.0f, 1.0f, 0.0f));

    const float upper = std::pow(std::max(view.y, 0.0f), settings.gradientExponent);
    const float lower = std::pow(std::max(-view.y, 0.0f), settings.gradientExponent);
    const glm::vec3 horizon = base * settings.horizonTint;
    const glm::vec3 upperColor = glm::mix(horizon, base * settings.zenithTint, upper);
    const glm::vec3 lowerColor =
        glm::mix(horizon, base * settings.lowerHemisphereTint, lower);
    glm::vec3 painted = view.y >= 0.0f ? upperColor : lowerColor;

    const float horizonBand =
        std::pow(std::max(1.0f - std::abs(view.y), 0.0f),
                 settings.horizonBandExponent);
    painted += base * glm::max(settings.horizonTint - glm::vec3(1.0f),
                               glm::vec3(0.0f)) *
               horizonBand * settings.horizonBandStrength;

    const float sunDot = std::clamp(glm::dot(view, sun), -1.0f, 1.0f);
    const float discInner =
        std::cos(std::max(settings.sunDiscAngularRadius - settings.sunDiscSoftness,
                          0.0f));
    const float discOuter =
        std::cos(settings.sunDiscAngularRadius + settings.sunDiscSoftness);
    const float disc = smoothstep(discOuter, discInner, sunDot);
    const float halo = std::pow(std::max(sunDot, 0.0f), settings.sunHaloExponent);
    painted += glm::max(sunColor, glm::vec3(0.0f)) *
               (disc * settings.sunDiscIntensity + halo * settings.sunHaloIntensity);
    return glm::mix(base, painted, settings.strength);
}

} // namespace engine::render
