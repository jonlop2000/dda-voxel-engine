#include "engine/game/GameRuntime.h"

#include <utility>

#include "engine/scene/SceneConfig.h"

namespace engine::game
{

GameRuntime::GameRuntime(const CareUtcClock& careUtcClock)
    : careRuntime_(careUtcClock)
{
}

GameRuntime::HeartbeatResult GameRuntime::updateGameStateHeartbeat(
    SceneConfig& sceneConfig, float dtSeconds, bool gameplayActive)
{
    return careRuntime_.updateGameStateHeartbeat(sceneConfig.gameState, dtSeconds,
                                                 gameplayActive);
}

OfflineCareResult GameRuntime::reconcileOfflineCare(
    GameState& gameState, const OfflineCareConfig& config)
{
    return careRuntime_.reconcileOfflineCare(gameState, config);
}

void GameRuntime::stampLastPlayedUtc(GameState& gameState) const
{
    careRuntime_.stampLastPlayedUtc(gameState);
}

void GameRuntime::tickGameplayActionFeedback(float dtSeconds)
{
    careRuntime_.tickGameplayActionFeedback(dtSeconds);
}

WaterHudTelemetry GameRuntime::waterHudTelemetry(const SceneConfig& sceneConfig) const
{
    return careRuntime_.waterHudTelemetry(sceneConfig.gameState);
}

WaterMaintenanceResult GameRuntime::maintainWater(
    WaterState& water, const WaterMaintenanceConfig& config)
{
    return careRuntime_.maintainWater(water, config);
}

PrimaryCreatureSyncResult GameRuntime::synchronizePrimaryCreature(
    GameState& gameState, std::span<const LiveCreatureSlot> liveCreatures)
{
    return careRuntime_.synchronizePrimaryCreature(gameState, liveCreatures);
}

PrimaryCreatureSyncResult GameRuntime::synchronizeCreatureRoster(
    GameState& gameState, std::span<const LiveCreatureSlot> liveCreatures)
{
    return careRuntime_.synchronizeCreatureRoster(gameState, liveCreatures);
}

PrimaryCreatureCareTelemetry GameRuntime::primaryCreatureCareTelemetry(
    const GameState& gameState, const PrimaryCreatureFeedConfig& config) const
{
    return careRuntime_.primaryCreatureCareTelemetry(gameState, config);
}

PrimaryCreatureFeedResult GameRuntime::feedPrimaryCreature(
    GameState& gameState, const PrimaryCreatureFeedConfig& config)
{
    return careRuntime_.feedPrimaryCreature(gameState, config);
}

CreatureVitalityPresentation GameRuntime::primaryCreaturePresentation(
    const GameState& gameState) const
{
    return careRuntime_.primaryCreaturePresentation(gameState);
}

uint64_t GameRuntime::primaryCreatureRuntimeId() const
{
    return careRuntime_.primaryCreatureRuntimeId();
}

const std::string& GameRuntime::primaryCreatureUuid() const
{
    return careRuntime_.primaryCreatureUuid();
}

std::span<const CreatureRuntimeBinding> GameRuntime::creatureRuntimeBindings() const
{
    return careRuntime_.creatureRuntimeBindings();
}

uint64_t GameRuntime::creatureRuntimeId(std::string_view creatureUuid) const
{
    return careRuntime_.creatureRuntimeId(creatureUuid);
}

std::string_view GameRuntime::creatureUuidForRuntimeId(uint64_t runtimeId) const
{
    return careRuntime_.creatureUuidForRuntimeId(runtimeId);
}

GameRuntime::PendingPlaceableEdit GameRuntime::beginCommitPlaceablePreview(
    const PlaceablePreview& preview)
{
    return placeableRuntime_.beginCommitPreview(preview);
}

GameRuntime::PendingPlaceableEdit GameRuntime::beginRemovePlaceableAtPreview(
    const SceneConfig& sceneConfig, const PlaceablePreview& preview)
{
    return placeableRuntime_.beginRemoveAtPreview(sceneConfig, preview);
}

GameRuntime::PendingPlaceableEdit GameRuntime::beginUndoLastPlaceableEdit()
{
    return placeableRuntime_.beginUndoLastEdit();
}

GameRuntime::PendingPlaceableEdit GameRuntime::beginRedoLastPlaceableEdit()
{
    return placeableRuntime_.beginRedoLastEdit();
}

GameRuntime::PlaceableApplyResult GameRuntime::applyPlaceableEditCommand(
    SceneConfig& sceneConfig, const PlaceableEditCommand& command, bool undo,
    bool redo)
{
    return placeableRuntime_.applyEditCommand(sceneConfig, command, undo, redo);
}

bool GameRuntime::completePlaceableEdit(const PendingPlaceableEdit& edit,
                                        SceneConfig& sceneConfig)
{
    return placeableRuntime_.completeEdit(edit, sceneConfig);
}

bool GameRuntime::completeExternalPlaceableEdit(const PlaceableEditCommand& command,
                                                std::string successStatus,
                                                SceneConfig& sceneConfig)
{
    return placeableRuntime_.completeExternalEdit(
        command, std::move(successStatus), sceneConfig);
}

void GameRuntime::failPlaceableEditRebuild()
{
    placeableRuntime_.failEditRebuild();
}

PlaceableInstance GameRuntime::createPlaceableInstance(std::string prototypeSlug,
                                                       uint32_t prototypeVersion,
                                                       const glm::vec3& position)
{
    return placeableRuntime_.createInstance(std::move(prototypeSlug), prototypeVersion,
                                            position);
}

bool GameRuntime::findPlaceableAtPreview(const SceneConfig& sceneConfig,
                                         const PlaceablePreview& preview,
                                         PlaceableInstance& outInstance) const
{
    return placeableRuntime_.findAtPreview(sceneConfig, preview, outInstance);
}

void GameRuntime::resetPlaceableEdits()
{
    placeableRuntime_.resetEdits();
}

void GameRuntime::markPlaceableSceneSaved()
{
    placeableRuntime_.markSceneSaved();
}

void GameRuntime::setPlaceableEditStatus(std::string status)
{
    placeableRuntime_.setEditStatus(std::move(status));
}

bool GameRuntime::beginIsolatedPlaceableEditSession()
{
    return placeableRuntime_.beginIsolatedEditSession();
}

bool GameRuntime::endIsolatedPlaceableEditSession()
{
    return placeableRuntime_.endIsolatedEditSession();
}

bool GameRuntime::hasPlaceableUndo() const
{
    return placeableRuntime_.hasUndo();
}

bool GameRuntime::hasPlaceableRedo() const
{
    return placeableRuntime_.hasRedo();
}

bool GameRuntime::placeableSceneDirty() const
{
    return placeableRuntime_.sceneDirty();
}

const std::string& GameRuntime::placeableEditStatus() const
{
    return placeableRuntime_.editStatus();
}

float GameRuntime::waterMaintenanceFeedbackSeconds() const
{
    return careRuntime_.waterMaintenanceFeedbackSeconds();
}

void GameRuntime::setFishCelebrationEnabled(bool enabled)
{
    fishCelebration_.setEnabled(enabled);
    if (enabled)
    {
        axolotlBellyFloat_.setEnabled(false);
    }
}

void GameRuntime::setAxolotlBellyFloatEnabled(bool enabled)
{
    axolotlBellyFloat_.setEnabled(enabled);
    if (enabled)
    {
        fishCelebration_.setEnabled(false);
    }
}

} // namespace engine::game
