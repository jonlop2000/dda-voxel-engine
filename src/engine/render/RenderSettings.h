#pragma once

#include <vector>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include "Resources/LightsBuffer.h"
#include "engine/render/AmbientOcclusionQualityTier.h"
#include "engine/render/AuxiliaryRayResolution.h"
#include "engine/render/HemisphereAmbient.h"
#include "engine/render/PaintedClouds.h"
#include "engine/render/PaintedSky.h"
#include "engine/render/RenderResolution.h"
#include "engine/render/SceneAtmosphere.h"
#include "engine/voxel/VoxelCellVariation.h"

namespace engine::render
{

struct EditableAreaLight
{
    LightShape shape = LightShape::Point;
    bool castsShadows = true;
    glm::vec3 position{0.0f, 8.0f, 0.0f};
    float influenceRadius = 24.0f;
    glm::vec3 color{1.0f, 1.0f, 1.0f};
    float intensity = 3.0f;
    float sourceRadius = 0.0f;
    glm::vec3 edge1{2.0f, 0.0f, 0.0f};
    glm::vec3 edge2{0.0f, 0.0f, 2.0f};
    glm::vec3 discNormal{0.0f, -1.0f, 0.0f};
    glm::vec3 capsuleEndA{-2.0f, 8.0f, 0.0f};
    glm::vec3 capsuleEndB{2.0f, 8.0f, 0.0f};
};

struct LightingSettings
{
    bool pointLightsEnabled_ = true;
    int lightCount_ = 32;
    bool lightHeatmap_ = false;
    bool cascadeDebug_ = false;
    int lightingDebugMode_ = 0;
    std::vector<EditableAreaLight> areaLights_{};
    float sunElevation_ = 55.0f;
    float sunAzimuth_ = 135.0f;
    glm::vec3 sunColor_{1.0f, 0.95f, 0.85f};
    float sunIntensity_ = 2.5f;
    PaintedSkySettings paintedSky_{};
    PaintedCloudSettings paintedClouds_{};
    HemisphereAmbientSettings hemisphereAmbient_{};
    SceneAtmosphereSettings sceneAtmosphere_{};
};

struct AmbientOcclusionSettings
{
    bool aoEnabled_ = true;
    bool adaptiveRayResolution_ = kAdaptiveAuxiliaryRayResolutionProductDefault;
    float aoRayResolutionScale_ = 1.0f;
    AmbientOcclusionDistanceMode aoDistanceMode_ =
        AmbientOcclusionDistanceMode::AuthoredWorldDistance;
    float aoProjectedRadiusPixels_ = 512.0f;
    float aoProjectedMinDistance_ = 2.0f;
    float aoMaxDistance_ = 8.0f;
    float aoStepSize_ = 0.15f;
    float aoIntensity_ = 1.5f;
    float aoContribution_ = 0.25f;
    float aoBias_ = 0.05f;
    int aoRayCount_ = 2;
    float aoBlendAlpha_ = 0.08f;
    float aoDepthReject_ = 0.02f;
    float aoNormalRejectDot_ = 0.85f;
    bool aoResetHistory_ = true;
};

struct PostFxSettings
{
    RenderResolutionSettings renderResolution_{};
    bool tonemapEnabled_ = true;
    float exposure_ = 1.0f;
    float highlightRecovery_ = 0.0f;
    bool bloomEnabled_ = true;
    float bloomThreshold_ = 1.2f;
    float bloomKnee_ = 0.5f;
    float bloomIntensity_ = 0.08f;
    float bloomSigma_ = 2.0f;
    float vignetteStrength_ = 0.12f;
    float grainStrength_ = 0.0f;
    bool colorGradeEnabled_ = false;
    float colorGradeStrength_ = 0.0f;
    float colorGradeSaturation_ = 1.0f;
    float colorGradeContrast_ = 1.0f;
    float colorGradeTemperature_ = 0.0f;
    bool postPixelizationEnabled_ = false;
    float postPixelizationBlockSize_ = 4.0f;
    float postPixelizationStrength_ = 0.70f;
    float postPixelizationEdgeFocus_ = 0.45f;
    float postMaterialDetailStrength_ = 0.0f;
    int postDebugMode_ = 0;
    bool dofEnabled_ = false;
    float dofFocusDistance_ = 10.0f;
    float dofFocusRange_ = 6.0f;
    float dofBlurStrength_ = 0.85f;
    bool taaEnabled_ = false;
    bool jitterEnabled_ = false;
    float taaSimilarityThreshold_ = 0.1f;
    float taaVelocityScale_ = 10.0f;
    float taaBlendMin_ = 0.05f;
    float taaBlendMax_ = 0.20f;
    float taaSharpen_ = 0.25f;
    float taaDepthEdgeThreshold_ = 0.1f;
    float taaCrossFrameDepthThreshold_ = 0.02f;
    float taaColorVarianceThreshold_ = 0.15f;
    float taaSoftEdgeStrength_ = 0.0f;
    bool fxaaEnabled_ = true;
    int taaDebugMode_ = 0;
    bool taaResetHistory_ = true;
};

struct ShadowSettings
{
    bool csmEnabled_ = true;
    bool useDDAShadows_ = false;
    bool adaptiveRayResolution_ = kAdaptiveAuxiliaryRayResolutionProductDefault;
    float ddaRayResolutionScale_ = 1.0f;
    float sunAngularRadius_ = 0.02f;
    float maxShadowDist_ = 100.0f;
    float shadowNormalBias_ = 0.05f;
    int maxShadowSteps_ = 128;
    int shadowDdaSunSampleCount_ = 4;
    float foliageShadowOpacity_ = 1.25f;
    int shadowDebugMode_ = 0;
    float shadowBlendAlpha_ = 0.1f;
    float shadowDepthReject_ = 0.001f;
    float shadowNormalRejectDot_ = 0.92f;
    float shadowClampSharpness_ = 0.85f;
    int shadowSpatialFilterRadius_ = 1;
    float shadowSpatialDepthSigma_ = 0.003f;
    float shadowSpatialValueSigma_ = 0.25f;
    float shadowSpatialNormalPower_ = 48.0f;
    int shadowPostDenoiseRadius_ = 1;
    float shadowPostDenoiseDepthSigma_ = 0.004f;
    float shadowPostDenoiseValueSigma_ = 0.18f;
    float shadowPostDenoiseNormalPower_ = 48.0f;
    bool shadowResetHistory_ = true;
    bool localLightShadowsEnabled_ = true;
    int localShadowCastingLightCount_ = 4;
    float localShadowBlendAlpha_ = 0.05f;
    float localShadowDepthReject_ = 0.01f;
    bool localShadowResetHistory_ = true;
    bool localShadowBlurEnabled_ = true;
    float localShadowBlurSigma_ = 1.5f;
    float terminatorSoftness_ = 0.15f;
    int terminatorMode_ = 1;
    bool csmDitherEnabled_ = true;
};

// runtime look-development controls for the bounded sunroof living-water layer.
// these controls never increase the authored maximum particle/object budgets.
struct SunroofLivingWaterVfxSettings
{
    bool enabled = true;
    float rayIntensity = 1.0f;
    float raySoftness = 1.0f;
    float rayWarmth = 1.0f;
    float illuminatedMoteIntensity = 1.0f;
    bool bubbleStreamsEnabled = true;
    float bubbleAmount = 1.0f;
    float bubbleSize = 1.0f;
    float bubbleRiseSpeed = 1.0f;
    float bubbleDrift = 1.0f;
    bool heroFoliageMotionEnabled = true;
    float heroFoliageSwayStrength = 1.0f;
    float heroFoliageMotionSpeed = 1.0f;
};

struct WaterSettings
{
    bool waterEnabled_ = true;
    bool useWaterV2_ = false;
    bool waterStylizedMode_ = true;
    float waterLevel_ = 10.0f;
    float waterAbsorption_ = 0.04f;
    float waterRefract_ = 0.03f;
    float waterDepthScale_ = 4.0f;
    float waterShallowBias_ = 0.5f;
    float waterDepthToMeters_ = 8.0f;
    float waterDebugDepthScale_ = 10.0f;
    float waterSpecIntensity_ = 1.6f;
    float waterSpecPower_ = 128.0f;
    float waterWaveScale_ = 0.09f;
    float waterWaveAmp_ = 0.72f;
    bool waterCrestHighlightsEnabled_ = true;
    float waterCrestThreshold_ = 0.62f;
    float waterCrestSoftness_ = 0.08f;
    float waterCrestIntensity_ = 0.38f;
    float waterFoamDepthThreshold_ = 0.8f;
    float waterFoamOpacity_ = 0.6f;
    float waterFoamScale_ = 0.2f;
    float waterDistortionDepthScale_ = 0.3f;
    float waterEdgeFadeDepth_ = 0.5f;
    float waterBandHardness_ = 0.6f;
    float waterReflectionStrength_ = 0.28f;
    float waterFresnelBias_ = 0.03f;
    bool waterCausticsEnabled_ = false;
    float waterCausticsIntensity_ = 0.52f;
    float waterCausticsScale_ = 0.12f;
    float waterCausticsSpeed_ = 0.28f;
    float waterCausticsBanding_ = 0.34f;
    float waterCausticsDepthFade_ = 18.0f;
    bool waterParticlesPlanned_ = true;
    float waterParticlesPlannedDensity_ = 0.35f;
    float waterParticlesPlannedDrift_ = 0.20f;
    float waterParticlesPlannedScale_ = 0.15f;
    bool waterFoamEmitterEnabled_ = false;
    float waterFoamEmitterIntensity_ = 0.65f;
    float waterFoamEmitterRadius_ = 3.0f;
    float waterFoamEmitterOffsetX_ = 0.0f;
    float waterFoamEmitterOffsetZ_ = 0.0f;
    float waterFoamEmitterScale_ = 0.25f;
    float waterFoamEmitterSpread_ = 0.35f;
    float waterGradientStrength_ = 0.25f;
    bool waterPlanarReflectionEnabled_ = false;
    bool waterPlanarObliqueClipEnabled_ = false;
    float waterPlanarStrength_ = 0.30f;
    int waterDebugMode_ = 0;
    SunroofLivingWaterVfxSettings sunroofLivingWaterVfx_{};
};

struct GlassSettings
{
    bool glassEnabled_ = false;
    glm::vec3 glassTint_{0.8f, 0.95f, 0.9f};
    glm::vec3 glassReflection_{0.09f, 0.29f, 0.88f};
    float glassAbsorption_ = 0.6f;
    float glassThicknessScale_ = 1.0f;
    float glassRefract_ = 0.01f;
    float glassIor_ = 1.52f;
    float glassThicknessDebugScale_ = 0.5f;
    float glassRefractDebugScale_ = 0.02f;
    int glassDebugMode_ = 0;
    float glassBubbleScale_ = 12.0f;
    float glassBubbleIntensity_ = 0.0f;
    float glassBubbleThicknessGate_ = 0.1f;
    float glassBubbleChromaticSplit_ = 0.0f;
    float glassIridescentStrength_ = 0.0f;
    float glassIridescentFilmThickness_ = 1.5f;
    float glassIridescentFrequency_ = 2.0f;
    bool voxelGlassRefractEnabled_ = true;
    float voxelGlassAbsorption_ = 0.14f;
    float voxelGlassRefractStrength_ = 0.018f;
    float voxelGlassIOR_ = 1.5f;
    float voxelGlassReflectStrength_ = 0.24f;
    glm::vec3 voxelGlassTint_{0.97f, 0.99f, 1.00f};
    glm::vec3 voxelGlassReflectionColor_{0.3f, 0.5f, 0.8f};
    int voxelGlassDebugMode_ = 0;
};

struct VoxelDebugSettings
{
    bool voxelVisible_ = true;
    bool chunkBoundsVisible_ = false;
    bool volumeBoundsVisible_ = false;
    bool axolotlRigDebugVisible_ = false;
    bool proceduralFishEnabled_ = true;
    int proceduralFishCount_ = 6;
    bool axolotlEnabled_ = true;
    int voxelDdaDebugMode_ = 0;
    int voxelDdaAdvancedDebug_ = 0;
    bool voxelDdaSkipEnabled_ = true;
    bool voxelAlignedLayerTraversalEnabled_ = true;
    int voxelDdaSkipMip_ = 3;
    int voxelHeatmapMode_ = 0;
    float voxelHeatmapMax_ = 128.0f;
    float voxelHeatmapGamma_ = 1.0f;
    bool voxelMeshingFrozen_ = false;
    float voxelNormalEdgeSmoothing_ = 0.12f;
    float voxelPixelEdgeShadowStrength_ = 0.35f;
    float voxelCavityStrength_ = 1.0f;
    float voxelPaintedMaterialStrength_ = 0.0f;
    engine::VoxelCellVariationSettings voxelCellVariation_{};
    bool voxelFreezeDebug_ = false;
    bool voxelFreezeCamera_ = false;
    bool voxelFreezeTime_ = false;
    bool voxelDisableJitter_ = false;
    bool voxelEditMode_ = false;
};

// debug visualization, profiling, and pixel-inspect state. the lastInspect*
// fields are readback results written by the engine and read by the editor.
struct DiagnosticsSettings
{
    int viewMode_ = 0;
    int edgeFlickerDebugMode_ = 0;
    bool gpuProfilerEnabled_ = false;
    bool shadowRayAuditEnabled_ = false;
    bool pixelInspectEnabled_ = false;
    glm::ivec2 inspectPixel_{-1, -1};
    glm::vec3 lastInspectNormal_{0.0f};
    glm::vec3 lastInspectWorldPos_{0.0f};
    float lastInspectDepth_ = 1.0f;
    float lastInspectNdotL_ = 0.0f;
    bool lastInspectHasWorld_ = false;
    bool voxelMetricsEnabled_ = false;
    int voxelMetricsSampleStride_ = 4;
    int voxelMetricsHistoryLength_ = 120;
    bool day16ShadowDebugVerified_ = false;
    bool day16AoDebugVerified_ = false;
    bool day16DepthRejectMaskVerified_ = false;
    bool day16AoStableRayCount2_ = false;
    bool day16GpuBaselineRecorded_ = false;
    bool day16ParamsTuned_ = false;
    bool day16DeadCodeChecked_ = false;
    bool day16NextStepChosen_ = false;
};

struct FramePacingSettings
{
    float maxFpsLimit_ = 60.0f;
    bool focusedIdleThrottleEnabled_ = true;
    float focusedIdleFpsLimit_ = 15.0f;
    float focusedIdleDelaySeconds_ = 0.35f;
    bool backgroundThrottleEnabled_ = true;
    float backgroundFpsLimit_ = 30.0f;
};

} // namespace engine::render
