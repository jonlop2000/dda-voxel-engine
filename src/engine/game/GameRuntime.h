#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>

#include <glm/vec3.hpp>

#include "engine/game/AxolotlBellyFloat.h"
#include "engine/game/CareRuntime.h"
#include "engine/game/FishCelebration.h"
#include "engine/game/PlaceableRuntime.h"

struct SceneConfig;

namespace engine::game
{

// compatibility facade for application-facing gameplay services. focused state
// ownership lives in CareRuntime and PlaceableRuntime; celebration coordination
// remains here because it spans the two independent presentation helpers.
class GameRuntime
{
public:
    explicit GameRuntime(
        const CareUtcClock& careUtcClock = systemCareUtcClock());
    GameRuntime(CareUtcClock&&) = delete;
    GameRuntime(const CareUtcClock&&) = delete;

    using HeartbeatResult = CareRuntime::HeartbeatResult;
    using PlaceableEditEffects = PlaceableRuntime::EditEffects;
    using PlaceableApplyResult = PlaceableRuntime::ApplyResult;
    using PlaceableCompletionAction = PlaceableRuntime::CompletionAction;
    using PendingPlaceableEdit = PlaceableRuntime::PendingEdit;

    HeartbeatResult updateGameStateHeartbeat(SceneConfig& sceneConfig,
                                             float dtSeconds,
                                             bool gameplayActive);
    OfflineCareResult reconcileOfflineCare(
        GameState& gameState, const OfflineCareConfig& config = {});
    void stampLastPlayedUtc(GameState& gameState) const;
    void tickGameplayActionFeedback(float dtSeconds);
    WaterHudTelemetry waterHudTelemetry(const SceneConfig& sceneConfig) const;
    WaterMaintenanceResult maintainWater(WaterState& water,
                                         const WaterMaintenanceConfig& config = {});
    PrimaryCreatureSyncResult synchronizePrimaryCreature(
        GameState& gameState, std::span<const LiveCreatureSlot> liveCreatures);
    PrimaryCreatureSyncResult synchronizeCreatureRoster(
        GameState& gameState, std::span<const LiveCreatureSlot> liveCreatures);
    PrimaryCreatureCareTelemetry primaryCreatureCareTelemetry(
        const GameState& gameState,
        const PrimaryCreatureFeedConfig& config = {}) const;
    PrimaryCreatureFeedResult feedPrimaryCreature(
        GameState& gameState, const PrimaryCreatureFeedConfig& config = {});
    CreatureVitalityPresentation primaryCreaturePresentation(
        const GameState& gameState) const;
    uint64_t primaryCreatureRuntimeId() const;
    const std::string& primaryCreatureUuid() const;
    std::span<const CreatureRuntimeBinding> creatureRuntimeBindings() const;
    uint64_t creatureRuntimeId(std::string_view creatureUuid) const;
    std::string_view creatureUuidForRuntimeId(uint64_t runtimeId) const;

    PendingPlaceableEdit beginCommitPlaceablePreview(
        const PlaceablePreview& preview);
    PendingPlaceableEdit beginRemovePlaceableAtPreview(
        const SceneConfig& sceneConfig, const PlaceablePreview& preview);
    PendingPlaceableEdit beginUndoLastPlaceableEdit();
    PendingPlaceableEdit beginRedoLastPlaceableEdit();
    PlaceableApplyResult applyPlaceableEditCommand(
        SceneConfig& sceneConfig, const PlaceableEditCommand& command, bool undo,
        bool redo = false);
    bool completePlaceableEdit(const PendingPlaceableEdit& edit,
                               SceneConfig& sceneConfig);
    bool completeExternalPlaceableEdit(const PlaceableEditCommand& command,
                                       std::string successStatus,
                                       SceneConfig& sceneConfig);
    void failPlaceableEditRebuild();

    PlaceableInstance createPlaceableInstance(std::string prototypeSlug,
                                              uint32_t prototypeVersion,
                                              const glm::vec3& position);
    bool findPlaceableAtPreview(const SceneConfig& sceneConfig,
                                const PlaceablePreview& preview,
                                PlaceableInstance& outInstance) const;

    void resetPlaceableEdits();
    void markPlaceableSceneSaved();
    void setPlaceableEditStatus(std::string status);
    bool beginIsolatedPlaceableEditSession();
    bool endIsolatedPlaceableEditSession();

    bool hasPlaceableUndo() const;
    bool hasPlaceableRedo() const;
    bool placeableSceneDirty() const;
    const std::string& placeableEditStatus() const;
    float waterMaintenanceFeedbackSeconds() const;
    FishCelebration& fishCelebration() { return fishCelebration_; }
    const FishCelebration& fishCelebration() const { return fishCelebration_; }
    AxolotlBellyFloat& axolotlBellyFloat() { return axolotlBellyFloat_; }
    const AxolotlBellyFloat& axolotlBellyFloat() const { return axolotlBellyFloat_; }

    // the two focus celebrations are mutually exclusive: enabling one disables
    // the other. always toggle through these, not the members' setEnabled.
    void setFishCelebrationEnabled(bool enabled);
    void setAxolotlBellyFloatEnabled(bool enabled);

private:
    CareRuntime careRuntime_;
    PlaceableRuntime placeableRuntime_{};
    FishCelebration fishCelebration_{};
    AxolotlBellyFloat axolotlBellyFloat_{};
};

} // namespace engine::game
