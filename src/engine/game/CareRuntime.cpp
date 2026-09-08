#include "engine/game/CareRuntime.h"

#include <algorithm>
#include <cmath>

#include "engine/game/CleanupCrew.h"
#include "engine/game/GameStateSimulation.h"

namespace engine::game
{

CareRuntime::CareRuntime(const CareUtcClock& careUtcClock)
    : careUtcClock_(careUtcClock)
{
}

OfflineCareResult CareRuntime::reconcileOfflineCare(
    GameState& gameState, const OfflineCareConfig& config)
{
    return engine::game::reconcileOfflineCare(gameState, careUtcClock_, config);
}

void CareRuntime::stampLastPlayedUtc(GameState& gameState) const
{
    engine::game::stampLastPlayedUtc(gameState, careUtcClock_);
}

CareRuntime::HeartbeatResult CareRuntime::updateGameStateHeartbeat(
    GameState& gameState, float dtSeconds, bool gameplayActive)
{
    if (!gameplayActive)
    {
        gameStateHeartbeatLogAccumulator_ = 0.0;
        return {};
    }

    HeartbeatResult result{};
    const CleanupCrewEffect cleanupCrew = evaluateCleanupCrewEffect(gameState);
    result.cleanupCrewCount = cleanupCrew.creatureCount;
    result.cleanlinessDecayReduction =
        cleanupCrew.cleanlinessDecayReduction;
    WaterHeartbeatConfig waterConfig{};
    waterConfig.cleanlinessDecayMultiplier =
        cleanupCrew.cleanlinessDecayMultiplier;
    result.waterChanged =
        updateWaterHeartbeat(gameState.water, dtSeconds, waterConfig);
    const PrimaryCreatureNeedsHeartbeatResult creatureResult =
        hasCurrentPrimaryBinding(gameState)
            ? updatePrimaryCreatureNeeds(gameState, dtSeconds)
            : PrimaryCreatureNeedsHeartbeatResult{};
    result.creatureChanged = creatureResult.changed;
    result.primaryCreatureFound = creatureResult.primaryFound;
    result.changed = result.waterChanged || result.creatureChanged;
    if (!result.changed)
    {
        return result;
    }

    gameStateHeartbeatLogAccumulator_ +=
        static_cast<double>(std::max(0.0f, dtSeconds));
    if (gameStateHeartbeatLogAccumulator_ >= 15.0)
    {
        gameStateHeartbeatLogAccumulator_ = 0.0;
        result.shouldLog = true;
    }
    return result;
}

void CareRuntime::tickGameplayActionFeedback(float dtSeconds)
{
    const float elapsed = std::max(0.0f, dtSeconds);
    waterMaintenanceFeedbackSeconds_ =
        std::max(0.0f, waterMaintenanceFeedbackSeconds_ - elapsed);
    primaryCreatureFeedCooldownSeconds_ =
        std::max(0.0f, primaryCreatureFeedCooldownSeconds_ - elapsed);
    primaryCreatureFeedFeedbackSeconds_ =
        std::max(0.0f, primaryCreatureFeedFeedbackSeconds_ - elapsed);
}

WaterHudTelemetry CareRuntime::waterHudTelemetry(const GameState& gameState) const
{
    WaterHudTelemetry telemetry{};
    telemetry.temperatureC = gameState.water.temperatureC;
    telemetry.oxygen = gameState.water.oxygen;
    telemetry.flow = gameState.water.flow;
    telemetry.cleanliness = gameState.water.cleanliness;
    const CleanupCrewEffect cleanupCrew = evaluateCleanupCrewEffect(gameState);
    telemetry.cleanupCrewCount = cleanupCrew.creatureCount;
    telemetry.cleanlinessDecayReduction =
        cleanupCrew.cleanlinessDecayReduction;
    telemetry.maintenanceFeedbackActive = waterMaintenanceFeedbackSeconds_ > 0.0f;
    return telemetry;
}

WaterMaintenanceResult CareRuntime::maintainWater(
    WaterState& water, const WaterMaintenanceConfig& config)
{
    WaterMaintenanceResult result{};
    const float currentCleanliness = std::clamp(water.cleanliness, 0.0f, 1.0f);
    const float currentOxygen = std::clamp(water.oxygen, 0.0f, 1.0f);
    result.cleanliness = currentCleanliness;
    result.oxygen = currentOxygen;

    const bool maintenanceNeeded =
        currentCleanliness < std::clamp(config.cleanlinessThreshold, 0.0f, 1.0f) ||
        currentOxygen < std::clamp(config.oxygenBaseline, 0.0f, 1.0f);
    if (!maintenanceNeeded)
    {
        return result;
    }

    water.cleanliness = 1.0f;
    water.oxygen =
        std::clamp(std::max(currentOxygen, config.oxygenBaseline), 0.0f, 1.0f);
    waterMaintenanceFeedbackSeconds_ = std::max(0.0f, config.feedbackSeconds);

    result.maintained = true;
    result.cleanliness = water.cleanliness;
    result.oxygen = water.oxygen;
    result.feedbackSeconds = waterMaintenanceFeedbackSeconds_;
    return result;
}

PrimaryCreatureSyncResult CareRuntime::synchronizePrimaryCreature(
    GameState& gameState, std::span<const LiveCreatureSlot> liveCreatures)
{
    return synchronizeCreatureRoster(gameState, liveCreatures);
}

PrimaryCreatureSyncResult CareRuntime::synchronizeCreatureRoster(
    GameState& gameState, std::span<const LiveCreatureSlot> liveCreatures)
{
    PrimaryCreatureSyncResult result =
        engine::game::synchronizeCreatureRoster(gameState, liveCreatures);
    if (CreatureInstance* primary = findPrimaryCreature(gameState))
    {
        primary->bond = normalizedCreatureBond(primary->bond);
        refreshCreatureVitality(*primary);
    }
    primaryCreatureRuntimeId_ = result.runtimeId;
    primaryCreatureUuid_ = result.creatureUuid;
    creatureRuntimeBindings_ = result.rosterBindings;
    return result;
}

uint64_t CareRuntime::creatureRuntimeId(std::string_view creatureUuid) const
{
    const CreatureRuntimeBinding* binding =
        creatureRuntimeBindings_.findByCreatureUuid(creatureUuid);
    return binding != nullptr ? binding->runtimeId : 0;
}

std::string_view CareRuntime::creatureUuidForRuntimeId(uint64_t runtimeId) const
{
    const CreatureRuntimeBinding* binding =
        creatureRuntimeBindings_.findByRuntimeId(runtimeId);
    return binding != nullptr ? std::string_view(binding->creatureUuid)
                              : std::string_view{};
}

bool CareRuntime::hasCurrentPrimaryBinding(const GameState& gameState) const
{
    const CreatureInstance* primary = findPrimaryCreature(gameState);
    if (primary == nullptr || primaryCreatureRuntimeId_ == 0 ||
        primaryCreatureUuid_ != primary->uuid)
    {
        return false;
    }
    const CreatureRuntimeBinding* binding =
        creatureRuntimeBindings_.findByCreatureUuid(primary->uuid);
    return binding != nullptr && binding->runtimeId == primaryCreatureRuntimeId_ &&
           binding->speciesId == primary->speciesId;
}

PrimaryCreatureCareTelemetry CareRuntime::primaryCreatureCareTelemetry(
    const GameState& gameState, const PrimaryCreatureFeedConfig& config) const
{
    PrimaryCreatureCareTelemetry telemetry{};
    const CreatureInstance* primary = findPrimaryCreature(gameState);
    if (primary == nullptr)
    {
        return telemetry;
    }

    telemetry.hasPrimary = true;
    telemetry.liveBound = hasCurrentPrimaryBinding(gameState);
    telemetry.displayName =
        primary->displayName.empty() ? std::string("Creature") : primary->displayName;
    telemetry.hunger = std::isfinite(primary->needs.hunger)
                           ? std::clamp(primary->needs.hunger, 0.0f, 1.0f)
                           : 0.0f;
    telemetry.cleanliness = std::isfinite(primary->needs.cleanliness)
                                ? std::clamp(primary->needs.cleanliness, 0.0f, 1.0f)
                                : 0.0f;
    telemetry.happiness = std::isfinite(primary->needs.happiness)
                              ? std::clamp(primary->needs.happiness, 0.0f, 1.0f)
                              : 0.0f;
    telemetry.health = std::isfinite(primary->needs.health)
                           ? std::clamp(primary->needs.health, 0.0f, 1.0f)
                           : 0.0f;
    telemetry.bond = normalizedCreatureBond(primary->bond);
    telemetry.vitality = deriveCreatureVitality(*primary);
    telemetry.feedFeedbackActive = primaryCreatureFeedFeedbackSeconds_ > 0.0f;
    telemetry.feedCooldownRemaining = primaryCreatureFeedCooldownSeconds_;

    if (!telemetry.liveBound)
    {
        telemetry.feedAvailability =
            PrimaryCreatureFeedAvailability::NoLivePrimaryCreature;
    }
    else if (primaryCreatureFeedCooldownSeconds_ > 0.0f)
    {
        telemetry.feedAvailability =
            PrimaryCreatureFeedAvailability::CooldownActive;
    }
    else if (telemetry.hunger <= std::clamp(config.minimumHunger, 0.0f, 1.0f))
    {
        telemetry.feedAvailability = PrimaryCreatureFeedAvailability::NotHungry;
    }
    else
    {
        telemetry.feedAvailability = PrimaryCreatureFeedAvailability::Available;
    }
    return telemetry;
}

PrimaryCreatureFeedResult CareRuntime::feedPrimaryCreature(
    GameState& gameState, const PrimaryCreatureFeedConfig& config)
{
    PrimaryCreatureFeedResult result{};
    CreatureInstance* primary = findPrimaryCreature(gameState);
    if (primary == nullptr)
    {
        return result;
    }
    if (!hasCurrentPrimaryBinding(gameState))
    {
        result.status = PrimaryCreatureFeedStatus::NoLivePrimaryCreature;
        return result;
    }
    if (primaryCreatureFeedCooldownSeconds_ > 0.0f)
    {
        result.status = PrimaryCreatureFeedStatus::CooldownActive;
        result.hungerBefore = std::clamp(primary->needs.hunger, 0.0f, 1.0f);
        result.hungerAfter = result.hungerBefore;
        result.cooldownRemaining = primaryCreatureFeedCooldownSeconds_;
        return result;
    }

    CreatureFeedConfig feedConfig{};
    feedConfig.hungerReduction = config.hungerReduction;
    feedConfig.minimumHunger = config.minimumHunger;
    feedConfig.bondGain = config.bondGain;
    const CreatureFeedResult feedResult = feedCreature(*primary, feedConfig);
    result.hungerBefore = feedResult.hungerBefore;
    result.hungerAfter = feedResult.hungerAfter;
    result.bondBefore = feedResult.bondBefore;
    result.bondAfter = feedResult.bondAfter;
    if (!feedResult.fed)
    {
        result.status = PrimaryCreatureFeedStatus::NotHungry;
        return result;
    }

    primaryCreatureFeedCooldownSeconds_ = std::max(0.0f, config.cooldownSeconds);
    primaryCreatureFeedFeedbackSeconds_ = std::max(0.0f, config.feedbackSeconds);
    result.status = PrimaryCreatureFeedStatus::Fed;
    result.cooldownRemaining = primaryCreatureFeedCooldownSeconds_;
    return result;
}

CreatureVitalityPresentation CareRuntime::primaryCreaturePresentation(
    const GameState& gameState) const
{
    const CreatureInstance* primary = findPrimaryCreature(gameState);
    if (primary == nullptr || !hasCurrentPrimaryBinding(gameState))
    {
        return {};
    }
    return creatureVitalityPresentation(deriveCreatureVitality(*primary));
}

} // namespace engine::game
