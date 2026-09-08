#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/mat4x4.hpp>

#include "engine/scene/SceneCatalog.h"
#include "engine/scene/SceneConfig.h"
#include "engine/scene/ScenePresentationProfileVariants.h"
#include "UI/EditorState.h"
#include "engine/game/FishTypes.h"
#include "engine/game/Placeables.h"
#include "engine/render/RenderSettings.h"
#include "engine/render/RenderQualityPreset.h"

class App;
struct Camera;
class GpuProfiler;
enum class SwapPresentMode;
namespace engine
{
struct DDAMetrics;
class WaterVolumeManager;
}

// the editor's only doorway into the engine. the facade depends on the
// per-domain settings structs and public App/subsystem apis, never App
// internals: App is an opaque forward declaration here, and every delegation
// body lives in EngineFacade.cpp. reads of shared world/config state go
// through the decision #6 WorldStateView; writes go through named App apis or
// the mutable settings buckets.
class EngineFacade
{
public:
    using EditableAreaLight = engine::render::EditableAreaLight;
    using LightingSettings = engine::render::LightingSettings;
    using ShadowSettings = engine::render::ShadowSettings;
    using AmbientOcclusionSettings = engine::render::AmbientOcclusionSettings;
    using PostFxSettings = engine::render::PostFxSettings;
    using WaterSettings = engine::render::WaterSettings;
    using GlassSettings = engine::render::GlassSettings;
    using VoxelDebugSettings = engine::render::VoxelDebugSettings;
    using DiagnosticsSettings = engine::render::DiagnosticsSettings;
    using FramePacingSettings = engine::render::FramePacingSettings;
    using FishHandle = engine::game::FishHandle;

    struct RenderingPanelAccess
    {
        using EditableAreaLight = engine::render::EditableAreaLight;

        EditorState& editorState;
        LightingSettings& lighting;
        ShadowSettings& shadows;
        AmbientOcclusionSettings& ao;
        PostFxSettings& postFx;
        WaterSettings& water;
        GlassSettings& glass;
        VoxelDebugSettings& voxelDebug;
        DiagnosticsSettings& diagnostics;

        RenderingPanelAccess(App& app, EditorState& state);

        Camera& camera();
        const Camera& camera() const;
        void resetAreaLightsToScenePreset();
        void updateSunDirection();
        void regenerateVoxelWorld();
        void applyRenderQualityPreset(engine::render::RenderQualityPreset preset);
        void setRenderScale(float scale);
        void setAuxiliaryRayScales(float ddaShadowScale, float aoScale);
        void setGpuProfilerEnabled(bool enabled);
        void startRenderPipelineShowcase(double secondsPerStage, bool loop,
                                         bool startPaused);

        const char* activePresentModeLabel() const;
        float cpuFrameTimeMs() const;
        float gpuFrameTimeMs() const;
        float effectiveFpsLimit() const;
        bool focusedIdleThrottleEnabled() const;
        void setFocusedIdleThrottleEnabled(bool enabled);
        float focusedIdleFpsLimit() const;
        void setFocusedIdleFpsLimit(float fps);
        float focusedIdleDelaySeconds() const;
        void setFocusedIdleDelaySeconds(float seconds);
        bool isFocusedIdleThrottleActive() const;
        bool isBackgroundThrottleActive() const;
        const std::vector<float>& cpuFrameHistoryMs() const;
        const std::vector<float>& gpuFrameHistoryMs() const;
        bool isDdaShadowsAvailable() const;
        bool isAmbientOcclusionAvailable() const;
        bool isLocalLightShadowsAvailable() const;
        bool isLocalShadowBlurAvailable() const;
        bool isSunroofScene() const;
        void markWaterParametersDirty();
        void setUseWaterV2(bool enabled);
        void setProceduralFishEnabled(bool enabled);
        void setProceduralFishCount(int count);

        GpuProfiler& gpuProfiler();
        const GpuProfiler& gpuProfiler() const;

        engine::DDAMetrics& cachedDdaMetrics();
        const engine::DDAMetrics& cachedDdaMetrics() const;

        engine::WaterVolumeManager& waterVolumeMgr();
        const engine::WaterVolumeManager& waterVolumeMgr() const;

    private:
        App* app_ = nullptr;  // opaque handle; dereferenced only in EngineFacade.cpp
    };

    EngineFacade(App& app, EditorState& editorState);

    EditorState& editorState() { return *editorState_; }
    const EditorState& editorState() const { return *editorState_; }

    const SceneConfig& sceneConfig() const;
    engine::scene::ScenePresentationProfileReadout presentationProfileReadout() const;
    const std::vector<SceneCatalogEntry>& availableScenes() const;
    uint64_t sceneCatalogRevision() const;
    const std::filesystem::path& currentScenePath() const;
    void refreshAvailableScenes(bool logResults = false);
    bool loadSceneFromFile(const std::filesystem::path& path);
    void reloadScene(const SceneConfig& config);
    void applyCloudSettings(const SceneConfig& config);
    bool isCloudBuildInProgress() const;

    void setEnvironmentWindSettings(
        const engine::scene::EnvironmentWindSettings& wind);
    void setEnvironmentTimeSettings(
        const engine::scene::EnvironmentTimeSettings& time);
    void setEnvironmentTimePreset(engine::scene::EnvironmentTimePreset preset);
    const engine::scene::EnvironmentTimeSample& environmentTimeSample() const;
    void setWindborneParticleSettings(
        const engine::scene::WindborneParticleSettings& particles);
    void setCloudWindSpeed(float speed);
    void setCloudWindDirection(const glm::vec2& direction);
    void setSceneCloudShadowStrength(float strength);

    void applySkyPreset(int preset);
    const glm::vec3& skyColor() const;
    void setSkyColor(const glm::vec3& color);

    int viewMode() const;
    void setViewMode(int mode);

    glm::vec3 cameraPosition() const;
    float cameraYaw() const;
    float cameraPitch() const;
    glm::mat4 cameraViewProjection(float aspect) const;
    void resetCamera();

    bool resolveEditorSelectedPlaceable(PlaceableInstance& instance) const;
    std::vector<PlaceableInstance> editorPlaceables() const;
    bool editorPlaceableAuthoringAvailable() const;
    std::vector<PlaceablePrototype> editorPlaceablePrototypes() const;
    bool editorCreatePlaceable(const std::string& prototypeSlug,
                               uint32_t prototypeVersion,
                               const glm::vec2& positionXZ);
    bool editorDuplicateSelectedPlaceable(const glm::vec2& offsetXZ);
    bool editorDeleteSelectedPlaceable();
    bool commitEditorPlaceableEdit(const PlaceableEditCommand& command);
    bool editorUndoPlaceableEdit();
    bool editorRedoPlaceableEdit();
    bool editorSaveCurrentScene();
    bool editorHasPlaceableUndo() const;
    bool editorHasPlaceableRedo() const;
    bool editorPlaceableSceneDirty() const;
    const std::string& editorPlaceableEditStatus() const;
    uint64_t editorVoxelStructureRevision() const;

    void enterRuntimeUiPlayPreview(bool startAtMainMenu = false);
    bool isRuntimeUiPlayPreviewActive() const;

    std::vector<FishHandle> fishList() const;
    bool selectFish(uint64_t fishId);
    void clearFishSelection();
    uint64_t selectedFishId() const;

    bool focusOnFish(uint64_t fishId);
    bool releaseFishFocus();
    bool isFishFocusActive() const;
    bool isFishFocusReturning() const;
    uint64_t focusedFishId() const;
    float fishFocusOrbitDistance() const;
    void setFishFocusOrbitDistance(float distance);
    float fishFocusHeightOffset() const;
    void setFishFocusHeightOffset(float offset);
    float fishFocusPositionDamping() const;
    void setFishFocusPositionDamping(float damping);
    float fishFocusRotationDamping() const;
    void setFishFocusRotationDamping(float damping);
    bool fishShakeEnabled() const;
    void setFishShakeEnabled(bool enabled);
    bool fishCelebrationEnabled() const;
    void setFishCelebrationEnabled(bool enabled);
    bool axolotlBellyFloatEnabled() const;
    void setAxolotlBellyFloatEnabled(bool enabled);
    bool axolotlEnabled() const;
    void setAxolotlEnabled(bool enabled);
    float fishShakePositionAmplitude() const;
    void setFishShakePositionAmplitude(float amplitude);
    float fishShakeRotationAmplitude() const;
    void setFishShakeRotationAmplitude(float amplitude);
    float fishShakeFrequency() const;
    void setFishShakeFrequency(float frequency);
    float fishShakeSettleDecay() const;
    void setFishShakeSettleDecay(float decay);

    SwapPresentMode presentMode() const;
    void setPresentMode(SwapPresentMode mode);
    const char* activePresentModeLabel() const;

    float maxFpsLimit() const;
    void setMaxFpsLimit(float fps);
    bool focusedIdleThrottleEnabled() const;
    void setFocusedIdleThrottleEnabled(bool enabled);
    float focusedIdleFpsLimit() const;
    void setFocusedIdleFpsLimit(float fps);
    float focusedIdleDelaySeconds() const;
    void setFocusedIdleDelaySeconds(float seconds);
    bool backgroundThrottleEnabled() const;
    void setBackgroundThrottleEnabled(bool enabled);
    float backgroundFpsLimit() const;
    void setBackgroundFpsLimit(float fps);

    void requestTaaHistoryReset();
    void requestShadowHistoryReset();
    void requestAoHistoryReset();

    bool tonemapEnabled() const;
    void setTonemapEnabled(bool enabled);
    bool bloomEnabled() const;
    void setBloomEnabled(bool enabled);

    bool gpuProfilerEnabled() const;
    void setGpuProfilerEnabled(bool enabled);
    bool pixelInspectEnabled() const;
    void setPixelInspectEnabled(bool enabled);

    float cpuFrameTimeMs() const;
    float gpuFrameTimeMs() const;
    float effectiveFpsLimit() const;
    bool isWindowFocused() const;
    bool isFocusedIdleThrottleActive() const;
    bool isBackgroundThrottleActive() const;
    const std::vector<float>& cpuFrameHistoryMs() const;
    const std::vector<float>& gpuFrameHistoryMs() const;

    GpuProfiler& gpuProfiler();
    const GpuProfiler& gpuProfiler() const;

    RenderingPanelAccess renderingPanel();

private:
    App* app_ = nullptr;
    EditorState* editorState_ = nullptr;
};
