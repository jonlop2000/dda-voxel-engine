#pragma once

#include <atomic>
#include <filesystem>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <glm/vec3.hpp>

#include "engine/scene/CloudScene.h"
#include "engine/scene/SceneCatalog.h"
#include "engine/scene/SceneConfig.h"
#include "engine/scene/ScenePresentationProfileVariants.h"

class JobSystem;

namespace engine::scene
{

struct SceneReloadDecision
{
    bool scenePresetChanged = false;
    bool sceneCameraChanged = false;
    bool voxelWorldChanged = false;
    bool waterRuntimeChanged = false;
    bool pointLightsRuntimeChanged = false;
    bool aquariumWaterV2ModeChanged = false;
    bool aquariumPlaceablesChanged = false;
    bool enteringProceduralScene = false;
    bool volumeScenePresetChanged = false;
    bool volumeSceneChanged = false;
    bool cloudGenerationChanged = false;
    bool skyColorChanged = false;

    bool shouldApplyProceduralPresetDefaults() const
    {
        return scenePresetChanged || enteringProceduralScene;
    }

    bool shouldUpdateCamera() const
    {
        return scenePresetChanged || sceneCameraChanged;
    }

    bool shouldApplyRuntimeToggles() const
    {
        return scenePresetChanged || waterRuntimeChanged ||
               pointLightsRuntimeChanged;
    }

    bool shouldHandleCloudLifecycle() const
    {
        return scenePresetChanged || volumeSceneChanged ||
               cloudGenerationChanged;
    }
};

struct SceneLoadResult
{
    bool loaded = false;
    bool usedAuthoredDefaultFallback = false;
    bool usedBuiltInFallback = false;
    SceneConfig config{};
    std::filesystem::path path{};
};

struct PendingCloudBuild
{
    uint64_t version = 0;
    uint64_t minRenderedFrameCountBeforeCommit = 0;
    double queuedAtSeconds = 0.0;
    double cpuBuildDurationMs = 0.0;
    CloudSettings settings{};
    CloudBuildResult result{};
    std::string error{};
    std::atomic<bool> ready{false};
    std::atomic<bool> failed{false};
};

struct CloudBuildQueueResult
{
    bool queued = false;
    bool clearAutomationCommitObserved = false;
};

enum class CloudBuildPollStatus
{
    None,
    WaitingForFrame,
    WaitingForCpu,
    Ready,
};

struct CloudBuildPollResult
{
    CloudBuildPollStatus status = CloudBuildPollStatus::None;
    std::shared_ptr<PendingCloudBuild> pending{};
};

enum class VolumeSceneOperationKind
{
    SceneReload,
    VolumeRebuild,
    ProceduralWorldRebuild,
    FallbackToVolumeRebuild,
};

struct VolumeSceneEffectPlan
{
    VolumeSceneOperationKind kind = VolumeSceneOperationKind::VolumeRebuild;
    const char* reason = "volume scene";
    bool invalidateCloudBuild = false;
    bool waitForInFlightFrame = false;
    bool shutdownVoxelWorld = false;
    bool resetCloudRuntimeState = false;
    bool runInitVolumeScene = false;
    bool runProceduralRegenerate = false;
    bool fallbackToVolumeRebuild = false;
    bool clearAnimatedStateOnFailure = false;
    bool clearWaterVolumesOnFailure = false;
    bool updateWaterVolumesOnSuccess = false;
    bool requestCloudBuildOnSuccess = false;
    bool rebuildAnimatedObjectsOnSuccess = false;
    bool rebuildFishbowlGlassOnSuccess = false;
    bool rebuildModularGlassOnSuccess = false;
    bool destroyFishbowlGlassOnFailure = false;
    bool clearModularGlassOnFailure = false;
    bool rebuildGlassObjectsAfter = false;
    bool markProceduralDirtyFromResult = false;
    bool markSceneObjectsDirty = false;
    bool markVoxelObjectsDirty = false;
    bool resetRenderHistory = false;
    bool logSuccess = false;
    bool logFailure = false;
};

class SceneManager
{
public:
    // SceneManager owns
    // the applied SceneConfig. subsystems read it through the const view;
    // every mutation goes through sceneConfigMutable() — the single, greppable
    // mutation door — so config writes stay a deliberate, visible act.
    const SceneConfig& sceneConfig() const { return sceneConfig_; }
    SceneConfig& sceneConfigMutable() { return sceneConfig_; }
    const ScenePresentationRuntimeState& presentationRuntimeState() const
    {
        return presentationRuntimeState_;
    }
    void setPresentationRuntimeState(ScenePresentationRuntimeState state)
    {
        presentationRuntimeState_ = std::move(state);
    }

    void setScenesRoot(std::filesystem::path root);
    const std::filesystem::path& scenesRoot() const { return scenesRoot_; }
    const std::vector<SceneCatalogEntry>& availableScenes() const
    {
        return availableScenes_;
    }
    uint64_t sceneCatalogRevision() const { return sceneCatalogRevision_; }
    const std::filesystem::path& currentScenePath() const
    {
        return currentScenePath_;
    }

    void refreshSceneCatalog(bool logResults = false);
    SceneLoadResult loadInitialScene(const std::string& sceneArg);
    SceneLoadResult loadSceneFromFile(const std::filesystem::path& path);

    static SceneReloadDecision evaluateReloadDecision(
        const SceneConfig& nextConfig,
        const SceneConfig& currentConfig);

    static bool placeableContentChanged(const SceneConfig& lhs,
                                        const SceneConfig& rhs);
    static bool cloudGenerationSettingsChanged(const SceneConfig& lhs,
                                               const SceneConfig& rhs);
    static void copyCloudGenerationSettings(SceneConfig& dst,
                                            const SceneConfig& src);
    static CloudSettings buildCloudSettingsFromSceneConfig(
        const SceneConfig& sceneConfig,
        const glm::vec3& cameraPosition);

    bool isCloudBuildInProgress() const;
    CloudBuildQueueResult requestCloudBuild(const CloudSettings& settings,
                                            const char* reason,
                                            JobSystem& jobSystem,
                                            uint64_t renderedFrameCount,
                                            double queuedAtSeconds);
    void invalidatePendingCloudBuild(const char* reason);
    CloudBuildPollResult pollPendingCloudBuild(uint64_t renderedFrameCount,
                                               bool cloudsEnabled);
    void clearPendingCloudBuild();

    static VolumeSceneEffectPlan planSceneReloadVolumeChange();
    static VolumeSceneEffectPlan planVolumeSceneRebuild();
    static VolumeSceneEffectPlan planProceduralWorldRebuild(
        bool loadProceduralWorld);

private:
    SceneConfig sceneConfig_{};
    ScenePresentationRuntimeState presentationRuntimeState_{};
    std::filesystem::path scenesRoot_{};
    std::filesystem::path currentScenePath_{};
    std::vector<SceneCatalogEntry> availableScenes_{};
    uint64_t sceneCatalogRevision_ = 0;
    std::shared_ptr<PendingCloudBuild> pendingCloudBuild_{};
    uint64_t cloudBuildVersion_ = 0;
};

} // namespace engine::scene
