#include "App/ScenePresentationProfileRuntime.h"

#include <algorithm>
#include <exception>
#include <sstream>
#include <string>
#include <utility>

#include "App/Input.h"
#include "Core/Logger.h"
#include "engine/render/RenderSettings.h"
#include "engine/scene/SceneConfig.h"
#include "engine/scene/SoftVoxelPresentationProfile.h"

namespace app
{
namespace
{

int maxActiveLightCount(uint32_t maxLights)
{
    return static_cast<int>(maxLights);
}

} // namespace

void syncPresentationSettingsFromInput(
    Input& input, engine::render::LightingSettings& lighting,
    engine::render::PostFxSettings& postFx, uint32_t maxLights)
{
    lighting.pointLightsEnabled_ = input.lightsEnabled();
    lighting.lightCount_ =
        std::clamp(input.lightCount(), 0, maxActiveLightCount(maxLights));
    input.setLightCount(lighting.lightCount_);

    postFx.tonemapEnabled_ = input.tonemapEnabled();
    postFx.exposure_ = input.exposure();
    postFx.bloomEnabled_ = input.bloomEnabled();
}

void syncPresentationInputFromSettings(
    Input& input, engine::render::LightingSettings& lighting,
    engine::render::PostFxSettings& postFx, uint32_t maxLights)
{
    lighting.lightCount_ =
        std::clamp(lighting.lightCount_, 0, maxActiveLightCount(maxLights));

    input.setLightsEnabled(lighting.pointLightsEnabled_);
    input.setLightCount(lighting.lightCount_);
    input.setTonemapEnabled(postFx.tonemapEnabled_);
    input.setExposure(postFx.exposure_);
    input.setBloomEnabled(postFx.bloomEnabled_);

    // Input applies its own public control-range clamps. reflect the effective
    // values back into the authoritative render buckets once at the bridge.
    lighting.pointLightsEnabled_ = input.lightsEnabled();
    lighting.lightCount_ = input.lightCount();
    postFx.tonemapEnabled_ = input.tonemapEnabled();
    postFx.exposure_ = input.exposure();
    postFx.bloomEnabled_ = input.bloomEnabled();
}

engine::scene::ScenePresentationRuntimeState resolveApplyAndSyncScenePresentationProfile(
    const SceneConfig& sceneConfig, Input& input, int& skyPreset, glm::vec3& skyColor,
    engine::render::LightingSettings& lighting,
    engine::render::ShadowSettings& shadows,
    engine::render::AmbientOcclusionSettings& ambientOcclusion,
    engine::render::PostFxSettings& postFx,
    engine::render::VoxelDebugSettings& voxel,
    engine::render::WaterSettings& water,
    engine::render::GlassSettings& glass, uint32_t maxLights)
{
    // begin every load from compatibility-off so predecessor live state cannot
    // leak. profile application below may then replace the presentation buckets.
    voxel.voxelCellVariation_ = {};
    lighting.hemisphereAmbient_ = {};
    lighting.sceneAtmosphere_ = {};
    lighting.paintedSky_ = {};
    lighting.paintedClouds_ = {};

    engine::scene::SceneProfileResolution resolution =
        engine::scene::resolveAndApplySceneProfile(
            sceneConfig, skyPreset, skyColor, lighting, shadows, ambientOcclusion,
            postFx, voxel, water, glass);
    syncPresentationInputFromSettings(input, lighting, postFx, maxLights);
    return engine::scene::ScenePresentationRuntimeState{
        resolution,
        engine::scene::captureScenePresentationProfile(
            resolution.appliedName, skyPreset, skyColor, lighting, shadows,
            ambientOcclusion, postFx, voxel, water, glass)};
}

engine::scene::ScenePresentationProfileReadout inspectScenePresentationProfileRuntimeState(
    const engine::scene::ScenePresentationRuntimeState& runtimeState,
    int skyPreset, const glm::vec3& skyColor,
    const engine::render::LightingSettings& lighting,
    const engine::render::ShadowSettings& shadows,
    const engine::render::AmbientOcclusionSettings& ambientOcclusion,
    const engine::render::PostFxSettings& postFx,
    const engine::render::VoxelDebugSettings& voxel,
    const engine::render::WaterSettings& water,
    const engine::render::GlassSettings& glass)
{
    const engine::scene::ScenePresentationProfile liveProfile =
        engine::scene::captureScenePresentationProfile(
            runtimeState.resolution.appliedName, skyPreset, skyColor, lighting,
            shadows, ambientOcclusion, postFx, voxel, water, glass);
    std::vector<std::string> liveEditDelta =
        engine::scene::describeScenePresentationProfileDelta(
            runtimeState.appliedProfile, liveProfile);

    return engine::scene::ScenePresentationProfileReadout{
        runtimeState.resolution.appliedName,
        runtimeState.resolution.namedBaseName,
        runtimeState.resolution.source,
        liveEditDelta.empty() ? runtimeState.resolution.source
                              : engine::scene::ScenePresentationProfileSource::LiveEdit,
        std::move(liveEditDelta)};
}

bool applyAutomationSoftProfileV0(
    std::string_view modeName, Input& input, int& skyPreset, glm::vec3& skyColor,
    engine::render::LightingSettings& lighting,
    engine::render::ShadowSettings& shadows,
    engine::render::AmbientOcclusionSettings& ambientOcclusion,
    engine::render::PostFxSettings& postFx,
    engine::render::VoxelDebugSettings& voxel,
    engine::render::WaterSettings& water,
    engine::render::GlassSettings& glass, uint32_t maxLights)
{
    if (modeName.empty())
    {
        return false;
    }

    const std::optional<engine::scene::ReconstructionComparisonMode> mode =
        engine::scene::reconstructionComparisonModeFromName(modeName);
    if (!mode)
    {
        logWarning(
            "Automation",
            std::string("Ignoring unknown reconstruction comparison mode: ") +
                std::string(modeName));
        return false;
    }

    engine::scene::ScenePresentationProfile profile =
        engine::scene::captureScenePresentationProfile(
            "resolved-scene", skyPreset, skyColor, lighting, shadows,
            ambientOcclusion, postFx, voxel, water, glass);
    profile = engine::scene::makeSoftOutdoorProfileV0(std::move(profile), *mode);
    engine::scene::applyScenePresentationProfile(
        profile, skyPreset, skyColor, lighting, shadows, ambientOcclusion,
        postFx, voxel, water, glass);
    syncPresentationInputFromSettings(input, lighting, postFx, maxLights);

    std::ostringstream summary;
    summary << "Soft Profile v0 reconstruction mode '"
            << engine::scene::reconstructionComparisonModeName(*mode)
            << "' applied: profile='" << profile.name << "', taa="
            << (profile.postFx.taaEnabled ? 1 : 0) << ", jitter="
            << (profile.postFx.jitterEnabled ? 1 : 0) << ", fxaa="
            << (profile.postFx.fxaaEnabled ? 1 : 0)
            << ", requestedSunRadius="
            << profile.shadows.requestedSunAngularRadius
            << ", effectiveSunRadius="
            << engine::scene::effectiveDdaSunAngularRadius(
                   profile.shadows.requestedSunAngularRadius,
                   profile.shadows.ddaSunSampleCount)
            << ".";
    logInfo("Automation", summary.str());
    return true;
}

bool applyAutomationVoxelCellVariationPreset(
    std::string_view presetName, engine::render::VoxelDebugSettings& voxel)
{
    if (presetName.empty())
    {
        return false;
    }

    const std::optional<engine::VoxelCellVariationPreset> preset =
        engine::voxelCellVariationPresetFromName(presetName);
    if (!preset)
    {
        logWarning(
            "Automation",
            std::string("Ignoring unknown voxel-cell variation preset: ") +
                std::string(presetName));
        return false;
    }

    voxel.voxelCellVariation_ =
        engine::makeVoxelCellVariationSettings(*preset);
    const engine::VoxelCellVariationSettings& settings =
        voxel.voxelCellVariation_;
    std::ostringstream summary;
    summary << "Voxel-cell variation preset '"
            << engine::voxelCellVariationPresetName(*preset)
            << "' applied: enabled=" << (settings.enabled() ? 1 : 0)
            << ", master=" << settings.masterStrength
            << ", generic=" << settings.genericAmplitude
            << ", gravel=" << settings.gravelAmplitude
            << ", plant=" << settings.plantAmplitude
            << ", stone=" << settings.stoneAmplitude
            << ", wood=" << settings.woodAmplitude
            << ", hueSpread=" << settings.hueSpread
            << ", saturationSpread=" << settings.saturationSpread
            << ", valueSpread=" << settings.valueSpread
            << ", family=" << settings.paletteFamilyStrength << ".";
    logInfo("Automation", summary.str());
    return true;
}

bool applyAutomationHemisphereAmbientPreset(
    std::string_view presetName, engine::render::LightingSettings& lighting)
{
    if (presetName.empty())
    {
        return false;
    }

    const std::optional<engine::render::HemisphereAmbientPreset> preset =
        engine::render::hemisphereAmbientPresetFromName(presetName);
    if (!preset)
    {
        logWarning(
            "Automation",
            std::string("Ignoring unknown hemisphere ambient preset: ") +
                std::string(presetName));
        return false;
    }

    lighting.hemisphereAmbient_ =
        engine::render::makeHemisphereAmbientSettings(*preset);
    const engine::render::HemisphereAmbientSettings& settings =
        lighting.hemisphereAmbient_;
    std::ostringstream summary;
    summary << "Hemisphere ambient preset '"
            << engine::render::hemisphereAmbientPresetName(*preset)
            << "' applied: enabled=" << (settings.enabled() ? 1 : 0)
            << ", strength=" << settings.strength
            << ", skyTint=(" << settings.skyTint.r << ","
            << settings.skyTint.g << "," << settings.skyTint.b << ")"
            << ", groundTint=(" << settings.groundTint.r << ","
            << settings.groundTint.g << "," << settings.groundTint.b << ").";
    logInfo("Automation", summary.str());
    return true;
}

bool applyAutomationSceneAtmospherePreset(
    std::string_view presetName, engine::render::LightingSettings& lighting)
{
    if (presetName.empty())
    {
        return false;
    }

    const std::optional<engine::render::SceneAtmospherePreset> preset =
        engine::render::sceneAtmospherePresetFromName(presetName);
    if (!preset)
    {
        logWarning(
            "Automation",
            std::string("Ignoring unknown scene-atmosphere preset: ") +
                std::string(presetName));
        return false;
    }

    lighting.sceneAtmosphere_ =
        engine::render::makeSceneAtmosphereSettings(*preset);
    const engine::render::SceneAtmosphereSettings& settings =
        lighting.sceneAtmosphere_;
    std::ostringstream summary;
    summary << "Scene atmosphere preset '"
            << engine::render::sceneAtmospherePresetName(*preset)
            << "' applied: enabled=" << (settings.enabled() ? 1 : 0)
            << ", density=" << settings.density
            << ", heightFalloff=" << settings.heightFalloff
            << ", baseHeight=" << settings.baseHeight
            << ", sunPhaseStrength=" << settings.sunPhaseStrength
            << ", sunPhaseExponent=" << settings.sunPhaseExponent << ".";
    logInfo("Automation", summary.str());
    return true;
}

bool applyAutomationLightingDebugMode(
    std::string_view modeName, engine::render::LightingSettings& lighting)
{
    if (modeName.empty())
    {
        return false;
    }

    try
    {
        lighting.lightingDebugMode_ =
            std::clamp(std::stoi(std::string(modeName)), 0, 15);
        logInfo(
            "Automation",
            "Initial lighting debug set to " +
                std::to_string(lighting.lightingDebugMode_) + ".");
        return true;
    }
    catch (const std::exception&)
    {
        logWarning(
            "Automation",
            "Ignoring invalid --automation-lighting-debug-mode value: " +
                std::string(modeName));
        return false;
    }
}

} // namespace app
