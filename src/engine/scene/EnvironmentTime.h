#pragma once

#include <cstdint>
#include <string_view>

#include <glm/vec3.hpp>

#include "engine/render/HemisphereAmbient.h"
#include "engine/render/PaintedClouds.h"
#include "engine/render/PaintedSky.h"
#include "engine/render/SceneAtmosphere.h"

namespace engine::scene
{

// scene-owned environmental clock. it is deliberately independent from the
// animation clock used by water, foliage, creatures, and cloud drift.
// disabled is the exact compatibility path for scene schemas v0-v9.
struct EnvironmentTimeSettings
{
    bool enabled = false;
    bool cycleEnabled = false;
    float timeOfDayHours = 12.0f;
    float dayLengthMinutes = 12.0f;

    bool operator==(const EnvironmentTimeSettings&) const = default;
};

enum class EnvironmentTimePreset : uint8_t
{
    Night,
    Dawn,
    Day,
    Sunset,
};

enum class EnvironmentTimePhase : uint8_t
{
    Night,
    Dawn,
    Day,
    Sunset,
};

[[nodiscard]] EnvironmentTimeSettings defaultEnvironmentTimeSettings();
[[nodiscard]] EnvironmentTimeSettings sanitizeEnvironmentTimeSettings(
    const EnvironmentTimeSettings& settings);
[[nodiscard]] bool environmentTimeSettingsEquivalent(
    const EnvironmentTimeSettings& lhs,
    const EnvironmentTimeSettings& rhs,
    float epsilon = 1e-5f);
[[nodiscard]] float environmentTimePresetHour(EnvironmentTimePreset preset);
[[nodiscard]] std::string_view environmentTimePresetName(
    EnvironmentTimePreset preset);
[[nodiscard]] EnvironmentTimePhase environmentTimePhase(float hour);
[[nodiscard]] std::string_view environmentTimePhaseLabel(
    EnvironmentTimePhase phase);

// resolves the dedicated clock from persisted authoring plus runtime elapsed
// seconds. animation time is intentionally not an input.
[[nodiscard]] float resolveEnvironmentTimeHour(
    const EnvironmentTimeSettings& settings,
    double elapsedSeconds);

// authored presentation remains the baseline and persistence authority. time-001
// produces a transient sample from it; disabled settings return this structure
// bit-for-bit without changing any render settings bucket.
struct EnvironmentTimePresentation
{
    float sunElevation = 55.0f;
    float sunAzimuth = 135.0f;
    glm::vec3 sunColor{1.0f, 0.95f, 0.85f};
    float sunIntensity = 2.5f;
    glm::vec3 skyColor{0.09f, 0.29f, 0.88f};
    engine::render::PaintedSkySettings paintedSky{};
    engine::render::PaintedCloudSettings paintedClouds{};
    engine::render::HemisphereAmbientSettings hemisphereAmbient{};
    engine::render::SceneAtmosphereSettings sceneAtmosphere{};

    bool operator==(const EnvironmentTimePresentation&) const = default;
};

struct EnvironmentTimeSample
{
    bool active = false;
    float hour = 12.0f;
    EnvironmentTimePhase phase = EnvironmentTimePhase::Day;
    EnvironmentTimePresentation presentation{};
    // direction of light travel, from the sun toward the scene.
    glm::vec3 lightDirection{0.0f, -1.0f, 0.0f};
};

[[nodiscard]] glm::vec3 environmentSunLightDirection(float elevationDegrees,
                                                     float azimuthDegrees);
[[nodiscard]] EnvironmentTimeSample sampleEnvironmentTime(
    const EnvironmentTimeSettings& settings,
    double elapsedSeconds,
    const EnvironmentTimePresentation& authored);

} // namespace engine::scene
