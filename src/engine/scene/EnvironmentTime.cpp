#include "engine/scene/EnvironmentTime.h"

#include <algorithm>
#include <cmath>

#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/trigonometric.hpp>

namespace engine::scene
{
namespace
{

constexpr float kHoursPerDay = 24.0f;

float finiteOr(float value, float fallback)
{
    return std::isfinite(value) ? value : fallback;
}

float wrapHour(float hour)
{
    float wrapped = std::fmod(hour, kHoursPerDay);
    if (wrapped < 0.0f)
    {
        wrapped += kHoursPerDay;
    }
    return wrapped;
}

float mixScalar(float lhs, float rhs, float amount)
{
    if (amount <= 0.0f)
    {
        return lhs;
    }
    if (amount >= 1.0f)
    {
        return rhs;
    }
    return lhs + (rhs - lhs) * amount;
}

glm::vec3 multiplyClamped(const glm::vec3& value, const glm::vec3& factor)
{
    return glm::clamp(value * factor, glm::vec3(0.0f), glm::vec3(4.0f));
}

engine::render::PaintedSkySettings mixPaintedSky(
    const engine::render::PaintedSkySettings& lhs,
    const engine::render::PaintedSkySettings& rhs,
    float amount)
{
    if (amount <= 0.0f)
    {
        return lhs;
    }
    if (amount >= 1.0f)
    {
        return rhs;
    }
    engine::render::PaintedSkySettings result{};
    result.strength = mixScalar(lhs.strength, rhs.strength, amount);
    result.horizonTint = glm::mix(lhs.horizonTint, rhs.horizonTint, amount);
    result.zenithTint = glm::mix(lhs.zenithTint, rhs.zenithTint, amount);
    result.lowerHemisphereTint =
        glm::mix(lhs.lowerHemisphereTint, rhs.lowerHemisphereTint, amount);
    result.gradientExponent =
        mixScalar(lhs.gradientExponent, rhs.gradientExponent, amount);
    result.horizonBandStrength =
        mixScalar(lhs.horizonBandStrength, rhs.horizonBandStrength, amount);
    result.horizonBandExponent =
        mixScalar(lhs.horizonBandExponent, rhs.horizonBandExponent, amount);
    result.sunDiscAngularRadius =
        mixScalar(lhs.sunDiscAngularRadius, rhs.sunDiscAngularRadius, amount);
    result.sunDiscSoftness =
        mixScalar(lhs.sunDiscSoftness, rhs.sunDiscSoftness, amount);
    result.sunDiscIntensity =
        mixScalar(lhs.sunDiscIntensity, rhs.sunDiscIntensity, amount);
    result.sunHaloIntensity =
        mixScalar(lhs.sunHaloIntensity, rhs.sunHaloIntensity, amount);
    result.sunHaloExponent =
        mixScalar(lhs.sunHaloExponent, rhs.sunHaloExponent, amount);
    return engine::render::sanitizePaintedSkySettings(result);
}

engine::render::PaintedCloudSettings mixPaintedClouds(
    const engine::render::PaintedCloudSettings& lhs,
    const engine::render::PaintedCloudSettings& rhs,
    float amount)
{
    if (amount <= 0.0f)
    {
        return lhs;
    }
    if (amount >= 1.0f)
    {
        return rhs;
    }
    engine::render::PaintedCloudSettings result{};
    result.strength = mixScalar(lhs.strength, rhs.strength, amount);
    result.coverage = mixScalar(lhs.coverage, rhs.coverage, amount);
    result.opacity = mixScalar(lhs.opacity, rhs.opacity, amount);
    result.softness = mixScalar(lhs.softness, rhs.softness, amount);
    result.altitude = mixScalar(lhs.altitude, rhs.altitude, amount);
    result.worldScale = mixScalar(lhs.worldScale, rhs.worldScale, amount);
    result.detailStrength =
        mixScalar(lhs.detailStrength, rhs.detailStrength, amount);
    result.lightTint = glm::mix(lhs.lightTint, rhs.lightTint, amount);
    result.shadowTint = glm::mix(lhs.shadowTint, rhs.shadowTint, amount);
    result.silverLiningStrength = mixScalar(
        lhs.silverLiningStrength, rhs.silverLiningStrength, amount);
    result.horizonFadeStart =
        mixScalar(lhs.horizonFadeStart, rhs.horizonFadeStart, amount);
    result.horizonFadeEnd =
        mixScalar(lhs.horizonFadeEnd, rhs.horizonFadeEnd, amount);
    return engine::render::sanitizePaintedCloudSettings(result);
}

engine::render::HemisphereAmbientSettings mixHemisphereAmbient(
    const engine::render::HemisphereAmbientSettings& lhs,
    const engine::render::HemisphereAmbientSettings& rhs,
    float amount)
{
    if (amount <= 0.0f)
    {
        return lhs;
    }
    if (amount >= 1.0f)
    {
        return rhs;
    }
    engine::render::HemisphereAmbientSettings result{};
    result.strength = mixScalar(lhs.strength, rhs.strength, amount);
    result.skyTint = glm::mix(lhs.skyTint, rhs.skyTint, amount);
    result.groundTint = glm::mix(lhs.groundTint, rhs.groundTint, amount);
    return result;
}

engine::render::SceneAtmosphereSettings mixAtmosphere(
    const engine::render::SceneAtmosphereSettings& lhs,
    const engine::render::SceneAtmosphereSettings& rhs,
    float amount)
{
    if (amount <= 0.0f)
    {
        return lhs;
    }
    if (amount >= 1.0f)
    {
        return rhs;
    }
    engine::render::SceneAtmosphereSettings result{};
    result.density = mixScalar(lhs.density, rhs.density, amount);
    result.heightFalloff =
        mixScalar(lhs.heightFalloff, rhs.heightFalloff, amount);
    result.baseHeight = mixScalar(lhs.baseHeight, rhs.baseHeight, amount);
    result.sunPhaseStrength =
        mixScalar(lhs.sunPhaseStrength, rhs.sunPhaseStrength, amount);
    result.sunPhaseExponent =
        mixScalar(lhs.sunPhaseExponent, rhs.sunPhaseExponent, amount);
    return result;
}

EnvironmentTimePresentation mixPresentation(
    const EnvironmentTimePresentation& lhs,
    const EnvironmentTimePresentation& rhs,
    float amount)
{
    if (amount <= 0.0f)
    {
        return lhs;
    }
    if (amount >= 1.0f)
    {
        return rhs;
    }
    EnvironmentTimePresentation result{};
    result.sunElevation = mixScalar(lhs.sunElevation, rhs.sunElevation, amount);
    result.sunAzimuth = mixScalar(lhs.sunAzimuth, rhs.sunAzimuth, amount);
    result.sunColor = glm::mix(lhs.sunColor, rhs.sunColor, amount);
    result.sunIntensity = mixScalar(lhs.sunIntensity, rhs.sunIntensity, amount);
    result.skyColor = glm::mix(lhs.skyColor, rhs.skyColor, amount);
    result.paintedSky = mixPaintedSky(lhs.paintedSky, rhs.paintedSky, amount);
    result.paintedClouds =
        mixPaintedClouds(lhs.paintedClouds, rhs.paintedClouds, amount);
    result.hemisphereAmbient = mixHemisphereAmbient(
        lhs.hemisphereAmbient, rhs.hemisphereAmbient, amount);
    result.sceneAtmosphere =
        mixAtmosphere(lhs.sceneAtmosphere, rhs.sceneAtmosphere, amount);
    return result;
}

EnvironmentTimePresentation makeKeyframe(
    EnvironmentTimePreset preset,
    const EnvironmentTimePresentation& authored)
{
    if (preset == EnvironmentTimePreset::Day)
    {
        return authored;
    }

    EnvironmentTimePresentation key = authored;
    engine::render::PaintedSkyPreset paintedPreset =
        engine::render::PaintedSkyPreset::SoftDayV0;
    glm::vec3 skyFactor{1.0f};
    glm::vec3 cloudLightFactor{1.0f};
    glm::vec3 cloudShadowFactor{1.0f};
    glm::vec3 ambientSkyFactor{1.0f};
    glm::vec3 ambientGroundFactor{1.0f};
    float cloudOpacityFactor = 1.0f;
    float cloudLiningFactor = 1.0f;
    float ambientStrengthFactor = 1.0f;
    float atmosphereDensityFactor = 1.0f;
    float atmospherePhaseFactor = 1.0f;

    switch (preset)
    {
    case EnvironmentTimePreset::Night:
        key.sunElevation = -20.0f;
        key.sunColor = {0.30f, 0.35f, 0.50f};
        key.sunIntensity = 0.30f;
        paintedPreset = engine::render::PaintedSkyPreset::NightV0;
        skyFactor = {0.10f, 0.12f, 0.26f};
        cloudLightFactor = {0.32f, 0.38f, 0.55f};
        cloudShadowFactor = {0.25f, 0.32f, 0.52f};
        ambientSkyFactor = {0.25f, 0.34f, 0.62f};
        ambientGroundFactor = {0.22f, 0.26f, 0.42f};
        cloudOpacityFactor = 0.82f;
        cloudLiningFactor = 0.20f;
        ambientStrengthFactor = 0.28f;
        atmosphereDensityFactor = 0.75f;
        atmospherePhaseFactor = 0.20f;
        break;
    case EnvironmentTimePreset::Dawn:
        key.sunElevation = 5.0f;
        key.sunColor = {1.0f, 0.70f, 0.50f};
        key.sunIntensity = 1.20f;
        paintedPreset = engine::render::PaintedSkyPreset::DawnV0;
        skyFactor = {0.85f, 0.55f, 0.78f};
        cloudLightFactor = {1.05f, 0.75f, 0.68f};
        cloudShadowFactor = {0.75f, 0.60f, 0.75f};
        ambientSkyFactor = {0.82f, 0.64f, 0.90f};
        ambientGroundFactor = {0.72f, 0.54f, 0.65f};
        cloudOpacityFactor = 0.94f;
        cloudLiningFactor = 1.15f;
        ambientStrengthFactor = 0.65f;
        atmosphereDensityFactor = 1.05f;
        atmospherePhaseFactor = 0.80f;
        break;
    case EnvironmentTimePreset::Sunset:
        key.sunElevation = 10.0f;
        key.sunColor = {1.0f, 0.50f, 0.20f};
        key.sunIntensity = 1.80f;
        paintedPreset = engine::render::PaintedSkyPreset::WarmSunsetV0;
        skyFactor = {1.20f, 0.58f, 0.52f};
        cloudLightFactor = {1.12f, 0.68f, 0.52f};
        cloudShadowFactor = {0.75f, 0.50f, 0.62f};
        ambientSkyFactor = {0.92f, 0.62f, 0.72f};
        ambientGroundFactor = {0.78f, 0.50f, 0.48f};
        cloudOpacityFactor = 0.96f;
        cloudLiningFactor = 1.35f;
        ambientStrengthFactor = 0.60f;
        atmosphereDensityFactor = 0.95f;
        atmospherePhaseFactor = 0.90f;
        break;
    case EnvironmentTimePreset::Day:
        break;
    }

    key.skyColor = multiplyClamped(authored.skyColor, skyFactor);
    if (authored.paintedSky.enabled())
    {
        key.paintedSky =
            engine::render::makePaintedSkySettings(paintedPreset);
        key.paintedSky.strength = authored.paintedSky.strength;
    }
    key.paintedClouds.lightTint = multiplyClamped(
        authored.paintedClouds.lightTint, cloudLightFactor);
    key.paintedClouds.shadowTint = multiplyClamped(
        authored.paintedClouds.shadowTint, cloudShadowFactor);
    key.paintedClouds.opacity = std::clamp(
        authored.paintedClouds.opacity * cloudOpacityFactor, 0.0f, 1.0f);
    key.paintedClouds.silverLiningStrength = std::clamp(
        authored.paintedClouds.silverLiningStrength * cloudLiningFactor,
        0.0f, 2.0f);
    key.hemisphereAmbient.strength = std::max(
        authored.hemisphereAmbient.strength * ambientStrengthFactor, 0.0f);
    key.hemisphereAmbient.skyTint = multiplyClamped(
        authored.hemisphereAmbient.skyTint, ambientSkyFactor);
    key.hemisphereAmbient.groundTint = multiplyClamped(
        authored.hemisphereAmbient.groundTint, ambientGroundFactor);
    key.sceneAtmosphere.density = std::max(
        authored.sceneAtmosphere.density * atmosphereDensityFactor, 0.0f);
    key.sceneAtmosphere.sunPhaseStrength = std::clamp(
        authored.sceneAtmosphere.sunPhaseStrength * atmospherePhaseFactor,
        0.0f, 1.0f);
    return key;
}

} // namespace

EnvironmentTimeSettings defaultEnvironmentTimeSettings()
{
    return {};
}

EnvironmentTimeSettings sanitizeEnvironmentTimeSettings(
    const EnvironmentTimeSettings& input)
{
    EnvironmentTimeSettings settings = input;
    settings.timeOfDayHours = wrapHour(
        finiteOr(settings.timeOfDayHours, 12.0f));
    settings.dayLengthMinutes = std::clamp(
        finiteOr(settings.dayLengthMinutes, 12.0f), 0.25f, 240.0f);
    return settings;
}

bool environmentTimeSettingsEquivalent(
    const EnvironmentTimeSettings& lhs,
    const EnvironmentTimeSettings& rhs,
    float epsilon)
{
    const EnvironmentTimeSettings a = sanitizeEnvironmentTimeSettings(lhs);
    const EnvironmentTimeSettings b = sanitizeEnvironmentTimeSettings(rhs);
    return a.enabled == b.enabled && a.cycleEnabled == b.cycleEnabled &&
           std::abs(a.timeOfDayHours - b.timeOfDayHours) <= epsilon &&
           std::abs(a.dayLengthMinutes - b.dayLengthMinutes) <= epsilon;
}

float environmentTimePresetHour(EnvironmentTimePreset preset)
{
    switch (preset)
    {
    case EnvironmentTimePreset::Night:
        return 0.0f;
    case EnvironmentTimePreset::Dawn:
        return 6.0f;
    case EnvironmentTimePreset::Day:
        return 12.0f;
    case EnvironmentTimePreset::Sunset:
        return 18.0f;
    }
    return 12.0f;
}

std::string_view environmentTimePresetName(EnvironmentTimePreset preset)
{
    switch (preset)
    {
    case EnvironmentTimePreset::Night:
        return "night";
    case EnvironmentTimePreset::Dawn:
        return "dawn";
    case EnvironmentTimePreset::Day:
        return "day";
    case EnvironmentTimePreset::Sunset:
        return "sunset";
    }
    return "day";
}

EnvironmentTimePhase environmentTimePhase(float inputHour)
{
    const float hour = wrapHour(finiteOr(inputHour, 12.0f));
    if (hour < 3.0f || hour >= 21.0f)
    {
        return EnvironmentTimePhase::Night;
    }
    if (hour < 9.0f)
    {
        return EnvironmentTimePhase::Dawn;
    }
    if (hour < 15.0f)
    {
        return EnvironmentTimePhase::Day;
    }
    return EnvironmentTimePhase::Sunset;
}

std::string_view environmentTimePhaseLabel(EnvironmentTimePhase phase)
{
    switch (phase)
    {
    case EnvironmentTimePhase::Night:
        return "NIGHT";
    case EnvironmentTimePhase::Dawn:
        return "DAWN";
    case EnvironmentTimePhase::Day:
        return "DAY";
    case EnvironmentTimePhase::Sunset:
        return "SUNSET";
    }
    return "DAY";
}

float resolveEnvironmentTimeHour(const EnvironmentTimeSettings& input,
                                 double elapsedSeconds)
{
    const EnvironmentTimeSettings settings =
        sanitizeEnvironmentTimeSettings(input);
    if (!settings.enabled || !settings.cycleEnabled ||
        !std::isfinite(elapsedSeconds) || elapsedSeconds <= 0.0)
    {
        return settings.timeOfDayHours;
    }
    const double cycleSeconds =
        static_cast<double>(settings.dayLengthMinutes) * 60.0;
    const double dayOffset = std::fmod(elapsedSeconds, cycleSeconds) /
                             cycleSeconds * 24.0;
    return wrapHour(settings.timeOfDayHours +
                    static_cast<float>(dayOffset));
}

glm::vec3 environmentSunLightDirection(float elevationDegrees,
                                       float azimuthDegrees)
{
    const float elevation = glm::radians(elevationDegrees);
    const float azimuth = glm::radians(azimuthDegrees);
    return glm::normalize(glm::vec3(
        std::cos(elevation) * std::cos(azimuth),
        -std::sin(elevation),
        std::cos(elevation) * std::sin(azimuth)));
}

EnvironmentTimeSample sampleEnvironmentTime(
    const EnvironmentTimeSettings& input,
    double elapsedSeconds,
    const EnvironmentTimePresentation& authored)
{
    const EnvironmentTimeSettings settings =
        sanitizeEnvironmentTimeSettings(input);
    EnvironmentTimeSample sample{};
    sample.active = settings.enabled;
    sample.hour = resolveEnvironmentTimeHour(settings, elapsedSeconds);
    sample.phase = environmentTimePhase(sample.hour);
    sample.presentation = authored;

    if (settings.enabled)
    {
        EnvironmentTimePreset lhsPreset = EnvironmentTimePreset::Night;
        EnvironmentTimePreset rhsPreset = EnvironmentTimePreset::Dawn;
        float segmentStart = 0.0f;
        if (sample.hour < 6.0f)
        {
            lhsPreset = EnvironmentTimePreset::Night;
            rhsPreset = EnvironmentTimePreset::Dawn;
            segmentStart = 0.0f;
        }
        else if (sample.hour < 12.0f)
        {
            lhsPreset = EnvironmentTimePreset::Dawn;
            rhsPreset = EnvironmentTimePreset::Day;
            segmentStart = 6.0f;
        }
        else if (sample.hour < 18.0f)
        {
            lhsPreset = EnvironmentTimePreset::Day;
            rhsPreset = EnvironmentTimePreset::Sunset;
            segmentStart = 12.0f;
        }
        else
        {
            lhsPreset = EnvironmentTimePreset::Sunset;
            rhsPreset = EnvironmentTimePreset::Night;
            segmentStart = 18.0f;
        }
        const float amount = (sample.hour - segmentStart) / 6.0f;
        sample.presentation = mixPresentation(
            makeKeyframe(lhsPreset, authored),
            makeKeyframe(rhsPreset, authored), amount);

        // one continuous east-to-west arc, offset so authored noon remains exact.
        const float azimuth = authored.sunAzimuth + (sample.hour - 12.0f) * 15.0f;
        sample.presentation.sunAzimuth = std::fmod(azimuth, 360.0f);
        if (sample.presentation.sunAzimuth < 0.0f)
        {
            sample.presentation.sunAzimuth += 360.0f;
        }
    }

    sample.lightDirection = environmentSunLightDirection(
        sample.presentation.sunElevation,
        sample.presentation.sunAzimuth);
    return sample;
}

} // namespace engine::scene
