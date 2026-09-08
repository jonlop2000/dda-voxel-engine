#include "App/App.h"

#include <cmath>

#include "App/ScenePresentationProfileRuntime.h"
#include "engine/scene/EnvironmentSettings.h"
#include "engine/scene/WorldStateView.h"

engine::scene::WorldStateView App::worldStateView() const
{
    return engine::scene::WorldStateView{
        sceneManager_.sceneConfig(), lightingSettings_,   shadowSettings_,
        aoSettings_,                 postFxSettings_,     waterSettings_,
        glassSettings_,              voxelDebugSettings_, diagnosticsSettings_,
        framePacingSettings_, sceneManager_.sceneCatalogRevision()};
}

engine::scene::ScenePresentationProfileReadout App::presentationProfileReadout() const
{
    return app::inspectScenePresentationProfileRuntimeState(
        sceneManager_.presentationRuntimeState(), skyPreset_, skyColor_,
        lightingSettings_, shadowSettings_, aoSettings_, postFxSettings_,
        voxelDebugSettings_, waterSettings_, glassSettings_);
}

void App::setEnvironmentWindSettings(
    const engine::scene::EnvironmentWindSettings& wind)
{
    auto settings =
        engine::scene::environmentSettingsFromSceneConfig(sceneConfig());
    settings.wind = engine::scene::sanitizeEnvironmentWindSettings(wind);
    engine::scene::applyEnvironmentSettingsToSceneConfig(
        sceneConfigMutable(), settings);
}

void App::setEnvironmentTimeSettings(
    const engine::scene::EnvironmentTimeSettings& time)
{
    auto settings =
        engine::scene::environmentSettingsFromSceneConfig(sceneConfig());
    settings.time = engine::scene::sanitizeEnvironmentTimeSettings(time);
    engine::scene::applyEnvironmentSettingsToSceneConfig(
        sceneConfigMutable(), settings);
    resetEnvironmentTimeRuntimeState();
    postFxSettings_.taaResetHistory_ = true;
    shadowSettings_.shadowResetHistory_ = true;
    shadowSettings_.localShadowResetHistory_ = true;
    aoSettings_.aoResetHistory_ = true;
}

void App::setEnvironmentTimePreset(engine::scene::EnvironmentTimePreset preset)
{
    engine::scene::EnvironmentTimeSettings time = sceneConfig().environmentTime;
    time.enabled = true;
    time.cycleEnabled = false;
    time.timeOfDayHours = engine::scene::environmentTimePresetHour(preset);
    setEnvironmentTimeSettings(time);
}

void App::updateEnvironmentTime(float deltaSeconds, bool frozen)
{
    const engine::scene::EnvironmentTimeSettings time =
        engine::scene::sanitizeEnvironmentTimeSettings(
            sceneConfig().environmentTime);
    if (time.enabled && time.cycleEnabled && !frozen &&
        std::isfinite(deltaSeconds) && deltaSeconds > 0.0f)
    {
        environmentTimeElapsedSeconds_ += static_cast<double>(deltaSeconds);
    }

    engine::scene::EnvironmentTimePresentation authored{};
    authored.sunElevation = lightingSettings_.sunElevation_;
    authored.sunAzimuth = lightingSettings_.sunAzimuth_;
    authored.sunColor = lightingSettings_.sunColor_;
    authored.sunIntensity = lightingSettings_.sunIntensity_;
    authored.skyColor = skyColor_;
    authored.paintedSky = lightingSettings_.paintedSky_;
    authored.paintedClouds = lightingSettings_.paintedClouds_;
    authored.hemisphereAmbient = lightingSettings_.hemisphereAmbient_;
    authored.sceneAtmosphere = lightingSettings_.sceneAtmosphere_;
    environmentTimeSample_ = engine::scene::sampleEnvironmentTime(
        time, environmentTimeElapsedSeconds_, authored);
}

void App::resetEnvironmentTimeRuntimeState()
{
    environmentTimeElapsedSeconds_ = 0.0;
    updateEnvironmentTime(0.0f, true);
}

void App::setWindborneParticleSettings(
    const engine::scene::WindborneParticleSettings& particles)
{
    auto settings =
        engine::scene::environmentSettingsFromSceneConfig(sceneConfig());
    settings.windborneParticles =
        engine::scene::sanitizeWindborneParticleSettings(particles);
    engine::scene::applyEnvironmentSettingsToSceneConfig(
        sceneConfigMutable(), settings);
}

void App::setCloudWindSpeed(float speed)
{
    auto settings =
        engine::scene::environmentSettingsFromSceneConfig(sceneConfig());
    settings.cloudWindSpeed = speed;
    engine::scene::applyEnvironmentSettingsToSceneConfig(
        sceneConfigMutable(), settings);
}

void App::setCloudWindDirection(const glm::vec2& dir)
{
    auto settings =
        engine::scene::environmentSettingsFromSceneConfig(sceneConfig());
    settings.cloudWindDirection = dir;
    engine::scene::applyEnvironmentSettingsToSceneConfig(
        sceneConfigMutable(), settings);
}

void App::setSceneCloudShadowStrength(float strength)
{
    auto settings =
        engine::scene::environmentSettingsFromSceneConfig(sceneConfig());
    settings.cloudShadowStrength = strength;
    engine::scene::applyEnvironmentSettingsToSceneConfig(
        sceneConfigMutable(), settings);
}

void App::applySceneSkyPreset(int preset)
{
    const auto settings = engine::scene::withSkyPreset(
        engine::scene::environmentSettingsFromSceneConfig(sceneConfig()),
        preset);
    applySkyPreset(preset);
    engine::scene::applyEnvironmentSettingsToSceneConfig(
        sceneConfigMutable(), settings);
}

void App::setSceneSkyColor(const glm::vec3& color)
{
    const auto settings = engine::scene::withSkyColor(
        engine::scene::environmentSettingsFromSceneConfig(sceneConfig()),
        color);
    skyColor_ = settings.skyColor;
    engine::scene::applyEnvironmentSettingsToSceneConfig(
        sceneConfigMutable(), settings);
}
