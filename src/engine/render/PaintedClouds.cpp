#include "engine/render/PaintedClouds.h"

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

glm::vec3 sanitizeTint(const glm::vec3& tint, const glm::vec3& fallback)
{
    return glm::clamp(glm::vec3(finiteOr(tint.x, fallback.x),
                                finiteOr(tint.y, fallback.y),
                                finiteOr(tint.z, fallback.z)),
                      glm::vec3(0.0f), glm::vec3(4.0f));
}

glm::vec2 safeDirection(const glm::vec2& direction)
{
    const glm::vec2 finite(finiteOr(direction.x, 0.8f),
                           finiteOr(direction.y, 0.6f));
    const float lengthSquared = glm::dot(finite, finite);
    if (!std::isfinite(lengthSquared) || lengthSquared <= 1e-10f)
    {
        return glm::vec2(0.8f, 0.6f);
    }
    return finite / std::sqrt(lengthSquared);
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

float fract(float value)
{
    return value - std::floor(value);
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

float cloudHash(const glm::vec2& cell, float seed)
{
    return fract(std::sin(glm::dot(cell, glm::vec2(127.1f, 311.7f)) +
                          seed * 0.013f) *
                 43758.5453f);
}

float cloudValueNoise(const glm::vec2& point, float seed)
{
    const glm::vec2 cell(std::floor(point.x), std::floor(point.y));
    glm::vec2 local(fract(point.x), fract(point.y));
    local = local * local * (glm::vec2(3.0f) - 2.0f * local);
    const float a = cloudHash(cell, seed);
    const float b = cloudHash(cell + glm::vec2(1.0f, 0.0f), seed);
    const float c = cloudHash(cell + glm::vec2(0.0f, 1.0f), seed);
    const float d = cloudHash(cell + glm::vec2(1.0f), seed);
    return glm::mix(glm::mix(a, b, local.x), glm::mix(c, d, local.x), local.y);
}

struct CloudNoiseSample
{
    float shape = 0.0f;
    float detail = 0.0f;
};

CloudNoiseSample sampleFixedCloudNoise(glm::vec2 point, float seed,
                                       float detailStrength)
{
    const float octave0 = cloudValueNoise(point, seed);
    point = glm::vec2(0.80f * point.x + 0.60f * point.y,
                      -0.60f * point.x + 0.80f * point.y) *
                2.03f +
            glm::vec2(7.13f, -3.71f);
    const float octave1 = cloudValueNoise(point, seed + 19.0f);
    point = glm::vec2(0.80f * point.x + 0.60f * point.y,
                      -0.60f * point.x + 0.80f * point.y) *
                2.01f +
            glm::vec2(-5.37f, 11.21f);
    const float octave2 = cloudValueNoise(point, seed + 47.0f);

    const float baseShape = octave0 * 0.72f + octave1 * 0.20f + octave2 * 0.08f;
    const float detailed = baseShape +
                           (octave1 - 0.5f) * detailStrength * 0.20f +
                           (octave2 - 0.5f) * detailStrength * 0.10f;
    return {std::clamp(detailed, 0.0f, 1.0f), octave1};
}

glm::vec2 facetCloudDomain(const glm::vec2& point)
{
    return glm::floor(point * kPaintedCloudFacetsPerWorldScale +
                      glm::vec2(0.5f)) /
           kPaintedCloudFacetsPerWorldScale;
}

} // namespace

std::string_view paintedCloudPresetName(PaintedCloudPreset preset)
{
    switch (preset)
    {
    case PaintedCloudPreset::CompatibilityOff:
        return "compatibility-off";
    case PaintedCloudPreset::SoftDayV0:
        return "soft-day-v0";
    case PaintedCloudPreset::SparseDayV0:
        return "sparse-day-v0";
    case PaintedCloudPreset::OvercastV0:
        return "overcast-v0";
    }
    return "compatibility-off";
}

PaintedCloudSettings makePaintedCloudSettings(PaintedCloudPreset preset)
{
    PaintedCloudSettings settings{};
    switch (preset)
    {
    case PaintedCloudPreset::CompatibilityOff:
        break;
    case PaintedCloudPreset::SoftDayV0:
        settings.strength = 1.0f;
        break;
    case PaintedCloudPreset::SparseDayV0:
        settings.strength = 1.0f;
        settings.coverage = 0.28f;
        settings.opacity = 0.68f;
        settings.worldScale = 125.0f;
        settings.detailStrength = 0.32f;
        settings.silverLiningStrength = 0.20f;
        break;
    case PaintedCloudPreset::OvercastV0:
        settings.strength = 1.0f;
        settings.coverage = 0.72f;
        settings.opacity = 0.88f;
        settings.softness = 0.11f;
        settings.worldScale = 135.0f;
        settings.detailStrength = 0.48f;
        settings.lightTint = {0.93f, 0.96f, 1.00f};
        settings.shadowTint = {0.52f, 0.59f, 0.68f};
        settings.silverLiningStrength = 0.08f;
        break;
    }
    return settings;
}

PaintedCloudSettings sanitizePaintedCloudSettings(
    const PaintedCloudSettings& input)
{
    PaintedCloudSettings settings = input;
    settings.strength = std::clamp(finiteOr(settings.strength, 0.0f), 0.0f, 1.0f);
    settings.coverage = std::clamp(finiteOr(settings.coverage, 0.46f), 0.0f, 1.0f);
    settings.opacity = std::clamp(finiteOr(settings.opacity, 0.76f), 0.0f, 1.0f);
    settings.softness = std::clamp(finiteOr(settings.softness, 0.085f), 0.01f, 0.30f);
    settings.altitude = std::clamp(finiteOr(settings.altitude, 90.0f), 10.0f, 500.0f);
    settings.worldScale = std::clamp(finiteOr(settings.worldScale, 105.0f), 16.0f, 512.0f);
    settings.detailStrength =
        std::clamp(finiteOr(settings.detailStrength, 0.38f), 0.0f, 1.0f);
    settings.lightTint = sanitizeTint(settings.lightTint, {1.08f, 1.06f, 1.00f});
    settings.shadowTint = sanitizeTint(settings.shadowTint, {0.64f, 0.72f, 0.82f});
    settings.silverLiningStrength =
        std::clamp(finiteOr(settings.silverLiningStrength, 0.16f), 0.0f, 2.0f);
    settings.horizonFadeStart =
        std::clamp(finiteOr(settings.horizonFadeStart, 0.035f), 0.0f, 0.80f);
    settings.horizonFadeEnd =
        std::clamp(finiteOr(settings.horizonFadeEnd, 0.18f),
                   settings.horizonFadeStart + 0.005f, 0.95f);
    return settings;
}

PaintedCloudGpuData packPaintedCloudGpuData(
    const PaintedCloudSettings& input,
    const PaintedCloudFrameInputs& frameInputs)
{
    const PaintedCloudSettings settings = sanitizePaintedCloudSettings(input);
    const glm::vec2 windDirection = safeDirection(frameInputs.windDirection);
    const float windSpeed =
        std::clamp(finiteOr(frameInputs.windSpeed, 1.0f), 0.0f, 8.0f);
    const float windStrength =
        std::clamp(finiteOr(frameInputs.windStrength, 1.0f), 0.0f, 2.0f);
    const glm::vec2 windVelocity = windDirection * windSpeed * windStrength;

    PaintedCloudGpuData gpu{};
    gpu.cloud0 = glm::vec4(settings.lightTint, settings.strength);
    gpu.cloud1 = glm::vec4(settings.shadowTint, settings.coverage);
    gpu.cloud2 = glm::vec4(settings.altitude, settings.worldScale,
                           settings.opacity, settings.softness);
    gpu.cloud3 = glm::vec4(settings.detailStrength,
                           settings.silverLiningStrength,
                           settings.horizonFadeStart,
                           settings.horizonFadeEnd);
    gpu.cloud4 = glm::vec4(windVelocity,
                           finiteOr(frameInputs.animationTime, 0.0f),
                           static_cast<float>(frameInputs.worldSeed & 0x00ffffffu));
    return gpu;
}

PaintedCloudEvaluation evaluatePaintedClouds(
    const PaintedCloudSettings& input,
    const PaintedCloudFrameInputs& frameInputs,
    const glm::vec3& skyColor,
    const glm::vec3& cameraPosition,
    const glm::vec3& viewDirection,
    const glm::vec3& directionToSun)
{
    const PaintedCloudSettings settings = sanitizePaintedCloudSettings(input);
    PaintedCloudEvaluation result{skyColor, 0.0f};
    if (!settings.enabled())
    {
        return result;
    }

    const glm::vec3 view = safeDirection(viewDirection, {0.0f, 1.0f, 0.0f});
    if (view.y <= settings.horizonFadeStart)
    {
        return result;
    }
    const PaintedCloudGpuData gpu = packPaintedCloudGpuData(settings, frameInputs);
    const float layerHeight =
        settings.altitude - finiteOr(cameraPosition.y, 0.0f);
    if (layerHeight <= 1.0f)
    {
        return result;
    }
    const float travel = layerHeight / std::max(view.y, 0.02f);
    const glm::vec2 worldPoint(finiteOr(cameraPosition.x, 0.0f) + view.x * travel,
                               finiteOr(cameraPosition.z, 0.0f) + view.z * travel);
    const glm::vec2 windOffset = glm::vec2(gpu.cloud4) * gpu.cloud4.z;
    const glm::vec2 noisePoint = facetCloudDomain(
        (worldPoint - windOffset) / settings.worldScale);
    const CloudNoiseSample noise = sampleFixedCloudNoise(
        noisePoint, gpu.cloud4.w, settings.detailStrength);
    const float threshold = glm::mix(0.74f, 0.30f, settings.coverage);
    const float mask = smoothstep(threshold - settings.softness,
                                  threshold + settings.softness, noise.shape);
    const float horizon = smoothstep(settings.horizonFadeStart,
                                     settings.horizonFadeEnd, view.y);
    const float opacity = std::clamp(mask * settings.opacity * settings.strength * horizon,
                                     0.0f, 1.0f);

    const glm::vec3 sun = safeDirection(directionToSun, {0.0f, 1.0f, 0.0f});
    const float bodyLight = std::clamp(0.46f + 0.34f * noise.shape +
                                           0.12f * (noise.detail - 0.5f) +
                                           0.08f * std::max(sun.y, 0.0f),
                                       0.0f, 1.0f);
    glm::vec3 cloudColor = glm::mix(settings.shadowTint, settings.lightTint, bodyLight);
    const float edge = 4.0f * mask * (1.0f - mask);
    const float sunFacing =
        std::pow(std::max(glm::dot(view, sun), 0.0f), 8.0f);
    cloudColor += settings.lightTint * settings.silverLiningStrength *
                  edge * sunFacing;

    result.opacity = opacity;
    result.color = glm::mix(glm::max(skyColor, glm::vec3(0.0f)), cloudColor, opacity);
    return result;
}

} // namespace engine::render
