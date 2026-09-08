#include "engine/scene/EnvironmentSettings.h"

#include "engine/scene/SceneConfig.h"

namespace engine::scene
{

EnvironmentSettings environmentSettingsFromSceneConfig(
    const SceneConfig& config)
{
    EnvironmentSettings settings{};
    settings.wind = config.environmentWind;
    settings.time = config.environmentTime;
    settings.windborneParticles = config.windborneParticles;
    settings.cloudWindSpeed = config.cloudWindSpeed;
    settings.cloudWindDirection = config.cloudWindDirection;
    settings.cloudShadowStrength = config.cloudShadowStrength;
    settings.skyPreset = config.skyPreset;
    settings.skyColor = config.skyColor;
    return settings;
}

void applyEnvironmentSettingsToSceneConfig(
    SceneConfig& config,
    const EnvironmentSettings& settings)
{
    config.environmentWind = sanitizeEnvironmentWindSettings(settings.wind);
    config.environmentTime = sanitizeEnvironmentTimeSettings(settings.time);
    config.windborneParticles =
        sanitizeWindborneParticleSettings(settings.windborneParticles);
    config.cloudWindSpeed = settings.cloudWindSpeed;
    config.cloudWindDirection = settings.cloudWindDirection;
    config.cloudShadowStrength = settings.cloudShadowStrength;
    config.skyPreset = settings.skyPreset;
    config.skyColor = settings.skyColor;
}

bool isCustomSkyPreset(int preset)
{
    return preset == 4;
}

glm::vec3 skyColorForPreset(int preset, const glm::vec3& customColor)
{
    switch (preset)
    {
    case 0:
        return glm::vec3(0.09f, 0.29f, 0.88f);
    case 1:
        return glm::vec3(0.95f, 0.45f, 0.25f);
    case 2:
        return glm::vec3(0.02f, 0.02f, 0.08f);
    case 3:
        return glm::vec3(0.6f, 0.35f, 0.55f);
    default:
        return customColor;
    }
}

EnvironmentSettings withSkyPreset(EnvironmentSettings settings, int preset)
{
    settings.skyPreset = preset;
    settings.skyColor = skyColorForPreset(preset, settings.skyColor);
    return settings;
}

EnvironmentSettings withSkyColor(EnvironmentSettings settings,
                                 const glm::vec3& color)
{
    settings.skyColor = color;
    return settings;
}

} // namespace engine::scene
