#include "engine/scene/ScenePresentationProfileVariants.h"

#include <cstddef>
#include <utility>

#include "Core/Logger.h"
#include "engine/scene/AquariumSceneVariant.h"
#include "engine/scene/NaturePondGenerator.h"
#include "engine/scene/SceneConfig.h"
#include "engine/scene/SoftVoxelPresentationProfile.h"

namespace engine::scene
{
namespace
{

// mirrors the classification used by the app scene-name helpers
// (App.cpp isDirectViewAquariumScene / AppSceneVolume.cpp isSunroofAquariumScene /
// isFishbowlAquariumScene / App.cpp isBeachSandScene).
bool isSunroofVariant(const SceneConfig& sceneConfig)
{
    return sceneConfig.loadAquariumTest &&
           isAquariumSunroofSceneName(sceneConfig.name);
}

bool isFishbowlVariant(const SceneConfig& sceneConfig)
{
    return sceneConfig.loadAquariumTest &&
           (sceneConfig.name == "fishbowl_perf_probe" || sceneConfig.name == "fishbowl");
}

bool isDirectAquariumVariant(const SceneConfig& sceneConfig)
{
    return sceneConfig.name == "aquarium_test" || sceneConfig.name == "aquarium_test_probe" ||
           sceneConfig.name == "aquarium_test_perf_probe";
}

bool isBeachSandVariant(const SceneConfig& sceneConfig)
{
    return sceneConfig.loadProceduralWorld &&
           (sceneConfig.name == "beach_sand_palettes" ||
            sceneConfig.name == "beach_sand_perf_probe");
}

// uses the generator's pure scene-name tier routing rather than
// NaturePondScene::isNaturePondScene so this module stays free of renderer
// dependencies.
bool isNaturePondVariant(const SceneConfig& sceneConfig)
{
    return naturePondDetailTierForSceneName(sceneConfig.name).has_value();
}

bool adoptsPaintedOutdoorProfileV3(const SceneConfig& sceneConfig)
{
    return isSunroofVariant(sceneConfig) ||
           isFishbowlVariant(sceneConfig) ||
           sceneConfig.name == "nature_pond_probe" ||
           isNaturePondSunroofProbeName(sceneConfig.name) ||
           isNaturePondDensityProbeName(sceneConfig.name) ||
           (sceneConfig.name == "beach_sand_palettes" &&
            isBeachSandVariant(sceneConfig)) ||
           (sceneConfig.name == "procedural_world" &&
            sceneConfig.loadProceduralWorld);
}

bool adoptsPaintedOutdoorSkyProfileV1(const SceneConfig& sceneConfig)
{
    return sceneConfig.name == "nature_pond_probe" ||
           isNaturePondSunroofProbeName(sceneConfig.name) ||
           isNaturePondDensityProbeName(sceneConfig.name) ||
           (sceneConfig.name == "beach_sand_palettes" &&
            isBeachSandVariant(sceneConfig)) ||
           (sceneConfig.name == "procedural_world" &&
            sceneConfig.loadProceduralWorld);
}

bool adoptsPaintedOutdoorCloudProfileV1(const SceneConfig& sceneConfig)
{
    return sceneConfig.name == "nature_pond_probe" ||
           isNaturePondSunroofProbeName(sceneConfig.name) ||
           isNaturePondDensityProbeName(sceneConfig.name);
}

// mirrors app::applySkyPreset: presets 0-3 author the sun; custom (4) leaves it.
void applySkyPresetSunToProfile(SceneLightingProfile& lighting, int preset)
{
    lighting.sunAzimuth = 135.0f;
    switch (preset)
    {
    case 0: // day
        lighting.sunElevation = 55.0f;
        lighting.sunColor = glm::vec3(1.0f, 0.95f, 0.85f);
        lighting.sunIntensity = 2.5f;
        break;
    case 1: // sunset
        lighting.sunElevation = 10.0f;
        lighting.sunColor = glm::vec3(1.0f, 0.5f, 0.2f);
        lighting.sunIntensity = 1.8f;
        break;
    case 2: // night
        lighting.sunElevation = -20.0f;
        lighting.sunColor = glm::vec3(0.3f, 0.35f, 0.5f);
        lighting.sunIntensity = 0.3f;
        break;
    case 3: // dawn
        lighting.sunElevation = 5.0f;
        lighting.sunColor = glm::vec3(1.0f, 0.7f, 0.5f);
        lighting.sunIntensity = 1.2f;
        break;
    default:
        break;
    }
}

void applyDirectViewAquariumPost(ScenePostFxProfile& postFx, bool sunroof)
{
    postFx.highlightRecovery = sunroof ? 0.16f : 0.25f;
    postFx.colorGradeEnabled = true;
    postFx.colorGradeStrength = sunroof ? 0.22f : 0.35f;
    postFx.colorGradeSaturation = 1.06f;
    postFx.colorGradeContrast = sunroof ? 1.03f : 1.05f;
    postFx.colorGradeTemperature = sunroof ? -0.02f : -0.04f;
}

// mirrors the base pass of applySceneWaterDefaults, including the scene-name foam
// emitter gate.
void applyBaseWaterDefaults(SceneWaterProfile& water, const SceneConfig& sceneConfig)
{
    water.stylizedMode = true;
    water.waveScale = 0.09f;
    water.waveAmp = 0.72f;
    water.specIntensity = 1.6f;
    water.specPower = 128.0f;
    water.reflectionStrength = 0.28f;
    water.causticsEnabled = false;
    water.causticsIntensity = 0.52f;
    water.causticsScale = 0.12f;
    water.causticsSpeed = 0.28f;
    water.causticsBanding = 0.34f;
    water.causticsDepthFade = 18.0f;
    water.gradientStrength = 0.25f;
    water.planarReflectionEnabled = false;
    water.particlesPlanned = true;
    water.particlesPlannedDensity = 0.35f;
    water.particlesPlannedDrift = 0.20f;
    water.particlesPlannedScale = 0.15f;
    water.foamEmitterEnabled = sceneConfig.name == "aquarium_test" ||
                               sceneConfig.name == "aquarium_test_probe";
    water.foamEmitterIntensity = 0.62f;
    water.foamEmitterRadius = 3.0f;
    water.foamEmitterOffsetX = 0.0f;
    water.foamEmitterOffsetZ = 0.0f;
    water.foamEmitterScale = 0.24f;
    water.foamEmitterSpread = 0.34f;
}

// mirrors the direct-view aquarium block of applySceneWaterDefaults plus the
// voxel-glass enforcement that runs after the helpers on scene load.
void applyDirectViewAquariumWaterAndGlass(ScenePresentationProfile& profile)
{
    profile.water.particlesPlanned = false;
    profile.glass.voxelGlassRefractEnabled = false;
}

// mirrors applyGlassMaterialPreset(kRoundFishbowlGlassPreset).
void applyFishbowlGlassPreset(SceneGlassProfile& glass)
{
    glass.tint = glm::vec3(240.0f / 255.0f, 251.0f / 255.0f, 1.0f);
    glass.reflection = glm::vec3(0.50f, 0.68f, 0.82f);
    glass.absorption = 0.810f;
    glass.thicknessScale = 0.100f;
    glass.refract = 0.000f;
    glass.ior = 1.50f;
    glass.bubbleScale = 18.0f;
    glass.bubbleIntensity = 0.05f;
    glass.bubbleThicknessGate = 0.18f;
    glass.bubbleChromaticSplit = 0.010f;
    glass.iridescentStrength = 0.62f;
    glass.iridescentFilmThickness = 3.1f;
    glass.iridescentFrequency = 1.6f;
    glass.voxelGlassRefractEnabled = false;
}

void applyNaturePondClearGlassPreset(SceneGlassProfile& glass)
{
    glass.tint = glm::vec3(0.94f, 0.985f, 1.0f);
    glass.reflection = glm::vec3(0.42f, 0.60f, 0.76f);
    glass.absorption = 0.32f;
    glass.thicknessScale = 0.16f;
    glass.refract = 0.0025f;
    glass.ior = 1.46f;
    glass.bubbleScale = 22.0f;
    glass.bubbleIntensity = 0.018f;
    glass.bubbleThicknessGate = 0.16f;
    glass.bubbleChromaticSplit = 0.004f;
    glass.iridescentStrength = 0.18f;
    glass.iridescentFilmThickness = 2.5f;
    glass.iridescentFrequency = 1.35f;
    glass.voxelGlassRefractEnabled = false;
}

} // namespace

ScenePresentationProfile namedBaseScenePresentationProfile(const SceneConfig& sceneConfig)
{
    ScenePresentationProfile profile{};
    profile.name = "scene-base";

    // base pass, mirroring applySceneLightingDefaults: config sky plus preset-authored
    // sun for presets 0-3. custom (4) keeps the schema-default sun values.
    profile.lighting.skyPreset = sceneConfig.skyPreset;
    profile.lighting.skyColor = sceneConfig.skyColor;
    if (sceneConfig.skyPreset >= 0 && sceneConfig.skyPreset <= 3)
    {
        applySkyPresetSunToProfile(profile.lighting, sceneConfig.skyPreset);
    }
    profile.lighting.pointLightsEnabled = sceneConfig.enablePointLights;
    applyBaseWaterDefaults(profile.water, sceneConfig);

    if (isSunroofVariant(sceneConfig))
    {
        profile.name = "aquarium-sunroof";
        profile.lighting.skyPreset = 4;
        profile.lighting.skyColor = glm::vec3(0.76f, 0.80f, 0.86f);
        profile.lighting.sunElevation = 68.0f;
        profile.lighting.sunAzimuth = 148.0f;
        profile.lighting.sunColor = glm::vec3(1.0f, 0.98f, 0.93f);
        profile.lighting.sunIntensity = 4.5f;
        profile.shadows.csmEnabled = true;
        profile.shadows.useDdaShadows = true;
        profile.shadows.requestedSunAngularRadius = 0.035f;
        profile.shadows.ddaSunSampleCount = 6;
        profile.shadows.temporalBlendAlpha = 0.06f;
        profile.shadows.temporalDepthReject = 0.0025f;
        profile.shadows.temporalNormalRejectDot = 0.95f;
        profile.shadows.temporalClampSharpness = 0.75f;
        profile.shadows.spatialFilterRadius = 1;
        profile.shadows.spatialDepthSigma = 0.0035f;
        profile.shadows.spatialValueSigma = 0.20f;
        profile.shadows.spatialNormalPower = 64.0f;
        profile.shadows.postDenoiseRadius = 1;
        profile.shadows.postDenoiseDepthSigma = 0.0045f;
        profile.shadows.postDenoiseValueSigma = 0.16f;
        profile.shadows.postDenoiseNormalPower = 48.0f;
        applyDirectViewAquariumPost(profile.postFx, true);
        profile.voxelSurface.materialDetailStrength = 0.55f;
        applyDirectViewAquariumWaterAndGlass(profile);
        profile.water.stylizedMode = false;
        profile.water.refract = 0.018f;
        profile.water.fresnelBias = 0.015f;
        profile.water.distortionDepthScale = 0.18f;
        profile.water.waveScale = 0.032f;
        profile.water.waveAmp = 0.16f;
        profile.water.specIntensity = 1.45f;
        profile.water.specPower = 160.0f;
        profile.water.reflectionStrength = 0.10f;
        profile.water.causticsIntensity = 0.46f;
        profile.water.causticsScale = 0.09f;
        profile.water.causticsSpeed = 0.22f;
        profile.water.causticsBanding = 0.24f;
        profile.water.causticsDepthFade = 24.0f;
        profile.water.gradientStrength = 0.02f;
        profile.water.particlesPlannedDensity = 0.18f;
        profile.water.particlesPlannedDrift = 0.16f;
        profile.water.particlesPlannedScale = 0.12f;
        profile.water.foamEmitterEnabled = false;
        profile.water.foamEmitterIntensity = 0.45f;
        profile.water.foamEmitterRadius = 4.0f;
        profile.water.foamEmitterScale = 0.18f;
        profile.water.foamEmitterSpread = 0.22f;
        profile.glass.tint = glm::vec3(1.0f, 0.98f, 0.92f);
        profile.glass.reflection = glm::vec3(0.95f, 0.94f, 0.90f);
        profile.glass.absorption = 0.08f;
        profile.glass.thicknessScale = 0.45f;
        profile.glass.refract = 0.0025f;
        profile.glass.ior = 1.03f;
    }
    else if (isFishbowlVariant(sceneConfig))
    {
        profile.name = "aquarium-fishbowl";
        profile.lighting.skyPreset = 4;
        profile.lighting.skyColor = glm::vec3(0.52f, 0.64f, 0.74f);
        profile.lighting.sunElevation = 48.0f;
        profile.lighting.sunAzimuth = 132.0f;
        profile.lighting.sunColor = glm::vec3(1.0f, 0.95f, 0.86f);
        profile.lighting.sunIntensity = 3.0f;
        profile.shadows.csmEnabled = true;
        profile.shadows.useDdaShadows = true;
        profile.shadows.requestedSunAngularRadius = 0.028f;
        profile.shadows.ddaSunSampleCount = 3;
        profile.shadows.temporalBlendAlpha = 0.07f;
        profile.shadows.temporalDepthReject = 0.0030f;
        profile.shadows.temporalNormalRejectDot = 0.94f;
        profile.shadows.spatialFilterRadius = 1;
        profile.shadows.spatialDepthSigma = 0.0040f;
        profile.shadows.spatialValueSigma = 0.18f;
        profile.shadows.postDenoiseRadius = 1;
        profile.shadows.postDenoiseDepthSigma = 0.0050f;
        profile.shadows.postDenoiseValueSigma = 0.18f;
        // fishbowl post overrides run after the shared direct-view aquarium block.
        applyDirectViewAquariumPost(profile.postFx, false);
        profile.postFx.highlightRecovery = 0.18f;
        profile.postFx.colorGradeStrength = 0.28f;
        profile.postFx.colorGradeSaturation = 1.08f;
        profile.postFx.colorGradeContrast = 1.04f;
        profile.postFx.colorGradeTemperature = -0.015f;
        profile.voxelSurface.materialDetailStrength = 0.50f;
        applyDirectViewAquariumWaterAndGlass(profile);
        profile.water.stylizedMode = false;
        profile.water.refract = 0.014f;
        profile.water.fresnelBias = 0.018f;
        profile.water.distortionDepthScale = 0.14f;
        profile.water.waveScale = 0.052f;
        profile.water.waveAmp = 0.18f;
        profile.water.specIntensity = 1.9f;
        profile.water.specPower = 180.0f;
        profile.water.reflectionStrength = 0.20f;
        profile.water.gradientStrength = 0.035f;
        profile.water.foamEmitterEnabled = false;
        applyFishbowlGlassPreset(profile.glass);
    }
    else if (isBeachSandVariant(sceneConfig))
    {
        profile.name = "beach-sand";
        profile.lighting.skyPreset = 4;
        profile.lighting.skyColor = glm::vec3(0.13f, 0.42f, 0.76f);
        profile.lighting.sunElevation = 42.0f;
        profile.lighting.sunAzimuth = 122.0f;
        profile.lighting.sunColor = glm::vec3(1.0f, 0.94f, 0.82f);
        profile.lighting.sunIntensity = 3.4f;
        profile.shadows.csmEnabled = true;
        profile.shadows.useDdaShadows = true;
        profile.shadows.requestedSunAngularRadius = 0.032f;
        profile.shadows.ddaSunSampleCount = 2;
        profile.shadows.temporalBlendAlpha = 0.07f;
        profile.water.stylizedMode = false;
        profile.water.refract = 0.022f;
        profile.water.fresnelBias = 0.018f;
        profile.water.distortionDepthScale = 0.16f;
        profile.water.waveScale = 0.045f;
        profile.water.waveAmp = 0.28f;
        profile.water.specIntensity = 1.75f;
        profile.water.specPower = 150.0f;
        profile.water.reflectionStrength = 0.18f;
        profile.water.causticsIntensity = 1.45f;
        profile.water.causticsScale = 0.105f;
        profile.water.causticsSpeed = 0.35f;
        profile.water.causticsBanding = 0.26f;
        profile.water.causticsDepthFade = 22.0f;
        profile.water.gradientStrength = 0.05f;
        profile.water.particlesPlanned = false;
        profile.water.foamEmitterEnabled = false;
        profile.water.foamDepthThreshold = 0.55f;
        profile.water.foamOpacity = 0.42f;
        profile.water.foamScale = 0.17f;
    }
    else if (isNaturePondVariant(sceneConfig))
    {
        profile.name = "nature-pond";
        profile.water.stylizedMode = false;
        profile.water.refract = 0.014f;
        profile.water.fresnelBias = 0.018f;
        profile.water.distortionDepthScale = 0.12f;
        profile.water.waveScale = 0.075f;
        profile.water.waveAmp = 0.16f;
        profile.water.specIntensity = 1.45f;
        profile.water.specPower = 150.0f;
        profile.water.reflectionStrength = 0.16f;
        profile.water.gradientStrength = 0.045f;
        profile.water.particlesPlanned = false;
        profile.water.foamEmitterEnabled = false;
        if ((sceneConfig.name == "nature_pond_probe" ||
             isNaturePondSunroofProbeName(sceneConfig.name)) &&
            sceneConfig.enableGlass)
        {
            applyNaturePondClearGlassPreset(profile.glass);
        }
    }
    else if (isDirectAquariumVariant(sceneConfig))
    {
        profile.name = "aquarium-direct";
        applyDirectViewAquariumPost(profile.postFx, false);
        profile.voxelSurface.materialDetailStrength = 0.55f;
        applyDirectViewAquariumWaterAndGlass(profile);
    }

    if (adoptsPaintedOutdoorProfileV3(sceneConfig))
    {
        profile = makePaintedOutdoorProfileV3(std::move(profile));
        if (isSunroofVariant(sceneConfig))
        {
            // keep the deliberately uniform, texture-free white chamber shell while
            // allowing the shared painted profile to vary plants, stone, and decor.
            profile.voxelCellVariation.genericAmplitude = 0.0f;

            // environment V2 uses the existing depth-aware composite gather and water
            // lighting owners. a broad, restrained focus falloff gives the chamber
            // depth without turning fish or fine foliage into a miniature-effect blur.
            profile.postFx.depthOfFieldEnabled = true;
            profile.postFx.depthOfFieldFocusDistance = 25.0f;
            profile.postFx.depthOfFieldFocusRange = 14.0f;
            profile.postFx.depthOfFieldBlurStrength = 0.24f;
            profile.water.causticsEnabled = true;
            profile.water.causticsIntensity = 0.30f;
            profile.water.gradientStrength = 0.065f;
        }
    }
    if (adoptsPaintedOutdoorCloudProfileV1(sceneConfig))
    {
        profile = makePaintedOutdoorCloudProfileV1(std::move(profile));
    }
    else if (adoptsPaintedOutdoorSkyProfileV1(sceneConfig))
    {
        profile = makePaintedOutdoorSkyProfileV1(std::move(profile));
    }

    return profile;
}

std::vector<std::string> describeScenePresentationProfileDelta(
    const ScenePresentationProfile& expected, const ScenePresentationProfile& actual)
{
    std::vector<std::string> delta;
    const auto add = [&delta](bool same, const char* field) {
        if (!same)
        {
            delta.emplace_back(field);
        }
    };

    const SceneLightingProfile& el = expected.lighting;
    const SceneLightingProfile& al = actual.lighting;
    add(el.skyPreset == al.skyPreset, "lighting.skyPreset");
    add(el.skyColor == al.skyColor, "lighting.skyColor");
    add(el.pointLightsEnabled == al.pointLightsEnabled, "lighting.pointLightsEnabled");
    add(el.lightCount == al.lightCount, "lighting.lightCount");
    add(el.sunElevation == al.sunElevation, "lighting.sunElevation");
    add(el.sunAzimuth == al.sunAzimuth, "lighting.sunAzimuth");
    add(el.sunColor == al.sunColor, "lighting.sunColor");
    add(el.sunIntensity == al.sunIntensity, "lighting.sunIntensity");

    const SceneShadowProfile& es = expected.shadows;
    const SceneShadowProfile& as = actual.shadows;
    add(es.csmEnabled == as.csmEnabled, "shadows.csmEnabled");
    add(es.useDdaShadows == as.useDdaShadows, "shadows.useDdaShadows");
    add(es.requestedSunAngularRadius == as.requestedSunAngularRadius,
        "shadows.requestedSunAngularRadius");
    add(es.maxShadowDistance == as.maxShadowDistance, "shadows.maxShadowDistance");
    add(es.normalBias == as.normalBias, "shadows.normalBias");
    add(es.maxSteps == as.maxSteps, "shadows.maxSteps");
    add(es.ddaSunSampleCount == as.ddaSunSampleCount, "shadows.ddaSunSampleCount");
    add(es.foliageOpacity == as.foliageOpacity, "shadows.foliageOpacity");
    add(es.temporalBlendAlpha == as.temporalBlendAlpha, "shadows.temporalBlendAlpha");
    add(es.temporalDepthReject == as.temporalDepthReject, "shadows.temporalDepthReject");
    add(es.temporalNormalRejectDot == as.temporalNormalRejectDot,
        "shadows.temporalNormalRejectDot");
    add(es.temporalClampSharpness == as.temporalClampSharpness,
        "shadows.temporalClampSharpness");
    add(es.spatialFilterRadius == as.spatialFilterRadius, "shadows.spatialFilterRadius");
    add(es.spatialDepthSigma == as.spatialDepthSigma, "shadows.spatialDepthSigma");
    add(es.spatialValueSigma == as.spatialValueSigma, "shadows.spatialValueSigma");
    add(es.spatialNormalPower == as.spatialNormalPower, "shadows.spatialNormalPower");
    add(es.postDenoiseRadius == as.postDenoiseRadius, "shadows.postDenoiseRadius");
    add(es.postDenoiseDepthSigma == as.postDenoiseDepthSigma,
        "shadows.postDenoiseDepthSigma");
    add(es.postDenoiseValueSigma == as.postDenoiseValueSigma,
        "shadows.postDenoiseValueSigma");
    add(es.postDenoiseNormalPower == as.postDenoiseNormalPower,
        "shadows.postDenoiseNormalPower");
    add(es.localLightShadowsEnabled == as.localLightShadowsEnabled,
        "shadows.localLightShadowsEnabled");
    add(es.localShadowCastingLightCount == as.localShadowCastingLightCount,
        "shadows.localShadowCastingLightCount");
    add(es.localTemporalBlendAlpha == as.localTemporalBlendAlpha,
        "shadows.localTemporalBlendAlpha");
    add(es.localTemporalDepthReject == as.localTemporalDepthReject,
        "shadows.localTemporalDepthReject");
    add(es.localBlurEnabled == as.localBlurEnabled, "shadows.localBlurEnabled");
    add(es.localBlurSigma == as.localBlurSigma, "shadows.localBlurSigma");
    add(es.terminatorSoftness == as.terminatorSoftness, "shadows.terminatorSoftness");
    add(es.terminatorMode == as.terminatorMode, "shadows.terminatorMode");
    add(es.csmDitherEnabled == as.csmDitherEnabled, "shadows.csmDitherEnabled");

    const SceneAmbientOcclusionProfile& ea = expected.ambientOcclusion;
    const SceneAmbientOcclusionProfile& aa = actual.ambientOcclusion;
    add(ea.enabled == aa.enabled, "ambientOcclusion.enabled");
    add(ea.distanceMode == aa.distanceMode, "ambientOcclusion.distanceMode");
    add(ea.projectedRadiusPixels == aa.projectedRadiusPixels,
        "ambientOcclusion.projectedRadiusPixels");
    add(ea.projectedMinimumWorldDistance == aa.projectedMinimumWorldDistance,
        "ambientOcclusion.projectedMinimumWorldDistance");
    add(ea.maxDistance == aa.maxDistance, "ambientOcclusion.maxDistance");
    add(ea.stepSize == aa.stepSize, "ambientOcclusion.stepSize");
    add(ea.intensity == aa.intensity, "ambientOcclusion.intensity");
    add(ea.contribution == aa.contribution, "ambientOcclusion.contribution");
    add(ea.bias == aa.bias, "ambientOcclusion.bias");
    add(ea.rayCount == aa.rayCount, "ambientOcclusion.rayCount");
    add(ea.temporalBlendAlpha == aa.temporalBlendAlpha,
        "ambientOcclusion.temporalBlendAlpha");
    add(ea.temporalDepthReject == aa.temporalDepthReject,
        "ambientOcclusion.temporalDepthReject");
    add(ea.temporalNormalRejectDot == aa.temporalNormalRejectDot,
        "ambientOcclusion.temporalNormalRejectDot");

    const ScenePostFxProfile& ep = expected.postFx;
    const ScenePostFxProfile& ap = actual.postFx;
    add(ep.tonemapEnabled == ap.tonemapEnabled, "postFx.tonemapEnabled");
    add(ep.exposure == ap.exposure, "postFx.exposure");
    add(ep.highlightRecovery == ap.highlightRecovery, "postFx.highlightRecovery");
    add(ep.bloomEnabled == ap.bloomEnabled, "postFx.bloomEnabled");
    add(ep.bloomThreshold == ap.bloomThreshold, "postFx.bloomThreshold");
    add(ep.bloomKnee == ap.bloomKnee, "postFx.bloomKnee");
    add(ep.bloomIntensity == ap.bloomIntensity, "postFx.bloomIntensity");
    add(ep.bloomSigma == ap.bloomSigma, "postFx.bloomSigma");
    add(ep.vignetteStrength == ap.vignetteStrength, "postFx.vignetteStrength");
    add(ep.grainStrength == ap.grainStrength, "postFx.grainStrength");
    add(ep.colorGradeEnabled == ap.colorGradeEnabled, "postFx.colorGradeEnabled");
    add(ep.colorGradeStrength == ap.colorGradeStrength, "postFx.colorGradeStrength");
    add(ep.colorGradeSaturation == ap.colorGradeSaturation, "postFx.colorGradeSaturation");
    add(ep.colorGradeContrast == ap.colorGradeContrast, "postFx.colorGradeContrast");
    add(ep.colorGradeTemperature == ap.colorGradeTemperature,
        "postFx.colorGradeTemperature");
    add(ep.pixelizationEnabled == ap.pixelizationEnabled, "postFx.pixelizationEnabled");
    add(ep.pixelizationBlockSize == ap.pixelizationBlockSize,
        "postFx.pixelizationBlockSize");
    add(ep.pixelizationStrength == ap.pixelizationStrength,
        "postFx.pixelizationStrength");
    add(ep.pixelizationEdgeFocus == ap.pixelizationEdgeFocus,
        "postFx.pixelizationEdgeFocus");
    add(ep.depthOfFieldEnabled == ap.depthOfFieldEnabled, "postFx.depthOfFieldEnabled");
    add(ep.depthOfFieldFocusDistance == ap.depthOfFieldFocusDistance,
        "postFx.depthOfFieldFocusDistance");
    add(ep.depthOfFieldFocusRange == ap.depthOfFieldFocusRange,
        "postFx.depthOfFieldFocusRange");
    add(ep.depthOfFieldBlurStrength == ap.depthOfFieldBlurStrength,
        "postFx.depthOfFieldBlurStrength");
    add(ep.taaEnabled == ap.taaEnabled, "postFx.taaEnabled");
    add(ep.jitterEnabled == ap.jitterEnabled, "postFx.jitterEnabled");
    add(ep.taaSimilarityThreshold == ap.taaSimilarityThreshold,
        "postFx.taaSimilarityThreshold");
    add(ep.taaVelocityScale == ap.taaVelocityScale, "postFx.taaVelocityScale");
    add(ep.taaBlendMin == ap.taaBlendMin, "postFx.taaBlendMin");
    add(ep.taaBlendMax == ap.taaBlendMax, "postFx.taaBlendMax");
    add(ep.taaSharpen == ap.taaSharpen, "postFx.taaSharpen");
    add(ep.taaDepthEdgeThreshold == ap.taaDepthEdgeThreshold,
        "postFx.taaDepthEdgeThreshold");
    add(ep.taaCrossFrameDepthThreshold == ap.taaCrossFrameDepthThreshold,
        "postFx.taaCrossFrameDepthThreshold");
    add(ep.taaColorVarianceThreshold == ap.taaColorVarianceThreshold,
        "postFx.taaColorVarianceThreshold");
    add(ep.taaSoftEdgeStrength == ap.taaSoftEdgeStrength,
        "postFx.taaSoftEdgeStrength");
    add(ep.fxaaEnabled == ap.fxaaEnabled, "postFx.fxaaEnabled");

    const SceneVoxelSurfaceProfile& ev = expected.voxelSurface;
    const SceneVoxelSurfaceProfile& av = actual.voxelSurface;
    add(ev.materialDetailStrength == av.materialDetailStrength,
        "voxelSurface.materialDetailStrength");
    add(ev.normalEdgeSmoothing == av.normalEdgeSmoothing,
        "voxelSurface.normalEdgeSmoothing");
    add(ev.pixelEdgeShadowStrength == av.pixelEdgeShadowStrength,
        "voxelSurface.pixelEdgeShadowStrength");
    add(ev.cavityStrength == av.cavityStrength,
        "voxelSurface.cavityStrength");
    add(ev.paintedMaterialStrength == av.paintedMaterialStrength,
        "voxelSurface.paintedMaterialStrength");

    const SceneVoxelCellVariationProfile& ecv = expected.voxelCellVariation;
    const SceneVoxelCellVariationProfile& acv = actual.voxelCellVariation;
    add(ecv.masterStrength == acv.masterStrength,
        "voxelCellVariation.masterStrength");
    add(ecv.genericAmplitude == acv.genericAmplitude,
        "voxelCellVariation.genericAmplitude");
    add(ecv.gravelAmplitude == acv.gravelAmplitude,
        "voxelCellVariation.gravelAmplitude");
    add(ecv.plantAmplitude == acv.plantAmplitude,
        "voxelCellVariation.plantAmplitude");
    add(ecv.stoneAmplitude == acv.stoneAmplitude,
        "voxelCellVariation.stoneAmplitude");
    add(ecv.woodAmplitude == acv.woodAmplitude,
        "voxelCellVariation.woodAmplitude");
    add(ecv.hueSpread == acv.hueSpread,
        "voxelCellVariation.hueSpread");
    add(ecv.saturationSpread == acv.saturationSpread,
        "voxelCellVariation.saturationSpread");
    add(ecv.valueSpread == acv.valueSpread,
        "voxelCellVariation.valueSpread");
    add(ecv.paletteFamilyStrength == acv.paletteFamilyStrength,
        "voxelCellVariation.paletteFamilyStrength");

    const SceneHemisphereAmbientProfile& eha = expected.hemisphereAmbient;
    const SceneHemisphereAmbientProfile& aha = actual.hemisphereAmbient;
    add(eha.strength == aha.strength, "hemisphereAmbient.strength");
    add(eha.skyTint == aha.skyTint, "hemisphereAmbient.skyTint");
    add(eha.groundTint == aha.groundTint, "hemisphereAmbient.groundTint");

    const SceneAtmosphereProfile& eat = expected.atmosphere;
    const SceneAtmosphereProfile& aat = actual.atmosphere;
    add(eat.density == aat.density, "atmosphere.density");
    add(eat.heightFalloff == aat.heightFalloff, "atmosphere.heightFalloff");
    add(eat.baseHeight == aat.baseHeight, "atmosphere.baseHeight");
    add(eat.sunPhaseStrength == aat.sunPhaseStrength,
        "atmosphere.sunPhaseStrength");
    add(eat.sunPhaseExponent == aat.sunPhaseExponent,
        "atmosphere.sunPhaseExponent");

    const ScenePaintedSkyProfile& eps = expected.paintedSky;
    const ScenePaintedSkyProfile& aps = actual.paintedSky;
    add(eps.strength == aps.strength, "paintedSky.strength");
    add(eps.horizonTint == aps.horizonTint, "paintedSky.horizonTint");
    add(eps.zenithTint == aps.zenithTint, "paintedSky.zenithTint");
    add(eps.lowerHemisphereTint == aps.lowerHemisphereTint,
        "paintedSky.lowerHemisphereTint");
    add(eps.gradientExponent == aps.gradientExponent,
        "paintedSky.gradientExponent");
    add(eps.horizonBandStrength == aps.horizonBandStrength,
        "paintedSky.horizonBandStrength");
    add(eps.horizonBandExponent == aps.horizonBandExponent,
        "paintedSky.horizonBandExponent");
    add(eps.sunDiscAngularRadius == aps.sunDiscAngularRadius,
        "paintedSky.sunDiscAngularRadius");
    add(eps.sunDiscSoftness == aps.sunDiscSoftness,
        "paintedSky.sunDiscSoftness");
    add(eps.sunDiscIntensity == aps.sunDiscIntensity,
        "paintedSky.sunDiscIntensity");
    add(eps.sunHaloIntensity == aps.sunHaloIntensity,
        "paintedSky.sunHaloIntensity");
    add(eps.sunHaloExponent == aps.sunHaloExponent,
        "paintedSky.sunHaloExponent");

    const ScenePaintedCloudProfile& epc = expected.paintedClouds;
    const ScenePaintedCloudProfile& apc = actual.paintedClouds;
    add(epc.strength == apc.strength, "paintedClouds.strength");
    add(epc.coverage == apc.coverage, "paintedClouds.coverage");
    add(epc.opacity == apc.opacity, "paintedClouds.opacity");
    add(epc.softness == apc.softness, "paintedClouds.softness");
    add(epc.altitude == apc.altitude, "paintedClouds.altitude");
    add(epc.worldScale == apc.worldScale, "paintedClouds.worldScale");
    add(epc.detailStrength == apc.detailStrength,
        "paintedClouds.detailStrength");
    add(epc.lightTint == apc.lightTint, "paintedClouds.lightTint");
    add(epc.shadowTint == apc.shadowTint, "paintedClouds.shadowTint");
    add(epc.silverLiningStrength == apc.silverLiningStrength,
        "paintedClouds.silverLiningStrength");
    add(epc.horizonFadeStart == apc.horizonFadeStart,
        "paintedClouds.horizonFadeStart");
    add(epc.horizonFadeEnd == apc.horizonFadeEnd,
        "paintedClouds.horizonFadeEnd");

    const SceneWaterProfile& ew = expected.water;
    const SceneWaterProfile& aw = actual.water;
    add(ew.stylizedMode == aw.stylizedMode, "water.stylizedMode");
    add(ew.absorption == aw.absorption, "water.absorption");
    add(ew.refract == aw.refract, "water.refract");
    add(ew.depthScale == aw.depthScale, "water.depthScale");
    add(ew.shallowBias == aw.shallowBias, "water.shallowBias");
    add(ew.depthToMeters == aw.depthToMeters, "water.depthToMeters");
    add(ew.specIntensity == aw.specIntensity, "water.specIntensity");
    add(ew.specPower == aw.specPower, "water.specPower");
    add(ew.waveScale == aw.waveScale, "water.waveScale");
    add(ew.waveAmp == aw.waveAmp, "water.waveAmp");
    add(ew.crestHighlightsEnabled == aw.crestHighlightsEnabled,
        "water.crestHighlightsEnabled");
    add(ew.crestThreshold == aw.crestThreshold, "water.crestThreshold");
    add(ew.crestSoftness == aw.crestSoftness, "water.crestSoftness");
    add(ew.crestIntensity == aw.crestIntensity, "water.crestIntensity");
    add(ew.foamDepthThreshold == aw.foamDepthThreshold, "water.foamDepthThreshold");
    add(ew.foamOpacity == aw.foamOpacity, "water.foamOpacity");
    add(ew.foamScale == aw.foamScale, "water.foamScale");
    add(ew.distortionDepthScale == aw.distortionDepthScale,
        "water.distortionDepthScale");
    add(ew.edgeFadeDepth == aw.edgeFadeDepth, "water.edgeFadeDepth");
    add(ew.bandHardness == aw.bandHardness, "water.bandHardness");
    add(ew.reflectionStrength == aw.reflectionStrength, "water.reflectionStrength");
    add(ew.fresnelBias == aw.fresnelBias, "water.fresnelBias");
    add(ew.causticsEnabled == aw.causticsEnabled, "water.causticsEnabled");
    add(ew.causticsIntensity == aw.causticsIntensity, "water.causticsIntensity");
    add(ew.causticsScale == aw.causticsScale, "water.causticsScale");
    add(ew.causticsSpeed == aw.causticsSpeed, "water.causticsSpeed");
    add(ew.causticsBanding == aw.causticsBanding, "water.causticsBanding");
    add(ew.causticsDepthFade == aw.causticsDepthFade, "water.causticsDepthFade");
    add(ew.particlesPlanned == aw.particlesPlanned, "water.particlesPlanned");
    add(ew.particlesPlannedDensity == aw.particlesPlannedDensity,
        "water.particlesPlannedDensity");
    add(ew.particlesPlannedDrift == aw.particlesPlannedDrift,
        "water.particlesPlannedDrift");
    add(ew.particlesPlannedScale == aw.particlesPlannedScale,
        "water.particlesPlannedScale");
    add(ew.foamEmitterEnabled == aw.foamEmitterEnabled, "water.foamEmitterEnabled");
    add(ew.foamEmitterIntensity == aw.foamEmitterIntensity,
        "water.foamEmitterIntensity");
    add(ew.foamEmitterRadius == aw.foamEmitterRadius, "water.foamEmitterRadius");
    add(ew.foamEmitterOffsetX == aw.foamEmitterOffsetX, "water.foamEmitterOffsetX");
    add(ew.foamEmitterOffsetZ == aw.foamEmitterOffsetZ, "water.foamEmitterOffsetZ");
    add(ew.foamEmitterScale == aw.foamEmitterScale, "water.foamEmitterScale");
    add(ew.foamEmitterSpread == aw.foamEmitterSpread, "water.foamEmitterSpread");
    add(ew.gradientStrength == aw.gradientStrength, "water.gradientStrength");
    add(ew.planarReflectionEnabled == aw.planarReflectionEnabled,
        "water.planarReflectionEnabled");
    add(ew.planarObliqueClipEnabled == aw.planarObliqueClipEnabled,
        "water.planarObliqueClipEnabled");
    add(ew.planarStrength == aw.planarStrength, "water.planarStrength");

    const SceneGlassProfile& eg = expected.glass;
    const SceneGlassProfile& ag = actual.glass;
    add(eg.tint == ag.tint, "glass.tint");
    add(eg.reflection == ag.reflection, "glass.reflection");
    add(eg.absorption == ag.absorption, "glass.absorption");
    add(eg.thicknessScale == ag.thicknessScale, "glass.thicknessScale");
    add(eg.refract == ag.refract, "glass.refract");
    add(eg.ior == ag.ior, "glass.ior");
    add(eg.bubbleScale == ag.bubbleScale, "glass.bubbleScale");
    add(eg.bubbleIntensity == ag.bubbleIntensity, "glass.bubbleIntensity");
    add(eg.bubbleThicknessGate == ag.bubbleThicknessGate, "glass.bubbleThicknessGate");
    add(eg.bubbleChromaticSplit == ag.bubbleChromaticSplit,
        "glass.bubbleChromaticSplit");
    add(eg.iridescentStrength == ag.iridescentStrength, "glass.iridescentStrength");
    add(eg.iridescentFilmThickness == ag.iridescentFilmThickness,
        "glass.iridescentFilmThickness");
    add(eg.iridescentFrequency == ag.iridescentFrequency, "glass.iridescentFrequency");
    add(eg.voxelGlassRefractEnabled == ag.voxelGlassRefractEnabled,
        "glass.voxelGlassRefractEnabled");
    add(eg.voxelGlassAbsorption == ag.voxelGlassAbsorption,
        "glass.voxelGlassAbsorption");
    add(eg.voxelGlassRefractStrength == ag.voxelGlassRefractStrength,
        "glass.voxelGlassRefractStrength");
    add(eg.voxelGlassIor == ag.voxelGlassIor, "glass.voxelGlassIor");
    add(eg.voxelGlassReflectStrength == ag.voxelGlassReflectStrength,
        "glass.voxelGlassReflectStrength");
    add(eg.voxelGlassTint == ag.voxelGlassTint, "glass.voxelGlassTint");
    add(eg.voxelGlassReflectionColor == ag.voxelGlassReflectionColor,
        "glass.voxelGlassReflectionColor");

    return delta;
}

SceneProfileResolution resolveAndApplySceneProfile(
    const SceneConfig& sceneConfig, int& skyPreset, glm::vec3& skyColor,
    engine::render::LightingSettings& lighting, engine::render::ShadowSettings& shadows,
    engine::render::AmbientOcclusionSettings& ambientOcclusion,
    engine::render::PostFxSettings& postFx, engine::render::VoxelDebugSettings& voxel,
    engine::render::WaterSettings& water, engine::render::GlassSettings& glass)
{
    SceneProfileResolution result{};

    ScenePresentationProfileLayers layers{};
    layers.namedBase = namedBaseScenePresentationProfile(sceneConfig);
    layers.serializedScene = sceneConfig.presentationProfile;
    result.namedBaseName = layers.namedBase->name;

    const ResolvedScenePresentationProfile resolved =
        resolveScenePresentationProfile(layers);
    result.source = resolved.source;
    result.appliedName = resolved.profile.name;
    result.serializedApplied =
        resolved.source == ScenePresentationProfileSource::SerializedScene;

    if (!result.serializedApplied)
    {
        // retain the helper-migration audit before replacing live state. a
        // mismatch remains visible without allowing predecessor state to survive.
        const ScenePresentationProfile live = captureScenePresentationProfile(
            "live", skyPreset, skyColor, lighting, shadows, ambientOcclusion, postFx,
            voxel, water, glass);
        result.namedBaseParityDelta =
            describeScenePresentationProfileDelta(resolved.profile, live);

        if (result.namedBaseParityDelta.empty())
        {
            logInfo("Scene", makeLogMessage("Named base profile '",
                                            result.namedBaseName,
                                            "' matches the live scene-default state."));
        }
        else
        {
            constexpr size_t kMaxLoggedFields = 6;
            std::string fields;
            const size_t count = result.namedBaseParityDelta.size();
            for (size_t i = 0; i < count && i < kMaxLoggedFields; ++i)
            {
                if (!fields.empty())
                {
                    fields += ", ";
                }
                fields += result.namedBaseParityDelta[i];
            }
            if (count > kMaxLoggedFields)
            {
                fields += ", ...";
            }
            logWarning(
                "Scene",
                makeLogMessage("Named base profile '", result.namedBaseName,
                               "' differs from the live scene-default state in ",
                               count, " field(s): ", fields));
        }
    }

    applyScenePresentationProfile(resolved.profile, skyPreset, skyColor, lighting,
                                  shadows, ambientOcclusion, postFx, voxel, water,
                                  glass);
    logInfo("Scene",
            makeLogMessage("Applied presentation profile '", resolved.profile.name,
                           "' (source: ",
                           scenePresentationProfileSourceName(resolved.source), ")."));
    return result;
}

} // namespace engine::scene
