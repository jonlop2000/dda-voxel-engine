#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include "engine/render/RenderSettings.h"
#include "engine/render/Swapchain.h"
#include "engine/scene/SceneCatalog.h"
#include "engine/scene/SceneConfig.h"

class GpuProfiler;
namespace engine
{
struct DDAMetrics;
struct ProceduralWorldSettings;
class VoxelWorld;
class WaterVolumeManager;
}

namespace DebugInfoPanel
{

// scene configuration is a command/result boundary: the drawer only reads
// state and records intents; app applies them (catalog refresh, scene
// load/reload, camera-snapshot save) right after the call so later panels
// see the same post-apply state as before the extraction.
struct SceneConfigurationRequests
{
    bool refreshCatalog = false;
    std::optional<std::filesystem::path> loadScenePath;
    std::optional<SceneConfig> reloadConfig;
    bool saveCurrentScene = false;
};

struct TemporalStatusInfo
{
    uint32_t frameIndex = 0;
    int depthHistoryIndex = 0;
    bool ddaShadowsAvailable = false;
    bool ddaShadowsEnabled = false;
    bool ambientOcclusionAvailable = false;
    bool ambientOcclusionEnabled = false;
    bool shadowResetPending = false;
    bool ambientOcclusionResetPending = false;
};

struct CameraStatus
{
    glm::vec3 position{0.0f};
    float yaw = 0.0f;
    float pitch = 0.0f;
};

struct PerformanceStatus
{
    bool focusedIdleThrottleActive = false;
    bool backgroundThrottleActive = false;
};

struct PostProcessingState
{
    bool tonemapEnabled = false;
    float exposure = 1.0f;
    bool bloomEnabled = false;
    int postDebugMode = 0;
};

struct PostProcessingRequests
{
    bool toggleTonemap = false;
    std::optional<float> exposure;
    std::optional<bool> bloomEnabled;
    std::optional<int> postDebugMode;
};

struct TaaRequests
{
    bool taaEnabledChanged = false;
    bool jitterChanged = false;
};

struct GlassState
{
    bool enabled = false;
    int debugMode = 0;
    const char* presetName = nullptr;
};

struct GlassRequests
{
    std::optional<bool> enabled;
    bool applyPreset = false;
    std::optional<int> debugMode;
};

// water panel: settings-bucket fields and water-volume data are edited
// directly (both are engine-owned plain data); everything with app-side
// effects (input-owned toggle, V2 path switch + rebuild, parameter-dirty
// flag, default-volume creation) is returned as a request.
struct WaterPanelState
{
    bool waterEnabled = false;
    int waterDebugMode = 0;
    bool isSunroofScene = false;
};

struct WaterPanelRequests
{
    std::optional<bool> waterEnabled;
    std::optional<bool> useWaterV2;
    bool markParametersDirty = false;
    std::optional<int> waterDebugMode;
    bool createDefaultVolume = false;
};

struct PixelInspectCapture
{
    bool middleMousePressed = false;
    glm::ivec2 cursor{0, 0};
};

struct PixelInspectState
{
    PixelInspectCapture capture{};
    glm::vec3 voxelFrac{0.0f};
    int hitAxis = 0;
    int volumeIndex = -1;
    glm::ivec3 voxel{0, 0, 0};
    float normalDelta = 0.0f;
    float ndotlDelta = 0.0f;
    bool axisChanged = false;
};

// voxel world panel: settings-bucket fields and VoxelWorld instance
// visibility/position are edited directly; input-owned toggles (f-keys),
// fish roster changes, temporal-history resets, and metrics capture state
// are returned as requests.
struct DdaMetricsSamplePoint
{
    float hitRate = 0.0f;
    float avgIterations = 0.0f;
    float avgSkipJumps = 0.0f;
    float sampleCount = 0.0f;
};

struct VoxelWorldPanelState
{
    bool voxelWorldEnabled = false;
    bool chunkBoundsEnabled = false;
    bool volumeBoundsEnabled = false;
    bool meshingFrozen = false;
    bool editModeEnabled = false;
    bool placeableEditHints = false;
    int maxFishCount = 0;
    bool showVolumeBSlider = false;
    bool showSkipControls = false;
    int metricsSamplePattern = 0;
    glm::ivec2 metricsSampleOffset{0};
    bool metricsWriteJson = false;
};

struct VoxelWorldPanelRequests
{
    bool toggleVoxelWorld = false;
    std::optional<bool> proceduralFishEnabled;
    std::optional<int> proceduralFishCount;
    bool regenerateWorld = false;
    bool toggleChunkBounds = false;
    bool toggleVolumeBounds = false;
    bool toggleMeshingFrozen = false;
    bool toggleEditMode = false;
    bool resetTemporalHistory = false;
    std::optional<int> metricsSamplePattern;
    std::optional<bool> metricsWriteJson;
    bool dumpMetricsCapture = false;
};

// procedural world panel: worldgen settings (including the dirty flag and
// range clamps) are edited directly on the bucket; reseeding (app-owned hash
// + world-seed sync) and the rebuild are requests.
struct ProceduralWorldPanelRequests
{
    bool reseed = false;
    bool rebuild = false;
};

// voxel import panel: import parameters live in app internals, so they arrive
// as state and change as requests; volume visibility edits VoxelWorld directly.
struct VoxelImportPanelState
{
    std::filesystem::path meshPath;
    int resolution = 64;
    bool splitChunks = false;
    bool dirty = false;
    uint32_t triangleCount = 0;
    uint32_t filledCount = 0;
    double voxelizeMs = 0.0;
};

struct VoxelImportPanelRequests
{
    bool refreshMeshList = false;
    std::optional<int> resolution;
    std::optional<bool> splitChunks;
    bool voxelize = false;
};

// aquarium volumes panel: volume visibility and the preview-enable/species
// selections are edited directly (engine data / ui-owned selections); the
// placeable edit commands and the aquarium rebuild are requests because they
// mutate scene, voxel, and gpu state app-side.
struct AquariumVolumesPanelState
{
    bool editModeEnabled = false;
    size_t savedPlaceableCount = 0;
    bool useDefaultPlaceables = false;
    bool canRemovePreview = false;
    std::string hoveredPrototypeSlug;
    bool hasPlaceableUndo = false;
    bool placeableSceneDirty = false;
    std::string placeableEditStatus;
};

struct AquariumVolumesPanelRequests
{
    bool placePreview = false;
    bool removeUnderCursor = false;
    bool undoPlaceable = false;
    bool rebuildAquarium = false;
};

struct LightingShadowState
{
    bool lightsEnabled = false;
    int lightCount = 0;
    bool heatmap = false;
    bool cascadeDebug = false;
    bool ddaShadowsAvailable = false;
    bool taaEnabled = false;
    PixelInspectState pixelInspect{};
};

struct LightingShadowRequests
{
    bool toggleLights = false;
    std::optional<int> lightCount;
    bool toggleHeatmap = false;
    bool toggleCascadeDebug = false;
};

SceneConfigurationRequests DrawSceneConfiguration(
    const SceneConfig& config,
    const std::filesystem::path& currentScenePath,
    const std::vector<SceneCatalogEntry>& availableScenes);
void DrawTemporalStatus(const TemporalStatusInfo& status);
std::optional<int> DrawViewMode(int currentMode);
bool DrawGpuProfiler(GpuProfiler& profiler, bool& enabled);
void DrawDay16Checklist(engine::render::DiagnosticsSettings& diagnostics);
void DrawRenderingPipelinePasses();
LightingShadowRequests DrawLightingAndShadows(
    engine::render::LightingSettings& lighting,
    engine::render::ShadowSettings& shadow,
    engine::render::DiagnosticsSettings& diagnostics,
    const LightingShadowState& state);
void DrawAmbientOcclusion(engine::render::AmbientOcclusionSettings& ao,
                          bool available);
PostProcessingRequests DrawPostProcessing(
    engine::render::PostFxSettings& postFx,
    const PostProcessingState& state);
TaaRequests DrawTaa(engine::render::PostFxSettings& postFx,
                    bool jitterSuppressedByDebug);
GlassRequests DrawGlass(engine::render::GlassSettings& glass,
                        const GlassState& state);
WaterPanelRequests DrawWater(engine::render::WaterSettings& water,
                             engine::WaterVolumeManager& volumes,
                             const WaterPanelState& state);
std::optional<int> DrawWaterDebugMode(int currentMode);
void DrawDdaDebugModes(engine::render::VoxelDebugSettings& voxelDebug);
VoxelWorldPanelRequests DrawVoxelWorld(
    engine::render::VoxelDebugSettings& voxelDebug,
    engine::render::DiagnosticsSettings& diagnostics,
    engine::VoxelWorld& world,
    const engine::DDAMetrics& metrics,
    const std::vector<DdaMetricsSamplePoint>& metricsHistory,
    const VoxelWorldPanelState& state);
void DrawObbVolumes(engine::VoxelWorld& world);
ProceduralWorldPanelRequests DrawProceduralWorld(
    engine::ProceduralWorldSettings& settings, engine::VoxelWorld& world);
VoxelImportPanelRequests DrawVoxelImport(engine::VoxelWorld& world,
                                         const VoxelImportPanelState& state);
AquariumVolumesPanelRequests DrawAquariumVolumes(
    engine::VoxelWorld& world, const PlaceablePreview& preview, bool& previewEnabled,
    int& selectedSpecies, const char* const* speciesLabels, int speciesCount,
    const AquariumVolumesPanelState& state);
bool DrawCamera(const CameraStatus& status);
std::optional<SwapPresentMode> DrawPerformanceStats(
    SwapPresentMode presentMode,
    engine::render::FramePacingSettings& framePacing,
    const PerformanceStatus& status);

} // namespace DebugInfoPanel
