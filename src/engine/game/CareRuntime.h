#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>

#include "engine/game/CreatureCareState.h"
#include "engine/game/CreatureVitality.h"
#include "engine/game/GameState.h"
#include "engine/game/OfflineCare.h"

namespace engine::game
{

struct WaterHudTelemetry
{
    float temperatureC = 24.0f;
    float oxygen = 0.82f;
    float flow = 0.45f;
    float cleanliness = 1.0f;
    uint32_t cleanupCrewCount = 0;
    float cleanlinessDecayReduction = 0.0f;
    bool maintenanceFeedbackActive = false;
};

struct WaterMaintenanceConfig
{
    float cleanlinessThreshold = 0.95f;
    float oxygenBaseline = 0.82f;
    float feedbackSeconds = 1.0f;
};

struct WaterMaintenanceResult
{
    bool maintained = false;
    float cleanliness = 1.0f;
    float oxygen = 0.82f;
    float feedbackSeconds = 0.0f;
};

enum class PrimaryCreatureFeedStatus
{
    Fed,
    NoPrimaryCreature,
    NoLivePrimaryCreature,
    NotHungry,
    CooldownActive,
};

enum class PrimaryCreatureFeedAvailability
{
    Available,
    NoPrimaryCreature,
    NoLivePrimaryCreature,
    NotHungry,
    CooldownActive,
};

struct PrimaryCreatureFeedConfig
{
    float hungerReduction = 0.30f;
    float minimumHunger = 0.01f;
    float bondGain = 0.02f;
    float cooldownSeconds = 1.25f;
    float feedbackSeconds = 0.80f;
};

struct PrimaryCreatureFeedResult
{
    PrimaryCreatureFeedStatus status =
        PrimaryCreatureFeedStatus::NoPrimaryCreature;
    float hungerBefore = 0.0f;
    float hungerAfter = 0.0f;
    float bondBefore = 0.0f;
    float bondAfter = 0.0f;
    float cooldownRemaining = 0.0f;

    bool fed() const { return status == PrimaryCreatureFeedStatus::Fed; }
};

struct PrimaryCreatureCareTelemetry
{
    bool hasPrimary = false;
    bool liveBound = false;
    std::string displayName{};
    float hunger = 0.0f;
    float cleanliness = 1.0f;
    float happiness = 1.0f;
    float health = 1.0f;
    float bond = 0.0f;
    float vitality = 1.0f;
    PrimaryCreatureFeedAvailability feedAvailability =
        PrimaryCreatureFeedAvailability::NoPrimaryCreature;
    bool feedFeedbackActive = false;
    float feedCooldownRemaining = 0.0f;
};

// owns care-loop state and rules independently of scene/rendering ownership.
// the injected clock is non-owning and must outlive this runtime.
class CareRuntime
{
public:
    explicit CareRuntime(
        const CareUtcClock& careUtcClock = systemCareUtcClock());
    CareRuntime(CareUtcClock&&) = delete;
    CareRuntime(const CareUtcClock&&) = delete;

    struct HeartbeatResult
    {
        bool changed = false;
        bool waterChanged = false;
        bool creatureChanged = false;
        bool primaryCreatureFound = false;
        uint32_t cleanupCrewCount = 0;
        float cleanlinessDecayReduction = 0.0f;
        bool shouldLog = false;
    };

    HeartbeatResult updateGameStateHeartbeat(GameState& gameState,
                                             float dtSeconds,
                                             bool gameplayActive);
    OfflineCareResult reconcileOfflineCare(
        GameState& gameState, const OfflineCareConfig& config = {});
    void stampLastPlayedUtc(GameState& gameState) const;
    void tickGameplayActionFeedback(float dtSeconds);
    WaterHudTelemetry waterHudTelemetry(const GameState& gameState) const;
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

    uint64_t primaryCreatureRuntimeId() const { return primaryCreatureRuntimeId_; }
    const std::string& primaryCreatureUuid() const { return primaryCreatureUuid_; }
    std::span<const CreatureRuntimeBinding> creatureRuntimeBindings() const
    {
        return creatureRuntimeBindings_.bindings;
    }
    uint64_t creatureRuntimeId(std::string_view creatureUuid) const;
    std::string_view creatureUuidForRuntimeId(uint64_t runtimeId) const;
    float waterMaintenanceFeedbackSeconds() const
    {
        return waterMaintenanceFeedbackSeconds_;
    }

private:
    bool hasCurrentPrimaryBinding(const GameState& gameState) const;

    const CareUtcClock& careUtcClock_;
    double gameStateHeartbeatLogAccumulator_ = 0.0;
    float waterMaintenanceFeedbackSeconds_ = 0.0f;
    float primaryCreatureFeedCooldownSeconds_ = 0.0f;
    float primaryCreatureFeedFeedbackSeconds_ = 0.0f;
    uint64_t primaryCreatureRuntimeId_ = 0;
    std::string primaryCreatureUuid_{};
    CreatureRuntimeBindingResult creatureRuntimeBindings_{};
};

} // namespace engine::game
