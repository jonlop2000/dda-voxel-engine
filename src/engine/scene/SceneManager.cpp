#include "engine/scene/SceneManager.h"

#include <cmath>
#include <chrono>
#include <exception>
#include <utility>

#include <glm/geometric.hpp>

#include "Core/JobSystem.h"
#include "Core/Logger.h"
#include "engine/game/PlaceableTransform.h"
#include "engine/scene/SceneSerializer.h"

namespace engine::scene
{

void SceneManager::setScenesRoot(std::filesystem::path root)
{
    scenesRoot_ = std::move(root).lexically_normal();
}

void SceneManager::refreshSceneCatalog(bool logResults)
{
    availableScenes_ = scanSceneCatalog(scenesRoot_);
    ++sceneCatalogRevision_;

    if (!logResults)
    {
        return;
    }

    if (availableScenes_.empty())
    {
        logWarning("Scene",
                   std::string("No authored scene files found in ") +
                       scenesRoot_.string());
        return;
    }

    size_t invalidSceneCount = 0;
    for (const SceneCatalogEntry& scene : availableScenes_)
    {
        if (!scene.valid)
        {
            ++invalidSceneCount;
            logError("Scene",
                     std::string("Invalid authored scene: ") +
                         scene.path.string() + " (" + scene.errorMessage + ")");
        }
    }

    logInfo("Scene",
            std::string("Scene catalog loaded: ") +
                std::to_string(availableScenes_.size()) +
                " authored scene(s), " + std::to_string(invalidSceneCount) +
                " invalid");
}

SceneLoadResult SceneManager::loadInitialScene(const std::string& sceneArg)
{
    SceneLoadResult result{};
    result.path = resolveScenePath(scenesRoot_, sceneArg).lexically_normal();
    if (loadSceneConfigFromFile(result.path, result.config))
    {
        currentScenePath_ = result.path;
        result.loaded = true;
        return result;
    }

    const std::filesystem::path defaultScenePath =
        resolveScenePath(scenesRoot_, "obb_test").lexically_normal();
    if (defaultScenePath != result.path &&
        loadSceneConfigFromFile(defaultScenePath, result.config))
    {
        currentScenePath_ = defaultScenePath;
        result.path = defaultScenePath;
        result.loaded = true;
        result.usedAuthoredDefaultFallback = true;
        logInfo("Scene",
                std::string("Falling back to authored default scene: ") +
                    result.config.name);
        return result;
    }

    currentScenePath_.clear();
    result.path.clear();
    result.config = SceneConfig::obbTest();
    result.loaded = true;
    result.usedBuiltInFallback = true;
    logInfo("Scene",
            std::string("Using built-in default scene: ") + result.config.name);
    return result;
}

SceneLoadResult SceneManager::loadSceneFromFile(const std::filesystem::path& path)
{
    SceneLoadResult result{};
    result.path = std::filesystem::path(path).lexically_normal();
    if (!loadSceneConfigFromFile(result.path, result.config))
    {
        return result;
    }

    currentScenePath_ = result.path;
    result.loaded = true;
    return result;
}

bool SceneManager::placeableContentChanged(const SceneConfig& lhs,
                                           const SceneConfig& rhs)
{
    if (lhs.useDefaultPlaceables != rhs.useDefaultPlaceables ||
        lhs.placeables.size() != rhs.placeables.size())
    {
        return true;
    }

    for (size_t i = 0; i < lhs.placeables.size(); ++i)
    {
        const PlaceableInstance& a = lhs.placeables[i];
        const PlaceableInstance& b = rhs.placeables[i];
        if (a.uuid != b.uuid || a.prototypeSlug != b.prototypeSlug ||
            a.prototypeVersion != b.prototypeVersion || a.seed != b.seed)
        {
            return true;
        }
        if (!engine::game::placeableTransformEquivalent(a, b, 1e-4f))
        {
            return true;
        }
    }

    return false;
}

bool SceneManager::cloudGenerationSettingsChanged(const SceneConfig& lhs,
                                                  const SceneConfig& rhs)
{
    return lhs.loadCloudScene != rhs.loadCloudScene ||
           lhs.cloudSeed != rhs.cloudSeed ||
           std::abs(lhs.cloudAltitude - rhs.cloudAltitude) > 1e-4f ||
           std::abs(lhs.cloudCoverage - rhs.cloudCoverage) > 1e-4f ||
           std::abs(lhs.cloudScale - rhs.cloudScale) > 1e-4f;
}

void SceneManager::copyCloudGenerationSettings(SceneConfig& dst,
                                               const SceneConfig& src)
{
    dst.loadCloudScene = src.loadCloudScene;
    dst.cloudSeed = src.cloudSeed;
    dst.cloudAltitude = src.cloudAltitude;
    dst.cloudCoverage = src.cloudCoverage;
    dst.cloudScale = src.cloudScale;
}

CloudSettings SceneManager::buildCloudSettingsFromSceneConfig(
    const SceneConfig& sceneConfig,
    const glm::vec3& cameraPosition)
{
    CloudSettings cloudSettings{};
    cloudSettings.seed = sceneConfig.cloudSeed;
    cloudSettings.position.x =
        cameraPosition.x - static_cast<float>(cloudSettings.dims.x) * 0.5f;
    cloudSettings.position.y = sceneConfig.cloudAltitude;
    cloudSettings.position.z =
        cameraPosition.z - static_cast<float>(cloudSettings.dims.z) * 0.5f;
    cloudSettings.cloudType = CloudType::Cumulus;
    cloudSettings.densityThreshold = 0.65f - sceneConfig.cloudCoverage * 0.45f;
    cloudSettings.baseNoiseScale = sceneConfig.cloudScale;
    return cloudSettings;
}

bool SceneManager::isCloudBuildInProgress() const
{
    const std::shared_ptr<PendingCloudBuild> pending = pendingCloudBuild_;
    return pending && !pending->ready.load(std::memory_order_acquire) &&
           !pending->failed.load(std::memory_order_acquire);
}

CloudBuildQueueResult SceneManager::requestCloudBuild(
    const CloudSettings& settings,
    const char* reason,
    JobSystem& jobSystem,
    uint64_t renderedFrameCount,
    double queuedAtSeconds)
{
    const uint64_t nextVersion = cloudBuildVersion_ + 1;
    if (pendingCloudBuild_)
    {
        logInfo("CloudScene",
                makeLogMessage("Superseding pending cloud build v",
                               pendingCloudBuild_->version, " with v",
                               nextVersion, " (",
                               reason == nullptr ? "unspecified" : reason,
                               ")."));
    }

    auto pending = std::make_shared<PendingCloudBuild>();
    pending->version = nextVersion;
    pending->minRenderedFrameCountBeforeCommit = renderedFrameCount + 1;
    pending->queuedAtSeconds = queuedAtSeconds;
    pending->settings = settings;

    cloudBuildVersion_ = nextVersion;
    pendingCloudBuild_ = pending;

    logInfo("CloudScene",
            makeLogMessage("Deferred cloud build queued. version=",
                           pending->version,
                           " reason=",
                           reason == nullptr ? "unspecified" : reason,
                           " position=(", settings.position.x, ", ",
                           settings.position.y, ", ", settings.position.z,
                           ")"));

    jobSystem.enqueue([pending]() {
        try
        {
            const auto cpuStart = std::chrono::steady_clock::now();
            pending->result = CloudScene::buildVolumeData(pending->settings);
            pending->cpuBuildDurationMs =
                std::chrono::duration<double, std::milli>(
                    std::chrono::steady_clock::now() - cpuStart)
                    .count();
            pending->ready.store(true, std::memory_order_release);
        }
        catch (const std::exception& e)
        {
            pending->error = e.what();
            pending->failed.store(true, std::memory_order_release);
        }
        catch (...)
        {
            pending->error = "unknown exception";
            pending->failed.store(true, std::memory_order_release);
        }
    });

    return CloudBuildQueueResult{true, true};
}

void SceneManager::invalidatePendingCloudBuild(const char* reason)
{
    if (!pendingCloudBuild_)
    {
        return;
    }

    logInfo("CloudScene",
            makeLogMessage("Discarding pending cloud build v",
                           pendingCloudBuild_->version, " (",
                           reason == nullptr ? "unspecified" : reason, ")."));
    ++cloudBuildVersion_;
    pendingCloudBuild_.reset();
}

CloudBuildPollResult SceneManager::pollPendingCloudBuild(
    uint64_t renderedFrameCount,
    bool cloudsEnabled)
{
    const std::shared_ptr<PendingCloudBuild> pending = pendingCloudBuild_;
    if (!pending)
    {
        return {};
    }

    if (pending->version != cloudBuildVersion_)
    {
        logInfo("CloudScene",
                makeLogMessage("Discarded stale cloud build result v",
                               pending->version,
                               " because the active version is ",
                               cloudBuildVersion_, "."));
        pendingCloudBuild_.reset();
        return {};
    }

    if (renderedFrameCount < pending->minRenderedFrameCountBeforeCommit)
    {
        return CloudBuildPollResult{CloudBuildPollStatus::WaitingForFrame,
                                    pending};
    }

    if (pending->failed.load(std::memory_order_acquire))
    {
        logWarning("CloudScene",
                   makeLogMessage("Deferred cloud build failed for version ",
                                  pending->version, ": ",
                                  pending->error.empty()
                                      ? "unknown cloud build failure"
                                      : pending->error));
        pendingCloudBuild_.reset();
        return {};
    }

    if (!pending->ready.load(std::memory_order_acquire))
    {
        return CloudBuildPollResult{CloudBuildPollStatus::WaitingForCpu,
                                    pending};
    }

    if (!cloudsEnabled)
    {
        logInfo("CloudScene",
                makeLogMessage("Discarded completed cloud build v",
                               pending->version,
                               " because clouds are disabled."));
        pendingCloudBuild_.reset();
        return {};
    }

    return CloudBuildPollResult{CloudBuildPollStatus::Ready, pending};
}

void SceneManager::clearPendingCloudBuild()
{
    pendingCloudBuild_.reset();
}

VolumeSceneEffectPlan SceneManager::planSceneReloadVolumeChange()
{
    VolumeSceneEffectPlan plan{};
    plan.kind = VolumeSceneOperationKind::SceneReload;
    plan.reason = "scene reload";
    plan.invalidateCloudBuild = true;
    plan.shutdownVoxelWorld = true;
    plan.resetCloudRuntimeState = true;
    plan.runInitVolumeScene = true;
    plan.clearWaterVolumesOnFailure = true;
    plan.updateWaterVolumesOnSuccess = true;
    plan.logFailure = true;
    return plan;
}

VolumeSceneEffectPlan SceneManager::planVolumeSceneRebuild()
{
    VolumeSceneEffectPlan plan{};
    plan.kind = VolumeSceneOperationKind::VolumeRebuild;
    plan.reason = "volume scene rebuild";
    plan.invalidateCloudBuild = true;
    plan.waitForInFlightFrame = true;
    plan.shutdownVoxelWorld = true;
    plan.resetCloudRuntimeState = true;
    plan.runInitVolumeScene = true;
    plan.clearAnimatedStateOnFailure = true;
    plan.clearWaterVolumesOnFailure = true;
    plan.updateWaterVolumesOnSuccess = true;
    plan.requestCloudBuildOnSuccess = true;
    plan.rebuildAnimatedObjectsOnSuccess = true;
    plan.rebuildFishbowlGlassOnSuccess = true;
    plan.rebuildModularGlassOnSuccess = true;
    plan.destroyFishbowlGlassOnFailure = true;
    plan.clearModularGlassOnFailure = true;
    plan.rebuildGlassObjectsAfter = true;
    plan.markSceneObjectsDirty = true;
    plan.markVoxelObjectsDirty = true;
    plan.resetRenderHistory = true;
    plan.logSuccess = true;
    plan.logFailure = true;
    return plan;
}

VolumeSceneEffectPlan SceneManager::planProceduralWorldRebuild(
    bool loadProceduralWorld)
{
    if (!loadProceduralWorld)
    {
        VolumeSceneEffectPlan fallback{};
        fallback.kind = VolumeSceneOperationKind::FallbackToVolumeRebuild;
        fallback.reason = "volume scene rebuild";
        fallback.fallbackToVolumeRebuild = true;
        return fallback;
    }

    VolumeSceneEffectPlan plan{};
    plan.kind = VolumeSceneOperationKind::ProceduralWorldRebuild;
    plan.reason = "procedural world rebuild";
    plan.invalidateCloudBuild = true;
    plan.waitForInFlightFrame = true;
    plan.resetCloudRuntimeState = true;
    plan.runProceduralRegenerate = true;
    plan.clearWaterVolumesOnFailure = true;
    plan.updateWaterVolumesOnSuccess = true;
    plan.requestCloudBuildOnSuccess = true;
    plan.markProceduralDirtyFromResult = true;
    plan.markSceneObjectsDirty = true;
    plan.markVoxelObjectsDirty = true;
    plan.resetRenderHistory = true;
    plan.logSuccess = true;
    plan.logFailure = true;
    return plan;
}

SceneReloadDecision SceneManager::evaluateReloadDecision(
    const SceneConfig& nextConfig,
    const SceneConfig& currentConfig)
{
    SceneReloadDecision decision{};
    decision.scenePresetChanged = (nextConfig.name != currentConfig.name);
    decision.sceneCameraChanged =
        (glm::length(nextConfig.cameraPosition - currentConfig.cameraPosition) > 1e-4f) ||
        (std::abs(nextConfig.cameraYaw - currentConfig.cameraYaw) > 1e-4f) ||
        (std::abs(nextConfig.cameraPitch - currentConfig.cameraPitch) > 1e-4f);
    decision.voxelWorldChanged =
        (nextConfig.loadVoxelWorld != currentConfig.loadVoxelWorld) ||
        (nextConfig.worldDimsX != currentConfig.worldDimsX) ||
        (nextConfig.worldDimsY != currentConfig.worldDimsY) ||
        (nextConfig.worldDimsZ != currentConfig.worldDimsZ) ||
        (nextConfig.worldSeed != currentConfig.worldSeed);
    decision.waterRuntimeChanged =
        (nextConfig.enableWater != currentConfig.enableWater) ||
        (nextConfig.useWaterV2 != currentConfig.useWaterV2);
    decision.pointLightsRuntimeChanged =
        nextConfig.enablePointLights != currentConfig.enablePointLights;
    decision.aquariumWaterV2ModeChanged =
        nextConfig.loadAquariumTest && currentConfig.loadAquariumTest &&
        (nextConfig.useWaterV2 != currentConfig.useWaterV2);
    decision.aquariumPlaceablesChanged =
        (nextConfig.loadAquariumTest || currentConfig.loadAquariumTest) &&
        placeableContentChanged(nextConfig, currentConfig);
    decision.enteringProceduralScene =
        nextConfig.loadProceduralWorld && !currentConfig.loadProceduralWorld;
    decision.volumeScenePresetChanged =
        decision.scenePresetChanged &&
        (nextConfig.loadAquariumTest || currentConfig.loadAquariumTest ||
         nextConfig.loadGlassTestScene || currentConfig.loadGlassTestScene ||
         nextConfig.loadOBBVolumes || currentConfig.loadOBBVolumes ||
         nextConfig.loadProceduralWorld || currentConfig.loadProceduralWorld ||
         nextConfig.loadVoxelImport || currentConfig.loadVoxelImport);
    decision.volumeSceneChanged =
        decision.volumeScenePresetChanged ||
        (nextConfig.loadAquariumTest != currentConfig.loadAquariumTest) ||
        (nextConfig.loadGlassTestScene != currentConfig.loadGlassTestScene) ||
        (nextConfig.loadOBBVolumes != currentConfig.loadOBBVolumes) ||
        (nextConfig.loadProceduralWorld != currentConfig.loadProceduralWorld) ||
        (nextConfig.loadVoxelImport != currentConfig.loadVoxelImport) ||
        (nextConfig.useMeshTankGlass != currentConfig.useMeshTankGlass) ||
        decision.aquariumWaterV2ModeChanged ||
        decision.aquariumPlaceablesChanged;
    decision.cloudGenerationChanged =
        cloudGenerationSettingsChanged(nextConfig, currentConfig);
    decision.skyColorChanged =
        (nextConfig.skyPreset != currentConfig.skyPreset) ||
        (glm::length(nextConfig.skyColor - currentConfig.skyColor) > 1e-4f);
    return decision;
}

} // namespace engine::scene
