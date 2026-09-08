#include "engine/scene/SoftVoxelPresentationProfile.h"

#include <string>
#include <utility>

#include "engine/render/HemisphereAmbient.h"
#include "engine/render/PaintedClouds.h"
#include "engine/render/PaintedSky.h"
#include "engine/render/SceneAtmosphere.h"
#include "engine/voxel/VoxelCellVariation.h"

namespace engine::scene
{

std::optional<ReconstructionComparisonMode>
reconstructionComparisonModeFromName(std::string_view name)
{
    if (name == "no-aa")
    {
        return ReconstructionComparisonMode::NoAa;
    }
    if (name == "fxaa")
    {
        return ReconstructionComparisonMode::Fxaa;
    }
    if (name == "taa")
    {
        return ReconstructionComparisonMode::Taa;
    }
    if (name == "taa-fxaa")
    {
        return ReconstructionComparisonMode::TaaFxaa;
    }
    return std::nullopt;
}

std::string_view
reconstructionComparisonModeName(ReconstructionComparisonMode mode)
{
    switch (mode)
    {
    case ReconstructionComparisonMode::NoAa:
        return "no-aa";
    case ReconstructionComparisonMode::Fxaa:
        return "fxaa";
    case ReconstructionComparisonMode::Taa:
        return "taa";
    case ReconstructionComparisonMode::TaaFxaa:
        return "taa-fxaa";
    }
    return "unknown";
}

ScenePresentationProfile makeSoftOutdoorProfileV0(
    ScenePresentationProfile profile,
    ReconstructionComparisonMode mode)
{
    profile.name =
        std::string("soft-outdoor-v0-") +
        std::string(reconstructionComparisonModeName(mode));

    profile.shadows.csmEnabled = true;
    profile.shadows.useDdaShadows = true;
    profile.shadows.requestedSunAngularRadius = 0.060f;
    profile.shadows.ddaSunSampleCount = 4;
    profile.shadows.foliageOpacity = 0.85f;
    profile.shadows.temporalBlendAlpha = 0.070f;
    profile.shadows.temporalDepthReject = 0.0025f;
    profile.shadows.temporalNormalRejectDot = 0.94f;
    profile.shadows.temporalClampSharpness = 0.75f;
    profile.shadows.spatialFilterRadius = 1;
    profile.shadows.spatialDepthSigma = 0.0040f;
    profile.shadows.spatialValueSigma = 0.22f;
    profile.shadows.spatialNormalPower = 64.0f;
    profile.shadows.postDenoiseRadius = 1;
    profile.shadows.postDenoiseDepthSigma = 0.0050f;
    profile.shadows.postDenoiseValueSigma = 0.16f;
    profile.shadows.postDenoiseNormalPower = 48.0f;

    profile.ambientOcclusion.maxDistance = 5.5f;
    profile.ambientOcclusion.stepSize = 0.15f;
    profile.ambientOcclusion.intensity = 1.15f;
    profile.ambientOcclusion.contribution = 0.18f;
    profile.ambientOcclusion.bias = 0.05f;
    profile.ambientOcclusion.rayCount = 2;
    profile.ambientOcclusion.temporalBlendAlpha = 0.08f;
    profile.ambientOcclusion.temporalDepthReject = 0.02f;
    profile.ambientOcclusion.temporalNormalRejectDot = 0.85f;

    profile.postFx.tonemapEnabled = true;
    profile.postFx.exposure = 1.0f;
    profile.postFx.highlightRecovery = 0.22f;
    profile.postFx.bloomEnabled = true;
    profile.postFx.bloomThreshold = 1.00f;
    profile.postFx.bloomKnee = 0.55f;
    profile.postFx.bloomIntensity = 0.10f;
    profile.postFx.bloomSigma = 3.0f;
    profile.postFx.vignetteStrength = 0.08f;
    profile.postFx.grainStrength = 0.0f;
    profile.postFx.colorGradeEnabled = true;
    profile.postFx.colorGradeStrength = 0.22f;
    profile.postFx.colorGradeSaturation = 1.08f;
    profile.postFx.colorGradeContrast = 0.98f;
    profile.postFx.colorGradeTemperature = 0.02f;
    profile.postFx.pixelizationEnabled = false;
    profile.postFx.depthOfFieldEnabled = false;
    profile.postFx.taaSimilarityThreshold = 0.10f;
    profile.postFx.taaVelocityScale = 12.0f;
    profile.postFx.taaBlendMin = 0.05f;
    profile.postFx.taaBlendMax = 0.16f;
    profile.postFx.taaSharpen = 0.18f;
    profile.postFx.taaDepthEdgeThreshold = 0.08f;
    profile.postFx.taaCrossFrameDepthThreshold = 0.018f;
    profile.postFx.taaColorVarianceThreshold = 0.12f;

    profile.voxelSurface.materialDetailStrength = 0.25f;
    profile.voxelSurface.normalEdgeSmoothing = 0.14f;
    profile.voxelSurface.pixelEdgeShadowStrength = 0.25f;

    // v0-v3 predate the painted-sky slice. explicitly preserve their flat-sky
    // contract even when they are derived from the newer landscape named base.
    profile.paintedSky = {};
    profile.paintedClouds = {};

    profile.postFx.taaEnabled =
        mode == ReconstructionComparisonMode::Taa ||
        mode == ReconstructionComparisonMode::TaaFxaa;
    profile.postFx.jitterEnabled = profile.postFx.taaEnabled;
    profile.postFx.fxaaEnabled =
        mode == ReconstructionComparisonMode::Fxaa ||
        mode == ReconstructionComparisonMode::TaaFxaa;
    return profile;
}

ScenePresentationProfile makeSoftOutdoorProfileV1(
    ScenePresentationProfile profile)
{
    profile = makeSoftOutdoorProfileV0(std::move(profile),
                                       ReconstructionComparisonMode::Taa);
    profile.version = ScenePresentationProfile::kCurrentVersion;
    profile.name = "soft-outdoor-v1";

    const engine::VoxelCellVariationSettings variation =
        engine::makeVoxelCellVariationSettings(
            engine::VoxelCellVariationPreset::SoftOutdoorV0);
    profile.voxelCellVariation.masterStrength = variation.masterStrength;
    profile.voxelCellVariation.genericAmplitude = variation.genericAmplitude;
    profile.voxelCellVariation.gravelAmplitude = variation.gravelAmplitude;
    profile.voxelCellVariation.plantAmplitude = variation.plantAmplitude;
    profile.voxelCellVariation.stoneAmplitude = variation.stoneAmplitude;
    profile.voxelCellVariation.woodAmplitude = variation.woodAmplitude;
    profile.voxelCellVariation.hueSpread = variation.hueSpread;
    profile.voxelCellVariation.saturationSpread = variation.saturationSpread;
    profile.voxelCellVariation.valueSpread = variation.valueSpread;
    profile.voxelCellVariation.paletteFamilyStrength =
        variation.paletteFamilyStrength;

    const engine::render::HemisphereAmbientSettings hemisphere =
        engine::render::makeHemisphereAmbientSettings(
            engine::render::HemisphereAmbientPreset::WarmCoolOutdoorV0);
    profile.hemisphereAmbient.strength = hemisphere.strength;
    profile.hemisphereAmbient.skyTint = hemisphere.skyTint;
    profile.hemisphereAmbient.groundTint = hemisphere.groundTint;

    const engine::render::SceneAtmosphereSettings atmosphere =
        engine::render::makeSceneAtmosphereSettings(
            engine::render::SceneAtmospherePreset::OutdoorHazeV0);
    profile.atmosphere.density = atmosphere.density;
    profile.atmosphere.heightFalloff = atmosphere.heightFalloff;
    profile.atmosphere.baseHeight = atmosphere.baseHeight;
    profile.atmosphere.sunPhaseStrength = atmosphere.sunPhaseStrength;
    profile.atmosphere.sunPhaseExponent = atmosphere.sunPhaseExponent;
    return profile;
}

ScenePresentationProfile makeSoftOutdoorProfileV2(
    ScenePresentationProfile profile)
{
    profile = makeSoftOutdoorProfileV1(std::move(profile));
    profile.version = ScenePresentationProfile::kCurrentVersion;
    profile.name = "soft-outdoor-v2";

    // a one-pixel, edge-local taa reconstruction supplies the watercolor-like
    // integration missing from v1's deliberately crisp stability treatment.
    profile.postFx.taaSoftEdgeStrength = 0.34f;
    profile.postFx.taaSharpen = 0.05f;

    // preserve voxel geometry while making its face response less cut-paper hard.
    profile.voxelSurface.materialDetailStrength = 0.14f;
    profile.voxelSurface.normalEdgeSmoothing = 0.22f;
    profile.voxelSurface.pixelEdgeShadowStrength = 0.10f;
    profile.shadows.terminatorSoftness = 0.26f;
    return profile;
}

ScenePresentationProfile makePaintedOutdoorProfileV3(
    ScenePresentationProfile profile)
{
    profile = makeSoftOutdoorProfileV2(std::move(profile));
    profile.version = ScenePresentationProfile::kCurrentVersion;
    profile.name = "painted-outdoor-v3";

    const engine::VoxelCellVariationSettings variation =
        engine::makeVoxelCellVariationSettings(
            engine::VoxelCellVariationPreset::PaintedOutdoorV0);
    profile.voxelCellVariation.masterStrength = variation.masterStrength;
    profile.voxelCellVariation.genericAmplitude = variation.genericAmplitude;
    profile.voxelCellVariation.gravelAmplitude = variation.gravelAmplitude;
    profile.voxelCellVariation.plantAmplitude = variation.plantAmplitude;
    profile.voxelCellVariation.stoneAmplitude = variation.stoneAmplitude;
    profile.voxelCellVariation.woodAmplitude = variation.woodAmplitude;
    profile.voxelCellVariation.hueSpread = variation.hueSpread;
    profile.voxelCellVariation.saturationSpread = variation.saturationSpread;
    profile.voxelCellVariation.valueSpread = variation.valueSpread;
    profile.voxelCellVariation.paletteFamilyStrength =
        variation.paletteFamilyStrength;

    profile.voxelSurface.cavityStrength = 0.32f;
    profile.voxelSurface.paintedMaterialStrength = 0.82f;
    profile.ambientOcclusion.intensity = 0.88f;
    profile.ambientOcclusion.contribution = 0.12f;

    // a restrained warm grade integrates the deliberately wider local palette
    // without crushing it back into dark, high-contrast clay shading.
    profile.postFx.highlightRecovery = 0.30f;
    profile.postFx.bloomIntensity = 0.12f;
    profile.postFx.colorGradeStrength = 0.34f;
    profile.postFx.colorGradeSaturation = 1.02f;
    profile.postFx.colorGradeContrast = 0.90f;
    profile.postFx.colorGradeTemperature = 0.08f;
    return profile;
}

ScenePresentationProfile makePaintedOutdoorSkyProfileV1(
    ScenePresentationProfile profile)
{
    profile = makePaintedOutdoorProfileV3(std::move(profile));
    profile.version = ScenePresentationProfile::kCurrentVersion;
    profile.name = "painted-outdoor-sky-v1";

    engine::render::PaintedSkyPreset preset =
        engine::render::PaintedSkyPreset::SoftDayV0;
    switch (profile.lighting.skyPreset)
    {
    case 1:
        preset = engine::render::PaintedSkyPreset::WarmSunsetV0;
        break;
    case 2:
        preset = engine::render::PaintedSkyPreset::NightV0;
        break;
    case 3:
        preset = engine::render::PaintedSkyPreset::DawnV0;
        break;
    default:
        break;
    }

    const engine::render::PaintedSkySettings sky =
        engine::render::makePaintedSkySettings(preset);
    profile.paintedSky.strength = sky.strength;
    profile.paintedSky.horizonTint = sky.horizonTint;
    profile.paintedSky.zenithTint = sky.zenithTint;
    profile.paintedSky.lowerHemisphereTint = sky.lowerHemisphereTint;
    profile.paintedSky.gradientExponent = sky.gradientExponent;
    profile.paintedSky.horizonBandStrength = sky.horizonBandStrength;
    profile.paintedSky.horizonBandExponent = sky.horizonBandExponent;
    profile.paintedSky.sunDiscAngularRadius = sky.sunDiscAngularRadius;
    profile.paintedSky.sunDiscSoftness = sky.sunDiscSoftness;
    profile.paintedSky.sunDiscIntensity = sky.sunDiscIntensity;
    profile.paintedSky.sunHaloIntensity = sky.sunHaloIntensity;
    profile.paintedSky.sunHaloExponent = sky.sunHaloExponent;
    return profile;
}

ScenePresentationProfile makePaintedOutdoorCloudProfileV1(
    ScenePresentationProfile profile)
{
    profile = makePaintedOutdoorSkyProfileV1(std::move(profile));
    profile.version = ScenePresentationProfile::kCurrentVersion;
    profile.name = "painted-outdoor-clouds-v1";

    const engine::render::PaintedCloudSettings clouds =
        engine::render::makePaintedCloudSettings(
            engine::render::PaintedCloudPreset::SoftDayV0);
    profile.paintedClouds.strength = clouds.strength;
    profile.paintedClouds.coverage = clouds.coverage;
    profile.paintedClouds.opacity = clouds.opacity;
    profile.paintedClouds.softness = clouds.softness;
    profile.paintedClouds.altitude = clouds.altitude;
    profile.paintedClouds.worldScale = clouds.worldScale;
    profile.paintedClouds.detailStrength = clouds.detailStrength;
    profile.paintedClouds.lightTint = clouds.lightTint;
    profile.paintedClouds.shadowTint = clouds.shadowTint;
    profile.paintedClouds.silverLiningStrength =
        clouds.silverLiningStrength;
    profile.paintedClouds.horizonFadeStart = clouds.horizonFadeStart;
    profile.paintedClouds.horizonFadeEnd = clouds.horizonFadeEnd;
    return profile;
}

} // namespace engine::scene
