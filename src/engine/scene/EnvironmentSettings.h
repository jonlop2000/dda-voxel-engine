#pragma once

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include "engine/scene/EnvironmentTime.h"
#include "engine/scene/EnvironmentWind.h"
#include "engine/scene/WindborneParticleSettings.h"

struct SceneConfig;

namespace engine::scene
{

struct EnvironmentSettings
{
    EnvironmentWindSettings wind{};
    EnvironmentTimeSettings time{};
    WindborneParticleSettings windborneParticles{};

    // compatibility controls for the legacy experimental voxel-cloud path.
    // production wind consumers must use wind above instead.
    float cloudWindSpeed = 0.5f;
    glm::vec2 cloudWindDirection{0.0f, 1.0f};
    float cloudShadowStrength = 0.35f;
    int skyPreset = 0;
    glm::vec3 skyColor{0.09f, 0.29f, 0.88f};
};

EnvironmentSettings environmentSettingsFromSceneConfig(
    const SceneConfig& config);
void applyEnvironmentSettingsToSceneConfig(
    SceneConfig& config,
    const EnvironmentSettings& settings);
bool isCustomSkyPreset(int preset);
glm::vec3 skyColorForPreset(int preset, const glm::vec3& customColor);
EnvironmentSettings withSkyPreset(EnvironmentSettings settings, int preset);
EnvironmentSettings withSkyColor(EnvironmentSettings settings,
                                 const glm::vec3& color);

} // namespace engine::scene
