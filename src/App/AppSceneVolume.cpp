#include "App/App.h"

#include <algorithm>

#include "App/AppSceneVolume.h"
#include "Core/Logger.h"
#include "Resources/Mesh.h"
#include "engine/game/CollectionCodex.h"
#include "engine/game/CreatureCareState.h"
#include "engine/scene/AquariumScene.h"
#include "engine/scene/AquariumSceneVariant.h"
#include "engine/scene/NaturePondScene.h"
#include "engine/scene/ProceduralWorldScene.h"
#include "engine/scene/SceneManager.h"
#include "engine/voxel/VoxelPalette.h"
#include "UI/Runtime/UiScreens.h"

bool App::isCareGameplayActive() const
{
    if (appMode_ == AppMode::Editor)
    {
        return false;
    }
#if VOXEL_WITH_RUNTIME_UI
    const std::string& screenId = runtimeUiContext_.activeScreenId();
    return screenId != ui::kMainMenuScreenId &&
           screenId != ui::kMainMenuOptionsScreenId &&
           runtimeUiContext_.overlayCount() == 0;
#else
    return true;
#endif
}

void App::updateGameStateHeartbeat(float dt)
{
    const engine::game::GameRuntime::HeartbeatResult result =
        gameRuntime_.updateGameStateHeartbeat(sceneConfigMutable(), dt,
                                              isCareGameplayActive());
    if (!result.shouldLog)
    {
        return;
    }

    const GameState& state = sceneConfig().gameState;
    const CreatureInstance* primary = engine::game::findPrimaryCreature(state);
    if (primary != nullptr && result.primaryCreatureFound)
    {
        logInfo("GameState",
                makeLogMessage("Care heartbeat clean=", state.water.cleanliness,
                               " oxygen=", state.water.oxygen,
                               " hunger=", primary->needs.hunger,
                               " personalClean=", primary->needs.cleanliness,
                               " happiness=", primary->needs.happiness,
                               " health=", primary->needs.health));
        return;
    }

    logInfo("GameState",
            makeLogMessage("Water heartbeat clean=", state.water.cleanliness,
                           " oxygen=", state.water.oxygen,
                           " flow=", state.water.flow,
                           " tempC=", state.water.temperatureC));
}

void App::synchronizePrimaryCreatureBinding(bool bindLiveCreatures)
{
    std::vector<engine::game::LiveCreatureSlot> liveCreatures;
    if (bindLiveCreatures)
    {
        liveCreatures.reserve(proceduralFish_.size());
        for (const ProceduralFishInstance& fish : proceduralFish_)
        {
            if (fish.id != 0)
            {
                const bool axolotl = fish.isAxolotl;
                liveCreatures.push_back(
                    {fish.id, axolotl ? engine::game::kAxolotlSpeciesId
                                      : engine::game::kStarterFishSpeciesId,
                     fish.stableSpeciesOrdinal});
            }
        }
    }

    const engine::game::PrimaryCreatureSyncResult result =
        gameRuntime_.synchronizeCreatureRoster(sceneConfigMutable().gameState,
                                               liveCreatures);
    if (result.normalization.changed)
    {
        logInfo("CreatureCare",
                makeLogMessage("Primary roster normalized uuid=", result.creatureUuid,
                               " created=", result.normalization.createdPrimary ? 1 : 0,
                               " fallback=",
                               result.normalization.selectedFallbackPrimary ? 1 : 0,
                               " extra_primary=",
                               result.normalization.clearedExtraPrimaryFlags,
                               " duplicate_uuid=",
                               result.normalization.repairedDuplicateUuids));
    }
}

#if VOXEL_WITH_RUNTIME_UI
void App::syncRuntimeUiCareHud(ui::UiTree& tree, uint32_t fishCount,
                               const char* timeOfDayLabel) const
{
    ui::UiOverlaySmokeHudStatus status{};
    status.fishCount = fishCount;
    status.timeOfDayLabel = timeOfDayLabel != nullptr ? timeOfDayLabel : "DAY";

    const engine::game::WaterHudTelemetry water =
        gameRuntime_.waterHudTelemetry(sceneConfig());
    status.waterTemperatureC = water.temperatureC;
    status.waterOxygen = water.oxygen;
    status.waterFlow = water.flow;
    status.waterCleanliness = water.cleanliness;
    status.waterMaintenanceFeedbackActive = water.maintenanceFeedbackActive;

    const engine::game::PrimaryCreatureCareTelemetry care =
        gameRuntime_.primaryCreatureCareTelemetry(sceneConfig().gameState);
    status.primaryCreatureName =
        care.hasPrimary ? care.displayName : std::string("NO CREATURE");
    status.primaryCreatureHunger = care.hunger;
    status.primaryCreatureCleanliness = care.cleanliness;
    status.primaryCreatureHappiness = care.happiness;
    status.primaryCreatureHealth = care.health;
    status.creatureFeedCooldownRemaining = care.feedCooldownRemaining;
    if (care.feedFeedbackActive && care.liveBound)
    {
        status.creatureFeedState = ui::UiCreatureFeedState::FedFeedback;
    }
    else
    {
        using FeedAvailability = engine::game::PrimaryCreatureFeedAvailability;
        switch (care.feedAvailability)
        {
        case FeedAvailability::Available:
            status.creatureFeedState = ui::UiCreatureFeedState::Ready;
            break;
        case FeedAvailability::NoPrimaryCreature:
            status.creatureFeedState = ui::UiCreatureFeedState::NoCreature;
            break;
        case FeedAvailability::NoLivePrimaryCreature:
            status.creatureFeedState = ui::UiCreatureFeedState::CreatureUnavailable;
            break;
        case FeedAvailability::NotHungry:
            status.creatureFeedState = ui::UiCreatureFeedState::NotHungry;
            break;
        case FeedAvailability::CooldownActive:
            status.creatureFeedState = ui::UiCreatureFeedState::Cooldown;
            break;
        }
    }
    ui::setOverlaySmokeHudStatus(tree, status);
}

void App::registerCareRuntimeUiActions()
{
    runtimeUiActionDispatcher_.registerHandler(
        std::string(ui::kMaintainWaterAction), [this](const ui::UiActionEvent&) {
            engine::game::WaterMaintenanceConfig config{};
            config.cleanlinessThreshold = ui::kMaintainWaterCleanlinessThreshold;
            config.oxygenBaseline = ui::kMaintainWaterOxygenBaseline;
            config.feedbackSeconds = 1.0f;
            const engine::game::WaterMaintenanceResult result =
                gameRuntime_.maintainWater(sceneConfigMutable().gameState.water, config);
            if (!result.maintained)
            {
                logInfo("RuntimeUI",
                        makeLogMessage("Water maintenance ignored clean=",
                                       result.cleanliness, " oxygen=", result.oxygen));
                return;
            }
            sceneObjectsDirty_ = true;
            logInfo("RuntimeUI",
                    makeLogMessage("Water maintained clean=", result.cleanliness,
                                   " oxygen=", result.oxygen,
                                   " scene_dirty=", sceneObjectsDirty_ ? 1 : 0,
                                   " feedback_s=", result.feedbackSeconds));
        });
    runtimeUiActionDispatcher_.registerHandler(
        std::string(ui::kFeedPrimaryCreatureAction),
        [this](const ui::UiActionEvent&) {
            const engine::game::PrimaryCreatureFeedResult result =
                gameRuntime_.feedPrimaryCreature(sceneConfigMutable().gameState);
            if (!result.fed())
            {
                logInfo("CreatureCare",
                        makeLogMessage("Feed ignored status=",
                                       static_cast<int>(result.status),
                                       " hunger=", result.hungerAfter,
                                       " cooldown_s=", result.cooldownRemaining));
                return;
            }
            logInfo("CreatureCare",
                    makeLogMessage("Primary fed hunger_before=", result.hungerBefore,
                                   " hunger_after=", result.hungerAfter,
                                   " cooldown_s=", result.cooldownRemaining));
        });
    runtimeUiActionDispatcher_.registerHandler(
        std::string(ui::kOpenCollectionCodexAction),
        [this](const ui::UiActionEvent&) {
            if (runtimeUiContext_.topScreenId() == ui::kCollectionCodexScreenId ||
                !runtimeUiContext_.pushScreen(ui::kCollectionCodexScreenId))
            {
                return;
            }
            ui::UiTree* tree = runtimeUiContext_.topTree();
            if (tree == nullptr)
            {
                return;
            }
            // authored scene geometry does not yet expose semantic habitat decor
            // contributions, so the codex reports those requirements honestly as
            // unmet advice instead of inferring them from placement permissions.
            const engine::game::CollectionCodexView view =
                engine::game::buildCollectionCodexView(sceneConfig().gameState);
            ui::setCollectionCodexView(*tree, view);
            const VkExtent2D extent = renderer_.swapchainExtent();
            ui::setCollectionCodexViewport(*tree, static_cast<float>(extent.width),
                                           static_cast<float>(extent.height));
            ui::computeLayout(tree->root);
            logInfo("RuntimeUI",
                    makeLogMessage("Screen pushed id=", ui::kCollectionCodexScreenId,
                                   " species=", view.entries.size(),
                                   " creatures=", view.totalOwnedCreatures,
                                   " depth=", runtimeUiContext_.overlayCount()));
        });
    runtimeUiActionDispatcher_.registerHandler(
        std::string(ui::kSelectCollectionCodexSpeciesAction),
        [this](const ui::UiActionEvent& event) {
            ui::UiTree* tree = runtimeUiContext_.topTree();
            if (runtimeUiContext_.topScreenId() != ui::kCollectionCodexScreenId ||
                tree == nullptr || event.payload.empty())
            {
                return;
            }
            const engine::game::CollectionCodexView view =
                engine::game::buildCollectionCodexView(sceneConfig().gameState);
            ui::setCollectionCodexView(*tree, view, event.payload);
            ui::computeLayout(tree->root);
        });
    runtimeUiActionDispatcher_.registerHandler(
        std::string(ui::kCloseCollectionCodexAction),
        [this](const ui::UiActionEvent&) {
            if (runtimeUiContext_.topScreenId() != ui::kCollectionCodexScreenId)
            {
                return;
            }
            runtimeUiContext_.popScreen();
            logInfo("RuntimeUI",
                    makeLogMessage("Screen popped id=", ui::kCollectionCodexScreenId,
                                   " depth=", runtimeUiContext_.overlayCount()));
        });
}
#endif

bool App::shouldDrawVoxelVolumeWithDda(uint32_t volumeIndex) const
{
    if (renderPasses().foliage.replacesVoxelVolume(volumeIndex))
    {
        return false;
    }
    if (shouldUseModularVoxelGlass())
    {
        const std::vector<engine::VoxelInstance>& instances = voxelWorld_.instances();
        if (volumeIndex < instances.size())
        {
            const engine::VoxelInstance& inst = instances[volumeIndex];
            if ((inst.volume.flags() & engine::VoxelVolume::FLAG_GLASS) != 0u &&
                voxelWorld_.hasCpuVoxelData(volumeIndex))
            {
                return false;
            }
        }
    }
    return !modularGlassMeshes_.ownsVolume(volumeIndex);
}

bool App::executeVolumeScenePlan(
    const engine::scene::VolumeSceneEffectPlan& plan)
{
    if (plan.invalidateCloudBuild)
    {
        invalidatePendingCloudBuild(plan.reason);
    }
    if (plan.waitForInFlightFrame)
    {
        waitForInFlightFrameWork(plan.reason);
    }
    if (plan.shutdownVoxelWorld)
    {
        voxelWorld_.clearScene(ctx_);
    }
    if (plan.resetCloudRuntimeState)
    {
        resetCloudRuntimeState();
    }

    const bool ok = plan.runInitVolumeScene ? initVolumeScene() : true;
    if (!ok)
    {
        if (plan.clearAnimatedStateOnFailure)
        {
            animatedObjects_.clear();
            proceduralFish_.clear();
            heroFoliage_.clear();
            proceduralFishPoseValid_ = false;
            animatedObjectsDirty_ = true;
        }
        if (plan.clearWaterVolumesOnFailure &&
            waterVolumeMgr_.buffer() != VK_NULL_HANDLE)
        {
            waterVolumeMgr_.setVolumes({});
        }
        if (plan.logFailure)
        {
            switch (plan.kind)
            {
            case engine::scene::VolumeSceneOperationKind::SceneReload:
                logError("Scene",
                         std::string(
                             "Failed to initialize volume scene while reloading '") +
                             sceneConfig().name + "'.");
                break;
            case engine::scene::VolumeSceneOperationKind::VolumeRebuild:
                logError("Scene",
                         std::string("Failed to rebuild volume scene for '") +
                             sceneConfig().name + "'.");
                break;
            default:
                logError("Scene", "Failed to execute volume scene plan.");
                break;
            }
        }
    }
    else if (plan.updateWaterVolumesOnSuccess &&
             waterVolumeMgr_.buffer() != VK_NULL_HANDLE)
    {
        waterVolumeMgr_.setVolumes(
            {makeSceneWaterVolume(sceneConfig(), voxelWorld_, waterSettings_.waterLevel_)});
        if (plan.logSuccess)
        {
            logInfo("Scene",
                    std::string("Rebuilt volume scene for '") +
                        sceneConfig().name + "'.");
        }
    }

    if (ok)
    {
        if (plan.rebuildAnimatedObjectsOnSuccess)
        {
            rebuildAnimatedObjects();
        }
        if (plan.requestCloudBuildOnSuccess)
        {
            requestCloudBuildFromSceneConfig(plan.reason);
        }
        if (plan.rebuildFishbowlGlassOnSuccess)
        {
            rebuildFishbowlGlassMesh();
        }
        if (plan.rebuildModularGlassOnSuccess)
        {
            rebuildModularGlassMeshes();
        }
    }
    else
    {
        if (plan.destroyFishbowlGlassOnFailure &&
            fishbowlGlassMesh_.vbo != VK_NULL_HANDLE)
        {
            destroyMeshBuffer(ctx_.device, fishbowlGlassMesh_);
        }
        if (plan.clearModularGlassOnFailure)
        {
            modularGlassMeshes_.clear(ctx_.device);
        }
    }

    if (plan.rebuildGlassObjectsAfter)
    {
        rebuildGlassObjectsForCurrentScene();
    }
    if (plan.markSceneObjectsDirty)
    {
        sceneObjectsDirty_ = true;
    }
    if (plan.markVoxelObjectsDirty)
    {
        voxelObjectsDirty_ = true;
    }
    if (plan.resetRenderHistory)
    {
        postFxSettings_.taaResetHistory_ = true;
        shadowSettings_.shadowResetHistory_ = true;
        shadowSettings_.localShadowResetHistory_ = true;
        aoSettings_.aoResetHistory_ = true;
    }

    return ok;
}

bool App::rebuildVolumeScene()
{
    return executeVolumeScenePlan(
        engine::scene::SceneManager::planVolumeSceneRebuild());
}

bool App::rebuildProceduralWorld()
{
    if (isNaturePondScene(sceneConfig()))
    {
        return rebuildVolumeScene();
    }

    const engine::scene::VolumeSceneEffectPlan plan =
        engine::scene::SceneManager::planProceduralWorldRebuild(
            sceneConfig().loadProceduralWorld);
    if (plan.fallbackToVolumeRebuild)
    {
        return rebuildVolumeScene();
    }

    if (plan.invalidateCloudBuild)
    {
        invalidatePendingCloudBuild(plan.reason);
    }
    if (plan.waitForInFlightFrame)
    {
        waitForInFlightFrameWork(plan.reason);
    }
    if (plan.resetCloudRuntimeState)
    {
        resetCloudRuntimeState();
    }

    ProceduralWorldSettings settings{};
    settings.seed = proceduralWorldSettings_.proceduralSeed_;
    settings.terrainStyle = isAppBeachSandScene(sceneConfig())
                                ? ProceduralWorldTerrainStyle::Beach
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

    const bool ok = ProceduralWorldScene::regenerate(
        ctx_, voxelWorld_, voxelPalette_, settings, true);
    if (!ok)
    {
        if (plan.clearWaterVolumesOnFailure &&
            waterVolumeMgr_.buffer() != VK_NULL_HANDLE)
        {
            waterVolumeMgr_.setVolumes({});
        }
        if (plan.logFailure)
        {
            logError("Scene", "Failed to rebuild procedural world.");
        }
    }
    else if (plan.updateWaterVolumesOnSuccess &&
             waterVolumeMgr_.buffer() != VK_NULL_HANDLE)
    {
        waterVolumeMgr_.setVolumes(
            {makeSceneWaterVolume(sceneConfig(), voxelWorld_, waterSettings_.waterLevel_)});
        if (plan.logSuccess)
        {
            logInfo("Scene", "Rebuilt procedural world.");
        }
    }
    if (ok && plan.requestCloudBuildOnSuccess)
    {
        requestCloudBuildFromSceneConfig(plan.reason);
    }
    if (plan.markProceduralDirtyFromResult)
    {
        proceduralWorldSettings_.proceduralDirty_ = !ok;
    }
    if (plan.markSceneObjectsDirty)
    {
        sceneObjectsDirty_ = true;
    }
    if (plan.markVoxelObjectsDirty)
    {
        voxelObjectsDirty_ = true;
    }
    if (plan.resetRenderHistory)
    {
        postFxSettings_.taaResetHistory_ = true;
        shadowSettings_.shadowResetHistory_ = true;
        shadowSettings_.localShadowResetHistory_ = true;
        aoSettings_.aoResetHistory_ = true;
    }
    return ok;
}

// cloud drift animation with camera-relative recentring for sky-wide coverage.
void App::updateCloudDriftAnimation(double animationNow)
{
    static constexpr float kCloudRecenteringMarginFraction = 0.25f;

    if (cloudVolumeIndex_ < 0 || !sceneConfig().loadCloudScene)
    {
        return;
    }
    const uint32_t cloudIndex = static_cast<uint32_t>(cloudVolumeIndex_);
    const auto& instances = voxelWorld_.instances();
    if (cloudIndex >= instances.size())
    {
        return;
    }

    const glm::ivec3 cloudDims = instances[cloudIndex].volume.dimensions();
    const float dimX = std::max(1.0f, static_cast<float>(cloudDims.x));
    const float dimZ = std::max(1.0f, static_cast<float>(cloudDims.z));
    const float marginX = dimX * kCloudRecenteringMarginFraction;
    const float marginZ = dimZ * kCloudRecenteringMarginFraction;

    glm::vec3 recenteredPos = cloudBasePosition_;
    const float minFollowX = cloudBasePosition_.x + marginX;
    const float maxFollowX = cloudBasePosition_.x + dimX - marginX;
    const float minFollowZ = cloudBasePosition_.z + marginZ;
    const float maxFollowZ = cloudBasePosition_.z + dimZ - marginZ;

    if (camera_.position.x < minFollowX || camera_.position.x > maxFollowX)
    {
        recenteredPos.x = camera_.position.x - dimX * 0.5f;
    }
    if (camera_.position.z < minFollowZ || camera_.position.z > maxFollowZ)
    {
        recenteredPos.z = camera_.position.z - dimZ * 0.5f;
    }

    if (glm::length(recenteredPos - cloudBasePosition_) > 0.001f)
    {
        const glm::vec3 delta = recenteredPos - cloudBasePosition_;
        cloudBasePosition_ = recenteredPos;
        cloudWrapRecenterOffset_ += delta;
        voxelWorld_.setInstancePosition(cloudIndex, cloudBasePosition_);
    }

    const float time = static_cast<float>(animationNow);
    // normalize direction (handle zero-length gracefully).
    glm::vec2 dir = sceneConfig().cloudWindDirection;
    float dirLen = glm::length(dir);
    if (dirLen > 0.001f)
    {
        dir /= dirLen;
    }
    else
    {
        dir = glm::vec2(0.0f, 1.0f);  // Default to +z.
    }

    // negate to fix direction (wrap offset moves content opposite to perceived motion).
    const float speed = time * sceneConfig().cloudWindSpeed;
    const glm::vec3 windWrapOffset(-dir.x * speed, 0.0f, -dir.y * speed);
    voxelWorld_.setVolumeWrapOffset(cloudIndex, cloudWrapRecenterOffset_ + windWrapOffset);
}

bool isSunroofAquariumScene(const SceneConfig& sceneConfig)
{
    return sceneConfig.loadAquariumTest &&
           engine::scene::isAquariumSunroofSceneName(sceneConfig.name);
}

bool isFishbowlAquariumScene(const SceneConfig& sceneConfig)
{
    return sceneConfig.loadAquariumTest &&
           (sceneConfig.name == "fishbowl_perf_probe" || sceneConfig.name == "fishbowl");
}

AquariumLayout aquariumLayoutForScene(const SceneConfig& sceneConfig)
{
    if (isSunroofAquariumScene(sceneConfig))
    {
        return AquariumLayout::SunroofChamber;
    }
    if (isFishbowlAquariumScene(sceneConfig))
    {
        return AquariumLayout::Fishbowl;
    }
    return AquariumLayout::Default;
}
