#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include <glm/vec3.hpp>

#include "engine/render/AmbientOcclusionQualityTier.h"

namespace engine::render
{
struct AmbientOcclusionSettings;
struct GlassSettings;
struct LightingSettings;
struct PostFxSettings;
struct ShadowSettings;
struct VoxelDebugSettings;
struct WaterSettings;
} // namespace engine::render

namespace engine::scene
{

struct SceneLightingProfile
{
    int skyPreset = 0;
    glm::vec3 skyColor{0.09f, 0.29f, 0.88f};
    bool pointLightsEnabled = true;
    int lightCount = 32;
    float sunElevation = 55.0f;
    float sunAzimuth = 135.0f;
    glm::vec3 sunColor{1.0f, 0.95f, 0.85f};
    float sunIntensity = 2.5f;

    bool operator==(const SceneLightingProfile&) const = default;
};

struct SceneShadowProfile
{
    bool csmEnabled = true;
    bool useDdaShadows = false;
    float requestedSunAngularRadius = 0.02f;
    float maxShadowDistance = 100.0f;
    float normalBias = 0.05f;
    int maxSteps = 128;
    int ddaSunSampleCount = 4;
    float foliageOpacity = 1.25f;
    float temporalBlendAlpha = 0.1f;
    float temporalDepthReject = 0.001f;
    float temporalNormalRejectDot = 0.92f;
    float temporalClampSharpness = 0.85f;
    int spatialFilterRadius = 1;
    float spatialDepthSigma = 0.003f;
    float spatialValueSigma = 0.25f;
    float spatialNormalPower = 48.0f;
    int postDenoiseRadius = 1;
    float postDenoiseDepthSigma = 0.004f;
    float postDenoiseValueSigma = 0.18f;
    float postDenoiseNormalPower = 48.0f;
    bool localLightShadowsEnabled = true;
    int localShadowCastingLightCount = 4;
    float localTemporalBlendAlpha = 0.05f;
    float localTemporalDepthReject = 0.01f;
    bool localBlurEnabled = true;
    float localBlurSigma = 1.5f;
    float terminatorSoftness = 0.15f;
    int terminatorMode = 1;
    bool csmDitherEnabled = true;

    bool operator==(const SceneShadowProfile&) const = default;
};

struct SceneAmbientOcclusionProfile
{
    bool enabled = true;
    engine::render::AmbientOcclusionDistanceMode distanceMode =
        engine::render::AmbientOcclusionDistanceMode::AuthoredWorldDistance;
    float projectedRadiusPixels = 512.0f;
    float projectedMinimumWorldDistance = 2.0f;
    float maxDistance = 8.0f;
    float stepSize = 0.15f;
    float intensity = 1.5f;
    float contribution = 0.25f;
    float bias = 0.05f;
    int rayCount = 2;
    float temporalBlendAlpha = 0.08f;
    float temporalDepthReject = 0.02f;
    float temporalNormalRejectDot = 0.85f;

    bool operator==(const SceneAmbientOcclusionProfile&) const = default;
};

struct ScenePostFxProfile
{
    bool tonemapEnabled = true;
    float exposure = 1.0f;
    float highlightRecovery = 0.0f;
    bool bloomEnabled = true;
    float bloomThreshold = 1.2f;
    float bloomKnee = 0.5f;
    float bloomIntensity = 0.08f;
    float bloomSigma = 2.0f;
    float vignetteStrength = 0.12f;
    float grainStrength = 0.0f;
    bool colorGradeEnabled = false;
    float colorGradeStrength = 0.0f;
    float colorGradeSaturation = 1.0f;
    float colorGradeContrast = 1.0f;
    float colorGradeTemperature = 0.0f;
    bool pixelizationEnabled = false;
    float pixelizationBlockSize = 4.0f;
    float pixelizationStrength = 0.70f;
    float pixelizationEdgeFocus = 0.45f;
    bool depthOfFieldEnabled = false;
    float depthOfFieldFocusDistance = 10.0f;
    float depthOfFieldFocusRange = 6.0f;
    float depthOfFieldBlurStrength = 0.85f;
    bool taaEnabled = false;
    bool jitterEnabled = false;
    float taaSimilarityThreshold = 0.1f;
    float taaVelocityScale = 10.0f;
    float taaBlendMin = 0.05f;
    float taaBlendMax = 0.20f;
    float taaSharpen = 0.25f;
    float taaDepthEdgeThreshold = 0.1f;
    float taaCrossFrameDepthThreshold = 0.02f;
    float taaColorVarianceThreshold = 0.15f;
    // bounded current-frame reconstruction around depth, normal, and luminance
    // discontinuities. zero preserves the historical taa resolve exactly.
    float taaSoftEdgeStrength = 0.0f;
    bool fxaaEnabled = true;

    bool operator==(const ScenePostFxProfile&) const = default;
};

struct SceneVoxelSurfaceProfile
{
    float materialDetailStrength = 0.0f;
    float normalEdgeSmoothing = 0.12f;
    float pixelEdgeShadowStrength = 0.35f;
    float cavityStrength = 1.0f;
    // zero preserves the historical pbr response. positive values opt opaque
    // voxels into the shared matte, color-preserving painted-light treatment.
    float paintedMaterialStrength = 0.0f;

    bool operator==(const SceneVoxelSurfaceProfile&) const = default;
};

struct SceneVoxelCellVariationProfile
{
    float masterStrength = 0.0f;
    float genericAmplitude = 0.0f;
    float gravelAmplitude = 0.0f;
    float plantAmplitude = 0.0f;
    float stoneAmplitude = 0.0f;
    float woodAmplitude = 0.0f;
    float hueSpread = 0.018f;
    float saturationSpread = 0.12f;
    float valueSpread = 0.10f;
    float paletteFamilyStrength = 0.0f;

    bool operator==(const SceneVoxelCellVariationProfile&) const = default;
};

struct SceneHemisphereAmbientProfile
{
    float strength = 0.0f;
    glm::vec3 skyTint{1.0f};
    glm::vec3 groundTint{1.0f};

    bool operator==(const SceneHemisphereAmbientProfile&) const = default;
};

struct SceneAtmosphereProfile
{
    float density = 0.0f;
    float heightFalloff = 0.0f;
    float baseHeight = 0.0f;
    float sunPhaseStrength = 0.0f;
    float sunPhaseExponent = 4.0f;

    bool operator==(const SceneAtmosphereProfile&) const = default;
};

struct ScenePaintedSkyProfile
{
    float strength = 0.0f;
    glm::vec3 horizonTint{1.0f};
    glm::vec3 zenithTint{1.0f};
    glm::vec3 lowerHemisphereTint{1.0f};
    float gradientExponent = 1.0f;
    float horizonBandStrength = 0.0f;
    float horizonBandExponent = 4.0f;
    float sunDiscAngularRadius = 0.0105f;
    float sunDiscSoftness = 0.0020f;
    float sunDiscIntensity = 0.0f;
    float sunHaloIntensity = 0.0f;
    float sunHaloExponent = 48.0f;

    bool operator==(const ScenePaintedSkyProfile&) const = default;
};

struct ScenePaintedCloudProfile
{
    float strength = 0.0f;
    float coverage = 0.46f;
    float opacity = 0.76f;
    float softness = 0.085f;
    float altitude = 90.0f;
    float worldScale = 105.0f;
    float detailStrength = 0.38f;
    glm::vec3 lightTint{1.08f, 1.06f, 1.00f};
    glm::vec3 shadowTint{0.64f, 0.72f, 0.82f};
    float silverLiningStrength = 0.16f;
    float horizonFadeStart = 0.035f;
    float horizonFadeEnd = 0.18f;

    bool operator==(const ScenePaintedCloudProfile&) const = default;
};

// water look tuning. feature enablement (enableWater/useWaterV2) and the world-space
// water level are scene content owned by SceneConfig and stay out of the profile.
struct SceneWaterProfile
{
    bool stylizedMode = true;
    float absorption = 0.04f;
    float refract = 0.03f;
    float depthScale = 4.0f;
    float shallowBias = 0.5f;
    float depthToMeters = 8.0f;
    float specIntensity = 1.6f;
    float specPower = 128.0f;
    float waveScale = 0.09f;
    float waveAmp = 0.72f;
    bool crestHighlightsEnabled = true;
    float crestThreshold = 0.62f;
    float crestSoftness = 0.08f;
    float crestIntensity = 0.38f;
    float foamDepthThreshold = 0.8f;
    float foamOpacity = 0.6f;
    float foamScale = 0.2f;
    float distortionDepthScale = 0.3f;
    float edgeFadeDepth = 0.5f;
    float bandHardness = 0.6f;
    float reflectionStrength = 0.28f;
    float fresnelBias = 0.03f;
    bool causticsEnabled = false;
    float causticsIntensity = 0.52f;
    float causticsScale = 0.12f;
    float causticsSpeed = 0.28f;
    float causticsBanding = 0.34f;
    float causticsDepthFade = 18.0f;
    bool particlesPlanned = true;
    float particlesPlannedDensity = 0.35f;
    float particlesPlannedDrift = 0.20f;
    float particlesPlannedScale = 0.15f;
    bool foamEmitterEnabled = false;
    float foamEmitterIntensity = 0.65f;
    float foamEmitterRadius = 3.0f;
    float foamEmitterOffsetX = 0.0f;
    float foamEmitterOffsetZ = 0.0f;
    float foamEmitterScale = 0.25f;
    float foamEmitterSpread = 0.35f;
    float gradientStrength = 0.25f;
    bool planarReflectionEnabled = false;
    bool planarObliqueClipEnabled = false;
    float planarStrength = 0.30f;

    bool operator==(const SceneWaterProfile&) const = default;
};

// mesh and voxel glass look tuning. feature enablement (enableGlass) is scene content
// owned by SceneConfig and stays out of the profile.
struct SceneGlassProfile
{
    glm::vec3 tint{0.8f, 0.95f, 0.9f};
    glm::vec3 reflection{0.09f, 0.29f, 0.88f};
    float absorption = 0.6f;
    float thicknessScale = 1.0f;
    float refract = 0.01f;
    float ior = 1.52f;
    float bubbleScale = 12.0f;
    float bubbleIntensity = 0.0f;
    float bubbleThicknessGate = 0.1f;
    float bubbleChromaticSplit = 0.0f;
    float iridescentStrength = 0.0f;
    float iridescentFilmThickness = 1.5f;
    float iridescentFrequency = 2.0f;
    bool voxelGlassRefractEnabled = true;
    float voxelGlassAbsorption = 0.14f;
    float voxelGlassRefractStrength = 0.018f;
    float voxelGlassIor = 1.5f;
    float voxelGlassReflectStrength = 0.24f;
    glm::vec3 voxelGlassTint{0.97f, 0.99f, 1.00f};
    glm::vec3 voxelGlassReflectionColor{0.3f, 0.5f, 0.8f};

    bool operator==(const SceneGlassProfile&) const = default;
};

struct ScenePresentationProfile
{
    // v0: original look contract through water/glass.
    // v1: voxel-cell variation, hemisphere ambient, and scene atmosphere.
    // v2: selectable authored-world or projected-screen-radius ao distance.
    // v3: profile-owned, engine-wide soft-edge taa reconstruction strength.
    // v4: painted-voxel palette families and matte/cavity surface controls.
    // v5: texture-free painted sky gradient, horizon, disc, and halo controls.
    // v6: fixed-budget, wind-001-driven painted cloud layer controls.
    static constexpr uint32_t kCurrentVersion = 6;

    uint32_t version = kCurrentVersion;
    std::string name = "legacy-v0";
    SceneLightingProfile lighting{};
    SceneShadowProfile shadows{};
    SceneAmbientOcclusionProfile ambientOcclusion{};
    ScenePostFxProfile postFx{};
    SceneVoxelSurfaceProfile voxelSurface{};
    SceneVoxelCellVariationProfile voxelCellVariation{};
    SceneHemisphereAmbientProfile hemisphereAmbient{};
    SceneAtmosphereProfile atmosphere{};
    ScenePaintedSkyProfile paintedSky{};
    ScenePaintedCloudProfile paintedClouds{};
    SceneWaterProfile water{};
    SceneGlassProfile glass{};

    bool operator==(const ScenePresentationProfile&) const = default;
};

enum class ScenePresentationProfileSource : uint8_t
{
    SchemaDefaults,
    NamedBase,
    SerializedScene,
    LiveEdit
};

struct ScenePresentationProfileLayers
{
    ScenePresentationProfile schemaDefaults{};
    std::optional<ScenePresentationProfile> namedBase{};
    std::optional<ScenePresentationProfile> serializedScene{};
    std::optional<ScenePresentationProfile> liveEdit{};
};

struct ResolvedScenePresentationProfile
{
    ScenePresentationProfile profile{};
    ScenePresentationProfileSource source = ScenePresentationProfileSource::SchemaDefaults;
};

ResolvedScenePresentationProfile
resolveScenePresentationProfile(const ScenePresentationProfileLayers& layers);

std::string_view scenePresentationProfileSourceName(ScenePresentationProfileSource source);

bool effectiveTaaJitterEnabled(bool taaEnabled, bool jitterEnabled,
                               bool debugDisableJitter, bool freezeDebug);

float maxEffectiveDdaSunAngularRadius(int sampleCount);
float effectiveDdaSunAngularRadius(float requestedRadius, int sampleCount);

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
    const engine::render::GlassSettings& glass);

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
    engine::render::GlassSettings& glass);

} // namespace engine::scene
