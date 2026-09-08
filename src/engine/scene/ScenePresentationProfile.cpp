#include "engine/scene/ScenePresentationProfile.h"

#include <algorithm>
#include <utility>

#include "engine/render/RenderSettings.h"

namespace engine::scene
{

ResolvedScenePresentationProfile
resolveScenePresentationProfile(const ScenePresentationProfileLayers& layers)
{
    ResolvedScenePresentationProfile resolved{layers.schemaDefaults,
                                               ScenePresentationProfileSource::SchemaDefaults};
    if (layers.namedBase)
    {
        resolved = {*layers.namedBase, ScenePresentationProfileSource::NamedBase};
    }
    if (layers.serializedScene)
    {
        resolved = {*layers.serializedScene, ScenePresentationProfileSource::SerializedScene};
    }
    if (layers.liveEdit)
    {
        resolved = {*layers.liveEdit, ScenePresentationProfileSource::LiveEdit};
    }
    return resolved;
}

std::string_view scenePresentationProfileSourceName(ScenePresentationProfileSource source)
{
    switch (source)
    {
    case ScenePresentationProfileSource::SchemaDefaults:
        return "schema-defaults";
    case ScenePresentationProfileSource::NamedBase:
        return "named-base";
    case ScenePresentationProfileSource::SerializedScene:
        return "serialized-scene";
    case ScenePresentationProfileSource::LiveEdit:
        return "live-edit";
    }
    return "unknown";
}

bool effectiveTaaJitterEnabled(bool taaEnabled, bool jitterEnabled,
                               bool debugDisableJitter, bool freezeDebug)
{
    return taaEnabled && jitterEnabled && !debugDisableJitter && !freezeDebug;
}

float maxEffectiveDdaSunAngularRadius(int sampleCount)
{
    return std::min(0.08f,
                    0.012f + 0.010f * static_cast<float>(std::max(sampleCount, 1)));
}

float effectiveDdaSunAngularRadius(float requestedRadius, int sampleCount)
{
    return std::min(requestedRadius, maxEffectiveDdaSunAngularRadius(sampleCount));
}

ScenePresentationProfile captureScenePresentationProfile(
    std::string name,
    int skyPreset,
    const glm::vec3& skyColor,
    const engine::render::LightingSettings& lighting,
    const engine::render::ShadowSettings& shadows,
    const engine::render::AmbientOcclusionSettings& ambientOcclusion,
    const engine::render::PostFxSettings& postFx,
    const engine::render::VoxelDebugSettings& voxel,
    const engine::render::WaterSettings& water,
    const engine::render::GlassSettings& glass)
{
    ScenePresentationProfile profile{};
    profile.name = std::move(name);

    profile.lighting.skyPreset = skyPreset;
    profile.lighting.skyColor = skyColor;
    profile.lighting.pointLightsEnabled = lighting.pointLightsEnabled_;
    profile.lighting.lightCount = lighting.lightCount_;
    profile.lighting.sunElevation = lighting.sunElevation_;
    profile.lighting.sunAzimuth = lighting.sunAzimuth_;
    profile.lighting.sunColor = lighting.sunColor_;
    profile.lighting.sunIntensity = lighting.sunIntensity_;

    profile.shadows.csmEnabled = shadows.csmEnabled_;
    profile.shadows.useDdaShadows = shadows.useDDAShadows_;
    profile.shadows.requestedSunAngularRadius = shadows.sunAngularRadius_;
    profile.shadows.maxShadowDistance = shadows.maxShadowDist_;
    profile.shadows.normalBias = shadows.shadowNormalBias_;
    profile.shadows.maxSteps = shadows.maxShadowSteps_;
    profile.shadows.ddaSunSampleCount = shadows.shadowDdaSunSampleCount_;
    profile.shadows.foliageOpacity = shadows.foliageShadowOpacity_;
    profile.shadows.temporalBlendAlpha = shadows.shadowBlendAlpha_;
    profile.shadows.temporalDepthReject = shadows.shadowDepthReject_;
    profile.shadows.temporalNormalRejectDot = shadows.shadowNormalRejectDot_;
    profile.shadows.temporalClampSharpness = shadows.shadowClampSharpness_;
    profile.shadows.spatialFilterRadius = shadows.shadowSpatialFilterRadius_;
    profile.shadows.spatialDepthSigma = shadows.shadowSpatialDepthSigma_;
    profile.shadows.spatialValueSigma = shadows.shadowSpatialValueSigma_;
    profile.shadows.spatialNormalPower = shadows.shadowSpatialNormalPower_;
    profile.shadows.postDenoiseRadius = shadows.shadowPostDenoiseRadius_;
    profile.shadows.postDenoiseDepthSigma = shadows.shadowPostDenoiseDepthSigma_;
    profile.shadows.postDenoiseValueSigma = shadows.shadowPostDenoiseValueSigma_;
    profile.shadows.postDenoiseNormalPower = shadows.shadowPostDenoiseNormalPower_;
    profile.shadows.localLightShadowsEnabled = shadows.localLightShadowsEnabled_;
    profile.shadows.localShadowCastingLightCount = shadows.localShadowCastingLightCount_;
    profile.shadows.localTemporalBlendAlpha = shadows.localShadowBlendAlpha_;
    profile.shadows.localTemporalDepthReject = shadows.localShadowDepthReject_;
    profile.shadows.localBlurEnabled = shadows.localShadowBlurEnabled_;
    profile.shadows.localBlurSigma = shadows.localShadowBlurSigma_;
    profile.shadows.terminatorSoftness = shadows.terminatorSoftness_;
    profile.shadows.terminatorMode = shadows.terminatorMode_;
    profile.shadows.csmDitherEnabled = shadows.csmDitherEnabled_;

    profile.ambientOcclusion.enabled = ambientOcclusion.aoEnabled_;
    profile.ambientOcclusion.distanceMode = ambientOcclusion.aoDistanceMode_;
    profile.ambientOcclusion.projectedRadiusPixels =
        ambientOcclusion.aoProjectedRadiusPixels_;
    profile.ambientOcclusion.projectedMinimumWorldDistance =
        ambientOcclusion.aoProjectedMinDistance_;
    profile.ambientOcclusion.maxDistance = ambientOcclusion.aoMaxDistance_;
    profile.ambientOcclusion.stepSize = ambientOcclusion.aoStepSize_;
    profile.ambientOcclusion.intensity = ambientOcclusion.aoIntensity_;
    profile.ambientOcclusion.contribution = ambientOcclusion.aoContribution_;
    profile.ambientOcclusion.bias = ambientOcclusion.aoBias_;
    profile.ambientOcclusion.rayCount = ambientOcclusion.aoRayCount_;
    profile.ambientOcclusion.temporalBlendAlpha = ambientOcclusion.aoBlendAlpha_;
    profile.ambientOcclusion.temporalDepthReject = ambientOcclusion.aoDepthReject_;
    profile.ambientOcclusion.temporalNormalRejectDot =
        ambientOcclusion.aoNormalRejectDot_;

    profile.postFx.tonemapEnabled = postFx.tonemapEnabled_;
    profile.postFx.exposure = postFx.exposure_;
    profile.postFx.highlightRecovery = postFx.highlightRecovery_;
    profile.postFx.bloomEnabled = postFx.bloomEnabled_;
    profile.postFx.bloomThreshold = postFx.bloomThreshold_;
    profile.postFx.bloomKnee = postFx.bloomKnee_;
    profile.postFx.bloomIntensity = postFx.bloomIntensity_;
    profile.postFx.bloomSigma = postFx.bloomSigma_;
    profile.postFx.vignetteStrength = postFx.vignetteStrength_;
    profile.postFx.grainStrength = postFx.grainStrength_;
    profile.postFx.colorGradeEnabled = postFx.colorGradeEnabled_;
    profile.postFx.colorGradeStrength = postFx.colorGradeStrength_;
    profile.postFx.colorGradeSaturation = postFx.colorGradeSaturation_;
    profile.postFx.colorGradeContrast = postFx.colorGradeContrast_;
    profile.postFx.colorGradeTemperature = postFx.colorGradeTemperature_;
    profile.postFx.pixelizationEnabled = postFx.postPixelizationEnabled_;
    profile.postFx.pixelizationBlockSize = postFx.postPixelizationBlockSize_;
    profile.postFx.pixelizationStrength = postFx.postPixelizationStrength_;
    profile.postFx.pixelizationEdgeFocus = postFx.postPixelizationEdgeFocus_;
    profile.postFx.depthOfFieldEnabled = postFx.dofEnabled_;
    profile.postFx.depthOfFieldFocusDistance = postFx.dofFocusDistance_;
    profile.postFx.depthOfFieldFocusRange = postFx.dofFocusRange_;
    profile.postFx.depthOfFieldBlurStrength = postFx.dofBlurStrength_;
    profile.postFx.taaEnabled = postFx.taaEnabled_;
    profile.postFx.jitterEnabled = postFx.jitterEnabled_;
    profile.postFx.taaSimilarityThreshold = postFx.taaSimilarityThreshold_;
    profile.postFx.taaVelocityScale = postFx.taaVelocityScale_;
    profile.postFx.taaBlendMin = postFx.taaBlendMin_;
    profile.postFx.taaBlendMax = postFx.taaBlendMax_;
    profile.postFx.taaSharpen = postFx.taaSharpen_;
    profile.postFx.taaDepthEdgeThreshold = postFx.taaDepthEdgeThreshold_;
    profile.postFx.taaCrossFrameDepthThreshold = postFx.taaCrossFrameDepthThreshold_;
    profile.postFx.taaColorVarianceThreshold = postFx.taaColorVarianceThreshold_;
    profile.postFx.taaSoftEdgeStrength = postFx.taaSoftEdgeStrength_;
    profile.postFx.fxaaEnabled = postFx.fxaaEnabled_;

    profile.voxelSurface.materialDetailStrength = postFx.postMaterialDetailStrength_;
    profile.voxelSurface.normalEdgeSmoothing = voxel.voxelNormalEdgeSmoothing_;
    profile.voxelSurface.pixelEdgeShadowStrength = voxel.voxelPixelEdgeShadowStrength_;
    profile.voxelSurface.cavityStrength = voxel.voxelCavityStrength_;
    profile.voxelSurface.paintedMaterialStrength =
        voxel.voxelPaintedMaterialStrength_;

    profile.voxelCellVariation.masterStrength =
        voxel.voxelCellVariation_.masterStrength;
    profile.voxelCellVariation.genericAmplitude =
        voxel.voxelCellVariation_.genericAmplitude;
    profile.voxelCellVariation.gravelAmplitude =
        voxel.voxelCellVariation_.gravelAmplitude;
    profile.voxelCellVariation.plantAmplitude =
        voxel.voxelCellVariation_.plantAmplitude;
    profile.voxelCellVariation.stoneAmplitude =
        voxel.voxelCellVariation_.stoneAmplitude;
    profile.voxelCellVariation.woodAmplitude =
        voxel.voxelCellVariation_.woodAmplitude;
    profile.voxelCellVariation.hueSpread = voxel.voxelCellVariation_.hueSpread;
    profile.voxelCellVariation.saturationSpread =
        voxel.voxelCellVariation_.saturationSpread;
    profile.voxelCellVariation.valueSpread = voxel.voxelCellVariation_.valueSpread;
    profile.voxelCellVariation.paletteFamilyStrength =
        voxel.voxelCellVariation_.paletteFamilyStrength;

    profile.hemisphereAmbient.strength = lighting.hemisphereAmbient_.strength;
    profile.hemisphereAmbient.skyTint = lighting.hemisphereAmbient_.skyTint;
    profile.hemisphereAmbient.groundTint = lighting.hemisphereAmbient_.groundTint;

    profile.atmosphere.density = lighting.sceneAtmosphere_.density;
    profile.atmosphere.heightFalloff = lighting.sceneAtmosphere_.heightFalloff;
    profile.atmosphere.baseHeight = lighting.sceneAtmosphere_.baseHeight;
    profile.atmosphere.sunPhaseStrength =
        lighting.sceneAtmosphere_.sunPhaseStrength;
    profile.atmosphere.sunPhaseExponent =
        lighting.sceneAtmosphere_.sunPhaseExponent;

    profile.paintedSky.strength = lighting.paintedSky_.strength;
    profile.paintedSky.horizonTint = lighting.paintedSky_.horizonTint;
    profile.paintedSky.zenithTint = lighting.paintedSky_.zenithTint;
    profile.paintedSky.lowerHemisphereTint =
        lighting.paintedSky_.lowerHemisphereTint;
    profile.paintedSky.gradientExponent = lighting.paintedSky_.gradientExponent;
    profile.paintedSky.horizonBandStrength =
        lighting.paintedSky_.horizonBandStrength;
    profile.paintedSky.horizonBandExponent =
        lighting.paintedSky_.horizonBandExponent;
    profile.paintedSky.sunDiscAngularRadius =
        lighting.paintedSky_.sunDiscAngularRadius;
    profile.paintedSky.sunDiscSoftness = lighting.paintedSky_.sunDiscSoftness;
    profile.paintedSky.sunDiscIntensity = lighting.paintedSky_.sunDiscIntensity;
    profile.paintedSky.sunHaloIntensity = lighting.paintedSky_.sunHaloIntensity;
    profile.paintedSky.sunHaloExponent = lighting.paintedSky_.sunHaloExponent;

    profile.paintedClouds.strength = lighting.paintedClouds_.strength;
    profile.paintedClouds.coverage = lighting.paintedClouds_.coverage;
    profile.paintedClouds.opacity = lighting.paintedClouds_.opacity;
    profile.paintedClouds.softness = lighting.paintedClouds_.softness;
    profile.paintedClouds.altitude = lighting.paintedClouds_.altitude;
    profile.paintedClouds.worldScale = lighting.paintedClouds_.worldScale;
    profile.paintedClouds.detailStrength =
        lighting.paintedClouds_.detailStrength;
    profile.paintedClouds.lightTint = lighting.paintedClouds_.lightTint;
    profile.paintedClouds.shadowTint = lighting.paintedClouds_.shadowTint;
    profile.paintedClouds.silverLiningStrength =
        lighting.paintedClouds_.silverLiningStrength;
    profile.paintedClouds.horizonFadeStart =
        lighting.paintedClouds_.horizonFadeStart;
    profile.paintedClouds.horizonFadeEnd =
        lighting.paintedClouds_.horizonFadeEnd;

    profile.water.stylizedMode = water.waterStylizedMode_;
    profile.water.absorption = water.waterAbsorption_;
    profile.water.refract = water.waterRefract_;
    profile.water.depthScale = water.waterDepthScale_;
    profile.water.shallowBias = water.waterShallowBias_;
    profile.water.depthToMeters = water.waterDepthToMeters_;
    profile.water.specIntensity = water.waterSpecIntensity_;
    profile.water.specPower = water.waterSpecPower_;
    profile.water.waveScale = water.waterWaveScale_;
    profile.water.waveAmp = water.waterWaveAmp_;
    profile.water.crestHighlightsEnabled = water.waterCrestHighlightsEnabled_;
    profile.water.crestThreshold = water.waterCrestThreshold_;
    profile.water.crestSoftness = water.waterCrestSoftness_;
    profile.water.crestIntensity = water.waterCrestIntensity_;
    profile.water.foamDepthThreshold = water.waterFoamDepthThreshold_;
    profile.water.foamOpacity = water.waterFoamOpacity_;
    profile.water.foamScale = water.waterFoamScale_;
    profile.water.distortionDepthScale = water.waterDistortionDepthScale_;
    profile.water.edgeFadeDepth = water.waterEdgeFadeDepth_;
    profile.water.bandHardness = water.waterBandHardness_;
    profile.water.reflectionStrength = water.waterReflectionStrength_;
    profile.water.fresnelBias = water.waterFresnelBias_;
    profile.water.causticsEnabled = water.waterCausticsEnabled_;
    profile.water.causticsIntensity = water.waterCausticsIntensity_;
    profile.water.causticsScale = water.waterCausticsScale_;
    profile.water.causticsSpeed = water.waterCausticsSpeed_;
    profile.water.causticsBanding = water.waterCausticsBanding_;
    profile.water.causticsDepthFade = water.waterCausticsDepthFade_;
    profile.water.particlesPlanned = water.waterParticlesPlanned_;
    profile.water.particlesPlannedDensity = water.waterParticlesPlannedDensity_;
    profile.water.particlesPlannedDrift = water.waterParticlesPlannedDrift_;
    profile.water.particlesPlannedScale = water.waterParticlesPlannedScale_;
    profile.water.foamEmitterEnabled = water.waterFoamEmitterEnabled_;
    profile.water.foamEmitterIntensity = water.waterFoamEmitterIntensity_;
    profile.water.foamEmitterRadius = water.waterFoamEmitterRadius_;
    profile.water.foamEmitterOffsetX = water.waterFoamEmitterOffsetX_;
    profile.water.foamEmitterOffsetZ = water.waterFoamEmitterOffsetZ_;
    profile.water.foamEmitterScale = water.waterFoamEmitterScale_;
    profile.water.foamEmitterSpread = water.waterFoamEmitterSpread_;
    profile.water.gradientStrength = water.waterGradientStrength_;
    profile.water.planarReflectionEnabled = water.waterPlanarReflectionEnabled_;
    profile.water.planarObliqueClipEnabled = water.waterPlanarObliqueClipEnabled_;
    profile.water.planarStrength = water.waterPlanarStrength_;

    profile.glass.tint = glass.glassTint_;
    profile.glass.reflection = glass.glassReflection_;
    profile.glass.absorption = glass.glassAbsorption_;
    profile.glass.thicknessScale = glass.glassThicknessScale_;
    profile.glass.refract = glass.glassRefract_;
    profile.glass.ior = glass.glassIor_;
    profile.glass.bubbleScale = glass.glassBubbleScale_;
    profile.glass.bubbleIntensity = glass.glassBubbleIntensity_;
    profile.glass.bubbleThicknessGate = glass.glassBubbleThicknessGate_;
    profile.glass.bubbleChromaticSplit = glass.glassBubbleChromaticSplit_;
    profile.glass.iridescentStrength = glass.glassIridescentStrength_;
    profile.glass.iridescentFilmThickness = glass.glassIridescentFilmThickness_;
    profile.glass.iridescentFrequency = glass.glassIridescentFrequency_;
    profile.glass.voxelGlassRefractEnabled = glass.voxelGlassRefractEnabled_;
    profile.glass.voxelGlassAbsorption = glass.voxelGlassAbsorption_;
    profile.glass.voxelGlassRefractStrength = glass.voxelGlassRefractStrength_;
    profile.glass.voxelGlassIor = glass.voxelGlassIOR_;
    profile.glass.voxelGlassReflectStrength = glass.voxelGlassReflectStrength_;
    profile.glass.voxelGlassTint = glass.voxelGlassTint_;
    profile.glass.voxelGlassReflectionColor = glass.voxelGlassReflectionColor_;
    return profile;
}

void applyScenePresentationProfile(
    const ScenePresentationProfile& profile,
    int& skyPreset,
    glm::vec3& skyColor,
    engine::render::LightingSettings& lighting,
    engine::render::ShadowSettings& shadows,
    engine::render::AmbientOcclusionSettings& ambientOcclusion,
    engine::render::PostFxSettings& postFx,
    engine::render::VoxelDebugSettings& voxel,
    engine::render::WaterSettings& water,
    engine::render::GlassSettings& glass)
{
    skyPreset = profile.lighting.skyPreset;
    skyColor = profile.lighting.skyColor;
    lighting.pointLightsEnabled_ = profile.lighting.pointLightsEnabled;
    lighting.lightCount_ = profile.lighting.lightCount;
    lighting.sunElevation_ = profile.lighting.sunElevation;
    lighting.sunAzimuth_ = profile.lighting.sunAzimuth;
    lighting.sunColor_ = profile.lighting.sunColor;
    lighting.sunIntensity_ = profile.lighting.sunIntensity;

    shadows.csmEnabled_ = profile.shadows.csmEnabled;
    shadows.useDDAShadows_ = profile.shadows.useDdaShadows;
    shadows.sunAngularRadius_ = profile.shadows.requestedSunAngularRadius;
    shadows.maxShadowDist_ = profile.shadows.maxShadowDistance;
    shadows.shadowNormalBias_ = profile.shadows.normalBias;
    shadows.maxShadowSteps_ = profile.shadows.maxSteps;
    shadows.shadowDdaSunSampleCount_ = profile.shadows.ddaSunSampleCount;
    shadows.foliageShadowOpacity_ = profile.shadows.foliageOpacity;
    shadows.shadowBlendAlpha_ = profile.shadows.temporalBlendAlpha;
    shadows.shadowDepthReject_ = profile.shadows.temporalDepthReject;
    shadows.shadowNormalRejectDot_ = profile.shadows.temporalNormalRejectDot;
    shadows.shadowClampSharpness_ = profile.shadows.temporalClampSharpness;
    shadows.shadowSpatialFilterRadius_ = profile.shadows.spatialFilterRadius;
    shadows.shadowSpatialDepthSigma_ = profile.shadows.spatialDepthSigma;
    shadows.shadowSpatialValueSigma_ = profile.shadows.spatialValueSigma;
    shadows.shadowSpatialNormalPower_ = profile.shadows.spatialNormalPower;
    shadows.shadowPostDenoiseRadius_ = profile.shadows.postDenoiseRadius;
    shadows.shadowPostDenoiseDepthSigma_ = profile.shadows.postDenoiseDepthSigma;
    shadows.shadowPostDenoiseValueSigma_ = profile.shadows.postDenoiseValueSigma;
    shadows.shadowPostDenoiseNormalPower_ = profile.shadows.postDenoiseNormalPower;
    shadows.localLightShadowsEnabled_ = profile.shadows.localLightShadowsEnabled;
    shadows.localShadowCastingLightCount_ =
        profile.shadows.localShadowCastingLightCount;
    shadows.localShadowBlendAlpha_ = profile.shadows.localTemporalBlendAlpha;
    shadows.localShadowDepthReject_ = profile.shadows.localTemporalDepthReject;
    shadows.localShadowBlurEnabled_ = profile.shadows.localBlurEnabled;
    shadows.localShadowBlurSigma_ = profile.shadows.localBlurSigma;
    shadows.terminatorSoftness_ = profile.shadows.terminatorSoftness;
    shadows.terminatorMode_ = profile.shadows.terminatorMode;
    shadows.csmDitherEnabled_ = profile.shadows.csmDitherEnabled;

    ambientOcclusion.aoEnabled_ = profile.ambientOcclusion.enabled;
    ambientOcclusion.aoDistanceMode_ = profile.ambientOcclusion.distanceMode;
    ambientOcclusion.aoProjectedRadiusPixels_ =
        profile.ambientOcclusion.projectedRadiusPixels;
    ambientOcclusion.aoProjectedMinDistance_ =
        profile.ambientOcclusion.projectedMinimumWorldDistance;
    ambientOcclusion.aoMaxDistance_ = profile.ambientOcclusion.maxDistance;
    ambientOcclusion.aoStepSize_ = profile.ambientOcclusion.stepSize;
    ambientOcclusion.aoIntensity_ = profile.ambientOcclusion.intensity;
    ambientOcclusion.aoContribution_ = profile.ambientOcclusion.contribution;
    ambientOcclusion.aoBias_ = profile.ambientOcclusion.bias;
    ambientOcclusion.aoRayCount_ = profile.ambientOcclusion.rayCount;
    ambientOcclusion.aoBlendAlpha_ = profile.ambientOcclusion.temporalBlendAlpha;
    ambientOcclusion.aoDepthReject_ = profile.ambientOcclusion.temporalDepthReject;
    ambientOcclusion.aoNormalRejectDot_ =
        profile.ambientOcclusion.temporalNormalRejectDot;

    postFx.tonemapEnabled_ = profile.postFx.tonemapEnabled;
    postFx.exposure_ = profile.postFx.exposure;
    postFx.highlightRecovery_ = profile.postFx.highlightRecovery;
    postFx.bloomEnabled_ = profile.postFx.bloomEnabled;
    postFx.bloomThreshold_ = profile.postFx.bloomThreshold;
    postFx.bloomKnee_ = profile.postFx.bloomKnee;
    postFx.bloomIntensity_ = profile.postFx.bloomIntensity;
    postFx.bloomSigma_ = profile.postFx.bloomSigma;
    postFx.vignetteStrength_ = profile.postFx.vignetteStrength;
    postFx.grainStrength_ = profile.postFx.grainStrength;
    postFx.colorGradeEnabled_ = profile.postFx.colorGradeEnabled;
    postFx.colorGradeStrength_ = profile.postFx.colorGradeStrength;
    postFx.colorGradeSaturation_ = profile.postFx.colorGradeSaturation;
    postFx.colorGradeContrast_ = profile.postFx.colorGradeContrast;
    postFx.colorGradeTemperature_ = profile.postFx.colorGradeTemperature;
    postFx.postPixelizationEnabled_ = profile.postFx.pixelizationEnabled;
    postFx.postPixelizationBlockSize_ = profile.postFx.pixelizationBlockSize;
    postFx.postPixelizationStrength_ = profile.postFx.pixelizationStrength;
    postFx.postPixelizationEdgeFocus_ = profile.postFx.pixelizationEdgeFocus;
    postFx.dofEnabled_ = profile.postFx.depthOfFieldEnabled;
    postFx.dofFocusDistance_ = profile.postFx.depthOfFieldFocusDistance;
    postFx.dofFocusRange_ = profile.postFx.depthOfFieldFocusRange;
    postFx.dofBlurStrength_ = profile.postFx.depthOfFieldBlurStrength;
    postFx.taaEnabled_ = profile.postFx.taaEnabled;
    postFx.jitterEnabled_ = profile.postFx.jitterEnabled;
    postFx.taaSimilarityThreshold_ = profile.postFx.taaSimilarityThreshold;
    postFx.taaVelocityScale_ = profile.postFx.taaVelocityScale;
    postFx.taaBlendMin_ = profile.postFx.taaBlendMin;
    postFx.taaBlendMax_ = profile.postFx.taaBlendMax;
    postFx.taaSharpen_ = profile.postFx.taaSharpen;
    postFx.taaDepthEdgeThreshold_ = profile.postFx.taaDepthEdgeThreshold;
    postFx.taaCrossFrameDepthThreshold_ = profile.postFx.taaCrossFrameDepthThreshold;
    postFx.taaColorVarianceThreshold_ = profile.postFx.taaColorVarianceThreshold;
    postFx.taaSoftEdgeStrength_ = profile.postFx.taaSoftEdgeStrength;
    postFx.fxaaEnabled_ = profile.postFx.fxaaEnabled;

    postFx.postMaterialDetailStrength_ = profile.voxelSurface.materialDetailStrength;
    voxel.voxelNormalEdgeSmoothing_ = profile.voxelSurface.normalEdgeSmoothing;
    voxel.voxelPixelEdgeShadowStrength_ = profile.voxelSurface.pixelEdgeShadowStrength;
    voxel.voxelCavityStrength_ = profile.voxelSurface.cavityStrength;
    voxel.voxelPaintedMaterialStrength_ =
        profile.voxelSurface.paintedMaterialStrength;

    voxel.voxelCellVariation_.masterStrength =
        profile.voxelCellVariation.masterStrength;
    voxel.voxelCellVariation_.genericAmplitude =
        profile.voxelCellVariation.genericAmplitude;
    voxel.voxelCellVariation_.gravelAmplitude =
        profile.voxelCellVariation.gravelAmplitude;
    voxel.voxelCellVariation_.plantAmplitude =
        profile.voxelCellVariation.plantAmplitude;
    voxel.voxelCellVariation_.stoneAmplitude =
        profile.voxelCellVariation.stoneAmplitude;
    voxel.voxelCellVariation_.woodAmplitude =
        profile.voxelCellVariation.woodAmplitude;
    voxel.voxelCellVariation_.hueSpread = profile.voxelCellVariation.hueSpread;
    voxel.voxelCellVariation_.saturationSpread =
        profile.voxelCellVariation.saturationSpread;
    voxel.voxelCellVariation_.valueSpread = profile.voxelCellVariation.valueSpread;
    voxel.voxelCellVariation_.paletteFamilyStrength =
        profile.voxelCellVariation.paletteFamilyStrength;

    lighting.hemisphereAmbient_.strength = profile.hemisphereAmbient.strength;
    lighting.hemisphereAmbient_.skyTint = profile.hemisphereAmbient.skyTint;
    lighting.hemisphereAmbient_.groundTint = profile.hemisphereAmbient.groundTint;

    lighting.sceneAtmosphere_.density = profile.atmosphere.density;
    lighting.sceneAtmosphere_.heightFalloff = profile.atmosphere.heightFalloff;
    lighting.sceneAtmosphere_.baseHeight = profile.atmosphere.baseHeight;
    lighting.sceneAtmosphere_.sunPhaseStrength =
        profile.atmosphere.sunPhaseStrength;
    lighting.sceneAtmosphere_.sunPhaseExponent =
        profile.atmosphere.sunPhaseExponent;

    lighting.paintedSky_.strength = profile.paintedSky.strength;
    lighting.paintedSky_.horizonTint = profile.paintedSky.horizonTint;
    lighting.paintedSky_.zenithTint = profile.paintedSky.zenithTint;
    lighting.paintedSky_.lowerHemisphereTint =
        profile.paintedSky.lowerHemisphereTint;
    lighting.paintedSky_.gradientExponent = profile.paintedSky.gradientExponent;
    lighting.paintedSky_.horizonBandStrength =
        profile.paintedSky.horizonBandStrength;
    lighting.paintedSky_.horizonBandExponent =
        profile.paintedSky.horizonBandExponent;
    lighting.paintedSky_.sunDiscAngularRadius =
        profile.paintedSky.sunDiscAngularRadius;
    lighting.paintedSky_.sunDiscSoftness = profile.paintedSky.sunDiscSoftness;
    lighting.paintedSky_.sunDiscIntensity = profile.paintedSky.sunDiscIntensity;
    lighting.paintedSky_.sunHaloIntensity = profile.paintedSky.sunHaloIntensity;
    lighting.paintedSky_.sunHaloExponent = profile.paintedSky.sunHaloExponent;
    lighting.paintedSky_ =
        engine::render::sanitizePaintedSkySettings(lighting.paintedSky_);

    lighting.paintedClouds_.strength = profile.paintedClouds.strength;
    lighting.paintedClouds_.coverage = profile.paintedClouds.coverage;
    lighting.paintedClouds_.opacity = profile.paintedClouds.opacity;
    lighting.paintedClouds_.softness = profile.paintedClouds.softness;
    lighting.paintedClouds_.altitude = profile.paintedClouds.altitude;
    lighting.paintedClouds_.worldScale = profile.paintedClouds.worldScale;
    lighting.paintedClouds_.detailStrength =
        profile.paintedClouds.detailStrength;
    lighting.paintedClouds_.lightTint = profile.paintedClouds.lightTint;
    lighting.paintedClouds_.shadowTint = profile.paintedClouds.shadowTint;
    lighting.paintedClouds_.silverLiningStrength =
        profile.paintedClouds.silverLiningStrength;
    lighting.paintedClouds_.horizonFadeStart =
        profile.paintedClouds.horizonFadeStart;
    lighting.paintedClouds_.horizonFadeEnd =
        profile.paintedClouds.horizonFadeEnd;
    lighting.paintedClouds_ =
        engine::render::sanitizePaintedCloudSettings(lighting.paintedClouds_);

    water.waterStylizedMode_ = profile.water.stylizedMode;
    water.waterAbsorption_ = profile.water.absorption;
    water.waterRefract_ = profile.water.refract;
    water.waterDepthScale_ = profile.water.depthScale;
    water.waterShallowBias_ = profile.water.shallowBias;
    water.waterDepthToMeters_ = profile.water.depthToMeters;
    water.waterSpecIntensity_ = profile.water.specIntensity;
    water.waterSpecPower_ = profile.water.specPower;
    water.waterWaveScale_ = profile.water.waveScale;
    water.waterWaveAmp_ = profile.water.waveAmp;
    water.waterCrestHighlightsEnabled_ = profile.water.crestHighlightsEnabled;
    water.waterCrestThreshold_ = profile.water.crestThreshold;
    water.waterCrestSoftness_ = profile.water.crestSoftness;
    water.waterCrestIntensity_ = profile.water.crestIntensity;
    water.waterFoamDepthThreshold_ = profile.water.foamDepthThreshold;
    water.waterFoamOpacity_ = profile.water.foamOpacity;
    water.waterFoamScale_ = profile.water.foamScale;
    water.waterDistortionDepthScale_ = profile.water.distortionDepthScale;
    water.waterEdgeFadeDepth_ = profile.water.edgeFadeDepth;
    water.waterBandHardness_ = profile.water.bandHardness;
    water.waterReflectionStrength_ = profile.water.reflectionStrength;
    water.waterFresnelBias_ = profile.water.fresnelBias;
    water.waterCausticsEnabled_ = profile.water.causticsEnabled;
    water.waterCausticsIntensity_ = profile.water.causticsIntensity;
    water.waterCausticsScale_ = profile.water.causticsScale;
    water.waterCausticsSpeed_ = profile.water.causticsSpeed;
    water.waterCausticsBanding_ = profile.water.causticsBanding;
    water.waterCausticsDepthFade_ = profile.water.causticsDepthFade;
    water.waterParticlesPlanned_ = profile.water.particlesPlanned;
    water.waterParticlesPlannedDensity_ = profile.water.particlesPlannedDensity;
    water.waterParticlesPlannedDrift_ = profile.water.particlesPlannedDrift;
    water.waterParticlesPlannedScale_ = profile.water.particlesPlannedScale;
    water.waterFoamEmitterEnabled_ = profile.water.foamEmitterEnabled;
    water.waterFoamEmitterIntensity_ = profile.water.foamEmitterIntensity;
    water.waterFoamEmitterRadius_ = profile.water.foamEmitterRadius;
    water.waterFoamEmitterOffsetX_ = profile.water.foamEmitterOffsetX;
    water.waterFoamEmitterOffsetZ_ = profile.water.foamEmitterOffsetZ;
    water.waterFoamEmitterScale_ = profile.water.foamEmitterScale;
    water.waterFoamEmitterSpread_ = profile.water.foamEmitterSpread;
    water.waterGradientStrength_ = profile.water.gradientStrength;
    water.waterPlanarReflectionEnabled_ = profile.water.planarReflectionEnabled;
    water.waterPlanarObliqueClipEnabled_ = profile.water.planarObliqueClipEnabled;
    water.waterPlanarStrength_ = profile.water.planarStrength;

    glass.glassTint_ = profile.glass.tint;
    glass.glassReflection_ = profile.glass.reflection;
    glass.glassAbsorption_ = profile.glass.absorption;
    glass.glassThicknessScale_ = profile.glass.thicknessScale;
    glass.glassRefract_ = profile.glass.refract;
    glass.glassIor_ = profile.glass.ior;
    glass.glassBubbleScale_ = profile.glass.bubbleScale;
    glass.glassBubbleIntensity_ = profile.glass.bubbleIntensity;
    glass.glassBubbleThicknessGate_ = profile.glass.bubbleThicknessGate;
    glass.glassBubbleChromaticSplit_ = profile.glass.bubbleChromaticSplit;
    glass.glassIridescentStrength_ = profile.glass.iridescentStrength;
    glass.glassIridescentFilmThickness_ = profile.glass.iridescentFilmThickness;
    glass.glassIridescentFrequency_ = profile.glass.iridescentFrequency;
    glass.voxelGlassRefractEnabled_ = profile.glass.voxelGlassRefractEnabled;
    glass.voxelGlassAbsorption_ = profile.glass.voxelGlassAbsorption;
    glass.voxelGlassRefractStrength_ = profile.glass.voxelGlassRefractStrength;
    glass.voxelGlassIOR_ = profile.glass.voxelGlassIor;
    glass.voxelGlassReflectStrength_ = profile.glass.voxelGlassReflectStrength;
    glass.voxelGlassTint_ = profile.glass.voxelGlassTint;
    glass.voxelGlassReflectionColor_ = profile.glass.voxelGlassReflectionColor;
}

} // namespace engine::scene
