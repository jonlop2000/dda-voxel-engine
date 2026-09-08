#include "App/Input.h"
#include "App/ScenePresentationProfileRuntime.h"
#include "engine/render/RenderSettings.h"
#include "engine/render/SceneAtmosphere.h"
#include "engine/scene/SceneConfig.h"
#include "engine/scene/ScenePresentationProfile.h"
#include "engine/scene/ScenePresentationProfileVariants.h"
#include "engine/scene/SoftVoxelPresentationProfile.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{

using engine::scene::ScenePresentationProfile;
using engine::scene::ScenePresentationProfileLayers;
using engine::scene::ScenePresentationProfileSource;

void require(bool condition, const std::string& message)
{
    if (!condition)
    {
        throw std::runtime_error(message);
    }
}

bool nearlyEqual(float lhs, float rhs, float epsilon = 1e-6f)
{
    return std::fabs(lhs - rhs) <= epsilon;
}

bool containsField(const std::vector<std::string>& fields,
                   const std::string& expected)
{
    return std::find(fields.begin(), fields.end(), expected) != fields.end();
}

SceneConfig makeNamedSceneConfig(const std::string& name, bool aquarium,
                                 bool procedural);

void testLegacyProfileMatchesRenderDefaults()
{
    const ScenePresentationProfile profile{};
    const engine::render::LightingSettings lighting{};
    const engine::render::ShadowSettings shadows{};
    const engine::render::AmbientOcclusionSettings ao{};
    const engine::render::PostFxSettings postFx{};
    const engine::render::VoxelDebugSettings voxel{};
    const engine::render::WaterSettings water{};
    const engine::render::GlassSettings glass{};

    const ScenePresentationProfile captured =
        engine::scene::captureScenePresentationProfile(
            "legacy-v0", 0, glm::vec3(0.09f, 0.29f, 0.88f), lighting, shadows, ao,
            postFx, voxel, water, glass);

    require(profile == captured,
            "Legacy profile defaults must match the current persistent render defaults");
    require(profile.version == ScenePresentationProfile::kCurrentVersion,
            "Default profile must use the current profile schema version");
}

void testProfileResolutionPrecedence()
{
    ScenePresentationProfileLayers layers{};
    layers.schemaDefaults.name = "schema";

    ScenePresentationProfile named = layers.schemaDefaults;
    named.name = "named";
    named.postFx.exposure = 1.25f;
    layers.namedBase = named;
    auto resolved = engine::scene::resolveScenePresentationProfile(layers);
    require(resolved.source == ScenePresentationProfileSource::NamedBase &&
                resolved.profile == named,
            "Named profile must override schema defaults");

    ScenePresentationProfile serialized = named;
    serialized.name = "serialized";
    serialized.postFx.exposure = 1.5f;
    layers.serializedScene = serialized;
    resolved = engine::scene::resolveScenePresentationProfile(layers);
    require(resolved.source == ScenePresentationProfileSource::SerializedScene &&
                resolved.profile == serialized,
            "Serialized scene profile must override the named profile");

    ScenePresentationProfile live = serialized;
    live.name = "live";
    live.postFx.exposure = 1.75f;
    layers.liveEdit = live;
    resolved = engine::scene::resolveScenePresentationProfile(layers);
    require(resolved.source == ScenePresentationProfileSource::LiveEdit &&
                resolved.profile == live,
            "Deliberate live edits must be the final profile layer");

    require(engine::scene::scenePresentationProfileSourceName(resolved.source) ==
                "live-edit",
            "Resolved profile source must have stable debug text");
}

void testEffectiveTaaJitterAuthority()
{
    using engine::scene::effectiveTaaJitterEnabled;

    require(effectiveTaaJitterEnabled(true, true, false, false),
            "TAA jitter must run only when both persistent controls enable it");
    require(!effectiveTaaJitterEnabled(false, true, false, false),
            "TAA disable must suppress jitter");
    require(!effectiveTaaJitterEnabled(true, false, false, false),
            "The presentation-profile jitter switch must be authoritative");
    require(!effectiveTaaJitterEnabled(true, true, true, false),
            "The voxel debug-disable override must suppress jitter");
    require(!effectiveTaaJitterEnabled(true, true, false, true),
            "Debug freeze must suppress jitter");
}

void testSoftOutdoorProfileV0ReconstructionMatrix()
{
    using engine::scene::ReconstructionComparisonMode;
    using engine::scene::makeSoftOutdoorProfileV0;

    require(engine::scene::reconstructionComparisonModeFromName("no-aa") ==
                ReconstructionComparisonMode::NoAa &&
                engine::scene::reconstructionComparisonModeFromName("fxaa") ==
                    ReconstructionComparisonMode::Fxaa &&
                engine::scene::reconstructionComparisonModeFromName("taa") ==
                    ReconstructionComparisonMode::Taa &&
                engine::scene::reconstructionComparisonModeFromName("taa-fxaa") ==
                    ReconstructionComparisonMode::TaaFxaa &&
                !engine::scene::reconstructionComparisonModeFromName("unknown"),
            "Reconstruction comparison mode names must be stable and reject typos");

    const SceneConfig pond =
        makeNamedSceneConfig("nature_pond_probe", false, true);
    const ScenePresentationProfile base =
        engine::scene::namedBaseScenePresentationProfile(pond);
    const ScenePresentationProfile noAa =
        makeSoftOutdoorProfileV0(base, ReconstructionComparisonMode::NoAa);
    const ScenePresentationProfile fxaa =
        makeSoftOutdoorProfileV0(base, ReconstructionComparisonMode::Fxaa);
    const ScenePresentationProfile taa =
        makeSoftOutdoorProfileV0(base, ReconstructionComparisonMode::Taa);
    const ScenePresentationProfile taaFxaa =
        makeSoftOutdoorProfileV0(base, ReconstructionComparisonMode::TaaFxaa);

    require(noAa.name == "soft-outdoor-v0-no-aa" &&
                fxaa.name == "soft-outdoor-v0-fxaa" &&
                taa.name == "soft-outdoor-v0-taa" &&
                taaFxaa.name == "soft-outdoor-v0-taa-fxaa",
            "Each comparison profile must carry explicit selected-mode provenance");
    require(noAa.lighting == base.lighting && noAa.water == base.water &&
                noAa.glass == base.glass,
            "Soft Profile v0 must preserve scene-specific lighting, water, and glass");
    require(noAa.shadows.useDdaShadows &&
                nearlyEqual(noAa.shadows.requestedSunAngularRadius, 0.060f) &&
                noAa.shadows.ddaSunSampleCount == 4 &&
                nearlyEqual(engine::scene::effectiveDdaSunAngularRadius(
                                noAa.shadows.requestedSunAngularRadius,
                                noAa.shadows.ddaSunSampleCount),
                            0.052f),
            "Soft Profile v0 must encode and expose its requested/effective sun radius");
    require(nearlyEqual(noAa.ambientOcclusion.maxDistance, 5.5f) &&
                nearlyEqual(noAa.ambientOcclusion.contribution, 0.18f) &&
                noAa.postFx.colorGradeEnabled &&
                !noAa.postFx.pixelizationEnabled &&
                !noAa.postFx.depthOfFieldEnabled &&
                nearlyEqual(noAa.voxelSurface.materialDetailStrength, 0.25f),
            "Soft Profile v0 must explicitly own its softness and detail controls");

    require(!noAa.postFx.taaEnabled && !noAa.postFx.jitterEnabled &&
                !noAa.postFx.fxaaEnabled &&
                !fxaa.postFx.taaEnabled && !fxaa.postFx.jitterEnabled &&
                fxaa.postFx.fxaaEnabled &&
                taa.postFx.taaEnabled && taa.postFx.jitterEnabled &&
                !taa.postFx.fxaaEnabled &&
                taaFxaa.postFx.taaEnabled && taaFxaa.postFx.jitterEnabled &&
                taaFxaa.postFx.fxaaEnabled,
            "The matrix must encode no-AA, FXAA, TAA, and combined modes exactly");

    require(engine::scene::describeScenePresentationProfileDelta(noAa, fxaa) ==
                std::vector<std::string>{"postFx.fxaaEnabled"} &&
                engine::scene::describeScenePresentationProfileDelta(taa, taaFxaa) ==
                    std::vector<std::string>{"postFx.fxaaEnabled"},
            "Paired comparison profiles must differ only by the intended FXAA switch");
}

void testSoftOutdoorProfileV1OwnsAcceptedRendererFields()
{
    const SceneConfig pond =
        makeNamedSceneConfig("nature_pond_probe", false, true);
    const ScenePresentationProfile base =
        engine::scene::namedBaseScenePresentationProfile(pond);
    const ScenePresentationProfile profile =
        engine::scene::makeSoftOutdoorProfileV1(base);
    const engine::VoxelCellVariationSettings expectedVariation =
        engine::makeVoxelCellVariationSettings(
            engine::VoxelCellVariationPreset::SoftOutdoorV0);
    const engine::render::HemisphereAmbientSettings expectedHemisphere =
        engine::render::makeHemisphereAmbientSettings(
            engine::render::HemisphereAmbientPreset::WarmCoolOutdoorV0);
    const engine::render::SceneAtmosphereSettings expectedAtmosphere =
        engine::render::makeSceneAtmosphereSettings(
            engine::render::SceneAtmospherePreset::OutdoorHazeV0);

    require(profile.version == ScenePresentationProfile::kCurrentVersion &&
                profile.name == "soft-outdoor-v1" && profile.postFx.taaEnabled &&
                profile.postFx.jitterEnabled && !profile.postFx.fxaaEnabled,
            "Soft Profile v1 must carry current-version and accepted TAA-only provenance");
    require(nearlyEqual(profile.voxelCellVariation.masterStrength,
                        expectedVariation.masterStrength) &&
                nearlyEqual(profile.voxelCellVariation.genericAmplitude,
                            expectedVariation.genericAmplitude) &&
                nearlyEqual(profile.voxelCellVariation.gravelAmplitude,
                            expectedVariation.gravelAmplitude) &&
                nearlyEqual(profile.voxelCellVariation.plantAmplitude,
                            expectedVariation.plantAmplitude) &&
                nearlyEqual(profile.voxelCellVariation.stoneAmplitude,
                            expectedVariation.stoneAmplitude) &&
                nearlyEqual(profile.voxelCellVariation.woodAmplitude,
                            expectedVariation.woodAmplitude),
            "Soft Profile v1 must persist the accepted voxel-cell variation values");
    require(nearlyEqual(profile.hemisphereAmbient.strength,
                        expectedHemisphere.strength) &&
                profile.hemisphereAmbient.skyTint == expectedHemisphere.skyTint &&
                profile.hemisphereAmbient.groundTint == expectedHemisphere.groundTint,
            "Soft Profile v1 must persist the accepted hemisphere ambient values");
    require(nearlyEqual(profile.atmosphere.density, expectedAtmosphere.density) &&
                nearlyEqual(profile.atmosphere.heightFalloff,
                            expectedAtmosphere.heightFalloff) &&
                nearlyEqual(profile.atmosphere.baseHeight,
                            expectedAtmosphere.baseHeight) &&
                nearlyEqual(profile.atmosphere.sunPhaseStrength,
                            expectedAtmosphere.sunPhaseStrength) &&
                nearlyEqual(profile.atmosphere.sunPhaseExponent,
                            expectedAtmosphere.sunPhaseExponent),
            "Soft Profile v1 must persist the accepted outdoor atmosphere values");
}

void testSoftOutdoorProfileV2OwnsSoftEdgePresentation()
{
    const SceneConfig pond =
        makeNamedSceneConfig("nature_pond_probe", false, true);
    const ScenePresentationProfile base =
        engine::scene::namedBaseScenePresentationProfile(pond);
    const ScenePresentationProfile profile =
        engine::scene::makeSoftOutdoorProfileV2(base);

    require(profile.version == ScenePresentationProfile::kCurrentVersion &&
                profile.name == "soft-outdoor-v2" && profile.postFx.taaEnabled &&
                profile.postFx.jitterEnabled && !profile.postFx.fxaaEnabled,
            "Soft Profile v2 must retain the accepted TAA-only reconstruction base");
    require(nearlyEqual(profile.postFx.taaSoftEdgeStrength, 0.34f) &&
                nearlyEqual(profile.postFx.taaSharpen, 0.05f) &&
                nearlyEqual(profile.voxelSurface.materialDetailStrength, 0.14f) &&
                nearlyEqual(profile.voxelSurface.normalEdgeSmoothing, 0.22f) &&
                nearlyEqual(profile.voxelSurface.pixelEdgeShadowStrength, 0.10f) &&
                nearlyEqual(profile.shadows.terminatorSoftness, 0.26f),
            "Soft Profile v2 must own its bounded full-scene softness controls");
}

void testPaintedOutdoorProfileV3OwnsPaintedVoxelPresentation()
{
    const SceneConfig pond = makeNamedSceneConfig("nature_pond_probe", false, true);
    const ScenePresentationProfile base =
        engine::scene::namedBaseScenePresentationProfile(pond);
    const ScenePresentationProfile profile =
        engine::scene::makePaintedOutdoorProfileV3(base);
    const engine::VoxelCellVariationSettings expected =
        engine::makeVoxelCellVariationSettings(
            engine::VoxelCellVariationPreset::PaintedOutdoorV0);

    require(profile.version == ScenePresentationProfile::kCurrentVersion &&
                profile.name == "painted-outdoor-v3" &&
                nearlyEqual(profile.voxelSurface.cavityStrength, 0.32f) &&
                nearlyEqual(profile.voxelSurface.paintedMaterialStrength, 0.82f),
            "Painted Profile v3 must own its matte and softened-cavity response");
    require(nearlyEqual(profile.voxelCellVariation.hueSpread,
                        expected.hueSpread) &&
                nearlyEqual(profile.voxelCellVariation.saturationSpread,
                            expected.saturationSpread) &&
                nearlyEqual(profile.voxelCellVariation.valueSpread,
                            expected.valueSpread) &&
                nearlyEqual(profile.voxelCellVariation.paletteFamilyStrength,
                            expected.paletteFamilyStrength),
            "Painted Profile v3 must persist its amplified palette-family controls");
    require(profile.postFx.colorGradeContrast < 1.0f &&
                profile.postFx.colorGradeTemperature > 0.0f &&
                profile.ambientOcclusion.contribution < 0.18f,
            "Painted Profile v3 must use warm low-contrast grading and restrained AO");
}

void testPaintedOutdoorSkyProfileV1OwnsBoundedSkyPresentation()
{
    ScenePresentationProfile dayBase{};
    dayBase.lighting.skyPreset = 0;
    const ScenePresentationProfile day =
        engine::scene::makePaintedOutdoorSkyProfileV1(dayBase);
    require(day.version == ScenePresentationProfile::kCurrentVersion &&
                day.name == "painted-outdoor-sky-v1" &&
                day.paintedSky.strength == 1.0f &&
                day.paintedSky.horizonTint != glm::vec3(1.0f) &&
                day.paintedSky.zenithTint != glm::vec3(1.0f) &&
                day.paintedSky.sunDiscIntensity > 0.0f &&
                day.paintedSky.sunHaloIntensity > 0.0f,
            "Painted Sky v1 must own its gradient, horizon, disc, and halo");
    require(nearlyEqual(day.voxelSurface.paintedMaterialStrength, 0.82f) &&
                day.postFx.taaEnabled && day.atmosphere.density > 0.0f,
            "Painted Sky v1 must retain the accepted painted-outdoor presentation");

    ScenePresentationProfile sunsetBase{};
    sunsetBase.lighting.skyPreset = 1;
    const ScenePresentationProfile sunset =
        engine::scene::makePaintedOutdoorSkyProfileV1(sunsetBase);
    require(sunset.paintedSky.horizonTint != day.paintedSky.horizonTint &&
                sunset.paintedSky.sunHaloIntensity > day.paintedSky.sunHaloIntensity,
            "Painted Sky v1 must select a warmer authored sunset response");

    const engine::render::PaintedSkySettings off{};
    const glm::vec3 authored{0.42f, 0.67f, 0.92f};
    require(engine::render::evaluatePaintedSky(
                off, authored, glm::vec3(0.0f, 1.0f, 0.0f),
                glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(1.0f)) == authored,
            "Painted sky strength zero must preserve the authored sky exactly");

    const engine::render::PaintedSkySettings settings =
        engine::render::makePaintedSkySettings(
            engine::render::PaintedSkyPreset::SoftDayV0);
    const glm::vec3 towardSun = engine::render::evaluatePaintedSky(
        settings, authored, glm::vec3(0.0f, 1.0f, 0.0f),
        glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(1.0f));
    const glm::vec3 awaySun = engine::render::evaluatePaintedSky(
        settings, authored, glm::vec3(0.0f, 1.0f, 0.0f),
        glm::vec3(0.0f, -1.0f, 0.0f), glm::vec3(1.0f));
    require(towardSun.x > awaySun.x && towardSun.y > awaySun.y &&
                towardSun.z > awaySun.z,
            "The painted sun disc must add radiance only around the authored sun");

    const engine::render::PaintedSkyGpuData packed =
        engine::render::packPaintedSkyGpuData(settings);
    require(packed.sky0.w == 1.0f &&
                nearlyEqual(packed.sky3.z, settings.sunDiscIntensity) &&
                nearlyEqual(packed.sky4.x, settings.sunHaloExponent),
            "LightingPass must receive one stable painted-sky UBO packing contract");
}

void testPaintedOutdoorCloudProfileV1OwnsBoundedWindDrivenLayer()
{
    const ScenePresentationProfile clouds =
        engine::scene::makePaintedOutdoorCloudProfileV1({});
    require(clouds.version == ScenePresentationProfile::kCurrentVersion &&
                clouds.name == "painted-outdoor-clouds-v1" &&
                clouds.paintedSky.strength > 0.0f &&
                clouds.paintedClouds.strength == 1.0f &&
                clouds.paintedClouds.coverage > 0.0f &&
                clouds.paintedClouds.opacity > 0.0f,
            "Painted Clouds v1 must layer a bounded cloud sheet over SKY-001");

    const glm::vec3 authoredSky{0.42f, 0.67f, 0.92f};
    const engine::render::PaintedCloudEvaluation off =
        engine::render::evaluatePaintedClouds(
            {}, {}, authoredSky, glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f),
            glm::vec3(0.0f, 1.0f, 0.0f));
    require(off.color == authoredSky && off.opacity == 0.0f,
            "Painted-cloud strength zero must preserve the SKY-001 color exactly");

    engine::render::PaintedCloudSettings settings =
        engine::render::makePaintedCloudSettings(
            engine::render::PaintedCloudPreset::SoftDayV0);
    engine::render::PaintedCloudFrameInputs frame{};
    frame.windDirection = {-0.6f, 0.8f};
    frame.windSpeed = 2.0f;
    frame.windStrength = 0.5f;
    frame.animationTime = 7.0f;
    frame.worldSeed = 2701u;
    const engine::render::PaintedCloudGpuData packed =
        engine::render::packPaintedCloudGpuData(settings, frame);
    require(nearlyEqual(packed.cloud4.x, -0.6f) &&
                nearlyEqual(packed.cloud4.y, 0.8f) &&
                nearlyEqual(packed.cloud4.z, 7.0f) &&
                nearlyEqual(packed.cloud4.w, 2701.0f),
            "Cloud UBO drift must be derived exclusively from normalized WIND-001 inputs");

    settings.coverage = 1.0f;
    settings.opacity = 1.0f;
    const engine::render::PaintedCloudEvaluation active =
        engine::render::evaluatePaintedClouds(
            settings, frame, authoredSky, glm::vec3(0.0f, 2.0f, 0.0f),
            glm::normalize(glm::vec3(0.2f, 1.0f, 0.1f)),
            glm::normalize(glm::vec3(0.3f, 0.8f, 0.2f)));
    require(active.opacity > 0.0f && active.color != authoredSky,
            "The fixed three-octave candidate must produce a visible bounded layer");

    engine::render::PaintedCloudFrameInputs stillFrame = frame;
    stillFrame.windStrength = 0.0f;
    stillFrame.animationTime = 0.0f;
    settings.worldScale = 96.0f;
    const engine::render::PaintedCloudEvaluation facetA =
        engine::render::evaluatePaintedClouds(
            settings, stillFrame, authoredSky, glm::vec3(0.0f, 2.0f, 0.0f),
            glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    const engine::render::PaintedCloudEvaluation facetB =
        engine::render::evaluatePaintedClouds(
            settings, stillFrame, authoredSky, glm::vec3(0.5f, 2.0f, 0.5f),
            glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    require(facetA.color == facetB.color && facetA.opacity == facetB.opacity,
            "Sub-cell camera motion must preserve the world-anchored cloud facet");
}

void testCaptureApplyRoundTripPreservesPersistentLook()
{
    engine::render::LightingSettings sourceLighting{};
    sourceLighting.pointLightsEnabled_ = false;
    sourceLighting.lightCount_ = 7;
    sourceLighting.sunElevation_ = 31.0f;
    sourceLighting.sunAzimuth_ = 117.0f;
    sourceLighting.sunColor_ = glm::vec3(0.9f, 0.8f, 0.7f);
    sourceLighting.sunIntensity_ = 3.25f;
    sourceLighting.hemisphereAmbient_ =
        engine::render::makeHemisphereAmbientSettings(
            engine::render::HemisphereAmbientPreset::WarmCoolOutdoorV0);
    sourceLighting.sceneAtmosphere_ =
        engine::render::makeSceneAtmosphereSettings(
            engine::render::SceneAtmospherePreset::OutdoorHazeV0);
    sourceLighting.paintedSky_ = engine::render::makePaintedSkySettings(
        engine::render::PaintedSkyPreset::WarmSunsetV0);
    sourceLighting.paintedClouds_ = engine::render::makePaintedCloudSettings(
        engine::render::PaintedCloudPreset::OvercastV0);

    engine::render::ShadowSettings sourceShadows{};
    sourceShadows.csmEnabled_ = false;
    sourceShadows.useDDAShadows_ = true;
    sourceShadows.sunAngularRadius_ = 0.06f;
    sourceShadows.maxShadowDist_ = 73.0f;
    sourceShadows.shadowNormalBias_ = 0.04f;
    sourceShadows.maxShadowSteps_ = 96;
    sourceShadows.shadowDdaSunSampleCount_ = 3;
    sourceShadows.foliageShadowOpacity_ = 0.8f;
    sourceShadows.shadowBlendAlpha_ = 0.07f;
    sourceShadows.shadowDepthReject_ = 0.002f;
    sourceShadows.shadowNormalRejectDot_ = 0.93f;
    sourceShadows.shadowClampSharpness_ = 0.7f;
    sourceShadows.shadowSpatialFilterRadius_ = 2;
    sourceShadows.shadowSpatialDepthSigma_ = 0.005f;
    sourceShadows.shadowSpatialValueSigma_ = 0.3f;
    sourceShadows.shadowSpatialNormalPower_ = 32.0f;
    sourceShadows.shadowPostDenoiseRadius_ = 2;
    sourceShadows.shadowPostDenoiseDepthSigma_ = 0.006f;
    sourceShadows.shadowPostDenoiseValueSigma_ = 0.22f;
    sourceShadows.shadowPostDenoiseNormalPower_ = 40.0f;
    sourceShadows.localLightShadowsEnabled_ = false;
    sourceShadows.localShadowCastingLightCount_ = 2;
    sourceShadows.localShadowBlendAlpha_ = 0.09f;
    sourceShadows.localShadowDepthReject_ = 0.015f;
    sourceShadows.localShadowBlurEnabled_ = false;
    sourceShadows.localShadowBlurSigma_ = 0.9f;
    sourceShadows.terminatorSoftness_ = 0.2f;
    sourceShadows.terminatorMode_ = 2;
    sourceShadows.csmDitherEnabled_ = false;

    engine::render::AmbientOcclusionSettings sourceAo{};
    sourceAo.aoEnabled_ = false;
    sourceAo.aoDistanceMode_ =
        engine::render::AmbientOcclusionDistanceMode::ProjectedScreenRadius;
    sourceAo.aoProjectedRadiusPixels_ = 448.0f;
    sourceAo.aoProjectedMinDistance_ = 1.5f;
    sourceAo.aoMaxDistance_ = 6.0f;
    sourceAo.aoStepSize_ = 0.2f;
    sourceAo.aoIntensity_ = 1.2f;
    sourceAo.aoContribution_ = 0.18f;
    sourceAo.aoBias_ = 0.03f;
    sourceAo.aoRayCount_ = 4;
    sourceAo.aoBlendAlpha_ = 0.1f;
    sourceAo.aoDepthReject_ = 0.025f;
    sourceAo.aoNormalRejectDot_ = 0.8f;

    engine::render::PostFxSettings sourcePostFx{};
    sourcePostFx.tonemapEnabled_ = false;
    sourcePostFx.exposure_ = 1.3f;
    sourcePostFx.highlightRecovery_ = 0.2f;
    sourcePostFx.bloomEnabled_ = false;
    sourcePostFx.bloomThreshold_ = 0.9f;
    sourcePostFx.bloomKnee_ = 0.4f;
    sourcePostFx.bloomIntensity_ = 0.12f;
    sourcePostFx.bloomSigma_ = 3.0f;
    sourcePostFx.vignetteStrength_ = 0.08f;
    sourcePostFx.grainStrength_ = 0.02f;
    sourcePostFx.colorGradeEnabled_ = true;
    sourcePostFx.colorGradeStrength_ = 0.3f;
    sourcePostFx.colorGradeSaturation_ = 1.15f;
    sourcePostFx.colorGradeContrast_ = 1.04f;
    sourcePostFx.colorGradeTemperature_ = -0.03f;
    sourcePostFx.postPixelizationEnabled_ = true;
    sourcePostFx.postPixelizationBlockSize_ = 3.0f;
    sourcePostFx.postPixelizationStrength_ = 0.65f;
    sourcePostFx.postPixelizationEdgeFocus_ = 0.5f;
    sourcePostFx.postMaterialDetailStrength_ = 0.45f;
    sourcePostFx.dofEnabled_ = true;
    sourcePostFx.dofFocusDistance_ = 8.0f;
    sourcePostFx.dofFocusRange_ = 4.0f;
    sourcePostFx.dofBlurStrength_ = 0.6f;
    sourcePostFx.taaEnabled_ = true;
    sourcePostFx.jitterEnabled_ = true;
    sourcePostFx.taaSimilarityThreshold_ = 0.12f;
    sourcePostFx.taaVelocityScale_ = 8.0f;
    sourcePostFx.taaBlendMin_ = 0.04f;
    sourcePostFx.taaBlendMax_ = 0.16f;
    sourcePostFx.taaSharpen_ = 0.3f;
    sourcePostFx.taaDepthEdgeThreshold_ = 0.08f;
    sourcePostFx.taaCrossFrameDepthThreshold_ = 0.018f;
    sourcePostFx.taaColorVarianceThreshold_ = 0.13f;
    sourcePostFx.taaSoftEdgeStrength_ = 0.41f;
    sourcePostFx.fxaaEnabled_ = false;

    engine::render::VoxelDebugSettings sourceVoxel{};
    sourceVoxel.voxelNormalEdgeSmoothing_ = 0.18f;
    sourceVoxel.voxelPixelEdgeShadowStrength_ = 0.42f;
    sourceVoxel.voxelCellVariation_ =
        engine::makeVoxelCellVariationSettings(
            engine::VoxelCellVariationPreset::SoftOutdoorV0);

    engine::render::WaterSettings sourceWater{};
    sourceWater.waterStylizedMode_ = false;
    sourceWater.waterAbsorption_ = 0.07f;
    sourceWater.waterRefract_ = 0.021f;
    sourceWater.waterDepthScale_ = 5.5f;
    sourceWater.waterShallowBias_ = 0.4f;
    sourceWater.waterDepthToMeters_ = 9.5f;
    sourceWater.waterSpecIntensity_ = 1.9f;
    sourceWater.waterSpecPower_ = 170.0f;
    sourceWater.waterWaveScale_ = 0.05f;
    sourceWater.waterWaveAmp_ = 0.3f;
    sourceWater.waterCrestHighlightsEnabled_ = false;
    sourceWater.waterCrestThreshold_ = 0.5f;
    sourceWater.waterCrestSoftness_ = 0.1f;
    sourceWater.waterCrestIntensity_ = 0.5f;
    sourceWater.waterFoamDepthThreshold_ = 0.7f;
    sourceWater.waterFoamOpacity_ = 0.5f;
    sourceWater.waterFoamScale_ = 0.3f;
    sourceWater.waterDistortionDepthScale_ = 0.22f;
    sourceWater.waterEdgeFadeDepth_ = 0.6f;
    sourceWater.waterBandHardness_ = 0.7f;
    sourceWater.waterReflectionStrength_ = 0.2f;
    sourceWater.waterFresnelBias_ = 0.02f;
    sourceWater.waterCausticsEnabled_ = true;
    sourceWater.waterCausticsIntensity_ = 0.8f;
    sourceWater.waterCausticsScale_ = 0.2f;
    sourceWater.waterCausticsSpeed_ = 0.4f;
    sourceWater.waterCausticsBanding_ = 0.5f;
    sourceWater.waterCausticsDepthFade_ = 25.0f;
    sourceWater.waterParticlesPlanned_ = false;
    sourceWater.waterParticlesPlannedDensity_ = 0.2f;
    sourceWater.waterParticlesPlannedDrift_ = 0.3f;
    sourceWater.waterParticlesPlannedScale_ = 0.1f;
    sourceWater.waterFoamEmitterEnabled_ = true;
    sourceWater.waterFoamEmitterIntensity_ = 0.5f;
    sourceWater.waterFoamEmitterRadius_ = 4.5f;
    sourceWater.waterFoamEmitterOffsetX_ = 1.5f;
    sourceWater.waterFoamEmitterOffsetZ_ = -2.5f;
    sourceWater.waterFoamEmitterScale_ = 0.3f;
    sourceWater.waterFoamEmitterSpread_ = 0.4f;
    sourceWater.waterGradientStrength_ = 0.1f;
    sourceWater.waterPlanarReflectionEnabled_ = true;
    sourceWater.waterPlanarObliqueClipEnabled_ = true;
    sourceWater.waterPlanarStrength_ = 0.5f;

    engine::render::GlassSettings sourceGlass{};
    sourceGlass.glassTint_ = glm::vec3(0.5f, 0.6f, 0.7f);
    sourceGlass.glassReflection_ = glm::vec3(0.1f, 0.2f, 0.3f);
    sourceGlass.glassAbsorption_ = 0.3f;
    sourceGlass.glassThicknessScale_ = 0.8f;
    sourceGlass.glassRefract_ = 0.02f;
    sourceGlass.glassIor_ = 1.3f;
    sourceGlass.glassBubbleScale_ = 15.0f;
    sourceGlass.glassBubbleIntensity_ = 0.2f;
    sourceGlass.glassBubbleThicknessGate_ = 0.25f;
    sourceGlass.glassBubbleChromaticSplit_ = 0.05f;
    sourceGlass.glassIridescentStrength_ = 0.4f;
    sourceGlass.glassIridescentFilmThickness_ = 2.5f;
    sourceGlass.glassIridescentFrequency_ = 3.0f;
    sourceGlass.voxelGlassRefractEnabled_ = false;
    sourceGlass.voxelGlassAbsorption_ = 0.2f;
    sourceGlass.voxelGlassRefractStrength_ = 0.03f;
    sourceGlass.voxelGlassIOR_ = 1.4f;
    sourceGlass.voxelGlassReflectStrength_ = 0.3f;
    sourceGlass.voxelGlassTint_ = glm::vec3(0.9f, 0.8f, 0.7f);
    sourceGlass.voxelGlassReflectionColor_ = glm::vec3(0.4f, 0.5f, 0.6f);

    const ScenePresentationProfile source =
        engine::scene::captureScenePresentationProfile(
            "roundtrip", 4, glm::vec3(0.2f, 0.3f, 0.4f), sourceLighting,
            sourceShadows, sourceAo, sourcePostFx, sourceVoxel, sourceWater,
            sourceGlass);

    int targetSkyPreset = 0;
    glm::vec3 targetSkyColor{};
    engine::render::LightingSettings targetLighting{};
    engine::render::ShadowSettings targetShadows{};
    engine::render::AmbientOcclusionSettings targetAo{};
    engine::render::PostFxSettings targetPostFx{};
    engine::render::VoxelDebugSettings targetVoxel{};
    engine::render::WaterSettings targetWater{};
    engine::render::GlassSettings targetGlass{};

    targetLighting.lightingDebugMode_ = 4;
    targetShadows.shadowDebugMode_ = 3;
    targetShadows.shadowResetHistory_ = false;
    targetShadows.ddaRayResolutionScale_ = 0.5f;
    targetAo.aoResetHistory_ = false;
    targetAo.aoRayResolutionScale_ = 0.5f;
    targetPostFx.postDebugMode_ = 2;
    targetPostFx.taaDebugMode_ = 1;
    targetPostFx.taaResetHistory_ = false;
    targetPostFx.renderResolution_.scale = 0.6f;
    targetVoxel.voxelDdaDebugMode_ = 5;
    targetWater.waterEnabled_ = false;
    targetWater.useWaterV2_ = true;
    targetWater.waterLevel_ = 42.0f;
    targetWater.waterDebugDepthScale_ = 99.0f;
    targetWater.waterDebugMode_ = 3;
    targetGlass.glassEnabled_ = true;
    targetGlass.glassThicknessDebugScale_ = 7.0f;
    targetGlass.glassRefractDebugScale_ = 8.0f;
    targetGlass.glassDebugMode_ = 4;
    targetGlass.voxelGlassDebugMode_ = 6;

    engine::scene::applyScenePresentationProfile(
        source, targetSkyPreset, targetSkyColor, targetLighting, targetShadows,
        targetAo, targetPostFx, targetVoxel, targetWater, targetGlass);

    const ScenePresentationProfile captured =
        engine::scene::captureScenePresentationProfile(
            "roundtrip", targetSkyPreset, targetSkyColor, targetLighting,
            targetShadows, targetAo, targetPostFx, targetVoxel, targetWater,
            targetGlass);
    require(captured == source,
            "Applying then capturing a profile must preserve every persistent look value");
    require(targetLighting.lightingDebugMode_ == 4 &&
                targetShadows.shadowDebugMode_ == 3 &&
                !targetShadows.shadowResetHistory_ && !targetAo.aoResetHistory_ &&
                targetPostFx.postDebugMode_ == 2 &&
                targetPostFx.taaDebugMode_ == 1 &&
                !targetPostFx.taaResetHistory_ &&
                targetVoxel.voxelDdaDebugMode_ == 5,
            "Profile application must not overwrite diagnostics or history-reset requests");
    require(nearlyEqual(targetShadows.ddaRayResolutionScale_, 0.5f) &&
                nearlyEqual(targetAo.aoRayResolutionScale_, 0.5f) &&
                nearlyEqual(targetPostFx.renderResolution_.scale, 0.6f),
            "Profile application must preserve transient render-quality policy");
    require(!targetWater.waterEnabled_ && targetWater.useWaterV2_ &&
                nearlyEqual(targetWater.waterLevel_, 42.0f) &&
                nearlyEqual(targetWater.waterDebugDepthScale_, 99.0f) &&
                targetWater.waterDebugMode_ == 3 && targetGlass.glassEnabled_ &&
                nearlyEqual(targetGlass.glassThicknessDebugScale_, 7.0f) &&
                nearlyEqual(targetGlass.glassRefractDebugScale_, 8.0f) &&
                targetGlass.glassDebugMode_ == 4 &&
                targetGlass.voxelGlassDebugMode_ == 6,
            "Profile application must not overwrite scene-content or debug water/glass state");
}

void testEffectiveDdaSunRadiusMatchesRuntimeCap()
{
    require(nearlyEqual(engine::scene::maxEffectiveDdaSunAngularRadius(0), 0.022f),
            "DDA sun radius cap must clamp the sample count to at least one");
    require(nearlyEqual(engine::scene::maxEffectiveDdaSunAngularRadius(2), 0.032f),
            "Two DDA samples must cap the effective radius at 0.032");
    require(nearlyEqual(engine::scene::maxEffectiveDdaSunAngularRadius(8), 0.08f),
            "DDA sun radius cap must not exceed 0.08");
    require(nearlyEqual(engine::scene::effectiveDdaSunAngularRadius(0.06f, 2),
                        0.032f) &&
                nearlyEqual(engine::scene::effectiveDdaSunAngularRadius(0.02f, 2),
                            0.02f),
            "Effective DDA radius must be the lower of requested radius and sample cap");
}

bool deltaContains(const std::vector<std::string>& delta, const std::string& field)
{
    return std::find(delta.begin(), delta.end(), field) != delta.end();
}

SceneConfig makeNamedSceneConfig(const std::string& name, bool aquarium, bool procedural)
{
    SceneConfig config{};
    config.name = name;
    config.loadAquariumTest = aquarium;
    config.loadProceduralWorld = procedural;
    return config;
}

void testNamedVariantsEncodeDistinctBaselineLooks()
{
    const ScenePresentationProfile sunroof =
        engine::scene::namedBaseScenePresentationProfile(
            makeNamedSceneConfig("aquarium_sunroof", true, false));
    require(sunroof.name == "painted-outdoor-v3",
            "Sunroof scene must adopt Painted Profile v3");
    require(sunroof.lighting.skyPreset == 4 &&
                nearlyEqual(sunroof.lighting.sunElevation, 68.0f) &&
                nearlyEqual(sunroof.lighting.sunAzimuth, 148.0f) &&
                nearlyEqual(sunroof.lighting.sunIntensity, 4.5f),
            "Sunroof variant must encode the authored sunroof sun");
    require(sunroof.shadows.useDdaShadows &&
                nearlyEqual(sunroof.shadows.requestedSunAngularRadius, 0.060f) &&
                sunroof.shadows.ddaSunSampleCount == 4 &&
                nearlyEqual(sunroof.shadows.temporalClampSharpness, 0.75f) &&
                nearlyEqual(sunroof.shadows.spatialNormalPower, 64.0f),
            "Sunroof variant must preserve its sun and adopt painted soft shadows");
    require(nearlyEqual(sunroof.postFx.highlightRecovery, 0.30f) &&
                sunroof.postFx.colorGradeEnabled &&
                sunroof.postFx.taaEnabled && sunroof.postFx.jitterEnabled &&
                !sunroof.postFx.fxaaEnabled &&
                nearlyEqual(sunroof.postFx.colorGradeStrength, 0.34f) &&
                nearlyEqual(sunroof.postFx.colorGradeSaturation, 1.02f) &&
                nearlyEqual(sunroof.postFx.colorGradeContrast, 0.90f) &&
                nearlyEqual(sunroof.postFx.colorGradeTemperature, 0.08f) &&
                sunroof.postFx.depthOfFieldEnabled &&
                nearlyEqual(sunroof.postFx.depthOfFieldFocusDistance, 25.0f) &&
                nearlyEqual(sunroof.postFx.depthOfFieldFocusRange, 14.0f) &&
                nearlyEqual(sunroof.postFx.depthOfFieldBlurStrength, 0.24f) &&
                nearlyEqual(sunroof.voxelSurface.materialDetailStrength, 0.14f) &&
                nearlyEqual(sunroof.voxelSurface.paintedMaterialStrength, 0.82f) &&
                nearlyEqual(sunroof.voxelCellVariation.genericAmplitude, 0.0f) &&
                sunroof.voxelCellVariation.plantAmplitude > 0.0f,
            "Sunroof variant must adopt the painted voxel and grade response while keeping the white shell uniform");
    require(!sunroof.water.stylizedMode && nearlyEqual(sunroof.water.refract, 0.018f) &&
                sunroof.water.causticsEnabled &&
                nearlyEqual(sunroof.water.causticsIntensity, 0.30f) &&
                nearlyEqual(sunroof.water.causticsDepthFade, 24.0f) &&
                nearlyEqual(sunroof.water.gradientStrength, 0.065f) &&
                !sunroof.water.particlesPlanned && !sunroof.water.foamEmitterEnabled,
            "Sunroof variant must encode the sunroof water look");
    require(nearlyEqual(sunroof.glass.absorption, 0.08f) &&
                nearlyEqual(sunroof.glass.ior, 1.03f) &&
                !sunroof.glass.voxelGlassRefractEnabled,
            "Sunroof variant must encode the sunroof glass and voxel-glass enforcement");

    const ScenePresentationProfile sunroofProbe =
        engine::scene::namedBaseScenePresentationProfile(
            makeNamedSceneConfig("aquarium_sunroof_probe", true, false));
    require(sunroofProbe == sunroof,
            "The isolated sunroof probe must retain the authored sunroof presentation profile");

    const ScenePresentationProfile fishbowl =
        engine::scene::namedBaseScenePresentationProfile(
            makeNamedSceneConfig("fishbowl_perf_probe", true, false));
    require(fishbowl.name == "painted-outdoor-v3",
            "Fishbowl scene must adopt Painted Profile v3");
    require(nearlyEqual(fishbowl.lighting.sunElevation, 48.0f) &&
                nearlyEqual(fishbowl.lighting.sunAzimuth, 132.0f) &&
                nearlyEqual(fishbowl.lighting.sunIntensity, 3.0f) &&
                fishbowl.shadows.useDdaShadows &&
                nearlyEqual(fishbowl.shadows.requestedSunAngularRadius, 0.060f) &&
                fishbowl.shadows.ddaSunSampleCount == 4 &&
                nearlyEqual(fishbowl.shadows.temporalDepthReject, 0.0025f) &&
                nearlyEqual(fishbowl.shadows.temporalNormalRejectDot, 0.94f) &&
                nearlyEqual(fishbowl.shadows.postDenoiseDepthSigma, 0.0050f),
            "Fishbowl variant must preserve its authored sun and adopt painted soft shadows");
    require(nearlyEqual(fishbowl.postFx.highlightRecovery, 0.30f) &&
                fishbowl.postFx.colorGradeEnabled &&
                fishbowl.postFx.taaEnabled && fishbowl.postFx.jitterEnabled &&
                !fishbowl.postFx.fxaaEnabled &&
                nearlyEqual(fishbowl.postFx.colorGradeStrength, 0.34f) &&
                nearlyEqual(fishbowl.postFx.colorGradeSaturation, 1.02f) &&
                nearlyEqual(fishbowl.postFx.colorGradeContrast, 0.90f) &&
                nearlyEqual(fishbowl.postFx.colorGradeTemperature, 0.08f) &&
                nearlyEqual(fishbowl.voxelSurface.materialDetailStrength, 0.14f) &&
                nearlyEqual(fishbowl.voxelSurface.paintedMaterialStrength, 0.82f),
            "Fishbowl variant must adopt the complete painted voxel and grade response");
    require(!fishbowl.water.stylizedMode &&
                nearlyEqual(fishbowl.water.specIntensity, 1.9f) &&
                nearlyEqual(fishbowl.water.specPower, 180.0f) &&
                !fishbowl.water.particlesPlanned && !fishbowl.water.foamEmitterEnabled,
            "Fishbowl variant must encode the fishbowl water look");
    require(nearlyEqual(fishbowl.glass.absorption, 0.810f) &&
                nearlyEqual(fishbowl.glass.thicknessScale, 0.100f) &&
                nearlyEqual(fishbowl.glass.iridescentStrength, 0.62f) &&
                nearlyEqual(fishbowl.glass.bubbleIntensity, 0.05f) &&
                !fishbowl.glass.voxelGlassRefractEnabled,
            "Fishbowl variant must encode the round-fishbowl glass preset");
    require(fishbowl.voxelCellVariation.masterStrength > 0.0f &&
                fishbowl.voxelCellVariation.paletteFamilyStrength > 0.0f &&
                fishbowl.hemisphereAmbient.strength > 0.0f &&
                fishbowl.atmosphere.density > 0.0f,
            "Fishbowl must adopt painted palette families, ambient fill, and atmosphere");

    SceneConfig directConfig = makeNamedSceneConfig("aquarium_test", true, false);
    directConfig.skyPreset = 0;
    directConfig.skyColor = glm::vec3(0.09f, 0.29f, 0.88f);
    const ScenePresentationProfile direct =
        engine::scene::namedBaseScenePresentationProfile(directConfig);
    require(direct.name == "aquarium-direct", "Direct aquarium scene must map to its variant");
    require(!direct.shadows.useDdaShadows,
            "Direct aquarium variant must keep DDA sun shadows disabled");
    require(nearlyEqual(direct.lighting.sunElevation, 55.0f) &&
                nearlyEqual(direct.lighting.sunAzimuth, 135.0f) &&
                nearlyEqual(direct.lighting.sunIntensity, 2.5f),
            "Direct aquarium variant must keep the day-preset sun");
    require(nearlyEqual(direct.postFx.highlightRecovery, 0.25f) &&
                nearlyEqual(direct.postFx.colorGradeStrength, 0.35f) &&
                nearlyEqual(direct.postFx.colorGradeContrast, 1.05f) &&
                nearlyEqual(direct.postFx.colorGradeTemperature, -0.04f) &&
                nearlyEqual(direct.voxelSurface.materialDetailStrength, 0.55f),
            "Direct aquarium variant must encode the direct-view grade");
    require(!direct.water.particlesPlanned && direct.water.foamEmitterEnabled &&
                nearlyEqual(direct.water.foamEmitterIntensity, 0.62f) &&
                !direct.glass.voxelGlassRefractEnabled,
            "aquarium_test must keep its foam emitter and voxel-glass enforcement");
    const ScenePresentationProfile directPerf =
        engine::scene::namedBaseScenePresentationProfile(
            makeNamedSceneConfig("aquarium_test_perf_probe", true, false));
    require(directPerf.name == "aquarium-direct" && !directPerf.water.foamEmitterEnabled,
            "aquarium_test_perf_probe must not enable the foam emitter");

    SceneConfig beachConfig = makeNamedSceneConfig("beach_sand_palettes", false, true);
    beachConfig.skyPreset = 4;
    const ScenePresentationProfile beach =
        engine::scene::namedBaseScenePresentationProfile(beachConfig);
    require(beach.name == "painted-outdoor-sky-v1",
            "Selected beach scene must adopt Painted Sky Profile v1");
    require(nearlyEqual(beach.lighting.sunElevation, 42.0f) &&
                nearlyEqual(beach.lighting.sunAzimuth, 122.0f) &&
                nearlyEqual(beach.lighting.sunIntensity, 3.4f) &&
                beach.shadows.useDdaShadows &&
                nearlyEqual(beach.shadows.requestedSunAngularRadius, 0.060f) &&
                beach.shadows.ddaSunSampleCount == 4 &&
                nearlyEqual(beach.shadows.temporalBlendAlpha, 0.07f),
            "Beach v1 must preserve its authored sun and adopt the accepted soft shadows");
    require(nearlyEqual(beach.postFx.highlightRecovery, 0.30f) &&
                beach.postFx.colorGradeEnabled && beach.postFx.taaEnabled &&
                beach.postFx.jitterEnabled && !beach.postFx.fxaaEnabled &&
                beach.voxelCellVariation.masterStrength > 0.0f &&
                beach.voxelSurface.paintedMaterialStrength > 0.0f &&
                beach.hemisphereAmbient.strength > 0.0f &&
                beach.atmosphere.density > 0.0f &&
                beach.paintedSky.strength > 0.0f &&
                beach.paintedClouds.strength == 0.0f,
            "Selected beach scene must adopt the complete Painted Sky v1 look");
    require(!beach.water.stylizedMode &&
                nearlyEqual(beach.water.causticsIntensity, 1.45f) &&
                nearlyEqual(beach.water.foamDepthThreshold, 0.55f) &&
                nearlyEqual(beach.water.foamOpacity, 0.42f) &&
                !beach.water.particlesPlanned && beach.glass.voxelGlassRefractEnabled,
            "Beach variant must encode the beach water look and keep default glass");

    const ScenePresentationProfile pond =
        engine::scene::namedBaseScenePresentationProfile(
            makeNamedSceneConfig("nature_pond_probe", false, false));
    require(pond.name == "painted-outdoor-clouds-v1",
            "Selected nature pond scene must adopt Painted Clouds Profile v1");
    require(!pond.water.stylizedMode && nearlyEqual(pond.water.refract, 0.014f) &&
                nearlyEqual(pond.water.waveScale, 0.075f) &&
                nearlyEqual(pond.water.reflectionStrength, 0.16f) &&
                nearlyEqual(pond.water.gradientStrength, 0.045f) &&
                !pond.water.particlesPlanned && !pond.water.foamEmitterEnabled,
            "Nature pond variant must encode the pond water look");
    require(pond.postFx.taaEnabled && pond.postFx.jitterEnabled &&
                !pond.postFx.fxaaEnabled &&
                pond.voxelCellVariation.masterStrength > 0.0f &&
                pond.voxelSurface.paintedMaterialStrength > 0.0f &&
                pond.hemisphereAmbient.strength > 0.0f &&
                pond.atmosphere.density > 0.0f &&
                pond.paintedSky.strength > 0.0f &&
                pond.paintedClouds.strength > 0.0f,
            "Selected nature pond scene must adopt every Painted Clouds v1 renderer bucket");
    require(pond.glass.tint == glm::vec3(0.94f, 0.985f, 1.0f) &&
                pond.glass.reflection == glm::vec3(0.42f, 0.60f, 0.76f) &&
                nearlyEqual(pond.glass.absorption, 0.32f) &&
                nearlyEqual(pond.glass.thicknessScale, 0.16f) &&
                nearlyEqual(pond.glass.refract, 0.0025f) &&
                nearlyEqual(pond.glass.ior, 1.46f) &&
                nearlyEqual(pond.glass.iridescentStrength, 0.18f) &&
                nearlyEqual(pond.glass.bubbleIntensity, 0.018f) &&
                !pond.glass.voxelGlassRefractEnabled,
            "Nature pond must own a clear, subtly iridescent dome-glass look");

    const ScenePresentationProfile densityProbe =
        engine::scene::namedBaseScenePresentationProfile(
            makeNamedSceneConfig("nature_pond_density_dense_bank_probe", false,
                                 true));
    require(densityProbe.name == "painted-outdoor-clouds-v1" &&
                densityProbe.shadows == pond.shadows &&
                densityProbe.ambientOcclusion == pond.ambientOcclusion &&
                densityProbe.postFx == pond.postFx &&
                densityProbe.voxelSurface == pond.voxelSurface &&
                densityProbe.paintedSky == pond.paintedSky &&
                densityProbe.paintedClouds == pond.paintedClouds,
            "Density probes must preserve the Soft Profile v2 "
            "renderer controls");

    const ScenePresentationProfile pondSunroof =
        engine::scene::namedBaseScenePresentationProfile(
            makeNamedSceneConfig("nature_pond_sunroof_probe", false, true));
    require(pondSunroof.name == "painted-outdoor-clouds-v1" &&
                pondSunroof.lighting == pond.lighting &&
                pondSunroof.shadows == pond.shadows &&
                pondSunroof.ambientOcclusion == pond.ambientOcclusion &&
                pondSunroof.postFx == pond.postFx &&
                pondSunroof.voxelSurface == pond.voxelSurface &&
                pondSunroof.water == pond.water &&
                pondSunroof.glass == pond.glass &&
                pondSunroof.paintedSky == pond.paintedSky &&
                pondSunroof.paintedClouds == pond.paintedClouds,
            "The pond sunroof performance probe must retain the complete painted pond renderer and clear-glass profile");

    const ScenePresentationProfile procedural =
        engine::scene::namedBaseScenePresentationProfile(
            makeNamedSceneConfig("procedural_world", false, true));
    require(procedural.name == "painted-outdoor-sky-v1" &&
                procedural.postFx.taaEnabled && procedural.postFx.jitterEnabled &&
                !procedural.postFx.fxaaEnabled &&
                procedural.voxelCellVariation.masterStrength > 0.0f &&
                procedural.hemisphereAmbient.strength > 0.0f &&
                procedural.atmosphere.density > 0.0f &&
                procedural.paintedSky.strength > 0.0f &&
                procedural.paintedClouds.strength == 0.0f,
            "Selected procedural world must adopt the complete Soft Profile v2 look");

    // classification must respect content flags, not just names.
    const ScenePresentationProfile beachWithoutFlag =
        engine::scene::namedBaseScenePresentationProfile(
            makeNamedSceneConfig("beach_sand_palettes", false, false));
    require(beachWithoutFlag.name == "scene-base",
            "Beach name without the procedural flag must fall back to scene-base");

    require(sunroof.lighting != fishbowl.lighting &&
                sunroof.water != fishbowl.water && sunroof.glass != fishbowl.glass &&
                sunroof.voxelCellVariation != fishbowl.voxelCellVariation &&
                sunroof.postFx != direct.postFx && fishbowl.postFx != direct.postFx &&
                beach.lighting != sunroof.lighting,
            "Shared painted presentation must preserve each aquarium/beach scene identity");
}

void testNamedBaseFollowsConfigSky()
{
    SceneConfig nightConfig = makeNamedSceneConfig("voxel_import", false, false);
    nightConfig.skyPreset = 2;
    nightConfig.skyColor = glm::vec3(0.01f, 0.02f, 0.05f);
    nightConfig.enablePointLights = false;
    const ScenePresentationProfile night =
        engine::scene::namedBaseScenePresentationProfile(nightConfig);
    require(night.name == "scene-base", "Unclassified scenes must use the base variant");
    require(night.lighting.skyPreset == 2 &&
                night.lighting.skyColor == nightConfig.skyColor &&
                nearlyEqual(night.lighting.sunElevation, -20.0f) &&
                nearlyEqual(night.lighting.sunIntensity, 0.3f) &&
                !night.lighting.pointLightsEnabled,
            "Base variant must take the config sky and the preset-authored sun");
    const ScenePresentationProfile defaults{};
    require(night.shadows == defaults.shadows &&
                night.ambientOcclusion == defaults.ambientOcclusion &&
                night.postFx == defaults.postFx &&
                night.voxelSurface == defaults.voxelSurface &&
                night.glass == defaults.glass,
            "Base variant must keep schema defaults outside the lighting/water buckets");
    require(!night.water.foamEmitterEnabled &&
                nearlyEqual(night.water.foamEmitterIntensity, 0.62f) &&
                nearlyEqual(night.water.foamEmitterScale, 0.24f) &&
                nearlyEqual(night.water.foamEmitterSpread, 0.34f),
            "Base variant must encode the helper's base water foam-emitter values");

    SceneConfig customConfig = makeNamedSceneConfig("voxel_import", false, false);
    customConfig.skyPreset = 4;
    customConfig.skyColor = glm::vec3(0.5f, 0.6f, 0.7f);
    const ScenePresentationProfile custom =
        engine::scene::namedBaseScenePresentationProfile(customConfig);
    require(custom.lighting.skyPreset == 4 &&
                custom.lighting.skyColor == customConfig.skyColor &&
                nearlyEqual(custom.lighting.sunElevation, 55.0f) &&
                nearlyEqual(custom.lighting.sunIntensity, 2.5f),
            "Custom sky preset must keep the schema-default sun");
}

void testProfileDeltaDescriber()
{
    const ScenePresentationProfile base{};
    ScenePresentationProfile changed = base;
    require(engine::scene::describeScenePresentationProfileDelta(base, changed).empty(),
            "Identical profiles must produce an empty delta");

    changed.lighting.skyColor = glm::vec3(1.0f, 0.0f, 0.0f);
    changed.shadows.useDdaShadows = true;
    changed.ambientOcclusion.distanceMode =
        engine::render::AmbientOcclusionDistanceMode::ProjectedScreenRadius;
    changed.postFx.taaEnabled = true;
    changed.postFx.taaSoftEdgeStrength = 0.3f;
    changed.voxelCellVariation.masterStrength = 1.0f;
    changed.hemisphereAmbient.strength = 0.5f;
    changed.atmosphere.density = 0.01f;
    changed.paintedSky.strength = 1.0f;
    changed.paintedClouds.strength = 1.0f;
    const std::vector<std::string> delta =
        engine::scene::describeScenePresentationProfileDelta(base, changed);
    require(delta.size() == 10, "Ten changed fields must produce ten delta entries");
    require(deltaContains(delta, "lighting.skyColor") &&
                deltaContains(delta, "shadows.useDdaShadows") &&
                deltaContains(delta, "ambientOcclusion.distanceMode") &&
                deltaContains(delta, "postFx.taaEnabled") &&
                deltaContains(delta, "postFx.taaSoftEdgeStrength") &&
                deltaContains(delta, "voxelCellVariation.masterStrength") &&
                deltaContains(delta, "hemisphereAmbient.strength") &&
                deltaContains(delta, "atmosphere.density") &&
                deltaContains(delta, "paintedSky.strength") &&
                deltaContains(delta, "paintedClouds.strength"),
            "The delta must name each changed field");
}

void testResolveAndApplySceneProfile()
{
    SceneConfig sunroofConfig = makeNamedSceneConfig("aquarium_sunroof", true, false);

    int skyPreset = 0;
    glm::vec3 skyColor{0.09f, 0.29f, 0.88f};
    engine::render::LightingSettings lighting{};
    engine::render::ShadowSettings shadows{};
    engine::render::AmbientOcclusionSettings ao{};
    engine::render::PostFxSettings postFx{};
    engine::render::VoxelDebugSettings voxel{};
    engine::render::WaterSettings water{};
    engine::render::GlassSettings glass{};

    // without a serialized profile, capture migration parity and then make the
    // named base the authoritative writer.
    const ScenePresentationProfile variant =
        engine::scene::namedBaseScenePresentationProfile(sunroofConfig);
    engine::scene::SceneProfileResolution resolution =
        engine::scene::resolveAndApplySceneProfile(sunroofConfig, skyPreset, skyColor,
                                                   lighting, shadows, ao, postFx, voxel,
                                                   water, glass);
    const ScenePresentationProfile after = engine::scene::captureScenePresentationProfile(
        variant.name, skyPreset, skyColor, lighting, shadows, ao, postFx, voxel, water,
        glass);
    require(!resolution.serializedApplied &&
                resolution.source == ScenePresentationProfileSource::NamedBase &&
                resolution.appliedName == variant.name,
            "Without a serialized profile the named base must be applied");
    require(after == variant, "Named resolution must replace predecessor settings");
    require(resolution.namedBaseName == "painted-outdoor-v3",
            "Resolution must report the sunroof's painted named base");
    require(!resolution.namedBaseParityDelta.empty() &&
                deltaContains(resolution.namedBaseParityDelta, "lighting.sunElevation") &&
                deltaContains(resolution.namedBaseParityDelta, "water.stylizedMode") &&
                deltaContains(resolution.namedBaseParityDelta,
                              "glass.voxelGlassRefractEnabled"),
            "Schema-default live state must differ from the sunroof variant");

    // when the live state matches the variant, the parity delta is empty.
    engine::scene::applyScenePresentationProfile(variant, skyPreset, skyColor, lighting,
                                                 shadows, ao, postFx, voxel, water,
                                                 glass);
    resolution = engine::scene::resolveAndApplySceneProfile(
        sunroofConfig, skyPreset, skyColor, lighting, shadows, ao, postFx, voxel, water,
        glass);
    require(!resolution.serializedApplied && resolution.namedBaseParityDelta.empty(),
            "Matching live state must produce an empty parity delta");

    // a serialized profile is the final owner and is applied.
    ScenePresentationProfile serialized = variant;
    serialized.name = "stored-look";
    serialized.postFx.bloomThreshold = 0.77f;
    serialized.lighting.pointLightsEnabled = false;
    serialized.water.waveAmp = 0.51f;
    serialized.glass.ior = 1.21f;
    sunroofConfig.presentationProfile = serialized;
    resolution = engine::scene::resolveAndApplySceneProfile(
        sunroofConfig, skyPreset, skyColor, lighting, shadows, ao, postFx, voxel, water,
        glass);
    require(resolution.serializedApplied &&
                resolution.source == ScenePresentationProfileSource::SerializedScene &&
                resolution.appliedName == "stored-look",
            "A serialized profile must be applied as the final owner");
    require(nearlyEqual(postFx.bloomThreshold_, 0.77f) && !lighting.pointLightsEnabled_ &&
                nearlyEqual(water.waterWaveAmp_, 0.51f) &&
                nearlyEqual(glass.glassIor_, 1.21f),
            "Applying the serialized profile must write the live settings");
}

struct RuntimePresentationState
{
    Input input;
    int skyPreset = 0;
    glm::vec3 skyColor{0.09f, 0.29f, 0.88f};
    engine::render::LightingSettings lighting;
    engine::render::ShadowSettings shadows;
    engine::render::AmbientOcclusionSettings ao;
    engine::render::PostFxSettings postFx;
    engine::render::VoxelDebugSettings voxel;
    engine::render::WaterSettings water;
    engine::render::GlassSettings glass;

    RuntimePresentationState()
    {
        input.setMaxLights(16);
        lighting.areaLights_.resize(16);
    }
};

engine::scene::ScenePresentationRuntimeState applyRuntimeProfile(
    const SceneConfig& config, RuntimePresentationState& state)
{
    return app::resolveApplyAndSyncScenePresentationProfile(
        config, state.input, state.skyPreset, state.skyColor, state.lighting,
        state.shadows, state.ao, state.postFx, state.voxel, state.water, state.glass,
        16);
}

ScenePresentationProfile captureRuntimeProfile(const RuntimePresentationState& state,
                                               const std::string& name)
{
    return engine::scene::captureScenePresentationProfile(
        name, state.skyPreset, state.skyColor, state.lighting, state.shadows, state.ao,
        state.postFx, state.voxel, state.water, state.glass);
}

void testRuntimeBridgeSynchronizesSerializedStartupAndLiveControls()
{
    SceneConfig config = makeNamedSceneConfig("aquarium_sunroof", true, false);
    ScenePresentationProfile stored =
        engine::scene::namedBaseScenePresentationProfile(config);
    stored.name = "startup-stored-look";
    stored.lighting.pointLightsEnabled = false;
    stored.lighting.lightCount = 3;
    stored.postFx.tonemapEnabled = false;
    stored.postFx.exposure = 1.7f;
    stored.postFx.bloomEnabled = false;
    config.presentationProfile = stored;

    RuntimePresentationState state;
    state.input.setLightsEnabled(true);
    state.input.setLightCount(12);
    state.input.setTonemapEnabled(true);
    state.input.setExposure(0.6f);
    state.input.setBloomEnabled(true);

    const engine::scene::ScenePresentationRuntimeState runtimeState =
        applyRuntimeProfile(config, state);
    const engine::scene::SceneProfileResolution& resolution =
        runtimeState.resolution;
    require(resolution.serializedApplied &&
                resolution.source == ScenePresentationProfileSource::SerializedScene,
            "Startup bridge must resolve the serialized scene profile");
    require(!state.lighting.pointLightsEnabled_ && state.lighting.lightCount_ == 3 &&
                !state.postFx.tonemapEnabled_ &&
                nearlyEqual(state.postFx.exposure_, 1.7f) &&
                !state.postFx.bloomEnabled_,
            "Serialized startup must own the render-settings buckets");
    require(!state.input.lightsEnabled() && state.input.lightCount() == 3 &&
                !state.input.tonemapEnabled() &&
                nearlyEqual(state.input.exposure(), 1.7f) &&
                !state.input.bloomEnabled(),
            "Serialized startup must synchronize every mirrored Input control");

    const engine::scene::ScenePresentationProfileReadout startupReadout =
        app::inspectScenePresentationProfileRuntimeState(
            runtimeState, state.skyPreset, state.skyColor, state.lighting,
            state.shadows, state.ao, state.postFx, state.voxel, state.water,
            state.glass);
    require(startupReadout.appliedName == "startup-stored-look" &&
                startupReadout.namedBaseName == "painted-outdoor-v3" &&
                startupReadout.resolvedSource ==
                    ScenePresentationProfileSource::SerializedScene &&
                startupReadout.effectiveSource ==
                    ScenePresentationProfileSource::SerializedScene &&
                startupReadout.liveEditDelta.empty(),
            "Fresh startup readout must report serialized provenance with no live delta");

    state.input.setLightsEnabled(true);
    state.input.setLightCount(5);
    state.input.setTonemapEnabled(true);
    state.input.setExposure(2.1f);
    state.input.setBloomEnabled(true);
    state.ao.aoMaxDistance_ += 1.0f;
    state.ao.aoIntensity_ += 0.1f;
    app::syncPresentationSettingsFromInput(
        state.input, state.lighting, state.postFx, 16);
    require(state.lighting.pointLightsEnabled_ && state.lighting.lightCount_ == 5 &&
                state.postFx.tonemapEnabled_ &&
                nearlyEqual(state.postFx.exposure_, 2.1f) &&
                state.postFx.bloomEnabled_,
            "Live Input controls must update the authoritative render buckets");

    const engine::scene::ScenePresentationProfileReadout liveReadout =
        app::inspectScenePresentationProfileRuntimeState(
            runtimeState, state.skyPreset, state.skyColor, state.lighting,
            state.shadows, state.ao, state.postFx, state.voxel, state.water,
            state.glass);
    require(liveReadout.resolvedSource ==
                    ScenePresentationProfileSource::SerializedScene &&
                liveReadout.effectiveSource ==
                    ScenePresentationProfileSource::LiveEdit,
            "Readout must retain resolved provenance while identifying live edits");
    require(containsField(liveReadout.liveEditDelta,
                          "lighting.pointLightsEnabled") &&
                containsField(liveReadout.liveEditDelta,
                              "lighting.lightCount") &&
                containsField(liveReadout.liveEditDelta,
                              "postFx.tonemapEnabled") &&
                containsField(liveReadout.liveEditDelta,
                              "postFx.exposure") &&
                containsField(liveReadout.liveEditDelta,
                              "postFx.bloomEnabled") &&
                containsField(liveReadout.liveEditDelta,
                              "ambientOcclusion.maxDistance") &&
                containsField(liveReadout.liveEditDelta,
                              "ambientOcclusion.intensity"),
            "Readout must name every live Input-owned and AO presentation override");
}

void testRuntimeBridgeReloadAndLoadOrderInvariance()
{
    const SceneConfig pond =
        makeNamedSceneConfig("nature_pond_probe", false, true);
    const SceneConfig fishbowl =
        makeNamedSceneConfig("fishbowl_perf_probe", true, false);

    RuntimePresentationState pondThenFishbowl;
    applyRuntimeProfile(pond, pondThenFishbowl);
    applyRuntimeProfile(fishbowl, pondThenFishbowl);
    const ScenePresentationProfile fishbowlAfterPond = captureRuntimeProfile(
        pondThenFishbowl, "painted-outdoor-v3");

    RuntimePresentationState freshFishbowl;
    applyRuntimeProfile(fishbowl, freshFishbowl);
    const ScenePresentationProfile fishbowlFresh =
        captureRuntimeProfile(freshFishbowl, "painted-outdoor-v3");
    require(fishbowlAfterPond == fishbowlFresh,
            "Pond -> fishbowl reload must equal a fresh fishbowl startup");
    require(pondThenFishbowl.input.lightsEnabled() ==
                    freshFishbowl.input.lightsEnabled() &&
                pondThenFishbowl.input.lightCount() ==
                    freshFishbowl.input.lightCount() &&
                pondThenFishbowl.input.tonemapEnabled() ==
                    freshFishbowl.input.tonemapEnabled() &&
                nearlyEqual(pondThenFishbowl.input.exposure(),
                            freshFishbowl.input.exposure()) &&
                pondThenFishbowl.input.bloomEnabled() ==
                    freshFishbowl.input.bloomEnabled(),
            "Pond -> fishbowl reload must synchronize the same Input controls as startup");

    RuntimePresentationState fishbowlThenPond;
    applyRuntimeProfile(fishbowl, fishbowlThenPond);
    applyRuntimeProfile(pond, fishbowlThenPond);
    const ScenePresentationProfile pondAfterFishbowl =
        captureRuntimeProfile(fishbowlThenPond, "nature-pond");

    RuntimePresentationState freshPond;
    applyRuntimeProfile(pond, freshPond);
    const ScenePresentationProfile pondFresh =
        captureRuntimeProfile(freshPond, "nature-pond");
    require(pondAfterFishbowl == pondFresh,
            "Fishbowl -> pond reload must equal a fresh pond startup");
}

void testRuntimeBridgeAppliesV1AndIsolatesCompatibilityScenes()
{
    const SceneConfig pond =
        makeNamedSceneConfig("nature_pond_probe", false, true);
    const SceneConfig compatibilityAquarium =
        makeNamedSceneConfig("aquarium_test", true, false);

    RuntimePresentationState state;
    state.voxel.voxelCellVariation_ =
        engine::makeVoxelCellVariationSettings(
            engine::VoxelCellVariationPreset::SoftOutdoorV0);
    state.lighting.hemisphereAmbient_ =
        engine::render::makeHemisphereAmbientSettings(
            engine::render::HemisphereAmbientPreset::WarmCoolOutdoorV0);
    state.lighting.sceneAtmosphere_ =
        engine::render::makeSceneAtmosphereSettings(
            engine::render::SceneAtmospherePreset::OutdoorHazeV0);
    state.lighting.paintedSky_ = engine::render::makePaintedSkySettings(
        engine::render::PaintedSkyPreset::SoftDayV0);
    state.lighting.paintedClouds_ = engine::render::makePaintedCloudSettings(
        engine::render::PaintedCloudPreset::SoftDayV0);
    require(state.voxel.voxelCellVariation_.enabled(),
            "The outdoor cell-variation candidate must be active before compatibility load");
    require(state.lighting.hemisphereAmbient_.enabled(),
            "The hemisphere-ambient candidate must be active before compatibility load");
    require(state.lighting.sceneAtmosphere_.enabled(),
            "The scene-atmosphere candidate must be active before compatibility load");
    require(state.lighting.paintedSky_.enabled(),
            "The painted-sky candidate must be active before compatibility load");
    require(state.lighting.paintedClouds_.enabled(),
            "The painted-cloud candidate must be active before compatibility load");

    applyRuntimeProfile(compatibilityAquarium, state);
    require(state.voxel.voxelCellVariation_ ==
                engine::VoxelCellVariationSettings{},
            "A profile without v1 look fields must reset cell variation to compatibility off");
    require(state.lighting.hemisphereAmbient_ ==
                engine::render::HemisphereAmbientSettings{},
            "A profile without v1 look fields must reset hemisphere ambient to compatibility off");
    require(state.lighting.sceneAtmosphere_ ==
                engine::render::SceneAtmosphereSettings{},
            "A profile without v1 look fields must reset atmosphere to compatibility off");
    require(state.lighting.paintedSky_ == engine::render::PaintedSkySettings{},
            "A profile without v5 sky fields must reset painted sky to compatibility off");
    require(state.lighting.paintedClouds_ ==
                engine::render::PaintedCloudSettings{},
            "A profile without v6 cloud fields must reset painted clouds to compatibility off");

    require(app::applyAutomationVoxelCellVariationPreset("soft-v0", state.voxel) &&
                state.voxel.voxelCellVariation_.enabled(),
            "The automation bridge must apply the outdoor candidate");
    require(app::applyAutomationHemisphereAmbientPreset(
                "warm-cool-v0", state.lighting) &&
                state.lighting.hemisphereAmbient_.enabled(),
            "The automation bridge must apply the ambient candidate");
    require(app::applyAutomationSceneAtmospherePreset(
                "outdoor-haze-v0", state.lighting) &&
                state.lighting.sceneAtmosphere_.enabled(),
            "The automation bridge must apply the atmosphere candidate");
    const engine::render::SceneAtmosphereSettings automatedAtmosphere =
        state.lighting.sceneAtmosphere_;
    require(!app::applyAutomationSceneAtmospherePreset(
                "unknown", state.lighting) &&
                state.lighting.sceneAtmosphere_ == automatedAtmosphere,
            "An invalid atmosphere preset must leave the live candidate unchanged");
    require(app::applyAutomationLightingDebugMode("15", state.lighting) &&
                state.lighting.lightingDebugMode_ == 15,
            "The automation bridge must select the hemisphere ambient debug view");
    require(app::applyAutomationLightingDebugMode("99", state.lighting) &&
                state.lighting.lightingDebugMode_ == 15,
            "The automation bridge must clamp lighting debug modes to the UI range");
    require(!app::applyAutomationLightingDebugMode("invalid", state.lighting) &&
                state.lighting.lightingDebugMode_ == 15,
            "Invalid automation debug modes must leave the active mode unchanged");

    applyRuntimeProfile(compatibilityAquarium, state);
    require(state.voxel.voxelCellVariation_ ==
                engine::VoxelCellVariationSettings{},
            "Outdoor cell variation must not leak into an aquarium reload");
    require(state.lighting.hemisphereAmbient_ ==
                engine::render::HemisphereAmbientSettings{},
            "Outdoor hemisphere ambient must not leak into an aquarium reload");
    require(state.lighting.sceneAtmosphere_ ==
                engine::render::SceneAtmosphereSettings{},
            "Outdoor atmosphere must not leak into an aquarium reload");
    require(state.lighting.paintedSky_ == engine::render::PaintedSkySettings{},
            "Outdoor painted sky must not leak into an aquarium reload");
    require(state.lighting.paintedClouds_ ==
                engine::render::PaintedCloudSettings{},
            "Outdoor painted clouds must not leak into an aquarium reload");

    SceneConfig persistedPond = pond;
    persistedPond.presentationProfile =
        engine::scene::makeSoftOutdoorProfileV1(
            engine::scene::namedBaseScenePresentationProfile(pond));
    applyRuntimeProfile(persistedPond, state);
    const engine::VoxelCellVariationSettings expectedVariation =
        engine::makeVoxelCellVariationSettings(
            engine::VoxelCellVariationPreset::SoftOutdoorV0);
    const engine::render::HemisphereAmbientSettings expectedHemisphere =
        engine::render::makeHemisphereAmbientSettings(
            engine::render::HemisphereAmbientPreset::WarmCoolOutdoorV0);
    const engine::render::SceneAtmosphereSettings expectedAtmosphere =
        engine::render::makeSceneAtmosphereSettings(
            engine::render::SceneAtmospherePreset::OutdoorHazeV0);
    require(state.voxel.voxelCellVariation_ == expectedVariation &&
                state.lighting.hemisphereAmbient_ == expectedHemisphere &&
                state.lighting.sceneAtmosphere_ == expectedAtmosphere,
            "A serialized Soft Profile v1 must completely apply all new renderer buckets");

    applyRuntimeProfile(compatibilityAquarium, state);
    require(state.voxel.voxelCellVariation_ ==
                engine::VoxelCellVariationSettings{} &&
                state.lighting.hemisphereAmbient_ ==
                    engine::render::HemisphereAmbientSettings{} &&
                state.lighting.sceneAtmosphere_ ==
                    engine::render::SceneAtmosphereSettings{},
            "Soft Profile v1 must not leak into a later aquarium load");

    RuntimePresentationState freshV1;
    applyRuntimeProfile(persistedPond, freshV1);
    require(state.voxel.voxelCellVariation_ != expectedVariation &&
                freshV1.voxel.voxelCellVariation_ == expectedVariation &&
                freshV1.lighting.hemisphereAmbient_ == expectedHemisphere &&
                freshV1.lighting.sceneAtmosphere_ == expectedAtmosphere,
            "A fresh Soft Profile v1 startup must match the persisted candidate after load-order isolation");
}

void testSceneAtmosphereContract()
{
    using engine::render::SceneAtmospherePreset;
    using engine::render::SceneAtmosphereSample;
    using engine::render::SceneAtmosphereSettings;

    const glm::vec3 sunDir = glm::normalize(glm::vec3(0.4f, 0.6f, 0.2f));
    const glm::vec3 skyTint{0.5f, 0.7f, 0.9f};
    const glm::vec3 sunTint{1.0f, 0.8f, 0.6f};
    const glm::vec3 origin{0.0f, 2.0f, 0.0f};
    const glm::vec3 forward{0.0f, 0.0f, -1.0f};

    // zero density is an exact no-op on every path, including composition.
    const SceneAtmosphereSettings off{};
    require(!off.enabled(), "Default atmosphere settings must be disabled");
    const SceneAtmosphereSample offSample = engine::render::evaluateSceneAtmosphere(
        off, origin, forward, 250.0f, sunDir, skyTint, sunTint);
    const glm::vec3 surface{0.31f, 0.62f, 0.18f};
    require(offSample.transmittance == 1.0f && offSample.inscatter == glm::vec3(0.0f) &&
                engine::render::composeSceneAtmosphere(surface, offSample) == surface,
            "Zero-density atmosphere must be an exact pass-through");
    require(engine::render::evaluateSceneAtmosphereSky(off, origin, forward, sunDir,
                                                       skyTint, sunTint)
                    .transmittance == 1.0f,
            "Zero-density sky atmosphere must be an exact pass-through");
    const glm::vec3 waterSurfaceComposite{0.82f, 0.71f, 0.44f};
    require(engine::render::attenuateSceneAtmosphereSurfaceContribution(
                off, 3.0f, surface, waterSurfaceComposite) == waterSurfaceComposite,
            "Disabled atmosphere must preserve the water surface composite exactly");
    const glm::vec3 starEmission{1.0f, 0.82f, 0.56f};
    require(engine::render::attenuateSceneAtmosphereSkyEmission(
                off, 3.0f, starEmission) == starEmission,
            "Disabled atmosphere must preserve procedural star emission exactly");

    const SceneAtmosphereSettings haze = engine::render::makeSceneAtmosphereSettings(
        SceneAtmospherePreset::OutdoorHazeV0);
    require(haze.enabled(), "The outdoor haze candidate must be enabled");
    const glm::vec4 packedAtmosphere =
        engine::render::packSceneAtmosphereParameters(haze);
    const glm::vec3 lightTravelDirection = -sunDir;
    const glm::vec4 packedSun =
        engine::render::packSceneAtmosphereSun(haze, lightTravelDirection);
    require(packedAtmosphere ==
                    glm::vec4(haze.density, haze.heightFalloff, haze.baseHeight,
                              haze.sunPhaseStrength) &&
                nearlyEqual(packedSun.x, haze.sunPhaseExponent) &&
                nearlyEqual(packedSun.y, sunDir.x) &&
                nearlyEqual(packedSun.z, sunDir.y) &&
                nearlyEqual(packedSun.w, sunDir.z),
            "Atmosphere-aware passes must share one stable parameter packing contract");

    // the refraction base already owns camera-path extinction and inscatter.
    // water surface atmosphere therefore attenuates only the added surface delta.
    const float surfaceOpticalDepth = 0.65f;
    const float surfaceTransmittance = std::exp(-surfaceOpticalDepth);
    const glm::vec3 atmospheredSurface =
        engine::render::attenuateSceneAtmosphereSurfaceContribution(
            haze, surfaceOpticalDepth, surface, waterSurfaceComposite);
    const glm::vec3 expectedAtmospheredSurface =
        surface + (waterSurfaceComposite - surface) * surfaceTransmittance;
    require(nearlyEqual(atmospheredSurface.x, expectedAtmospheredSurface.x) &&
                nearlyEqual(atmospheredSurface.y, expectedAtmospheredSurface.y) &&
                nearlyEqual(atmospheredSurface.z, expectedAtmospheredSurface.z),
            "Water atmosphere must preserve the refracted base and attenuate only surface-added light");
    require(engine::render::attenuateSceneAtmosphereSurfaceContribution(
                haze, 0.0f, surface, waterSurfaceComposite) == waterSurfaceComposite,
            "A zero-air path must preserve the water surface composite exactly");
    require(engine::render::attenuateSceneAtmosphereSkyEmission(
                haze, 0.0f, starEmission) == starEmission,
            "A zero-air sky path must preserve procedural star emission exactly");
    const glm::vec3 attenuatedStars =
        engine::render::attenuateSceneAtmosphereSkyEmission(
            haze, surfaceOpticalDepth, starEmission);
    const glm::vec3 expectedStars = starEmission * surfaceTransmittance;
    require(nearlyEqual(attenuatedStars.x, expectedStars.x) &&
                nearlyEqual(attenuatedStars.y, expectedStars.y) &&
                nearlyEqual(attenuatedStars.z, expectedStars.z),
            "Procedural stars must receive extinction only, without duplicate inscatter");

    // transmittance falls monotonically with distance and density.
    const float tNear =
        engine::render::evaluateSceneAtmosphere(haze, origin, forward, 10.0f, sunDir,
                                                skyTint, sunTint)
            .transmittance;
    const float tFar =
        engine::render::evaluateSceneAtmosphere(haze, origin, forward, 120.0f, sunDir,
                                                skyTint, sunTint)
            .transmittance;
    require(tNear > tFar && tNear < 1.0f && tFar > 0.0f,
            "Haze must accumulate monotonically with distance");
    SceneAtmosphereSettings denser = haze;
    denser.density *= 2.0f;
    require(engine::render::evaluateSceneAtmosphere(denser, origin, forward, 120.0f,
                                                    sunDir, skyTint, sunTint)
                    .transmittance < tFar,
            "Higher density must lower transmittance");

    // the horizontal analytic limit matches a near-horizontal evaluation.
    const float flatDepth =
        engine::render::sceneAtmosphereOpticalDepth(haze, 2.0f, 0.0f, 80.0f);
    const float nearFlatDepth =
        engine::render::sceneAtmosphereOpticalDepth(haze, 2.0f, 1e-7f, 80.0f);
    require(nearlyEqual(flatDepth, nearFlatDepth, 1e-4f),
            "The horizontal limit of the height integral must be continuous");

    // segment additivity: fogging two consecutive segments equals fogging their sum.
    const glm::vec3 slanted = glm::normalize(glm::vec3(0.3f, 0.4f, -0.85f));
    const float lengthA = 22.0f;
    const float lengthB = 47.0f;
    const glm::vec3 midpoint = origin + slanted * lengthA;
    const float depthA =
        engine::render::sceneAtmosphereOpticalDepth(haze, origin.y, slanted.y, lengthA);
    const float depthB = engine::render::sceneAtmosphereOpticalDepth(
        haze, midpoint.y, slanted.y, lengthB);
    const float depthTotal = engine::render::sceneAtmosphereOpticalDepth(
        haze, origin.y, slanted.y, lengthA + lengthB);
    require(nearlyEqual(depthA + depthB, depthTotal, 1e-4f),
            "Optical depth must be additive across consecutive segments");

    // ordered medium ownership: water intervals may arrive out of order and
    // overlap, but only their merged union is removed from the air integral.
    const glm::vec3 horizontalOrigin{0.0f, 2.0f, 0.0f};
    const glm::vec3 horizontalRay{1.0f, 0.0f, 0.0f};
    const std::vector<engine::render::SceneAtmosphereExcludedInterval>
        mixedIntervals{{20.0f, 35.0f}, {0.0f, 10.0f}, {30.0f, 40.0f}};
    const float mixedAirDepth = engine::render::sceneAtmosphereAirOpticalDepth(
        haze, horizontalOrigin, horizontalRay, 60.0f, mixedIntervals);
    const float expectedMixedDepth =
        engine::render::sceneAtmosphereOpticalDepth(haze, horizontalOrigin.y,
                                                    horizontalRay.y, 30.0f);
    require(nearlyEqual(mixedAirDepth, expectedMixedDepth, 1e-4f),
            "Air optical depth must clip, sort, and merge excluded water intervals");

    const std::vector<engine::render::SceneAtmosphereExcludedInterval>
        underwaterToAir{{0.0f, 12.0f}};
    const float exitOwnedDepth = engine::render::sceneAtmosphereAirOpticalDepth(
        haze, horizontalOrigin, horizontalRay, 30.0f, underwaterToAir);
    const float expectedExitDepth = engine::render::sceneAtmosphereOpticalDepth(
        haze, horizontalOrigin.y, horizontalRay.y, 18.0f);
    require(nearlyEqual(exitOwnedDepth, expectedExitDepth, 1e-4f),
            "Underwater-to-air rays must begin atmospheric integration at the water exit");

    const std::vector<engine::render::SceneAtmosphereExcludedInterval>
        fullySubmerged{{-5.0f, 50.0f}};
    require(engine::render::sceneAtmosphereAirOpticalDepth(
                haze, horizontalOrigin, horizontalRay, 30.0f, fullySubmerged) == 0.0f,
            "Fully submerged camera-to-surface paths must receive no air atmosphere");

    // positive height falloff: the same segment higher up carries less haze.
    require(engine::render::sceneAtmosphereOpticalDepth(haze, 30.0f, 0.0f, 80.0f) <
                flatDepth,
            "Raising the segment must reduce optical depth under height falloff");

    // sun-facing inscatter warms; anti-sun stays at the sky tint.
    const glm::vec3 towardSun = engine::render::sceneAtmosphereInscatterTint(
        haze, sunDir, sunDir, skyTint, sunTint);
    const glm::vec3 awaySun = engine::render::sceneAtmosphereInscatterTint(
        haze, -sunDir, sunDir, skyTint, sunTint);
    require(towardSun.x > awaySun.x && nearlyEqual(awaySun.z, skyTint.z, 1e-4f),
            "Inscatter must warm toward the sun and rest at the sky tint away from it");

    // sky limit: horizontal rays converge fully; upward rays keep finite clarity.
    require(engine::render::evaluateSceneAtmosphereSky(haze, origin, forward, sunDir,
                                                       skyTint, sunTint)
                    .transmittance == 0.0f,
            "Horizontal sky rays must converge to the inscatter tint");
    const float skyUp = engine::render::evaluateSceneAtmosphereSky(
                            haze, origin, glm::vec3(0.0f, 1.0f, 0.0f), sunDir, skyTint,
                            sunTint)
                            .transmittance;
    require(skyUp > 0.0f && skyUp < 1.0f,
            "Upward sky rays must keep finite transmittance");

    // preset naming round-trips through the automation-facing helpers.
    require(engine::render::sceneAtmospherePresetFromName("outdoor-haze-v0") ==
                    SceneAtmospherePreset::OutdoorHazeV0 &&
                engine::render::sceneAtmospherePresetFromName("disabled") ==
                    SceneAtmospherePreset::Disabled &&
                !engine::render::sceneAtmospherePresetFromName("unknown").has_value() &&
                engine::render::sceneAtmospherePresetName(
                    SceneAtmospherePreset::OutdoorHazeV0) == "outdoor-haze-v0",
            "Atmosphere preset names must round-trip and reject unknown names");
}

} // namespace

int main()
{
    try
    {
        testLegacyProfileMatchesRenderDefaults();
        testProfileResolutionPrecedence();
        testEffectiveTaaJitterAuthority();
        testSoftOutdoorProfileV0ReconstructionMatrix();
        testSoftOutdoorProfileV1OwnsAcceptedRendererFields();
        testSoftOutdoorProfileV2OwnsSoftEdgePresentation();
        testPaintedOutdoorProfileV3OwnsPaintedVoxelPresentation();
        testPaintedOutdoorSkyProfileV1OwnsBoundedSkyPresentation();
        testPaintedOutdoorCloudProfileV1OwnsBoundedWindDrivenLayer();
        testCaptureApplyRoundTripPreservesPersistentLook();
        testEffectiveDdaSunRadiusMatchesRuntimeCap();
        testNamedVariantsEncodeDistinctBaselineLooks();
        testNamedBaseFollowsConfigSky();
        testProfileDeltaDescriber();
        testResolveAndApplySceneProfile();
        testRuntimeBridgeSynchronizesSerializedStartupAndLiveControls();
        testRuntimeBridgeReloadAndLoadOrderInvariance();
        testRuntimeBridgeAppliesV1AndIsolatesCompatibilityScenes();
        testSceneAtmosphereContract();
        std::cout << "Scene presentation profile tests passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "Scene presentation profile test failure: " << error.what() << "\n";
        return 1;
    }
}
