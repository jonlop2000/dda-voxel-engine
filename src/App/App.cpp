#include "App/App.h"
#include "App/AppRenderHelpers.h"
#include "App/AppSceneGlass.h"
#include "App/AppSceneVolume.h"
#include "App/ScenePresentationProfileRuntime.h"

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cfloat>
#include <cmath>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
#include <system_error>
#include <thread>
#include <mutex>
#include <optional>
#include <utility>
#include <vector>

#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <GLFW/glfw3.h>
#if VOXEL_WITH_EDITOR
#include <imgui.h>
#endif

#include "App/GlassPresets.h"
#include "Water/WaterPresets.h"
#include "engine/render/Utils.h"
#include "engine/render/Frustum.h"
#include "Resources/Mesh.h"
#include "Resources/GpuBuffer.h"
#include "Resources/GpuImage.h"
#include "Resources/ShaderModule.h"
#include "Assets/MeshLoaderOBJ.h"
#include "Core/Logger.h"
#include "Core/RuntimeContentManifest.h"
#if VOXEL_WITH_EDITOR
#include "UI/Editor.h"
#include "UI/EngineFacade.h"
#include "UI/Panels/DebugInfoPanel.h"
#include "UI/Panels/ProfilerOverlay.h"
#endif
#include "UI/Runtime/UiDebugFont.h"
#include "UI/Runtime/BuildPlacement.h"
#include "UI/Runtime/UiScreens.h"
#include "UI/Runtime/UiTheme.h"
#include "UI/Runtime/UiTween.h"
#include "engine/voxel/Raycast.h"
#include "engine/voxel/VoxelMath.h"
#include "engine/voxel/VoxelTypes.h"
#include "engine/game/AxolotlCreaturePolish.h"
#include "engine/game/AxolotlModel.h"
#include "engine/game/FishHabitatResolver.h"
#include "engine/game/PlaceableTransform.h"
#include "engine/voxel/VoxelBuilder.h"
#include "engine/voxel/VoxelPalette.h"
#include "engine/voxel/VoxelSystem.h"
#include "engine/render/FrameCharacterization.h"
#include "engine/render/FrameInputs.h"
#include "engine/render/voxel/VoxelRenderResources.h"
#include "engine/scene/SceneManager.h"
#include "engine/scene/WorldStateView.h"
#include "engine/scene/SceneObjectBuilder.h"
#include "engine/scene/AquariumScene.h"
#include "engine/scene/DenseSkipProbeScene.h"
#include "engine/game/FoliageCatalog.h"
#include "engine/scene/StarScene.h"
#include "engine/scene/ScenePresentationProfile.h"
#include "engine/scene/SceneSerializer.h"
#include "engine/scene/GlassTestScene.h"
#include "engine/scene/ObbTestScene.h"
#include "engine/scene/NaturePondGlassDome.h"
#include "engine/scene/NaturePondScene.h"
#include "engine/scene/LightingParityProbe.h"
#include "engine/scene/ProceduralWorldScene.h"
#include "engine/scene/TankGlassBuilder.h"
#include "engine/scene/VoxScene.h"
#include "engine/scene/VoxelImportScene.h"
namespace
{
static constexpr uint32_t kMaxLights = 64;
static constexpr uint32_t kMaxMaterials = 64;
static constexpr float kMouseSensitivity = 0.002f;
static constexpr float kMoveSpeed = 5.0f;
static constexpr float kSprintMultiplier = 3.0f;
static constexpr float kPitchLimit = 1.55f;
static constexpr float kRuntimeUiPlacementProbeDistance = 64.0f;
static constexpr float kRuntimeUiPlacementFallbackDistance = 8.0f;
static constexpr float kRuntimeUiPlacementFallbackPlaneY = 0.0f;
static constexpr float kVoxelRenderDistance = 500.0f;
static constexpr float kTau = 6.28318530718f;
static constexpr uint32_t kMaxMeshJobsPerFrame = 2;
static constexpr VkDeviceSize kPixelInspectNormalOffset = 0;
static constexpr VkDeviceSize kPixelInspectDepthOffset = 16;
static constexpr VkDeviceSize kPixelInspectBufferSize = 32;
static constexpr VkDeviceSize kRuntimeUiPixelInspectBufferSize = 4;

const char* runtimeUiSkyPresetLabel(int preset)
{
    switch (preset)
    {
    case 0:
        return "DAY";
    case 1:
        return "SUNSET";
    case 2:
        return "NIGHT";
    case 3:
        return "DAWN";
    default:
        return "CUSTOM";
    }
}

glm::ivec3 voxelFromPointFloor(const glm::vec3& point)
{
    return glm::ivec3(static_cast<int>(std::floor(point.x)),
                      static_cast<int>(std::floor(point.y)),
                      static_cast<int>(std::floor(point.z)));
}

#if VOXEL_WITH_RUNTIME_UI
glm::mat4 runtimeUiPlacementSurfaceHighlightModel(
    const ui::BuildPlacementProbe& probe)
{
    constexpr float kThickness = 0.08f;

    const glm::ivec3 footprint = glm::max(probe.footprintVoxels, glm::ivec3(1));
    const glm::ivec3 normal = glm::clamp(probe.hitNormal, glm::ivec3(-1),
                                         glm::ivec3(1));
    glm::vec3 origin(probe.placementVoxel);
    glm::vec3 scale(footprint);

    if (normal.x != 0)
    {
        const float plane =
            normal.x > 0 ? static_cast<float>(probe.placementVoxel.x)
                         : static_cast<float>(probe.placementVoxel.x + footprint.x);
        origin.x = plane - kThickness * 0.5f;
        scale.x = kThickness;
    }
    else if (normal.y != 0)
    {
        const float plane =
            normal.y > 0 ? static_cast<float>(probe.placementVoxel.y)
                         : static_cast<float>(probe.placementVoxel.y + footprint.y);
        origin.y = plane - kThickness * 0.5f;
        scale.y = kThickness;
    }
    else if (normal.z != 0)
    {
        const float plane =
            normal.z > 0 ? static_cast<float>(probe.placementVoxel.z)
                         : static_cast<float>(probe.placementVoxel.z + footprint.z);
        origin.z = plane - kThickness * 0.5f;
        scale.z = kThickness;
    }

    return glm::translate(glm::mat4(1.0f), origin) *
           glm::scale(glm::mat4(1.0f), scale);
}

void applyRuntimeUiVoxelPlacementProbe(
    ui::BuildPlacementProbe& probe,
    const engine::voxel::PlacementProbeResult& voxelProbe)
{
    probe.rayHit = voxelProbe.rayHit;
    probe.hasPlacementCandidate = voxelProbe.hasPlacementCandidate;
    probe.snappedToGrid = voxelProbe.snappedToGrid;
    probe.hitVoxel = voxelProbe.hitVoxel;
    probe.hitNormal = voxelProbe.hitNormal;
    probe.placementVoxel = voxelProbe.placementVoxel;
    probe.ghostWorldPosition = voxelProbe.ghostWorldPosition;
    probe.distance = voxelProbe.distance;
}
#endif

std::string runtimePlaceablePrototypeLabel(
    const ui::BuildCatalogItemDefinition& item)
{
    if (!item.hasPlaceablePrototype())
    {
        return "none";
    }
    return std::string(item.placeablePrototypeSlug) + "@" +
           std::to_string(item.placeablePrototypeVersion);
}

std::string runtimePlaceablePrototypeLabel(const PlaceableInstance& instance)
{
    return instance.prototypeSlug + "@" +
           std::to_string(instance.prototypeVersion);
}

glm::vec3 runtimePlaceablePositionFromProbe(
    const ui::BuildPlacementProbe& probe)
{
    glm::vec3 position = probe.ghostWorldPosition;
    position.y = static_cast<float>(probe.placementVoxel.y);
    return position;
}

bool runtimePlaceableFootprintOverlapsExisting(
    const PlaceableInstance& candidate,
    const FoliagePrototype& candidatePrototype,
    const std::vector<PlaceableInstance>& existingInstances)
{
    const float candidateRadius =
        std::max(0.0f, candidatePrototype.placeable.placement.footprintRadius);
    for (const PlaceableInstance& existing : existingInstances)
    {
        const FoliagePrototype* existingPrototype =
            FoliageCatalog::findPrototype(existing.prototypeSlug,
                                          existing.prototypeVersion);
        const float existingRadius =
            existingPrototype != nullptr
                ? std::max(0.0f,
                           existingPrototype->placeable.placement.footprintRadius)
                : candidateRadius;
        const float minDistance = candidateRadius + existingRadius;
        const float dx = candidate.position.x - existing.position.x;
        const float dz = candidate.position.z - existing.position.z;
        if (dx * dx + dz * dz < minDistance * minDistance)
        {
            return true;
        }
    }
    return false;
}

bool findRuntimePlaceableByUuid(const SceneConfig& sceneConfig,
                                const std::string& uuid,
                                PlaceableInstance& outInstance)
{
    if (!sceneConfig.loadAquariumTest || uuid.empty())
    {
        return false;
    }

    const std::vector<PlaceableInstance> instances =
        FoliageCatalog::aquariumHeroFoliageInstances(&sceneConfig.placeables,
                                                     sceneConfig.useDefaultPlaceables);
    const auto found =
        std::find_if(instances.begin(), instances.end(),
                     [&](const PlaceableInstance& instance) {
                         return instance.uuid == uuid;
                     });
    if (found == instances.end())
    {
        return false;
    }

    outInstance = *found;
    return true;
}

bool findRuntimePlaceableOnRay(const SceneConfig& sceneConfig,
                               const glm::vec3& rayOrigin,
                               const glm::vec3& rayDirection,
                               float maxDistance,
                               PlaceableInstance& outInstance,
                               float& outDistance)
{
    if (!sceneConfig.loadAquariumTest || maxDistance <= 0.0f)
    {
        return false;
    }

    const float rayLength = glm::length(rayDirection);
    if (rayLength <= 1e-5f)
    {
        return false;
    }
    const glm::vec3 direction = rayDirection / rayLength;
    const glm::vec2 directionXz(direction.x, direction.z);
    const float directionXzLengthSq = glm::dot(directionXz, directionXz);
    if (directionXzLengthSq <= 1e-5f)
    {
        return false;
    }

    const std::vector<PlaceableInstance> instances =
        FoliageCatalog::aquariumHeroFoliageInstances(&sceneConfig.placeables,
                                                     sceneConfig.useDefaultPlaceables);

    bool found = false;
    float bestDistance = std::numeric_limits<float>::max();
    PlaceableInstance best{};
    const glm::vec2 originXz(rayOrigin.x, rayOrigin.z);
    for (const PlaceableInstance& instance : instances)
    {
        const FoliagePrototype* prototype =
            FoliageCatalog::findPrototype(instance.prototypeSlug,
                                          instance.prototypeVersion);
        const float radius =
            prototype != nullptr
                ? prototype->placeable.placement.footprintRadius
                : 1.5f;
        const float selectRadius = std::max(radius, 1.0f);
        const glm::vec2 instanceXz(instance.position.x, instance.position.z);
        const float distanceAlongRay =
            glm::dot(instanceXz - originXz, directionXz) / directionXzLengthSq;
        if (distanceAlongRay < 0.0f || distanceAlongRay > maxDistance)
        {
            continue;
        }

        const glm::vec2 closestXz =
            originXz + directionXz * distanceAlongRay;
        const glm::vec2 delta = closestXz - instanceXz;
        if (glm::dot(delta, delta) > selectRadius * selectRadius)
        {
            continue;
        }

        if (distanceAlongRay < bestDistance)
        {
            bestDistance = distanceAlongRay;
            best = instance;
            found = true;
        }
    }

    if (!found)
    {
        return false;
    }

    outInstance = best;
    outDistance = bestDistance;
    return true;
}

bool isDirectViewAquariumScene(const SceneConfig& sceneConfig)
{
    return sceneConfig.name == "aquarium_test" ||
           sceneConfig.name == "aquarium_test_probe" ||
           sceneConfig.name == "aquarium_test_perf_probe" || isSunroofAquariumScene(sceneConfig) ||
           isFishbowlAquariumScene(sceneConfig);
}

bool isBeachSandScene(const SceneConfig& sceneConfig)
{
    return sceneConfig.loadProceduralWorld &&
           (sceneConfig.name == "beach_sand_palettes" ||
            sceneConfig.name == "beach_sand_perf_probe");
}

bool shouldIncludeAquariumVoxelWater(const SceneConfig& sceneConfig,
                                     bool automationDisableAquariumWater)
{
    return sceneConfig.loadAquariumTest && !automationDisableAquariumWater &&
           !sceneConfig.useWaterV2;
}

void applySceneLightingDefaults(App& app, const SceneConfig& sceneConfig)
{
    if (sceneConfig.skyPreset >= 0 && sceneConfig.skyPreset <= 3)
    {
        app.applySkyPreset(sceneConfig.skyPreset);
        app.skyColor_ = sceneConfig.skyColor;
        app.shadowSettings_.sunAngularRadius_ = 0.02f;
    }
    else
    {
        app.skyPreset_ = sceneConfig.skyPreset;
        app.skyColor_ = sceneConfig.skyColor;
        app.shadowSettings_.sunAngularRadius_ = 0.02f;
        app.updateSunDirection();
    }

    if (isSunroofAquariumScene(sceneConfig))
    {
        app.skyPreset_ = 4;
        app.skyColor_ = glm::vec3(0.76f, 0.80f, 0.86f);
        app.lightingSettings_.sunElevation_ = 68.0f;
        app.lightingSettings_.sunAzimuth_ = 148.0f;
        app.lightingSettings_.sunColor_ = glm::vec3(1.0f, 0.98f, 0.93f);
        app.lightingSettings_.sunIntensity_ = 4.5f;
        app.shadowSettings_.csmEnabled_ = true;
        app.shadowSettings_.useDDAShadows_ = true;
        app.shadowSettings_.sunAngularRadius_ = 0.035f;
        app.shadowSettings_.shadowDdaSunSampleCount_ = 6;
        app.shadowSettings_.shadowBlendAlpha_ = 0.06f;
        app.shadowSettings_.shadowDepthReject_ = 0.0025f;
        app.shadowSettings_.shadowNormalRejectDot_ = 0.95f;
        app.shadowSettings_.shadowClampSharpness_ = 0.75f;
        app.shadowSettings_.shadowSpatialFilterRadius_ = 1;
        app.shadowSettings_.shadowSpatialDepthSigma_ = 0.0035f;
        app.shadowSettings_.shadowSpatialValueSigma_ = 0.20f;
        app.shadowSettings_.shadowSpatialNormalPower_ = 64.0f;
        app.shadowSettings_.shadowPostDenoiseRadius_ = 1;
        app.shadowSettings_.shadowPostDenoiseDepthSigma_ = 0.0045f;
        app.shadowSettings_.shadowPostDenoiseValueSigma_ = 0.16f;
        app.shadowSettings_.shadowPostDenoiseNormalPower_ = 48.0f;
        app.updateSunDirection();
    }
    else if (isFishbowlAquariumScene(sceneConfig))
    {
        app.skyPreset_ = 4;
        app.skyColor_ = glm::vec3(0.52f, 0.64f, 0.74f);
        app.lightingSettings_.sunElevation_ = 48.0f;
        app.lightingSettings_.sunAzimuth_ = 132.0f;
        app.lightingSettings_.sunColor_ = glm::vec3(1.0f, 0.95f, 0.86f);
        app.lightingSettings_.sunIntensity_ = 3.0f;
        app.shadowSettings_.csmEnabled_ = true;
        app.shadowSettings_.useDDAShadows_ = true;
        app.shadowSettings_.sunAngularRadius_ = 0.028f;
        app.shadowSettings_.shadowDdaSunSampleCount_ = 3;
        app.shadowSettings_.shadowBlendAlpha_ = 0.07f;
        app.shadowSettings_.shadowDepthReject_ = 0.0030f;
        app.shadowSettings_.shadowNormalRejectDot_ = 0.94f;
        app.shadowSettings_.shadowSpatialFilterRadius_ = 1;
        app.shadowSettings_.shadowSpatialDepthSigma_ = 0.0040f;
        app.shadowSettings_.shadowSpatialValueSigma_ = 0.18f;
        app.shadowSettings_.shadowPostDenoiseRadius_ = 1;
        app.shadowSettings_.shadowPostDenoiseDepthSigma_ = 0.0050f;
        app.shadowSettings_.shadowPostDenoiseValueSigma_ = 0.18f;
        app.updateSunDirection();
    }
    else if (isBeachSandScene(sceneConfig))
    {
        app.skyPreset_ = 4;
        app.skyColor_ = glm::vec3(0.13f, 0.42f, 0.76f);
        app.lightingSettings_.sunElevation_ = 42.0f;
        app.lightingSettings_.sunAzimuth_ = 122.0f;
        app.lightingSettings_.sunColor_ = glm::vec3(1.0f, 0.94f, 0.82f);
        app.lightingSettings_.sunIntensity_ = 3.4f;
        app.shadowSettings_.csmEnabled_ = true;
        app.shadowSettings_.useDDAShadows_ = true;
        app.shadowSettings_.sunAngularRadius_ = 0.032f;
        app.shadowSettings_.shadowDdaSunSampleCount_ = 2;
        app.shadowSettings_.shadowBlendAlpha_ = 0.07f;
        app.updateSunDirection();
    }
}

void applySceneWaterDefaults(App& app, const SceneConfig& sceneConfig)
{
    app.waterSettings_.waterStylizedMode_ = true;
    app.waterSettings_.waterWaveScale_ = 0.09f;
    app.waterSettings_.waterWaveAmp_ = 0.72f;
    app.waterSettings_.waterSpecIntensity_ = 1.6f;
    app.waterSettings_.waterSpecPower_ = 128.0f;
    app.waterSettings_.waterReflectionStrength_ = 0.28f;
    app.waterSettings_.waterCausticsEnabled_ = false;
    app.waterSettings_.waterCausticsIntensity_ = 0.52f;
    app.waterSettings_.waterCausticsScale_ = 0.12f;
    app.waterSettings_.waterCausticsSpeed_ = 0.28f;
    app.waterSettings_.waterCausticsBanding_ = 0.34f;
    app.waterSettings_.waterCausticsDepthFade_ = 18.0f;
    app.waterSettings_.waterGradientStrength_ = 0.25f;
    app.waterSettings_.waterPlanarReflectionEnabled_ = false;
    app.waterSettings_.waterParticlesPlanned_ = true;
    app.waterSettings_.waterParticlesPlannedDensity_ = 0.35f;
    app.waterSettings_.waterParticlesPlannedDrift_ = 0.20f;
    app.waterSettings_.waterParticlesPlannedScale_ = 0.15f;
    app.waterSettings_.waterFoamEmitterEnabled_ =
        sceneConfig.name == "aquarium_test" || sceneConfig.name == "aquarium_test_probe";
    app.waterSettings_.waterFoamEmitterIntensity_ = 0.62f;
    app.waterSettings_.waterFoamEmitterRadius_ = 3.0f;
    app.waterSettings_.waterFoamEmitterOffsetX_ = 0.0f;
    app.waterSettings_.waterFoamEmitterOffsetZ_ = 0.0f;
    app.waterSettings_.waterFoamEmitterScale_ = 0.24f;
    app.waterSettings_.waterFoamEmitterSpread_ = 0.34f;

    if (isDirectViewAquariumScene(sceneConfig))
    {
        // sparse voxel-like water motes are useful for vfx lookdev, but in the direct-view
        // aquarium scenes they read as black stipple artifacts on underwater voxel faces.
        app.waterSettings_.waterParticlesPlanned_ = false;
        app.waterSettings_.waterCausticsEnabled_ = false;
        app.waterSettings_.waterCausticsIntensity_ = 0.52f;
        app.waterSettings_.waterCausticsScale_ = 0.12f;
        app.waterSettings_.waterCausticsSpeed_ = 0.28f;
        app.waterSettings_.waterCausticsBanding_ = 0.34f;
        app.waterSettings_.waterCausticsDepthFade_ = 18.0f;
    }

    if (isFishbowlAquariumScene(sceneConfig))
    {
        app.waterSettings_.waterStylizedMode_ = false;
        app.waterSettings_.waterRefract_ = 0.014f;
        app.waterSettings_.waterFresnelBias_ = 0.018f;
        app.waterSettings_.waterDistortionDepthScale_ = 0.14f;
        app.waterSettings_.waterWaveScale_ = 0.052f;
        app.waterSettings_.waterWaveAmp_ = 0.18f;
        app.waterSettings_.waterSpecIntensity_ = 1.9f;
        app.waterSettings_.waterSpecPower_ = 180.0f;
        app.waterSettings_.waterReflectionStrength_ = 0.20f;
        app.waterSettings_.waterGradientStrength_ = 0.035f;
        app.waterSettings_.waterParticlesPlanned_ = false;
        app.waterSettings_.waterFoamEmitterEnabled_ = false;
    }
    else if (isSunroofAquariumScene(sceneConfig))
    {
        app.waterSettings_.waterStylizedMode_ = false;
        app.waterSettings_.waterRefract_ = 0.018f;
        app.waterSettings_.waterFresnelBias_ = 0.015f;
        app.waterSettings_.waterDistortionDepthScale_ = 0.18f;
        app.waterSettings_.waterWaveScale_ = 0.032f;
        app.waterSettings_.waterWaveAmp_ = 0.16f;
        app.waterSettings_.waterSpecIntensity_ = 1.45f;
        app.waterSettings_.waterSpecPower_ = 160.0f;
        app.waterSettings_.waterReflectionStrength_ = 0.10f;
        app.waterSettings_.waterCausticsIntensity_ = 0.46f;
        app.waterSettings_.waterCausticsScale_ = 0.09f;
        app.waterSettings_.waterCausticsSpeed_ = 0.22f;
        app.waterSettings_.waterCausticsBanding_ = 0.24f;
        app.waterSettings_.waterCausticsDepthFade_ = 24.0f;
        app.waterSettings_.waterGradientStrength_ = 0.02f;
        app.waterSettings_.waterParticlesPlanned_ = false;
        app.waterSettings_.waterParticlesPlannedDensity_ = 0.18f;
        app.waterSettings_.waterParticlesPlannedDrift_ = 0.16f;
        app.waterSettings_.waterParticlesPlannedScale_ = 0.12f;
        app.waterSettings_.waterFoamEmitterEnabled_ = false;
        app.waterSettings_.waterFoamEmitterIntensity_ = 0.45f;
        app.waterSettings_.waterFoamEmitterRadius_ = 4.0f;
        app.waterSettings_.waterFoamEmitterScale_ = 0.18f;
        app.waterSettings_.waterFoamEmitterSpread_ = 0.22f;
    }
    else if (isBeachSandScene(sceneConfig))
    {
        app.waterSettings_.waterStylizedMode_ = false;
        app.waterSettings_.waterRefract_ = 0.022f;
        app.waterSettings_.waterFresnelBias_ = 0.018f;
        app.waterSettings_.waterDistortionDepthScale_ = 0.16f;
        app.waterSettings_.waterWaveScale_ = 0.045f;
        app.waterSettings_.waterWaveAmp_ = 0.28f;
        app.waterSettings_.waterSpecIntensity_ = 1.75f;
        app.waterSettings_.waterSpecPower_ = 150.0f;
        app.waterSettings_.waterReflectionStrength_ = 0.18f;
        app.waterSettings_.waterCausticsIntensity_ = 1.45f;
        app.waterSettings_.waterCausticsScale_ = 0.105f;
        app.waterSettings_.waterCausticsSpeed_ = 0.35f;
        app.waterSettings_.waterCausticsBanding_ = 0.26f;
        app.waterSettings_.waterCausticsDepthFade_ = 22.0f;
        app.waterSettings_.waterGradientStrength_ = 0.05f;
        app.waterSettings_.waterParticlesPlanned_ = false;
        app.waterSettings_.waterFoamEmitterEnabled_ = false;
        app.waterSettings_.waterFoamDepthThreshold_ = 0.55f;
        app.waterSettings_.waterFoamOpacity_ = 0.42f;
        app.waterSettings_.waterFoamScale_ = 0.17f;
    }
    else if (isNaturePondScene(sceneConfig))
    {
        app.waterSettings_.waterStylizedMode_ = false;
        app.waterSettings_.waterRefract_ = 0.014f;
        app.waterSettings_.waterFresnelBias_ = 0.018f;
        app.waterSettings_.waterDistortionDepthScale_ = 0.12f;
        app.waterSettings_.waterWaveScale_ = 0.075f;
        app.waterSettings_.waterWaveAmp_ = 0.16f;
        app.waterSettings_.waterSpecIntensity_ = 1.45f;
        app.waterSettings_.waterSpecPower_ = 150.0f;
        app.waterSettings_.waterReflectionStrength_ = 0.16f;
        app.waterSettings_.waterGradientStrength_ = 0.045f;
        app.waterSettings_.waterParticlesPlanned_ = false;
        app.waterSettings_.waterFoamEmitterEnabled_ = false;
    }
}

void applySceneGlassDefaults(App& app, const SceneConfig& sceneConfig)
{
    app.glassSettings_.glassTint_ = glm::vec3(0.8f, 0.95f, 0.9f);
    app.glassSettings_.glassReflection_ = glm::vec3(0.09f, 0.29f, 0.88f);
    app.glassSettings_.glassAbsorption_ = 0.6f;
    app.glassSettings_.glassThicknessScale_ = 1.0f;
    app.glassSettings_.glassRefract_ = 0.01f;
    app.glassSettings_.glassIor_ = 1.52f;
    app.glassSettings_.glassBubbleScale_ = 12.0f;
    app.glassSettings_.glassBubbleIntensity_ = 0.0f;
    app.glassSettings_.glassBubbleThicknessGate_ = 0.1f;
    app.glassSettings_.glassBubbleChromaticSplit_ = 0.0f;
    app.glassSettings_.glassIridescentStrength_ = 0.0f;
    app.glassSettings_.glassIridescentFilmThickness_ = 1.5f;
    app.glassSettings_.glassIridescentFrequency_ = 2.0f;

    if (isFishbowlAquariumScene(sceneConfig))
    {
        applyGlassMaterialPreset(app.glassSettings_, kRoundFishbowlGlassPreset);
    }
    else if (isSunroofAquariumScene(sceneConfig))
    {
        app.glassSettings_.glassTint_ = glm::vec3(1.0f, 0.98f, 0.92f);
        app.glassSettings_.glassReflection_ = glm::vec3(0.95f, 0.94f, 0.90f);
        app.glassSettings_.glassAbsorption_ = 0.08f;
        app.glassSettings_.glassThicknessScale_ = 0.45f;
        app.glassSettings_.glassRefract_ = 0.0025f;
        app.glassSettings_.glassIor_ = 1.03f;
    }
}

void applyScenePostDefaults(App& app, const SceneConfig& sceneConfig)
{
    app.postFxSettings_.highlightRecovery_ = 0.0f;
    app.postFxSettings_.colorGradeEnabled_ = false;
    app.postFxSettings_.colorGradeStrength_ = 0.0f;
    app.postFxSettings_.colorGradeSaturation_ = 1.0f;
    app.postFxSettings_.colorGradeContrast_ = 1.0f;
    app.postFxSettings_.colorGradeTemperature_ = 0.0f;
    app.postFxSettings_.postPixelizationEnabled_ = false;
    app.postFxSettings_.postPixelizationBlockSize_ = 4.0f;
    app.postFxSettings_.postPixelizationStrength_ = 0.70f;
    app.postFxSettings_.postPixelizationEdgeFocus_ = 0.45f;
    app.postFxSettings_.postMaterialDetailStrength_ = 0.0f;

    if (isDirectViewAquariumScene(sceneConfig))
    {
        app.postFxSettings_.highlightRecovery_ = isSunroofAquariumScene(sceneConfig) ? 0.16f : 0.25f;
        app.postFxSettings_.colorGradeEnabled_ = true;
        app.postFxSettings_.colorGradeStrength_ = isSunroofAquariumScene(sceneConfig) ? 0.22f : 0.35f;
        app.postFxSettings_.colorGradeSaturation_ = 1.06f;
        app.postFxSettings_.colorGradeContrast_ = isSunroofAquariumScene(sceneConfig) ? 1.03f : 1.05f;
        app.postFxSettings_.colorGradeTemperature_ = isSunroofAquariumScene(sceneConfig) ? -0.02f : -0.04f;
        app.postFxSettings_.postMaterialDetailStrength_ = 0.55f;
    }

    if (isFishbowlAquariumScene(sceneConfig))
    {
        app.postFxSettings_.highlightRecovery_ = 0.18f;
        app.postFxSettings_.colorGradeStrength_ = 0.28f;
        app.postFxSettings_.colorGradeSaturation_ = 1.08f;
        app.postFxSettings_.colorGradeContrast_ = 1.04f;
        app.postFxSettings_.colorGradeTemperature_ = -0.015f;
        app.postFxSettings_.postMaterialDetailStrength_ = 0.50f;
    }
}

float halfToFloat(uint16_t h)
{
    const uint32_t sign = (static_cast<uint32_t>(h) & 0x8000u) << 16;
    const uint32_t exp = (static_cast<uint32_t>(h) & 0x7C00u) >> 10;
    const uint32_t mant = static_cast<uint32_t>(h) & 0x03FFu;

    uint32_t outExp = 0;
    uint32_t outMant = 0;

    if (exp == 0)
    {
        if (mant != 0)
        {
            int shift = 0;
            uint32_t m = mant;
            while ((m & 0x0400u) == 0u)
            {
                m <<= 1;
                ++shift;
            }
            m &= 0x03FFu;
            outExp = static_cast<uint32_t>(127 - 15 - shift);
            outMant = m << 13;
        }
    }
    else if (exp == 0x1Fu)
    {
        outExp = 0xFFu;
        outMant = mant << 13;
    }
    else
    {
        outExp = static_cast<uint32_t>(exp - 15 + 127);
        outMant = mant << 13;
    }

    const uint32_t bits = sign | (outExp << 23) | outMant;
    float out = 0.0f;
    std::memcpy(&out, &bits, sizeof(out));
    return out;
}
bool InitVoxelPaletteTest(VulkanContext& ctx, engine::VoxelPalette& palette)
{
    if (!palette.create(ctx, 1, "MainPalette"))
    {
        logError("Renderer", "Palette create failed.");
        return false;
    }

    palette.buildDefaultPalette0();

    if (!palette.upload(ctx))
    {
        logError("Renderer", "Palette upload failed.");
        palette.destroy(ctx);
        return false;
    }

    std::vector<engine::PaletteEntryCPU> readback;
    if (!palette.download(ctx, readback))
    {
        logError("Renderer", "Palette download failed.");
        palette.destroy(ctx);
        return false;
    }

    const auto& e1 = readback[0 * 256 + 1];
    if (e1.baseColor_alpha.a <= 0.0f)
    {
        logError("Renderer", "Palette entry seems invalid.");
        palette.destroy(ctx);
        return false;
    }

    logInfo("Renderer", std::string("Palette test OK (entry1 rgb=") +
                            std::to_string(e1.baseColor_alpha.r) + "," +
                            std::to_string(e1.baseColor_alpha.g) + "," +
                            std::to_string(e1.baseColor_alpha.b) + ").");
    return true;
}

std::string getArgValue(int argc, char** argv, const char* name)
{
    for (int i = 1; i + 1 < argc; i++)
    {
        if (std::strcmp(argv[i], name) == 0)
        {
            return argv[i + 1];
        }
    }
    return {};
}

bool hasArg(int argc, char** argv, const char* name)
{
    for (int i = 1; i < argc; i++)
    {
        if (std::strcmp(argv[i], name) == 0)
        {
            return true;
        }
    }
    return false;
}

std::optional<uint64_t> parseNonNegativeFrameCount(const std::string& value)
{
    if (value.empty() || value.front() == '-')
    {
        return std::nullopt;
    }

    try
    {
        size_t consumed = 0;
        const unsigned long long parsed = std::stoull(value, &consumed);
        if (consumed != value.size())
        {
            return std::nullopt;
        }
        return static_cast<uint64_t>(parsed);
    }
    catch (const std::exception&)
    {
        return std::nullopt;
    }
}

std::string normalizedArg(std::string value)
{
    for (char& c : value)
    {
        if (c == '_')
        {
            c = '-';
        }
        else
        {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
    }
    return value;
}

bool parseAppModeArg(const std::string& value, AppMode& outMode)
{
    const std::string normalized = normalizedArg(value);
    if (normalized == "editor")
    {
        outMode = AppMode::Editor;
        return true;
    }
    if (normalized == "play-preview" || normalized == "play")
    {
        outMode = AppMode::PlayPreview;
        return true;
    }
    if (normalized == "game" || normalized == "runtime")
    {
        outMode = AppMode::Game;
        return true;
    }
    return false;
}

bool parseWindowSizeArg(const std::string& value, int& outWidth, int& outHeight)
{
    const size_t separator = value.find_first_of("xX");
    if (separator == std::string::npos)
    {
        return false;
    }

    try
    {
        size_t widthEnd = 0;
        size_t heightEnd = 0;
        const int width = std::stoi(value.substr(0, separator), &widthEnd);
        const int height = std::stoi(value.substr(separator + 1), &heightEnd);
        if (widthEnd != separator || heightEnd != value.size() - separator - 1 || width <= 0 ||
            height <= 0)
        {
            return false;
        }

        outWidth = width;
        outHeight = height;
        return true;
    }
    catch (const std::exception&)
    {
        return false;
    }
}

std::filesystem::path findAncestorContentRoot(const std::filesystem::path& startDir,
                                              const char* childName)
{
    namespace fs = std::filesystem;

    std::error_code ec;
    fs::path current = startDir;
    while (!current.empty())
    {
        const fs::path candidate = (current / childName).lexically_normal();
        if (fs::exists(candidate, ec) && fs::is_directory(candidate, ec))
        {
            return candidate;
        }

        const fs::path parent = current.parent_path();
        if (parent.empty() || parent == current)
        {
            break;
        }
        current = parent;
    }

    return {};
}

bool tryGetPresetCamera(const std::string& sceneName, glm::vec3& outPos, float& outYaw, float& outPitch)
{
    SceneConfig cfg{};
    if (sceneName == "empty")
    {
        cfg = SceneConfig::empty();
    }
    else if (sceneName == "obb_test")
    {
        cfg = SceneConfig::obbTest();
    }
    else if (sceneName == "voxel_world")
    {
        cfg = SceneConfig::voxelWorld();
    }
    else if (sceneName == "procedural_world")
    {
        cfg = SceneConfig::proceduralWorld();
    }
    else if (sceneName == "voxel_import")
    {
        cfg = SceneConfig::voxelImport();
    }
    else if (sceneName == "full_demo")
    {
        cfg = SceneConfig::fullDemo();
    }
    else if (sceneName == "aquarium_test")
    {
        cfg = SceneConfig::aquariumTest();
    }
    else if (sceneName == "glass_focus")
    {
        cfg = SceneConfig::glassFocus();
    }
    else
    {
        return false;
    }

    outPos = cfg.cameraPosition;
    outYaw = cfg.cameraYaw;
    outPitch = cfg.cameraPitch;
    return true;
}

std::filesystem::path resolveAssetRoot(const char* argv0, int argc, char** argv)
{
    namespace fs = std::filesystem;

    const std::string overridePath = getArgValue(argc, argv, "--assets");
    if (!overridePath.empty())
    {
        return fs::absolute(overridePath).lexically_normal();
    }

    const fs::path cwdAssets = findAncestorContentRoot(fs::current_path(), "assets");
    if (!cwdAssets.empty())
    {
        return cwdAssets;
    }

    const fs::path exePath = fs::absolute(argv0 == nullptr ? "" : argv0);
    const fs::path exeDir = exePath.parent_path();
    const fs::path exeAssets = findAncestorContentRoot(exeDir, "assets");
    if (!exeAssets.empty())
    {
        return exeAssets;
    }

    return (fs::current_path() / "assets").lexically_normal();
}

std::filesystem::path resolveScenesRoot(const char* argv0, int argc, char** argv)
{
    namespace fs = std::filesystem;

    const std::string overridePath = getArgValue(argc, argv, "--scenes");
    if (!overridePath.empty())
    {
        return fs::absolute(overridePath).lexically_normal();
    }

    const fs::path cwdScenes = findAncestorContentRoot(fs::current_path(), "scenes");
    if (!cwdScenes.empty())
    {
        return cwdScenes;
    }

    const fs::path exePath = fs::absolute(argv0 == nullptr ? "" : argv0);
    const fs::path exeDir = exePath.parent_path();
    const fs::path exeScenes = findAncestorContentRoot(exeDir, "scenes");
    if (!exeScenes.empty())
    {
        return exeScenes;
    }

    return (fs::current_path() / "scenes").lexically_normal();
}

std::filesystem::path resolveLogFilePath(const char* argv0)
{
    namespace fs = std::filesystem;

    fs::path baseDir = fs::current_path();
    if (argv0 != nullptr && argv0[0] != '\0')
    {
        fs::path exePath = fs::absolute(argv0);
        if (exePath.has_parent_path())
        {
            baseDir = exePath.parent_path();
        }
    }

    return baseDir / "logs" / "voxel_aquarium.log";
}

std::filesystem::path findFirstFileWithExtension(const std::filesystem::path& dir,
                                                 const std::string& ext)
{
    namespace fs = std::filesystem;
    if (!fs::exists(dir) || !fs::is_directory(dir))
    {
        return {};
    }

    std::string target = ext;
    std::transform(target.begin(), target.end(), target.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    for (const auto& entry : fs::directory_iterator(dir))
    {
        if (!entry.is_regular_file())
        {
            continue;
        }
        std::string entryExt = entry.path().extension().string();
        std::transform(entryExt.begin(), entryExt.end(), entryExt.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (entryExt == target)
        {
            return entry.path();
        }
    }
    return {};
}

uint32_t hashU32(uint32_t v)
{
    v ^= v >> 16;
    v *= 0x7feb352d;
    v ^= v >> 15;
    v *= 0x846ca68b;
    v ^= v >> 16;
    return v;
}

const char* samplePatternLabel(int pattern)
{
    switch (pattern)
    {
    case 1:
        return "Cycling";
    case 2:
        return "Random";
    default:
        return "Fixed";
    }
}


engine::WaterVolume makeDefaultWaterVolume(const engine::VoxelWorld& world, float waterLevel)
{
    engine::WaterVolume water{};
    const auto& instances = world.instances();
    if (instances.empty())
    {
        water.boundsMin = glm::vec3(-50.0f, -10.0f, -50.0f);
        water.boundsMax = glm::vec3(50.0f, 20.0f, 50.0f);
        water.surfaceHeight = waterLevel;
        return water;
    }

    glm::vec3 worldMin(FLT_MAX);
    glm::vec3 worldMax(-FLT_MAX);
    bool foundBounds = false;

    // prefer bounds of explicitly water-flagged volumes when available.
    for (const auto& inst : instances)
    {
        if ((inst.volume.flags() & engine::VoxelVolume::FLAG_WATER) == 0u)
        {
            continue;
        }

        worldMin = glm::min(worldMin, inst.volume.worldAabbMin());
        worldMax = glm::max(worldMax, inst.volume.worldAabbMax());
        foundBounds = true;
    }

    // fallback: whole scene bounds if there are no water-flagged volumes.
    if (!foundBounds)
    {
        for (const auto& inst : instances)
        {
            worldMin = glm::min(worldMin, inst.volume.worldAabbMin());
            worldMax = glm::max(worldMax, inst.volume.worldAabbMax());
            foundBounds = true;
        }
    }

    if (!foundBounds)
    {
        water.boundsMin = glm::vec3(-50.0f, -10.0f, -50.0f);
        water.boundsMax = glm::vec3(50.0f, 20.0f, 50.0f);
        water.surfaceHeight = waterLevel;
        return water;
    }

    const glm::vec3 pad(0.5f);
    water.boundsMin = worldMin - pad;
    water.boundsMax = worldMax + pad;
    water.surfaceHeight = waterLevel;
    return water;
}

engine::WaterVolume makeAquariumWaterVolume(const engine::VoxelWorld& world, float waterLevel)
{
    // start with a robust fallback in case aquarium water bounds are unavailable.
    engine::WaterVolume water = makeDefaultWaterVolume(world, waterLevel);
    glm::vec3 waterMin(0.0f);
    glm::vec3 waterMax(0.0f);
    if (AquariumScene::getWaterBounds(waterMin, waterMax))
    {
        const glm::vec3 pad(0.02f);
        water.boundsMin = waterMin - pad;
        water.boundsMax = waterMax + pad;
    }

    water.surfaceHeight = waterLevel;
    water.absorptionCoeff = glm::vec3(0.45f, 0.08f, 0.04f);
    water.deepColor = glm::vec3(0.02f, 0.12f, 0.22f);
    water.fogDensity = 0.06f;
    return water;
}

engine::WaterVolume makeSceneWaterVolumeImpl(const SceneConfig& sceneConfig,
                                             const engine::VoxelWorld& world,
                                             float waterLevel)
{
    if (isNaturePondScene(sceneConfig))
    {
        engine::WaterVolume water = makeDefaultWaterVolume(world, waterLevel);
        water.boundsMin = NaturePondScene::pondBoundsMin();
        water.boundsMax = NaturePondScene::pondBoundsMax();
        water.surfaceHeight = NaturePondScene::pondSurfaceHeight();
        water.absorptionCoeff = glm::vec3(0.24f, 0.055f, 0.022f);
        water.deepColor = glm::vec3(0.015f, 0.14f, 0.12f);
        water.fogDensity = 0.028f;
        return water;
    }

    if (!sceneConfig.loadAquariumTest)
    {
        engine::WaterVolume water = makeDefaultWaterVolume(world, waterLevel);
        if (isBeachSandScene(sceneConfig))
        {
            water.absorptionCoeff = glm::vec3(0.26f, 0.055f, 0.018f);
            water.deepColor = glm::vec3(0.01f, 0.16f, 0.22f);
            water.fogDensity = 0.035f;
        }
        return water;
    }

    engine::WaterVolume water = makeAquariumWaterVolume(world, waterLevel);
    if (isFishbowlAquariumScene(sceneConfig))
    {
        water.absorptionCoeff = glm::vec3(0.18f, 0.035f, 0.014f);
        water.deepColor = glm::vec3(0.012f, 0.105f, 0.150f);
        water.fogDensity = 0.018f;
        water.shape = engine::waterVolumeShapeValue(engine::WaterVolumeShape::Fishbowl);
    }
    else if (isSunroofAquariumScene(sceneConfig))
    {
        // keep the near field airy while separating the chamber's foliage, fish, and
        // white walls with restrained underwater distance extinction.
        applyWaterPreset(water, kSunroofEnvironmentV2WaterPreset);
    }
    else if (sceneConfig.useWaterV2 && isDirectViewAquariumScene(sceneConfig))
    {
        applyWaterPreset(water, kWaterV2AquariumPreset);
    }
    return water;
}

uint32_t floatToBits(float v)
{
    return std::bit_cast<uint32_t>(v);
}

glm::vec3 safeNormalizeOr(glm::vec3 v, const glm::vec3& fallback)
{
    const float len2 = glm::dot(v, v);
    if (len2 <= 1e-10f)
    {
        return fallback;
    }
    return v * glm::inversesqrt(len2);
}

glm::vec3 transformPoint(const glm::mat4& transform, const glm::vec3& point)
{
    return glm::vec3(transform * glm::vec4(point, 1.0f));
}

glm::mat4 centeredBoxModel(const glm::vec3& center, const glm::vec3& size)
{
    return glm::translate(glm::mat4(1.0f), center) *
           glm::scale(glm::mat4(1.0f), size);
}

bool centeredBoneModel(const glm::vec3& start, const glm::vec3& end,
                       float thickness, glm::mat4& outModel)
{
    const glm::vec3 delta = end - start;
    const float length = glm::length(delta);
    if (length <= 1e-4f)
    {
        return false;
    }

    const glm::vec3 xAxis = delta / length;
    const glm::vec3 up =
        std::abs(xAxis.y) < 0.92f ? glm::vec3(0.0f, 1.0f, 0.0f)
                                  : glm::vec3(0.0f, 0.0f, 1.0f);
    const glm::vec3 zAxis = safeNormalizeOr(glm::cross(xAxis, up),
                                            glm::vec3(0.0f, 0.0f, 1.0f));
    const glm::vec3 yAxis = safeNormalizeOr(glm::cross(zAxis, xAxis),
                                            glm::vec3(0.0f, 1.0f, 0.0f));

    glm::mat4 basis(1.0f);
    basis[0] = glm::vec4(xAxis, 0.0f);
    basis[1] = glm::vec4(yAxis, 0.0f);
    basis[2] = glm::vec4(zAxis, 0.0f);

    outModel = glm::translate(glm::mat4(1.0f), (start + end) * 0.5f) *
               basis *
               glm::scale(glm::mat4(1.0f),
                          glm::vec3(length, thickness, thickness));
    return true;
}

float clamp01(float value)
{
    return std::clamp(value, 0.0f, 1.0f);
}

float smoothstep01(float value)
{
    const float t = clamp01(value);
    return t * t * (3.0f - 2.0f * t);
}

struct FishPaletteBandIds
{
    std::array<uint8_t, 4> body{};
    std::array<uint8_t, 2> accent{};
    std::array<uint8_t, 2> fin{};
    uint8_t eye = AquariumMaterial::FishEye;
};

const FishPaletteBandIds& fishPaletteBandIds(size_t paletteIndex)
{
    static const std::array<FishPaletteBandIds, 3> kPalettes = {
        FishPaletteBandIds{{AquariumMaterial::FishSunset::BodyBase + 0,
                            AquariumMaterial::FishSunset::BodyBase + 1,
                            AquariumMaterial::FishSunset::BodyBase + 2,
                            AquariumMaterial::FishSunset::BodyBase + 3},
                           {AquariumMaterial::FishSunset::AccentBase + 0,
                            AquariumMaterial::FishSunset::AccentBase + 1},
                           {AquariumMaterial::FishSunset::FinBase + 0,
                            AquariumMaterial::FishSunset::FinBase + 1},
                           AquariumMaterial::FishEye},
        FishPaletteBandIds{{AquariumMaterial::FishPearl::BodyBase + 0,
                            AquariumMaterial::FishPearl::BodyBase + 1,
                            AquariumMaterial::FishPearl::BodyBase + 2,
                            AquariumMaterial::FishPearl::BodyBase + 3},
                           {AquariumMaterial::FishPearl::AccentBase + 0,
                            AquariumMaterial::FishPearl::AccentBase + 1},
                           {AquariumMaterial::FishPearl::FinBase + 0,
                            AquariumMaterial::FishPearl::FinBase + 1},
                           AquariumMaterial::FishEye},
        FishPaletteBandIds{{AquariumMaterial::FishReef::BodyBase + 0,
                            AquariumMaterial::FishReef::BodyBase + 1,
                            AquariumMaterial::FishReef::BodyBase + 2,
                            AquariumMaterial::FishReef::BodyBase + 3},
                           {AquariumMaterial::FishReef::AccentBase + 0,
                            AquariumMaterial::FishReef::AccentBase + 1},
                           {AquariumMaterial::FishReef::FinBase + 0,
                            AquariumMaterial::FishReef::FinBase + 1},
                           AquariumMaterial::FishEye},
    };
    return kPalettes[paletteIndex % kPalettes.size()];
}

constexpr glm::ivec3 kFishBodyVolumeDims{16, 10, 10};
constexpr glm::ivec3 kFishTailVolumeDims{8, 10, 6};
constexpr glm::vec3 kFishBodyAnchorLocal{9.0f, 5.0f, 5.0f};
constexpr glm::vec3 kFishTailAnchorLocal{7.0f, 5.0f, 3.0f};
constexpr glm::vec3 kFishTailRootFromBodyAnchorLocal{-6.5f, 0.0f, 0.0f};
constexpr int kMaxProceduralFishCount = 50;

struct AxolotlLimbRig
{
    glm::vec3 attachLocal;
    int sideSign = 1; // -1 = negative z side, +1 = positive z side
    float phaseOffset = 0.0f;
};

const std::array<AxolotlLimbRig, engine::game::kAxolotlLimbCount> kAxolotlLimbRig{{
    {glm::vec3(10.25f, 2.65f, 2.55f), -1, 0.0f},
    {glm::vec3(10.25f, 2.65f, 7.45f), 1, 3.14159265f},
    {glm::vec3(3.45f, 2.55f, 2.75f), -1, 3.14159265f},
    {glm::vec3(3.45f, 2.55f, 7.25f), 1, 0.0f},
}};

constexpr glm::vec3 kAxolotlLimbDebugPawLocal{3.35f, 1.15f, 1.5f};
constexpr glm::vec3 kAxolotlTailDebugTipLocal{0.5f, 5.0f, 3.0f};

std::string fishBodyVolumeName(size_t fishIndex)
{
    return "ProceduralFishBody" + std::to_string(fishIndex);
}

std::string fishTailVolumeName(size_t fishIndex)
{
    return "ProceduralFishTail" + std::to_string(fishIndex);
}

std::string heroFoliageVolumeName(size_t foliageIndex)
{
    return FoliageCatalog::heroFoliageVolumeName(foliageIndex);
}

std::string axolotlBodyVolumeName()
{
    return "ProceduralAxolotlBody0";
}

std::string axolotlTailVolumeName()
{
    return "ProceduralAxolotlTail0";
}

std::string axolotlLimbVolumeName(size_t limbIndex)
{
    return "ProceduralAxolotlLimb" + std::to_string(limbIndex);
}

engine::game::AxolotlPaletteBandIds axolotlPaletteBandIds()
{
    return engine::game::AxolotlPaletteBandIds{
        {AquariumMaterial::AxolotlPink::BodyBase + 0,
         AquariumMaterial::AxolotlPink::BodyBase + 1,
         AquariumMaterial::AxolotlPink::BodyBase + 2,
         AquariumMaterial::AxolotlPink::BodyBase + 3},
        {AquariumMaterial::AxolotlPink::GillBase + 0,
         AquariumMaterial::AxolotlPink::GillBase + 1},
        {AquariumMaterial::AxolotlPink::FinBase + 0,
         AquariumMaterial::AxolotlPink::FinBase + 1},
        {AquariumMaterial::AxolotlPink::DetailBase + 0,
         AquariumMaterial::AxolotlPink::DetailBase + 1,
         AquariumMaterial::AxolotlPink::DetailBase + 2,
         AquariumMaterial::AxolotlPink::DetailBase + 3},
        AquariumMaterial::FishEye};
}

int findVoxelInstanceByName(const engine::VoxelWorld& world, const std::string& debugName)
{
    const auto& instances = world.instances();
    for (size_t i = 0; i < instances.size(); ++i)
    {
        if (instances[i].volume.debugName() == debugName)
        {
            return static_cast<int>(i);
        }
    }
    return -1;
}

std::vector<uint8_t> buildFishBodyVoxelData(size_t paletteIndex)
{
    engine::VoxelBuilder builder(kFishBodyVolumeDims);
    const FishPaletteBandIds& palette = fishPaletteBandIds(paletteIndex);
    const glm::vec3 center(0.0f, kFishBodyAnchorLocal.y, kFishBodyAnchorLocal.z);

    for (int x = 0; x < kFishBodyVolumeDims.x; ++x)
    {
        const float t = static_cast<float>(x) /
                        static_cast<float>(std::max(1, kFishBodyVolumeDims.x - 1));
        const float bodyRise = smoothstep01((t - 0.08f) / 0.22f);
        const float noseTaper = 1.0f - 0.88f * smoothstep01((t - 0.76f) / 0.24f);
        const float shoulder = std::sin(clamp01((t - 0.06f) / 0.90f) * 3.14159265f);
        const float yRadius = 0.60f + 2.10f * bodyRise * noseTaper * (0.88f + 0.18f * shoulder);
        const float zRadius = 0.42f + 1.65f * bodyRise * noseTaper * (0.84f + 0.16f * shoulder);
        const float localCenterY =
            center.y + 0.28f * smoothstep01((t - 0.26f) / 0.26f) -
            0.16f * smoothstep01((t - 0.84f) / 0.16f);
        const float nosePinch = 1.0f - 0.35f * smoothstep01((t - 0.84f) / 0.16f);

        for (int y = 0; y < kFishBodyVolumeDims.y; ++y)
        {
            for (int z = 0; z < kFishBodyVolumeDims.z; ++z)
            {
                const float dy = (static_cast<float>(y) + 0.5f - localCenterY) / yRadius;
                const float dz =
                    ((static_cast<float>(z) + 0.5f - center.z) / std::max(0.2f, zRadius)) /
                    nosePinch;
                const float superEllipse =
                    std::pow(std::abs(dy), 1.75f) + std::pow(std::abs(dz), 1.55f);
                if (superEllipse > 1.0f)
                {
                    continue;
                }

                if (t > 0.90f && std::abs(dy) + std::abs(dz) > 0.92f)
                {
                    continue;
                }

                int bodyShade = 1;
                if (dy > 0.36f)
                {
                    bodyShade = 0;
                }
                else if (dy < -0.24f)
                {
                    bodyShade = 3;
                }
                else if (t > 0.54f)
                {
                    bodyShade = 2;
                }
                uint8_t id = palette.body[bodyShade];

                const bool accentStripe =
                    t > 0.20f && t < 0.88f && std::abs(dz) > 0.46f && std::abs(dz) < 0.90f &&
                    dy > -0.16f && dy < 0.24f;
                if (accentStripe)
                {
                    id = palette.accent[t > 0.60f ? 1 : 0];
                }
                else if (t > 0.84f && std::abs(dz) > 0.38f && dy > -0.08f && dy < 0.16f)
                {
                    id = palette.accent[0];
                }
                else if (t < 0.16f && std::abs(dz) > 0.26f && dy > -0.08f && dy < 0.24f)
                {
                    id = palette.accent[1];
                }

                builder.setVoxel(x, y, z, id);
            }
        }
    }

    for (int x = 5; x <= 10; ++x)
    {
        const float finT = static_cast<float>(x - 5) / 5.0f;
        const int finHeight =
            2 + static_cast<int>(std::round((1.0f - std::abs(finT - 0.45f) * 2.0f) * 2.5f));
        const int yStart = kFishBodyVolumeDims.y - 3;
        for (int y = yStart; y < std::min(kFishBodyVolumeDims.y, yStart + finHeight); ++y)
        {
            for (int z = 4; z <= 5; ++z)
            {
                builder.setVoxel(x, y, z, palette.fin[(x + y) & 1]);
            }
        }
    }

    for (int x = 8; x <= 10; ++x)
    {
        builder.setVoxel(x, 4, 1, palette.fin[(x + 0) & 1]);
        builder.setVoxel(x, 3, 1, palette.fin[(x + 1) & 1]);
        builder.setVoxel(x, 4, 8, palette.fin[(x + 1) & 1]);
        builder.setVoxel(x, 3, 8, palette.fin[(x + 0) & 1]);
    }

    for (int x = 6; x <= 8; ++x)
    {
        builder.setVoxel(x, 2, 3, palette.fin[(x + 0) & 1]);
        builder.setVoxel(x, 2, 6, palette.fin[(x + 1) & 1]);
    }

    for (int x = 11; x <= 13; ++x)
    {
        builder.setVoxel(x, 5, 3, palette.accent[1]);
        builder.setVoxel(x, 5, 6, palette.accent[1]);
    }
    builder.setVoxel(12, 5, 2, palette.eye);
    builder.setVoxel(12, 5, 7, palette.eye);
    builder.setVoxel(13, 5, 2, palette.eye);
    builder.setVoxel(13, 5, 7, palette.eye);

    return builder.data();
}

std::vector<uint8_t> buildFishTailVoxelData(size_t paletteIndex)
{
    engine::VoxelBuilder builder(kFishTailVolumeDims);
    const FishPaletteBandIds& palette = fishPaletteBandIds(paletteIndex);
    const glm::vec3 center(0.0f, kFishTailAnchorLocal.y, kFishTailAnchorLocal.z);

    for (int x = 0; x < kFishTailVolumeDims.x; ++x)
    {
        const float tipT = 1.0f - static_cast<float>(x) /
                                      static_cast<float>(std::max(1, kFishTailVolumeDims.x - 1));
        const float lobeCenter = 0.90f + 1.40f * tipT;
        const float lobeRadius = 0.55f + 1.05f * tipT;
        const float zLimit = 0.78f + 0.28f * tipT;

        for (int y = 0; y < kFishTailVolumeDims.y; ++y)
        {
            for (int z = 0; z < kFishTailVolumeDims.z; ++z)
            {
                const float dy = static_cast<float>(y) + 0.5f - center.y;
                const float dz = std::abs(static_cast<float>(z) + 0.5f - center.z);
                const bool upperLobe = std::abs(dy - lobeCenter) <= lobeRadius;
                const bool lowerLobe = std::abs(dy + lobeCenter) <= lobeRadius;
                const bool rootFill = tipT < 0.30f && std::abs(dy) <= 1.15f;
                const bool forkGap = tipT > 0.46f && std::abs(dy) < 0.70f;
                if ((upperLobe || lowerLobe || rootFill) && !forkGap && dz <= zLimit)
                {
                    const bool brightEdge = tipT > 0.62f || std::abs(dy) > 2.2f;
                    builder.setVoxel(x, y, z, palette.fin[brightEdge ? 1 : 0]);
                }
            }
        }
    }

    return builder.data();
}

std::string jsonEscape(const std::string& input)
{
    std::string out;
    out.reserve(input.size());
    for (char c : input)
    {
        switch (c)
        {
        case '\\':
            out += "\\\\";
            break;
        case '"':
            out += "\\\"";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\t':
            out += "\\t";
            break;
        default:
            out += c;
            break;
        }
    }
    return out;
}

std::string formatTimestamp(const char* format)
{
    const auto now = std::chrono::system_clock::now();
    const std::time_t nowT = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &nowT);
#else
    localtime_r(&nowT, &tm);
#endif
    std::ostringstream oss;
    oss << std::put_time(&tm, format);
    return oss.str();
}

void setCameraLookAt(Camera& camera, const glm::vec3& position, const glm::vec3& target)
{
    glm::vec3 forward = target - position;
    if (glm::length(forward) < 0.0001f)
    {
        return;
    }

    forward = glm::normalize(forward);
    camera.position = position;
    camera.pitch = std::asin(std::clamp(forward.y, -1.0f, 1.0f));
    camera.yaw = std::atan2(forward.x, -forward.z);
}

bool projectWorldToRuntimeUi(const glm::mat4& viewProj, const glm::vec3& world,
                             float viewportW, float viewportH, glm::vec2& outScreen)
{
    const glm::vec4 clip = viewProj * glm::vec4(world, 1.0f);
    if (clip.w <= 0.0001f || !std::isfinite(clip.x) || !std::isfinite(clip.y) ||
        !std::isfinite(clip.z) || !std::isfinite(clip.w))
    {
        return false;
    }

    const glm::vec3 ndc = glm::vec3(clip) / clip.w;
    if (ndc.x < -1.0f || ndc.x > 1.0f || ndc.y < -1.0f || ndc.y > 1.0f ||
        ndc.z < 0.0f || ndc.z > 1.0f)
    {
        return false;
    }

    outScreen.x = (ndc.x * 0.5f + 0.5f) * viewportW;
    outScreen.y = (ndc.y * 0.5f + 0.5f) * viewportH;
    return std::isfinite(outScreen.x) && std::isfinite(outScreen.y);
}

} // namespace

bool isAppBeachSandScene(const SceneConfig& sceneConfig)
{
    return isBeachSandScene(sceneConfig);
}

engine::WaterVolume makeSceneWaterVolume(const SceneConfig& sceneConfig,
                                         const engine::VoxelWorld& world,
                                         float waterLevel)
{
    return makeSceneWaterVolumeImpl(sceneConfig, world, waterLevel);
}

int App::run(int argc, char** argv)
{
    init(argc, argv);
    mainLoop();
    shutdown();
    return 0;
}

const char* App::activePresentModeLabel() const
{
    switch (renderer_.swapchainPresentMode())
    {
    case VK_PRESENT_MODE_MAILBOX_KHR:
        return "MAILBOX";
    case VK_PRESENT_MODE_IMMEDIATE_KHR:
        return "IMMEDIATE";
    case VK_PRESENT_MODE_FIFO_KHR:
    default:
        return "FIFO";
    }
}

void App::setPresentMode(SwapPresentMode mode)
{
    if (presentMode_ == mode)
    {
        return;
    }

    presentMode_ = mode;
    swapchainRecreateRequested_ = true;
}

void App::setGpuProfilerEnabled(bool enabled)
{
    diagnosticsSettings_.gpuProfilerEnabled_ = enabled;
    gpuProfiler_.setEnabled(enabled);
    lastGpuFrameMs_ = 0.0f;
    gpuFrameHistoryMs_.clear();
}

void App::enterRuntimeUiPlayPreview(const char* reason, bool startAtMainMenu)
{
#if VOXEL_WITH_EDITOR
    if (appMode_ == AppMode::PlayPreview)
    {
        return;
    }
    if (appMode_ != AppMode::Editor)
    {
        logWarning("AppMode", "Play-preview can only be entered from editor mode.");
        return;
    }
    if (!editorSceneDocument_.replaceAuthoredConfig(sceneConfig()) || !editorSceneDocument_.beginPlaySnapshot()) { logWarning("AppMode", "Play-preview rejected an invalid editor scene document."); return; }
    if (!gameRuntime_.beginIsolatedPlaceableEditSession()) { (void)editorSceneDocument_.endPlayAndRestoreAuthored(); logWarning("AppMode", "Play-preview rejected an active placeable edit session."); return; }
    sceneConfigMutable() = *editorSceneDocument_.playSnapshot();
    editorVisibilityBeforePlayPreview_ = {
        true,
        editorState_.enabled,
        editorState_.leftPanelVisible,
        editorState_.rightPanelVisible,
        editorState_.statusBarVisible,
        editorState_.profilerOverlayVisible,
    };

#if VOXEL_WITH_RUNTIME_UI
    resetRuntimeUiTransientSessionState("enter_play_preview");
#endif
    if (startAtMainMenu)
    {
        runtimeUiContext_.setActiveScreen(ui::kMainMenuScreenId);
        logInfo("RuntimeUI", "Play-preview opened on ui_main_menu.");
    }
    else if (!runtimeUiContext_.hasActiveScreen())
    {
        runtimeUiContext_.setActiveScreen(ui::kOverlaySmokeScreenId);
        logInfo("RuntimeUI",
                "Play-preview defaulted to ui_overlay_smoke because no runtime UI screen was selected.");
    }

    setAppMode(AppMode::PlayPreview, reason);
#else
    (void)reason;
    (void)startAtMainMenu;
    logWarning("AppMode", "Play-preview is unavailable because VOXEL_WITH_EDITOR=OFF.");
#endif
}

void App::exitRuntimeUiPlayPreview(const char* reason)
{
#if VOXEL_WITH_EDITOR
    if (appMode_ != AppMode::PlayPreview)
    {
        return;
    }
    stopRenderPipelineShowcaseFromEditor();
#if VOXEL_WITH_RUNTIME_UI
    deactivateRuntimeUiShowcaseCamera(reason);
    resetRuntimeUiTransientSessionState("exit_play_preview");
#endif
    if (editorSceneDocument_.playSnapshotActive()) { reloadScene(editorSceneDocument_.endPlayAndRestoreAuthored()); if (!gameRuntime_.endIsolatedPlaceableEditSession()) { logWarning("AppMode", "Play-preview could not restore the authored placeable edit session."); } }
    setAppMode(AppMode::Editor, reason);
#else
    (void)reason;
#endif
}
void App::setAppMode(AppMode mode, const char* reason)
{
#if !VOXEL_WITH_EDITOR
    if (mode == AppMode::Editor || mode == AppMode::PlayPreview)
    {
        logWarning("AppMode",
                   std::string("Ignoring app mode '") + InputRouter::label(mode) +
                       "' because this build has VOXEL_WITH_EDITOR=OFF; using game mode.");
        mode = AppMode::Game;
    }
#endif

    appMode_ = mode;
    inputRouter_.setAppMode(appMode_);
    inputRouter_.setRuntimeUiCapture(false, false);
#if VOXEL_WITH_RUNTIME_UI
    inputRouter_.setRuntimeUiModalCapture(runtimeUiModalInputActive());
#else
    inputRouter_.setRuntimeUiModalCapture(false);
#endif
    applyAppModeVisibility(reason);

    logInfo("AppMode",
            std::string("App mode set to ") + InputRouter::label(appMode_) +
                (reason != nullptr && reason[0] != '\0' ? std::string(" (") + reason + ")"
                                                         : std::string()));
}

void App::applyAppModeVisibility(const char* reason)
{
    (void)reason;
#if VOXEL_WITH_EDITOR
    if (appMode_ == AppMode::Editor)
    {
        if (editorVisibilityBeforePlayPreview_.valid)
        {
            editorState_.enabled = editorVisibilityBeforePlayPreview_.enabled;
            editorState_.leftPanelVisible = editorVisibilityBeforePlayPreview_.leftPanelVisible;
            editorState_.rightPanelVisible = editorVisibilityBeforePlayPreview_.rightPanelVisible;
            editorState_.statusBarVisible = editorVisibilityBeforePlayPreview_.statusBarVisible;
            editorState_.profilerOverlayVisible =
                editorVisibilityBeforePlayPreview_.profilerOverlayVisible;
            editorVisibilityBeforePlayPreview_.valid = false;
        }
        return;
    }

    editorState_.enabled = false;
    editorState_.leftPanelVisible = false;
    editorState_.rightPanelVisible = false;
    editorState_.statusBarVisible = false;
    editorState_.profilerOverlayVisible = false;
#endif
}

bool App::shouldDrawEditorUi() const
{
#if VOXEL_WITH_EDITOR
    return appMode_ == AppMode::Editor;
#else
    return false;
#endif
}

void App::refreshInputRouterEditorCapture()
{
#if VOXEL_WITH_EDITOR
    if (appMode_ == AppMode::Editor)
    {
        const ImGuiIO& io = ImGui::GetIO();
        inputRouter_.setEditorCapture(io.WantCaptureMouse, io.WantCaptureKeyboard);
        return;
    }
#endif
    inputRouter_.setEditorCapture(false, false);
}

void App::onInputFramebufferResized()
{
    framebufferResized_ = true;
}

void App::onInputUserInteraction(double timeSeconds)
{
    lastInteractionTime_ = timeSeconds;
}

#if VOXEL_WITH_RUNTIME_UI
bool App::runtimeUiModalInputActive() const
{
    return appMode_ != AppMode::Editor &&
           (runtimeUiContext_.overlayCount() > 0 ||
            (runtimeUiContext_.activeScreenRegistered() &&
             runtimeUiContext_.activeScreenId() != ui::kOverlaySmokeScreenId));
}

void App::resetRuntimeUiTransientSessionState(const char* reason)
{
    const bool controllerChanged =
        runtimeUiOverlaySmokeController_.resetTransientSessionState();
    const bool focusChanged = resetFishFocusForSessionTransition();
    const bool changed = controllerChanged || focusChanged;
    runtimeUiContext_.invalidateActiveTree();
    inputRouter_.setRuntimeUiCapture(false, false);
    inputRouter_.setRuntimeUiModalCapture(false);
    sceneObjectsDirty_ = sceneObjectsDirty_ || changed;
    logInfo("RuntimeUI",
            makeLogMessage("Transient session reset reason=",
                           reason != nullptr && reason[0] != '\0' ? reason : "unspecified",
                           " changed=", changed ? 1 : 0));
}

ui::UiTree* App::ensureRuntimeUiActiveTree()
{
    ui::UiTree* tree = runtimeUiContext_.ensureActiveTree();
    if (tree != nullptr)
    {
        syncRuntimeUiShowcaseCameraForActiveScreen("active_screen");
        bool viewportChanged = false;
        const VkExtent2D viewportExtent = renderer_.swapchainExtent();
        const int viewportW = static_cast<int>(viewportExtent.width);
        const int viewportH = static_cast<int>(viewportExtent.height);
        const bool hasViewport = viewportW > 0 && viewportH > 0;
        if (runtimeUiContext_.activeScreenId() == ui::kOverlaySmokeScreenId)
        {
            if (hasViewport)
            {
                viewportChanged =
                    ui::setOverlaySmokeViewport(*tree, static_cast<float>(viewportW),
                                                static_cast<float>(viewportH));
            }
            const std::vector<FishHandle> fish = fishList();
            const uint64_t focusedFish = focusedFishId();
            {
                std::vector<ui::UiFishListEntry> fishEntries;
                fishEntries.reserve(fish.size());
                for (const FishHandle& handle : fish)
                {
                    ui::UiFishListEntry entry{};
                    entry.id = handle.id;
                    entry.label = (handle.isAxolotl ? "AXOLOTL " : "FISH ") +
                                  std::to_string(handle.index + 1);
                    entry.focused = focusedFish != 0 && focusedFish == handle.id;
                    fishEntries.push_back(std::move(entry));
                }
                runtimeUiOverlaySmokeController_.setFishList(std::move(fishEntries));
            }
            syncRuntimeUiCareHud(
                *tree,
                static_cast<uint32_t>(std::min<size_t>(
                    fish.size(), std::numeric_limits<uint32_t>::max())),
                environmentTimeSample_.active
                    ? engine::scene::environmentTimePhaseLabel(
                          environmentTimeSample_.phase).data()
                    : runtimeUiSkyPresetLabel(skyPreset_));
            if ((runtimeUiFishWorldLabelsVisible_ || focusedFish != 0) &&
                viewportW > 0 && viewportH > 0)
            {
                const float viewportWidth = static_cast<float>(viewportW);
                const float viewportHeight = static_cast<float>(viewportH);
                const float aspect =
                    viewportHeight > 0.0f ? viewportWidth / viewportHeight : 1.0f;
                const glm::mat4 viewProj =
                    camera_.projMatrix(aspect) * camera_.viewMatrix();
                std::vector<ui::UiFishWorldLabelEntry> labelEntries;
                labelEntries.reserve(fish.size());
                for (const FishHandle& handle : fish)
                {
                    const bool isFocused =
                        focusedFish != 0 && focusedFish == handle.id;
                    if (!runtimeUiFishWorldLabelsVisible_ && !isFocused) { continue; }

                    const float labelLift = std::max(1.1f, handle.scale * 2.2f);
                    glm::vec2 screen(0.0f);
                    if (!projectWorldToRuntimeUi(
                            viewProj, handle.worldPos + glm::vec3(0.0f, labelLift, 0.0f),
                            viewportWidth, viewportHeight, screen))
                    {
                        continue;
                    }

                    ui::UiFishWorldLabelEntry entry{};
                    entry.id = handle.id;
                    entry.axolotlVersus = handle.isAxolotl && isFocused;
                    entry.label =
                        entry.axolotlVersus
                            ? gameRuntime_.axolotlBellyFloat().typedVersusBanner()
                            : (handle.isAxolotl ? "AXOLOTL " : "FISH ") +
                                  std::to_string(handle.index + 1);
                    entry.screenX = screen.x;
                    entry.screenY = screen.y;
                    entry.focused = isFocused;
                    labelEntries.push_back(std::move(entry));
                }
                ui::setOverlaySmokeFishWorldLabels(*tree, labelEntries);
            }
            else
            {
                ui::setOverlaySmokeFishWorldLabels(*tree, {});
            }
            runtimeUiOverlaySmokeController_.syncTree(
                *tree, runtimeUiContext_.activeTreeRevision());
            if (runtimeUiOverlaySmokeController_.shouldLogFishList())
            {
                logInfo("RuntimeUI",
                        makeLogMessage("Fish list updated count=",
                                       runtimeUiOverlaySmokeController_.fishListCount()));
                runtimeUiOverlaySmokeController_.markFishListLogged();
            }
            const ui::UiBindingStats bindingStats =
                runtimeUiOverlaySmokeController_.lastBindingStats();
            if (bindingStats.applied > 0)
            {
                logInfo("RuntimeUI",
                        makeLogMessage("Binding sync applied=", bindingStats.applied,
                                       " total=", bindingStats.total));
            }
            if (runtimeUiOverlaySmokeController_.consumeCatalogOpenedTransition())
            {
                const ui::UiTheme& theme = ui::defaultUiTheme();
                ui::UiTweenSet& tweens = runtimeUiContext_.tweens();
                // automation scripts complete tweens in one update so smoke logs and
                // layout needles stay frame-deterministic.
                tweens.setInstant(!automationUiScript_.empty());
                tweens.start(ui::UiTweenSpec{"build_catalog_panel",
                                             ui::UiTweenProperty::Opacity, 0.0f, 1.0f,
                                             theme.fadeInSeconds, ui::UiEase::EaseOutCubic},
                             *tree);
                tweens.start(ui::UiTweenSpec{"build_catalog_panel",
                                             ui::UiTweenProperty::OffsetY, -12.0f, 0.0f,
                                             theme.slideSeconds, ui::UiEase::EaseOutCubic},
                             *tree);
                logInfo("RuntimeUI",
                        makeLogMessage("Build catalog open tween started fade_s=",
                                       theme.fadeInSeconds,
                                       " slide_s=", theme.slideSeconds,
                                       " instant=", tweens.instant() ? 1 : 0));
            }
        }
        else if (runtimeUiContext_.activeScreenId() == ui::kMainMenuScreenId &&
                 hasViewport)
        {
            viewportChanged =
                ui::setMainMenuViewport(*tree, static_cast<float>(viewportW),
                                        static_cast<float>(viewportH)) ||
                viewportChanged;
        }
        else if (runtimeUiContext_.activeScreenId() == ui::kMainMenuOptionsScreenId)
        {
            syncRuntimeUiOptionsState(*tree);
        }
        if (hasViewport)
        {
            for (ui::UiTree* overlayTree : runtimeUiContext_.overlayTrees())
            {
                syncRuntimeUiOptionsState(*overlayTree);
                const bool overlayViewportChanged = ui::setRuntimeUiOverlayViewport(
                    *overlayTree, static_cast<float>(viewportW),
                    static_cast<float>(viewportH));
                if (overlayViewportChanged)
                {
                    ui::computeLayout(overlayTree->root);
                    viewportChanged = true;
                }
            }
        }
        bool tweenMovedLayout = false;
        if (runtimeUiContext_.tweens().anyActive() &&
            runtimeUiTweenFrame_ != renderedFrameCount_)
        {
            runtimeUiTweenFrame_ = renderedFrameCount_;
            tweenMovedLayout = runtimeUiContext_.tweens().hasLayoutAffectingTweens();
            const std::vector<ui::UiTweenCompletion> completions =
                runtimeUiContext_.tweens().update(runtimeUiFrameDtSeconds_, *tree);
            for (const ui::UiTweenCompletion& completion : completions)
            {
                logInfo("RuntimeUI",
                        makeLogMessage("Tween completed element=", completion.elementId,
                                       " property=",
                                       ui::tweenPropertyLabel(completion.property),
                                       " value=", completion.value));
            }
        }

        // layout dirty flags: recompute only when the tree was rebuilt, a
        // binding re-applied state, or an offset tween moved elements. otherwise the
        // previous computedRects stay valid for hit-testing and painting.
        const char* layoutReason = nullptr;
        if (runtimeUiLayoutRevision_ != runtimeUiContext_.activeTreeRevision())
        {
            layoutReason = "tree_rebuilt";
        }
        else if (runtimeUiOverlaySmokeController_.lastBindingStats().applied > 0)
        {
            layoutReason = "binding";
        }
        else if (viewportChanged)
        {
            layoutReason = "viewport";
        }
        else if (tweenMovedLayout)
        {
            layoutReason = "tween";
        }

        if (layoutReason != nullptr)
        {
            ui::computeLayout(tree->root);
            runtimeUiLayoutRevision_ = runtimeUiContext_.activeTreeRevision();
            ++runtimeUiLayoutComputedCount_;
            logInfo("RuntimeUI",
                    makeLogMessage("Layout computed reason=", layoutReason,
                                   " computed=", runtimeUiLayoutComputedCount_,
                                   " skipped=", runtimeUiLayoutSkippedCount_));
        }
        else
        {
            ++runtimeUiLayoutSkippedCount_;
        }
    }
    // screen stack: the top overlay owns input, so every hit-test/click call site
    // becomes modal by routing through the top tree (overlays are laid out at push).
    if (tree != nullptr && runtimeUiContext_.overlayCount() > 0)
    {
        return runtimeUiContext_.topTree();
    }
    return tree;
}

bool App::toggleRuntimeUiPauseScreen()
{
#if VOXEL_WITH_RUNTIME_UI
    if (appMode_ == AppMode::Editor)
    {
        return false;
    }
    if (const std::string topScreen = runtimeUiContext_.topScreenId();
        topScreen == ui::kMainMenuOptionsScreenId ||
        topScreen == ui::kCollectionCodexScreenId)
    {
        runtimeUiContext_.popScreen();
        logInfo("RuntimeUI",
                makeLogMessage("Screen popped id=", topScreen,
                               " depth=", runtimeUiContext_.overlayCount()));
        return true;
    }
    if (runtimeUiContext_.activeScreenId() != ui::kOverlaySmokeScreenId)
    {
        return false;
    }
    if (runtimeUiContext_.topScreenId() == ui::kPauseScreenId)
    {
        runtimeUiContext_.popScreen();
        logInfo("RuntimeUI",
                makeLogMessage("Screen popped id=", ui::kPauseScreenId,
                               " depth=", runtimeUiContext_.overlayCount()));
        return true;
    }
    if (!runtimeUiContext_.pushScreen(ui::kPauseScreenId))
    {
        return false;
    }
    logInfo("RuntimeUI",
            makeLogMessage("Screen pushed id=", ui::kPauseScreenId,
                           " depth=", runtimeUiContext_.overlayCount()));
    return true;
#else
    return false;
#endif
}

ui::UiHitResult App::refreshRuntimeUiPointerCapture(double x, double y)
{
    ui::UiHitResult hit{};
    if (appMode_ != AppMode::Editor)
    {
        if (ui::UiTree* tree = ensureRuntimeUiActiveTree())
        {
            const VkExtent2D extent = renderer_.swapchainExtent();
            const auto point =
                inputController_.framebufferPoint(x, y, extent.width, extent.height);
            hit = ui::hitTest(tree->root, point.x, point.y);
        }
    }

    runtimeUiContext_.updatePointerHover(hit);
    const bool modal = runtimeUiModalInputActive();
    inputRouter_.setRuntimeUiModalCapture(modal);
    inputRouter_.setRuntimeUiCapture(
        modal || hit.hit || !runtimeUiContext_.pointerState().pressedElementId.empty(),
        false);
    return hit;
}

bool App::handleRuntimeUiScroll(double x, double y, double yOffset)
{
    if (appMode_ == AppMode::Editor) { return false; }

    ui::UiTree* tree = ensureRuntimeUiActiveTree();
    if (tree == nullptr) { return false; }

    const VkExtent2D extent = renderer_.swapchainExtent();
    const auto point =
        inputController_.framebufferPoint(x, y, extent.width, extent.height);
    const bool modal = runtimeUiModalInputActive();

    if (runtimeUiContext_.topScreenId() == ui::kCollectionCodexScreenId)
    {
        ui::scrollCollectionCodexAt(*tree, point.x, point.y, yOffset);
        const ui::UiHitResult hit = ui::hitTest(tree->root, point.x, point.y);
        runtimeUiContext_.updatePointerHover(hit);
        inputRouter_.setRuntimeUiModalCapture(true);
        inputRouter_.setRuntimeUiCapture(true, false);
        return true;
    }

    if (modal)
    {
        inputRouter_.setRuntimeUiModalCapture(true);
        inputRouter_.setRuntimeUiCapture(true, false);
        return true;
    }

    if (runtimeUiContext_.activeScreenId() != ui::kOverlaySmokeScreenId)
    {
        return false;
    }

    if (ui::handleOverlaySmokeFishListWheel(
            *tree, point.x, point.y, yOffset))
    {
        ui::computeLayout(tree->root);
        const ui::UiHitResult hit = ui::hitTest(tree->root, point.x, point.y);
        runtimeUiContext_.updatePointerHover(hit);
        inputRouter_.setRuntimeUiCapture(true, false);
        return true;
    }

    const ui::UiElement* viewport =
        ui::findElementById(tree->root, ui::kBuildCatalogListViewportId);
    if (!runtimeUiOverlaySmokeController_.state().buildCatalogOpen ||
        viewport == nullptr || !viewport->visible || !viewport->enabled)
    {
        return false;
    }

    const ui::UiRect& rect = viewport->computedRect;
    const bool inside = rect.width > 0.0f && rect.height > 0.0f &&
                        point.x >= rect.x && point.y >= rect.y &&
                        point.x < rect.x + rect.width &&
                        point.y < rect.y + rect.height;
    if (!inside)
    {
        return false;
    }

    runtimeUiOverlaySmokeController_.scrollBuildCatalog(yOffset);
    runtimeUiOverlaySmokeController_.syncTree(*tree,
                                              runtimeUiContext_.activeTreeRevision());
    ui::computeLayout(tree->root);
    const ui::UiHitResult hit = ui::hitTest(tree->root, point.x, point.y);
    runtimeUiContext_.updatePointerHover(hit);
    inputRouter_.setRuntimeUiCapture(true, false);
    return true;
}

void App::handleRuntimeUiPointerDown(const ui::UiHitResult& hit)
{
    runtimeUiContext_.handlePointerDown(hit);
}

void App::handleRuntimeUiPointerUp(const ui::UiHitResult& hit)
{
    runtimeUiContext_.handlePointerUp(hit);
    dispatchRuntimeUiAction(runtimeUiContext_.consumeActionEvent(), nullptr);
}

bool App::configureRuntimeUiManualPlacementQaCandidate(
    ui::BuildPlacementProbe& probe,
    const glm::vec3& forward)
{
    const bool manualQaAutomation =
        automationUiScript_ == "ui_manual_placement_qa_target_smoke" ||
        automationUiScript_ == "ui_placeable_prototype_ghost_smoke" ||
        automationUiScript_ == "ui_placeable_prototype_commit_smoke" ||
        automationUiScript_ == "ui_placeable_prototype_remove_smoke";
    if ((!automationUiScript_.empty() && !manualQaAutomation) ||
        sceneConfig().loadVoxelWorld)
    {
        return false;
    }

    const engine::voxel::PlacementProbeResult voxelProbe =
        engine::voxel::fallbackPlacementProbe(
            {camera_.position, forward, kRuntimeUiPlacementProbeDistance,
             kRuntimeUiPlacementFallbackDistance,
             kRuntimeUiPlacementFallbackPlaneY, true});
    const glm::ivec3 placementVoxel = voxelProbe.placementVoxel;
    const glm::ivec3 targetChunk(floorDiv(placementVoxel.x, Chunk::SX),
                                 floorDiv(placementVoxel.y, Chunk::SY),
                                 floorDiv(placementVoxel.z, Chunk::SZ));
    if (!engine::voxel::containsChunk(voxelGrid_, targetChunk))
    {
        // recreating chunks invalidates pointers captured by queued meshing jobs.
        jobSystem_.waitIdle();
        const engine::voxel::GridExpansionResult expansion =
            engine::voxel::expandGridToIncludeChunk(voxelGrid_, targetChunk, 1);
        if (expansion.expanded)
        {
            voxelRenderResources_.resetChunks(expansion.chunkCoords);
            voxelObjectsDirty_ = true;
            sceneObjectsDirty_ = true;
        }
    }

    if (const ui::BuildCatalogItemDefinition* item =
            ui::findRuntimeBuildCatalogItem(probe.itemKey))
    {
        if (voxelMaterials_[item->block].set == VK_NULL_HANDLE)
        {
            initializeVoxelWorldMaterials();
        }
    }

    applyRuntimeUiVoxelPlacementProbe(probe, voxelProbe);

    logInfo("RuntimeUI",
            makeLogMessage("Manual placement QA candidate placement_voxel=",
                           placementVoxel.x, ",", placementVoxel.y, ",",
                           placementVoxel.z, " synthetic_surface=1",
                           " hit_normal=", probe.hitNormal.x, ",",
                           probe.hitNormal.y, ",", probe.hitNormal.z,
                           " distance_mm=",
                           static_cast<int>(std::round(voxelProbe.distance * 1000.0f)),
                           " camera_world_mm=",
                           static_cast<int>(
                               std::round(camera_.position.x * 1000.0f)),
                           ",",
                           static_cast<int>(
                               std::round(camera_.position.y * 1000.0f)),
                           ",",
                           static_cast<int>(
                               std::round(camera_.position.z * 1000.0f))));
    return true;
}

void App::updateRuntimeUiPlacementProbe(const glm::vec3& forward)
{
    ui::BuildPlacementProbe probe{};
    if (appMode_ == AppMode::Editor ||
        runtimeUiContext_.activeScreenId() != ui::kOverlaySmokeScreenId ||
        !runtimeUiOverlaySmokeController_.pendingPlacementActive() ||
        !voxelDebugSettings_.voxelVisible_)
    {
        if (runtimeUiOverlaySmokeController_.updatePlacementProbe(probe))
        {
            sceneObjectsDirty_ = true;
        }
        return;
    }

    const ui::OverlaySmokeScreenState& screenState =
        runtimeUiOverlaySmokeController_.state();
    const bool removalMode =
        screenState.buildModeState == ui::BuildModeState::PendingRemoval;
    probe.active = true;
    probe.itemKey = screenState.pendingBuildItemKey;
    probe.rotationSteps =
        runtimeUiOverlaySmokeController_.placementRotationSteps();
    if (!removalMode)
    {
        if (const ui::BuildCatalogItemDefinition* item =
            ui::findRuntimeBuildCatalogItem(probe.itemKey))
        {
            probe.footprintVoxels =
                ui::buildPlacementRotatedFootprintVoxels(*item,
                                                         probe.rotationSteps);
        }
    }

    const engine::voxel::PlacementProbeResult voxelProbe =
        engine::voxel::raycastPlacementProbe(
            voxelGrid_,
            {camera_.position, forward, kRuntimeUiPlacementProbeDistance,
             removalMode});
    PlaceableInstance removalPlaceableTarget{};
    float removalPlaceableDistance = 0.0f;
    const bool removalPlaceableHit =
        removalMode &&
        findRuntimePlaceableOnRay(sceneConfig(), camera_.position, forward,
                                  kRuntimeUiPlacementProbeDistance,
                                  removalPlaceableTarget,
                                  removalPlaceableDistance);
    const bool manualQaCandidate =
        !removalMode && !voxelProbe.rayHit &&
        configureRuntimeUiManualPlacementQaCandidate(probe, forward);
    if (voxelProbe.rayHit)
    {
        applyRuntimeUiVoxelPlacementProbe(probe, voxelProbe);
    }
    if (removalPlaceableHit &&
        (!voxelProbe.rayHit ||
         removalPlaceableDistance <= voxelProbe.distance + 1.0f))
    {
        probe.rayHit = true;
        probe.hasPlacementCandidate = true;
        probe.snappedToGrid = false;
        probe.hitVoxel = voxelFromPointFloor(removalPlaceableTarget.position);
        probe.hitNormal = glm::ivec3(0);
        probe.placementVoxel = probe.hitVoxel;
        probe.ghostWorldPosition = removalPlaceableTarget.position;
        probe.distance = removalPlaceableDistance;
        probe.targetPlaceableUuid = removalPlaceableTarget.uuid;
        probe.targetPlaceablePrototype =
            runtimePlaceablePrototypeLabel(removalPlaceableTarget);
    }
    else if (!removalMode && !voxelProbe.rayHit && !manualQaCandidate)
    {
        applyRuntimeUiVoxelPlacementProbe(
            probe,
            engine::voxel::fallbackPlacementProbe(
                {camera_.position, forward, kRuntimeUiPlacementProbeDistance,
                 kRuntimeUiPlacementFallbackDistance,
                 kRuntimeUiPlacementFallbackPlaneY, false}));
    }

    ui::BuildPlacementEvaluation placementEvaluation{};
    ui::BuildRemovalEvaluation removalEvaluation{};
    {
        auto lock = voxelGrid_.lockShared();
        if (removalMode)
        {
            if (!probe.targetPlaceableUuid.empty())
            {
                removalEvaluation.targetVoxel = probe.placementVoxel;
                removalEvaluation.block = BLOCK_AIR;
                removalEvaluation.valid = true;
                removalEvaluation.invalidReason =
                    ui::BuildRemovalInvalidReason::None;
            }
            else
            {
                removalEvaluation =
                    ui::evaluateBuildRemovalCandidate(voxelGrid_,
                                                      probe.placementVoxel,
                                                      probe.rayHit);
            }
        }
        else
        {
            placementEvaluation =
                ui::evaluateBuildPlacementCandidate(voxelGrid_, probe.itemKey,
                                                    probe.placementVoxel,
                                                    probe.rayHit,
                                                    probe.rotationSteps);
        }
    }
    probe.footprintVoxels =
        removalMode ? glm::ivec3(1) : placementEvaluation.footprintVoxels;
    if (probe.hasPlacementCandidate)
    {
        probe.ghostWorldPosition =
            glm::vec3(probe.placementVoxel) + glm::vec3(probe.footprintVoxels) * 0.5f;
    }
    if (removalMode)
    {
        probe.placementValid = removalEvaluation.valid;
        probe.invalidReason =
            ui::buildRemovalInvalidReasonLabel(removalEvaluation.invalidReason);
    }
    else
    {
        bool placementValid = placementEvaluation.valid;
        std::string placementInvalidReason =
            ui::buildPlacementInvalidReasonLabel(placementEvaluation.invalidReason);
        if (placementValid)
        {
            const ui::BuildCatalogItemDefinition* item =
                ui::findRuntimeBuildCatalogItem(probe.itemKey);
            const bool prototypePlacement =
                item != nullptr && item->hasPlaceablePrototype();
            const FoliagePrototype* prototype =
                prototypePlacement
                    ? FoliageCatalog::findPrototype(item->placeablePrototypeSlug,
                                                    item->placeablePrototypeVersion)
                    : nullptr;
            if (prototypePlacement && prototype != nullptr &&
                sceneConfig().loadAquariumTest)
            {
                PlaceableInstance candidate{};
                candidate.prototypeSlug = std::string(item->placeablePrototypeSlug);
                candidate.prototypeVersion = item->placeablePrototypeVersion;
                candidate.position = runtimePlaceablePositionFromProbe(probe);
                const std::vector<PlaceableInstance> existingInstances =
                    FoliageCatalog::aquariumHeroFoliageInstances(
                        &sceneConfig().placeables, sceneConfig().useDefaultPlaceables);
                if (runtimePlaceableFootprintOverlapsExisting(candidate, *prototype,
                                                              existingInstances))
                {
                    placementValid = false;
                    placementInvalidReason =
                        ui::buildPlacementInvalidReasonLabel(
                            ui::BuildPlacementInvalidReason::Occupied);
                }
            }
        }
        probe.placementValid = placementValid;
        probe.invalidReason = placementInvalidReason;
    }

    if (runtimeUiOverlaySmokeController_.updatePlacementProbe(probe))
    {
        sceneObjectsDirty_ = true;
    }
    if (probe.hasPlacementCandidate && chunkBoundsMesh_.vbo == VK_NULL_HANDLE)
    {
        buildChunkBoundsMesh();
        sceneObjectsDirty_ = true;
    }
}
#endif

#if VOXEL_WITH_RUNTIME_UI
void App::configureRuntimeUiSnappedCommitSmokeTarget()
{
    const glm::ivec3 surfaceVoxel(16, 16, 16);
    const glm::ivec3 placementVoxel(16, 16, 17);
    const glm::ivec3 targetChunk(0, 0, 0);

    camera_.position = glm::vec3(16.5f, 16.5f, 24.5f);
    camera_.yaw = 0.0f;
    camera_.pitch = 0.0f;

    if (!engine::voxel::containsChunk(voxelGrid_, targetChunk))
    {
        voxelGrid_.create(glm::ivec3(1), targetChunk);
        const std::vector<glm::ivec3> chunkCoords =
            engine::voxel::chunkCoords(voxelGrid_);
        voxelRenderResources_.resetChunks(chunkCoords);
    }
    if (voxelMaterials_[BLOCK_STONE].set == VK_NULL_HANDLE)
    {
        initializeVoxelWorldMaterials();
    }

    bool surfaceReady = false;
    bool placementReady = false;
    {
        auto lock = voxelGrid_.lockUnique();
        const BlockId currentSurface =
            voxelGrid_.getVoxelWorld(surfaceVoxel.x, surfaceVoxel.y,
                                     surfaceVoxel.z);
        surfaceReady =
            currentSurface == BLOCK_STONE ||
            voxelGrid_.setVoxelWorld(surfaceVoxel.x, surfaceVoxel.y,
                                     surfaceVoxel.z, BLOCK_STONE);

        const BlockId currentPlacement =
            voxelGrid_.getVoxelWorld(placementVoxel.x, placementVoxel.y,
                                     placementVoxel.z);
        placementReady =
            currentPlacement == BLOCK_AIR ||
            voxelGrid_.setVoxelWorld(placementVoxel.x, placementVoxel.y,
                                     placementVoxel.z, BLOCK_AIR);
    }

    engine::voxel::markVoxelEditDirtyNeighbors(voxelGrid_, surfaceVoxel, true);
    engine::voxel::markVoxelEditDirtyNeighbors(voxelGrid_, placementVoxel, true);
    sceneObjectsDirty_ = true;
    voxelObjectsDirty_ = true;

    logInfo("Automation][UISnappedCommitFixture",
            makeLogMessage("surface_voxel=", surfaceVoxel.x, ",", surfaceVoxel.y,
                           ",", surfaceVoxel.z,
                           " placement_voxel=", placementVoxel.x, ",",
                           placementVoxel.y, ",", placementVoxel.z,
                           " camera_world_mm=",
                           static_cast<int>(
                               std::round(camera_.position.x * 1000.0f)),
                           ",",
                           static_cast<int>(
                               std::round(camera_.position.y * 1000.0f)),
                           ",",
                           static_cast<int>(
                               std::round(camera_.position.z * 1000.0f)),
                           " yaw_millirad=",
                           static_cast<int>(std::round(camera_.yaw * 1000.0f)),
                           " pitch_millirad=",
                           static_cast<int>(
                               std::round(camera_.pitch * 1000.0f)),
                           " surface_ready=", surfaceReady ? 1 : 0,
                           " placement_ready=", placementReady ? 1 : 0));
}

void App::configureRuntimeUiRemovalSmokeTarget()
{
    const glm::ivec3 targetVoxel(16, 16, 16);
    const glm::ivec3 targetChunk(0, 0, 0);

    camera_.position = glm::vec3(16.5f, 16.5f, 24.5f);
    camera_.yaw = 0.0f;
    camera_.pitch = 0.0f;

    if (!engine::voxel::containsChunk(voxelGrid_, targetChunk))
    {
        voxelGrid_.create(glm::ivec3(1), targetChunk);
        const std::vector<glm::ivec3> chunkCoords =
            engine::voxel::chunkCoords(voxelGrid_);
        voxelRenderResources_.resetChunks(chunkCoords);
    }
    if (voxelMaterials_[BLOCK_SAND].set == VK_NULL_HANDLE)
    {
        initializeVoxelWorldMaterials();
    }

    bool targetReady = false;
    {
        auto lock = voxelGrid_.lockUnique();
        const BlockId currentTarget =
            voxelGrid_.getVoxelWorld(targetVoxel.x, targetVoxel.y,
                                     targetVoxel.z);
        targetReady =
            currentTarget == BLOCK_SAND ||
            voxelGrid_.setVoxelWorld(targetVoxel.x, targetVoxel.y,
                                     targetVoxel.z, BLOCK_SAND);
    }

    engine::voxel::markVoxelEditDirtyNeighbors(voxelGrid_, targetVoxel, true);
    sceneObjectsDirty_ = true;
    voxelObjectsDirty_ = true;

    logInfo("Automation][UIRemovalFixture",
            makeLogMessage("target_voxel=", targetVoxel.x, ",", targetVoxel.y,
                           ",", targetVoxel.z,
                           " target_block=", BLOCK_SAND,
                           " target_ready=", targetReady ? 1 : 0));
}

void App::configureRuntimeUiMultiFootprintSmokeTarget(bool blocked,
                                                      int rotationSteps)
{
    const int normalizedRotation =
        ui::normalizeBuildPlacementRotationSteps(rotationSteps);
    const bool sideApproach = normalizedRotation % 2 == 1;
    const glm::ivec3 surfaceVoxel =
        sideApproach ? glm::ivec3(16, 16, 17) : glm::ivec3(16, 16, 16);
    const glm::ivec3 placementVoxel =
        sideApproach ? glm::ivec3(17, 16, 17) : glm::ivec3(16, 16, 17);
    const glm::ivec3 blockedFootprintVoxel =
        sideApproach ? glm::ivec3(17, 16, 18) : glm::ivec3(17, 16, 17);
    const glm::ivec3 clearFootprintVoxel =
        sideApproach ? glm::ivec3(18, 16, 17) : glm::ivec3(16, 16, 18);
    const glm::ivec3 targetChunk(0, 0, 0);

    camera_.position = sideApproach ? glm::vec3(24.5f, 16.5f, 17.5f)
                                    : glm::vec3(16.5f, 16.5f, 24.5f);
    camera_.yaw = sideApproach ? -1.57079632679f : 0.0f;
    camera_.pitch = 0.0f;

    if (!engine::voxel::containsChunk(voxelGrid_, targetChunk))
    {
        voxelGrid_.create(glm::ivec3(1), targetChunk);
        const std::vector<glm::ivec3> chunkCoords =
            engine::voxel::chunkCoords(voxelGrid_);
        voxelRenderResources_.resetChunks(chunkCoords);
    }
    if (voxelMaterials_[BLOCK_SAND].set == VK_NULL_HANDLE ||
        voxelMaterials_[BLOCK_STONE].set == VK_NULL_HANDLE ||
        (blocked && voxelMaterials_[BLOCK_LOG].set == VK_NULL_HANDLE))
    {
        initializeVoxelWorldMaterials();
    }

    bool surfaceReady = false;
    bool placementReady = false;
    bool blockedFootprintReady = false;
    bool clearFootprintReady = false;
    {
        auto lock = voxelGrid_.lockUnique();
        const BlockId currentSurface =
            voxelGrid_.getVoxelWorld(surfaceVoxel.x, surfaceVoxel.y,
                                     surfaceVoxel.z);
        surfaceReady =
            currentSurface == BLOCK_STONE ||
            voxelGrid_.setVoxelWorld(surfaceVoxel.x, surfaceVoxel.y,
                                     surfaceVoxel.z, BLOCK_STONE);

        const BlockId currentPlacement =
            voxelGrid_.getVoxelWorld(placementVoxel.x, placementVoxel.y,
                                     placementVoxel.z);
        placementReady =
            currentPlacement == BLOCK_AIR ||
            voxelGrid_.setVoxelWorld(placementVoxel.x, placementVoxel.y,
                                     placementVoxel.z, BLOCK_AIR);

        const BlockId desiredBlocked =
            blocked ? BLOCK_LOG : BLOCK_AIR;
        const BlockId currentBlocked =
            voxelGrid_.getVoxelWorld(blockedFootprintVoxel.x,
                                     blockedFootprintVoxel.y,
                                     blockedFootprintVoxel.z);
        blockedFootprintReady =
            currentBlocked == desiredBlocked ||
            voxelGrid_.setVoxelWorld(blockedFootprintVoxel.x,
                                     blockedFootprintVoxel.y,
                                     blockedFootprintVoxel.z,
                                     desiredBlocked);

        const BlockId currentClear =
            voxelGrid_.getVoxelWorld(clearFootprintVoxel.x,
                                     clearFootprintVoxel.y,
                                     clearFootprintVoxel.z);
        clearFootprintReady =
            currentClear == BLOCK_AIR ||
            voxelGrid_.setVoxelWorld(clearFootprintVoxel.x,
                                     clearFootprintVoxel.y,
                                     clearFootprintVoxel.z,
                                     BLOCK_AIR);
    }

    engine::voxel::markVoxelEditDirtyNeighbors(voxelGrid_, surfaceVoxel, true);
    engine::voxel::markVoxelEditDirtyNeighbors(voxelGrid_, placementVoxel, true);
    engine::voxel::markVoxelEditDirtyNeighbors(voxelGrid_, blockedFootprintVoxel, true);
    engine::voxel::markVoxelEditDirtyNeighbors(voxelGrid_, clearFootprintVoxel, true);
    sceneObjectsDirty_ = true;
    voxelObjectsDirty_ = true;

    logInfo("Automation][UIMultiFootprintFixture",
            makeLogMessage("blocked=", blocked ? 1 : 0,
                           " rotation_steps=", normalizedRotation,
                           " surface_voxel=", surfaceVoxel.x, ",", surfaceVoxel.y,
                           ",", surfaceVoxel.z,
                           " placement_voxel=", placementVoxel.x, ",",
                           placementVoxel.y, ",", placementVoxel.z,
                           " blocked_footprint_voxel=",
                           blockedFootprintVoxel.x, ",",
                           blockedFootprintVoxel.y, ",",
                           blockedFootprintVoxel.z,
                           " clear_footprint_voxel=",
                           clearFootprintVoxel.x, ",",
                           clearFootprintVoxel.y, ",",
                           clearFootprintVoxel.z,
                           " surface_ready=", surfaceReady ? 1 : 0,
                           " placement_ready=", placementReady ? 1 : 0,
                           " blocked_footprint_ready=",
                           blockedFootprintReady ? 1 : 0,
                           " clear_footprint_ready=",
                           clearFootprintReady ? 1 : 0));
}
#endif

void App::logRuntimeUiProbe()
{
#if VOXEL_WITH_RUNTIME_UI
    if (runtimeUiContext_.activeScreenId() == ui::kOverlaySmokeScreenId)
    {
        ui::UiTree* tree = ensureRuntimeUiActiveTree();
        if (tree == nullptr)
        {
            return;
        }
        const ui::UiElement* panel =
            ui::findElementById(tree->root, "test_panel");
        const ui::UiRect rect =
            panel != nullptr ? panel->computedRect : ui::UiRect{};
        const VkExtent2D viewportExtent = renderer_.swapchainExtent();
        const int viewportW = static_cast<int>(viewportExtent.width);
        const int viewportH = static_cast<int>(viewportExtent.height);
        const bool insideViewport =
            viewportW > 0 && viewportH > 0 && rect.x >= 0.0f && rect.y >= 0.0f &&
            (rect.x + rect.width) <= static_cast<float>(viewportW) &&
            (rect.y + rect.height) <= static_cast<float>(viewportH);
        logInfo("Automation][UIProbe",
                makeLogMessage("screen=ui_overlay_smoke element=test_panel visible=1 enabled=1 "
                               "rect=",
                               static_cast<int>(rect.x), ",", static_cast<int>(rect.y), ",",
                               static_cast<int>(rect.width), ",",
                               static_cast<int>(rect.height),
                               " synthetic=0 retained=1 viewport=",
                               viewportW, ",", viewportH,
                               " inside_viewport=", insideViewport ? 1 : 0));
        return;
    }
    if (runtimeUiContext_.activeScreenId() == ui::kMainMenuScreenId)
    {
        ui::UiTree* tree = ensureRuntimeUiActiveTree();
        if (tree == nullptr)
        {
            return;
        }
        const ui::UiElement* button =
            ui::findElementById(tree->root, ui::kMainMenuStartButtonId);
        const ui::UiElement* backdrop =
            ui::findElementById(tree->root, "main_menu_backdrop");
        const ui::UiRect rect =
            button != nullptr ? button->computedRect : ui::UiRect{};
        const ui::UiRect backdropRect =
            backdrop != nullptr ? backdrop->computedRect : ui::UiRect{};
        const VkExtent2D viewportExtent = renderer_.swapchainExtent();
        const int viewportW = static_cast<int>(viewportExtent.width);
        const int viewportH = static_cast<int>(viewportExtent.height);
        const bool backdropFillsViewport =
            viewportW > 0 && viewportH > 0 && backdrop != nullptr &&
            backdropRect.x == 0.0f && backdropRect.y == 0.0f &&
            backdropRect.width == static_cast<float>(viewportW) &&
            backdropRect.height == static_cast<float>(viewportH);
        logInfo("Automation][UIProbe",
                makeLogMessage("screen=ui_main_menu element=",
                               ui::kMainMenuStartButtonId,
                               " visible=", button != nullptr ? 1 : 0,
                               " enabled=",
                               button != nullptr && button->enabled ? 1 : 0,
                               " rect=", static_cast<int>(rect.x), ",",
                               static_cast<int>(rect.y), ",",
                               static_cast<int>(rect.width), ",",
                               static_cast<int>(rect.height),
                               " synthetic=0 retained=1 viewport=",
                               viewportW, ",", viewportH,
                               " backdrop_rect=", static_cast<int>(backdropRect.x), ",",
                               static_cast<int>(backdropRect.y), ",",
                               static_cast<int>(backdropRect.width), ",",
                               static_cast<int>(backdropRect.height),
                               " backdrop_fills_viewport=",
                               backdropFillsViewport ? 1 : 0));
        return;
    }
#endif

    logInfo("Automation][UIProbe",
            makeLogMessage("screen=",
                           runtimeUiContext_.hasActiveScreen()
                               ? runtimeUiContext_.activeScreenId()
                               : std::string("<none>"),
                           " element=<none> visible=0 enabled=0 rect=0,0,0,0 synthetic=1"));
}

void App::updateAutomationUiScript()
{
    if (automationUiScript_.empty() || automationUiScriptCompleted_)
    {
        return;
    }

    if (automationUiScript_ == "fish_focus_smoke")
    {
        updateFishFocusAutomation();
        return;
    }

    if (automationUiScript_ == "play_preview_enter_exit")
    {
#if VOXEL_WITH_EDITOR
        if (automationUiInputCursor_ == 0)
        {
            enterRuntimeUiPlayPreview("automation-ui-script");
            logInfo("Automation][PlayPreview",
                    makeLogMessage("action=enter frame=", renderedFrameCount_,
                                   " mode=", InputRouter::label(appMode_)));
            if (!automationUiProbeLogged_)
            {
                logRuntimeUiProbe();
                automationUiProbeLogged_ = true;
            }
            automationUiInputCursor_ = 1;
            return;
        }
        if (automationUiInputCursor_ == 1 && renderedFrameCount_ >= 1)
        {
            const bool handled =
                inputController_.handleReservedKey(GLFW_KEY_F5, GLFW_PRESS);
            logInfo("Automation][PlayPreview",
                    makeLogMessage("action=exit_hotkey frame=", renderedFrameCount_,
                                   " handled=", handled ? 1 : 0,
                                   " mode=", InputRouter::label(appMode_)));
            automationUiInputCursor_ = 2;
            automationUiScriptCompleted_ = true;
            logInfo("Automation][UIScript", "script=play_preview_enter_exit completed=1");
        }
#else
        logAndExit("Automation",
                   "play_preview_enter_exit requires VOXEL_WITH_EDITOR=ON.");
#endif
        return;
    }

    if (automationUiScript_ == "play_preview_main_menu_enter_exit")
    {
#if VOXEL_WITH_EDITOR
        if (automationUiInputCursor_ == 0)
        {
            const bool handled = inputController_.handleReservedKey(
                GLFW_KEY_F5, GLFW_PRESS, GLFW_MOD_SHIFT);
            logInfo("Automation][PlayPreview",
                    makeLogMessage("action=enter_main_menu_hotkey frame=",
                                   renderedFrameCount_, " handled=", handled ? 1 : 0,
                                   " mode=", InputRouter::label(appMode_)));
            if (!automationUiProbeLogged_)
            {
                logRuntimeUiProbe();
                automationUiProbeLogged_ = true;
            }
            automationUiInputCursor_ = 1;
            return;
        }
        if (automationUiInputCursor_ == 1 && renderedFrameCount_ >= 1)
        {
            const bool handled =
                inputController_.handleReservedKey(GLFW_KEY_F5, GLFW_PRESS);
            logInfo("Automation][PlayPreview",
                    makeLogMessage("action=exit_hotkey frame=", renderedFrameCount_,
                                   " handled=", handled ? 1 : 0,
                                   " mode=", InputRouter::label(appMode_)));
            automationUiInputCursor_ = 2;
            automationUiScriptCompleted_ = true;
            logInfo("Automation][UIScript",
                    "script=play_preview_main_menu_enter_exit completed=1");
        }
#else
        logAndExit("Automation",
                   "play_preview_main_menu_enter_exit requires VOXEL_WITH_EDITOR=ON.");
#endif
        return;
    }

    if (automationUiScript_ != "ui_overlay_smoke" &&
        automationUiScript_ != "ui_click_smoke" &&
        automationUiScript_ != "ui_water_maintain_smoke" &&
        automationUiScript_ != "ui_catalog_item_smoke" &&
        automationUiScript_ != "ui_catalog_scroll_smoke" &&
        automationUiScript_ != "ui_catalog_close_smoke" &&
        automationUiScript_ != "ui_pending_cancel_smoke" &&
        automationUiScript_ != "ui_pending_escape_cancel_smoke" &&
        automationUiScript_ != "ui_pending_right_mouse_ignored_smoke" &&
        automationUiScript_ != "ui_pending_confirm_invalid_smoke" &&
        automationUiScript_ != "ui_snapped_candidate_smoke" &&
        automationUiScript_ != "ui_manual_placement_qa_target_smoke" &&
        automationUiScript_ != "ui_placeable_prototype_ghost_smoke" &&
        automationUiScript_ != "ui_placeable_prototype_commit_smoke" &&
        automationUiScript_ != "ui_placeable_prototype_remove_smoke" &&
        automationUiScript_ != "ui_snapped_commit_smoke" &&
        automationUiScript_ != "ui_multi_footprint_commit_smoke" &&
        automationUiScript_ != "ui_multi_footprint_blocked_smoke" &&
        automationUiScript_ != "ui_rotated_footprint_commit_smoke" &&
        automationUiScript_ != "ui_rotated_footprint_blocked_smoke" &&
        automationUiScript_ != "ui_rotation_idle_ignored_smoke" &&
        automationUiScript_ != "ui_remove_commit_smoke" &&
        automationUiScript_ != "ui_pause_stack_smoke" &&
        automationUiScript_ != "ui_pause_options_menu_smoke" &&
        automationUiScript_ != "ui_main_menu_escape_ignored_smoke" &&
        automationUiScript_ != "ui_fish_list_focus_smoke" &&
        automationUiScript_ != "ui_main_menu_start_smoke" &&
        automationUiScript_ != "ui_main_menu_options_smoke" &&
        automationUiScript_ != "ui_main_menu_options_labels_smoke")
    {
        logAndExit("Automation",
                   std::string("Unknown --automation-ui-script: ") + automationUiScript_);
    }

    if (!automationUiProbeLogged_)
    {
        logRuntimeUiProbe();
        automationUiProbeLogged_ = true;
    }

    if (automationUiInputQueue_.empty())
    {
        const bool multiFootprintScript =
            automationUiScript_ == "ui_multi_footprint_commit_smoke" ||
            automationUiScript_ == "ui_multi_footprint_blocked_smoke";
        const bool rotatedFootprintScript =
            automationUiScript_ == "ui_rotated_footprint_commit_smoke" ||
            automationUiScript_ == "ui_rotated_footprint_blocked_smoke";
        const bool blockedFootprintScript =
            automationUiScript_ == "ui_multi_footprint_blocked_smoke" ||
            automationUiScript_ == "ui_rotated_footprint_blocked_smoke";
        const bool decorFootprintScript =
            multiFootprintScript || rotatedFootprintScript;

        if (automationUiScript_ == "ui_main_menu_start_smoke")
        {
            automationUiInputQueue_.push_back(AutomationUiInputEvent{
                2, AutomationUiInputKind::PointerMove, 282.0, 254.0, 0, 0});
            automationUiInputQueue_.push_back(AutomationUiInputEvent{
                3, AutomationUiInputKind::PointerDown, 282.0, 254.0, 0,
                GLFW_MOUSE_BUTTON_LEFT});
            automationUiInputQueue_.push_back(AutomationUiInputEvent{
                4, AutomationUiInputKind::PointerUp, 282.0, 254.0, 0,
                GLFW_MOUSE_BUTTON_LEFT});
        }
        if (automationUiScript_ == "ui_main_menu_options_smoke")
        {
            automationUiInputQueue_.push_back(AutomationUiInputEvent{
                2, AutomationUiInputKind::PointerMove, 238.0, 306.0, 0, 0});
            automationUiInputQueue_.push_back(AutomationUiInputEvent{
                3, AutomationUiInputKind::PointerDown, 238.0, 306.0, 0,
                GLFW_MOUSE_BUTTON_LEFT});
            automationUiInputQueue_.push_back(AutomationUiInputEvent{
                4, AutomationUiInputKind::PointerUp, 238.0, 306.0, 0,
                GLFW_MOUSE_BUTTON_LEFT});
            automationUiInputQueue_.push_back(AutomationUiInputEvent{
                6, AutomationUiInputKind::PointerMove, 640.0, 424.0, 0, 0});
            automationUiInputQueue_.push_back(AutomationUiInputEvent{
                7, AutomationUiInputKind::PointerDown, 640.0, 424.0, 0,
                GLFW_MOUSE_BUTTON_LEFT});
            automationUiInputQueue_.push_back(AutomationUiInputEvent{
                8, AutomationUiInputKind::PointerUp, 640.0, 424.0, 0,
                GLFW_MOUSE_BUTTON_LEFT});
        }
        if (automationUiScript_ == "ui_main_menu_options_labels_smoke")
        {
            automationUiInputQueue_.push_back(AutomationUiInputEvent{
                2, AutomationUiInputKind::PointerMove, 238.0, 306.0, 0, 0});
            automationUiInputQueue_.push_back(AutomationUiInputEvent{
                3, AutomationUiInputKind::PointerDown, 238.0, 306.0, 0,
                GLFW_MOUSE_BUTTON_LEFT});
            automationUiInputQueue_.push_back(AutomationUiInputEvent{
                4, AutomationUiInputKind::PointerUp, 238.0, 306.0, 0,
                GLFW_MOUSE_BUTTON_LEFT});
            automationUiInputQueue_.push_back(AutomationUiInputEvent{
                6, AutomationUiInputKind::PointerMove, 640.0, 276.0, 0, 0});
            automationUiInputQueue_.push_back(AutomationUiInputEvent{
                7, AutomationUiInputKind::PointerDown, 640.0, 276.0, 0,
                GLFW_MOUSE_BUTTON_LEFT});
            automationUiInputQueue_.push_back(AutomationUiInputEvent{
                8, AutomationUiInputKind::PointerUp, 640.0, 276.0, 0,
                GLFW_MOUSE_BUTTON_LEFT});
            automationUiInputQueue_.push_back(AutomationUiInputEvent{
                10, AutomationUiInputKind::PointerMove, 640.0, 424.0, 0, 0});
            automationUiInputQueue_.push_back(AutomationUiInputEvent{
                11, AutomationUiInputKind::PointerDown, 640.0, 424.0, 0,
                GLFW_MOUSE_BUTTON_LEFT});
            automationUiInputQueue_.push_back(AutomationUiInputEvent{
                12, AutomationUiInputKind::PointerUp, 640.0, 424.0, 0,
                GLFW_MOUSE_BUTTON_LEFT});
            automationUiInputQueue_.push_back(AutomationUiInputEvent{
                14, AutomationUiInputKind::PointerMove, 282.0, 254.0, 0, 0});
            automationUiInputQueue_.push_back(AutomationUiInputEvent{
                15, AutomationUiInputKind::PointerDown, 282.0, 254.0, 0,
                GLFW_MOUSE_BUTTON_LEFT});
            automationUiInputQueue_.push_back(AutomationUiInputEvent{
                16, AutomationUiInputKind::PointerUp, 282.0, 254.0, 0,
                GLFW_MOUSE_BUTTON_LEFT});
        }
        if (automationUiScript_ == "ui_main_menu_escape_ignored_smoke")
        {
            automationUiInputQueue_.push_back(AutomationUiInputEvent{
                2, AutomationUiInputKind::KeyDown, 0.0, 0.0, GLFW_KEY_ESCAPE, 0});
        }
        if (automationUiScript_ == "ui_fish_list_focus_smoke")
        {
            automationUiInputQueue_.push_back(AutomationUiInputEvent{
                4, AutomationUiInputKind::PointerMove, 1140.0, 94.0, 0, 0});
            automationUiInputQueue_.push_back(AutomationUiInputEvent{
                5, AutomationUiInputKind::PointerDown, 1140.0, 94.0, 0,
                GLFW_MOUSE_BUTTON_LEFT});
            automationUiInputQueue_.push_back(AutomationUiInputEvent{
                6, AutomationUiInputKind::PointerUp, 1140.0, 94.0, 0,
                GLFW_MOUSE_BUTTON_LEFT});
            automationUiInputQueue_.push_back(AutomationUiInputEvent{
                8, AutomationUiInputKind::PointerMove, 1192.0, 61.0, 0, 0});
            automationUiInputQueue_.push_back(AutomationUiInputEvent{
                9, AutomationUiInputKind::PointerDown, 1192.0, 61.0, 0,
                GLFW_MOUSE_BUTTON_LEFT});
            automationUiInputQueue_.push_back(AutomationUiInputEvent{
                10, AutomationUiInputKind::PointerUp, 1192.0, 61.0, 0,
                GLFW_MOUSE_BUTTON_LEFT});
        }
        if (automationUiScript_ == "ui_pause_stack_smoke")
        {
            automationUiInputQueue_.push_back(AutomationUiInputEvent{
                0, AutomationUiInputKind::KeyDown, 0.0, 0.0, GLFW_KEY_ESCAPE, 0});
            automationUiInputQueue_.push_back(AutomationUiInputEvent{
                2, AutomationUiInputKind::PointerMove, 640.0, 352.0, 0, 0});
            automationUiInputQueue_.push_back(AutomationUiInputEvent{
                3, AutomationUiInputKind::PointerDown, 640.0, 352.0, 0,
                GLFW_MOUSE_BUTTON_LEFT});
            automationUiInputQueue_.push_back(AutomationUiInputEvent{
                4, AutomationUiInputKind::PointerUp, 640.0, 352.0, 0,
                GLFW_MOUSE_BUTTON_LEFT});
        }
        if (automationUiScript_ == "ui_pause_options_menu_smoke")
        {
            automationUiInputQueue_.push_back(AutomationUiInputEvent{
                0, AutomationUiInputKind::KeyDown, 0.0, 0.0, GLFW_KEY_ESCAPE, 0});
            automationUiInputQueue_.push_back(AutomationUiInputEvent{
                2, AutomationUiInputKind::PointerMove, 640.0, 394.0, 0, 0});
            automationUiInputQueue_.push_back(AutomationUiInputEvent{
                3, AutomationUiInputKind::PointerDown, 640.0, 394.0, 0,
                GLFW_MOUSE_BUTTON_LEFT});
            automationUiInputQueue_.push_back(AutomationUiInputEvent{
                4, AutomationUiInputKind::PointerUp, 640.0, 394.0, 0,
                GLFW_MOUSE_BUTTON_LEFT});
            automationUiInputQueue_.push_back(AutomationUiInputEvent{
                6, AutomationUiInputKind::PointerMove, 640.0, 424.0, 0, 0});
            automationUiInputQueue_.push_back(AutomationUiInputEvent{
                7, AutomationUiInputKind::PointerDown, 640.0, 424.0, 0,
                GLFW_MOUSE_BUTTON_LEFT});
            automationUiInputQueue_.push_back(AutomationUiInputEvent{
                8, AutomationUiInputKind::PointerUp, 640.0, 424.0, 0,
                GLFW_MOUSE_BUTTON_LEFT});
            automationUiInputQueue_.push_back(AutomationUiInputEvent{
                10, AutomationUiInputKind::PointerMove, 640.0, 436.0, 0, 0});
            automationUiInputQueue_.push_back(AutomationUiInputEvent{
                11, AutomationUiInputKind::PointerDown, 640.0, 436.0, 0,
                GLFW_MOUSE_BUTTON_LEFT});
            automationUiInputQueue_.push_back(AutomationUiInputEvent{
                12, AutomationUiInputKind::PointerUp, 640.0, 436.0, 0,
                GLFW_MOUSE_BUTTON_LEFT});
        }
#if VOXEL_WITH_RUNTIME_UI
        if (automationUiScript_ == "ui_snapped_commit_smoke")
        {
            configureRuntimeUiSnappedCommitSmokeTarget();
        }
        if (automationUiScript_ == "ui_remove_commit_smoke")
        {
            configureRuntimeUiRemovalSmokeTarget();
        }
        if (decorFootprintScript)
        {
            configureRuntimeUiMultiFootprintSmokeTarget(blockedFootprintScript,
                                                        rotatedFootprintScript ? 1 : 0);
        }
#endif
        if (automationUiScript_ == "ui_water_maintain_smoke")
        {
            sceneConfigMutable().gameState.water.cleanliness = 0.50f;
            sceneConfigMutable().gameState.water.oxygen = 0.70f;
            automationUiInputQueue_.push_back(AutomationUiInputEvent{
                0, AutomationUiInputKind::PointerMove, 96.0, 210.0, 0, 0});
            automationUiInputQueue_.push_back(AutomationUiInputEvent{
                1, AutomationUiInputKind::PointerDown, 96.0, 210.0, 0,
                GLFW_MOUSE_BUTTON_LEFT});
            automationUiInputQueue_.push_back(AutomationUiInputEvent{
                2, AutomationUiInputKind::PointerUp, 96.0, 210.0, 0,
                GLFW_MOUSE_BUTTON_LEFT});
        }
        if (automationUiScript_ == "ui_click_smoke" ||
            automationUiScript_ == "ui_catalog_item_smoke" ||
            automationUiScript_ == "ui_catalog_scroll_smoke" ||
            automationUiScript_ == "ui_catalog_close_smoke" ||
            automationUiScript_ == "ui_pending_cancel_smoke" ||
            automationUiScript_ == "ui_pending_escape_cancel_smoke" ||
            automationUiScript_ == "ui_pending_right_mouse_ignored_smoke" ||
            automationUiScript_ == "ui_pending_confirm_invalid_smoke" ||
            automationUiScript_ == "ui_snapped_candidate_smoke" ||
            automationUiScript_ == "ui_manual_placement_qa_target_smoke" ||
            automationUiScript_ == "ui_placeable_prototype_ghost_smoke" ||
            automationUiScript_ == "ui_placeable_prototype_commit_smoke" ||
            automationUiScript_ == "ui_placeable_prototype_remove_smoke" ||
            automationUiScript_ == "ui_snapped_commit_smoke" ||
            automationUiScript_ == "ui_remove_commit_smoke" ||
            decorFootprintScript)
        {
            automationUiInputQueue_.push_back(AutomationUiInputEvent{
                0, AutomationUiInputKind::PointerMove, 96.0, 240.0, 0, 0});
            automationUiInputQueue_.push_back(AutomationUiInputEvent{
                1, AutomationUiInputKind::PointerDown, 96.0, 240.0, 0,
                GLFW_MOUSE_BUTTON_LEFT});
            automationUiInputQueue_.push_back(AutomationUiInputEvent{
                2, AutomationUiInputKind::PointerUp, 96.0, 240.0, 0,
                GLFW_MOUSE_BUTTON_LEFT});
            if (automationUiScript_ == "ui_catalog_item_smoke" ||
                automationUiScript_ == "ui_catalog_close_smoke" ||
                automationUiScript_ == "ui_pending_cancel_smoke" ||
                automationUiScript_ == "ui_pending_escape_cancel_smoke" ||
                automationUiScript_ == "ui_pending_right_mouse_ignored_smoke" ||
                automationUiScript_ == "ui_pending_confirm_invalid_smoke" ||
                automationUiScript_ == "ui_snapped_candidate_smoke" ||
                automationUiScript_ == "ui_manual_placement_qa_target_smoke" ||
                automationUiScript_ == "ui_placeable_prototype_ghost_smoke" ||
                automationUiScript_ == "ui_placeable_prototype_commit_smoke" ||
                automationUiScript_ == "ui_placeable_prototype_remove_smoke" ||
                automationUiScript_ == "ui_snapped_commit_smoke" ||
                automationUiScript_ == "ui_remove_commit_smoke" ||
                decorFootprintScript)
            {
                const double itemY =
                    automationUiScript_ == "ui_remove_commit_smoke"
                        ? 524.0
                        : automationUiScript_ == "ui_placeable_prototype_ghost_smoke" ||
                                  automationUiScript_ ==
                                      "ui_placeable_prototype_commit_smoke" ||
                                  automationUiScript_ ==
                                      "ui_placeable_prototype_remove_smoke"
                            ? 436.0
                            : decorFootprintScript ? 474.0 : 398.0;
                const double itemX =
                    automationUiScript_ == "ui_remove_commit_smoke" ? 316.0 : 72.0;
                if (decorFootprintScript)
                {
                    automationUiInputQueue_.push_back(AutomationUiInputEvent{
                        3, AutomationUiInputKind::Wheel, 92.0, 404.0, 0, 0, -3.0});
                }
                automationUiInputQueue_.push_back(AutomationUiInputEvent{
                    3, AutomationUiInputKind::PointerMove, itemX, itemY, 0, 0});
                automationUiInputQueue_.push_back(AutomationUiInputEvent{
                    4, AutomationUiInputKind::PointerDown, itemX, itemY, 0,
                    GLFW_MOUSE_BUTTON_LEFT});
                automationUiInputQueue_.push_back(AutomationUiInputEvent{
                    5, AutomationUiInputKind::PointerUp, itemX, itemY, 0,
                    GLFW_MOUSE_BUTTON_LEFT});
            }
            if (automationUiScript_ == "ui_catalog_scroll_smoke")
            {
                automationUiInputQueue_.push_back(AutomationUiInputEvent{
                    6, AutomationUiInputKind::Wheel, 92.0, 404.0, 0, 0, -3.0});
                automationUiInputQueue_.push_back(AutomationUiInputEvent{
                    7, AutomationUiInputKind::PointerMove, 72.0, 398.0, 0, 0});
                automationUiInputQueue_.push_back(AutomationUiInputEvent{
                    8, AutomationUiInputKind::PointerDown, 72.0, 398.0, 0,
                    GLFW_MOUSE_BUTTON_LEFT});
                automationUiInputQueue_.push_back(AutomationUiInputEvent{
                    9, AutomationUiInputKind::PointerUp, 72.0, 398.0, 0,
                    GLFW_MOUSE_BUTTON_LEFT});
            }
            if (automationUiScript_ == "ui_pending_cancel_smoke")
            {
                automationUiInputQueue_.push_back(AutomationUiInputEvent{
                    6, AutomationUiInputKind::PointerMove, 72.0, 524.0, 0, 0});
                automationUiInputQueue_.push_back(AutomationUiInputEvent{
                    7, AutomationUiInputKind::PointerDown, 72.0, 524.0, 0,
                    GLFW_MOUSE_BUTTON_LEFT});
                automationUiInputQueue_.push_back(AutomationUiInputEvent{
                    8, AutomationUiInputKind::PointerUp, 72.0, 524.0, 0,
                    GLFW_MOUSE_BUTTON_LEFT});
            }
            if (automationUiScript_ == "ui_pending_escape_cancel_smoke")
            {
                automationUiInputQueue_.push_back(AutomationUiInputEvent{
                    6, AutomationUiInputKind::KeyDown, 0.0, 0.0, GLFW_KEY_ESCAPE, 0});
            }
            if (automationUiScript_ == "ui_pending_right_mouse_ignored_smoke")
            {
                automationUiInputQueue_.push_back(AutomationUiInputEvent{
                    6, AutomationUiInputKind::PointerMove, 900.0, 500.0, 0, 0});
                automationUiInputQueue_.push_back(AutomationUiInputEvent{
                    7, AutomationUiInputKind::PointerDown, 900.0, 500.0, 0,
                    GLFW_MOUSE_BUTTON_RIGHT});
                automationUiInputQueue_.push_back(AutomationUiInputEvent{
                    8, AutomationUiInputKind::PointerUp, 900.0, 500.0, 0,
                    GLFW_MOUSE_BUTTON_RIGHT});
                automationUiInputQueue_.push_back(AutomationUiInputEvent{
                    9, AutomationUiInputKind::PointerDown, 900.0, 500.0, 0,
                    GLFW_MOUSE_BUTTON_RIGHT});
                automationUiInputQueue_.push_back(AutomationUiInputEvent{
                    10, AutomationUiInputKind::PointerMove, 930.0, 500.0, 0, 0});
                automationUiInputQueue_.push_back(AutomationUiInputEvent{
                    11, AutomationUiInputKind::PointerUp, 930.0, 500.0, 0,
                    GLFW_MOUSE_BUTTON_RIGHT});
                automationUiInputQueue_.push_back(AutomationUiInputEvent{
                    12, AutomationUiInputKind::KeyDown, 0.0, 0.0, GLFW_KEY_ESCAPE, 0});
            }
            if (automationUiScript_ == "ui_pending_confirm_invalid_smoke")
            {
                automationUiInputQueue_.push_back(AutomationUiInputEvent{
                    6, AutomationUiInputKind::PointerMove, 900.0, 500.0, 0, 0});
                automationUiInputQueue_.push_back(AutomationUiInputEvent{
                    7, AutomationUiInputKind::PointerDown, 900.0, 500.0, 0,
                    GLFW_MOUSE_BUTTON_LEFT});
                automationUiInputQueue_.push_back(AutomationUiInputEvent{
                    8, AutomationUiInputKind::KeyDown, 0.0, 0.0, GLFW_KEY_ESCAPE, 0});
            }
            if (automationUiScript_ == "ui_manual_placement_qa_target_smoke" ||
                automationUiScript_ == "ui_placeable_prototype_commit_smoke" ||
                automationUiScript_ == "ui_placeable_prototype_remove_smoke" ||
                automationUiScript_ == "ui_snapped_commit_smoke" ||
                automationUiScript_ == "ui_remove_commit_smoke" ||
                decorFootprintScript)
            {
                const uint64_t moveFrame = rotatedFootprintScript ? 7 : 6;
                const uint64_t downFrame = rotatedFootprintScript ? 8 : 7;
                const uint64_t upFrame = rotatedFootprintScript ? 9 : 8;
                if (rotatedFootprintScript)
                {
                    automationUiInputQueue_.push_back(AutomationUiInputEvent{
                        6, AutomationUiInputKind::KeyDown, 0.0, 0.0, GLFW_KEY_R, 0});
                }
                automationUiInputQueue_.push_back(AutomationUiInputEvent{
                    moveFrame, AutomationUiInputKind::PointerMove, 900.0, 500.0,
                    0, 0});
                automationUiInputQueue_.push_back(AutomationUiInputEvent{
                    downFrame, AutomationUiInputKind::PointerDown, 900.0, 500.0, 0,
                    GLFW_MOUSE_BUTTON_LEFT});
                automationUiInputQueue_.push_back(AutomationUiInputEvent{
                    upFrame, AutomationUiInputKind::PointerUp, 900.0, 500.0, 0,
                    GLFW_MOUSE_BUTTON_LEFT});
                if (blockedFootprintScript)
                {
                    automationUiInputQueue_.push_back(AutomationUiInputEvent{
                        upFrame + 1, AutomationUiInputKind::KeyDown, 0.0, 0.0,
                        GLFW_KEY_ESCAPE, 0});
                }
            }
            if (automationUiScript_ == "ui_placeable_prototype_remove_smoke")
            {
                automationUiInputQueue_.push_back(AutomationUiInputEvent{
                    10, AutomationUiInputKind::PointerMove, 316.0, 524.0, 0, 0});
                automationUiInputQueue_.push_back(AutomationUiInputEvent{
                    11, AutomationUiInputKind::PointerDown, 316.0, 524.0, 0,
                    GLFW_MOUSE_BUTTON_LEFT});
                automationUiInputQueue_.push_back(AutomationUiInputEvent{
                    12, AutomationUiInputKind::PointerUp, 316.0, 524.0, 0,
                    GLFW_MOUSE_BUTTON_LEFT});
                automationUiInputQueue_.push_back(AutomationUiInputEvent{
                    14, AutomationUiInputKind::PointerMove, 900.0, 500.0, 0, 0});
                automationUiInputQueue_.push_back(AutomationUiInputEvent{
                    15, AutomationUiInputKind::PointerDown, 900.0, 500.0, 0,
                    GLFW_MOUSE_BUTTON_LEFT});
                automationUiInputQueue_.push_back(AutomationUiInputEvent{
                    16, AutomationUiInputKind::PointerUp, 900.0, 500.0, 0,
                    GLFW_MOUSE_BUTTON_LEFT});
            }
            if (automationUiScript_ == "ui_catalog_close_smoke")
            {
                automationUiInputQueue_.push_back(AutomationUiInputEvent{
                    6, AutomationUiInputKind::PointerMove, 420.0, 322.0, 0, 0});
                automationUiInputQueue_.push_back(AutomationUiInputEvent{
                    7, AutomationUiInputKind::PointerDown, 420.0, 322.0, 0,
                    GLFW_MOUSE_BUTTON_LEFT});
                automationUiInputQueue_.push_back(AutomationUiInputEvent{
                    8, AutomationUiInputKind::PointerUp, 420.0, 322.0, 0,
                    GLFW_MOUSE_BUTTON_LEFT});
                automationUiInputQueue_.push_back(AutomationUiInputEvent{
                    9, AutomationUiInputKind::PointerMove, 420.0, 322.0, 0, 0});
            }
        }
        else if (automationUiScript_ == "ui_rotation_idle_ignored_smoke")
        {
            automationUiInputQueue_.push_back(AutomationUiInputEvent{
                0, AutomationUiInputKind::KeyDown, 0.0, 0.0, GLFW_KEY_R, 0});
        }
        else if (automationUiScript_ != "ui_pause_stack_smoke" &&
                 automationUiScript_ != "ui_pause_options_menu_smoke" &&
                 automationUiScript_ != "ui_fish_list_focus_smoke" &&
                 automationUiScript_ != "ui_main_menu_start_smoke" &&
                 automationUiScript_ != "ui_main_menu_options_smoke" &&
                 automationUiScript_ != "ui_main_menu_options_labels_smoke" &&
                 automationUiScript_ != "ui_main_menu_escape_ignored_smoke")
        {
            automationUiInputQueue_.push_back(AutomationUiInputEvent{
                0, AutomationUiInputKind::PointerMove, 48.0, 48.0, 0, 0});
            automationUiInputQueue_.push_back(AutomationUiInputEvent{
                0, AutomationUiInputKind::KeyDown, 0.0, 0.0, GLFW_KEY_SPACE, 0});
        }
    }

    while (automationUiInputCursor_ < automationUiInputQueue_.size() &&
           automationUiInputQueue_[automationUiInputCursor_].frame <= renderedFrameCount_)
    {
        const AutomationUiInputEvent& event = automationUiInputQueue_[automationUiInputCursor_];
        switch (event.kind)
        {
        case AutomationUiInputKind::PointerMove:
        {
#if VOXEL_WITH_RUNTIME_UI
            const ui::UiHitResult hit = refreshRuntimeUiPointerCapture(event.x, event.y);
#else
            const ui::UiHitResult hit{};
#endif
            const InputOwner owner = inputRouter_.routePointerMove(event.x, event.y);
            logInfo("Automation][UIInput",
                    makeLogMessage("script=", automationUiScript_, " frame=", event.frame,
                                   " action=pointer_move owner=", InputRouter::label(owner),
                                   " x=", static_cast<int>(event.x),
                                   " y=", static_cast<int>(event.y),
                                   " hit=", hit.element != nullptr ? hit.element->id
                                                                    : std::string("<none>")));
            break;
        }
        case AutomationUiInputKind::PointerDown:
        case AutomationUiInputKind::PointerUp:
        {
#if VOXEL_WITH_RUNTIME_UI
            const ui::UiHitResult hit = refreshRuntimeUiPointerCapture(event.x, event.y);
#else
            const ui::UiHitResult hit{};
#endif
            const int action = event.kind == AutomationUiInputKind::PointerDown
                                   ? GLFW_PRESS
                                   : GLFW_RELEASE;
            // capture the hit id now: dispatching the click action below can pop an
            // overlay screen, destroying the tree hit.element points into.
            const std::string hitElementId =
                hit.element != nullptr ? hit.element->id : std::string("<none>");
            const InputOwner routedOwner =
                inputRouter_.routePointerButton(event.button, action);
            bool pendingPlacementConfirmHandled = false;
#if VOXEL_WITH_RUNTIME_UI
            if (event.kind == AutomationUiInputKind::PointerDown &&
                event.button == GLFW_MOUSE_BUTTON_LEFT && hit.element == nullptr &&
                routedOwner == InputOwner::Game)
            {
                pendingPlacementConfirmHandled = confirmRuntimeUiPendingPlacement(
                    "automation", automationUiScript_.c_str(), event.frame);
            }
#endif
            const InputOwner owner =
                pendingPlacementConfirmHandled
                    ? InputOwner::RuntimeUi
                    : routedOwner;
            if (!pendingPlacementConfirmHandled && owner == InputOwner::RuntimeUi)
            {
#if VOXEL_WITH_RUNTIME_UI
                if (event.kind == AutomationUiInputKind::PointerDown)
                {
                    runtimeUiContext_.handlePointerDown(hit);
                }
                else
                {
                    runtimeUiContext_.handlePointerUp(hit);
                    dispatchRuntimeUiAction(runtimeUiContext_.consumeActionEvent(),
                                            automationUiScript_.c_str(), event.frame);
                }
#endif
            }
            logInfo("Automation][UIInput",
                    makeLogMessage("script=", automationUiScript_, " frame=", event.frame,
                                   " action=",
                                   event.kind == AutomationUiInputKind::PointerDown
                                       ? "pointer_down"
                                       : "pointer_up",
                                   " owner=", InputRouter::label(owner),
                                   " x=", static_cast<int>(event.x),
                                   " y=", static_cast<int>(event.y),
                                   " hit=", hitElementId));
            break;
        }
        case AutomationUiInputKind::Wheel:
        {
#if VOXEL_WITH_RUNTIME_UI
            const ui::UiHitResult hit = refreshRuntimeUiPointerCapture(event.x, event.y);
            const bool handled = handleRuntimeUiScroll(event.x, event.y, event.scrollY);
#else
            const ui::UiHitResult hit{};
            const bool handled = false;
#endif
            const InputOwner owner =
                handled ? InputOwner::RuntimeUi
                        : inputRouter_.routePointerMove(event.x, event.y);
            logInfo("Automation][UIInput",
                    makeLogMessage("script=", automationUiScript_, " frame=", event.frame,
                                   " action=wheel owner=", InputRouter::label(owner),
                                   " x=", static_cast<int>(event.x),
                                   " y=", static_cast<int>(event.y),
                                   " scroll_y=",
                                   static_cast<int>(event.scrollY),
                                   " handled=", handled ? 1 : 0,
                                   " hit=", hit.element != nullptr ? hit.element->id
                                                                    : std::string("<none>")));
            break;
        }
        case AutomationUiInputKind::KeyDown:
        {
            bool shortcutHandled = false;
#if VOXEL_WITH_RUNTIME_UI
            inputRouter_.setRuntimeUiModalCapture(runtimeUiModalInputActive());
#endif
            const InputOwner routedOwner =
                inputRouter_.routeKey(event.key, GLFW_PRESS);
#if VOXEL_WITH_RUNTIME_UI
            if (routedOwner == InputOwner::RuntimeUi)
            {
                if (event.key == GLFW_KEY_ESCAPE)
                {
                    shortcutHandled = toggleRuntimeUiPauseScreen();
                }
            }
            else if (event.key == GLFW_KEY_ESCAPE)
            {
                shortcutHandled = cancelRuntimeUiPendingPlacement(
                    "escape", automationUiScript_.c_str(), event.frame);
                if (!shortcutHandled)
                {
                    shortcutHandled = toggleRuntimeUiPauseScreen();
                }
            }
            else if (event.key == GLFW_KEY_R)
            {
                shortcutHandled = rotateRuntimeUiPendingPlacement(
                    "keyboard", automationUiScript_.c_str(), event.frame);
            }
#endif
            const InputOwner owner =
                shortcutHandled ? InputOwner::RuntimeUi
                                : routedOwner;
            const char* keyLabel = event.key == GLFW_KEY_ESCAPE
                                       ? "ESCAPE"
                                       : event.key == GLFW_KEY_R ? "R" : "SPACE";
            logInfo("Automation][UIInput",
                    makeLogMessage("script=", automationUiScript_, " frame=", event.frame,
                                   " action=key_down owner=", InputRouter::label(owner),
                                   " key=", keyLabel,
                                   " handled=", shortcutHandled ? 1 : 0));
            break;
        }
        }
        ++automationUiInputCursor_;
    }

    if (automationUiInputCursor_ == automationUiInputQueue_.size())
    {
        automationUiScriptCompleted_ = true;
        logInfo("Automation][UIScript",
                makeLogMessage("script=", automationUiScript_, " completed=1"));
    }
}

#if VOXEL_WITH_RUNTIME_UI
void App::syncRuntimeUiOptionsState(ui::UiTree& tree) const
{
    ui::UiMainMenuOptionsState state{};
    state.fishWorldLabelsVisible = runtimeUiFishWorldLabelsVisible_;
    ui::setMainMenuOptionsState(tree, state);
}

void App::activateRuntimeUiShowcaseCamera(const char* reason)
{
    if (runtimeUiShowcaseCameraActive_)
    {
        return;
    }

    resetFishFocusForSessionTransition();
    runtimeUiShowcaseSavedCamera_ = camera_;

    const glm::vec3 kShowcaseCameraPosition{18.0f, 42.0f, 110.0f};
    const glm::vec3 kShowcaseCameraTarget{-6.0f, 9.0f, 0.0f};
    constexpr float kShowcaseFovY = glm::radians(30.0f);

    setCameraLookAt(camera_, kShowcaseCameraPosition, kShowcaseCameraTarget);
    camera_.projectionMode = CameraProjectionMode::Perspective;
    camera_.fovY = kShowcaseFovY;
    runtimeUiShowcaseCameraActive_ = true;

    postFxSettings_.taaResetHistory_ = true;
    shadowSettings_.shadowResetHistory_ = true;
    shadowSettings_.localShadowResetHistory_ = true;
    aoSettings_.aoResetHistory_ = true;

    logInfo("RuntimeUI",
            makeLogMessage("Showcase camera activated projection=perspective reason=",
                           reason != nullptr && reason[0] != '\0' ? reason : "runtime_ui"));
}

void App::deactivateRuntimeUiShowcaseCamera(const char* reason)
{
    if (!runtimeUiShowcaseCameraActive_)
    {
        return;
    }

    camera_ = runtimeUiShowcaseSavedCamera_;
    runtimeUiShowcaseCameraActive_ = false;

    postFxSettings_.taaResetHistory_ = true;
    shadowSettings_.shadowResetHistory_ = true;
    shadowSettings_.localShadowResetHistory_ = true;
    aoSettings_.aoResetHistory_ = true;

    logInfo("RuntimeUI",
            makeLogMessage("Showcase camera restored projection=perspective reason=",
                           reason != nullptr && reason[0] != '\0' ? reason : "runtime_ui"));
}

void App::syncRuntimeUiShowcaseCameraForActiveScreen(const char* reason)
{
    const std::string screenId = runtimeUiContext_.activeScreenId();
    const bool wantsShowcaseCamera =
        appMode_ != AppMode::Editor &&
        (screenId == ui::kMainMenuScreenId || screenId == ui::kMainMenuOptionsScreenId);
    if (wantsShowcaseCamera)
    {
        activateRuntimeUiShowcaseCamera(reason);
    }
    else
    {
        deactivateRuntimeUiShowcaseCamera(reason);
    }
}

void App::registerRuntimeUiActions()
{
    runtimeUiActionDispatcher_.clear();
    runtimeUiOverlaySmokeController_.registerActions(runtimeUiActionDispatcher_);
    runtimeUiActionDispatcher_.registerHandler(
        std::string(ui::kFocusFishAction), [this](const ui::UiActionEvent& event) {
            const uint64_t fishId = static_cast<uint64_t>(
                std::strtoull(event.payload.c_str(), nullptr, 10));
            focusOnFish(fishId);
        });
    runtimeUiActionDispatcher_.registerHandler(
        std::string(ui::kReleaseFishFocusAction), [this](const ui::UiActionEvent&) {
            releaseFishFocus();
        });
    runtimeUiActionDispatcher_.registerHandler(
        std::string(ui::kResumeGameAction), [this](const ui::UiActionEvent&) {
            if (runtimeUiContext_.topScreenId() == ui::kPauseScreenId)
            {
                runtimeUiContext_.popScreen();
                logInfo("RuntimeUI",
                        makeLogMessage("Screen popped id=", ui::kPauseScreenId,
                                       " depth=", runtimeUiContext_.overlayCount()));
            }
        });
    runtimeUiActionDispatcher_.registerHandler(
        std::string(ui::kReturnMainMenuAction), [this](const ui::UiActionEvent&) {
            resetRuntimeUiTransientSessionState("return_to_main_menu");
            runtimeUiContext_.setActiveScreen(ui::kMainMenuScreenId);
            runtimeUiMainMenuLogged_ = false;
            syncRuntimeUiShowcaseCameraForActiveScreen("return_to_main_menu");
            logInfo("RuntimeUI",
                    makeLogMessage("Pause main menu selected next_screen=",
                                   ui::kMainMenuScreenId));
        });
    runtimeUiActionDispatcher_.registerHandler(
        std::string(ui::kStartAquariumAction), [this](const ui::UiActionEvent&) {
            resetRuntimeUiTransientSessionState("start_aquarium");
            runtimeUiContext_.setActiveScreen(ui::kOverlaySmokeScreenId);
            runtimeUiMainMenuLogged_ = false;
            deactivateRuntimeUiShowcaseCamera("start_aquarium");
            logInfo("RuntimeUI",
                    makeLogMessage("Main menu start selected next_screen=",
                                   ui::kOverlaySmokeScreenId));
        });
    runtimeUiActionDispatcher_.registerHandler(
        std::string(ui::kOpenMainMenuOptionsAction), [this](const ui::UiActionEvent&) {
            if (runtimeUiContext_.topScreenId() == ui::kMainMenuOptionsScreenId)
            {
                return;
            }
            if (runtimeUiContext_.pushScreen(ui::kMainMenuOptionsScreenId))
            {
                logInfo("RuntimeUI",
                        makeLogMessage("Screen pushed id=", ui::kMainMenuOptionsScreenId,
                                       " depth=", runtimeUiContext_.overlayCount()));
            }
        });
    runtimeUiActionDispatcher_.registerHandler(
        std::string(ui::kCloseMainMenuOptionsAction), [this](const ui::UiActionEvent&) {
            if (runtimeUiContext_.topScreenId() == ui::kMainMenuOptionsScreenId)
            {
                runtimeUiContext_.popScreen();
                logInfo("RuntimeUI",
                        makeLogMessage("Screen popped id=",
                                       ui::kMainMenuOptionsScreenId,
                                       " depth=", runtimeUiContext_.overlayCount()));
            }
        });
    runtimeUiActionDispatcher_.registerHandler(
        std::string(ui::kToggleFishWorldLabelsAction), [this](const ui::UiActionEvent&) {
            runtimeUiFishWorldLabelsVisible_ = !runtimeUiFishWorldLabelsVisible_;
            if (ui::UiTree* tree = runtimeUiContext_.topTree())
            {
                syncRuntimeUiOptionsState(*tree);
            }
            logInfo("RuntimeUI",
                    makeLogMessage("Options labels toggled visible=",
                                   runtimeUiFishWorldLabelsVisible_ ? 1 : 0));
        });
    registerCareRuntimeUiActions();
}

void App::dispatchRuntimeUiAction(const ui::UiActionEvent& event, const char* source,
                                  uint64_t automationFrame)
{
    if (event.elementId.empty())
    {
        return;
    }

    const bool pendingPlacementWasActive =
        runtimeUiOverlaySmokeController_.pendingPlacementActive();
    const ui::UiActionDispatchResult result =
        runtimeUiActionDispatcher_.dispatch(event);
    const bool placementSceneInvalidated =
        result.handled &&
        (event.action == std::string(ui::kCancelBuildPlacementAction) ||
         event.action == std::string(ui::kCommitBuildPlacementAction) ||
         event.action == std::string(ui::kRemoveBuildPlacementAction) ||
         (event.action == std::string(ui::kCloseBuildCatalogAction) &&
          pendingPlacementWasActive));
    if (placementSceneInvalidated)
    {
        sceneObjectsDirty_ = true;
        logInfo("RuntimeUI",
                makeLogMessage("Placement scene invalidated element=",
                               event.elementId,
                               " action=", event.action,
                               " source=",
                               source != nullptr && source[0] != '\0'
                                   ? source
                                   : "mouse"));
    }
    const ui::OverlaySmokeScreenState& screenState =
        runtimeUiOverlaySmokeController_.state();
    const std::string selectedCatalogItem =
        runtimeUiOverlaySmokeController_.selectedCatalogItemLogValue();
    const std::string selectedCatalogKey =
        runtimeUiOverlaySmokeController_.selectedCatalogKeyLogValue();
    const std::string pendingBuildItemKey =
        runtimeUiOverlaySmokeController_.pendingBuildItemKeyLogValue();
    const char* buildModeState =
        runtimeUiOverlaySmokeController_.buildModeStateLogValue();

    if (source != nullptr && source[0] != '\0')
    {
        if (event.elementId != std::string(ui::kPendingPlacementShortcutId) &&
            event.elementId != std::string(ui::kPendingPlacementWorldId))
        {
            logInfo("Automation][UIClick",
                    makeLogMessage("script=", source, " frame=", automationFrame,
                                   " clicked=", event.elementId));
        }
        if (!event.action.empty())
        {
            logInfo("Automation][UIAction",
                    makeLogMessage("script=", source, " frame=", automationFrame,
                                   " element=", event.elementId,
                                   " action=", event.action));
            logInfo("Automation][UIActionHandled",
                    makeLogMessage("script=", source, " frame=", automationFrame,
                                   " element=", result.elementId,
                                   " action=", result.action,
                                   " handled=", result.handled ? 1 : 0,
                                   " build_catalog_open=",
                                   screenState.buildCatalogOpen ? 1 : 0,
                                   " selected_catalog_item=", selectedCatalogItem,
                                   " selected_catalog_key=", selectedCatalogKey,
                                   " payload=",
                                   result.payload.empty() ? std::string("<none>")
                                                          : result.payload,
                                   " pending_build_item_key=", pendingBuildItemKey,
                                   " build_mode_state=", buildModeState));
        }
        return;
    }

    if (!event.action.empty())
    {
        logInfo("RuntimeUI",
                makeLogMessage("Action source=mouse element=", event.elementId,
                               " action=", event.action,
                               " handled=", result.handled ? 1 : 0,
                               " build_catalog_open=",
                               screenState.buildCatalogOpen ? 1 : 0,
                               " selected_catalog_item=", selectedCatalogItem,
                               " selected_catalog_key=", selectedCatalogKey,
                               " payload=",
                               result.payload.empty() ? std::string("<none>")
                                                      : result.payload,
                               " pending_build_item_key=", pendingBuildItemKey,
                               " build_mode_state=", buildModeState));
    }
}

bool App::cancelRuntimeUiPendingPlacement(const char* source,
                                          const char* automationScript,
                                          uint64_t automationFrame)
{
    if (appMode_ == AppMode::Editor ||
        runtimeUiContext_.activeScreenId() != ui::kOverlaySmokeScreenId ||
        runtimeUiContext_.overlayCount() != 0 ||
        !runtimeUiOverlaySmokeController_.pendingPlacementActive())
    {
        return false;
    }

    ui::UiActionEvent event{};
    event.elementId = std::string(ui::kPendingPlacementShortcutId);
    event.action = std::string(ui::kCancelBuildPlacementAction);

    if (automationScript != nullptr && automationScript[0] != '\0')
    {
        dispatchRuntimeUiAction(event, automationScript, automationFrame);
    }
    else
    {
        const ui::UiActionDispatchResult result =
            runtimeUiActionDispatcher_.dispatch(event);
        logInfo("RuntimeUI",
                makeLogMessage("Pending placement cancel source=",
                               source != nullptr ? source : "shortcut",
                               " element=", result.elementId,
                               " action=", result.action,
                               " handled=", result.handled ? 1 : 0,
                               " build_catalog_open=",
                               runtimeUiOverlaySmokeController_.state()
                                       .buildCatalogOpen
                                   ? 1
                                   : 0,
                               " selected_catalog_item=",
                               runtimeUiOverlaySmokeController_
                                   .selectedCatalogItemLogValue(),
                               " selected_catalog_key=",
                               runtimeUiOverlaySmokeController_
                                   .selectedCatalogKeyLogValue(),
                               " pending_build_item_key=",
                               runtimeUiOverlaySmokeController_
                                   .pendingBuildItemKeyLogValue(),
                               " build_mode_state=",
                               runtimeUiOverlaySmokeController_
                                   .buildModeStateLogValue()));
    }

    runtimeUiContext_.clearPointerState();
    sceneObjectsDirty_ = true;
    return true;
}

bool App::rotateRuntimeUiPendingPlacement(const char* source,
                                          const char* automationScript,
                                          uint64_t automationFrame)
{
    if (appMode_ == AppMode::Editor ||
        runtimeUiContext_.activeScreenId() != ui::kOverlaySmokeScreenId ||
        runtimeUiContext_.overlayCount() != 0)
    {
        return false;
    }

    const bool handled =
        runtimeUiOverlaySmokeController_.rotatePendingPlacement();
    if (!handled)
    {
        return false;
    }

    sceneObjectsDirty_ = true;
    if (automationScript != nullptr && automationScript[0] != '\0')
    {
        logInfo("Automation][UIAction",
                makeLogMessage("script=", automationScript, " frame=", automationFrame,
                               " element=", ui::kPendingPlacementShortcutId,
                               " action=rotate_build_placement"));
        logInfo("Automation][UIActionHandled",
                makeLogMessage("script=", automationScript, " frame=", automationFrame,
                               " element=", ui::kPendingPlacementShortcutId,
                               " action=rotate_build_placement handled=1",
                               " build_catalog_open=",
                               runtimeUiOverlaySmokeController_.state()
                                       .buildCatalogOpen
                                   ? 1
                                   : 0,
                               " selected_catalog_item=",
                               runtimeUiOverlaySmokeController_
                                   .selectedCatalogItemLogValue(),
                               " selected_catalog_key=",
                               runtimeUiOverlaySmokeController_
                                   .selectedCatalogKeyLogValue(),
                               " payload=<none>",
                               " pending_build_item_key=",
                               runtimeUiOverlaySmokeController_
                                   .pendingBuildItemKeyLogValue(),
                               " build_mode_state=",
                               runtimeUiOverlaySmokeController_
                                   .buildModeStateLogValue(),
                               " rotation_steps=",
                               runtimeUiOverlaySmokeController_
                                   .placementRotationSteps()));
    }
    else
    {
        logInfo("RuntimeUI",
                makeLogMessage("Pending placement rotate source=",
                               source != nullptr ? source : "shortcut",
                               " handled=1",
                               " build_catalog_open=",
                               runtimeUiOverlaySmokeController_.state()
                                       .buildCatalogOpen
                                   ? 1
                                   : 0,
                               " selected_catalog_item=",
                               runtimeUiOverlaySmokeController_
                                   .selectedCatalogItemLogValue(),
                               " selected_catalog_key=",
                               runtimeUiOverlaySmokeController_
                                   .selectedCatalogKeyLogValue(),
                               " pending_build_item_key=",
                               runtimeUiOverlaySmokeController_
                                   .pendingBuildItemKeyLogValue(),
                               " build_mode_state=",
                               runtimeUiOverlaySmokeController_
                                   .buildModeStateLogValue(),
                               " rotation_steps=",
                               runtimeUiOverlaySmokeController_
                                   .placementRotationSteps()));
    }
    return true;
}

bool App::confirmRuntimeUiPendingPlacement(const char* source,
                                           const char* automationScript,
                                           uint64_t automationFrame)
{
    if (appMode_ == AppMode::Editor ||
        runtimeUiContext_.activeScreenId() != ui::kOverlaySmokeScreenId ||
        runtimeUiContext_.overlayCount() != 0 ||
        !runtimeUiOverlaySmokeController_.pendingPlacementActive())
    {
        return false;
    }

    const ui::OverlaySmokeScreenState& screenState =
        runtimeUiOverlaySmokeController_.state();
    const bool removalMode =
        screenState.buildModeState == ui::BuildModeState::PendingRemoval;
    const ui::BuildPlacementProbe probe = screenState.placementProbe;
    const std::string itemKey =
        probe.itemKey.empty() ? screenState.pendingBuildItemKey : probe.itemKey;
    const std::string payload = itemKey.empty() ? std::string("<none>") : itemKey;
    const std::string invalidReason =
        probe.invalidReason.empty() ? std::string("<none>") : probe.invalidReason;
    const bool automation =
        automationScript != nullptr && automationScript[0] != '\0';
    const std::string action =
        removalMode ? std::string(ui::kRemoveBuildPlacementAction)
                    : std::string(ui::kCommitBuildPlacementAction);

    auto logAutomationAction = [&](bool handled) {
        if (!automation)
        {
            return;
        }
        logInfo("Automation][UIAction",
                makeLogMessage("script=", automationScript, " frame=", automationFrame,
                               " element=", ui::kPendingPlacementWorldId,
                               " action=", action));
        logInfo("Automation][UIActionHandled",
                makeLogMessage("script=", automationScript, " frame=", automationFrame,
                               " element=", ui::kPendingPlacementWorldId,
                               " action=", action,
                               " handled=", handled ? 1 : 0,
                               " build_catalog_open=",
                               screenState.buildCatalogOpen ? 1 : 0,
                               " selected_catalog_item=",
                               runtimeUiOverlaySmokeController_
                                   .selectedCatalogItemLogValue(),
                               " selected_catalog_key=",
                               runtimeUiOverlaySmokeController_
                                   .selectedCatalogKeyLogValue(),
                               " payload=", payload,
                               " pending_build_item_key=",
                               runtimeUiOverlaySmokeController_
                                   .pendingBuildItemKeyLogValue(),
                               " build_mode_state=",
                               runtimeUiOverlaySmokeController_
                                   .buildModeStateLogValue()));
    };

    auto logConfirm = [&](bool accepted, const char* reason, BlockId block,
                          std::string placeablePrototype = "none") {
        logInfo("RuntimeUI",
                makeLogMessage(removalMode ? "Pending removal confirm source="
                                           : "Pending placement confirm source=",
                               source != nullptr ? source : "mouse",
                               " item_key=", payload,
                               " accepted=", accepted ? 1 : 0,
                               " reason=", reason,
                               " active=", probe.active ? 1 : 0,
                               " ray_hit=", probe.rayHit ? 1 : 0,
                               " candidate=",
                               probe.hasPlacementCandidate ? 1 : 0,
                               " snapped=", probe.snappedToGrid ? 1 : 0,
                               " valid=", probe.placementValid ? 1 : 0,
                               " invalid_reason=", invalidReason,
                               " hit_normal=", probe.hitNormal.x, ",",
                               probe.hitNormal.y, ",", probe.hitNormal.z,
                               " placement_voxel=", probe.placementVoxel.x, ",",
                               probe.placementVoxel.y, ",", probe.placementVoxel.z,
                               " rotation_steps=", probe.rotationSteps,
                               " footprint=", probe.footprintVoxels.x, ",",
                               probe.footprintVoxels.y, ",",
                               probe.footprintVoxels.z,
                               " world_center_mm=",
                               static_cast<int>(
                                   std::round(probe.ghostWorldPosition.x * 1000.0f)),
                               ",",
                               static_cast<int>(
                                   std::round(probe.ghostWorldPosition.y * 1000.0f)),
                               ",",
                               static_cast<int>(
                                   std::round(probe.ghostWorldPosition.z * 1000.0f)),
                               " block=", block,
                               " build_catalog_open=",
                               runtimeUiOverlaySmokeController_.state()
                                       .buildCatalogOpen
                                   ? 1
                                   : 0,
                               " pending_build_item_key=",
                               runtimeUiOverlaySmokeController_
                                   .pendingBuildItemKeyLogValue(),
                               " build_mode_state=",
                               runtimeUiOverlaySmokeController_
                                   .buildModeStateLogValue(),
                               " placeable_prototype=", placeablePrototype,
                               " target_placeable_uuid=",
                               probe.targetPlaceableUuid.empty()
                                   ? std::string("<none>")
                                   : probe.targetPlaceableUuid,
                               " target_placeable_prototype=",
                               probe.targetPlaceablePrototype.empty()
                                   ? std::string("none")
                                   : probe.targetPlaceablePrototype));
    };

    const ui::BuildCatalogItemDefinition* itemDefinition =
        ui::findRuntimeBuildCatalogItem(itemKey);
    const BlockId block =
        itemDefinition != nullptr ? itemDefinition->block : BLOCK_AIR;
    const bool prototypePlacement =
        !removalMode && itemDefinition != nullptr &&
        itemDefinition->hasPlaceablePrototype();
    const std::string placeablePrototypeLabel =
        !probe.targetPlaceablePrototype.empty()
            ? probe.targetPlaceablePrototype
            : itemDefinition != nullptr ? runtimePlaceablePrototypeLabel(*itemDefinition)
                                        : std::string("none");
    if (!probe.active || !probe.hasPlacementCandidate || !probe.placementValid)
    {
        logAutomationAction(false);
        logConfirm(false, "invalid_probe", block, placeablePrototypeLabel);
        runtimeUiContext_.clearPointerState();
        return true;
    }

    if (removalMode)
    {
        if (!probe.targetPlaceableUuid.empty())
        {
            PlaceableInstance targetPlaceable{};
            if (!findRuntimePlaceableByUuid(sceneConfig(), probe.targetPlaceableUuid,
                                            targetPlaceable))
            {
                logAutomationAction(false);
                logConfirm(false, "placeable_not_found", BLOCK_AIR,
                           placeablePrototypeLabel);
                runtimeUiContext_.clearPointerState();
                return true;
            }

            PlaceableEditCommand command{};
            command.op = PlaceableEditCommand::Op::Remove;
            command.before = targetPlaceable;
            if (!applyPlaceableEditCommand(command, false))
            {
                logAutomationAction(false);
                logConfirm(false, "placeable_rebuild_failed", BLOCK_AIR,
                           placeablePrototypeLabel);
                runtimeUiContext_.clearPointerState();
                return true;
            }

            if (!gameRuntime_.completeExternalPlaceableEdit(
                    command, "Removed " + targetPlaceable.prototypeSlug + ".", sceneConfigMutable())) { (void)rebuildVolumeScene(); logAutomationAction(false); logConfirm(false, "placeable_finalize_failed", BLOCK_AIR, placeablePrototypeLabel); runtimeUiContext_.clearPointerState(); return true; }
            sceneObjectsDirty_ = true;
            animatedObjectsDirty_ = true;

            ui::UiActionEvent event{};
            event.elementId = std::string(ui::kPendingPlacementWorldId);
            event.action = std::string(ui::kRemoveBuildPlacementAction);
            event.payload = itemKey;
            if (automation)
            {
                dispatchRuntimeUiAction(event, automationScript, automationFrame);
            }
            else
            {
                runtimeUiActionDispatcher_.dispatch(event);
            }

            logInfo("RuntimeUI",
                    makeLogMessage("Runtime placeable removal item_key=", payload,
                                   " prototype=", placeablePrototypeLabel,
                                   " uuid=", targetPlaceable.uuid,
                                   " world_position_mm=",
                                   static_cast<int>(std::round(
                                       targetPlaceable.position.x * 1000.0f)),
                                   ",",
                                   static_cast<int>(std::round(
                                       targetPlaceable.position.y * 1000.0f)),
                                   ",",
                                   static_cast<int>(std::round(
                                       targetPlaceable.position.z * 1000.0f))));
            logConfirm(true, "removed_placeable", BLOCK_AIR,
                       placeablePrototypeLabel);
            runtimeUiContext_.clearPointerState();
            return true;
        }

        const glm::ivec3 target = probe.placementVoxel;
        ui::BuildRemovalEvaluation evaluation{};
        {
            auto lock = voxelGrid_.lockShared();
            evaluation =
                ui::evaluateBuildRemovalCandidate(voxelGrid_, target,
                                                  probe.rayHit);
        }
        if (!evaluation.valid)
        {
            logAutomationAction(false);
            logConfirm(false,
                       ui::buildRemovalInvalidReasonLabel(
                           evaluation.invalidReason),
                       evaluation.block);
            runtimeUiContext_.clearPointerState();
            return true;
        }
        const BlockId removedBlock = evaluation.block;
        if (!engine::voxel::commitBlockRemoval(voxelGrid_, evaluation.targetVoxel,
                                               evaluation.block))
        {
            logAutomationAction(false);
            logConfirm(false, "remove_failed", removedBlock);
            runtimeUiContext_.clearPointerState();
            return true;
        }
        voxelObjectsDirty_ = true;
        sceneObjectsDirty_ = true;

        ui::UiActionEvent event{};
        event.elementId = std::string(ui::kPendingPlacementWorldId);
        event.action = std::string(ui::kRemoveBuildPlacementAction);
        event.payload = itemKey;
        if (automation)
        {
            dispatchRuntimeUiAction(event, automationScript, automationFrame);
        }
        else
        {
            runtimeUiActionDispatcher_.dispatch(event);
        }

        logConfirm(true, "removed", removedBlock);
        runtimeUiContext_.clearPointerState();
        return true;
    }

    const glm::ivec3 target = probe.placementVoxel;
    if (prototypePlacement)
    {
        const FoliagePrototype* prototype =
            FoliageCatalog::findPrototype(itemDefinition->placeablePrototypeSlug,
                                          itemDefinition->placeablePrototypeVersion);
        if (prototype == nullptr)
        {
            logAutomationAction(false);
            logConfirm(false, "unknown_placeable_prototype", block,
                       placeablePrototypeLabel);
            runtimeUiContext_.clearPointerState();
            return true;
        }
        if (!sceneConfig().loadAquariumTest)
        {
            logAutomationAction(false);
            logConfirm(false, "placeable_scene_unavailable", block,
                       placeablePrototypeLabel);
            runtimeUiContext_.clearPointerState();
            return true;
        }

        ui::BuildPlacementEvaluation evaluation{};
        {
            auto lock = voxelGrid_.lockShared();
            evaluation = ui::evaluateBuildPlacementCandidate(
                voxelGrid_, itemKey, target, probe.rayHit, probe.rotationSteps);
        }
        if (!evaluation.valid)
        {
            logAutomationAction(false);
            logConfirm(false,
                       ui::buildPlacementInvalidReasonLabel(
                           evaluation.invalidReason),
                       block, placeablePrototypeLabel);
            runtimeUiContext_.clearPointerState();
            return true;
        }

        PlaceableInstance instance = gameRuntime_.createPlaceableInstance(
            std::string(itemDefinition->placeablePrototypeSlug),
            itemDefinition->placeablePrototypeVersion,
            runtimePlaceablePositionFromProbe(probe));

        const std::vector<PlaceableInstance> existingInstances =
            FoliageCatalog::aquariumHeroFoliageInstances(
                &sceneConfig().placeables, sceneConfig().useDefaultPlaceables);
        if (runtimePlaceableFootprintOverlapsExisting(instance, *prototype,
                                                      existingInstances))
        {
            logAutomationAction(false);
            logConfirm(false, "footprint_occupied", block,
                       placeablePrototypeLabel);
            runtimeUiContext_.clearPointerState();
            return true;
        }

        PlaceableEditCommand command{};
        command.op = PlaceableEditCommand::Op::Add;
        command.after = instance;
        if (!applyPlaceableEditCommand(command, false))
        {
            logAutomationAction(false);
            logConfirm(false, "placeable_rebuild_failed", block,
                       placeablePrototypeLabel);
            runtimeUiContext_.clearPointerState();
            return true;
        }

        if (!gameRuntime_.completeExternalPlaceableEdit(
                command, "Placed " + instance.prototypeSlug + ".", sceneConfigMutable())) { (void)rebuildVolumeScene(); logAutomationAction(false); logConfirm(false, "placeable_finalize_failed", block, placeablePrototypeLabel); runtimeUiContext_.clearPointerState(); return true; }
        sceneObjectsDirty_ = true;
        animatedObjectsDirty_ = true;

        ui::UiActionEvent event{};
        event.elementId = std::string(ui::kPendingPlacementWorldId);
        event.action = std::string(ui::kCommitBuildPlacementAction);
        event.payload = itemKey;
        if (automation)
        {
            dispatchRuntimeUiAction(event, automationScript, automationFrame);
        }
        else
        {
            runtimeUiActionDispatcher_.dispatch(event);
        }

        logInfo("RuntimeUI",
                makeLogMessage("Runtime placeable placement item_key=", payload,
                               " prototype=", placeablePrototypeLabel,
                               " uuid=", instance.uuid,
                               " world_position_mm=",
                               static_cast<int>(
                                   std::round(instance.position.x * 1000.0f)),
                               ",",
                               static_cast<int>(
                                   std::round(instance.position.y * 1000.0f)),
                               ",",
                               static_cast<int>(
                                   std::round(instance.position.z * 1000.0f)),
                               " fallback_block=", block));
        logConfirm(true, "placed_placeable", BLOCK_AIR, placeablePrototypeLabel);
        runtimeUiContext_.clearPointerState();
        return true;
    }

    ui::BuildPlacementEvaluation evaluation{};
    {
        auto lock = voxelGrid_.lockShared();
        evaluation =
            ui::evaluateBuildPlacementCandidate(voxelGrid_, itemKey, target,
                                                probe.rayHit,
                                                probe.rotationSteps);
    }
    if (!evaluation.valid)
    {
        logAutomationAction(false);
        logConfirm(false,
                   ui::buildPlacementInvalidReasonLabel(
                       evaluation.invalidReason),
                   block);
        runtimeUiContext_.clearPointerState();
        return true;
    }
    const std::vector<glm::ivec3> committedCells =
        ui::buildPlacementFootprintCells(*evaluation.item,
                                         evaluation.anchorVoxel,
                                         evaluation.rotationSteps);
    if (!engine::voxel::commitBlockPlacement(voxelGrid_, committedCells,
                                             evaluation.block))
    {
        logAutomationAction(false);
        logConfirm(false, "set_failed", block);
        runtimeUiContext_.clearPointerState();
        return true;
    }

    voxelObjectsDirty_ = true;
    sceneObjectsDirty_ = true;

    ui::UiActionEvent event{};
    event.elementId = std::string(ui::kPendingPlacementWorldId);
    event.action = std::string(ui::kCommitBuildPlacementAction);
    event.payload = itemKey;
    if (automation)
    {
        dispatchRuntimeUiAction(event, automationScript, automationFrame);
    }
    else
    {
        runtimeUiActionDispatcher_.dispatch(event);
    }

    logConfirm(true, "placed", block);
    runtimeUiContext_.clearPointerState();
    return true;
}

void App::buildRuntimeUiDrawList(VkExtent2D extent)
{
    runtimeUiDrawList_.clear();
    if (renderPipelineShowcase_.enabled()) { appendRenderPipelineShowcaseOverlay(extent); return; }
    if (appMode_ == AppMode::Editor || !runtimeUiContext_.hasActiveScreen() ||
        extent.width == 0 || extent.height == 0)
    {
        return;
    }
    const bool isOverlaySmoke =
        runtimeUiContext_.activeScreenId() == ui::kOverlaySmokeScreenId;

    ui::UiTree* tree = ensureRuntimeUiActiveTree();
    if (tree == nullptr)
    {
        return;
    }
    // ensureRuntimeUiActiveTree returns the top of the screen stack; the smoke
    // probes and base paint always target the base screen tree, with overlay
    // screens painted on top afterwards.
    ui::UiTree* baseTree = runtimeUiContext_.activeTree();
    if (baseTree == nullptr)
    {
        return;
    }

    const ui::UiElement* panel =
        isOverlaySmoke ? ui::findElementById(baseTree->root, "test_panel") : nullptr;
    if (isOverlaySmoke && panel == nullptr)
    {
        return;
    }
    const ui::UiElement* contentStack =
        ui::findElementById(baseTree->root, "content_stack");
    const ui::UiElement* secondaryPanel =
        ui::findElementById(baseTree->root, "secondary_panel");
    const ui::UiElement* secondaryStack =
        ui::findElementById(baseTree->root, "secondary_stack");
    const ui::UiElement* secondaryMetricA =
        ui::findElementById(baseTree->root, "secondary_metric_a");
    const ui::UiElement* fishWorldLabelLayer =
        ui::findElementById(baseTree->root, ui::kFishWorldLabelLayerId);
    const ui::UiElement* buildCatalogPanel =
        ui::findElementById(baseTree->root, "build_catalog_panel");
    const ui::UiElement* buildCatalogViewport =
        ui::findElementById(baseTree->root, ui::kBuildCatalogListViewportId);
    const ui::UiElement* buildGhostPreview =
        ui::findElementById(baseTree->root, ui::kBuildGhostPreviewId);
    const ui::UiElement* buildGhostPreviewLabel =
        ui::findElementById(baseTree->root, ui::kBuildGhostPreviewLabelId);

    const ui::UiPointerState& pointerState = runtimeUiContext_.pointerState();
    const ui::UiPaintOptions paintOptions{pointerState.hoveredElementId,
                                          pointerState.pressedElementId};
    const ui::UiPaintStats paintStats =
        ui::paintTree(baseTree->root, runtimeUiDebugFont_, runtimeUiDrawList_,
                      paintOptions);
    for (ui::UiTree* overlayTree : runtimeUiContext_.overlayTrees())
    {
        ui::paintTree(overlayTree->root, runtimeUiDebugFont_, runtimeUiDrawList_,
                      paintOptions);
    }

    if (runtimeUiContext_.activeScreenId() == ui::kMainMenuScreenId &&
        !runtimeUiMainMenuLogged_ && paintStats.glyphCount > 0)
    {
        const ui::UiElement* menuPanel =
            ui::findElementById(baseTree->root, "main_menu_panel");
        const ui::UiElement* startButton =
            ui::findElementById(baseTree->root, ui::kMainMenuStartButtonId);
        const ui::UiElement* optionsButton =
            ui::findElementById(baseTree->root, ui::kMainMenuOptionsButtonId);
        if (menuPanel != nullptr && startButton != nullptr && optionsButton != nullptr)
        {
            const ui::UiRect& panelRect = menuPanel->computedRect;
            const ui::UiRect& startRect = startButton->computedRect;
            const ui::UiRect& optionsRect = optionsButton->computedRect;
            logInfo("RuntimeUI",
                    makeLogMessage("Main menu rendered panel=main_menu_panel rect=",
                                   static_cast<int>(panelRect.x), ",",
                                   static_cast<int>(panelRect.y), ",",
                                   static_cast<int>(panelRect.width), ",",
                                   static_cast<int>(panelRect.height),
                                   " start=", startButton->id,
                                   " start_rect=", static_cast<int>(startRect.x), ",",
                                   static_cast<int>(startRect.y), ",",
                                   static_cast<int>(startRect.width), ",",
                                   static_cast<int>(startRect.height),
                                   " options=", optionsButton->id,
                                   " options_rect=", static_cast<int>(optionsRect.x), ",",
                                   static_cast<int>(optionsRect.y), ",",
                                   static_cast<int>(optionsRect.width), ",",
                                   static_cast<int>(optionsRect.height),
                                   " overlays=", runtimeUiContext_.overlayCount()));
            runtimeUiMainMenuLogged_ = true;
        }
    }

    if (isOverlaySmoke && panel != nullptr)
    {
    if (runtimeUiOverlaySmokeController_.shouldLogBuildCatalogShell() &&
        buildCatalogPanel != nullptr && buildCatalogPanel->visible)
    {
        const ui::UiRect& catalogRect = buildCatalogPanel->computedRect;
        logInfo("RuntimeUI",
                makeLogMessage("Build catalog panel shell visible=1 enabled=",
                               buildCatalogPanel->enabled ? 1 : 0,
                               " rect=", static_cast<int>(catalogRect.x), ",",
                               static_cast<int>(catalogRect.y), ",",
                               static_cast<int>(catalogRect.width), ",",
                               static_cast<int>(catalogRect.height),
                               " close=build_catalog_close"));
        runtimeUiOverlaySmokeController_.markBuildCatalogShellLogged();
    }

    if (runtimeUiOverlaySmokeController_.shouldLogBuildCatalogScroll() &&
        buildCatalogViewport != nullptr && buildCatalogViewport->visible)
    {
        const ui::UiRect& viewportRect = buildCatalogViewport->computedRect;
        logInfo("RuntimeUI",
                makeLogMessage("Build catalog scroll viewport=",
                               ui::kBuildCatalogListViewportId,
                               " rect=", static_cast<int>(viewportRect.x), ",",
                               static_cast<int>(viewportRect.y), ",",
                               static_cast<int>(viewportRect.width), ",",
                               static_cast<int>(viewportRect.height),
                               " offset=",
                               static_cast<int>(std::round(
                                   runtimeUiOverlaySmokeController_
                                       .buildCatalogScrollOffsetY())),
                               " max_offset=",
                               static_cast<int>(std::round(
                                   ui::overlaySmokeBuildCatalogMaxScrollOffset()))));
        runtimeUiOverlaySmokeController_.markBuildCatalogScrollLogged();
    }

    if (runtimeUiOverlaySmokeController_.shouldLogBuildGhostPreview() &&
        buildGhostPreview != nullptr &&
        buildGhostPreview->visible)
    {
        const ui::UiRect& ghostRect = buildGhostPreview->computedRect;
        logInfo("RuntimeUI",
                makeLogMessage("Build ghost preview visible=1 enabled=",
                               buildGhostPreview->enabled ? 1 : 0,
                               " rect=", static_cast<int>(ghostRect.x), ",",
                               static_cast<int>(ghostRect.y), ",",
                               static_cast<int>(ghostRect.width), ",",
                               static_cast<int>(ghostRect.height),
                               " item_key=",
                               runtimeUiOverlaySmokeController_
                                   .pendingBuildItemKeyLogValue(),
                               " label=",
                               buildGhostPreviewLabel != nullptr
                                   ? buildGhostPreviewLabel->text.value
                                   : std::string("<none>")));
        runtimeUiOverlaySmokeController_.markBuildGhostPreviewLogged();
    }

    if (runtimeUiOverlaySmokeController_.shouldLogBuildPlacementProbe())
    {
        const ui::BuildPlacementProbe& probe =
            runtimeUiOverlaySmokeController_.state().placementProbe;
        const int distanceMm =
            static_cast<int>(std::round(probe.distance * 1000.0f));
        logInfo("RuntimeUI",
                makeLogMessage("Build placement probe active=", probe.active ? 1 : 0,
                               " item_key=",
                               probe.itemKey.empty() ? std::string("<none>")
                                                     : probe.itemKey,
                               " ray_hit=", probe.rayHit ? 1 : 0,
                               " candidate=", probe.hasPlacementCandidate ? 1 : 0,
                               " snapped=", probe.snappedToGrid ? 1 : 0,
                               " valid=", probe.placementValid ? 1 : 0,
                               " invalid_reason=",
                               probe.invalidReason.empty() ? std::string("<none>")
                                                           : probe.invalidReason,
                               " hit_voxel=", probe.hitVoxel.x, ",", probe.hitVoxel.y,
                               ",", probe.hitVoxel.z,
                               " hit_normal=", probe.hitNormal.x, ",",
                               probe.hitNormal.y, ",", probe.hitNormal.z,
                               " placement_voxel=", probe.placementVoxel.x, ",",
                               probe.placementVoxel.y, ",", probe.placementVoxel.z,
                               " rotation_steps=", probe.rotationSteps,
                               " footprint=", probe.footprintVoxels.x, ",",
                               probe.footprintVoxels.y, ",",
                               probe.footprintVoxels.z,
                               " target_placeable_uuid=",
                               probe.targetPlaceableUuid.empty()
                                   ? std::string("<none>")
                                   : probe.targetPlaceableUuid,
                               " target_placeable_prototype=",
                               probe.targetPlaceablePrototype.empty()
                                   ? std::string("none")
                                   : probe.targetPlaceablePrototype,
                               " distance_mm=", distanceMm));
        runtimeUiOverlaySmokeController_.markBuildPlacementProbeLogged();
    }

    const ui::UiRect& panelRect = panel->computedRect;
    const ui::UiScissor panelScissor{
        static_cast<int32_t>(panelRect.x),
        static_cast<int32_t>(panelRect.y),
        static_cast<uint32_t>(panelRect.width),
        static_cast<uint32_t>(panelRect.height),
    };

    if (!runtimeUiOverlayTextLogged_ && paintStats.glyphCount > 0)
    {
        logInfo("RuntimeUI",
                makeLogMessage("Overlay smoke retained tree painted elements=",
                               paintStats.elements,
                               " rect_elements=", paintStats.rectElements,
                               " text_elements=", paintStats.textElements,
                               " glyphs=", paintStats.glyphCount,
                               " root=", tree->root.id,
                               " panel=", panel->id));
        if (contentStack != nullptr)
        {
            const ui::UiRect& contentRect = contentStack->computedRect;
            logInfo("RuntimeUI",
                    makeLogMessage("Overlay smoke retained layout root=", panel->id,
                                   " content=", contentStack->id,
                                   " mode=", ui::layoutModeLabel(contentStack->layoutMode),
                                   " align=",
                                   ui::crossAxisAlignLabel(contentStack->crossAxisAlign),
                                   " rect=", static_cast<int>(contentRect.x), ",",
                                   static_cast<int>(contentRect.y), ",",
                                   static_cast<int>(contentRect.width), ",",
                                   static_cast<int>(contentRect.height),
                                   " spacing=", static_cast<int>(contentStack->spacing),
                                   " padding=", static_cast<int>(contentStack->padding.left),
                                   ",", static_cast<int>(contentStack->padding.top), ",",
                                   static_cast<int>(contentStack->padding.right), ",",
                                   static_cast<int>(contentStack->padding.bottom)));
        }

        if (secondaryPanel != nullptr && secondaryStack != nullptr &&
            secondaryMetricA != nullptr)
        {
            const ui::UiRect& secondaryRect = secondaryPanel->computedRect;
            const ui::UiRect& secondaryStackRect = secondaryStack->computedRect;
            const ui::UiRect& metricRect = secondaryMetricA->computedRect;
            logInfo("RuntimeUI",
                    makeLogMessage("Overlay smoke secondary layout panel=",
                                   secondaryPanel->id,
                                   " rect=", static_cast<int>(secondaryRect.x), ",",
                                   static_cast<int>(secondaryRect.y), ",",
                                   static_cast<int>(secondaryRect.width), ",",
                                   static_cast<int>(secondaryRect.height),
                                   " stack=", secondaryStack->id,
                                   " mode=",
                                   ui::layoutModeLabel(secondaryStack->layoutMode),
                                   " align=",
                                   ui::crossAxisAlignLabel(
                                       secondaryStack->crossAxisAlign),
                                   " rect=", static_cast<int>(secondaryStackRect.x),
                                   ",", static_cast<int>(secondaryStackRect.y), ",",
                                   static_cast<int>(secondaryStackRect.width), ",",
                                   static_cast<int>(secondaryStackRect.height),
                                   " first_child=", secondaryMetricA->id,
                                   " child_rect=", static_cast<int>(metricRect.x),
                                   ",", static_cast<int>(metricRect.y), ",",
                                   static_cast<int>(metricRect.width), ",",
                                   static_cast<int>(metricRect.height)));
        }

        const auto textLayoutFor = [](const ui::UiElement& element) {
            ui::TextLayoutOptions layout = element.text.layout;
            if (layout.maxWidth <= 0.0f && element.computedRect.width > 0.0f)
            {
                layout.maxWidth = element.computedRect.width;
            }
            return layout;
        };

        const ui::UiElement* title =
            ui::findElementById(tree->root, "title");
        if (title != nullptr)
        {
            const ui::DebugTextResult textResult =
                ui::measureDebugText(runtimeUiDebugFont_, title->text.value,
                                     title->text.pixelHeight, textLayoutFor(*title));
            logInfo("RuntimeUI",
                    makeLogMessage("Overlay smoke debug text emitted glyphs=",
                                   textResult.glyphCount,
                                   " lines=", textResult.lineCount,
                                   " width=", static_cast<int>(textResult.width),
                                   " height=", static_cast<int>(textResult.height)));
        }

        const std::array<const char*, 6> statusTextIds{
            "status_row_fish_label",  "status_row_fish_value",
            "status_row_time_label",  "status_row_time_value",
            "status_row_clean_label", "status_row_clean_value",
        };
        uint32_t statusGlyphs = 0;
        uint32_t statusTextCount = 0;
        bool statusRowsInsidePanel = true;
        for (const char* statusTextId : statusTextIds)
        {
            const ui::UiElement* statusText =
                ui::findElementById(tree->root, statusTextId);
            if (statusText == nullptr)
            {
                statusRowsInsidePanel = false;
                continue;
            }

            const ui::TextLayoutOptions statusOptions = textLayoutFor(*statusText);
            const ui::DebugTextResult statusResult =
                ui::measureDebugText(runtimeUiDebugFont_, statusText->text.value,
                                     statusText->text.pixelHeight, statusOptions);
            const float statusWidth =
                statusOptions.maxWidth > 0.0f ? statusOptions.maxWidth
                                              : statusResult.width;
            statusRowsInsidePanel =
                statusRowsInsidePanel &&
                statusText->computedRect.x >= panelRect.x &&
                statusText->computedRect.y >= panelRect.y &&
                (statusText->computedRect.x + statusWidth) <=
                    (panelRect.x + panelRect.width) &&
                (statusText->computedRect.y + statusResult.height) <=
                    (panelRect.y + panelRect.height);
            statusGlyphs += statusResult.glyphCount;
            ++statusTextCount;
        }
        if (statusTextCount > 0)
        {
            logInfo("RuntimeUI",
                    makeLogMessage("Overlay smoke status rows emitted glyphs=",
                                   statusGlyphs,
                                   " text_elements=", statusTextCount,
                                   " rows=3 first=status_row_fish_label",
                                   " inside_panel=", statusRowsInsidePanel ? 1 : 0));
        }

        logInfo("RuntimeUI",
                makeLogMessage("Overlay smoke retained tree commands_added=",
                               paintStats.commandCountAfter -
                                   paintStats.commandCountBefore));
        runtimeUiOverlayTextLogged_ = true;
    }

    if (!runtimeUiWorldLabelsLogged_ && fishWorldLabelLayer != nullptr &&
        !fishWorldLabelLayer->children.empty())
    {
        const ui::UiElement& firstLabel = fishWorldLabelLayer->children.front();
        const ui::UiRect& layerRect = fishWorldLabelLayer->computedRect;
        const ui::UiRect& labelRect = firstLabel.computedRect;
        logInfo("RuntimeUI",
                makeLogMessage("Fish world labels visible=",
                               fishWorldLabelLayer->children.size(),
                               " first=", firstLabel.id,
                               " rect=", static_cast<int>(labelRect.x), ",",
                               static_cast<int>(labelRect.y), ",",
                               static_cast<int>(labelRect.width), ",",
                               static_cast<int>(labelRect.height),
                               " viewport=", static_cast<int>(layerRect.width), "x",
                               static_cast<int>(layerRect.height)));
        runtimeUiWorldLabelsLogged_ = true;
    }

    if (automationRuntimeUiPixelInspect_)
    {
        runtimeUiDrawList_.addSolidRect(ui::UiRect{48.0f, 48.0f, 96.0f, 96.0f},
                                        ui::packColor(0.0f, 1.0f, 0.0f, 1.0f),
                                        panelScissor);
    }
    }
}
#endif

void App::init(int argc, char** argv)
{
    argv0_ = argc > 0 ? argv[0] : nullptr;
    setShaderSearchRoot(argv0_);

    std::string loggerError;
    const std::filesystem::path logPath = resolveLogFilePath(argv0_);
    if (!configureLogFile(logPath, &loggerError))
    {
        logWarning("Core", std::string("File logging disabled: ") +
                               (loggerError.empty() ? std::string("unknown logger error")
                                                    : loggerError));
    }
    else
    {
        logInfo("Core", std::string("Logging to ") + logPath.string());
    }

    inputRouter_.setAppMode(appMode_);
    logInfo("AppMode",
            std::string("Runtime UI compile gate: ") +
#if VOXEL_WITH_RUNTIME_UI
                "VOXEL_WITH_RUNTIME_UI=ON"
#else
                "VOXEL_WITH_RUNTIME_UI=OFF"
#endif
    );
#if VOXEL_WITH_RUNTIME_UI
    registerRuntimeUiActions();
#endif

    const std::string appModeArg = getArgValue(argc, argv, "--app-mode");
    if (!appModeArg.empty())
    {
        AppMode requestedMode = appMode_;
        if (parseAppModeArg(appModeArg, requestedMode))
        {
            setAppMode(requestedMode, "--app-mode");
        }
        else
        {
            logWarning("AppMode", std::string("Ignoring invalid --app-mode value: ") +
                                      appModeArg +
                                      " (expected editor, play-preview, or game).");
            setAppMode(appMode_, "default");
        }
    }
    else
    {
        setAppMode(appMode_, "default");
    }
    configureRenderPipelineShowcase(argc, argv);
    const std::string runtimeUiScreenArg = getArgValue(argc, argv, "--ui-screen");
    if (!runtimeUiScreenArg.empty())
    {
        const bool registered = runtimeUiContext_.setActiveScreen(runtimeUiScreenArg);
        logInfo("RuntimeUI",
                std::string("Initial runtime UI screen requested: ") + runtimeUiScreenArg);
        if (!registered)
        {
            logWarning("RuntimeUI",
                       std::string("Runtime UI screen is not registered yet: ") +
                           runtimeUiScreenArg);
        }
    }

    automationUiScript_ = getArgValue(argc, argv, "--automation-ui-script");
    if (!automationUiScript_.empty())
    {
        if (!runtimeUiContext_.hasActiveScreen() &&
            (automationUiScript_ == "ui_overlay_smoke" ||
             automationUiScript_ == "ui_click_smoke" ||
             automationUiScript_ == "ui_water_maintain_smoke" ||
             automationUiScript_ == "ui_catalog_item_smoke" ||
             automationUiScript_ == "ui_catalog_close_smoke" ||
             automationUiScript_ == "ui_pending_cancel_smoke" ||
             automationUiScript_ == "ui_pending_escape_cancel_smoke" ||
             automationUiScript_ == "ui_pending_right_mouse_ignored_smoke" ||
             automationUiScript_ == "ui_pending_confirm_invalid_smoke" ||
             automationUiScript_ == "ui_snapped_candidate_smoke" ||
             automationUiScript_ == "ui_manual_placement_qa_target_smoke" ||
             automationUiScript_ == "ui_snapped_commit_smoke" ||
             automationUiScript_ == "ui_multi_footprint_commit_smoke" ||
             automationUiScript_ == "ui_multi_footprint_blocked_smoke" ||
             automationUiScript_ == "ui_rotated_footprint_commit_smoke" ||
             automationUiScript_ == "ui_rotated_footprint_blocked_smoke" ||
             automationUiScript_ == "ui_rotation_idle_ignored_smoke" ||
             automationUiScript_ == "ui_remove_commit_smoke" ||
             automationUiScript_ == "ui_main_menu_start_smoke" ||
             automationUiScript_ == "ui_main_menu_options_smoke" ||
             automationUiScript_ == "ui_main_menu_options_labels_smoke" ||
             automationUiScript_ == "play_preview_enter_exit"))
        {
            runtimeUiContext_.setActiveScreen(
                automationUiScript_ == "ui_main_menu_start_smoke" ||
                        automationUiScript_ == "ui_main_menu_options_smoke" ||
                        automationUiScript_ == "ui_main_menu_options_labels_smoke"
                    ? ui::kMainMenuScreenId
                    : ui::kOverlaySmokeScreenId);
            logInfo("RuntimeUI",
                    std::string("Initial runtime UI screen inferred from automation script: ") +
                        runtimeUiContext_.activeScreenId());
        }
        logInfo("Automation",
                std::string("UI automation script enabled: ") + automationUiScript_);
    }

    const bool automationHideUiRequested = hasArg(argc, argv, "--automation-hide-ui");
    automationHideUi_ = automationHideUi_ || automationHideUiRequested;
    if (automationHideUiRequested)
    {
        setAppMode(AppMode::Game, "--automation-hide-ui");
        logInfo("Automation", "--automation-hide-ui mapped to app-mode game.");
    }

#if VOXEL_WITH_RUNTIME_UI
    // Standalone/game launches open on the main menu by default. explicit --ui-screen and
    // automation-script-inferred screens above already take precedence, and editor F5
    // play-preview still enters ui_overlay_smoke through enterRuntimeUiPlayPreview(). scene
    // pixel-inspect probes validate the live render, so skip the menu for them: the
    // full-viewport main menu backdrop would otherwise tint the inspected pixel.
    const bool automationScenePixelInspect =
        hasArg(argc, argv, "--automation-pixel-inspect-center") ||
        hasArg(argc, argv, "--automation-pixel-inspect-hit-axis");
    if (appMode_ == AppMode::Game && !automationHideUi_ && !automationScenePixelInspect &&
        !runtimeUiContext_.hasActiveScreen())
    {
        runtimeUiContext_.setActiveScreen(ui::kMainMenuScreenId);
        logInfo("RuntimeUI",
                "Game mode defaulted to ui_main_menu (no --ui-screen or automation screen).");
    }
    inputRouter_.setRuntimeUiModalCapture(runtimeUiModalInputActive());
#endif
    automationSkipLastUsedSave_ =
        automationSkipLastUsedSave_ || hasArg(argc, argv, "--no-persist-last-used");
    if (automationSkipLastUsedSave_)
    {
        logInfo("Automation", "Skipping last_used scene persistence for this run.");
    }
    const std::string automationViewModeArg = getArgValue(argc, argv, "--automation-view-mode");
    if (!automationViewModeArg.empty())
    {
        try
        {
            automationInitialViewMode_ = std::clamp(std::stoi(automationViewModeArg), 0, 7);
            diagnosticsSettings_.viewMode_ = automationInitialViewMode_;
            input_.handleKey(GLFW_KEY_0 + automationInitialViewMode_, GLFW_PRESS);
            logInfo("Automation",
                    makeLogMessage("Initial view mode set to ", automationInitialViewMode_, "."));
        }
        catch (const std::exception&)
        {
            automationInitialViewMode_ = -1;
            logWarning("Automation",
                       std::string("Ignoring invalid --automation-view-mode value: ") +
                           automationViewModeArg);
        }
    }

    const std::string automationDdaAdvancedArg =
        getArgValue(argc, argv, "--automation-dda-advanced-debug");
    if (!automationDdaAdvancedArg.empty())
    {
        try
        {
            automationInitialDdaAdvancedDebug_ =
                std::clamp(std::stoi(automationDdaAdvancedArg), 0, 15);
            voxelDebugSettings_.voxelDdaAdvancedDebug_ = automationInitialDdaAdvancedDebug_;
            if (voxelDebugSettings_.voxelDdaAdvancedDebug_ > 0)
            {
                voxelDebugSettings_.voxelDdaDebugMode_ = 0;
            }
            logInfo("Automation",
                    makeLogMessage("Initial DDA advanced debug set to ",
                                   automationInitialDdaAdvancedDebug_, "."));
        }
        catch (const std::exception&)
        {
            automationInitialDdaAdvancedDebug_ = -1;
            logWarning("Automation",
                       std::string("Ignoring invalid --automation-dda-advanced-debug value: ") +
                           automationDdaAdvancedArg);
        }
    }

    app::applyAutomationLightingDebugMode(
        getArgValue(argc, argv, "--automation-lighting-debug-mode"),
        lightingSettings_);

    if (hasArg(argc, argv, "--automation-pixel-inspect-center"))
    {
        automationPixelInspectMode_ = 1;
    }
    if (hasArg(argc, argv, "--automation-pixel-inspect-hit-axis"))
    {
        automationPixelInspectMode_ = 2;
    }
    if (automationPixelInspectMode_ == 1)
    {
        logInfo("Automation", "Center-pixel inspect validation enabled for this run.");
    }
    else if (automationPixelInspectMode_ == 2)
    {
        logInfo("Automation", "Center-pixel hit-axis validation enabled for this run.");
    }

    const std::string inspectPointArg =
        getArgValue(argc, argv, "--automation-pixel-inspect-point");
    if (!inspectPointArg.empty())
    {
        const size_t commaPos = inspectPointArg.find(',');
        if (commaPos != std::string::npos)
        {
            try
            {
                automationPixelInspectPoint_.x =
                    std::stoi(inspectPointArg.substr(0, commaPos));
                automationPixelInspectPoint_.y =
                    std::stoi(inspectPointArg.substr(commaPos + 1));
                logInfo("Automation",
                        makeLogMessage("Custom pixel inspect point requested at (",
                                       automationPixelInspectPoint_.x, ", ",
                                       automationPixelInspectPoint_.y, ")."));
            }
            catch (const std::exception&)
            {
                automationPixelInspectPoint_ = glm::ivec2(-1);
                logWarning("Automation",
                           std::string("Ignoring invalid --automation-pixel-inspect-point value: ") +
                               inspectPointArg);
            }
        }
        else
        {
            logWarning("Automation",
                       std::string("Ignoring invalid --automation-pixel-inspect-point value: ") +
                           inspectPointArg);
        }
    }

#if VOXEL_WITH_RUNTIME_UI
    automationRuntimeUiPixelInspect_ = hasArg(argc, argv, "--automation-runtime-ui-pixel-inspect");
    const std::string runtimeUiInspectPointArg =
        getArgValue(argc, argv, "--automation-runtime-ui-pixel-inspect-point");
    if (!runtimeUiInspectPointArg.empty())
    {
        const size_t commaPos = runtimeUiInspectPointArg.find(',');
        if (commaPos != std::string::npos)
        {
            try
            {
                automationRuntimeUiPixelInspectPoint_.x =
                    std::stoi(runtimeUiInspectPointArg.substr(0, commaPos));
                automationRuntimeUiPixelInspectPoint_.y =
                    std::stoi(runtimeUiInspectPointArg.substr(commaPos + 1));
            }
            catch (const std::exception&)
            {
                automationRuntimeUiPixelInspectPoint_ = glm::ivec2(80, 80);
                logWarning("Automation",
                           std::string("Ignoring invalid "
                                       "--automation-runtime-ui-pixel-inspect-point value: ") +
                               runtimeUiInspectPointArg);
            }
        }
        else
        {
            logWarning("Automation",
                       std::string("Ignoring invalid "
                                   "--automation-runtime-ui-pixel-inspect-point value: ") +
                           runtimeUiInspectPointArg);
        }
    }
    if (automationRuntimeUiPixelInspect_)
    {
        logInfo("Automation",
                makeLogMessage("Runtime UI pixel validation enabled at (",
                               automationRuntimeUiPixelInspectPoint_.x, ", ",
                               automationRuntimeUiPixelInspectPoint_.y, ")."));
    }
#endif

    automationWaitForCloudCommit_ = hasArg(argc, argv, "--automation-wait-for-cloud-commit");
    if (automationWaitForCloudCommit_)
    {
        logInfo("Automation",
                "Auto-exit will wait for deferred cloud readiness; cloudless scenes are "
                "immediately ready.");
    }

    configureAutomationAoProjectedDistanceTier(
        getArgValue(argc, argv, "--automation-ao-projected-radius-px"), getArgValue(argc, argv, "--automation-ao-projected-min-distance-m"));
    configureAutomationRenderQualityPreset(getArgValue(argc, argv, "--automation-render-quality-preset"));
    configureAutomationAuxiliaryRayScales(getArgValue(argc, argv, "--automation-shadow-ray-scale"),
                                          getArgValue(argc, argv, "--automation-ao-ray-scale"));
    configureAutomationUncapped(hasArg(argc, argv, "--automation-uncapped"));
    automationLogGpuProfile_ = hasArg(argc, argv, "--automation-log-gpu-profile");
    diagnosticsSettings_.shadowRayAuditEnabled_ =
        hasArg(argc, argv, "--automation-shadow-ray-audit");
    if (automationLogGpuProfile_)
    {
        diagnosticsSettings_.gpuProfilerEnabled_ = true;
        framePacingSettings_.focusedIdleThrottleEnabled_ = false;
        framePacingSettings_.backgroundThrottleEnabled_ = false;
        logInfo("Automation", "GPU profiler dump enabled for this run.");
        logInfo("Automation",
                "Foreground-style profiling enabled; idle and background throttles disabled.");
    }
    if (diagnosticsSettings_.shadowRayAuditEnabled_)
    {
        logInfo("Automation",
                "Shadow Rays per-volume GPU attribution enabled for this run.");
    }

    uint64_t profileWarmupFrames = 0;
    uint64_t profileSampleFrames = 0;
    const std::string profileWarmupArg =
        getArgValue(argc, argv, "--automation-profile-warmup-frames");
    const std::string profileSampleArg =
        getArgValue(argc, argv, "--automation-profile-sample-frames");
    if (!profileWarmupArg.empty())
    {
        const std::optional<uint64_t> parsed =
            parseNonNegativeFrameCount(profileWarmupArg);
        if (parsed.has_value())
        {
            profileWarmupFrames = *parsed;
        }
        else
        {
            logWarning(
                "Automation",
                std::string("Ignoring invalid --automation-profile-warmup-frames value: ") +
                    profileWarmupArg);
        }
    }
    if (!profileSampleArg.empty())
    {
        const std::optional<uint64_t> parsed =
            parseNonNegativeFrameCount(profileSampleArg);
        if (parsed.has_value())
        {
            profileSampleFrames = *parsed;
        }
        else
        {
            logWarning(
                "Automation",
                std::string("Ignoring invalid --automation-profile-sample-frames value: ") +
                    profileSampleArg);
        }
    }
    automationProfileWaitForSceneReady_ =
        hasArg(argc, argv, "--automation-profile-wait-for-scene-ready");
    automationProfileSession_.configure(profileWarmupFrames, profileSampleFrames);
    if (automationLogGpuProfile_ &&
        (profileWarmupFrames > 0 || profileSampleFrames > 0 ||
         automationProfileWaitForSceneReady_))
    {
        logInfo(
            "Automation",
            makeLogMessage(
                "Steady-state GPU profile configured: waitForSceneReady=",
                automationProfileWaitForSceneReady_ ? 1 : 0,
                " warmupFrames=", profileWarmupFrames,
                " sampleFrames=", profileSampleFrames, "."));
    }

    automationLogDdaMetrics_ = hasArg(argc, argv, "--automation-log-dda-metrics");
    if (automationLogDdaMetrics_)
    {
        diagnosticsSettings_.voxelMetricsEnabled_ = true;
        diagnosticsSettings_.voxelMetricsSampleStride_ = 4;
        voxelMetricsSamplePattern_ = 0;
        voxelMetricsWriteJson_ = false;
        framePacingSettings_.focusedIdleThrottleEnabled_ = false;
        framePacingSettings_.backgroundThrottleEnabled_ = false;
        logInfo("Automation", "DDA metrics dump enabled for this run.");
        logInfo("Automation",
                "Foreground-style metrics capture enabled; idle and background throttles disabled.");
    }

    automationDisableEmptySkip_ = hasArg(argc, argv, "--automation-disable-empty-skip");
    if (automationDisableEmptySkip_)
    {
        logInfo("Automation", "Voxel empty-space skip disabled for this run.");
    }

    voxelDebugSettings_.voxelAlignedLayerTraversalEnabled_ = !hasArg(argc, argv, "--automation-disable-aligned-primary-traversal");
    renderPasses().foliage.setRequestedRenderer(engine::render::parseFoliageRendererMode(
        getArgValue(argc, argv, "--automation-foliage-renderer")));
    renderPasses().foliage.setSwayEnabled(!hasArg(argc, argv, "--automation-disable-foliage-sway"));
    renderPasses().foliage.setPaletteResponse(engine::render::parseFoliagePaletteResponse(getArgValue(argc, argv, "--automation-foliage-palette")));
    renderPasses().shadowRayPass.setSamplingMode(
        engine::render::parseSunShadowSamplingMode(getArgValue(argc, argv, "--automation-sun-shadow-sampling")).value_or(engine::render::SunShadowSamplingMode::FullPerFrame));
    renderPasses().shadowRayPass.configureTerrainShadowColumns(hasArg(argc, argv, "--automation-terrain-shadow-columns"), hasArg(argc, argv, "--automation-terrain-shadow-column-parity"));

    automationDisableAquariumWater_ = hasArg(argc, argv, "--automation-disable-aquarium-water");
    if (automationDisableAquariumWater_)
    {
        logInfo("Automation", "Aquarium water disabled for this run.");
    }

    automationDisableProceduralFish_ = hasArg(argc, argv, "--automation-disable-procedural-fish");
    if (automationDisableProceduralFish_)
    {
        logInfo("Automation", "Procedural fish disabled for this run.");
        voxelDebugSettings_.proceduralFishEnabled_ = false;
    }

    if (hasArg(argc, argv, "--automation-freeze-debug"))
    {
        voxelDebugSettings_.voxelFreezeCamera_ = true;
        voxelDebugSettings_.voxelFreezeTime_ = true;
        voxelDebugSettings_.voxelDisableJitter_ = true;
        voxelDebugSettings_.voxelFreezeDebug_ = true;
        postFxSettings_.taaResetHistory_ = true;
        shadowSettings_.shadowResetHistory_ = true;
        shadowSettings_.localShadowResetHistory_ = true;
        aoSettings_.aoResetHistory_ = true;
        logInfo("Automation", "Freeze Debug enabled for this run (camera, time, jitter).");
    }
    else if (hasArg(argc, argv, "--automation-freeze-scene"))
    {
        voxelDebugSettings_.voxelFreezeCamera_ = true;
        voxelDebugSettings_.voxelFreezeTime_ = true;
        logInfo("Automation",
                "Scene freeze enabled for this run (camera and time; TAA jitter preserved).");
    }
    else if (hasArg(argc, argv, "--automation-freeze-camera"))
    {
        voxelDebugSettings_.voxelFreezeCamera_ = true;
        logInfo("Automation", "Camera freeze enabled for this run; animation time remains live.");
    }
    configureAutomationFixedSceneTime(getArgValue(argc, argv, "--automation-fixed-scene-time-seconds"));

    const std::string autoExitArg = getArgValue(argc, argv, "--auto-exit-ms");
    if (!autoExitArg.empty())
    {
        try
        {
            automationAutoExitMs_ = std::max(0, std::stoi(autoExitArg));
        }
        catch (const std::exception&)
        {
            automationAutoExitMs_ = 0;
            logWarning("Automation", std::string("Ignoring invalid --auto-exit-ms value: ") +
                                         autoExitArg);
        }
        if (automationAutoExitMs_ > 0)
        {
            logInfo("Automation",
                    std::string("Auto-exit enabled after ") +
                        std::to_string(automationAutoExitMs_) + " ms.");
        }
    }

    const std::string autoExitFramesArg = getArgValue(argc, argv, "--auto-exit-frames");
    if (!autoExitFramesArg.empty())
    {
        try
        {
            automationAutoExitFrames_ =
                static_cast<uint64_t>(std::max(0, std::stoi(autoExitFramesArg)));
        }
        catch (const std::exception&)
        {
            automationAutoExitFrames_ = 0;
            logWarning("Automation", std::string("Ignoring invalid --auto-exit-frames value: ") +
                                         autoExitFramesArg);
        }
        if (automationAutoExitFrames_ > 0)
        {
            logInfo("Automation",
                    std::string("Auto-exit enabled after ") +
                        std::to_string(automationAutoExitFrames_) + " rendered frames.");
        }
    }

    configureAutomationRenderScale(getArgValue(argc, argv, "--automation-render-scale"));

#if defined(VOXEL_PLATFORM_MACOS) && VOXEL_PLATFORM_MACOS
    // keep GLFW surface queries on the loader that creates the instance.
    glfwInitVulkanLoader(vkGetInstanceProcAddr);
#endif
    if (glfwInit() == GLFW_FALSE)
    {
        die("glfwInit failed");
    }
    if (glfwVulkanSupported() == GLFW_FALSE)
    {
        die("GLFW reports Vulkan not supported");
    }

    int requestedWindowWidth = 1280;
    int requestedWindowHeight = 720;
    const std::string automationWindowSizeArg =
        getArgValue(argc, argv, "--automation-window-size");
    if (!automationWindowSizeArg.empty())
    {
        if (parseWindowSizeArg(automationWindowSizeArg, requestedWindowWidth,
                               requestedWindowHeight))
        {
            logInfo("Automation",
                    makeLogMessage("Window size requested: ", requestedWindowWidth, "x",
                                   requestedWindowHeight, "."));
        }
        else
        {
            requestedWindowWidth = 1280;
            requestedWindowHeight = 720;
            logWarning("Automation",
                       std::string("Ignoring invalid --automation-window-size value: ") +
                           automationWindowSizeArg);
        }
    }

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
#if defined(VOXEL_PLATFORM_MACOS) && VOXEL_PLATFORM_MACOS
    if (hasArg(argc, argv, "--automation-framebuffer-scale-1x"))
        glfwWindowHint(GLFW_COCOA_RETINA_FRAMEBUFFER, GLFW_FALSE);
#endif
    window_ = glfwCreateWindow(requestedWindowWidth, requestedWindowHeight, kWindowTitle, nullptr,
                               nullptr);
    if (window_ == nullptr)
    {
        die("glfwCreateWindow failed");
    }

    inputController_.attach(window_, *this);

    if (automationPixelInspectMode_ != 0)
    {
        int windowWidth = 0;
        int windowHeight = 0;
        glfwGetWindowSize(window_, &windowWidth, &windowHeight);
        diagnosticsSettings_.pixelInspectEnabled_ = true;
        if (automationPixelInspectPoint_.x >= 0 && automationPixelInspectPoint_.y >= 0)
        {
            diagnosticsSettings_.inspectPixel_ = glm::ivec2(std::clamp(automationPixelInspectPoint_.x, 0, windowWidth - 1),
                                       std::clamp(automationPixelInspectPoint_.y, 0, windowHeight - 1));
        }
        else
        {
            diagnosticsSettings_.inspectPixel_ = glm::ivec2(windowWidth / 2, windowHeight / 2);
        }
        if (automationPixelInspectMode_ == 2)
        {
            voxelDebugSettings_.voxelDdaDebugMode_ = 0;
            voxelDebugSettings_.voxelDdaAdvancedDebug_ = 3;
        }
        logInfo("Automation",
                makeLogMessage("Pixel inspect sample point set to (", diagnosticsSettings_.inspectPixel_.x, ", ",
                               diagnosticsSettings_.inspectPixel_.y, ")."));
    }

    ctx_.create(window_);
    if (!InitVoxelPaletteTest(ctx_, voxelPalette_))
    {
        die("VoxelPalette test failed");
    }
    // note: voxelWorld_ init is deferred until after scene config is loaded
    if (!bindRendererFrameLifecycle()) { die("Renderer frame lifecycle binding failed"); }
    renderer_.createCommandResources();
    renderer_.createSwapchainResources(window_, presentMode_);
    const uint32_t workerCount =
        std::clamp(std::thread::hardware_concurrency() / 2u, 1u, 4u);
    jobSystem_.start(workerCount);

    passCreateInfo_.extent = engine::render::internalRenderExtent(
        renderer_.swapchainExtent(), postFxSettings_.renderResolution_.scale);
    passCreateInfo_.commands = &renderer_.commands();
    logInfo("Renderer",
            makeLogMessage("Render resolution internal=", passCreateInfo_.extent.width,
                           "x", passCreateInfo_.extent.height, " presentation=",
                           renderer_.swapchainExtent().width, "x",
                           renderer_.swapchainExtent().height, " scale=",
                           postFxSettings_.renderResolution_.scale, " upscaler=",
                           engine::render::spatialUpscaleModeName(
                               postFxSettings_.renderResolution_.upscaleMode), "."));

    createLightsBuffer(ctx_, kMaxLights, lights_);
    input_.setMaxLights(static_cast<int>(lights_.maxLights));
    input_.setLightCount(16);

    lastFrameTime_ = glfwGetTime();
    lastInteractionTime_ = lastFrameTime_;
    assetRoot_ = resolveAssetRoot(argv0_, argc, argv);
    if (!std::filesystem::exists(assetRoot_))
    {
        logWarning("Content", std::string("Asset root not found: ") + assetRoot_.string());
    }
    else
    {
        logInfo("Content", std::string("Asset root: ") + assetRoot_.string());
    }

    const std::filesystem::path runtimeManifestPath = resolveRuntimeContentManifestPath(argv0_);
    RuntimeContentManifest runtimeManifest;
    std::string runtimeManifestError;
    if (!loadRuntimeContentManifest(runtimeManifestPath, runtimeManifest, &runtimeManifestError))
    {
        std::string fatal = "Runtime content manifest load failed: " + runtimeManifestPath.string();
        if (!runtimeManifestError.empty())
        {
            fatal += " (" + runtimeManifestError + ")";
        }
        logAndExit("Content", fatal);
    }
    logInfo("Content",
            std::string("Loaded runtime content manifest: ") + runtimeManifestPath.string());

    const RuntimeContentValidationReport contentReport =
        validateRuntimeContentManifest(runtimeManifest, assetRoot_, argv0_);
    for (const std::string& warning : contentReport.warnings)
    {
        logWarning("Content", warning);
    }
    if (!contentReport.ok())
    {
        std::string fatal = "Runtime content validation failed:";
        for (const std::string& error : contentReport.errors)
        {
            fatal += "\n - " + error;
        }
        logAndExit("Content", fatal);
    }
    logInfo("Content", "Runtime content validation succeeded.");

#if VOXEL_WITH_RUNTIME_UI
    const std::filesystem::path runtimeUiDebugFontPath =
        assetRoot_ / "ui" / "fonts" / "runtime_debug_font.json";
    std::string runtimeUiFontError;
    if (!ui::loadDebugFontAsset(runtimeUiDebugFontPath, runtimeUiDebugFont_,
                                &runtimeUiFontError))
    {
        std::string fatal =
            "Runtime UI font metadata load failed: " + runtimeUiDebugFontPath.string();
        if (!runtimeUiFontError.empty())
        {
            fatal += " (" + runtimeUiFontError + ")";
        }
        logAndExit("RuntimeUI", fatal);
    }
    if (!ui::loadFontAtlasImage(assetRoot_, runtimeUiDebugFont_, runtimeUiFontAtlasImage_,
                                &runtimeUiFontError))
    {
        std::string fatal =
            "Runtime UI font atlas image load failed for font id=" + runtimeUiDebugFont_.id;
        if (!runtimeUiFontError.empty())
        {
            fatal += " (" + runtimeUiFontError + ")";
        }
        logAndExit("RuntimeUI", fatal);
    }
    logInfo("RuntimeUI",
            makeLogMessage("Loaded font asset id=", runtimeUiDebugFont_.id,
                           " mode=", ui::fontRenderModeLabel(runtimeUiDebugFont_.renderMode),
                           " source=", runtimeUiDebugFont_.atlasSource,
                           " atlas=", runtimeUiDebugFont_.atlasWidth, "x",
                           runtimeUiDebugFont_.atlasHeight,
                           " glyphs=",
                           runtimeUiDebugFont_.lastCodepoint -
                               runtimeUiDebugFont_.firstCodepoint + 1,
                           " metadata_glyphs=", runtimeUiDebugFont_.glyphs.size(),
                           " kerning_pairs=", runtimeUiDebugFont_.kerningByPair.size()));
    logInfo("RuntimeUI",
            makeLogMessage("Loaded font atlas image id=", runtimeUiDebugFont_.id,
                           " image=",
                           runtimeUiDebugFont_.atlasImage.empty()
                               ? std::string("<generated>")
                               : runtimeUiDebugFont_.atlasImage,
                           " extent=", runtimeUiFontAtlasImage_.w, "x",
                           runtimeUiFontAtlasImage_.h));
#endif

    voxelImportMeshPath_ = findFirstFileWithExtension(assetRoot_ / "meshes", ".obj");
    if (voxelImportMeshPath_.empty())
    {
        logWarning("Assets", std::string("No OBJ found for voxel import in ") +
                                 (assetRoot_ / "meshes").string());
    }

    sceneManager_.setScenesRoot(resolveScenesRoot(argv0_, argc, argv));
    logInfo("Scene", std::string("Scenes root: ") +
                         sceneManager_.scenesRoot().string());
    refreshSceneCatalog(true);
    const std::string sceneArg = getArgValue(argc, argv, "--scene");
    const engine::scene::SceneLoadResult sceneLoad =
        sceneManager_.loadInitialScene(sceneArg);
    sceneConfigMutable() = sceneLoad.config;
    gameRuntime_.reconcileOfflineCare(sceneConfigMutable().gameState);
    // apply scene config to engine state
    worldSeed_ = sceneConfig().worldSeed;
    applyProceduralPresetDefaultsForScene();

    // apply camera from scene config
    camera_.position = sceneConfig().cameraPosition;
    camera_.yaw = sceneConfig().cameraYaw;
    camera_.pitch = sceneConfig().cameraPitch;

    // apply runtime toggles from scene config
    waterSettings_.waterEnabled_ = sceneConfig().enableWater;
    waterSettings_.useWaterV2_ = sceneConfig().useWaterV2;
    if (automationDisableAquariumWater_ && sceneConfig().loadAquariumTest)
    {
        waterSettings_.waterEnabled_ = false;
    }
    input_.setWaterEnabled(sceneConfig().enableWater);
    if (automationDisableAquariumWater_ && sceneConfig().loadAquariumTest)
    {
        input_.setWaterEnabled(false);
    }
    glassSettings_.glassEnabled_ = sceneConfig().enableGlass;
    input_.setGlassEnabled(sceneConfig().enableGlass);
    lightingSettings_.pointLightsEnabled_ = sceneConfig().enablePointLights;
    input_.setLightsEnabled(sceneConfig().enablePointLights);

    applySceneLightingDefaults(*this, sceneConfig());
    applySceneWaterDefaults(*this, sceneConfig());
    applySceneGlassDefaults(*this, sceneConfig());
    applyScenePostDefaults(*this, sceneConfig());

    const std::filesystem::path voxelMaterialAtlasPath =
        assetRoot_ / "textures" / "voxel_material_atlas.png";
    if (!voxelMaterialAtlas_.create(ctx_, voxelMaterialAtlasPath))
    {
        die("Voxel material atlas init failed");
    }
    voxelWorld_.setMaterialAtlasDescriptor(voxelMaterialAtlas_.descriptorInfo());

    // scene-specific glass-system defaults:
    // - direct-view aquarium scenes isolate the authored aquarium lighting/readability
    // - glass_focus keeps voxel-glass post enabled for that scene's purpose
    if (isDirectViewAquariumScene(sceneConfig()))
    {
        glassSettings_.voxelGlassRefractEnabled_ = false;
        glassSettings_.voxelGlassDebugMode_ = 0;
    }
    else if (sceneConfig().name == "glass_focus")
    {
        glassSettings_.voxelGlassRefractEnabled_ = true;
    }

    // one shared bridge owns both startup and reload profile application.
    sceneManager_.setPresentationRuntimeState(
        app::resolveApplyAndSyncScenePresentationProfile(
            sceneConfig(), input_, skyPreset_, skyColor_, lightingSettings_,
            shadowSettings_, aoSettings_, postFxSettings_, voxelDebugSettings_,
            waterSettings_, glassSettings_, lights_.maxLights));
    app::applyAutomationSoftProfileV0(
        getArgValue(argc, argv, "--automation-reconstruction-mode"), input_,
        skyPreset_, skyColor_, lightingSettings_, shadowSettings_, aoSettings_,
        postFxSettings_, voxelDebugSettings_, waterSettings_, glassSettings_,
        lights_.maxLights);
    app::applyAutomationVoxelCellVariationPreset(
        getArgValue(argc, argv, "--automation-voxel-cell-variation"),
        voxelDebugSettings_);
    app::applyAutomationHemisphereAmbientPreset(
        getArgValue(argc, argv, "--automation-hemisphere-ambient"),
        lightingSettings_);
    app::applyAutomationSceneAtmospherePreset(
        getArgValue(argc, argv, "--automation-scene-atmosphere"),
        lightingSettings_);
    updateSunDirection();
    resetEnvironmentTimeRuntimeState();

    if (!initVolumeScene())
    {
        die("Volume scene init failed");
    }
    requestCloudBuildFromSceneConfig("startup");

    if (!waterVolumeMgr_.init(ctx_))
    {
        die("WaterVolumeManager init failed");
    }
    if (!waterContainerMgr_.init(ctx_))
    {
        die("WaterContainerManager init failed");
    }
    waterVolumeMgr_.setVolumes({makeSceneWaterVolume(sceneConfig(), voxelWorld_, waterSettings_.waterLevel_)});
    syncWaterContainersFromVolumes();
    waterVolumeMgr_.upload();
    waterContainerMgr_.upload();
    resetAreaLightsToScenePreset();

    materialPool_.create(ctx_, kMaxMaterials);

    if (!bindRendererPassLifecycle()) { die("Renderer pass lifecycle binding failed"); }
    renderer_.createPasses();
    if (!gpuProfiler_.create(ctx_, 32))
    {
        logWarning("Renderer", "GPU profiler create failed.");
    }
    setGpuProfilerEnabled(diagnosticsSettings_.gpuProfilerEnabled_);

#if VOXEL_WITH_EDITOR
    // initialize ImGui after render passes are created
    imgui_.create(ctx_, window_, renderer_.swapchainRenderPass(),
                  renderer_.swapchainImageCount());
    Editor::Init();
#endif

    loadAssets();

    renderer_.createFrameSyncResources();

    cpuFrameHistoryMs_.reserve(600);
    gpuFrameHistoryMs_.reserve(600);

    logInfo("Vulkan", "Vulkan initialized successfully.");
    logInfo("Vulkan", std::string("Graphics queue family: ") +
                          std::to_string(ctx_.queueFamilies.graphics.value()));
    logInfo("Vulkan", std::string("Present queue family: ") +
                          std::to_string(ctx_.queueFamilies.present.value()));
}

void App::mainLoop()
{
    automationLoopStartTimeSeconds_ = glfwGetTime();
    automationProfileSession_.reset();

    while (glfwWindowShouldClose(window_) == GLFW_FALSE)
    {
        glfwPollEvents();
        updateAutomationUiScript();

        const bool automationGpuProfileReady =
            !automationLogGpuProfile_ || !gpuProfiler_.isSupported() || !gpuProfiler_.isEnabled() ||
            (automationProfileSession_.requestedSampleFrames() > 0
                 ? automationProfileSession_.samplingComplete()
                 : automationProfileSession_.sampledFrameCount() > 0);
        const bool automationDdaMetricsReady =
            !automationLogDdaMetrics_ || cachedDdaMetrics_.sampleCount > 0;
        const bool automationUiReady =
            automationUiScript_.empty() || automationUiScriptCompleted_;
#if VOXEL_WITH_RUNTIME_UI
        const bool automationRuntimeUiPixelReady =
            !automationRuntimeUiPixelInspect_ || automationRuntimeUiPixelInspectValidated_;
#else
        const bool automationRuntimeUiPixelReady = true;
#endif
        const bool automationReadyToExit =
            renderedFrameCount_ > 0 &&
            (!automationWaitForCloudCommit_ || !sceneConfig().loadCloudScene ||
             automationCloudCommitObserved_) &&
            (automationPixelInspectMode_ == 0 || automationPixelInspectValidated_) &&
            automationGpuProfileReady && automationDdaMetricsReady && automationUiReady &&
            automationRuntimeUiPixelReady;
        const bool autoExitByTime =
            automationAutoExitMs_ > 0 &&
            ((glfwGetTime() - automationLoopStartTimeSeconds_) * 1000.0) >=
                static_cast<double>(automationAutoExitMs_);
        const uint64_t automationCompletedFrames =
            automationLogGpuProfile_ && gpuProfiler_.isSupported() && gpuProfiler_.isEnabled()
                ? automationProfileSession_.sampledFrameCount()
                : renderedFrameCount_;
        const bool autoExitByFrames =
            automationAutoExitFrames_ > 0 &&
            automationCompletedFrames >= automationAutoExitFrames_;
        if (automationReadyToExit && (autoExitByTime || autoExitByFrames))
        {
            dumpAutomationGpuProfileSummary();
            dumpAutomationDdaMetricsSummary();
            glfwSetWindowShouldClose(window_, GLFW_TRUE);
            continue;
        }

        int width = 0;
        int height = 0;
        glfwGetFramebufferSize(window_, &width, &height);
        const bool iconified = glfwGetWindowAttrib(window_, GLFW_ICONIFIED) == GLFW_TRUE;
        if (iconified || width == 0 || height == 0)
        {
            lastFrameTime_ = glfwGetTime();
            glfwWaitEventsTimeout(0.1);
            continue;
        }

        drawFrame();
    }
}

void App::shutdown()
{
    if (!automationSkipLastUsedSave_ && !editorSceneDocument_.playSnapshotActive())
    {
        sceneConfigMutable().cameraPosition = camera_.position;
        sceneConfigMutable().cameraYaw = camera_.yaw;
        sceneConfigMutable().cameraPitch = camera_.pitch;
        sceneConfigMutable().worldSeed = worldSeed_;
        sceneConfigMutable().presentationProfile =
            engine::scene::captureScenePresentationProfile(
                sceneConfig().name, skyPreset_, skyColor_, lightingSettings_,
                shadowSettings_, aoSettings_, postFxSettings_, voxelDebugSettings_,
                waterSettings_, glassSettings_);
        gameRuntime_.stampLastPlayedUtc(sceneConfigMutable().gameState);
        const std::filesystem::path lastUsedPath =
            sceneManager_.scenesRoot() / "last_used.json";
        std::string lastUsedSaveError;
        if (!saveSceneConfigToFile(lastUsedPath, sceneConfig(), &lastUsedSaveError,
                                   SceneSerializerLogMode::Quiet))
        {
            logWarning("Scene", std::string("Failed to persist last_used scene state to ") +
                                   lastUsedPath.string() +
                                   (lastUsedSaveError.empty() ? std::string()
                                                              : " (" + lastUsedSaveError + ")"));
        }
    }

    if (ctx_.device != VK_NULL_HANDLE)
    {
        vkDeviceWaitIdle(ctx_.device);
    }

    renderer_.destroyPasses();
    voxelWorld_.shutdown(ctx_);
    voxelMaterialAtlas_.destroy(ctx_);
    if (voxelPalette_.isValid())
    {
        voxelPalette_.destroy(ctx_);
    }

    jobSystem_.stop();
#if VOXEL_WITH_EDITOR
    Editor::Shutdown();
    imgui_.destroy(ctx_);
#endif
    gpuProfiler_.destroy(ctx_);
    waterContainerMgr_.shutdown(ctx_);
    waterVolumeMgr_.shutdown(ctx_);
    destroyAssets();
    destroyLightsBuffer(ctx_, lights_);
    renderer_.destroyFrameSyncResources();
    renderer_.destroySwapchainResources();
    renderer_.destroyCommandResources();
    ctx_.destroy();

    if (window_ != nullptr)
    {
        inputController_.detach();
        glfwDestroyWindow(window_);
        window_ = nullptr;
    }
    glfwTerminate();

    logInfo("App", "Clean shutdown.");
    closeLogFile();
}

void App::waitForInFlightFrameWork(const char* reason)
{
    std::string error;
    if (!renderer_.waitForInFlightFrameWork(reason, &error))
    {
        logAndExit("Vulkan", error);
    }
}

void App::setUseWaterV2(bool enabled)
{
    if (waterSettings_.useWaterV2_ == enabled && sceneConfig().useWaterV2 == enabled)
    {
        return;
    }

    waterSettings_.useWaterV2_ = enabled;
    sceneConfigMutable().useWaterV2 = enabled;

    if (sceneConfig().loadAquariumTest)
    {
        rebuildVolumeScene();
    }

    postFxSettings_.taaResetHistory_ = true;
    shadowSettings_.shadowResetHistory_ = true;
    shadowSettings_.localShadowResetHistory_ = true;
    aoSettings_.aoResetHistory_ = true;
}

void App::sanitizeWaterParameters()
{
    if (!waterParametersDirty_)
    {
        return;
    }

    waterSettings_.waterRefract_ = std::clamp(waterSettings_.waterRefract_, 0.0f, 0.06f);
    waterSettings_.waterDistortionDepthScale_ = std::clamp(waterSettings_.waterDistortionDepthScale_, 0.0f, 0.6f);
    waterSettings_.waterWaveScale_ = std::clamp(waterSettings_.waterWaveScale_, 0.02f, 0.30f);
    waterSettings_.waterWaveAmp_ = std::clamp(waterSettings_.waterWaveAmp_, 0.0f, 1.2f);
    waterSettings_.waterCrestThreshold_ = std::clamp(waterSettings_.waterCrestThreshold_, 0.0f, 1.0f);
    waterSettings_.waterCrestSoftness_ = std::clamp(waterSettings_.waterCrestSoftness_, 0.01f, 0.35f);
    waterSettings_.waterCrestIntensity_ = std::clamp(waterSettings_.waterCrestIntensity_, 0.0f, 1.5f);
    waterSettings_.waterSpecPower_ = std::clamp(waterSettings_.waterSpecPower_, 64.0f, 640.0f);
    waterSettings_.waterSpecIntensity_ = std::clamp(waterSettings_.waterSpecIntensity_, 0.1f, 4.0f);
    waterSettings_.waterBandHardness_ = std::clamp(waterSettings_.waterBandHardness_, 0.0f, 1.0f);
    waterSettings_.waterReflectionStrength_ = std::clamp(waterSettings_.waterReflectionStrength_, 0.0f, 1.0f);
    waterSettings_.waterFresnelBias_ = std::clamp(waterSettings_.waterFresnelBias_, 0.0f, 0.5f);
    waterSettings_.waterCausticsIntensity_ = std::clamp(waterSettings_.waterCausticsIntensity_, 0.0f, 4.0f);
    waterSettings_.waterCausticsScale_ = std::clamp(waterSettings_.waterCausticsScale_, 0.02f, 1.0f);
    waterSettings_.waterCausticsSpeed_ = std::clamp(waterSettings_.waterCausticsSpeed_, 0.0f, 3.0f);
    waterSettings_.waterCausticsBanding_ = std::clamp(waterSettings_.waterCausticsBanding_, 0.0f, 1.0f);
    waterSettings_.waterCausticsDepthFade_ = std::clamp(waterSettings_.waterCausticsDepthFade_, 1.0f, 40.0f);
    waterSettings_.waterParticlesPlannedDensity_ = std::clamp(waterSettings_.waterParticlesPlannedDensity_, 0.0f, 1.0f);
    waterSettings_.waterParticlesPlannedDrift_ = std::clamp(waterSettings_.waterParticlesPlannedDrift_, 0.0f, 1.0f);
    waterSettings_.waterParticlesPlannedScale_ = std::clamp(waterSettings_.waterParticlesPlannedScale_, 0.0f, 1.0f);
    waterSettings_.waterFoamEmitterIntensity_ = std::clamp(waterSettings_.waterFoamEmitterIntensity_, 0.0f, 1.0f);
    waterSettings_.waterFoamEmitterRadius_ = std::clamp(waterSettings_.waterFoamEmitterRadius_, 0.25f, 12.0f);
    waterSettings_.waterFoamEmitterOffsetX_ = std::clamp(waterSettings_.waterFoamEmitterOffsetX_, -1.0f, 1.0f);
    waterSettings_.waterFoamEmitterOffsetZ_ = std::clamp(waterSettings_.waterFoamEmitterOffsetZ_, -1.0f, 1.0f);
    waterSettings_.waterFoamEmitterScale_ = std::clamp(waterSettings_.waterFoamEmitterScale_, 0.0f, 1.0f);
    waterSettings_.waterFoamEmitterSpread_ = std::clamp(waterSettings_.waterFoamEmitterSpread_, 0.0f, 1.0f);
    waterSettings_.waterGradientStrength_ = std::clamp(waterSettings_.waterGradientStrength_, 0.0f, 1.0f);
    waterSettings_.waterPlanarStrength_ = std::clamp(waterSettings_.waterPlanarStrength_, 0.0f, 1.0f);
    waterParametersDirty_ = false;
}

void App::syncWaterContainersFromVolumes()
{
    if (waterContainerMgr_.buffer() == VK_NULL_HANDLE)
    {
        return;
    }

    std::vector<engine::WaterContainer> containers{};
    containers.reserve(waterVolumeMgr_.volumes().size());
    for (const engine::WaterVolume& volume : waterVolumeMgr_.volumes())
    {
        engine::WaterContainer container{};
        container.boundsMin = volume.boundsMin;
        container.boundsMax = volume.boundsMax;
        container.surfaceHeight = volume.surfaceHeight;
        container.absorptionCoeff = volume.absorptionCoeff;
        container.deepColor = volume.deepColor;
        container.fogDensity = volume.fogDensity;
        container.shape = volume.shape;
        container.flags = volume.flags;
        containers.push_back(container);
    }

    waterContainerMgr_.setContainers(containers);
}

void App::waitForRenderQueuesIdle(const char* reason)
{
    auto waitForQueue = [&](VkQueue queue, const char* queueName) {
        if (queue == VK_NULL_HANDLE)
        {
            return;
        }

        const VkResult result = vkQueueWaitIdle(queue);
        if (result != VK_SUCCESS)
        {
            logAndExit("Vulkan", std::string("Failed to idle the ") + queueName + " queue during " +
                                     reason + " (VkResult " +
                                     std::to_string(static_cast<int>(result)) + ")");
        }
    };

    waitForQueue(ctx_.graphicsQueue, "graphics");
    if (ctx_.presentQueue != ctx_.graphicsQueue)
    {
        waitForQueue(ctx_.presentQueue, "present");
    }
}

bool App::isCloudBuildInProgress() const
{
    return sceneManager_.isCloudBuildInProgress();
}

void App::createPixelInspectResources()
{
    for (auto& rb : pixelInspectReadback_)
    {
        if (rb.buffer != VK_NULL_HANDLE)
        {
            continue;
        }
        createBuffer(ctx_, kPixelInspectBufferSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                     rb.buffer, rb.memory);
        rb.pending = false;
        rb.pixel = glm::ivec2(-1);
        rb.debugMode = 0;
    }

#if VOXEL_WITH_RUNTIME_UI
    if (automationRuntimeUiPixelInspect_)
    {
        for (auto& rb : runtimeUiPixelReadback_)
        {
            if (rb.buffer != VK_NULL_HANDLE)
            {
                continue;
            }
            createBuffer(ctx_, kRuntimeUiPixelInspectBufferSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                             VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                         rb.buffer, rb.memory);
            rb.pending = false;
            rb.pixel = glm::ivec2(-1);
        }
    }
#endif
}

void App::destroyPixelInspectResources()
{
    for (auto& rb : pixelInspectReadback_)
    {
        if (rb.buffer != VK_NULL_HANDLE)
        {
            vkDestroyBuffer(ctx_.device, rb.buffer, nullptr);
            rb.buffer = VK_NULL_HANDLE;
        }
        if (rb.memory != VK_NULL_HANDLE)
        {
            vkFreeMemory(ctx_.device, rb.memory, nullptr);
            rb.memory = VK_NULL_HANDLE;
        }
        rb.pending = false;
        rb.pixel = glm::ivec2(-1);
        rb.debugMode = 0;
    }

#if VOXEL_WITH_RUNTIME_UI
    for (auto& rb : runtimeUiPixelReadback_)
    {
        if (rb.buffer != VK_NULL_HANDLE)
        {
            vkDestroyBuffer(ctx_.device, rb.buffer, nullptr);
            rb.buffer = VK_NULL_HANDLE;
        }
        if (rb.memory != VK_NULL_HANDLE)
        {
            vkFreeMemory(ctx_.device, rb.memory, nullptr);
            rb.memory = VK_NULL_HANDLE;
        }
        rb.pending = false;
        rb.pixel = glm::ivec2(-1);
    }
#endif
}

void App::updatePixelInspectReadback(uint32_t frameIndex)
{
    if (!diagnosticsSettings_.pixelInspectEnabled_)
    {
        return;
    }
    if (frameIndex >= pixelInspectReadback_.size())
    {
        return;
    }

    auto& rb = pixelInspectReadback_[frameIndex];
    if (!rb.pending || rb.memory == VK_NULL_HANDLE)
    {
        return;
    }

    uint8_t raw[kPixelInspectBufferSize] = {};
    void* mapped = nullptr;
    if (vkMapMemory(ctx_.device, rb.memory, 0, sizeof(raw), 0, &mapped) == VK_SUCCESS)
    {
        std::memcpy(raw, mapped, sizeof(raw));
        vkUnmapMemory(ctx_.device, rb.memory);
    }
    rb.pending = false;

    const uint16_t* normalRaw =
        reinterpret_cast<const uint16_t*>(raw + kPixelInspectNormalOffset);
    const float r = halfToFloat(normalRaw[0]);
    const float g = halfToFloat(normalRaw[1]);
    const float b = halfToFloat(normalRaw[2]);
    const float a = halfToFloat(normalRaw[3]);
    (void)a;

    glm::vec3 prevNormal = diagnosticsSettings_.lastInspectNormal_;
    const float prevNdotL = diagnosticsSettings_.lastInspectNdotL_;
    const int prevAxis = lastInspectHitAxis_;

    lastInspectVoxelFrac_ = glm::vec3(0.0f);
    lastInspectHitAxis_ = 0;
    diagnosticsSettings_.lastInspectNormal_ = glm::vec3(0.0f);
    diagnosticsSettings_.lastInspectNdotL_ = 0.0f;
    diagnosticsSettings_.lastInspectWorldPos_ = glm::vec3(0.0f);
    lastInspectVoxel_ = glm::ivec3(0);
    lastInspectVolumeIndex_ = -1;
    diagnosticsSettings_.lastInspectDepth_ = 1.0f;
    diagnosticsSettings_.lastInspectHasWorld_ = false;

    if (rb.debugMode == 10u)
    {
        lastInspectVoxelFrac_ = glm::vec3(r, g, 0.0f);
        const float axisIndex = b * 3.0f;
        if (axisIndex > 0.5f && axisIndex < 1.5f)
        {
            lastInspectHitAxis_ = 1;
        }
        else if (axisIndex >= 1.5f && axisIndex < 2.5f)
        {
            lastInspectHitAxis_ = 2;
        }
        else if (axisIndex >= 2.5f)
        {
            lastInspectHitAxis_ = 3;
        }
    }
    else if (rb.debugMode == 11u)
    {
        lastInspectVoxelFrac_ = glm::vec3(r, g, b);
    }
    else if (rb.debugMode == 12u)
    {
        if (r > 0.5f) lastInspectHitAxis_ = 1;
        else if (g > 0.5f) lastInspectHitAxis_ = 2;
        else if (b > 0.5f) lastInspectHitAxis_ = 3;
    }
    else
    {
        const glm::vec3 n = glm::vec3(r, g, b) * 2.0f - glm::vec3(1.0f);
        const float nLen = glm::length(n);
        if (nLen > 1e-4f)
        {
            diagnosticsSettings_.lastInspectNormal_ = n / nLen;
            const glm::vec3 lightDir = environmentTimeSample_.lightDirection;
            diagnosticsSettings_.lastInspectNdotL_ = glm::dot(diagnosticsSettings_.lastInspectNormal_, -lightDir);
        }
    }

    inspectNormalDelta_ = glm::length(diagnosticsSettings_.lastInspectNormal_ - prevNormal);
    inspectNdotlDelta_ = std::abs(diagnosticsSettings_.lastInspectNdotL_ - prevNdotL);
    inspectAxisChanged_ = (lastInspectHitAxis_ != 0 && lastInspectHitAxis_ != prevAxis);

    uint32_t depthRaw = 0;
    std::memcpy(&depthRaw, raw + kPixelInspectDepthOffset, sizeof(depthRaw));
    const VkFormat depthFormat = renderPasses().gbuffer.depthFormat();
    if (depthFormat == VK_FORMAT_D32_SFLOAT || depthFormat == VK_FORMAT_D32_SFLOAT_S8_UINT)
    {
        std::memcpy(&diagnosticsSettings_.lastInspectDepth_, &depthRaw, sizeof(diagnosticsSettings_.lastInspectDepth_));
    }
    else if (depthFormat == VK_FORMAT_D24_UNORM_S8_UINT)
    {
        diagnosticsSettings_.lastInspectDepth_ =
            static_cast<float>(depthRaw & 0x00FFFFFFu) / static_cast<float>(0x00FFFFFFu);
    }
    else
    {
        diagnosticsSettings_.lastInspectDepth_ =
            static_cast<float>(depthRaw) / static_cast<float>(std::numeric_limits<uint32_t>::max());
    }

    if (diagnosticsSettings_.lastInspectDepth_ > 0.0f && diagnosticsSettings_.lastInspectDepth_ < 1.0f && rb.pixel.x >= 0 &&
        rb.pixel.y >= 0)
    {
        const VkExtent2D extent = renderPasses().gbuffer.extent();
        const float ndcX =
            (static_cast<float>(rb.pixel.x) + 0.5f) / static_cast<float>(extent.width) * 2.0f -
            1.0f;
        const float ndcY =
            (static_cast<float>(rb.pixel.y) + 0.5f) / static_cast<float>(extent.height) * 2.0f -
            1.0f;
        const glm::vec4 clip(ndcX, ndcY, diagnosticsSettings_.lastInspectDepth_, 1.0f);
        glm::vec4 world = lastInvViewProjUnjittered_ * clip;
        if (std::abs(world.w) > 1e-6f)
        {
            world /= world.w;
            diagnosticsSettings_.lastInspectWorldPos_ = glm::vec3(world);
            diagnosticsSettings_.lastInspectHasWorld_ = true;

            const auto& instances = voxelWorld_.instances();
            for (size_t i = 0; i < instances.size(); ++i)
            {
                const auto& inst = instances[i];
                const glm::vec3 wmin = inst.volume.worldAabbMin();
                const glm::vec3 wmax = inst.volume.worldAabbMax();
                if (diagnosticsSettings_.lastInspectWorldPos_.x < wmin.x || diagnosticsSettings_.lastInspectWorldPos_.y < wmin.y ||
                    diagnosticsSettings_.lastInspectWorldPos_.z < wmin.z || diagnosticsSettings_.lastInspectWorldPos_.x > wmax.x ||
                    diagnosticsSettings_.lastInspectWorldPos_.y > wmax.y || diagnosticsSettings_.lastInspectWorldPos_.z > wmax.z)
                {
                    continue;
                }

                const glm::vec3 local =
                    glm::vec3(inst.volume.localFromWorld() * glm::vec4(diagnosticsSettings_.lastInspectWorldPos_, 1.0f));
                const glm::ivec3 dims = inst.volume.dimensions();
                if (local.x >= 0.0f && local.y >= 0.0f && local.z >= 0.0f &&
                    local.x < static_cast<float>(dims.x) &&
                    local.y < static_cast<float>(dims.y) &&
                    local.z < static_cast<float>(dims.z))
                {
                    lastInspectVolumeIndex_ = static_cast<int>(i);
                    lastInspectVoxel_ = glm::ivec3(glm::floor(local));
                    break;
                }
            }
        }
    }

    if (automationPixelInspectMode_ != 0 && !automationPixelInspectValidated_)
    {
        automationPixelInspectValidated_ = true;
        const float normalLength = glm::length(diagnosticsSettings_.lastInspectNormal_);
        const bool validDepth = std::isfinite(diagnosticsSettings_.lastInspectDepth_) && diagnosticsSettings_.lastInspectDepth_ > 0.0f &&
                                diagnosticsSettings_.lastInspectDepth_ < 1.0f;
        const bool validNormal = std::isfinite(normalLength) && normalLength > 0.5f;
        const bool validHitAxis = lastInspectHitAxis_ >= 1 && lastInspectHitAxis_ <= 3;

        if (automationPixelInspectMode_ == 2)
        {
            if (diagnosticsSettings_.lastInspectHasWorld_ && validDepth && validHitAxis)
            {
                logInfo("Automation",
                        makeLogMessage("Pixel inspect hit-axis validation passed axis=",
                                       lastInspectHitAxis_, " depth=", diagnosticsSettings_.lastInspectDepth_,
                                       " world=(", diagnosticsSettings_.lastInspectWorldPos_.x, ", ",
                                       diagnosticsSettings_.lastInspectWorldPos_.y, ", ", diagnosticsSettings_.lastInspectWorldPos_.z,
                                       ")"));
            }
            else
            {
                logAndExit("Automation",
                           makeLogMessage("Pixel inspect hit-axis validation failed axis=",
                                          lastInspectHitAxis_, " depth=", diagnosticsSettings_.lastInspectDepth_,
                                          " hasWorld=", diagnosticsSettings_.lastInspectHasWorld_, " pixel=(",
                                          rb.pixel.x, ", ", rb.pixel.y, ")"),
                           1);
            }
        }
        else
        {
            if (diagnosticsSettings_.lastInspectHasWorld_ && validDepth && validNormal)
            {
                logInfo("Automation",
                        makeLogMessage("Pixel inspect validation passed depth=", diagnosticsSettings_.lastInspectDepth_,
                                       " normalLen=", normalLength, " world=(",
                                       diagnosticsSettings_.lastInspectWorldPos_.x, ", ", diagnosticsSettings_.lastInspectWorldPos_.y, ", ",
                                       diagnosticsSettings_.lastInspectWorldPos_.z, ")"));
            }
            else
            {
                logAndExit("Automation",
                           makeLogMessage("Pixel inspect validation failed depth=",
                                          diagnosticsSettings_.lastInspectDepth_, " normalLen=", normalLength,
                                          " hasWorld=", diagnosticsSettings_.lastInspectHasWorld_, " pixel=(",
                                          rb.pixel.x, ", ", rb.pixel.y, ")"),
                           1);
            }
        }
    }
}

#if VOXEL_WITH_RUNTIME_UI
void App::updateRuntimeUiPixelInspectReadback(uint32_t frameIndex)
{
    if (!automationRuntimeUiPixelInspect_ || automationRuntimeUiPixelInspectValidated_)
    {
        return;
    }
    if (frameIndex >= runtimeUiPixelReadback_.size())
    {
        return;
    }

    auto& rb = runtimeUiPixelReadback_[frameIndex];
    if (!rb.pending || rb.memory == VK_NULL_HANDLE)
    {
        return;
    }

    uint8_t raw[static_cast<size_t>(kRuntimeUiPixelInspectBufferSize)] = {};
    void* mapped = nullptr;
    if (vkMapMemory(ctx_.device, rb.memory, 0, sizeof(raw), 0, &mapped) == VK_SUCCESS)
    {
        std::memcpy(raw, mapped, sizeof(raw));
        vkUnmapMemory(ctx_.device, rb.memory);
    }
    rb.pending = false;

    uint32_t r = 0;
    uint32_t g = 0;
    uint32_t b = 0;
    uint32_t a = 0;
    const VkFormat format = renderer_.swapchainImageFormat();
    if (format == VK_FORMAT_B8G8R8A8_SRGB || format == VK_FORMAT_B8G8R8A8_UNORM)
    {
        b = raw[0];
        g = raw[1];
        r = raw[2];
        a = raw[3];
    }
    else if (format == VK_FORMAT_R8G8B8A8_SRGB || format == VK_FORMAT_R8G8B8A8_UNORM)
    {
        r = raw[0];
        g = raw[1];
        b = raw[2];
        a = raw[3];
    }
    else
    {
        logAndExit("Automation][UIPixel",
                   makeLogMessage("runtime-ui pixel validation failed unsupported format=",
                                  static_cast<int>(format)),
                   1);
    }

    const bool validGreenPatch = g >= 220 && r <= 40 && b <= 40 && a >= 220;
    if (validGreenPatch)
    {
        automationRuntimeUiPixelInspectValidated_ = true;
        logInfo("Automation][UIPixel",
                makeLogMessage("runtime-ui pixel validation passed rgba=(", r, ",", g, ",", b,
                               ",", a, ") pixel=(", rb.pixel.x, ",", rb.pixel.y, ")"));
    }
    else
    {
        logAndExit("Automation][UIPixel",
                   makeLogMessage("runtime-ui pixel validation failed rgba=(", r, ",", g, ",", b,
                                  ",", a, ") pixel=(", rb.pixel.x, ",", rb.pixel.y, ")"),
                   1);
    }
}
#endif

void App::recordPixelInspectCopy(VkCommandBuffer cmd, uint32_t frameIndex, uint32_t debugMode)
{
    if (!diagnosticsSettings_.pixelInspectEnabled_)
    {
        return;
    }
    if (diagnosticsSettings_.inspectPixel_.x < 0 || diagnosticsSettings_.inspectPixel_.y < 0)
    {
        return;
    }
    if (frameIndex >= pixelInspectReadback_.size())
    {
        return;
    }

    VkExtent2D extent = renderPasses().gbuffer.extent();
    if (extent.width == 0 || extent.height == 0)
    {
        return;
    }

    int winW = 0;
    int winH = 0;
    glfwGetWindowSize(window_, &winW, &winH);
    float scaleX = 1.0f;
    float scaleY = 1.0f;
    if (winW > 0 && winH > 0)
    {
        scaleX = static_cast<float>(extent.width) / static_cast<float>(winW);
        scaleY = static_cast<float>(extent.height) / static_cast<float>(winH);
    }

    const int x = std::clamp(static_cast<int>(diagnosticsSettings_.inspectPixel_.x * scaleX), 0,
                             static_cast<int>(extent.width) - 1);
    const int y = std::clamp(static_cast<int>(diagnosticsSettings_.inspectPixel_.y * scaleY), 0,
                             static_cast<int>(extent.height) - 1);

    auto& rb = pixelInspectReadback_[frameIndex];
    if (rb.buffer == VK_NULL_HANDLE)
    {
        return;
    }
    rb.pending = true;
    rb.pixel = glm::ivec2(x, y);
    rb.debugMode = debugMode;

    VkImage normalImage = renderPasses().gbuffer.color(frameIndex, GBufferPass::Slot::Normal).image;
    VkImage depthImage = renderPasses().gbuffer.depth(frameIndex).image;

    std::array<VkImageMemoryBarrier, 2> toTransfer{};
    toTransfer[0].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    toTransfer[0].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    toTransfer[0].dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    toTransfer[0].oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    toTransfer[0].newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    toTransfer[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toTransfer[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toTransfer[0].image = normalImage;
    toTransfer[0].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    toTransfer[0].subresourceRange.baseMipLevel = 0;
    toTransfer[0].subresourceRange.levelCount = 1;
    toTransfer[0].subresourceRange.baseArrayLayer = 0;
    toTransfer[0].subresourceRange.layerCount = 1;

    toTransfer[1].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    toTransfer[1].srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    toTransfer[1].dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    toTransfer[1].oldLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
    toTransfer[1].newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    toTransfer[1].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toTransfer[1].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toTransfer[1].image = depthImage;
    toTransfer[1].subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    toTransfer[1].subresourceRange.baseMipLevel = 0;
    toTransfer[1].subresourceRange.levelCount = 1;
    toTransfer[1].subresourceRange.baseArrayLayer = 0;
    toTransfer[1].subresourceRange.layerCount = 1;

    vkCmdPipelineBarrier(cmd,
                         VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                             VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr,
                         static_cast<uint32_t>(toTransfer.size()), toTransfer.data());

    VkBufferImageCopy normalCopy{};
    normalCopy.bufferOffset = kPixelInspectNormalOffset;
    normalCopy.bufferRowLength = 0;
    normalCopy.bufferImageHeight = 0;
    normalCopy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    normalCopy.imageSubresource.mipLevel = 0;
    normalCopy.imageSubresource.baseArrayLayer = 0;
    normalCopy.imageSubresource.layerCount = 1;
    normalCopy.imageOffset = {x, y, 0};
    normalCopy.imageExtent = {1, 1, 1};

    vkCmdCopyImageToBuffer(cmd, normalImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, rb.buffer, 1,
                           &normalCopy);

    VkBufferImageCopy depthCopy{};
    depthCopy.bufferOffset = kPixelInspectDepthOffset;
    depthCopy.bufferRowLength = 0;
    depthCopy.bufferImageHeight = 0;
    depthCopy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    depthCopy.imageSubresource.mipLevel = 0;
    depthCopy.imageSubresource.baseArrayLayer = 0;
    depthCopy.imageSubresource.layerCount = 1;
    depthCopy.imageOffset = {x, y, 0};
    depthCopy.imageExtent = {1, 1, 1};

    vkCmdCopyImageToBuffer(cmd, depthImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, rb.buffer, 1,
                           &depthCopy);

    std::array<VkImageMemoryBarrier, 2> toRead{};
    toRead[0].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    toRead[0].srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    toRead[0].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    toRead[0].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    toRead[0].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    toRead[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toRead[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toRead[0].image = normalImage;
    toRead[0].subresourceRange = toTransfer[0].subresourceRange;

    toRead[1].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    toRead[1].srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    toRead[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    toRead[1].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    toRead[1].newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
    toRead[1].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toRead[1].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toRead[1].image = depthImage;
    toRead[1].subresourceRange = toTransfer[1].subresourceRange;

    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         0, 0, nullptr, 0, nullptr, static_cast<uint32_t>(toRead.size()),
                         toRead.data());
}

#if VOXEL_WITH_RUNTIME_UI
void App::recordRuntimeUiPixelInspectCopy(VkCommandBuffer cmd, const FrameContext& fc)
{
    if (!automationRuntimeUiPixelInspect_ || automationRuntimeUiPixelInspectValidated_)
    {
        return;
    }
    if (!renderer_.swapchainSupportsTransferSrc())
    {
        logAndExit("Automation][UIPixel",
                   "runtime-ui pixel validation requires swapchain TRANSFER_SRC support.", 1);
    }
    if (fc.frameIndex >= runtimeUiPixelReadback_.size() ||
        fc.swapImageIndex >= renderer_.swapchainImageCount() || fc.extent.width == 0 ||
        fc.extent.height == 0)
    {
        return;
    }

    auto& rb = runtimeUiPixelReadback_[fc.frameIndex];
    if (rb.buffer == VK_NULL_HANDLE)
    {
        return;
    }

    const int x = std::clamp(automationRuntimeUiPixelInspectPoint_.x, 0,
                             static_cast<int>(fc.extent.width) - 1);
    const int y = std::clamp(automationRuntimeUiPixelInspectPoint_.y, 0,
                             static_cast<int>(fc.extent.height) - 1);

    rb.pending = true;
    rb.pixel = glm::ivec2(x, y);

    VkImage swapImage = renderer_.swapchainImage(fc.swapImageIndex);

    VkImageMemoryBarrier toTransfer{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    toTransfer.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    toTransfer.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    toTransfer.oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    toTransfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    toTransfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toTransfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toTransfer.image = swapImage;
    toTransfer.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    toTransfer.subresourceRange.baseMipLevel = 0;
    toTransfer.subresourceRange.levelCount = 1;
    toTransfer.subresourceRange.baseArrayLayer = 0;
    toTransfer.subresourceRange.layerCount = 1;

    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1,
                         &toTransfer);

    VkBufferImageCopy copy{};
    copy.bufferOffset = 0;
    copy.bufferRowLength = 0;
    copy.bufferImageHeight = 0;
    copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copy.imageSubresource.mipLevel = 0;
    copy.imageSubresource.baseArrayLayer = 0;
    copy.imageSubresource.layerCount = 1;
    copy.imageOffset = {x, y, 0};
    copy.imageExtent = {1, 1, 1};

    vkCmdCopyImageToBuffer(cmd, swapImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, rb.buffer, 1,
                           &copy);

    VkImageMemoryBarrier toPresent = toTransfer;
    toPresent.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    toPresent.dstAccessMask = 0;
    toPresent.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    toPresent.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &toPresent);
}
#endif

#if VOXEL_WITH_EDITOR
void App::syncEditorStateFromInput()
{
    diagnosticsSettings_.viewMode_ = input_.gbufferMode();
    app::syncPresentationSettingsFromInput(
        input_, lightingSettings_, postFxSettings_,
        std::min<uint32_t>(
            lights_.maxLights,
            static_cast<uint32_t>(lightingSettings_.areaLights_.size())));
    lightingSettings_.lightHeatmap_ = input_.heatmap();
    lightingSettings_.cascadeDebug_ = input_.cascadeDebug();

    postFxSettings_.postDebugMode_ = input_.postDebugMode();

    waterSettings_.waterEnabled_ = input_.waterEnabled();
    waterSettings_.waterDebugMode_ = input_.waterDebugMode();
    glassSettings_.glassEnabled_ = input_.glassEnabled();
    glassSettings_.glassDebugMode_ = input_.glassDebugMode();

    voxelDebugSettings_.voxelVisible_ = input_.voxelWorldEnabled();
    voxelDebugSettings_.chunkBoundsVisible_ = input_.chunkBoundsEnabled();
    voxelDebugSettings_.volumeBoundsVisible_ = input_.volumeBoundsEnabled();
    voxelDebugSettings_.voxelMeshingFrozen_ = input_.voxelMeshingFrozen();
    voxelDebugSettings_.voxelEditMode_ = input_.editModeEnabled();
}

void App::syncInputFromEditorState()
{
    auto toggleByKey = [this](bool desired, bool current, int key) {
        if (desired != current)
        {
            input_.handleKey(key, GLFW_PRESS);
        }
    };

    const int clampedViewMode = std::clamp(diagnosticsSettings_.viewMode_, 0, 7);
    if (clampedViewMode != input_.gbufferMode())
    {
        switch (clampedViewMode)
        {
        case 0:
            input_.handleKey(GLFW_KEY_0, GLFW_PRESS);
            break;
        case 1:
            input_.handleKey(GLFW_KEY_1, GLFW_PRESS);
            break;
        case 2:
            input_.handleKey(GLFW_KEY_2, GLFW_PRESS);
            break;
        case 3:
            input_.handleKey(GLFW_KEY_3, GLFW_PRESS);
            break;
        case 4:
            input_.handleKey(GLFW_KEY_4, GLFW_PRESS);
            break;
        case 5:
            input_.handleKey(GLFW_KEY_5, GLFW_PRESS);
            break;
        case 6:
            input_.handleKey(GLFW_KEY_6, GLFW_PRESS);
            break;
        case 7:
            input_.handleKey(GLFW_KEY_7, GLFW_PRESS);
            break;
        default:
            break;
        }
    }
    diagnosticsSettings_.viewMode_ = clampedViewMode;

    app::syncPresentationInputFromSettings(
        input_, lightingSettings_, postFxSettings_,
        std::min<uint32_t>(
            lights_.maxLights,
            static_cast<uint32_t>(lightingSettings_.areaLights_.size())));
    toggleByKey(lightingSettings_.lightHeatmap_, input_.heatmap(), GLFW_KEY_H);
    toggleByKey(lightingSettings_.cascadeDebug_, input_.cascadeDebug(), GLFW_KEY_C);

    if (postFxSettings_.postDebugMode_ != input_.postDebugMode())
    {
        input_.setPostDebugMode(postFxSettings_.postDebugMode_);
    }
    sceneConfigMutable().useWaterV2 = waterSettings_.useWaterV2_;

    if (waterSettings_.waterEnabled_ != input_.waterEnabled())
    {
        input_.setWaterEnabled(waterSettings_.waterEnabled_);
    }
    if (waterSettings_.waterDebugMode_ != input_.waterDebugMode())
    {
        input_.setWaterDebugMode(waterSettings_.waterDebugMode_);
    }
    if (glassSettings_.glassEnabled_ != input_.glassEnabled())
    {
        input_.setGlassEnabled(glassSettings_.glassEnabled_);
    }
    if (glassSettings_.glassDebugMode_ != input_.glassDebugMode())
    {
        input_.setGlassDebugMode(glassSettings_.glassDebugMode_);
    }

    if (voxelDebugSettings_.voxelVisible_ != input_.voxelWorldEnabled())
    {
        input_.handleKey(GLFW_KEY_F1, GLFW_PRESS);
        sceneObjectsDirty_ = true;
    }
    if (voxelDebugSettings_.chunkBoundsVisible_ != input_.chunkBoundsEnabled())
    {
        input_.handleKey(GLFW_KEY_F3, GLFW_PRESS);
        sceneObjectsDirty_ = true;
    }
    if (voxelDebugSettings_.volumeBoundsVisible_ != input_.volumeBoundsEnabled())
    {
        input_.handleKey(GLFW_KEY_F12, GLFW_PRESS);
        sceneObjectsDirty_ = true;
    }
    toggleByKey(voxelDebugSettings_.voxelMeshingFrozen_, input_.voxelMeshingFrozen(), GLFW_KEY_F4);
    toggleByKey(voxelDebugSettings_.voxelEditMode_, input_.editModeEnabled(), GLFW_KEY_F5);
}
#endif

void App::rebuildFishbowlGlassMesh()
{
    if (fishbowlGlassMesh_.vbo != VK_NULL_HANDLE)
    {
        destroyMeshBuffer(ctx_.device, fishbowlGlassMesh_);
    }

    const auto dome = naturePondGlassDomePlacement(sceneConfig());
    const bool aquariumFishbowl = sceneConfig().loadAquariumTest &&
        sceneConfig().useMeshTankGlass && isFishbowlAquariumScene(sceneConfig());
    if (!dome && !aquariumFishbowl)
    {
        return;
    }

    TankGlassBuilder::FishbowlGlassSpec fishbowlSpec{};
    if (dome)
    {
        fishbowlSpec = dome->shellSpec;
    }
    else
    {
        const auto& volumeInfos = AquariumScene::getVolumeInfos();
        if (volumeInfos.size() <= AquariumScene::VOLUME_TANK_COMPOSITE)
        {
            return;
        }
        const AquariumVolumeInfo& tankInfo =
            volumeInfos[AquariumScene::VOLUME_TANK_COMPOSITE];
        fishbowlSpec.volumeDims = tankInfo.dimensions;
        fishbowlSpec.volumeWorldPos = tankInfo.worldPosition;
        fishbowlSpec.paneThickness = 0.075f;
        fishbowlSpec.radialSegments = 96;
        fishbowlSpec.verticalBands = 26;
        fishbowlSpec.includeBase = true;
        fishbowlSpec.includeRim = true;
    }

    const std::vector<Vertex> shellVertices =
        TankGlassBuilder::buildFishbowlGlassShellVertices(fishbowlSpec);
    if (shellVertices.empty())
    {
        return;
    }

    createMeshBuffer(ctx_, renderer_.commands(), shellVertices, fishbowlGlassMesh_);
    if (dome)
    {
        logInfo("NaturePondGlassDome", makeLogMessage(
            "Built inverted fishbowl glass dome: vertices=", shellVertices.size(),
            " rimHeight=", dome->rimHeight, " apexHeight=", dome->apexHeight,
            " openingRadii=", dome->openingRadii.x, "x", dome->openingRadii.y, "."));
    }
}

bool App::shouldUseModularVoxelGlass() const
{
    return sceneConfig().loadGlassTestScene;
}

void App::rebuildModularGlassMeshes()
{
    if (!shouldUseModularVoxelGlass())
    {
        modularGlassMeshes_.clear(ctx_.device);
        return;
    }

    modularGlassMeshes_.rebuild(ctx_, renderer_.commands(), voxelWorld_, {AquariumMaterial::Glass});
}

void App::rebuildGlassObjectsForCurrentScene()
{
    rebuildGlassObjectsForScene(glassObjects_, &cubeMesh_, &fishbowlGlassMesh_, sceneConfig(),
                                sunDirection_);
    modularGlassMeshes_.appendRenderObjects(glassObjects_, voxelWorld_);
}

void App::rebuildStaticObjects()
{
    objects_.clear();
    if (!sceneConfig().loadTestFloor)
    {
        return;
    }

    const bool hasAssetMesh = assetMesh_.vbo != VK_NULL_HANDLE;
    MeshGpu* primaryMesh = hasAssetMesh ? &assetMesh_ : &cubeMesh_;
    Material* primaryMat = hasAssetMesh ? &assetMaterial_ : &cubeMaterial_;

    Transform center{};
    RenderObject obj0{};
    obj0.model = center.modelMatrix();
    obj0.mesh = primaryMesh;
    obj0.material = primaryMat;
    objects_.push_back(obj0);

    Transform offsetA{};
    offsetA.position = glm::vec3(2.5f, 0.0f, 0.0f);
    RenderObject objA{};
    objA.model = offsetA.modelMatrix();
    objA.mesh = &cubeMesh_;
    objA.material = &cubeMaterial_;
    objects_.push_back(objA);

    Transform offsetB{};
    offsetB.position = glm::vec3(-2.5f, 0.0f, -1.5f);
    offsetB.scale = glm::vec3(0.7f, 0.7f, 0.7f);
    RenderObject objB{};
    objB.model = offsetB.modelMatrix();
    objB.mesh = &cubeMesh_;
    objB.material = &cubeMaterial_;
    objects_.push_back(objB);

    Transform ground{};
    ground.position = glm::vec3(0.0f, -0.5f, 0.0f);
    RenderObject groundObj{};
    groundObj.model = ground.modelMatrix();
    groundObj.mesh = &groundMesh_;
    groundObj.material = &groundMaterial_;
    objects_.push_back(groundObj);
}

void App::rebuildAnimatedObjects()
{
    animatedObjects_.clear();
    proceduralFish_.clear();
    heroFoliage_.clear();
    proceduralFishPoseValid_ = false;
    animatedObjectsDirty_ = true;

    if (sceneConfig().loadAquariumTest &&
        voxelWorld_.instances().size() >= AquariumScene::VOLUME_COUNT)
    {
        const std::vector<PlaceableInstance> heroFoliageInstances =
            FoliageCatalog::aquariumHeroFoliageInstances(&sceneConfig().placeables,
                                                         sceneConfig().useDefaultPlaceables);
        heroFoliage_.reserve(heroFoliageInstances.size());
        for (size_t i = 0; i < heroFoliageInstances.size(); ++i)
        {
            const PlaceableInstance& placeable = heroFoliageInstances[i];
            const FoliagePrototype* prototype = FoliageCatalog::findPrototype(
                placeable.prototypeSlug, placeable.prototypeVersion);
            if (prototype == nullptr)
            {
                continue;
            }

            const int volumeIndex =
                findVoxelInstanceByName(voxelWorld_, heroFoliageVolumeName(i));
            if (volumeIndex < 0 ||
                static_cast<size_t>(volumeIndex) >= voxelWorld_.instances().size())
            {
                continue;
            }

            const auto& volume =
                voxelWorld_.instances()[static_cast<size_t>(volumeIndex)].volume;
            const float seed = static_cast<float>(placeable.seed);
            HeroFoliageInstance foliage{};
            foliage.placeableUuid = placeable.uuid;
            foliage.prototypeSlug = placeable.prototypeSlug;
            foliage.prototypeVersion = placeable.prototypeVersion;
            foliage.seed = placeable.seed;
            foliage.volumeIndex = volumeIndex;
            foliage.rootWorld = volume.worldPosition();
            foliage.scale = volume.worldScale();
            const float seedYaw = kTau * std::fmod(0.17f + seed * 0.271828f, 1.0f);
            foliage.restRotation = engine::game::composePlaceableRotation(placeable.rotation, glm::quat(1.0f, 0.0f, 0.0f, 0.0f), glm::angleAxis(seedYaw, glm::vec3(0.0f, 1.0f, 0.0f))).canonical;
            const float swayAngle = seedYaw + 0.55f + 0.31f * seed;
            foliage.swayDirection = glm::vec2(std::cos(swayAngle), std::sin(swayAngle));
            const float amplitudeJitter =
                0.92f + 0.04f * static_cast<float>(placeable.seed % 5u);
            const float speedJitter =
                0.94f + 0.035f * static_cast<float>((placeable.seed * 3u) % 5u);
            foliage.amplitude = prototype->motion.amplitude * amplitudeJitter;
            foliage.speed = prototype->motion.speed * speedJitter;
            foliage.twistScale =
                prototype->motion.twistScale + 0.015f * std::sin(seed * 1.71f);
            foliage.phase = kTau * std::fmod(0.23f + seed * 0.193f, 1.0f);
            heroFoliage_.push_back(foliage);
            voxelWorld_.setVolumeVisible(static_cast<uint32_t>(volumeIndex), true);
        }
    }

    const std::optional<engine::game::FishHabitat> habitat =
        engine::game::resolveFishHabitat(sceneConfig());
    if (!voxelDebugSettings_.proceduralFishEnabled_ || automationDisableProceduralFish_ ||
        !habitat)
    {
        synchronizePrimaryCreatureBinding(false); setProceduralFishVisibility(false);
        updateAnimatedObjects(static_cast<float>(glfwGetTime()));
        return;
    }

    const glm::vec3 waterMin = habitat->boundsMin;
    const glm::vec3 waterMax = habitat->boundsMax;
    const glm::vec3 waterCenter = habitat->schoolCenter;

    struct FishSeed
    {
        glm::vec3 centerOffsetFrac{0.0f};
        glm::vec3 orbitRadiusFrac{0.0f};
        glm::vec3 wobbleAmplitudeFrac{0.0f};
        float speed = 1.0f;
        float pathPhase = 0.0f;
        float wagPhase = 0.0f;
        float scale = 1.0f;
        size_t paletteIndex = 0;
    };
    const std::array<FishSeed, 10> fishSeeds = {
        FishSeed{{-0.10f, 0.08f, 0.18f}, {0.15f, 0.08f, 0.10f}, {0.03f, 0.02f, 0.03f},
                 0.46f, 0.0f, 0.1f, 0.44f, 0},
        FishSeed{{0.14f, 0.00f, -0.05f}, {0.23f, 0.10f, 0.19f}, {0.05f, 0.04f, 0.05f},
                 0.63f, 1.7f, 1.0f, 0.32f, 1},
        FishSeed{{0.22f, -0.05f, 0.14f}, {0.16f, 0.08f, 0.15f}, {0.04f, 0.03f, 0.04f},
                 0.82f, 3.0f, 2.4f, 0.29f, 2},
        FishSeed{{-0.18f, -0.12f, -0.12f}, {0.12f, 0.06f, 0.10f}, {0.02f, 0.02f, 0.03f},
                 0.54f, 4.2f, 3.8f, 0.39f, 2},
        FishSeed{{-0.02f, 0.12f, 0.05f}, {0.20f, 0.09f, 0.16f}, {0.04f, 0.03f, 0.04f},
                 0.68f, 5.0f, 4.5f, 0.34f, 0},
        FishSeed{{0.05f, -0.15f, -0.18f}, {0.14f, 0.07f, 0.12f}, {0.03f, 0.03f, 0.04f},
                 0.92f, 5.9f, 5.2f, 0.27f, 1},
        FishSeed{{-0.25f, 0.02f, 0.02f}, {0.10f, 0.05f, 0.09f}, {0.02f, 0.02f, 0.02f},
                 0.58f, 2.3f, 0.7f, 0.26f, 0},
        FishSeed{{0.26f, 0.10f, -0.16f}, {0.12f, 0.06f, 0.11f}, {0.02f, 0.02f, 0.03f},
                 0.74f, 3.7f, 1.9f, 0.24f, 1},
        FishSeed{{-0.08f, -0.02f, 0.22f}, {0.18f, 0.07f, 0.13f}, {0.03f, 0.02f, 0.03f},
                 0.88f, 4.9f, 3.2f, 0.30f, 2},
        FishSeed{{0.10f, -0.10f, 0.00f}, {0.16f, 0.05f, 0.10f}, {0.02f, 0.02f, 0.02f},
                 0.52f, 6.4f, 4.8f, 0.25f, 0},
    };
    auto fract01 = [](float value) { return value - std::floor(value); };
    auto hash01 = [&](float seed) { return fract01(std::sin(seed) * 43758.5453f); };
    auto mixFloat = [](float a, float b, float t) { return a + (b - a) * t; };
    auto fishSeedForIndex = [&](size_t index) {
        if (index < fishSeeds.size())
        {
            return fishSeeds[index];
        }

        constexpr size_t kExtraFishColumns = 5;
        constexpr size_t kExtraFishRows = 4;
        constexpr size_t kExtraFishLayers = 2;
        constexpr size_t kExtraFishSlots =
            kExtraFishColumns * kExtraFishRows * kExtraFishLayers;
        const size_t extraIndexInt = index - fishSeeds.size();
        const size_t slot = (extraIndexInt * 17u) % kExtraFishSlots;
        const size_t xCell = slot % kExtraFishColumns;
        const size_t zCell = (slot / kExtraFishColumns) % kExtraFishRows;
        const size_t yCell = (slot / (kExtraFishColumns * kExtraFishRows)) % kExtraFishLayers;
        const float extraIndex = static_cast<float>(extraIndexInt);
        auto cellCenter = [](size_t cell, size_t count) {
            return ((static_cast<float>(cell) + 0.5f) / static_cast<float>(count)) * 2.0f -
                   1.0f;
        };

        FishSeed seed{};
        seed.centerOffsetFrac = glm::vec3(
            std::clamp(cellCenter(xCell, kExtraFishColumns) * 0.56f +
                           mixFloat(-0.045f, 0.045f, hash01(extraIndex * 13.17f + 4.10f)),
                       -0.56f, 0.56f),
            std::clamp(cellCenter(yCell, kExtraFishLayers) * 0.42f +
                           mixFloat(-0.040f, 0.040f, hash01(extraIndex * 7.97f + 2.40f)),
                       -0.42f, 0.42f),
            std::clamp(cellCenter(zCell, kExtraFishRows) * 0.54f +
                           mixFloat(-0.050f, 0.050f, hash01(extraIndex * 6.73f + 3.20f)),
                       -0.54f, 0.54f));
        seed.orbitRadiusFrac = glm::vec3(
            mixFloat(0.030f, 0.075f, hash01(extraIndex * 5.31f + 1.70f)),
            mixFloat(0.018f, 0.045f, hash01(extraIndex * 11.03f + 0.60f)),
            mixFloat(0.030f, 0.075f, hash01(extraIndex * 17.81f + 2.90f)));
        seed.wobbleAmplitudeFrac = glm::vec3(
            mixFloat(0.006f, 0.018f, hash01(extraIndex * 19.27f + 5.30f)),
            mixFloat(0.006f, 0.016f, hash01(extraIndex * 23.11f + 9.70f)),
            mixFloat(0.006f, 0.018f, hash01(extraIndex * 29.43f + 1.90f)));
        seed.speed = mixFloat(0.35f, 0.72f, hash01(extraIndex * 3.71f + 8.40f));
        seed.pathPhase = 6.28318531f * hash01(extraIndex * 2.17f + 0.20f);
        seed.wagPhase = 6.28318531f * hash01(extraIndex * 2.89f + 1.30f);
        seed.scale = mixFloat(0.16f, 0.23f, hash01(extraIndex * 9.41f + 6.60f));
        seed.paletteIndex = index % kFishPaletteCount;
        return seed;
    };

    setProceduralFishVisibility(false);
    const size_t activeFishCount =
        static_cast<size_t>(std::clamp(voxelDebugSettings_.proceduralFishCount_, 0, kMaxProceduralFishCount));
    const float sceneFishScale = isNaturePondScene(sceneConfig()) ? 0.26f : 1.0f;
    proceduralFish_.reserve(activeFishCount);
    for (size_t i = 0; i < activeFishCount; ++i)
    {
        const FishSeed seed = fishSeedForIndex(i);
        ProceduralFishInstance fish{};
        fish.id = nextFishId_++;
        fish.stableSpeciesOrdinal = static_cast<uint32_t>(i);
        fish.centerOffsetFrac = seed.centerOffsetFrac;
        fish.orbitRadiusFrac = seed.orbitRadiusFrac;
        fish.wobbleAmplitudeFrac = seed.wobbleAmplitudeFrac;
        fish.speed = seed.speed;
        fish.pathPhase = seed.pathPhase;
        fish.wagPhase = seed.wagPhase;
        fish.scale = seed.scale * sceneFishScale;
        fish.paletteIndex = seed.paletteIndex % kFishPaletteCount;
        const std::vector<uint8_t> bodyData = buildFishBodyVoxelData(fish.paletteIndex);
        const std::vector<uint8_t> tailData = buildFishTailVoxelData(fish.paletteIndex);

        fish.bodyVolumeIndex = findVoxelInstanceByName(voxelWorld_, fishBodyVolumeName(i));
        if (fish.bodyVolumeIndex < 0)
        {
            engine::VolumeSpec spec{};
            spec.name = fishBodyVolumeName(i);
            spec.dims = kFishBodyVolumeDims;
            spec.position = waterCenter - kFishBodyAnchorLocal * fish.scale;
            spec.flags = engine::VoxelVolume::FLAG_DYNAMIC;
            fish.bodyVolumeIndex = voxelWorld_.addVolume(ctx_, voxelPalette_, spec, bodyData);
        }
        else if (static_cast<size_t>(fish.bodyVolumeIndex) < voxelWorld_.instances().size())
        {
            auto& bodyVolume =
                voxelWorld_.instances()[static_cast<size_t>(fish.bodyVolumeIndex)].volume;
            if (!bodyVolume.upload(ctx_, bodyData, voxelPalette_.buffer()))
            {
                logWarning("AquariumFish",
                           makeLogMessage("Failed to refresh body voxels for fish ", i, "."));
            }
        }

        fish.tailVolumeIndex = findVoxelInstanceByName(voxelWorld_, fishTailVolumeName(i));
        if (fish.tailVolumeIndex < 0)
        {
            engine::VolumeSpec spec{};
            spec.name = fishTailVolumeName(i);
            spec.dims = kFishTailVolumeDims;
            spec.position =
                waterCenter +
                (kFishTailRootFromBodyAnchorLocal - kFishTailAnchorLocal) * fish.scale;
            spec.flags = engine::VoxelVolume::FLAG_DYNAMIC;
            fish.tailVolumeIndex = voxelWorld_.addVolume(ctx_, voxelPalette_, spec, tailData);
        }
        else if (static_cast<size_t>(fish.tailVolumeIndex) < voxelWorld_.instances().size())
        {
            auto& tailVolume =
                voxelWorld_.instances()[static_cast<size_t>(fish.tailVolumeIndex)].volume;
            if (!tailVolume.upload(ctx_, tailData, voxelPalette_.buffer()))
            {
                logWarning("AquariumFish",
                           makeLogMessage("Failed to refresh tail voxels for fish ", i, "."));
            }
        }

        if (fish.bodyVolumeIndex < 0 || fish.tailVolumeIndex < 0)
        {
            logError("AquariumFish",
                     makeLogMessage("Failed to create voxel fish volumes for fish ", i, "."));
            continue;
        }

        proceduralFish_.push_back(fish);
    }

    if (voxelDebugSettings_.axolotlEnabled_)
    {
        ProceduralFishInstance axolotl{};
        axolotl.id = nextFishId_++;
        axolotl.stableSpeciesOrdinal = 0;
        axolotl.isAxolotl = true;
        // bottom-dwelling stroll: hugs the lower tank, slow and hero-sized.
        axolotl.centerOffsetFrac = glm::vec3(0.04f, -0.34f, 0.02f);
        axolotl.orbitRadiusFrac = glm::vec3(0.19f, 0.03f, 0.15f);
        axolotl.wobbleAmplitudeFrac = glm::vec3(0.02f, 0.012f, 0.02f);
        axolotl.speed = 0.30f;
        axolotl.pathPhase = 2.4f;
        axolotl.wagPhase = 0.8f;
        axolotl.scale = 0.46f * sceneFishScale;
        const engine::game::AxolotlPaletteBandIds axolotlBands = axolotlPaletteBandIds();
        const std::vector<uint8_t> axolotlBodyData =
            engine::game::buildAxolotlBodyVoxelData(axolotlBands);
        const std::vector<uint8_t> axolotlTailData =
            engine::game::buildAxolotlTailVoxelData(axolotlBands);
        const std::vector<uint8_t> axolotlLimbData =
            engine::game::buildAxolotlLimbVoxelData(axolotlBands);

        axolotl.bodyVolumeIndex = findVoxelInstanceByName(voxelWorld_, axolotlBodyVolumeName());
        if (axolotl.bodyVolumeIndex < 0)
        {
            engine::VolumeSpec spec{};
            spec.name = axolotlBodyVolumeName();
            spec.dims = engine::game::kAxolotlBodyVolumeDims;
            spec.position = waterCenter - kFishBodyAnchorLocal * axolotl.scale;
            spec.flags = engine::VoxelVolume::FLAG_DYNAMIC;
            axolotl.bodyVolumeIndex =
                voxelWorld_.addVolume(ctx_, voxelPalette_, spec, axolotlBodyData);
        }
        else if (static_cast<size_t>(axolotl.bodyVolumeIndex) < voxelWorld_.instances().size())
        {
            auto& bodyVolume =
                voxelWorld_.instances()[static_cast<size_t>(axolotl.bodyVolumeIndex)].volume;
            if (!bodyVolume.upload(ctx_, axolotlBodyData, voxelPalette_.buffer()))
            {
                logWarning("AquariumFish", "Failed to refresh axolotl body voxels.");
            }
        }

        axolotl.tailVolumeIndex = findVoxelInstanceByName(voxelWorld_, axolotlTailVolumeName());
        if (axolotl.tailVolumeIndex < 0)
        {
            engine::VolumeSpec spec{};
            spec.name = axolotlTailVolumeName();
            spec.dims = engine::game::kAxolotlTailVolumeDims;
            spec.position =
                waterCenter +
                (kFishTailRootFromBodyAnchorLocal - kFishTailAnchorLocal) * axolotl.scale;
            spec.flags = engine::VoxelVolume::FLAG_DYNAMIC;
            axolotl.tailVolumeIndex =
                voxelWorld_.addVolume(ctx_, voxelPalette_, spec, axolotlTailData);
        }
        else if (static_cast<size_t>(axolotl.tailVolumeIndex) < voxelWorld_.instances().size())
        {
            auto& tailVolume =
                voxelWorld_.instances()[static_cast<size_t>(axolotl.tailVolumeIndex)].volume;
            if (!tailVolume.upload(ctx_, axolotlTailData, voxelPalette_.buffer()))
            {
                logWarning("AquariumFish", "Failed to refresh axolotl tail voxels.");
            }
        }

        bool axolotlLimbsReady = true;
        for (int limb = 0; limb < engine::game::kAxolotlLimbCount; ++limb)
        {
            int& limbVolumeIndex = axolotl.axolotlLimbVolumeIndices[static_cast<size_t>(limb)];
            limbVolumeIndex =
                findVoxelInstanceByName(voxelWorld_, axolotlLimbVolumeName(limb));
            if (limbVolumeIndex < 0)
            {
                engine::VolumeSpec spec{};
                spec.name = axolotlLimbVolumeName(limb);
                spec.dims = engine::game::kAxolotlLimbVolumeDims;
                spec.position = waterCenter;
                spec.flags = engine::VoxelVolume::FLAG_DYNAMIC;
                limbVolumeIndex =
                    voxelWorld_.addVolume(ctx_, voxelPalette_, spec, axolotlLimbData);
            }
            else if (static_cast<size_t>(limbVolumeIndex) < voxelWorld_.instances().size())
            {
                auto& limbVolume =
                    voxelWorld_.instances()[static_cast<size_t>(limbVolumeIndex)].volume;
                if (!limbVolume.upload(ctx_, axolotlLimbData, voxelPalette_.buffer()))
                {
                    logWarning("AquariumFish",
                               makeLogMessage("Failed to refresh axolotl limb voxels for limb ",
                                              limb, "."));
                }
            }

            axolotlLimbsReady = axolotlLimbsReady && limbVolumeIndex >= 0;
        }

        if (axolotl.bodyVolumeIndex >= 0 && axolotl.tailVolumeIndex >= 0)
        {
            if (!axolotlLimbsReady)
            {
                logWarning("AquariumFish",
                           "One or more axolotl limb volumes failed to initialize.");
            }
            proceduralFish_.push_back(axolotl);
        }
        else
        {
            logError("AquariumFish", "Failed to create voxel volumes for the axolotl.");
        }
    }

    synchronizePrimaryCreatureBinding(); updateAnimatedObjects(static_cast<float>(glfwGetTime()));
    for (const ProceduralFishInstance& fish : proceduralFish_)
    {
        if (fish.bodyVolumeIndex >= 0)
        {
            voxelWorld_.setVolumeVisible(static_cast<uint32_t>(fish.bodyVolumeIndex), true);
        }
        if (fish.tailVolumeIndex >= 0)
        {
            voxelWorld_.setVolumeVisible(static_cast<uint32_t>(fish.tailVolumeIndex), true);
        }
        if (fish.isAxolotl)
        {
            for (int limbVolumeIndex : fish.axolotlLimbVolumeIndices)
            {
                if (limbVolumeIndex >= 0)
                {
                    voxelWorld_.setVolumeVisible(static_cast<uint32_t>(limbVolumeIndex), true);
                }
            }
        }
    }
}

void App::setProceduralFishVisibility(bool visible)
{
    for (size_t i = 0; i < static_cast<size_t>(kMaxProceduralFishCount); ++i)
    {
        const int bodyIndex = findVoxelInstanceByName(voxelWorld_, fishBodyVolumeName(i));
        if (bodyIndex >= 0)
        {
            voxelWorld_.setVolumeVisible(static_cast<uint32_t>(bodyIndex), visible);
        }
        const int tailIndex = findVoxelInstanceByName(voxelWorld_, fishTailVolumeName(i));
        if (tailIndex >= 0)
        {
            voxelWorld_.setVolumeVisible(static_cast<uint32_t>(tailIndex), visible);
        }
    }

    const int axolotlBodyIndex = findVoxelInstanceByName(voxelWorld_, axolotlBodyVolumeName());
    if (axolotlBodyIndex >= 0)
    {
        voxelWorld_.setVolumeVisible(static_cast<uint32_t>(axolotlBodyIndex), visible);
    }
    const int axolotlTailIndex = findVoxelInstanceByName(voxelWorld_, axolotlTailVolumeName());
    if (axolotlTailIndex >= 0)
    {
        voxelWorld_.setVolumeVisible(static_cast<uint32_t>(axolotlTailIndex), visible);
    }
    for (int limb = 0; limb < engine::game::kAxolotlLimbCount; ++limb)
    {
        const int limbIndex =
            findVoxelInstanceByName(voxelWorld_, axolotlLimbVolumeName(limb));
        if (limbIndex >= 0)
        {
            voxelWorld_.setVolumeVisible(static_cast<uint32_t>(limbIndex), visible);
        }
    }
}

void App::setProceduralFishEnabled(bool enabled)
{
    if (voxelDebugSettings_.proceduralFishEnabled_ == enabled)
    {
        return;
    }

    voxelDebugSettings_.proceduralFishEnabled_ = enabled;

    if (!engine::game::sceneSupportsFishHabitat(sceneConfig()))
    {
        return;
    }

    if (enabled)
    {
        rebuildAnimatedObjects();
    }
    else
    {
        proceduralFish_.clear(); synchronizePrimaryCreatureBinding();
        proceduralFishPoseValid_ = false;
        setProceduralFishVisibility(false);
        animatedObjectsDirty_ = true;
    }

    sceneObjectsDirty_ = true;
    voxelObjectsDirty_ = true;
}

void App::setProceduralFishCount(int count)
{
    const int clampedCount = std::clamp(count, 0, kMaxProceduralFishCount);
    if (voxelDebugSettings_.proceduralFishCount_ == clampedCount)
    {
        return;
    }

    voxelDebugSettings_.proceduralFishCount_ = clampedCount;
    proceduralFishPoseValid_ = false;

    if (!engine::game::sceneSupportsFishHabitat(sceneConfig()) ||
        automationDisableProceduralFish_)
    {
        return;
    }

    if (voxelDebugSettings_.proceduralFishEnabled_)
    {
        rebuildAnimatedObjects();
    }
    else
    {
        setProceduralFishVisibility(false);
    }

    animatedObjectsDirty_ = true;
    sceneObjectsDirty_ = true;
    voxelObjectsDirty_ = true;
}

void App::setAxolotlEnabled(bool enabled)
{
    if (voxelDebugSettings_.axolotlEnabled_ == enabled)
    {
        return;
    }

    voxelDebugSettings_.axolotlEnabled_ = enabled;
    proceduralFishPoseValid_ = false;

    if (!engine::game::sceneSupportsFishHabitat(sceneConfig()) ||
        automationDisableProceduralFish_)
    {
        return;
    }

    if (voxelDebugSettings_.proceduralFishEnabled_)
    {
        // rebuild respawns the school with or without the axolotl; the rebuild's
        // visibility reset hides the axolotl volumes when it is excluded.
        rebuildAnimatedObjects();
    }
    else
    {
        setProceduralFishVisibility(false);
    }

    animatedObjectsDirty_ = true;
    sceneObjectsDirty_ = true;
    voxelObjectsDirty_ = true;
}

void App::updateAnimatedObjects(float timeSeconds)
{
    if (proceduralFish_.empty() && heroFoliage_.empty())
    {
        return;
    }
    if (proceduralFishPoseValid_ && std::abs(timeSeconds - proceduralFishPoseTime_) <= 1e-4f)
    {
        return;
    }

    const float frameDt =
        proceduralFishPoseValid_ ? (timeSeconds - proceduralFishPoseTime_) : 0.0f;
    auto& fishCelebration = gameRuntime_.fishCelebration();
    fishCelebration.update(frameDt);
    const float fishPathTime = timeSeconds - fishCelebration.pathTimeOffset();

    auto& bellyFloat = gameRuntime_.axolotlBellyFloat();
    const uint64_t focusedId = focusedFishId();
    const ProceduralFishInstance* focusedFish = findFishById(focusedId);
    const bool focusedAxolotl = focusedFish != nullptr && focusedFish->isAxolotl;
    bellyFloat.update(frameDt, focusedAxolotl);

    for (const HeroFoliageInstance& foliage : heroFoliage_)
    {
        if (foliage.volumeIndex < 0)
        {
            continue;
        }

        const float primarySway =
            std::sin(timeSeconds * foliage.speed + foliage.phase) * foliage.amplitude;
        const float secondarySway =
            std::sin(timeSeconds * foliage.speed * 1.63f + foliage.phase * 0.71f) *
            foliage.amplitude * foliage.twistScale;
        const glm::vec3 swayAxis =
            glm::normalize(glm::vec3(-foliage.swayDirection.y, 0.0f,
                                     foliage.swayDirection.x));
        const glm::quat sway = glm::angleAxis(primarySway, swayAxis);
        const glm::quat subtleTwist =
            glm::angleAxis(secondarySway, glm::vec3(0.0f, 1.0f, 0.0f));
        const glm::quat rotation =
            glm::normalize(subtleTwist * sway * foliage.restRotation);
        voxelWorld_.setInstanceTransform(static_cast<uint32_t>(foliage.volumeIndex),
                                         foliage.rootWorld, rotation, foliage.scale);
    }

    if (proceduralFish_.empty())
    {
        proceduralFishPoseTime_ = timeSeconds;
        proceduralFishPoseValid_ = true;
        return;
    }

    const std::optional<engine::game::FishHabitat> habitat =
        engine::game::resolveFishHabitat(sceneConfig());
    if (!habitat)
    {
        proceduralFishPoseTime_ = timeSeconds;
        proceduralFishPoseValid_ = true;
        return;
    }

    const glm::vec3 waterMin = habitat->boundsMin;
    const glm::vec3 waterMax = habitat->boundsMax;
    const glm::vec3 waterCenter = habitat->schoolCenter;
    const glm::vec3 halfExtent = (waterMax - waterMin) * 0.5f;
    const uint64_t careFishId = gameRuntime_.primaryCreatureRuntimeId();
    const auto careMotion = gameRuntime_.primaryCreaturePresentation(sceneConfig().gameState);
    auto samplePosition = [&](const ProceduralFishInstance& fish, float t) {
        const glm::vec3 center = waterCenter + fish.centerOffsetFrac * halfExtent;
        const glm::vec3 orbit = fish.orbitRadiusFrac * halfExtent;
        const float careScale = fish.id == careFishId ? careMotion.pathWobbleScale : 1.0f;
        const glm::vec3 wobble = fish.wobbleAmplitudeFrac * halfExtent * careScale;
        const float phase = t * fish.speed + fish.pathPhase;
        glm::vec3 pos = center;
        pos.x += std::cos(phase) * orbit.x;
        pos.y += std::sin(phase * 1.31f + 0.6f * fish.pathPhase) * orbit.y;
        pos.z += std::sin(phase * 1.11f + 0.3f * fish.pathPhase) * orbit.z;
        pos.x += std::sin(phase * 2.37f + fish.wagPhase) * wobble.x;
        pos.y += std::sin(phase * 2.93f + fish.pathPhase) * wobble.y;
        pos.z += std::cos(phase * 2.41f + fish.wagPhase) * wobble.z;
        return pos;
    };
    for (const ProceduralFishInstance& fish : proceduralFish_)
    {
        if (fish.bodyVolumeIndex < 0 || fish.tailVolumeIndex < 0)
        {
            continue;
        }
        const glm::vec3 scaleVec(fish.scale);
        const glm::vec3 position = samplePosition(fish, fishPathTime);
        const glm::vec3 velocity = samplePosition(fish, fishPathTime + 0.08f) -
                                   samplePosition(fish, fishPathTime - 0.08f);
        const glm::vec3 forward = safeNormalizeOr(velocity, glm::vec3(1.0f, 0.0f, 0.0f));
        float yaw = std::atan2(-forward.z, forward.x);
        const float planarSpeed =
            std::max(0.001f, glm::length(glm::vec2(velocity.x, velocity.z)));
        float pitch = std::clamp(velocity.y / planarSpeed, -0.24f, 0.24f);
        if (fishCelebration.active())
        {
            const auto facing =
                fishCelebration.faceCamera(yaw, pitch, position, camera_.position);
            yaw = facing.yaw;
            pitch = facing.pitch;
        }
        const float heroBlend = clamp01((fish.scale - 0.30f) / 0.26f);
        const bool focusedAxolotlInstance = fish.isAxolotl && fish.id == focusedId;
        const float bodyCareScale = fish.id == careFishId ? careMotion.bodyMotionScale : 1.0f;
        const float tailCareScale = fish.id == careFishId ? careMotion.tailMotionScale : 1.0f;
        const float axolotlFocusSeconds =
            focusedAxolotlInstance ? bellyFloat.activeSeconds() : 0.0f;
        const engine::game::AxolotlFocusPoseOffsets axolotlFocusPose =
            engine::game::axolotlFocusPoseOffsets(timeSeconds, fish.pathPhase,
                                                  fish.wagPhase, axolotlFocusSeconds);
        const float rollAmp = std::lerp(0.060f, 0.035f, heroBlend) * bodyCareScale;
        const float bodyYawAmp = std::lerp(0.14f, 0.09f, heroBlend) * bodyCareScale;
        const float tailYawAmp = std::lerp(0.58f, 0.40f, heroBlend) * tailCareScale;
        float roll = rollAmp * std::sin(fishPathTime * fish.speed * 2.2f + fish.pathPhase);
        if (fish.isAxolotl)
        {
            // belly-float celebration: the focused axolotl rolls over and swims
            // on its back; tail and limbs follow through the body rotation.
            roll += bellyFloat.rollRadians();
            roll += axolotlFocusPose.rollRadians;
            pitch += axolotlFocusPose.pitchRadians;
        }
        const float wag = fishPathTime * (5.4f + fish.speed * 1.2f) + fish.wagPhase;

        float bodyYaw = bodyYawAmp * std::sin(wag - 0.45f);
        bodyYaw += fishCelebration.bodyWiggle(timeSeconds, fish.wagPhase);
        bodyYaw += axolotlFocusPose.bodyYawRadians;
        const float tailYaw = tailYawAmp * std::sin(wag - 1.10f);

        glm::mat4 bodyRotMat(1.0f);
        bodyRotMat = glm::rotate(bodyRotMat, yaw, glm::vec3(0.0f, 1.0f, 0.0f));
        bodyRotMat = glm::rotate(bodyRotMat, pitch, glm::vec3(0.0f, 0.0f, 1.0f));
        bodyRotMat = glm::rotate(bodyRotMat, roll, glm::vec3(1.0f, 0.0f, 0.0f));
        bodyRotMat = glm::rotate(bodyRotMat, bodyYaw, glm::vec3(0.0f, 1.0f, 0.0f));

        const glm::quat bodyRotation = glm::normalize(glm::quat_cast(bodyRotMat));
        const glm::mat3 bodyBasis = glm::mat3_cast(bodyRotation);
        const glm::vec3 bodyWorldPos = position - bodyBasis * (scaleVec * kFishBodyAnchorLocal);

        const glm::quat tailLocalRotation =
            glm::angleAxis(tailYaw, glm::vec3(0.0f, 1.0f, 0.0f));
        const glm::quat tailRotation = glm::normalize(bodyRotation * tailLocalRotation);
        const glm::mat3 tailBasis = glm::mat3_cast(tailRotation);
        const glm::vec3 tailRootWorld =
            position + bodyBasis * (scaleVec * kFishTailRootFromBodyAnchorLocal);
        const glm::vec3 tailWorldPos =
            tailRootWorld - tailBasis * (scaleVec * kFishTailAnchorLocal);

        voxelWorld_.setInstanceTransform(static_cast<uint32_t>(fish.bodyVolumeIndex), bodyWorldPos,
                                         bodyRotation, scaleVec);
        voxelWorld_.setInstanceTransform(static_cast<uint32_t>(fish.tailVolumeIndex), tailWorldPos,
                                         tailRotation, scaleVec);

        if (fish.isAxolotl)
        {
            const float limbCycle =
                fishPathTime * (2.15f + fish.speed * 0.9f) + fish.wagPhase;
            for (size_t limb = 0; limb < fish.axolotlLimbVolumeIndices.size(); ++limb)
            {
                const int limbVolumeIndex = fish.axolotlLimbVolumeIndices[limb];
                if (limbVolumeIndex < 0)
                {
                    continue;
                }

                const AxolotlLimbRig& rig = kAxolotlLimbRig[limb];
                const float limbPhase = limbCycle + rig.phaseOffset;
                const float limbIndex = static_cast<float>(limb);
                const float swing = 0.28f * std::sin(limbPhase);
                const float splay = -static_cast<float>(rig.sideSign) * 0.16f;
                const float toeWiggle = engine::game::axolotlToeWiggleRadians(
                    limbCycle, limbIndex, fish.pathPhase, axolotlFocusSeconds);
                const float focusPawLift = engine::game::axolotlPawLiftLocal(
                    timeSeconds, limbIndex, fish.wagPhase, axolotlFocusSeconds);
                const glm::quat limbLocalRotation = glm::normalize(
                    glm::angleAxis(splay + focusPawLift * 0.35f,
                                   glm::vec3(1.0f, 0.0f, 0.0f)) *
                    glm::angleAxis(toeWiggle, glm::vec3(0.0f, 1.0f, 0.0f)) *
                    glm::angleAxis(swing, glm::vec3(0.0f, 0.0f, 1.0f)));
                const glm::quat limbRotation = glm::normalize(bodyRotation * limbLocalRotation);
                const glm::mat3 limbBasis = glm::mat3_cast(limbRotation);
                glm::vec3 attachWorld =
                    bodyWorldPos + bodyBasis * (scaleVec * rig.attachLocal);
                attachWorld +=
                    bodyBasis * (scaleVec * glm::vec3(0.0f, focusPawLift, 0.0f));
                const glm::vec3 limbWorldPos =
                    attachWorld -
                    limbBasis * (scaleVec * engine::game::kAxolotlLimbAnchorLocal);
                voxelWorld_.setInstanceTransform(static_cast<uint32_t>(limbVolumeIndex),
                                                 limbWorldPos, limbRotation, scaleVec);
            }
        }
    }

    proceduralFishPoseTime_ = timeSeconds;
    proceduralFishPoseValid_ = true;
}

const App::ProceduralFishInstance* App::findFishById(uint64_t fishId) const
{
    if (fishId == 0)
    {
        return nullptr;
    }
    for (const ProceduralFishInstance& fish : proceduralFish_)
    {
        if (fish.id == fishId)
        {
            return &fish;
        }
    }
    return nullptr;
}

glm::vec3 App::fishWorldPosition(const ProceduralFishInstance& fish) const
{
    const auto& instances = voxelWorld_.instances();
    if (fish.bodyVolumeIndex < 0 ||
        static_cast<size_t>(fish.bodyVolumeIndex) >= instances.size())
    {
        return glm::vec3(0.0f);
    }
    const auto& volume = instances[static_cast<size_t>(fish.bodyVolumeIndex)].volume;
    return (volume.worldAabbMin() + volume.worldAabbMax()) * 0.5f;
}

std::vector<App::FishHandle> App::fishList() const
{
    std::vector<FishHandle> handles;
    handles.reserve(proceduralFish_.size());
    for (size_t i = 0; i < proceduralFish_.size(); ++i)
    {
        const ProceduralFishInstance& fish = proceduralFish_[i];
        if (fish.id == 0 || fish.bodyVolumeIndex < 0)
        {
            continue;
        }
        FishHandle handle{};
        handle.id = fish.id;
        handle.index = static_cast<int>(i);
        handle.paletteIndex = fish.paletteIndex;
        handle.scale = fish.scale;
        handle.isAxolotl = fish.isAxolotl;
        handle.worldPos = fishWorldPosition(fish);
        handles.push_back(handle);
    }
    return handles;
}

bool App::selectFish(uint64_t fishId)
{
    if (fishId == 0)
    {
        clearFishSelection();
        return true;
    }
    const ProceduralFishInstance* fish = findFishById(fishId);
    if (fish == nullptr)
    {
        logWarning("FishFocus",
                   makeLogMessage("Select rejected: fish id=", fishId, " not found."));
        return false;
    }
    selectedFishId_ = fishId;
    const glm::vec3 pos = fishWorldPosition(*fish);
    logInfo("FishFocus",
            makeLogMessage("Fish selected id=", fishId, " palette=", fish->paletteIndex,
                           " pos=(", pos.x, ", ", pos.y, ", ", pos.z, ")"));
    return true;
}

void App::clearFishSelection()
{
    if (selectedFishId_ != 0)
    {
        logInfo("FishFocus",
                makeLogMessage("Fish selection cleared id=", selectedFishId_, "."));
    }
    selectedFishId_ = 0;
}

bool App::focusOnFish(uint64_t fishId)
{
    const ProceduralFishInstance* fish = findFishById(fishId);
    if (fish == nullptr)
    {
        logWarning("FishFocus",
                   makeLogMessage("Focus rejected: fish id=", fishId, " not found."));
        return false;
    }
    if (fishFocus_.fishId == 0)
    {
        // only snapshot the free-cam pose when entering focus from free flight, so
        // refocusing another fish mid-focus still returns to the original pose.
        fishFocus_.savedPosition = camera_.position;
        fishFocus_.savedYaw = camera_.yaw;
        fishFocus_.savedPitch = camera_.pitch;
        fishFocus_.basePosition = camera_.position;
        fishFocus_.baseYaw = camera_.yaw;
        fishFocus_.basePitch = camera_.pitch;
        fishFocus_.shakeTime = 0.0f;
    }
    fishFocus_.fishId = fishId;
    fishFocus_.returning = false;
    fishFocus_.settled = false;
    fishFocus_.timeSinceSettled = 0.0f;
    fishFocus_.timeSinceReturning = 0.0f;
    const glm::vec3 fishPos = fishWorldPosition(*fish);
    glm::vec3 horiz = camera_.position - fishPos;
    horiz.y = 0.0f;
    fishFocus_.framingDirHoriz = safeNormalizeOr(horiz, glm::vec3(0.0f, 0.0f, 1.0f));
    selectedFishId_ = fishId;
    gameRuntime_.fishCelebration().trigger();
    logInfo("FishFocus",
            makeLogMessage("Focus started id=", fishId, " pos=(", fishPos.x, ", ",
                           fishPos.y, ", ", fishPos.z, ")"));
    return true;
}

bool App::releaseFishFocus()
{
    if (fishFocus_.fishId == 0 || fishFocus_.returning)
    {
        return false;
    }
    fishFocus_.returning = true;
    logInfo("FishFocus", makeLogMessage("Focus released id=", fishFocus_.fishId, "."));
    return true;
}

void App::updateFishFocus(float dt)
{
    // selection validation: a scene rebuild or fish-count change degrades gracefully.
    if (selectedFishId_ != 0 && findFishById(selectedFishId_) == nullptr)
    {
        logInfo("FishFocus",
                makeLogMessage("Selected fish id=", selectedFishId_,
                               " no longer exists; clearing selection."));
        selectedFishId_ = 0;
    }

    if (fishFocus_.fishId == 0)
    {
        return;
    }

    if (!fishFocus_.returning && findFishById(fishFocus_.fishId) == nullptr)
    {
        logInfo("FishFocus",
                makeLogMessage("Focused fish id=", fishFocus_.fishId,
                               " no longer exists; releasing focus."));
        fishFocus_.returning = true;
    }

    glm::vec3 desiredPos;
    float desiredYaw = 0.0f;
    float desiredPitch = 0.0f;
    if (fishFocus_.returning)
    {
        desiredPos = fishFocus_.savedPosition;
        desiredYaw = fishFocus_.savedYaw;
        desiredPitch = fishFocus_.savedPitch;
    }
    else
    {
        const ProceduralFishInstance* fish = findFishById(fishFocus_.fishId);
        const glm::vec3 fishPos = fishWorldPosition(*fish);
        // DoF focal distance from the base pose (not the shaken camera) for stability.
        fishFocusCurrentDistance_ = glm::length(fishPos - fishFocus_.basePosition);
        desiredPos = fishPos + fishFocus_.framingDirHoriz * fishFocusSettings_.fishFocusOrbitDistance_ +
                     glm::vec3(0.0f, fishFocusSettings_.fishFocusHeightOffset_, 0.0f);
        const glm::vec3 lookDir = safeNormalizeOr(fishPos - fishFocus_.basePosition,
                                                  glm::vec3(0.0f, 0.0f, -1.0f));
        // Camera convention (Camera::viewMatrix): forward = (cosP*sinY, sinP, -cosP*cosY).
        desiredYaw = std::atan2(lookDir.x, -lookDir.z);
        desiredPitch = std::asin(std::clamp(lookDir.y, -1.0f, 1.0f));
        desiredPitch = std::clamp(desiredPitch, -kPitchLimit, kPitchLimit);
    }

    const auto wrapPi = [](float angle) {
        constexpr float kTwoPi = 6.28318530718f;
        angle = std::fmod(angle + 3.14159265359f, kTwoPi);
        if (angle < 0.0f)
        {
            angle += kTwoPi;
        }
        return angle - 3.14159265359f;
    };

    // exponential smoothing of the base pose, with per-frame steps clamped below the
    // camera-cut thresholds (cameraCutThreshold_ position / 0.9-dot rotation) so the
    // glide never triggers TAA/shadow history resets. shake rides on top afterwards
    // and never feeds back into this loop.
    const float posBlend = 1.0f - std::exp(-fishFocusSettings_.fishFocusPositionDamping_ * dt);
    const float rotBlend = 1.0f - std::exp(-fishFocusSettings_.fishFocusRotationDamping_ * dt);
    constexpr float kMaxAngleStep = 0.30f;

    glm::vec3 posStep = (desiredPos - fishFocus_.basePosition) * posBlend;
    const float maxPosStep = cameraCutThreshold_ * 0.9f;
    const float posStepLen = glm::length(posStep);
    if (posStepLen > maxPosStep)
    {
        posStep *= maxPosStep / posStepLen;
    }
    fishFocus_.basePosition += posStep;

    const float yawErr = wrapPi(desiredYaw - fishFocus_.baseYaw);
    const float pitchErr = desiredPitch - fishFocus_.basePitch;
    fishFocus_.baseYaw += std::clamp(yawErr * rotBlend, -kMaxAngleStep, kMaxAngleStep);
    fishFocus_.basePitch += std::clamp(pitchErr * rotBlend, -kMaxAngleStep, kMaxAngleStep);
    fishFocus_.basePitch = std::clamp(fishFocus_.basePitch, -kPitchLimit, kPitchLimit);

    const float posErr = glm::length(desiredPos - fishFocus_.basePosition);
    const float yawAbs = std::abs(wrapPi(desiredYaw - fishFocus_.baseYaw));
    const float pitchAbs = std::abs(desiredPitch - fishFocus_.basePitch);
    const auto toMm = [](float v) { return static_cast<int>(std::lround(v * 1000.0f)); };

    if (!fishFocus_.returning)
    {
        // the desired position trails the moving fish, so the settle threshold allows
        // for steady-state tracking lag; it exists as a deterministic signal, not a
        // framing guarantee.
        if (!fishFocus_.settled && posErr < 3.0f && yawAbs < 0.15f && pitchAbs < 0.15f)
        {
            fishFocus_.settled = true;
            logInfo("FishFocus",
                    makeLogMessage("Focus settled id=", fishFocus_.fishId,
                                   " pos_err_mm=", toMm(posErr)));
        }
        if (fishFocus_.settled)
        {
            fishFocus_.timeSinceSettled += dt;
        }
    }
    else
    {
        fishFocus_.timeSinceReturning += dt;
        if (posErr < 0.05f && yawAbs < 0.01f && pitchAbs < 0.01f)
        {
            camera_.position = fishFocus_.savedPosition;
            camera_.yaw = fishFocus_.savedYaw;
            camera_.pitch = fishFocus_.savedPitch;
            logInfo("FishFocus",
                    makeLogMessage("Focus return completed id=", fishFocus_.fishId,
                                   " pos_err_mm=", toMm(posErr), " restored=1"));
            fishFocus_ = FishFocusState{};
            return;
        }
    }

    // handheld shake: layered sum-of-sines on the camera/view pose only (never the
    // projection, so it stays orthogonal to taa's sub-pixel jitter). ramps in at
    // focus entry, decays toward an idle floor after settle, and fades out fast
    // during the return so the exact restore snap lands clean.
    fishFocus_.shakeTime += dt;
    glm::vec3 shakeOffset(0.0f);
    float shakeYaw = 0.0f;
    float shakePitch = 0.0f;
    if (fishFocusSettings_.fishShakeEnabled_)
    {
        const float rampIn = 1.0f - std::exp(-2.0f * fishFocus_.shakeTime);
        const float settleEnv =
            fishFocus_.settled
                ? 0.35f + 0.65f * std::exp(-fishFocusSettings_.fishShakeSettleDecay_ *
                                           fishFocus_.timeSinceSettled)
                : 1.0f;
        const float returnFade =
            fishFocus_.returning ? std::exp(-6.0f * fishFocus_.timeSinceReturning)
                                 : 1.0f;
        const float intensity = rampIn * settleEnv * returnFade;

        const auto layered = [](float t, float p0, float p1, float p2) {
            return 0.6f * std::sin(t + p0) + 0.3f * std::sin(t * 2.17f + p1) +
                   0.1f * std::sin(t * 4.73f + p2);
        };
        const float t =
            fishFocus_.shakeTime * fishFocusSettings_.fishShakeFrequency_ * 6.28318530718f;
        shakeYaw = fishFocusSettings_.fishShakeRotationAmplitude_ * intensity * layered(t, 0.0f, 1.7f, 4.2f);
        shakePitch = fishFocusSettings_.fishShakeRotationAmplitude_ * 0.8f * intensity *
                     layered(t * 0.83f, 2.6f, 5.1f, 0.9f);
        const glm::vec3 rightDir(std::cos(fishFocus_.baseYaw), 0.0f,
                                 std::sin(fishFocus_.baseYaw));
        const glm::vec3 upDir(0.0f, 1.0f, 0.0f);
        shakeOffset = fishFocusSettings_.fishShakePositionAmplitude_ * intensity *
                      (rightDir * layered(t * 0.91f, 3.3f, 0.4f, 2.2f) +
                       upDir * 0.7f * layered(t * 1.13f, 5.9f, 2.8f, 1.1f));
    }

    camera_.position = fishFocus_.basePosition + shakeOffset;
    camera_.yaw = fishFocus_.baseYaw + shakeYaw;
    camera_.pitch =
        std::clamp(fishFocus_.basePitch + shakePitch, -kPitchLimit, kPitchLimit);
}

void App::updateFishFocusAutomation()
{
    if (automationUiInputCursor_ == 0)
    {
        const std::vector<FishHandle> fish = fishList();
        if (fish.empty())
        {
            if (renderedFrameCount_ >= 120)
            {
                logAndExit("Automation", "fish_focus_smoke: no procedural fish available.");
            }
            return;
        }
        if (!focusOnFish(fish.front().id))
        {
            logAndExit("Automation", "fish_focus_smoke: focusOnFish rejected.");
        }
        logInfo("Automation][FishFocus",
                makeLogMessage("action=focus frame=", renderedFrameCount_,
                               " id=", fish.front().id));
        automationUiInputCursor_ = 1;
        return;
    }
    if (automationUiInputCursor_ == 1)
    {
        if (fishFocus_.fishId != 0 && fishFocus_.settled)
        {
            const bool dofActive = isFishFocusActive() && !isFishFocusReturning();
            logInfo("Automation][FishFocus",
                    makeLogMessage("dof_active=", dofActive ? 1 : 0, " focus_distance_mm=",
                                   static_cast<int>(std::lround(fishFocusCurrentDistance_ *
                                                                1000.0f))));
            releaseFishFocus();
            logInfo("Automation][FishFocus",
                    makeLogMessage("action=release frame=", renderedFrameCount_));
            automationUiInputCursor_ = 2;
        }
        return;
    }
    if (automationUiInputCursor_ == 2 && fishFocus_.fishId == 0)
    {
        logInfo("Automation][FishFocus",
                makeLogMessage("validation passed frame=", renderedFrameCount_,
                               " focus_inactive=1"));
        automationUiScriptCompleted_ = true;
        logInfo("Automation][UIScript", "script=fish_focus_smoke completed=1");
    }
}

void App::appendAxolotlCreaturePolishObjects(float timeSeconds)
{
    if (cubeMesh_.vbo == VK_NULL_HANDLE || cubeMaterial_.set == VK_NULL_HANDLE ||
        waterFoamMaterial_.set == VK_NULL_HANDLE)
    {
        return;
    }

    const auto& instances = voxelWorld_.instances();
    if (instances.empty())
    {
        return;
    }

    const uint64_t focusedId = focusedFishId();
    const float focusSeconds = gameRuntime_.axolotlBellyFloat().activeSeconds();
    std::vector<engine::game::AxolotlPolishObject> polishObjects;
    polishObjects.reserve(engine::game::axolotlCreaturePolishReserve(proceduralFish_.size()));

    for (const ProceduralFishInstance& fish : proceduralFish_)
    {
        if (!fish.isAxolotl || fish.bodyVolumeIndex < 0 ||
            static_cast<size_t>(fish.bodyVolumeIndex) >= instances.size())
        {
            continue;
        }

        const engine::VoxelInstance& bodyInst =
            instances[static_cast<size_t>(fish.bodyVolumeIndex)];
        if (!bodyInst.visible)
        {
            continue;
        }

        const bool focused = fish.id == focusedId;
        engine::game::appendAxolotlCreaturePolishObjects(
            engine::game::AxolotlPolishPose{bodyInst.volume.worldFromLocal(), fish.scale,
                                            fish.pathPhase, fish.wagPhase, focused,
                                            focused ? focusSeconds : 0.0f},
            timeSeconds, polishObjects);
    }

    waterFoamObjects_.reserve(waterFoamObjects_.size() + polishObjects.size());
    for (const engine::game::AxolotlPolishObject& object : polishObjects)
    {
        RenderObject renderObject{};
        renderObject.model = object.model;
        renderObject.mesh = &cubeMesh_;
        renderObject.material =
            object.material == engine::game::AxolotlPolishMaterial::Bubble
                ? &waterFoamMaterial_
                : &cubeMaterial_;
        waterFoamObjects_.push_back(renderObject);
    }
}

void App::loadAssets()
{
    objects_.clear();
    animatedObjects_.clear();
    proceduralFish_.clear();
    heroFoliage_.clear();
    proceduralFishPoseValid_ = false;

    const CpuImage white = makeSolidImage(255, 255, 255, 255, true);
    const CpuImage normal = makeSolidImage(128, 128, 255, 255, false);
    const CpuImage orm = makeSolidImage(255, 128, 0, 255, false);

    createTexture2D(ctx_, renderer_.commands(), white, defaultBaseColor_);
    createTexture2D(ctx_, renderer_.commands(), normal, defaultNormal_);
    createTexture2D(ctx_, renderer_.commands(), orm, defaultOrm_);

    createMeshBuffer(ctx_, renderer_.commands(), makeCubeVertices(), cubeMesh_);
    createMeshBuffer(ctx_, renderer_.commands(), makePlaneVertices(10.0f), groundMesh_);

    const std::filesystem::path meshDir = assetRoot_ / "meshes";
    const std::filesystem::path meshPath = findFirstFileWithExtension(meshDir, ".obj");
    if (!meshPath.empty())
    {
        CpuMesh cpuMesh{};
        std::string err;
        if (loadMeshOBJ(meshPath, cpuMesh, &err))
        {
            createMeshBuffer(ctx_, renderer_.commands(), cpuMesh.vertices, cpuMesh.indices,
                             assetMesh_);
            logInfo("Assets", std::string("Loaded mesh: ") + meshPath.string() + " (v=" +
                                  std::to_string(cpuMesh.vertices.size()) + ", i=" +
                                  std::to_string(cpuMesh.indices.size()) + ")");
        }
        else
        {
            logWarning("Assets", std::string("Mesh load failed for ") + meshPath.string() +
                                     (err.empty() ? std::string() : " (" + err + ")") +
                                     "; using generated cube mesh fallback.");
        }
    }
    else
    {
        logInfo("Assets", std::string("No optional OBJ found in ") + meshDir.string() +
                              "; using generated cube mesh fallback.");
    }

    const std::filesystem::path texDir = assetRoot_ / "textures";
    const std::filesystem::path texPath = findFirstFileWithExtension(texDir, ".png");
    if (!texPath.empty())
    {
        CpuImage image{};
        std::string err;
        if (loadImageRGBA8(texPath, true, image, &err))
        {
            createTexture2D(ctx_, renderer_.commands(), image, assetBaseColor_);
            logInfo("Assets", std::string("Loaded texture: ") + texPath.string());
        }
        else
        {
            logWarning("Assets", std::string("Texture load failed for ") + texPath.string() +
                                     (err.empty() ? std::string() : " (" + err + ")") +
                                     "; using default base-color texture.");
        }
    }
    else
    {
        logInfo("Assets", std::string("No optional PNG found in ") + texDir.string() +
                              "; using default base-color texture.");
    }

    cubeMaterial_ = Material{};
    cubeMaterial_.baseColorFactor = glm::vec4(0.8f, 0.3f, 0.2f, 1.0f);
    cubeMaterial_.roughness = 0.6f;
    cubeMaterial_.metallic = 0.0f;
    cubeMaterial_.ao = 1.0f;
    cubeMaterial_.baseColorTex = &defaultBaseColor_;
    cubeMaterial_.normalTex = &defaultNormal_;
    cubeMaterial_.ormTex = &defaultOrm_;
    cubeMaterial_.set = materialPool_.allocate(ctx_);
    materialPool_.updateDescriptorSet(ctx_, cubeMaterial_.set, *cubeMaterial_.baseColorTex,
                                      *cubeMaterial_.normalTex, *cubeMaterial_.ormTex);

    assetMaterial_ = Material{};
    assetMaterial_.baseColorFactor = glm::vec4(1.0f);
    assetMaterial_.roughness = 0.5f;
    assetMaterial_.metallic = 0.0f;
    assetMaterial_.ao = 1.0f;
    assetMaterial_.baseColorTex =
        assetBaseColor_.image != VK_NULL_HANDLE ? &assetBaseColor_ : &defaultBaseColor_;
    assetMaterial_.normalTex = &defaultNormal_;
    assetMaterial_.ormTex = &defaultOrm_;
    assetMaterial_.set = materialPool_.allocate(ctx_);
    materialPool_.updateDescriptorSet(ctx_, assetMaterial_.set, *assetMaterial_.baseColorTex,
                                      *assetMaterial_.normalTex, *assetMaterial_.ormTex);
    if (assetMesh_.vbo == VK_NULL_HANDLE)
    {
        logInfo("Assets", "Primary scene mesh is currently using the generated cube fallback.");
    }
    if (assetBaseColor_.image == VK_NULL_HANDLE)
    {
        logInfo("Assets", "Primary asset material is currently using the default base-color texture.");
    }

    groundMaterial_ = Material{};
    groundMaterial_.baseColorFactor = glm::vec4(0.7f, 0.7f, 0.75f, 1.0f);
    groundMaterial_.roughness = 0.9f;
    groundMaterial_.metallic = 0.0f;
    groundMaterial_.ao = 1.0f;
    groundMaterial_.baseColorTex = &defaultBaseColor_;
    groundMaterial_.normalTex = &defaultNormal_;
    groundMaterial_.ormTex = &defaultOrm_;
    groundMaterial_.set = materialPool_.allocate(ctx_);
    materialPool_.updateDescriptorSet(ctx_, groundMaterial_.set, *groundMaterial_.baseColorTex,
                                      *groundMaterial_.normalTex, *groundMaterial_.ormTex);

    waterFoamMaterial_ = Material{};
    waterFoamMaterial_.baseColorFactor = glm::vec4(0.82f, 0.97f, 1.0f, 1.0f);
    waterFoamMaterial_.roughness = 0.18f;
    waterFoamMaterial_.metallic = 0.0f;
    waterFoamMaterial_.ao = 1.0f;
    waterFoamMaterial_.baseColorTex = &defaultBaseColor_;
    waterFoamMaterial_.normalTex = &defaultNormal_;
    waterFoamMaterial_.ormTex = &defaultOrm_;
    waterFoamMaterial_.set = materialPool_.allocate(ctx_);
    materialPool_.updateDescriptorSet(ctx_, waterFoamMaterial_.set,
                                      *waterFoamMaterial_.baseColorTex,
                                      *waterFoamMaterial_.normalTex,
                                      *waterFoamMaterial_.ormTex);

    rebuildStaticObjects();
    rebuildAnimatedObjects();

    // load voxel world (ChunkGrid-based greedy mesh system) if enabled
    if (sceneConfig().loadVoxelWorld)
    {
        createVoxelWorld();
    }

    rebuildFishbowlGlassMesh();
    rebuildModularGlassMeshes();
    rebuildGlassObjectsForCurrentScene();
}

void App::destroyAssets()
{
    destroyVoxelWorld();
    destroyMeshBuffer(ctx_.device, cubeMesh_);
    destroyMeshBuffer(ctx_.device, assetMesh_);
    destroyMeshBuffer(ctx_.device, groundMesh_);
    destroyMeshBuffer(ctx_.device, waterMesh_);
    destroyMeshBuffer(ctx_.device, fishbowlGlassMesh_);
    modularGlassMeshes_.clear(ctx_.device);
    destroyTexture(ctx_, assetBaseColor_);
    destroyTexture(ctx_, defaultBaseColor_);
    destroyTexture(ctx_, defaultNormal_);
    destroyTexture(ctx_, defaultOrm_);
    materialPool_.destroy(ctx_);
    objects_.clear();
    glassObjects_.clear();
    animatedObjects_.clear();
    waterFoamObjects_.clear();
    proceduralFish_.clear();
    heroFoliage_.clear();
    proceduralFishPoseValid_ = false;
}

bool App::initVolumeScene()
{
    bool sceneOk = true;
    resetCloudRuntimeState();
    renderPasses().foliage.clearSceneData();

    if (sceneConfig().loadAquariumTest)
    {
        const bool includeVoxelWater =
            shouldIncludeAquariumVoxelWater(sceneConfig(), automationDisableAquariumWater_);
        AquariumSceneRenderData renderData{};
        sceneOk = AquariumScene::init(ctx_, voxelWorld_, voxelPalette_,
                                      sceneConfig().useMeshTankGlass,
                                      aquariumLayoutForScene(sceneConfig()), includeVoxelWater,
                                      &sceneConfig().placeables,
                                      sceneConfig().useDefaultPlaceables, &renderData);
        if (sceneOk)
        {
            if (!renderData.foliageInstances.empty())
            {
                (void)renderPasses().foliage.setSceneData(
                    ctx_, renderData.foliageInstances);
            }
            // match default water plane to the aquarium tank interior for first load.
            glm::vec3 waterBoundsMin(0.0f);
            glm::vec3 waterBoundsMax(0.0f);
            if (AquariumScene::getWaterBounds(waterBoundsMin, waterBoundsMax))
            {
                waterSettings_.waterLevel_ = waterBoundsMax.y;
            }
            else
            {
                for (const auto& inst : voxelWorld_.instances())
                {
                    if ((inst.volume.flags() & engine::VoxelVolume::FLAG_WATER) != 0u)
                    {
                        waterSettings_.waterLevel_ = inst.volume.worldAabbMax().y - 3.0f;
                        break;
                    }
                }
            }

            // Glass focus preset: hide the outer room shell to view the tank directly.
            if (sceneConfig().name == "glass_focus" &&
                voxelWorld_.instances().size() > AquariumScene::VOLUME_ROOM)
            {
                voxelWorld_.setVolumeVisible(AquariumScene::VOLUME_ROOM, false);
            }
        }
    }
    else if (isNaturePondScene(sceneConfig()))
    {
        NaturePondSceneRenderData renderData{};
        sceneOk = NaturePondScene::init(ctx_, voxelWorld_, voxelPalette_, worldSeed_,
                                        sceneConfig(), &renderData);
        if (sceneOk)
        {
            waterSettings_.waterLevel_ = NaturePondScene::pondSurfaceHeight();
            (void)renderPasses().foliage.setSceneData(
                ctx_, renderData.foliageInstances,
                renderData.legacyFoliageVolumeIndex,
                renderData.windborneParticles);
        }
        proceduralWorldSettings_.proceduralDirty_ = false;
    }
    else if (sceneConfig().loadProceduralWorld)
    {
        proceduralWorldSettings_.proceduralSeed_ = worldSeed_;
        ProceduralWorldSettings settings{};
        settings.seed = proceduralWorldSettings_.proceduralSeed_;
        settings.terrainStyle = isBeachSandScene(sceneConfig()) ? ProceduralWorldTerrainStyle::Beach
                                                               : ProceduralWorldTerrainStyle::Default;
        settings.gridDims = proceduralWorldSettings_.proceduralGridDims_;
        settings.chunkDims = proceduralWorldSettings_.proceduralChunkDims_;
        settings.noiseScale = proceduralWorldSettings_.proceduralNoiseScale_;
        settings.baseHeight = proceduralWorldSettings_.proceduralBaseHeight_;
        settings.heightAmplitude = proceduralWorldSettings_.proceduralHeightAmplitude_;
        settings.dirtDepth = proceduralWorldSettings_.proceduralDirtDepth_;
        settings.enableCaves = proceduralWorldSettings_.proceduralCavesEnabled_;
        settings.caveNoiseScale = proceduralWorldSettings_.proceduralCaveNoiseScale_;
        settings.caveThreshold = proceduralWorldSettings_.proceduralCaveThreshold_;
        settings.caveMinY = proceduralWorldSettings_.proceduralCaveMinY_;
        settings.caveMaxY = proceduralWorldSettings_.proceduralCaveMaxY_;
        settings.enableTrees = proceduralWorldSettings_.proceduralTreesEnabled_;
        settings.treesPerChunk = proceduralWorldSettings_.proceduralTreesPerChunk_;
        settings.trunkMinH = proceduralWorldSettings_.proceduralTrunkMinH_;
        settings.trunkMaxH = proceduralWorldSettings_.proceduralTrunkMaxH_;
        settings.leafRadius = proceduralWorldSettings_.proceduralLeafRadius_;
        settings.enableRocks = proceduralWorldSettings_.proceduralRocksEnabled_;
        settings.rocksPerChunk = proceduralWorldSettings_.proceduralRocksPerChunk_;
        settings.rockMinR = proceduralWorldSettings_.proceduralRockMinR_;
        settings.rockMaxR = proceduralWorldSettings_.proceduralRockMaxR_;
        sceneOk = ProceduralWorldScene::regenerate(ctx_, voxelWorld_, voxelPalette_, settings,
                                                   false);
        proceduralWorldSettings_.proceduralDirty_ = !sceneOk;
    }
    else if (sceneConfig().loadVoxelImport)
    {
        const std::filesystem::path voxPath = assetRoot_ / "vox" / "test.vox";
        if (std::filesystem::exists(voxPath))
        {
            voxelImportTriangleCount_ = 0;
            voxelImportFilledCount_ = 0;
            voxelImportVoxelizeMs_ = 0.0f;
            sceneOk = VoxScene::init(ctx_, voxelWorld_, voxelPalette_, voxPath);
            voxelImportDirty_ = !sceneOk;
        }
        else
        {
            VoxelImportSettings settings{};
            settings.meshPath = voxelImportMeshPath_;
            settings.resolution = voxelImportResolution_;
            settings.splitIntoChunks = voxelImportSplitChunks_;

            voxelImportTriangleCount_ = 0;
            voxelImportFilledCount_ = 0;
            voxelImportVoxelizeMs_ = 0.0;
            VoxelImportStats stats{};
            sceneOk =
                VoxelImportScene::init(ctx_, voxelWorld_, voxelPalette_, settings, &stats);
            if (sceneOk)
            {
                voxelImportVoxelizeMs_ = stats.voxelizeMs;
                voxelImportTriangleCount_ = stats.triangleCount;
                voxelImportFilledCount_ = stats.filledVoxels;
            }
            voxelImportDirty_ = !sceneOk;
        }
    }
    else if (sceneConfig().loadOBBVolumes)
    {
        if (sceneConfig().name == "dense_skip_probe")
        {
            sceneOk = DenseSkipProbeScene::init(ctx_, voxelWorld_, voxelPalette_);
        }
        else if (sceneConfig().name == "dense_npot_skip_probe")
        {
            sceneOk = DenseSkipProbeScene::initNpot(ctx_, voxelWorld_, voxelPalette_);
        }
        else if (sceneConfig().name == "dense_irregular_skip_probe")
        {
            sceneOk = DenseSkipProbeScene::initIrregular(ctx_, voxelWorld_, voxelPalette_);
        }
        else
        {
            sceneOk = ObbTestScene::init(ctx_, voxelWorld_, voxelPalette_);
        }
    }
    else if (sceneConfig().loadGlassTestScene)
    {
        sceneOk = GlassTestScene::init(ctx_, voxelWorld_, voxelPalette_);
    }
    else
    {
        // keep descriptor infrastructure valid even when no voxel volume scene is active.
        sceneOk = voxelWorld_.initEmptyWorld(ctx_, voxelPalette_);
    }

    // stars are now rendered procedurally in the composite shader
    // (no voxel-based star scene needed)

    // sync sky color from scene config
    skyPreset_ = sceneConfig().skyPreset;
    skyColor_ = sceneConfig().skyColor;

    // create water mesh for all volume-based scenes
    // use a large extent so water extends to horizon (infinite ocean look)
    if (sceneOk && waterMesh_.vbo == VK_NULL_HANDLE)
    {
        constexpr float kWaterHalfExtent = 500.0f;
        constexpr int kWaterGridSize = 64;
        const std::vector<Vertex> waterVerts = makeGridVertices(kWaterHalfExtent, kWaterGridSize);
        createMeshBuffer(ctx_, renderer_.commands(), waterVerts, waterMesh_);
    }

    return sceneOk;
}

void App::applyProceduralPresetDefaultsForScene()
{
    if (!sceneConfig().loadProceduralWorld)
    {
        return;
    }

    proceduralWorldSettings_.proceduralSeed_ = sceneConfig().worldSeed;
    proceduralWorldSettings_.proceduralBaseHeight_ = 10.0f;
    proceduralWorldSettings_.proceduralNoiseScale_ = 32.0f;
    proceduralWorldSettings_.proceduralHeightAmplitude_ = 16.0f;
    proceduralWorldSettings_.proceduralDirtDepth_ = 4;
    proceduralWorldSettings_.proceduralCavesEnabled_ = true;
    proceduralWorldSettings_.proceduralCaveNoiseScale_ = 24.0f;
    proceduralWorldSettings_.proceduralCaveThreshold_ = 0.65f;
    proceduralWorldSettings_.proceduralCaveMinY_ = 0;
    proceduralWorldSettings_.proceduralCaveMaxY_ = 1024;
    proceduralWorldSettings_.proceduralTreesEnabled_ = true;
    proceduralWorldSettings_.proceduralTreesPerChunk_ = 2;
    proceduralWorldSettings_.proceduralTrunkMinH_ = 4;
    proceduralWorldSettings_.proceduralTrunkMaxH_ = 7;
    proceduralWorldSettings_.proceduralLeafRadius_ = 3;
    proceduralWorldSettings_.proceduralRocksEnabled_ = true;
    proceduralWorldSettings_.proceduralRocksPerChunk_ = 1;
    proceduralWorldSettings_.proceduralRockMinR_ = 2;
    proceduralWorldSettings_.proceduralRockMaxR_ = 4;
    proceduralWorldSettings_.proceduralGridDims_ = glm::ivec3(4, 4, 1);
    proceduralWorldSettings_.proceduralChunkDims_ = glm::ivec3(32, 32, 32);
    waterSettings_.waterLevel_ = 10.0f;

    if (isNaturePondScene(sceneConfig()))
    {
        proceduralWorldSettings_.proceduralCavesEnabled_ = false;
        proceduralWorldSettings_.proceduralTreesEnabled_ = false;
        proceduralWorldSettings_.proceduralRocksEnabled_ = false;
        proceduralWorldSettings_.proceduralGridDims_ = glm::ivec3(1);
        proceduralWorldSettings_.proceduralChunkDims_ = glm::ivec3(1);
        waterSettings_.waterLevel_ = NaturePondScene::pondSurfaceHeight();
    }
    else if (isBeachSandScene(sceneConfig()))
    {
        proceduralWorldSettings_.proceduralBaseHeight_ = 10.5f;
        proceduralWorldSettings_.proceduralNoiseScale_ = 44.0f;
        proceduralWorldSettings_.proceduralHeightAmplitude_ = 8.0f;
        proceduralWorldSettings_.proceduralDirtDepth_ = 8;
        proceduralWorldSettings_.proceduralCavesEnabled_ = false;
        proceduralWorldSettings_.proceduralTreesEnabled_ = true;
        proceduralWorldSettings_.proceduralTreesPerChunk_ = 3;
        proceduralWorldSettings_.proceduralTrunkMinH_ = 1;
        proceduralWorldSettings_.proceduralTrunkMaxH_ = 3;
        proceduralWorldSettings_.proceduralLeafRadius_ = 1;
        proceduralWorldSettings_.proceduralRocksEnabled_ = true;
        proceduralWorldSettings_.proceduralRocksPerChunk_ = 3;
        proceduralWorldSettings_.proceduralRockMinR_ = 1;
        proceduralWorldSettings_.proceduralRockMaxR_ = 2;
        proceduralWorldSettings_.proceduralGridDims_ = glm::ivec3(4, 2, 4);
        proceduralWorldSettings_.proceduralChunkDims_ = glm::ivec3(32, 32, 32);
        waterSettings_.waterLevel_ = 10.75f;
    }
}

void App::reloadScene(const SceneConfig& newConfig)
{
    // wait for gpu to finish any pending work
    waitForInFlightFrameWork("scene reload");

    const engine::scene::SceneReloadDecision reloadDecision =
        engine::scene::SceneManager::evaluateReloadDecision(newConfig, sceneConfig());
    // note: star settings are no longer in volumeSceneChanged since stars are
    // now rendered procedurally in the composite shader (real-time updates)

    // update config
    sceneConfigMutable() = newConfig;
    worldSeed_ = sceneConfig().worldSeed;
    if (reloadDecision.shouldApplyProceduralPresetDefaults())
    {
        applyProceduralPresetDefaultsForScene();
    }

    // update sky color live (no scene reload needed)
    if (reloadDecision.skyColorChanged)
    {
        skyPreset_ = sceneConfig().skyPreset;
        skyColor_ = sceneConfig().skyColor;
    }

    applySceneLightingDefaults(*this, sceneConfig());
    applySceneWaterDefaults(*this, sceneConfig());
    applySceneGlassDefaults(*this, sceneConfig());
    applyScenePostDefaults(*this, sceneConfig());

    // enforce direct-view aquarium defaults even when reloading the same scene.
    if (isDirectViewAquariumScene(sceneConfig()))
    {
        glassSettings_.voxelGlassRefractEnabled_ = false;
        glassSettings_.voxelGlassDebugMode_ = 0;
    }

    // quick-load scene presets should move camera to the scene-authored start view.
    if (reloadDecision.shouldUpdateCamera())
    {
        camera_.position = sceneConfig().cameraPosition;
        camera_.yaw = sceneConfig().cameraYaw;
        camera_.pitch = sceneConfig().cameraPitch;
    }

    // apply runtime toggles from scene config when switching scenes
    if (reloadDecision.shouldApplyRuntimeToggles())
    {
        waterSettings_.waterEnabled_ = sceneConfig().enableWater;
        waterSettings_.useWaterV2_ = sceneConfig().useWaterV2;
        if (automationDisableAquariumWater_ && sceneConfig().loadAquariumTest)
        {
            waterSettings_.waterEnabled_ = false;
        }
        input_.setWaterEnabled(sceneConfig().enableWater);
        if (automationDisableAquariumWater_ && sceneConfig().loadAquariumTest)
        {
            input_.setWaterEnabled(false);
        }
        glassSettings_.glassEnabled_ = sceneConfig().enableGlass;
        input_.setGlassEnabled(sceneConfig().enableGlass);
        lightingSettings_.pointLightsEnabled_ = sceneConfig().enablePointLights;
        input_.setLightsEnabled(sceneConfig().enablePointLights);

        if (isDirectViewAquariumScene(sceneConfig()))
        {
            // direct-view aquarium scenes should not be influenced by voxel-glass post.
            glassSettings_.voxelGlassRefractEnabled_ = false;
            glassSettings_.voxelGlassDebugMode_ = 0;
        }
        else if (sceneConfig().name == "glass_focus")
        {
            glassSettings_.voxelGlassRefractEnabled_ = true;
            glassSettings_.voxelGlassAbsorption_ = 0.14f;
            glassSettings_.voxelGlassRefractStrength_ = 0.018f;
            glassSettings_.voxelGlassIOR_ = 1.5f;
            glassSettings_.voxelGlassReflectStrength_ = 0.24f;
            glassSettings_.voxelGlassTint_ = glm::vec3(0.97f, 0.99f, 1.0f);
            glassSettings_.voxelGlassReflectionColor_ = glm::vec3(0.3f, 0.5f, 0.8f);
            glassSettings_.voxelGlassDebugMode_ = 0;
        }
    }

    // keep reload on the exact same resolve/apply/input bridge as startup.
    sceneManager_.setPresentationRuntimeState(
        app::resolveApplyAndSyncScenePresentationProfile(
            sceneConfig(), input_, skyPreset_, skyColor_, lightingSettings_,
            shadowSettings_, aoSettings_, postFxSettings_, voxelDebugSettings_,
            waterSettings_, glassSettings_, lights_.maxLights));
    updateSunDirection();
    resetEnvironmentTimeRuntimeState();

    // handle OBB/Aquarium volume changes
    if (reloadDecision.volumeSceneChanged)
    {
        executeVolumeScenePlan(
            engine::scene::SceneManager::planSceneReloadVolumeChange());
    }

    if (reloadDecision.shouldHandleCloudLifecycle())
    {
        if (sceneConfig().loadCloudScene)
        {
            requestCloudBuildFromSceneConfig(
                reloadDecision.scenePresetChanged || reloadDecision.volumeSceneChanged
                    ? "scene reload"
                    : "scene cloud update");
        }
        else
        {
            invalidatePendingCloudBuild("clouds disabled");
            if (cloudVolumeIndex_ >= 0 &&
                static_cast<size_t>(cloudVolumeIndex_) < voxelWorld_.instances().size())
            {
                voxelWorld_.setVolumeVisible(static_cast<uint32_t>(cloudVolumeIndex_), false);
                voxelWorld_.setVolumeWrapOffset(static_cast<uint32_t>(cloudVolumeIndex_),
                                                glm::vec3(0.0f));
            }
            cloudBasePosition_ = glm::vec3(0.0f);
            cloudWrapRecenterOffset_ = glm::vec3(0.0f);
        }
    }

    // handle voxel world changes
    if (reloadDecision.voxelWorldChanged)
    {
        // destroy existing voxel world
        destroyVoxelWorld();

        // create new one if enabled
        if (sceneConfig().loadVoxelWorld)
        {
            createVoxelWorld();
        }
    }

    rebuildStaticObjects();
    rebuildAnimatedObjects();

    rebuildFishbowlGlassMesh();
    rebuildModularGlassMeshes();
    rebuildGlassObjectsForCurrentScene();
    if (reloadDecision.scenePresetChanged || reloadDecision.volumeSceneChanged)
    {
        resetAreaLightsToScenePreset();
    }

    // mark scene as dirty to trigger rebuild
    sceneObjectsDirty_ = true;
    voxelObjectsDirty_ = true;
    placeablePreview_ = {};
    gameRuntime_.resetPlaceableEdits();
    postFxSettings_.taaResetHistory_ = true;
    shadowSettings_.shadowResetHistory_ = true;
    shadowSettings_.localShadowResetHistory_ = true;
    aoSettings_.aoResetHistory_ = true;

    logInfo("Scene", std::string("Scene reloaded: ") + sceneConfig().name);
}

void App::applyCloudSettings(const SceneConfig& config)
{
    const bool cloudGenerationChanged =
        engine::scene::SceneManager::cloudGenerationSettingsChanged(config,
                                                                    sceneConfig());
    engine::scene::SceneManager::copyCloudGenerationSettings(sceneConfigMutable(), config);

    if (!cloudGenerationChanged)
    {
        return;
    }

    if (!sceneConfig().loadCloudScene)
    {
        invalidatePendingCloudBuild("clouds disabled");
        if (cloudVolumeIndex_ >= 0 &&
            static_cast<size_t>(cloudVolumeIndex_) < voxelWorld_.instances().size())
        {
            voxelWorld_.setVolumeVisible(static_cast<uint32_t>(cloudVolumeIndex_), false);
            voxelWorld_.setVolumeWrapOffset(static_cast<uint32_t>(cloudVolumeIndex_),
                                            glm::vec3(0.0f));
        }
        cloudBasePosition_ = glm::vec3(0.0f);
        cloudWrapRecenterOffset_ = glm::vec3(0.0f);
        postFxSettings_.taaResetHistory_ = true;
        shadowSettings_.shadowResetHistory_ = true;
        shadowSettings_.localShadowResetHistory_ = true;
        aoSettings_.aoResetHistory_ = true;
        logInfo("CloudScene", "Clouds disabled; active cloud volume hidden.");
        return;
    }

    requestCloudBuildFromSceneConfig("cloud apply");
}

void App::createVoxelWorld()
{
    const glm::ivec3 dims(sceneConfig().worldDimsX, sceneConfig().worldDimsY, sceneConfig().worldDimsZ);
    const engine::voxel::WorldGridCreateResult gridState =
        engine::voxel::createWorldGrid(voxelGrid_, dims);

    voxelRenderResources_.resetChunks(gridState.chunkCoords);

    initializeVoxelWorldMaterials();

    // use a large extent so water extends to horizon (infinite ocean look)
    if (waterMesh_.vbo == VK_NULL_HANDLE)
    {
        constexpr float kWaterHalfExtent = 500.0f;
        constexpr int kWaterGridSize = 64;
        const std::vector<Vertex> waterVerts = makeGridVertices(kWaterHalfExtent, kWaterGridSize);
        createMeshBuffer(ctx_, renderer_.commands(), waterVerts, waterMesh_);
    }

    const engine::voxel::WorldGenerationResult generation =
        engine::voxel::generateWorldState(voxelGrid_, worldSeed_);
    worldTrees_ = generation.treesPlaced;
    worldFoliage_ = generation.foliagePlaced;
    waterSettings_.waterLevel_ = generation.waterLevel;

    buildChunkBoundsMesh();
    rebuildChunkBoundsObjects();
    voxelObjects_.clear();
    voxelShadowObjects_.clear();
    chunkBoundsVisibleObjects_.clear();
    voxelObjectsDirty_ = true;
    voxelDebugSettings_.voxelVisible_ = true;
    voxelCursorVisible_ = false;
    sceneObjectsDirty_ = true;
}

void App::initializeVoxelWorldMaterials()
{
    for (auto& mat : voxelMaterials_)
    {
        mat = Material{};
    }
    for (BlockId id = 1; id < kBlockTypeCount; ++id)
    {
        Material mat{};
        mat.baseColorFactor = glm::vec4(blockColor(id), 1.0f);
        mat.roughness = 0.85f;
        mat.metallic = 0.0f;
        mat.ao = 1.0f;
        mat.baseColorTex = &defaultBaseColor_;
        mat.normalTex = &defaultNormal_;
        mat.ormTex = &defaultOrm_;
        mat.set = materialPool_.allocate(ctx_);
        materialPool_.updateDescriptorSet(ctx_, mat.set, *mat.baseColorTex, *mat.normalTex,
                                          *mat.ormTex);
        voxelMaterials_[id] = mat;
    }
}

void App::destroyVoxelWorld()
{
    jobSystem_.waitIdle();

    for (uint32_t i = 0; i < kMaxFramesInFlight; ++i)
    {
        meshUploadQueue_.releaseFrame(ctx_, i);
    }
    meshUploadQueue_.clearPending();

    voxelRenderResources_.destroyGpuResources(ctx_.device);
    voxelObjects_.clear();
    voxelShadowObjects_.clear();
    chunkBoundsObjects_.clear();
    chunkBoundsVisibleObjects_.clear();
    volumeBoundsVisibleObjects_.clear();
    destroyMeshBuffer(ctx_.device, chunkBoundsMesh_);
    engine::voxel::clearWorldGrid(voxelGrid_);
    sceneObjectsDirty_ = true;
}

void App::regenerateVoxelWorld()
{
    jobSystem_.waitIdle();
    meshUploadQueue_.clearPending();

    const engine::voxel::WorldGenerationResult generation =
        engine::voxel::generateWorldState(voxelGrid_, worldSeed_);
    worldTrees_ = generation.treesPlaced;
    worldFoliage_ = generation.foliagePlaced;
    waterSettings_.waterLevel_ = generation.waterLevel;
    voxelObjectsDirty_ = true;
}

bool App::findPlaceableAtPreview(PlaceableInstance& outInstance) const
{
    return gameRuntime_.findPlaceableAtPreview(sceneConfig(), placeablePreview_,
                                               outInstance);
}

bool App::applyPlaceableEditCommand(const PlaceableEditCommand& command, bool undo, bool redo)
{
    const SceneConfig previousConfig = sceneConfig();
    const engine::game::GameRuntime::PlaceableApplyResult result =
        gameRuntime_.applyPlaceableEditCommand(sceneConfigMutable(), command, undo, redo);

    if (!result.accepted)
    {
        return false;
    }

    if (result.effects.sceneRebuildRequired && !rebuildVolumeScene())
    {
        sceneConfigMutable() = previousConfig;
        rebuildVolumeScene();
        gameRuntime_.failPlaceableEditRebuild();
        return false;
    }

    if (result.effects.clearPreview)
    {
        placeablePreview_ = {};
    }
    return true;
}

bool App::commitPlaceablePreview()
{
    const engine::game::GameRuntime::PendingPlaceableEdit edit =
        gameRuntime_.beginCommitPlaceablePreview(placeablePreview_);
    if (!edit.valid || !applyPlaceableEditCommand(edit.command, edit.undo))
    {
        return false;
    }

    if (!gameRuntime_.completePlaceableEdit(edit, sceneConfigMutable())) { (void)rebuildVolumeScene(); return false; }
    if (edit.affectedPlaceable)
    {
        const PlaceableInstance& instance = *edit.affectedPlaceable;
        logInfo("Placeables",
                makeLogMessage("Placed foliage instance ", instance.uuid, " (",
                               instance.prototypeSlug, ") at ", instance.position.x, ", ",
                               instance.position.y, ", ", instance.position.z, "."));
    }
    return true;
}

bool App::removePlaceableAtPreview()
{
    const engine::game::GameRuntime::PendingPlaceableEdit edit =
        gameRuntime_.beginRemovePlaceableAtPreview(sceneConfig(), placeablePreview_);
    if (!edit.valid || !applyPlaceableEditCommand(edit.command, edit.undo))
    {
        return false;
    }

    if (!gameRuntime_.completePlaceableEdit(edit, sceneConfigMutable())) { (void)rebuildVolumeScene(); return false; }
    if (edit.affectedPlaceable)
    {
        const PlaceableInstance& target = *edit.affectedPlaceable;
        logInfo("Placeables",
                makeLogMessage("Removed foliage instance ", target.uuid, " (",
                               target.prototypeSlug, ")."));
    }
    return true;
}

bool App::undoLastPlaceableEdit()
{
    const engine::game::GameRuntime::PendingPlaceableEdit edit =
        gameRuntime_.beginUndoLastPlaceableEdit();
    if (!edit.valid || !applyPlaceableEditCommand(edit.command, edit.undo))
    {
        return false;
    }

    if (!gameRuntime_.completePlaceableEdit(edit, sceneConfigMutable())) { (void)rebuildVolumeScene(); return false; }
    logInfo("Placeables", "Undid last placeable edit.");
    return true;
}

void App::updateVoxelMeshing(const glm::vec3& cameraPos)
{
    if (input_.voxelMeshingFrozen())
    {
        return;
    }

    engine::voxel::scheduleDirtyChunkMeshing(voxelGrid_, jobSystem_, cameraPos,
                                             kMaxMeshJobsPerFrame);
}

void App::applyPendingVoxelMeshes(uint32_t frameIndex, VkCommandBuffer cmd)
{
    const engine::render::VoxelRenderResources::PendingMeshApplyResult result =
        voxelRenderResources_.applyPendingMeshes(voxelGrid_, ctx_, renderer_.commands(),
                                                 meshUploadQueue_, cmd, frameIndex);
    voxelRebuildsSinceTitle_ += result.rebuiltChunks;
    if (result.changed)
    {
        voxelObjectsDirty_ = true;
    }
}

void App::rebuildVoxelObjects(const Frustum& frustum, const glm::vec3& cameraPos)
{
    voxelObjects_.clear();
    voxelShadowObjects_.clear();
    chunkBoundsVisibleObjects_.clear();

    if (voxelRenderResources_.empty())
    {
        voxelObjectsDirty_ = false;
        return;
    }

    const float maxDistSq = kVoxelRenderDistance * kVoxelRenderDistance;
    voxelObjects_.reserve(voxelRenderResources_.size() * (kBlockTypeCount - 1));
    voxelShadowObjects_.reserve(voxelRenderResources_.size() * (kBlockTypeCount - 1));
    if (voxelDebugSettings_.chunkBoundsVisible_)
    {
        chunkBoundsVisibleObjects_.reserve(voxelRenderResources_.size());
    }

    const std::vector<engine::voxel::ChunkBounds> chunkBounds =
        engine::voxel::chunkBounds(voxelGrid_);
    auto& chunkRenders = voxelRenderResources_.chunkRenders();
    const size_t renderCount = std::min(chunkRenders.size(),
                                        chunkBounds.size());
    for (size_t i = 0; i < renderCount; ++i)
    {
        auto& render = chunkRenders[i];
        const glm::vec3 min = chunkBounds[i].min;
        const glm::vec3 max = chunkBounds[i].max;
        const glm::vec3 center = (min + max) * 0.5f;
        const glm::vec3 delta = center - cameraPos;
        const float distSq = glm::dot(delta, delta);
        if (distSq > maxDistSq)
        {
            continue;
        }
        const bool inFrustum = aabbInFrustum(frustum, min, max);

        if (voxelDebugSettings_.chunkBoundsVisible_ && inFrustum && i < chunkBoundsObjects_.size())
        {
            chunkBoundsVisibleObjects_.push_back(chunkBoundsObjects_[i]);
        }

        for (BlockId id = 1; id < kBlockTypeCount; ++id)
        {
            MeshGpu& mesh = render.meshes[id];
            if (mesh.vertexCount == 0)
            {
                continue;
            }

            RenderObject obj{};
            obj.model = glm::mat4(1.0f);
            obj.mesh = &mesh;
            obj.material = &voxelMaterials_[id];
            if (voxelDebugSettings_.voxelVisible_ && inFrustum)
            {
                voxelObjects_.push_back(obj);
            }
            if (voxelDebugSettings_.voxelVisible_)
            {
                voxelShadowObjects_.push_back(obj);
            }
        }
    }

    voxelObjectsDirty_ = false;
    sceneObjectsDirty_ = true;
}

void App::rebuildVolumeBoundsObjects(const Frustum& frustum)
{
    volumeBoundsVisibleObjects_.clear();

    const auto& instances = voxelWorld_.instances();
    if (instances.empty())
    {
        return;
    }

    if (chunkBoundsMesh_.vbo == VK_NULL_HANDLE)
    {
        buildChunkBoundsMesh();
    }
    if (volumeBoundsMaterial_.set == VK_NULL_HANDLE)
    {
        volumeBoundsMaterial_ = Material{};
        volumeBoundsMaterial_.baseColorFactor = glm::vec4(0.2f, 0.7f, 1.0f, 1.0f);
        volumeBoundsMaterial_.roughness = 0.4f;
        volumeBoundsMaterial_.metallic = 0.0f;
        volumeBoundsMaterial_.ao = 1.0f;
        volumeBoundsMaterial_.baseColorTex = &defaultBaseColor_;
        volumeBoundsMaterial_.normalTex = &defaultNormal_;
        volumeBoundsMaterial_.ormTex = &defaultOrm_;
        volumeBoundsMaterial_.set = materialPool_.allocate(ctx_);
        materialPool_.updateDescriptorSet(ctx_, volumeBoundsMaterial_.set,
                                          *volumeBoundsMaterial_.baseColorTex,
                                          *volumeBoundsMaterial_.normalTex,
                                          *volumeBoundsMaterial_.ormTex);
    }

    std::vector<engine::scene::VolumeBoundsInstance> boundsInstances;
    boundsInstances.reserve(instances.size());
    for (const auto& inst : instances)
    {
        engine::scene::VolumeBoundsInstance bounds{};
        bounds.visible = inst.visible;
        bounds.aabbMin = inst.volume.worldAabbMin();
        bounds.aabbMax = inst.volume.worldAabbMax();
        bounds.dimensions = inst.volume.dimensions();
        bounds.worldFromLocal = inst.volume.worldFromLocal();
        boundsInstances.push_back(bounds);
    }

    engine::scene::VolumeBoundsInputs inputs{};
    inputs.instances = boundsInstances;
    inputs.frustum = frustum;
    inputs.mesh = &chunkBoundsMesh_;
    inputs.material = &volumeBoundsMaterial_;
    engine::scene::SceneObjectBuilder::buildVolumeBoundsObjects(
        inputs, volumeBoundsVisibleObjects_);
}

void App::buildChunkBoundsMesh()
{
    if (chunkBoundsMesh_.vbo != VK_NULL_HANDLE)
    {
        return;
    }

    std::vector<Vertex> verts{};
    verts.reserve(12 * 36);

    const std::vector<Vertex> base = makeCubeVertices();
    auto appendBox = [&verts, &base](const glm::vec3& center, const glm::vec3& size) {
        for (const auto& v : base)
        {
            Vertex out = v;
            out.pos = v.pos * size + center;
            verts.push_back(out);
        }
    };

    const float t = 0.05f;
    const float half = t * 0.5f;

    const float min = half;
    const float max = 1.0f - half;

    for (float y : {min, max})
    {
        for (float z : {min, max})
        {
            appendBox(glm::vec3(0.5f, y, z), glm::vec3(1.0f, t, t));
        }
    }

    for (float x : {min, max})
    {
        for (float z : {min, max})
        {
            appendBox(glm::vec3(x, 0.5f, z), glm::vec3(t, 1.0f, t));
        }
    }

    for (float x : {min, max})
    {
        for (float y : {min, max})
        {
            appendBox(glm::vec3(x, y, 0.5f), glm::vec3(t, t, 1.0f));
        }
    }

    createMeshBuffer(ctx_, renderer_.commands(), verts, chunkBoundsMesh_);

    if (chunkBoundsMaterial_.set == VK_NULL_HANDLE)
    {
        chunkBoundsMaterial_ = Material{};
        chunkBoundsMaterial_.baseColorFactor = glm::vec4(1.0f, 0.2f, 0.9f, 1.0f);
        chunkBoundsMaterial_.roughness = 0.4f;
        chunkBoundsMaterial_.metallic = 0.0f;
        chunkBoundsMaterial_.ao = 1.0f;
        chunkBoundsMaterial_.baseColorTex = &defaultBaseColor_;
        chunkBoundsMaterial_.normalTex = &defaultNormal_;
        chunkBoundsMaterial_.ormTex = &defaultOrm_;
        chunkBoundsMaterial_.set = materialPool_.allocate(ctx_);
        materialPool_.updateDescriptorSet(ctx_, chunkBoundsMaterial_.set,
                                          *chunkBoundsMaterial_.baseColorTex,
                                          *chunkBoundsMaterial_.normalTex,
                                          *chunkBoundsMaterial_.ormTex);
    }

    if (volumeBoundsMaterial_.set == VK_NULL_HANDLE)
    {
        volumeBoundsMaterial_ = Material{};
        volumeBoundsMaterial_.baseColorFactor = glm::vec4(0.2f, 0.7f, 1.0f, 1.0f);
        volumeBoundsMaterial_.roughness = 0.4f;
        volumeBoundsMaterial_.metallic = 0.0f;
        volumeBoundsMaterial_.ao = 1.0f;
        volumeBoundsMaterial_.baseColorTex = &defaultBaseColor_;
        volumeBoundsMaterial_.normalTex = &defaultNormal_;
        volumeBoundsMaterial_.ormTex = &defaultOrm_;
        volumeBoundsMaterial_.set = materialPool_.allocate(ctx_);
        materialPool_.updateDescriptorSet(ctx_, volumeBoundsMaterial_.set,
                                          *volumeBoundsMaterial_.baseColorTex,
                                          *volumeBoundsMaterial_.normalTex,
                                          *volumeBoundsMaterial_.ormTex);
    }

    if (voxelCursorMaterial_.set == VK_NULL_HANDLE)
    {
        voxelCursorMaterial_ = Material{};
        voxelCursorMaterial_.baseColorFactor = glm::vec4(1.0f, 0.95f, 0.2f, 1.0f);
        voxelCursorMaterial_.roughness = 0.2f;
        voxelCursorMaterial_.metallic = 0.0f;
        voxelCursorMaterial_.ao = 1.0f;
        voxelCursorMaterial_.baseColorTex = &defaultBaseColor_;
        voxelCursorMaterial_.normalTex = &defaultNormal_;
        voxelCursorMaterial_.ormTex = &defaultOrm_;
        voxelCursorMaterial_.set = materialPool_.allocate(ctx_);
        materialPool_.updateDescriptorSet(ctx_, voxelCursorMaterial_.set,
                                          *voxelCursorMaterial_.baseColorTex,
                                          *voxelCursorMaterial_.normalTex,
                                          *voxelCursorMaterial_.ormTex);
    }

    if (placeablePreviewValidMaterial_.set == VK_NULL_HANDLE)
    {
        placeablePreviewValidMaterial_ = Material{};
        placeablePreviewValidMaterial_.baseColorFactor = glm::vec4(0.16f, 0.9f, 0.42f, 1.0f);
        placeablePreviewValidMaterial_.roughness = 0.35f;
        placeablePreviewValidMaterial_.metallic = 0.0f;
        placeablePreviewValidMaterial_.ao = 1.0f;
        placeablePreviewValidMaterial_.baseColorTex = &defaultBaseColor_;
        placeablePreviewValidMaterial_.normalTex = &defaultNormal_;
        placeablePreviewValidMaterial_.ormTex = &defaultOrm_;
        placeablePreviewValidMaterial_.set = materialPool_.allocate(ctx_);
        materialPool_.updateDescriptorSet(ctx_, placeablePreviewValidMaterial_.set,
                                          *placeablePreviewValidMaterial_.baseColorTex,
                                          *placeablePreviewValidMaterial_.normalTex,
                                          *placeablePreviewValidMaterial_.ormTex);
    }

    if (placeablePreviewInvalidMaterial_.set == VK_NULL_HANDLE)
    {
        placeablePreviewInvalidMaterial_ = Material{};
        placeablePreviewInvalidMaterial_.baseColorFactor = glm::vec4(1.0f, 0.22f, 0.16f, 1.0f);
        placeablePreviewInvalidMaterial_.roughness = 0.35f;
        placeablePreviewInvalidMaterial_.metallic = 0.0f;
        placeablePreviewInvalidMaterial_.ao = 1.0f;
        placeablePreviewInvalidMaterial_.baseColorTex = &defaultBaseColor_;
        placeablePreviewInvalidMaterial_.normalTex = &defaultNormal_;
        placeablePreviewInvalidMaterial_.ormTex = &defaultOrm_;
        placeablePreviewInvalidMaterial_.set = materialPool_.allocate(ctx_);
        materialPool_.updateDescriptorSet(ctx_, placeablePreviewInvalidMaterial_.set,
                                          *placeablePreviewInvalidMaterial_.baseColorTex,
                                          *placeablePreviewInvalidMaterial_.normalTex,
                                          *placeablePreviewInvalidMaterial_.ormTex);
    }

    if (runtimeUiPlacementValidMaterial_.set == VK_NULL_HANDLE)
    {
        runtimeUiPlacementValidMaterial_ = Material{};
        runtimeUiPlacementValidMaterial_.baseColorFactor =
            glm::vec4(0.42f, 0.96f, 1.0f, 1.0f);
        runtimeUiPlacementValidMaterial_.roughness = 0.28f;
        runtimeUiPlacementValidMaterial_.metallic = 0.0f;
        runtimeUiPlacementValidMaterial_.ao = 1.0f;
        runtimeUiPlacementValidMaterial_.baseColorTex = &defaultBaseColor_;
        runtimeUiPlacementValidMaterial_.normalTex = &defaultNormal_;
        runtimeUiPlacementValidMaterial_.ormTex = &defaultOrm_;
        runtimeUiPlacementValidMaterial_.set = materialPool_.allocate(ctx_);
        materialPool_.updateDescriptorSet(ctx_, runtimeUiPlacementValidMaterial_.set,
                                          *runtimeUiPlacementValidMaterial_.baseColorTex,
                                          *runtimeUiPlacementValidMaterial_.normalTex,
                                          *runtimeUiPlacementValidMaterial_.ormTex);
    }

    if (runtimeUiPlacementInvalidMaterial_.set == VK_NULL_HANDLE)
    {
        runtimeUiPlacementInvalidMaterial_ = Material{};
        runtimeUiPlacementInvalidMaterial_.baseColorFactor =
            glm::vec4(1.0f, 0.34f, 0.24f, 1.0f);
        runtimeUiPlacementInvalidMaterial_.roughness = 0.34f;
        runtimeUiPlacementInvalidMaterial_.metallic = 0.0f;
        runtimeUiPlacementInvalidMaterial_.ao = 1.0f;
        runtimeUiPlacementInvalidMaterial_.baseColorTex = &defaultBaseColor_;
        runtimeUiPlacementInvalidMaterial_.normalTex = &defaultNormal_;
        runtimeUiPlacementInvalidMaterial_.ormTex = &defaultOrm_;
        runtimeUiPlacementInvalidMaterial_.set = materialPool_.allocate(ctx_);
        materialPool_.updateDescriptorSet(ctx_, runtimeUiPlacementInvalidMaterial_.set,
                                          *runtimeUiPlacementInvalidMaterial_.baseColorTex,
                                          *runtimeUiPlacementInvalidMaterial_.normalTex,
                                          *runtimeUiPlacementInvalidMaterial_.ormTex);
    }

    if (runtimeUiRemovalTargetMaterial_.set == VK_NULL_HANDLE)
    {
        runtimeUiRemovalTargetMaterial_ = Material{};
        runtimeUiRemovalTargetMaterial_.baseColorFactor =
            glm::vec4(1.0f, 0.72f, 0.24f, 1.0f);
        runtimeUiRemovalTargetMaterial_.roughness = 0.30f;
        runtimeUiRemovalTargetMaterial_.metallic = 0.0f;
        runtimeUiRemovalTargetMaterial_.ao = 1.0f;
        runtimeUiRemovalTargetMaterial_.baseColorTex = &defaultBaseColor_;
        runtimeUiRemovalTargetMaterial_.normalTex = &defaultNormal_;
        runtimeUiRemovalTargetMaterial_.ormTex = &defaultOrm_;
        runtimeUiRemovalTargetMaterial_.set = materialPool_.allocate(ctx_);
        materialPool_.updateDescriptorSet(ctx_, runtimeUiRemovalTargetMaterial_.set,
                                          *runtimeUiRemovalTargetMaterial_.baseColorTex,
                                          *runtimeUiRemovalTargetMaterial_.normalTex,
                                          *runtimeUiRemovalTargetMaterial_.ormTex);
    }
}

void App::rebuildChunkBoundsObjects()
{
    chunkBoundsObjects_.clear();
    if (chunkBoundsMesh_.vbo == VK_NULL_HANDLE)
    {
        return;
    }

    const std::vector<glm::ivec3> chunkCoords = engine::voxel::chunkCoords(voxelGrid_);
    engine::scene::ChunkBoundsInputs inputs{};
    inputs.chunkCoords = chunkCoords;
    inputs.chunkDimensions = glm::ivec3(Chunk::SX, Chunk::SY, Chunk::SZ);
    inputs.mesh = &chunkBoundsMesh_;
    inputs.material = &chunkBoundsMaterial_;
    engine::scene::SceneObjectBuilder::buildChunkBoundsObjects(inputs,
                                                               chunkBoundsObjects_);
}

void App::rebuildAxolotlRigDebugObjects()
{
    axolotlRigDebugObjects_.clear();

    if (!voxelDebugSettings_.axolotlRigDebugVisible_ || cubeMesh_.vbo == VK_NULL_HANDLE)
    {
        return;
    }

    const auto& instances = voxelWorld_.instances();
    if (instances.empty())
    {
        return;
    }

    auto ensureMaterial = [&](Material& material, const glm::vec4& color, float roughness) {
        if (material.set != VK_NULL_HANDLE)
        {
            return;
        }

        material = Material{};
        material.baseColorFactor = color;
        material.roughness = roughness;
        material.metallic = 0.0f;
        material.ao = 1.0f;
        material.baseColorTex = &defaultBaseColor_;
        material.normalTex = &defaultNormal_;
        material.ormTex = &defaultOrm_;
        material.set = materialPool_.allocate(ctx_);
        materialPool_.updateDescriptorSet(ctx_, material.set, *material.baseColorTex,
                                          *material.normalTex, *material.ormTex);
    };

    ensureMaterial(axolotlRigJointMaterial_, glm::vec4(1.0f, 0.88f, 0.15f, 1.0f), 0.24f);
    ensureMaterial(axolotlRigBoneMaterial_, glm::vec4(0.10f, 0.95f, 0.95f, 1.0f), 0.30f);
    ensureMaterial(axolotlRigPawMaterial_, glm::vec4(1.0f, 0.34f, 0.78f, 1.0f), 0.28f);

    axolotlRigDebugObjects_.reserve(
        proceduralFish_.size() *
        static_cast<size_t>(engine::game::kAxolotlLimbCount * 4 + 6));

    auto appendCube = [&](const glm::vec3& center, const glm::vec3& size,
                          Material& material) {
        RenderObject marker{};
        marker.model = centeredBoxModel(center, size);
        marker.mesh = &cubeMesh_;
        marker.material = &material;
        axolotlRigDebugObjects_.push_back(marker);
    };

    auto appendBone = [&](const glm::vec3& start, const glm::vec3& end, float thickness) {
        glm::mat4 model(1.0f);
        if (!centeredBoneModel(start, end, thickness, model))
        {
            return;
        }

        RenderObject bone{};
        bone.model = model;
        bone.mesh = &cubeMesh_;
        bone.material = &axolotlRigBoneMaterial_;
        axolotlRigDebugObjects_.push_back(bone);
    };

    for (const ProceduralFishInstance& fish : proceduralFish_)
    {
        if (!fish.isAxolotl || fish.bodyVolumeIndex < 0 ||
            static_cast<size_t>(fish.bodyVolumeIndex) >= instances.size())
        {
            continue;
        }

        const engine::VoxelInstance& bodyInst =
            instances[static_cast<size_t>(fish.bodyVolumeIndex)];
        if (!bodyInst.visible)
        {
            continue;
        }

        const glm::mat4& bodyFromLocal = bodyInst.volume.worldFromLocal();
        const float jointSize = std::max(0.08f, fish.scale * 0.34f);
        const float endSize = std::max(0.06f, fish.scale * 0.25f);
        const float boneThickness = std::max(0.035f, fish.scale * 0.11f);
        const glm::vec3 bodyAnchorWorld =
            transformPoint(bodyFromLocal, kFishBodyAnchorLocal);
        const glm::vec3 tailRootWorld =
            transformPoint(bodyFromLocal,
                           kFishBodyAnchorLocal + kFishTailRootFromBodyAnchorLocal);

        appendCube(bodyAnchorWorld, glm::vec3(jointSize), axolotlRigJointMaterial_);
        appendCube(tailRootWorld, glm::vec3(jointSize * 0.85f),
                   axolotlRigJointMaterial_);
        appendBone(bodyAnchorWorld, tailRootWorld, boneThickness);

        if (fish.tailVolumeIndex >= 0 &&
            static_cast<size_t>(fish.tailVolumeIndex) < instances.size())
        {
            const glm::mat4& tailFromLocal =
                instances[static_cast<size_t>(fish.tailVolumeIndex)].volume.worldFromLocal();
            const glm::vec3 tailTipWorld =
                transformPoint(tailFromLocal, kAxolotlTailDebugTipLocal);
            appendCube(tailTipWorld, glm::vec3(endSize), axolotlRigPawMaterial_);
            appendBone(tailRootWorld, tailTipWorld, boneThickness * 0.9f);
        }

        for (size_t limb = 0; limb < fish.axolotlLimbVolumeIndices.size(); ++limb)
        {
            const int limbVolumeIndex = fish.axolotlLimbVolumeIndices[limb];
            if (limbVolumeIndex < 0 ||
                static_cast<size_t>(limbVolumeIndex) >= instances.size())
            {
                continue;
            }

            const glm::mat4& limbFromLocal =
                instances[static_cast<size_t>(limbVolumeIndex)].volume.worldFromLocal();
            const glm::vec3 jointWorld =
                transformPoint(bodyFromLocal, kAxolotlLimbRig[limb].attachLocal);
            const glm::vec3 pivotWorld =
                transformPoint(limbFromLocal, engine::game::kAxolotlLimbAnchorLocal);
            const glm::vec3 pawWorld =
                transformPoint(limbFromLocal, kAxolotlLimbDebugPawLocal);

            appendCube(jointWorld, glm::vec3(jointSize * 0.72f),
                       axolotlRigJointMaterial_);
            appendCube(pawWorld, glm::vec3(endSize), axolotlRigPawMaterial_);
            appendBone(bodyAnchorWorld, jointWorld, boneThickness * 0.75f);
            appendBone(pivotWorld, pawWorld, boneThickness);
        }
    }
}

void App::rebuildSceneObjects()
{
    engine::scene::SceneObjectBuilderInputs builderInputs{};
    builderInputs.staticObjects = std::span<const RenderObject>(objects_);
    builderInputs.animatedObjects = std::span<const RenderObject>(animatedObjects_);
    builderInputs.waterFoamObjects = std::span<const RenderObject>(waterFoamObjects_);
    builderInputs.voxelObjects = std::span<const RenderObject>(voxelObjects_);
    builderInputs.voxelShadowObjects = std::span<const RenderObject>(voxelShadowObjects_);
    builderInputs.chunkBoundsVisibleObjects =
        std::span<const RenderObject>(chunkBoundsVisibleObjects_);
    builderInputs.volumeBoundsVisibleObjects =
        std::span<const RenderObject>(volumeBoundsVisibleObjects_);
    builderInputs.voxelVisible = voxelDebugSettings_.voxelVisible_;
    builderInputs.chunkBoundsVisible = voxelDebugSettings_.chunkBoundsVisible_;
    builderInputs.volumeBoundsVisible = voxelDebugSettings_.volumeBoundsVisible_;
    builderInputs.overlayReserveHint = 4;
    engine::scene::SceneObjectBuilder::buildBaseDrawLists(builderInputs,
                                                          sceneObjects_,
                                                          shadowObjects_);

    if (voxelDebugSettings_.axolotlRigDebugVisible_)
    {
        rebuildAxolotlRigDebugObjects();
        sceneObjects_.insert(sceneObjects_.end(), axolotlRigDebugObjects_.begin(),
                             axolotlRigDebugObjects_.end());
    }
    else if (!axolotlRigDebugObjects_.empty())
    {
        axolotlRigDebugObjects_.clear();
    }

    const bool placeablePreviewVisible =
        placeablePreview_.active && placeablePreview_.evaluation.hasHit;
#if VOXEL_WITH_RUNTIME_UI
    const ui::OverlaySmokeScreenState& runtimeUiOverlayState =
        runtimeUiOverlaySmokeController_.state();
    const ui::BuildPlacementProbe& runtimeUiPlacementProbe =
        runtimeUiOverlayState.placementProbe;
    const bool runtimeUiPlacementGhostVisible =
        runtimeUiPlacementProbe.active && runtimeUiPlacementProbe.hasPlacementCandidate;
#endif
    if (voxelCursorVisible_ && chunkBoundsMesh_.vbo != VK_NULL_HANDLE &&
        voxelCursorMaterial_.set != VK_NULL_HANDLE)
    {
        RenderObject cursor{};
        cursor.model = glm::translate(glm::mat4(1.0f), glm::vec3(voxelCursorVoxel_));
        cursor.mesh = &chunkBoundsMesh_;
        cursor.material = &voxelCursorMaterial_;
        sceneObjects_.push_back(cursor);
    }
    if (placeablePreviewVisible && chunkBoundsMesh_.vbo != VK_NULL_HANDLE &&
        placeablePreviewValidMaterial_.set != VK_NULL_HANDLE &&
        placeablePreviewInvalidMaterial_.set != VK_NULL_HANDLE)
    {
        const float radius = std::max(0.25f, placeablePreview_.footprintRadius);
        const glm::vec3 origin =
            placeablePreview_.evaluation.hit.position + glm::vec3(-radius, 0.04f, -radius);
        RenderObject preview{};
        preview.model = glm::translate(glm::mat4(1.0f), origin) *
                        glm::scale(glm::mat4(1.0f),
                                   glm::vec3(radius * 2.0f, 0.18f, radius * 2.0f));
        preview.mesh = &chunkBoundsMesh_;
        preview.material = placeablePreview_.evaluation.valid ? &placeablePreviewValidMaterial_
                                                              : &placeablePreviewInvalidMaterial_;
        sceneObjects_.push_back(preview);
    }
#if VOXEL_WITH_RUNTIME_UI
    bool runtimeUiPlacementGhostRendered = false;
    bool runtimeUiPlacementSurfaceHighlightRendered = false;
    bool runtimeUiPlacementGhostFillRendered = false;
    int runtimeUiPlacementGhostFillPrimitiveCount = 0;
    std::string runtimeUiPlacementGhostFillMaterial = "none";
    std::string runtimeUiPlacementGhostFillShape = "none";
    std::string runtimeUiPlacementGhostPlaceablePrototype = "none";
    std::string runtimeUiPlacementGhostBoundsShape = "none";
    int runtimeUiPlacementGhostPlaceableFootprintRadiusMm = 0;
    bool runtimeUiPlacementGhostPrototypeBacked = false;
    if (runtimeUiPlacementGhostVisible && chunkBoundsMesh_.vbo != VK_NULL_HANDLE &&
        runtimeUiPlacementValidMaterial_.set != VK_NULL_HANDLE &&
        runtimeUiPlacementInvalidMaterial_.set != VK_NULL_HANDLE &&
        runtimeUiRemovalTargetMaterial_.set != VK_NULL_HANDLE)
    {
        constexpr float kRuntimeUiPlacementGhostMargin = 0.03f;
        const glm::vec3 footprint =
            glm::vec3(glm::max(runtimeUiPlacementProbe.footprintVoxels,
                               glm::ivec3(1)));
        const bool removalMode =
            runtimeUiOverlayState.buildModeState == ui::BuildModeState::PendingRemoval;
        const bool targetPlaceable =
            removalMode && !runtimeUiPlacementProbe.targetPlaceableUuid.empty();
        Material* placementMaterial =
            !runtimeUiPlacementProbe.placementValid
                ? &runtimeUiPlacementInvalidMaterial_
                : removalMode ? &runtimeUiRemovalTargetMaterial_
                              : &runtimeUiPlacementValidMaterial_;
        const bool hasSurfaceNormal =
            runtimeUiPlacementProbe.hitNormal.x != 0 ||
            runtimeUiPlacementProbe.hitNormal.y != 0 ||
            runtimeUiPlacementProbe.hitNormal.z != 0;
        if (runtimeUiPlacementProbe.rayHit &&
            runtimeUiPlacementProbe.snappedToGrid &&
            hasSurfaceNormal)
        {
            RenderObject highlight{};
            highlight.model =
                runtimeUiPlacementSurfaceHighlightModel(runtimeUiPlacementProbe);
            highlight.mesh = &chunkBoundsMesh_;
            highlight.material = placementMaterial;
            sceneObjects_.push_back(highlight);
            runtimeUiPlacementSurfaceHighlightRendered = true;
        }

        if (runtimeUiOverlayState.buildModeState == ui::BuildModeState::PendingPlacement &&
            cubeMesh_.vbo != VK_NULL_HANDLE)
        {
            constexpr float kRuntimeUiPlacementGhostFillInset = 0.16f;
            const ui::BuildCatalogItemDefinition* item =
                ui::findRuntimeBuildCatalogItem(runtimeUiPlacementProbe.itemKey);
            const FoliagePrototype* foliagePrototype = nullptr;
            Material* fillMaterial = nullptr;
            if (item != nullptr)
            {
                if (item->hasPlaceablePrototype())
                {
                    foliagePrototype =
                        FoliageCatalog::findPrototype(item->placeablePrototypeSlug,
                                                      item->placeablePrototypeVersion);
                    runtimeUiPlacementGhostPlaceablePrototype =
                        std::string(item->placeablePrototypeSlug) + "@" +
                        std::to_string(item->placeablePrototypeVersion);
                    if (foliagePrototype != nullptr)
                    {
                        runtimeUiPlacementGhostPrototypeBacked = true;
                        runtimeUiPlacementGhostPlaceableFootprintRadiusMm =
                            static_cast<int>(std::round(
                                foliagePrototype->placeable.placement.footprintRadius *
                                1000.0f));
                    }
                }
                if (runtimeUiPlacementProbe.placementValid &&
                    item->block > BLOCK_AIR && item->block < kBlockTypeCount &&
                    voxelMaterials_[item->block].set != VK_NULL_HANDLE)
                {
                    fillMaterial = &voxelMaterials_[item->block];
                    runtimeUiPlacementGhostFillMaterial =
                        std::string("block:") + std::to_string(item->block);
                }
                else if (!runtimeUiPlacementProbe.placementValid)
                {
                    fillMaterial = &runtimeUiPlacementInvalidMaterial_;
                    runtimeUiPlacementGhostFillMaterial = "invalid";
                }
            }

            if (fillMaterial != nullptr && fillMaterial->set != VK_NULL_HANDLE)
            {
                auto appendFillBox =
                    [&](const glm::vec3& center01, const glm::vec3& size01) {
                        const glm::vec3 clampedSize01 =
                            glm::clamp(size01, glm::vec3(0.02f),
                                       glm::vec3(1.0f, 1.65f, 1.0f));
                        const glm::vec3 size =
                            glm::max(glm::vec3(0.06f), clampedSize01 * footprint);
                        const glm::vec3 origin =
                            glm::vec3(runtimeUiPlacementProbe.placementVoxel) +
                            center01 * footprint - size * 0.5f;
                        RenderObject fill{};
                        fill.model = glm::translate(glm::mat4(1.0f), origin) *
                                     glm::scale(glm::mat4(1.0f), size);
                        fill.mesh = &cubeMesh_;
                        fill.material = fillMaterial;
                        sceneObjects_.push_back(fill);
                        runtimeUiPlacementGhostFillRendered = true;
                        ++runtimeUiPlacementGhostFillPrimitiveCount;
                    };

                auto appendPlantGhostShape = [&]() {
                    appendFillBox(glm::vec3(0.25f, 0.54f, 0.36f),
                                  glm::vec3(0.045f, 1.00f, 0.045f));
                    appendFillBox(glm::vec3(0.34f, 0.68f, 0.48f),
                                  glm::vec3(0.050f, 1.24f, 0.050f));
                    appendFillBox(glm::vec3(0.46f, 0.80f, 0.38f),
                                  glm::vec3(0.045f, 1.42f, 0.045f));
                    appendFillBox(glm::vec3(0.56f, 0.66f, 0.58f),
                                  glm::vec3(0.050f, 1.18f, 0.050f));
                    appendFillBox(glm::vec3(0.67f, 0.52f, 0.44f),
                                  glm::vec3(0.045f, 0.92f, 0.045f));
                    appendFillBox(glm::vec3(0.42f, 0.34f, 0.62f),
                                  glm::vec3(0.040f, 0.62f, 0.040f));
                    appendFillBox(glm::vec3(0.74f, 0.38f, 0.62f),
                                  glm::vec3(0.040f, 0.70f, 0.040f));
                };
                auto appendCoralGhostShape = [&]() {
                    appendFillBox(glm::vec3(0.50f, 0.34f, 0.50f),
                                  glm::vec3(0.12f, 0.58f, 0.12f));
                    appendFillBox(glm::vec3(0.34f, 0.52f, 0.50f),
                                  glm::vec3(0.30f, 0.12f, 0.12f));
                    appendFillBox(glm::vec3(0.66f, 0.66f, 0.50f),
                                  glm::vec3(0.30f, 0.12f, 0.12f));
                    appendFillBox(glm::vec3(0.54f, 0.78f, 0.36f),
                                  glm::vec3(0.12f, 0.24f, 0.12f));
                };

                if (foliagePrototype != nullptr)
                {
                    const std::string& slug = foliagePrototype->placeable.slug;
                    runtimeUiPlacementGhostFillShape =
                        std::string("prototype:") + slug;
                    if (slug == "eelgrass" || slug == "ribbon_kelp")
                    {
                        appendPlantGhostShape();
                    }
                    else
                    {
                        appendCoralGhostShape();
                    }
                }
                else if (runtimeUiPlacementProbe.itemKey == ui::kBuildCatalogKeyFilter)
                {
                    runtimeUiPlacementGhostFillShape = "filter";
                    appendFillBox(glm::vec3(0.5f, 0.42f, 0.5f),
                                  glm::vec3(0.42f, 0.62f, 0.42f));
                    appendFillBox(glm::vec3(0.5f, 0.80f, 0.5f),
                                  glm::vec3(0.56f, 0.14f, 0.56f));
                    appendFillBox(glm::vec3(0.5f, 0.13f, 0.5f),
                                  glm::vec3(0.54f, 0.16f, 0.54f));
                }
                else if (runtimeUiPlacementProbe.itemKey == ui::kBuildCatalogKeyDecor)
                {
                    runtimeUiPlacementGhostFillShape = "decor";
                    appendFillBox(glm::vec3(0.50f, 0.16f, 0.50f),
                                  glm::vec3(0.84f, 0.28f, 0.72f));
                    appendFillBox(glm::vec3(0.34f, 0.46f, 0.50f),
                                  glm::vec3(0.28f, 0.34f, 0.50f));
                    appendFillBox(glm::vec3(0.66f, 0.46f, 0.50f),
                                  glm::vec3(0.28f, 0.34f, 0.50f));
                }
                else if (runtimeUiPlacementProbe.itemKey == ui::kBuildCatalogKeyHeater)
                {
                    runtimeUiPlacementGhostFillShape = "heater";
                    appendFillBox(glm::vec3(0.38f, 0.48f, 0.48f),
                                  glm::vec3(0.18f, 0.82f, 0.18f));
                    appendFillBox(glm::vec3(0.62f, 0.48f, 0.52f),
                                  glm::vec3(0.18f, 0.82f, 0.18f));
                    appendFillBox(glm::vec3(0.50f, 0.18f, 0.50f),
                                  glm::vec3(0.54f, 0.16f, 0.34f));
                }
                else if (runtimeUiPlacementProbe.itemKey == ui::kBuildCatalogKeyLight)
                {
                    runtimeUiPlacementGhostFillShape = "light";
                    appendFillBox(glm::vec3(0.50f, 0.68f, 0.50f),
                                  glm::vec3(0.78f, 0.18f, 0.28f));
                    appendFillBox(glm::vec3(0.28f, 0.36f, 0.50f),
                                  glm::vec3(0.12f, 0.52f, 0.16f));
                    appendFillBox(glm::vec3(0.72f, 0.36f, 0.50f),
                                  glm::vec3(0.12f, 0.52f, 0.16f));
                }
                else if (runtimeUiPlacementProbe.itemKey == ui::kBuildCatalogKeyRock)
                {
                    runtimeUiPlacementGhostFillShape = "rock";
                    appendFillBox(glm::vec3(0.48f, 0.18f, 0.50f),
                                  glm::vec3(0.76f, 0.30f, 0.70f));
                    appendFillBox(glm::vec3(0.36f, 0.44f, 0.44f),
                                  glm::vec3(0.44f, 0.28f, 0.40f));
                    appendFillBox(glm::vec3(0.66f, 0.48f, 0.58f),
                                  glm::vec3(0.34f, 0.36f, 0.34f));
                }
                else
                {
                    runtimeUiPlacementGhostFillShape = "block";
                    const glm::vec3 inset =
                        glm::min(glm::vec3(kRuntimeUiPlacementGhostFillInset),
                                 footprint * 0.24f);
                    const glm::vec3 fillSize =
                        glm::max(glm::vec3(0.08f), footprint - inset * 2.0f);
                    appendFillBox((inset + fillSize * 0.5f) / footprint,
                                  fillSize / footprint);
                }
            }
        }

        if (removalMode && runtimeUiPlacementProbe.placementValid &&
            cubeMesh_.vbo != VK_NULL_HANDLE)
        {
            const glm::vec3 center =
                targetPlaceable
                    ? runtimeUiPlacementProbe.ghostWorldPosition + glm::vec3(0.0f, 0.08f, 0.0f)
                    : glm::vec3(runtimeUiPlacementProbe.placementVoxel) +
                          glm::vec3(0.5f, 0.08f, 0.5f);
            const float halfExtent = targetPlaceable ? 1.42f : 0.48f;
            const float barLength = targetPlaceable ? 0.86f : 0.36f;
            constexpr float kBarThickness = 0.07f;
            auto appendRemovalBar = [&](const glm::vec3& offset,
                                        const glm::vec3& size) {
                RenderObject bar{};
                bar.model =
                    glm::translate(glm::mat4(1.0f), center + offset - size * 0.5f) *
                    glm::scale(glm::mat4(1.0f), size);
                bar.mesh = &cubeMesh_;
                bar.material = placementMaterial;
                sceneObjects_.push_back(bar);
            };

            appendRemovalBar(glm::vec3(-halfExtent + barLength * 0.5f, 0.0f, -halfExtent),
                             glm::vec3(barLength, kBarThickness, kBarThickness));
            appendRemovalBar(glm::vec3(halfExtent - barLength * 0.5f, 0.0f, halfExtent),
                             glm::vec3(barLength, kBarThickness, kBarThickness));
            appendRemovalBar(glm::vec3(-halfExtent, 0.0f, halfExtent - barLength * 0.5f),
                             glm::vec3(kBarThickness, kBarThickness, barLength));
            appendRemovalBar(glm::vec3(halfExtent, 0.0f, -halfExtent + barLength * 0.5f),
                             glm::vec3(kBarThickness, kBarThickness, barLength));
        }

        RenderObject ghost{};
        if (targetPlaceable)
        {
            const glm::vec3 baseScale(2.85f, 0.07f, 2.85f);
            const glm::vec3 baseOrigin =
                runtimeUiPlacementProbe.ghostWorldPosition -
                glm::vec3(baseScale.x * 0.5f, -kRuntimeUiPlacementGhostMargin,
                          baseScale.z * 0.5f);
            ghost.model = glm::translate(glm::mat4(1.0f), baseOrigin) *
                          glm::scale(glm::mat4(1.0f), baseScale);
            runtimeUiPlacementGhostBoundsShape = "target-base";
        }
        else if (runtimeUiPlacementGhostPrototypeBacked)
        {
            const glm::vec3 baseScale =
                glm::max(glm::vec3(0.16f),
                         glm::vec3(footprint.x * 0.78f, 0.08f,
                                   footprint.z * 0.78f));
            const glm::vec3 baseOrigin =
                glm::vec3(runtimeUiPlacementProbe.placementVoxel) +
                glm::vec3((footprint.x - baseScale.x) * 0.5f,
                          kRuntimeUiPlacementGhostMargin,
                          (footprint.z - baseScale.z) * 0.5f);
            ghost.model = glm::translate(glm::mat4(1.0f), baseOrigin) *
                          glm::scale(glm::mat4(1.0f), baseScale);
            runtimeUiPlacementGhostBoundsShape = "base";
        }
        else if (removalMode)
        {
            constexpr float kRuntimeUiRemovalBoxInset = 0.08f;
            const glm::vec3 origin =
                glm::vec3(runtimeUiPlacementProbe.placementVoxel) +
                glm::vec3(kRuntimeUiRemovalBoxInset);
            ghost.model =
                glm::translate(glm::mat4(1.0f), origin) *
                glm::scale(glm::mat4(1.0f),
                           glm::max(glm::vec3(0.10f),
                                    footprint -
                                        glm::vec3(kRuntimeUiRemovalBoxInset * 2.0f)));
            runtimeUiPlacementGhostBoundsShape = "target-box";
        }
        else
        {
            const glm::vec3 origin =
                glm::vec3(runtimeUiPlacementProbe.placementVoxel) -
                glm::vec3(kRuntimeUiPlacementGhostMargin);
            ghost.model = glm::translate(glm::mat4(1.0f), origin) *
                          glm::scale(glm::mat4(1.0f),
                                     footprint +
                                         glm::vec3(kRuntimeUiPlacementGhostMargin *
                                                   2.0f));
            runtimeUiPlacementGhostBoundsShape = "box";
        }
        ghost.mesh = &chunkBoundsMesh_;
        ghost.material = placementMaterial;
        sceneObjects_.push_back(ghost);
        runtimeUiPlacementGhostRendered = true;
    }
    if (runtimeUiOverlaySmokeController_.shouldLogBuildPlacementGhost())
    {
        logInfo("RuntimeUI",
                makeLogMessage("Build placement ghost visible=",
                               runtimeUiPlacementGhostRendered ? 1 : 0,
                               " active=", runtimeUiPlacementProbe.active ? 1 : 0,
                               " item_key=",
                               runtimeUiPlacementProbe.itemKey.empty()
                                   ? std::string("<none>")
                                   : runtimeUiPlacementProbe.itemKey,
                               " ray_hit=", runtimeUiPlacementProbe.rayHit ? 1 : 0,
                               " candidate=",
                               runtimeUiPlacementProbe.hasPlacementCandidate ? 1 : 0,
                               " snapped=",
                               runtimeUiPlacementProbe.snappedToGrid ? 1 : 0,
                               " valid=", runtimeUiPlacementProbe.placementValid ? 1 : 0,
                               " invalid_reason=",
                               runtimeUiPlacementProbe.invalidReason.empty()
                                   ? std::string("<none>")
                                   : runtimeUiPlacementProbe.invalidReason,
                               " placement_voxel=",
                               runtimeUiPlacementProbe.placementVoxel.x, ",",
                               runtimeUiPlacementProbe.placementVoxel.y, ",",
                               runtimeUiPlacementProbe.placementVoxel.z,
                               " rotation_steps=",
                               runtimeUiPlacementProbe.rotationSteps,
                               " footprint=",
                               runtimeUiPlacementProbe.footprintVoxels.x, ",",
                               runtimeUiPlacementProbe.footprintVoxels.y, ",",
                               runtimeUiPlacementProbe.footprintVoxels.z,
                               " world_center_mm=",
                               static_cast<int>(std::round(
                                   runtimeUiPlacementProbe.ghostWorldPosition.x *
                                   1000.0f)),
                               ",",
                               static_cast<int>(std::round(
                                   runtimeUiPlacementProbe.ghostWorldPosition.y *
                                   1000.0f)),
                               ",",
                               static_cast<int>(std::round(
                                   runtimeUiPlacementProbe.ghostWorldPosition.z *
                                   1000.0f)),
                               " highlight=",
                               runtimeUiPlacementSurfaceHighlightRendered ? 1 : 0,
                               " highlight_normal=",
                               runtimeUiPlacementProbe.hitNormal.x, ",",
                               runtimeUiPlacementProbe.hitNormal.y, ",",
                               runtimeUiPlacementProbe.hitNormal.z,
                               " fill=", runtimeUiPlacementGhostFillRendered ? 1 : 0,
                               " fill_material=",
                               runtimeUiPlacementGhostFillMaterial,
                               " fill_shape=", runtimeUiPlacementGhostFillShape,
                               " fill_primitives=",
                               runtimeUiPlacementGhostFillPrimitiveCount,
                               " placeable_prototype=",
                               runtimeUiPlacementGhostPlaceablePrototype,
                               " placeable_footprint_radius_mm=",
                               runtimeUiPlacementGhostPlaceableFootprintRadiusMm,
                               " bounds_shape=",
                               runtimeUiPlacementGhostBoundsShape,
                               " material=",
                               runtimeUiPlacementProbe.placementValid ? "valid"
                                                                       : "invalid"));
        runtimeUiOverlaySmokeController_.markBuildPlacementGhostLogged();
    }
#endif
    sceneObjectsDirty_ = false;
    animatedObjectsDirty_ = false;
}

void App::resetAreaLightsToScenePreset()
{
    lightingSettings_.areaLights_.clear();

    auto pushSphere = [this](const glm::vec3& pos, float sourceRadius, float influenceRadius,
                             const glm::vec3& color, float intensity, bool castsShadows) {
        EditableAreaLight light{};
        light.shape = LightShape::Sphere;
        light.castsShadows = castsShadows;
        light.position = pos;
        light.sourceRadius = sourceRadius;
        light.influenceRadius = influenceRadius;
        light.color = color;
        light.intensity = intensity;
        lightingSettings_.areaLights_.push_back(light);
    };

    auto pushRectangle = [this](const glm::vec3& center, const glm::vec3& edge1,
                                const glm::vec3& edge2, float influenceRadius,
                                const glm::vec3& color, float intensity, bool castsShadows) {
        EditableAreaLight light{};
        light.shape = LightShape::Rectangle;
        light.castsShadows = castsShadows;
        light.position = center;
        light.edge1 = edge1;
        light.edge2 = edge2;
        light.influenceRadius = influenceRadius;
        light.color = color;
        light.intensity = intensity;
        lightingSettings_.areaLights_.push_back(light);
    };

    auto pushCapsule = [this](const glm::vec3& endA, const glm::vec3& endB, float sourceRadius,
                              float influenceRadius, const glm::vec3& color, float intensity,
                              bool castsShadows) {
        EditableAreaLight light{};
        light.shape = LightShape::Capsule;
        light.castsShadows = castsShadows;
        light.capsuleEndA = endA;
        light.capsuleEndB = endB;
        light.position = (endA + endB) * 0.5f;
        light.sourceRadius = sourceRadius;
        light.influenceRadius = influenceRadius;
        light.color = color;
        light.intensity = intensity;
        lightingSettings_.areaLights_.push_back(light);
    };

    if (!engine::scene::applyLightingParityProbeAreaLights(
            sceneConfig(), lightingSettings_) && sceneConfig().loadAquariumTest)
    {
        const auto& infos = AquariumScene::getVolumeInfos();
        if (infos.size() > AquariumScene::VOLUME_TANK_COMPOSITE)
        {
            const AquariumVolumeInfo& tank = infos[AquariumScene::VOLUME_TANK_COMPOSITE];
            const glm::vec3 tankMin = tank.worldPosition;
            const glm::vec3 tankDims = glm::vec3(tank.dimensions);
            const glm::vec3 tankCenter = tankMin + tankDims * 0.5f;
            const float maxDim = std::max(tankDims.x, std::max(tankDims.y, tankDims.z));

            if (isFishbowlAquariumScene(sceneConfig()))
            {
                pushRectangle(glm::vec3(tankCenter.x, tankMin.y + tankDims.y + 4.0f,
                                         tankCenter.z + tankDims.z * 0.05f),
                              glm::vec3(tankDims.x * 0.28f, 0.0f, 0.0f),
                              glm::vec3(0.0f, 0.0f, tankDims.z * 0.22f), maxDim * 1.8f,
                              glm::vec3(0.93f, 0.98f, 1.0f), 4.0f, true);

                pushSphere(glm::vec3(tankCenter.x + tankDims.x * 0.24f,
                                     tankCenter.y + tankDims.y * 0.15f,
                                     tankCenter.z + tankDims.z * 0.36f),
                           0.42f, maxDim * 1.25f, glm::vec3(1.0f, 0.86f, 0.66f), 1.5f,
                           true);

                pushCapsule(glm::vec3(tankCenter.x - tankDims.x * 0.18f,
                                      tankCenter.y + tankDims.y * 0.46f,
                                      tankCenter.z + tankDims.z * 0.16f),
                            glm::vec3(tankCenter.x + tankDims.x * 0.18f,
                                      tankCenter.y + tankDims.y * 0.46f,
                                      tankCenter.z + tankDims.z * 0.16f),
                            0.12f, maxDim * 1.1f, glm::vec3(0.62f, 0.82f, 1.0f), 0.8f,
                            false);
            }
            else if (isSunroofAquariumScene(sceneConfig()))
            {
                const AquariumVolumeInfo& room =
                    infos[static_cast<size_t>(AquariumScene::VOLUME_ROOM)];
                const glm::vec3 roomMin = room.worldPosition;
                const glm::vec3 roomDims = glm::vec3(room.dimensions);
                const float chamberMaxDim =
                    std::max(roomDims.x, std::max(roomDims.y, roomDims.z));

                pushRectangle(glm::vec3(tankCenter.x, roomMin.y + roomDims.y - 1.0f, tankCenter.z),
                              glm::vec3(tankDims.x * 0.46f, 0.0f, 0.0f),
                              glm::vec3(0.0f, 0.0f, tankDims.z * 0.46f), chamberMaxDim * 1.7f,
                              glm::vec3(1.0f, 0.98f, 0.93f), 12.0f, false);
            }
            else
            {
                pushRectangle(glm::vec3(tankCenter.x, tankMin.y + tankDims.y + 2.0f, tankCenter.z),
                              glm::vec3(tankDims.x * 0.33f, 0.0f, 0.0f),
                              glm::vec3(0.0f, 0.0f, tankDims.z * 0.26f), maxDim * 2.0f,
                              glm::vec3(0.92f, 0.96f, 1.0f), 5.0f, true);

                pushSphere(glm::vec3(tankCenter.x + tankDims.x * 0.30f,
                                     tankCenter.y + tankDims.y * 0.25f,
                                     tankCenter.z - tankDims.z * 0.24f),
                           0.65f, maxDim * 1.6f, glm::vec3(1.0f, 0.90f, 0.72f), 2.2f, true);

                const float tubeY = tankMin.y + tankDims.y * 0.68f;
                const float tubeZ = tankMin.z + tankDims.z * 0.82f;
                pushCapsule(glm::vec3(tankMin.x + tankDims.x * 0.18f, tubeY, tubeZ),
                            glm::vec3(tankMin.x + tankDims.x * 0.82f, tubeY, tubeZ), 0.18f,
                            maxDim * 1.5f, glm::vec3(0.70f, 0.85f, 1.0f), 1.3f, false);
            }
        }
    }

    if (lightingSettings_.areaLights_.empty())
    {
        pushSphere(glm::vec3(0.0f, 6.0f, 0.0f), 0.4f, 20.0f, glm::vec3(1.0f, 0.95f, 0.85f), 3.2f,
                   true);
        pushRectangle(glm::vec3(0.0f, 8.5f, -3.0f), glm::vec3(2.4f, 0.0f, 0.0f),
                      glm::vec3(0.0f, 0.0f, 1.6f), 24.0f, glm::vec3(0.85f, 0.92f, 1.0f), 2.0f,
                      false);
    }

    const int maxLights = static_cast<int>(std::min<uint32_t>(
        lights_.maxLights, static_cast<uint32_t>(lightingSettings_.areaLights_.size())));
    lightingSettings_.lightCount_ = std::clamp(lightingSettings_.lightCount_, 0, maxLights);
    input_.setLightCount(lightingSettings_.lightCount_);

    shadowSettings_.shadowResetHistory_ = true;
    shadowSettings_.localShadowResetHistory_ = true;
}

void App::updateSunDirection()
{
    const float elevRad = glm::radians(lightingSettings_.sunElevation_);
    const float azimRad = glm::radians(lightingSettings_.sunAzimuth_);

    // direction from sun toward scene (light travel direction)
    // y is negative because sun shines downward
    sunDirection_ = glm::normalize(glm::vec3(
        std::cos(elevRad) * std::cos(azimRad),
        -std::sin(elevRad),
        std::cos(elevRad) * std::sin(azimRad)));

    if (isSunroofAquariumScene(sceneConfig()) && cubeMesh_.vbo != VK_NULL_HANDLE)
    {
        rebuildGlassObjectsForCurrentScene();
        sceneObjectsDirty_ = true;
        postFxSettings_.taaResetHistory_ = true;
        shadowSettings_.shadowResetHistory_ = true;
        shadowSettings_.localShadowResetHistory_ = true;
        aoSettings_.aoResetHistory_ = true;
    }
}

void App::applySkyPreset(int preset)
{
    const bool updatePaintedSky = lightingSettings_.paintedSky_.enabled();
    skyPreset_ = preset;
    lightingSettings_.sunAzimuth_ = 135.0f;
    switch (preset)
    {
    case 0: // day - bright blue sky
        skyColor_ = glm::vec3(0.09f, 0.29f, 0.88f);
        lightingSettings_.sunElevation_ = 55.0f;
        lightingSettings_.sunColor_ = glm::vec3(1.0f, 0.95f, 0.85f);
        lightingSettings_.sunIntensity_ = 2.5f;
        break;
    case 1: // sunset - orange/pink sky
        skyColor_ = glm::vec3(0.95f, 0.45f, 0.25f);
        lightingSettings_.sunElevation_ = 10.0f;
        lightingSettings_.sunColor_ = glm::vec3(1.0f, 0.5f, 0.2f);
        lightingSettings_.sunIntensity_ = 1.8f;
        break;
    case 2: // night - dark blue with stars
        skyColor_ = glm::vec3(0.02f, 0.02f, 0.08f);
        lightingSettings_.sunElevation_ = -20.0f;
        lightingSettings_.sunColor_ = glm::vec3(0.3f, 0.35f, 0.5f);  // moonlight
        lightingSettings_.sunIntensity_ = 0.3f;
        break;
    case 3: // dawn - soft pink/purple
        skyColor_ = glm::vec3(0.6f, 0.35f, 0.55f);
        lightingSettings_.sunElevation_ = 5.0f;
        lightingSettings_.sunColor_ = glm::vec3(1.0f, 0.7f, 0.5f);
        lightingSettings_.sunIntensity_ = 1.2f;
        break;
    default: // custom - don't change anything
        break;
    }
    if (updatePaintedSky)
    {
        engine::render::PaintedSkyPreset paintedPreset =
            engine::render::PaintedSkyPreset::SoftDayV0;
        switch (preset)
        {
        case 1:
            paintedPreset = engine::render::PaintedSkyPreset::WarmSunsetV0;
            break;
        case 2:
            paintedPreset = engine::render::PaintedSkyPreset::NightV0;
            break;
        case 3:
            paintedPreset = engine::render::PaintedSkyPreset::DawnV0;
            break;
        default:
            break;
        }
        if (preset >= 0 && preset <= 3)
        {
            lightingSettings_.paintedSky_ =
                engine::render::makePaintedSkySettings(paintedPreset);
        }
    }
    updateSunDirection();
}

void App::updateLights(float timeSeconds)
{
    (void)timeSeconds;
    if (lights_.mapped == nullptr || lights_.maxLights == 0)
    {
        return;
    }

    auto* gpuLights = reinterpret_cast<AreaLightGpu*>(lights_.mapped);
    uint32_t count = 0u;
    if (lightingSettings_.pointLightsEnabled_)
    {
        const uint32_t availableLights =
            static_cast<uint32_t>(std::min<size_t>(lightingSettings_.areaLights_.size(), lights_.maxLights));
        count = std::min(static_cast<uint32_t>(lightingSettings_.lightCount_),
                         availableLights);
    }
    if (count == 0)
    {
        return;
    }

    for (uint32_t i = 0; i < count; ++i)
    {
        const EditableAreaLight& src = lightingSettings_.areaLights_[i];
        glm::vec3 center = src.position;
        const glm::vec3 color = glm::max(src.color, glm::vec3(0.0f));
        const float influence = std::max(src.influenceRadius, 0.001f);

        for (int c = 0; c < 4; ++c)
        {
            gpuLights[i].shapeParam0[c] = 0.0f;
            gpuLights[i].shapeParam1[c] = 0.0f;
        }

        float sourceRadius = 0.0f;

        switch (src.shape)
        {
        case LightShape::Rectangle:
            gpuLights[i].shapeParam0[0] = src.edge1.x;
            gpuLights[i].shapeParam0[1] = src.edge1.y;
            gpuLights[i].shapeParam0[2] = src.edge1.z;
            gpuLights[i].shapeParam1[0] = src.edge2.x;
            gpuLights[i].shapeParam1[1] = src.edge2.y;
            gpuLights[i].shapeParam1[2] = src.edge2.z;
            break;
        case LightShape::Disc:
        {
            const glm::vec3 n = safeNormalizeOr(src.discNormal, glm::vec3(0.0f, -1.0f, 0.0f));
            gpuLights[i].shapeParam0[0] = n.x;
            gpuLights[i].shapeParam0[1] = n.y;
            gpuLights[i].shapeParam0[2] = n.z;
            sourceRadius = std::max(src.sourceRadius, 0.0f);
            break;
        }
        case LightShape::Capsule:
            gpuLights[i].shapeParam0[0] = src.capsuleEndA.x;
            gpuLights[i].shapeParam0[1] = src.capsuleEndA.y;
            gpuLights[i].shapeParam0[2] = src.capsuleEndA.z;
            gpuLights[i].shapeParam1[0] = src.capsuleEndB.x;
            gpuLights[i].shapeParam1[1] = src.capsuleEndB.y;
            gpuLights[i].shapeParam1[2] = src.capsuleEndB.z;
            center = (src.capsuleEndA + src.capsuleEndB) * 0.5f;
            sourceRadius = std::max(src.sourceRadius, 0.0f);
            break;
        case LightShape::Sphere:
            sourceRadius = std::max(src.sourceRadius, 0.0f);
            break;
        case LightShape::Point:
        default:
            sourceRadius = 0.0f;
            break;
        }

        gpuLights[i].posRadius[0] = center.x;
        gpuLights[i].posRadius[1] = center.y;
        gpuLights[i].posRadius[2] = center.z;
        gpuLights[i].posRadius[3] = influence;

        gpuLights[i].colorIntensity[0] = color.x;
        gpuLights[i].colorIntensity[1] = color.y;
        gpuLights[i].colorIntensity[2] = color.z;
        gpuLights[i].colorIntensity[3] = std::max(src.intensity, 0.0f);

        gpuLights[i].shapeInfo[0] = static_cast<uint32_t>(src.shape);
        gpuLights[i].shapeInfo[1] = floatToBits(sourceRadius);
        gpuLights[i].shapeInfo[2] = src.castsShadows ? 1u : 0u;
        gpuLights[i].shapeInfo[3] = 0u;
    }
}

void App::updateCamera(float dt)
{
#if VOXEL_WITH_RUNTIME_UI
    if (window_ != nullptr && !input_.mouseLookActive())
    {
        double xpos = 0.0;
        double ypos = 0.0;
        glfwGetCursorPos(window_, &xpos, &ypos);
        refreshRuntimeUiPointerCapture(xpos, ypos);
    }
#endif

    inputController_.updateMouseCapture();
    if (inputRouter_.routePolledGameInput() != InputOwner::Game)
    {
        double xpos = 0.0;
        double ypos = 0.0;
        glfwGetCursorPos(window_, &xpos, &ypos);
        input_.syncMousePosition(xpos, ypos);
        (void)input_.consumeMouseDelta();
        return;
    }

    input_.updateFromWindow(window_);

#if VOXEL_WITH_RUNTIME_UI
    if (runtimeUiShowcaseCameraActive_)
    {
        (void)input_.consumeResetCamera();
        (void)input_.consumeMouseDelta();
        return;
    }
#endif

    if (input_.consumeResetCamera())
    {
        if (!voxelDebugSettings_.voxelFreezeCamera_ && !isFishFocusActive())
        {
            resetCamera();
            postFxSettings_.taaResetHistory_ = true;
            shadowSettings_.shadowResetHistory_ = true;
            shadowSettings_.localShadowResetHistory_ = true;
            aoSettings_.aoResetHistory_ = true;
        }
    }

    const Input::MouseDelta delta = input_.consumeMouseDelta();
    if (voxelDebugSettings_.voxelFreezeCamera_)
    {
        return;
    }
    if (isFishFocusActive())
    {
        // cinematic fish focus owns the camera pose; free-fly input stays consumed
        // above so no accumulated mouse delta jumps the camera on release.
        return;
    }
    if (input_.mouseLookActive())
    {
        camera_.yaw += delta.x * kMouseSensitivity;
        camera_.pitch += -delta.y * kMouseSensitivity;
        camera_.pitch = std::clamp(camera_.pitch, -kPitchLimit, kPitchLimit);
    }

    const float cosPitch = std::cos(camera_.pitch);
    const float sinPitch = std::sin(camera_.pitch);
    const float cosYaw = std::cos(camera_.yaw);
    const float sinYaw = std::sin(camera_.yaw);

    glm::vec3 forward{};
    forward.x = cosPitch * sinYaw;
    forward.y = sinPitch;
    forward.z = -cosPitch * cosYaw;
    forward = glm::normalize(forward);

    const glm::vec3 worldUp(0.0f, 1.0f, 0.0f);
    const glm::vec3 right = glm::normalize(glm::cross(forward, worldUp));
    const glm::vec3 up = glm::normalize(glm::cross(right, forward));

    glm::vec3 move(0.0f);
    if (input_.moveForward())
    {
        move += forward;
    }
    if (input_.moveBackward())
    {
        move -= forward;
    }
    if (input_.moveRight())
    {
        move += right;
    }
    if (input_.moveLeft())
    {
        move -= right;
    }
    if (input_.moveUp())
    {
        move += up;
    }
    if (input_.moveDown())
    {
        move -= up;
    }

    if (glm::length(move) > 0.0001f)
    {
        const float speed = kMoveSpeed * (input_.sprint() ? kSprintMultiplier : 1.0f);
        camera_.position += glm::normalize(move) * speed * dt;
    }
}

void App::detectCameraCut()
{
    const glm::vec3 currentPos = camera_.position;
    const glm::vec3 currentForward = cameraForward(camera_);

    if (taaFrameIndex_ == 0)
    {
        prevCameraPosition_ = currentPos;
        prevCameraForward_ = currentForward;
        return;
    }

    const glm::vec3 delta = currentPos - prevCameraPosition_;
    const float rotation = glm::dot(currentForward, prevCameraForward_);

    if (glm::length(delta) > cameraCutThreshold_ || rotation < 0.9f)
    {
        postFxSettings_.taaResetHistory_ = true;
        shadowSettings_.shadowResetHistory_ = true;
        shadowSettings_.localShadowResetHistory_ = true;
        aoSettings_.aoResetHistory_ = true;
    }

    prevCameraPosition_ = currentPos;
    prevCameraForward_ = currentForward;
}

void App::resetCamera()
{
    glm::vec3 presetPos{};
    float presetYaw = 0.0f;
    float presetPitch = 0.0f;
    if (tryGetPresetCamera(sceneConfig().name, presetPos, presetYaw, presetPitch))
    {
        camera_.position = presetPos;
        camera_.yaw = presetYaw;
        camera_.pitch = presetPitch;
        return;
    }

    camera_.position = sceneConfig().cameraPosition;
    camera_.yaw = sceneConfig().cameraYaw;
    camera_.pitch = sceneConfig().cameraPitch;
}

void App::refreshSceneCatalog(bool logResults)
{
    sceneManager_.refreshSceneCatalog(logResults);
}

bool App::loadSceneFromFile(const std::filesystem::path& path)
{
    engine::scene::SceneLoadResult sceneLoad =
        sceneManager_.loadSceneFromFile(path);
    if (!sceneLoad.loaded)
    {
        return false;
    }
    gameRuntime_.reconcileOfflineCare(sceneLoad.config.gameState);
    reloadScene(sceneLoad.config);
    return true;
}

void App::requestCloudBuildFromSceneConfig(const char* reason)
{
    if (!sceneConfig().loadCloudScene)
    {
        return;
    }

    const CloudSettings settings =
        engine::scene::SceneManager::buildCloudSettingsFromSceneConfig(
            sceneConfig(), camera_.position);
    const engine::scene::CloudBuildQueueResult result =
        sceneManager_.requestCloudBuild(settings, reason, jobSystem_,
                                        renderedFrameCount_, glfwGetTime());
    if (result.clearAutomationCommitObserved)
    {
        automationCloudCommitObserved_ = false;
    }
}

void App::invalidatePendingCloudBuild(const char* reason)
{
    sceneManager_.invalidatePendingCloudBuild(reason);
}

void App::consumePendingCloudBuild()
{
    const engine::scene::CloudBuildPollResult poll =
        sceneManager_.pollPendingCloudBuild(renderedFrameCount_,
                                            sceneConfig().loadCloudScene);
    if (poll.status != engine::scene::CloudBuildPollStatus::Ready ||
        !poll.pending)
    {
        return;
    }
    const std::shared_ptr<engine::scene::PendingCloudBuild> pending =
        poll.pending;

    if (!cloudPaletteUploaded_)
    {
        voxelPalette_.buildCloudPalette();
        if (!voxelPalette_.upload(ctx_))
        {
            logWarning("CloudScene", "Failed to upload cloud palette for deferred cloud commit.");
            sceneManager_.clearPendingCloudBuild();
            return;
        }
        cloudPaletteUploaded_ = true;
    }

    const auto commitStart = std::chrono::steady_clock::now();
    bool commitOk = false;
    const glm::ivec3 targetDims = pending->result.spec.dims;

    if (cloudVolumeIndex_ >= 0 &&
        static_cast<size_t>(cloudVolumeIndex_) < voxelWorld_.instances().size())
    {
        auto& inst = voxelWorld_.instances()[static_cast<size_t>(cloudVolumeIndex_)];
        if (inst.volume.dimensions() == targetDims)
        {
            commitOk =
                inst.volume.upload(ctx_, pending->result.voxels, voxelPalette_.buffer());
            if (commitOk)
            {
                voxelWorld_.setInstancePosition(static_cast<uint32_t>(cloudVolumeIndex_),
                                                pending->result.spec.position);
                voxelWorld_.setVolumeWrapOffset(static_cast<uint32_t>(cloudVolumeIndex_),
                                                glm::vec3(0.0f));
                voxelWorld_.setVolumeVisible(static_cast<uint32_t>(cloudVolumeIndex_), true);
            }
        }
        else
        {
            voxelWorld_.setVolumeVisible(static_cast<uint32_t>(cloudVolumeIndex_), false);
            const int newIndex =
                voxelWorld_.addVolume(ctx_, voxelPalette_, pending->result.spec,
                                      pending->result.voxels);
            if (newIndex >= 0)
            {
                cloudVolumeIndex_ = newIndex;
                commitOk = true;
            }
        }
    }
    else
    {
        const int newIndex =
            voxelWorld_.addVolume(ctx_, voxelPalette_, pending->result.spec,
                                  pending->result.voxels);
        if (newIndex >= 0)
        {
            cloudVolumeIndex_ = newIndex;
            commitOk = true;
        }
    }

    if (!commitOk)
    {
        if (cloudVolumeIndex_ >= 0 &&
            static_cast<size_t>(cloudVolumeIndex_) < voxelWorld_.instances().size())
        {
            voxelWorld_.setVolumeVisible(static_cast<uint32_t>(cloudVolumeIndex_), false);
        }
        logWarning("CloudScene",
                   makeLogMessage("Deferred cloud commit failed for version ", pending->version,
                                  "."));
        sceneManager_.clearPendingCloudBuild();
        return;
    }

    cloudBasePosition_ = pending->result.spec.position;
    cloudWrapRecenterOffset_ = glm::vec3(0.0f);
    automationCloudCommitObserved_ = true;
    postFxSettings_.taaResetHistory_ = true;
    shadowSettings_.shadowResetHistory_ = true;
    shadowSettings_.localShadowResetHistory_ = true;
    aoSettings_.aoResetHistory_ = true;

    const double gpuCommitMs =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - commitStart)
            .count();
    const double totalLatencyMs = (glfwGetTime() - pending->queuedAtSeconds) * 1000.0;
    logInfo("CloudScene",
            makeLogMessage("Deferred cloud commit complete. version=", pending->version,
                           " cpuMs=", pending->cpuBuildDurationMs, " gpuMs=", gpuCommitMs,
                           " totalMs=", totalLatencyMs, " filled=", pending->result.filledVoxelCount,
                           " position=(", pending->result.spec.position.x, ", ",
                           pending->result.spec.position.y, ", ",
                           pending->result.spec.position.z, ")"));
    sceneManager_.clearPendingCloudBuild();
}

void App::resetCloudRuntimeState()
{
    cloudVolumeIndex_ = -1;
    cloudBasePosition_ = glm::vec3(0.0f);
    cloudWrapRecenterOffset_ = glm::vec3(0.0f);
    cloudPaletteUploaded_ = false;
}

#if VOXEL_WITH_EDITOR
void App::drawDebugUI()
{
    ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(400, 600), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Rendering Pipeline Debug", nullptr))
    {
        ImGui::End();
        return;
    }

    ImGui::Text("Deferred Rendering Pipeline");
    ImGui::Separator();

    DebugInfoPanel::DrawTemporalStatus({
        taaFrameIndex_,
        renderPasses().depthHistory.index,
        isDdaShadowsAvailable(),
        shadowSettings_.useDDAShadows_,
        isAmbientOcclusionAvailable(),
        aoSettings_.aoEnabled_,
        shadowSettings_.shadowResetHistory_,
        aoSettings_.aoResetHistory_,
    });

    // === scene configuration ===
    const DebugInfoPanel::SceneConfigurationRequests sceneConfigRequests =
        DebugInfoPanel::DrawSceneConfiguration(sceneConfig(),
                                               sceneManager_.currentScenePath(),
                                               sceneManager_.availableScenes());
    if (sceneConfigRequests.refreshCatalog)
    {
        refreshSceneCatalog();
    }
    if (sceneConfigRequests.loadScenePath)
    {
        loadSceneFromFile(*sceneConfigRequests.loadScenePath);
    }
    if (sceneConfigRequests.reloadConfig)
    {
        reloadScene(*sceneConfigRequests.reloadConfig);
    }
    if (sceneConfigRequests.saveCurrentScene)
    {
        sceneConfigMutable().cameraPosition = camera_.position;
        sceneConfigMutable().cameraYaw = camera_.yaw;
        sceneConfigMutable().cameraPitch = camera_.pitch;
        sceneConfigMutable().worldSeed = worldSeed_;
        sceneConfigMutable().presentationProfile =
            engine::scene::captureScenePresentationProfile(
                sceneConfig().name, skyPreset_, skyColor_, lightingSettings_,
                shadowSettings_, aoSettings_, postFxSettings_, voxelDebugSettings_,
                waterSettings_, glassSettings_);
        gameRuntime_.stampLastPlayedUtc(sceneConfigMutable().gameState);
        const std::filesystem::path lastUsedPath =
            sceneManager_.scenesRoot() / "last_used.json";
        std::string saveError;
        if (saveSceneConfigToFile(lastUsedPath, sceneConfig(), &saveError,
                                  SceneSerializerLogMode::Quiet))
        {
            gameRuntime_.markPlaceableSceneSaved();
            logInfo("Scene", std::string("Saved current scene state to ") +
                                 lastUsedPath.string());
        }
        else
        {
            logWarning("Scene", std::string("Failed to save current scene state to ") +
                                   lastUsedPath.string() +
                                   (saveError.empty() ? std::string()
                                                      : " (" + saveError + ")"));
        }
    }

    ImGui::Separator();

    if (const std::optional<int> selectedViewMode =
            DebugInfoPanel::DrawViewMode(input_.gbufferMode()))
    {
        input_.handleKey(GLFW_KEY_0 + *selectedViewMode, GLFW_PRESS);
    }

    ImGui::Separator();

    bool gpuProfilerEnabled = diagnosticsSettings_.gpuProfilerEnabled_;
    if (DebugInfoPanel::DrawGpuProfiler(gpuProfiler_, gpuProfilerEnabled))
    {
        setGpuProfilerEnabled(gpuProfilerEnabled);
    }
    DebugInfoPanel::DrawDay16Checklist(diagnosticsSettings_);
    DebugInfoPanel::DrawRenderingPipelinePasses();

    ImGui::Separator();

    DebugInfoPanel::PixelInspectCapture pixelInspectCapture{};
    if (glfwGetMouseButton(window_, GLFW_MOUSE_BUTTON_MIDDLE) == GLFW_PRESS)
    {
        double xpos = 0.0;
        double ypos = 0.0;
        glfwGetCursorPos(window_, &xpos, &ypos);
        pixelInspectCapture.middleMousePressed = true;
        pixelInspectCapture.cursor = glm::ivec2(static_cast<int>(xpos), static_cast<int>(ypos));
    }
    const DebugInfoPanel::LightingShadowRequests lightingShadowRequests =
        DebugInfoPanel::DrawLightingAndShadows(
            lightingSettings_,
            shadowSettings_,
            diagnosticsSettings_,
            {input_.lightsEnabled(),
             input_.lightCount(),
             input_.heatmap(),
             input_.cascadeDebug(),
             isDdaShadowsAvailable(),
             renderPasses().taa.isEnabled(),
             {pixelInspectCapture,
              lastInspectVoxelFrac_,
              lastInspectHitAxis_,
              lastInspectVolumeIndex_,
              lastInspectVoxel_,
              inspectNormalDelta_,
              inspectNdotlDelta_,
              inspectAxisChanged_}});
    if (lightingShadowRequests.toggleLights)
    {
        input_.handleKey(GLFW_KEY_L, GLFW_PRESS);
    }
    if (lightingShadowRequests.lightCount)
    {
        input_.setLightCount(*lightingShadowRequests.lightCount);
    }
    if (lightingShadowRequests.toggleHeatmap)
    {
        input_.handleKey(GLFW_KEY_H, GLFW_PRESS);
    }
    if (lightingShadowRequests.toggleCascadeDebug)
    {
        input_.handleKey(GLFW_KEY_C, GLFW_PRESS);
    }

    DebugInfoPanel::DrawAmbientOcclusion(aoSettings_, isAmbientOcclusionAvailable());

    ImGui::Separator();

    const DebugInfoPanel::PostProcessingRequests postRequests =
        DebugInfoPanel::DrawPostProcessing(postFxSettings_,
                                           {input_.tonemapEnabled(),
                                            input_.exposure(),
                                            input_.bloomEnabled(),
                                            input_.postDebugMode()});
    if (postRequests.toggleTonemap)
    {
        input_.handleKey(GLFW_KEY_T, GLFW_PRESS);
    }
    if (postRequests.exposure)
    {
        input_.setExposure(*postRequests.exposure);
    }
    if (postRequests.bloomEnabled)
    {
        input_.setBloomEnabled(*postRequests.bloomEnabled);
    }
    if (postRequests.postDebugMode)
    {
        input_.setPostDebugMode(*postRequests.postDebugMode);
    }

    ImGui::Separator();

    const DebugInfoPanel::TaaRequests taaRequests =
        DebugInfoPanel::DrawTaa(postFxSettings_, voxelDebugSettings_.voxelDisableJitter_);
    if (taaRequests.taaEnabledChanged)
    {
        renderPasses().taa.setEnabled(postFxSettings_.taaEnabled_);
    }
    if (taaRequests.jitterChanged)
    {
        shadowSettings_.shadowResetHistory_ = true;
        shadowSettings_.localShadowResetHistory_ = true;
        aoSettings_.aoResetHistory_ = true;
    }

    ImGui::Separator();

    const DebugInfoPanel::WaterPanelRequests waterRequests = DebugInfoPanel::DrawWater(
        waterSettings_, waterVolumeMgr_,
        {input_.waterEnabled(), input_.waterDebugMode(),
         isSunroofAquariumScene(sceneConfig())});
    if (waterRequests.waterEnabled)
    {
        input_.setWaterEnabled(*waterRequests.waterEnabled);
    }
    if (waterRequests.useWaterV2)
    {
        setUseWaterV2(*waterRequests.useWaterV2);
    }
    if (waterRequests.markParametersDirty)
    {
        markWaterParametersDirty();
    }
    if (waterRequests.waterDebugMode)
    {
        input_.setWaterDebugMode(*waterRequests.waterDebugMode);
    }
    if (waterRequests.createDefaultVolume)
    {
        waterVolumeMgr_.setVolumes(
            {makeSceneWaterVolume(sceneConfig(), voxelWorld_, waterSettings_.waterLevel_)});
    }

    ImGui::Separator();

    const DebugInfoPanel::GlassRequests glassRequests =
        DebugInfoPanel::DrawGlass(glassSettings_,
                                  {input_.glassEnabled(),
                                   input_.glassDebugMode(),
                                   kRoundFishbowlGlassPreset.name});
    if (glassRequests.enabled)
    {
        input_.setGlassEnabled(*glassRequests.enabled);
    }
    if (glassRequests.applyPreset)
    {
        applyGlassMaterialPreset(glassSettings_, kRoundFishbowlGlassPreset);
    }
    if (glassRequests.debugMode)
    {
        input_.setGlassDebugMode(*glassRequests.debugMode);
    }

    ImGui::Separator();

    // === voxel world controls ===
    std::vector<DebugInfoPanel::DdaMetricsSamplePoint> metricsHistoryPoints;
    if (diagnosticsSettings_.voxelMetricsEnabled_)
    {
        metricsHistoryPoints.reserve(ddaMetricsHistory_.size());
        for (const DdaMetricsSample& sample : ddaMetricsHistory_)
        {
            metricsHistoryPoints.push_back({sample.hitRate, sample.avgIterations,
                                            sample.avgSkipJumps, sample.sampleCount});
        }
    }
    const DebugInfoPanel::VoxelWorldPanelRequests voxelWorldRequests =
        DebugInfoPanel::DrawVoxelWorld(
            voxelDebugSettings_, diagnosticsSettings_, voxelWorld_, cachedDdaMetrics_,
            metricsHistoryPoints,
            {input_.voxelWorldEnabled(), input_.chunkBoundsEnabled(),
             input_.volumeBoundsEnabled(), input_.voxelMeshingFrozen(),
             input_.editModeEnabled(),
             sceneConfig().loadAquariumTest && placeablePreviewEnabled_,
             kMaxProceduralFishCount, sceneConfig().loadOBBVolumes,
             sceneConfig().loadOBBVolumes || sceneConfig().loadAquariumTest ||
                 sceneConfig().loadProceduralWorld || sceneConfig().loadVoxelImport,
             voxelMetricsSamplePattern_, voxelMetricsSampleOffset_,
             voxelMetricsWriteJson_});
    if (voxelWorldRequests.toggleVoxelWorld)
    {
        input_.handleKey(GLFW_KEY_F1, GLFW_PRESS);
    }
    if (voxelWorldRequests.proceduralFishEnabled)
    {
        setProceduralFishEnabled(*voxelWorldRequests.proceduralFishEnabled);
    }
    if (voxelWorldRequests.proceduralFishCount)
    {
        setProceduralFishCount(*voxelWorldRequests.proceduralFishCount);
    }
    if (voxelWorldRequests.regenerateWorld)
    {
        input_.handleKey(GLFW_KEY_F2, GLFW_PRESS);
    }
    if (voxelWorldRequests.toggleChunkBounds)
    {
        input_.handleKey(GLFW_KEY_F3, GLFW_PRESS);
    }
    if (voxelWorldRequests.toggleVolumeBounds)
    {
        input_.handleKey(GLFW_KEY_F12, GLFW_PRESS);
    }
    if (voxelWorldRequests.toggleMeshingFrozen)
    {
        input_.handleKey(GLFW_KEY_F4, GLFW_PRESS);
    }
    if (voxelWorldRequests.toggleEditMode)
    {
        input_.handleKey(GLFW_KEY_F5, GLFW_PRESS);
    }
    if (voxelWorldRequests.resetTemporalHistory)
    {
        postFxSettings_.taaResetHistory_ = true;
        shadowSettings_.shadowResetHistory_ = true;
        shadowSettings_.localShadowResetHistory_ = true;
        aoSettings_.aoResetHistory_ = true;
    }
    if (voxelWorldRequests.metricsSamplePattern)
    {
        voxelMetricsSamplePattern_ = *voxelWorldRequests.metricsSamplePattern;
    }
    if (voxelWorldRequests.metricsWriteJson)
    {
        voxelMetricsWriteJson_ = *voxelWorldRequests.metricsWriteJson;
    }
    if (voxelWorldRequests.dumpMetricsCapture)
    {
        voxelMetricsCapturePending_ = true;
    }

    // === procedural world panel ===
    if (sceneConfig().loadProceduralWorld && !isNaturePondScene(sceneConfig()))
    {
        ImGui::Separator();
        const DebugInfoPanel::ProceduralWorldPanelRequests proceduralRequests =
            DebugInfoPanel::DrawProceduralWorld(proceduralWorldSettings_, voxelWorld_);
        if (proceduralRequests.reseed)
        {
            proceduralWorldSettings_.proceduralSeed_ =
                hashU32(proceduralWorldSettings_.proceduralSeed_ + 1u);
            worldSeed_ = proceduralWorldSettings_.proceduralSeed_;
            proceduralWorldSettings_.proceduralDirty_ = true;
        }
        if (proceduralRequests.rebuild)
        {
            rebuildProceduralWorld();
        }
    }

    // === voxel import panel ===
    if (sceneConfig().loadVoxelImport)
    {
        ImGui::Separator();
        const DebugInfoPanel::VoxelImportPanelRequests voxelImportRequests =
            DebugInfoPanel::DrawVoxelImport(
                voxelWorld_,
                {voxelImportMeshPath_, voxelImportResolution_, voxelImportSplitChunks_,
                 voxelImportDirty_, voxelImportTriangleCount_, voxelImportFilledCount_,
                 voxelImportVoxelizeMs_});
        if (voxelImportRequests.refreshMeshList)
        {
            voxelImportMeshPath_ =
                findFirstFileWithExtension(assetRoot_ / "meshes", ".obj");
            voxelImportDirty_ = true;
        }
        if (voxelImportRequests.resolution)
        {
            voxelImportResolution_ = *voxelImportRequests.resolution;
            voxelImportDirty_ = true;
        }
        if (voxelImportRequests.splitChunks)
        {
            voxelImportSplitChunks_ = *voxelImportRequests.splitChunks;
            voxelImportDirty_ = true;
        }
        if (voxelImportRequests.voxelize)
        {
            rebuildVolumeScene();
        }
    }

    // === obb volumes panel ===
    if (sceneConfig().loadOBBVolumes && !voxelWorld_.instances().empty())
    {
        ImGui::Separator();
        DebugInfoPanel::DrawObbVolumes(voxelWorld_);
    }

    // === aquarium volumes panel ===
    if (sceneConfig().loadAquariumTest && !voxelWorld_.instances().empty())
    {
        ImGui::Separator();
        PlaceableInstance hoveredPlaceable{};
        const bool canRemovePreview = findPlaceableAtPreview(hoveredPlaceable);
        const DebugInfoPanel::AquariumVolumesPanelRequests aquariumRequests =
            DebugInfoPanel::DrawAquariumVolumes(
                voxelWorld_, placeablePreview_, placeablePreviewEnabled_,
                selectedFoliagePreviewPrototype_, kFoliagePreviewSpeciesLabels,
                kFoliagePreviewSpeciesCount,
                {input_.editModeEnabled(), sceneConfig().placeables.size(),
                 sceneConfig().useDefaultPlaceables, canRemovePreview,
                 hoveredPlaceable.prototypeSlug, gameRuntime_.hasPlaceableUndo(),
                 gameRuntime_.placeableSceneDirty(),
                 gameRuntime_.placeableEditStatus()});
        if (aquariumRequests.placePreview)
        {
            commitPlaceablePreview();
        }
        if (aquariumRequests.removeUnderCursor)
        {
            removePlaceableAtPreview();
        }
        if (aquariumRequests.undoPlaceable)
        {
            undoLastPlaceableEdit();
        }
        if (aquariumRequests.rebuildAquarium)
        {
            waitForInFlightFrameWork("aquarium rebuild");
            voxelWorld_.clearScene(ctx_);
            const bool includeVoxelWater =
                shouldIncludeAquariumVoxelWater(sceneConfig(), automationDisableAquariumWater_);
            if (!AquariumScene::init(ctx_, voxelWorld_, voxelPalette_,
                                     sceneConfig().useMeshTankGlass,
                                     aquariumLayoutForScene(sceneConfig()),
                                     includeVoxelWater, &sceneConfig().placeables,
                                     sceneConfig().useDefaultPlaceables))
            {
                animatedObjects_.clear();
                proceduralFish_.clear();
                heroFoliage_.clear();
                proceduralFishPoseValid_ = false;
                animatedObjectsDirty_ = true;
                if (waterVolumeMgr_.buffer() != VK_NULL_HANDLE)
                {
                    waterVolumeMgr_.setVolumes({});
                }
                sceneObjectsDirty_ = true;
                voxelObjectsDirty_ = true;
                postFxSettings_.taaResetHistory_ = true;
                shadowSettings_.shadowResetHistory_ = true;
                shadowSettings_.localShadowResetHistory_ = true;
                aoSettings_.aoResetHistory_ = true;
                logError("Scene", "Failed to rebuild aquarium scene.");
            }
            else
            {
                waterVolumeMgr_.setVolumes(
                    {makeSceneWaterVolume(sceneConfig(), voxelWorld_, waterSettings_.waterLevel_)});
                rebuildFishbowlGlassMesh();
                rebuildModularGlassMeshes();
                rebuildGlassObjectsForCurrentScene();
                rebuildAnimatedObjects();
                sceneObjectsDirty_ = true;
                voxelObjectsDirty_ = true;
                postFxSettings_.taaResetHistory_ = true;
                shadowSettings_.shadowResetHistory_ = true;
                shadowSettings_.localShadowResetHistory_ = true;
                aoSettings_.aoResetHistory_ = true;
                logInfo("Scene", "Rebuilt aquarium scene.");
            }
        }
    }

    ImGui::Separator();

    if (DebugInfoPanel::DrawCamera({camera_.position, camera_.yaw, camera_.pitch}))
    {
        input_.handleKey(GLFW_KEY_R, GLFW_PRESS);
    }

    ImGui::Separator();

    if (const std::optional<SwapPresentMode> requestedPresentMode =
            DebugInfoPanel::DrawPerformanceStats(
                presentMode_,
                framePacingSettings_,
                {focusedIdleThrottleActive_, backgroundThrottleActive_}))
    {
        setPresentMode(*requestedPresentMode);
    }

    ImGui::End();
}
#endif

void App::dumpDdaMetricsCapture(float timeSeconds)
{
    const std::filesystem::path captureDir =
        std::filesystem::current_path() / "out" / "captures";
    std::error_code ec;
    std::filesystem::create_directories(captureDir, ec);
    if (ec)
    {
        logWarning("Metrics", std::string("Failed to create capture directory: ") +
                                  captureDir.string());
        return;
    }

    const std::string timestampFile = formatTimestamp("%Y%m%d_%H%M%S");
    const std::string timestampCsv = formatTimestamp("%Y-%m-%d %H:%M:%S");

    const uint32_t stride = static_cast<uint32_t>(std::max(1, diagnosticsSettings_.voxelMetricsSampleStride_));
    const uint32_t totalHitMiss = cachedDdaMetrics_.hitCount + cachedDdaMetrics_.missCount;
    const float sampleCount = static_cast<float>(cachedDdaMetrics_.sampleCount);
    const float avgIters =
        sampleCount > 0.0f ? static_cast<float>(cachedDdaMetrics_.sumIters) / sampleCount : 0.0f;
    const float avgSkipJumps =
        sampleCount > 0.0f ? static_cast<float>(cachedDdaMetrics_.sumSkipJumps) / sampleCount : 0.0f;
    const float hitRate =
        totalHitMiss > 0 ? static_cast<float>(cachedDdaMetrics_.hitCount) /
                               static_cast<float>(totalHitMiss)
                         : 0.0f;

    const glm::vec3 forward = cameraForward(camera_);
    const VkExtent2D extent = renderer_.swapchainExtent();
    const char* patternLabel = samplePatternLabel(voxelMetricsSamplePattern_);

    const std::filesystem::path csvPath = captureDir / "metrics.csv";
    const bool csvExists = std::filesystem::exists(csvPath);
    std::ofstream csv(csvPath, std::ios::app);
    if (!csv)
    {
        logWarning("Metrics", std::string("Failed to open metrics CSV: ") + csvPath.string());
        return;
    }
    if (!csvExists)
    {
        csv << "timestamp,frameIndex,resX,resY,stride,skipMip,skipEnabled,heatmapMode,samplePattern,"
               "sampleOffsetX,sampleOffsetY,samples,hits,misses,hitRate,sumIters,avgIters,"
               "sumSkipJumps,avgSkipJumps,cameraPosX,cameraPosY,cameraPosZ,cameraFwdX,cameraFwdY,"
               "cameraFwdZ\n";
    }

    csv << timestampCsv << "," << voxelMetricsFrameIndex_ << "," << extent.width << ","
        << extent.height << "," << stride << "," << voxelDebugSettings_.voxelDdaSkipMip_ << ","
        << (voxelDebugSettings_.voxelDdaSkipEnabled_ ? 1 : 0) << "," << voxelDebugSettings_.voxelHeatmapMode_ << "," << patternLabel << ","
        << voxelMetricsSampleOffset_.x << "," << voxelMetricsSampleOffset_.y << ","
        << cachedDdaMetrics_.sampleCount << "," << cachedDdaMetrics_.hitCount << ","
        << cachedDdaMetrics_.missCount << "," << hitRate << "," << cachedDdaMetrics_.sumIters
        << "," << avgIters << "," << cachedDdaMetrics_.sumSkipJumps << "," << avgSkipJumps << ","
        << camera_.position.x << "," << camera_.position.y << "," << camera_.position.z << ","
        << forward.x << "," << forward.y << "," << forward.z << "\n";

    if (!voxelMetricsWriteJson_)
    {
        logInfo("Metrics", std::string("Metrics capture appended: ") + csvPath.string());
        return;
    }

    const std::string jsonName =
        std::string("capture_") + timestampFile + "_f" + std::to_string(voxelMetricsFrameIndex_) +
        ".json";
    const std::filesystem::path jsonPath = captureDir / jsonName;
    std::ofstream json(jsonPath, std::ios::trunc);
    if (!json)
    {
        logWarning("Metrics", std::string("Failed to write capture JSON: ") + jsonPath.string());
        return;
    }

    json << "{\n";
    json << "  \"timestamp\": \"" << timestampCsv << "\",\n";
    json << "  \"frameIndex\": " << voxelMetricsFrameIndex_ << ",\n";
    json << "  \"scene\": \"" << jsonEscape(sceneConfig().name) << "\",\n";
    json << "  \"resolution\": [" << extent.width << ", " << extent.height << "],\n";
    json << "  \"camera\": {\n";
    json << "    \"position\": [" << camera_.position.x << ", " << camera_.position.y << ", "
         << camera_.position.z << "],\n";
    json << "    \"forward\": [" << forward.x << ", " << forward.y << ", " << forward.z << "]\n";
    json << "  },\n";
    json << "  \"settings\": {\n";
    json << "    \"stride\": " << stride << ",\n";
    json << "    \"samplePattern\": \"" << patternLabel << "\",\n";
    json << "    \"sampleOffset\": [" << voxelMetricsSampleOffset_.x << ", "
         << voxelMetricsSampleOffset_.y << "],\n";
    json << "    \"skipEnabled\": " << (voxelDebugSettings_.voxelDdaSkipEnabled_ ? "true" : "false") << ",\n";
    json << "    \"skipMip\": " << voxelDebugSettings_.voxelDdaSkipMip_ << ",\n";
    json << "    \"heatmapMode\": " << voxelDebugSettings_.voxelHeatmapMode_ << ",\n";
    json << "    \"heatmapMax\": " << voxelDebugSettings_.voxelHeatmapMax_ << ",\n";
    json << "    \"heatmapGamma\": " << voxelDebugSettings_.voxelHeatmapGamma_ << ",\n";
    json << "    \"timeSeconds\": " << timeSeconds << "\n";
    json << "  },\n";
    json << "  \"freeze\": {\n";
    json << "    \"freezeDebug\": " << (voxelDebugSettings_.voxelFreezeDebug_ ? "true" : "false") << ",\n";
    json << "    \"freezeCamera\": " << (voxelDebugSettings_.voxelFreezeCamera_ ? "true" : "false") << ",\n";
    json << "    \"freezeTime\": " << (voxelDebugSettings_.voxelFreezeTime_ ? "true" : "false") << ",\n";
    json << "    \"disableJitter\": " << (voxelDebugSettings_.voxelDisableJitter_ ? "true" : "false") << "\n";
    json << "  },\n";
    json << "  \"metrics\": {\n";
    json << "    \"samples\": " << cachedDdaMetrics_.sampleCount << ",\n";
    json << "    \"hits\": " << cachedDdaMetrics_.hitCount << ",\n";
    json << "    \"misses\": " << cachedDdaMetrics_.missCount << ",\n";
    json << "    \"hitRate\": " << hitRate << ",\n";
    json << "    \"sumIterations\": " << cachedDdaMetrics_.sumIters << ",\n";
    json << "    \"avgIterations\": " << avgIters << ",\n";
    json << "    \"sumSkipJumps\": " << cachedDdaMetrics_.sumSkipJumps << ",\n";
    json << "    \"avgSkipJumps\": " << avgSkipJumps << "\n";
    json << "  }\n";
    json << "}\n";

    logInfo("Metrics", std::string("Metrics capture written: ") + csvPath.string() + " + " +
                           jsonPath.string());
}
