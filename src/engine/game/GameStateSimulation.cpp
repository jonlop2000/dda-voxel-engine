#include "engine/game/GameStateSimulation.h"

#include <algorithm>
#include <cmath>

#include "engine/game/CreatureCareState.h"
#include "engine/game/CreatureVitality.h"

namespace
{

bool assignIfChanged(float& target, float value)
{
    if (std::fabs(target - value) <= 1e-6f)
    {
        return false;
    }
    target = value;
    return true;
}

float finiteOr(float value, float fallback)
{
    return std::isfinite(value) ? value : fallback;
}

float clamp01(float value, float fallback)
{
    return std::clamp(finiteOr(value, fallback), 0.0f, 1.0f);
}

float responseAlpha(float responsePerSecond, float dtSeconds)
{
    const float response = std::max(0.0f, finiteOr(responsePerSecond, 0.0f));
    return 1.0f - std::exp(-response * dtSeconds);
}

float approachTarget(float current, float target, float responsePerSecond,
                     float dtSeconds)
{
    return current + (target - current) *
                         responseAlpha(responsePerSecond, dtSeconds);
}

} // namespace

bool updateWaterHeartbeat(WaterState& water, float dtSeconds,
                          const WaterHeartbeatConfig& config)
{
    if (dtSeconds <= 0.0f)
    {
        bool changed = false;
        changed |= assignIfChanged(water.temperatureC,
                                   std::clamp(water.temperatureC, 0.0f, 40.0f));
        changed |= assignIfChanged(water.oxygen, std::clamp(water.oxygen, 0.0f, 1.0f));
        changed |= assignIfChanged(water.flow, std::clamp(water.flow, 0.0f, 1.0f));
        changed |= assignIfChanged(water.cleanliness,
                                   std::clamp(water.cleanliness, 0.0f, 1.0f));
        return changed;
    }

    const float maxStepSeconds = std::max(0.0f, config.maxStepSeconds);
    const float dt = maxStepSeconds > 0.0f ? std::min(dtSeconds, maxStepSeconds) : dtSeconds;

    const float currentTemperature = std::clamp(water.temperatureC, 0.0f, 40.0f);
    const float currentOxygen = std::clamp(water.oxygen, 0.0f, 1.0f);
    const float currentFlow = std::clamp(water.flow, 0.0f, 1.0f);
    const float currentCleanliness = std::clamp(water.cleanliness, 0.0f, 1.0f);

    const float cleanlinessDecay = std::max(0.0f, config.cleanlinessDecayPerSecond);
    const float cleanlinessDecayMultiplier =
        std::clamp(finiteOr(config.cleanlinessDecayMultiplier, 1.0f), 0.0f,
                   1.0f);
    const float oxygenDecay = std::max(0.0f, config.oxygenDecayPerSecond);
    const float oxygenFloor = std::clamp(config.oxygenFloor, 0.0f, 1.0f);

    bool changed = false;
    changed |= assignIfChanged(water.temperatureC, currentTemperature);
    changed |= assignIfChanged(water.flow, currentFlow);
    changed |= assignIfChanged(
        water.cleanliness,
        std::clamp(currentCleanliness -
                       cleanlinessDecay * cleanlinessDecayMultiplier * dt,
                   0.0f, 1.0f));
    changed |= assignIfChanged(
        water.oxygen,
        std::clamp(std::max(oxygenFloor, currentOxygen - oxygenDecay * dt), 0.0f, 1.0f));
    return changed;
}

namespace engine::game
{

bool updateCreatureNeeds(CreatureInstance& creature, const WaterState& water,
                         float dtSeconds,
                         const CreatureNeedsHeartbeatConfig& config)
{
    const float maxStepSeconds =
        std::max(0.0f, finiteOr(config.maxStepSeconds, 0.0f));
    const float requestedDt = std::max(0.0f, finiteOr(dtSeconds, 0.0f));
    const float dt = maxStepSeconds > 0.0f
                         ? std::min(requestedDt, maxStepSeconds)
                         : requestedDt;

    const float cleanlinessFloor = clamp01(config.cleanlinessFloor, 0.15f);
    const float happinessFloor = clamp01(config.happinessFloor, 0.20f);
    const float healthFloor = clamp01(config.healthFloor, 0.30f);
    const float hungerCeiling = clamp01(config.hungerCeiling, 1.0f);

    const float hunger = clamp01(creature.needs.hunger, 0.0f);
    const float cleanliness =
        std::clamp(finiteOr(creature.needs.cleanliness, 1.0f), cleanlinessFloor,
                   1.0f);
    const float happiness =
        std::clamp(finiteOr(creature.needs.happiness, 1.0f), happinessFloor,
                   1.0f);
    const float health =
        std::clamp(finiteOr(creature.needs.health, 1.0f), healthFloor, 1.0f);
    const float waterCleanliness = clamp01(water.cleanliness, 1.0f);
    const float waterOxygen = clamp01(water.oxygen, 0.82f);

    const float hungerIncrease =
        std::max(0.0f, finiteOr(config.hungerIncreasePerSecond, 0.0f));
    const float nextHunger = std::clamp(hunger + hungerIncrease * dt, 0.0f,
                                       std::max(hunger, hungerCeiling));

    // clean habitat water can restore personal condition, while dirty water pulls
    // it down only as far as the configured cozy floor.
    const float cleanlinessTarget =
        cleanlinessFloor + (1.0f - cleanlinessFloor) * waterCleanliness;
    const float nextCleanliness = std::clamp(
        approachTarget(cleanliness, cleanlinessTarget,
                       config.cleanlinessResponsePerSecond, dt),
        cleanlinessFloor, 1.0f);

    const float satiety = 1.0f - nextHunger;
    const float happinessQuality =
        0.45f * satiety + 0.20f * nextCleanliness +
        0.15f * waterCleanliness + 0.20f * waterOxygen;
    const float happinessTarget =
        happinessFloor + (1.0f - happinessFloor) * happinessQuality;
    const float nextHappiness = std::clamp(
        approachTarget(happiness, happinessTarget,
                       config.happinessResponsePerSecond, dt),
        happinessFloor, 1.0f);

    const float healthQuality =
        0.35f * satiety + 0.15f * nextCleanliness +
        0.20f * waterCleanliness + 0.30f * waterOxygen;
    const float healthTarget =
        healthFloor + (1.0f - healthFloor) * healthQuality;
    const float nextHealth = std::clamp(
        approachTarget(health, healthTarget, config.healthResponsePerSecond, dt),
        healthFloor, 1.0f);

    bool changed = false;
    changed |= assignIfChanged(creature.needs.hunger, nextHunger);
    changed |= assignIfChanged(creature.needs.cleanliness, nextCleanliness);
    changed |= assignIfChanged(creature.needs.happiness, nextHappiness);
    changed |= assignIfChanged(creature.needs.health, nextHealth);
    changed |= refreshCreatureVitality(creature);
    return changed;
}

PrimaryCreatureNeedsHeartbeatResult updatePrimaryCreatureNeeds(
    GameState& gameState, float dtSeconds,
    const CreatureNeedsHeartbeatConfig& config)
{
    PrimaryCreatureNeedsHeartbeatResult result{};
    CreatureInstance* primary = findPrimaryCreature(gameState);
    if (primary == nullptr)
    {
        return result;
    }

    result.primaryFound = true;
    const float maxStepSeconds =
        std::max(0.0f, finiteOr(config.maxStepSeconds, 0.0f));
    const float requestedDt = std::max(0.0f, finiteOr(dtSeconds, 0.0f));
    result.simulatedSeconds = maxStepSeconds > 0.0f
                                  ? std::min(requestedDt, maxStepSeconds)
                                  : requestedDt;
    result.changed =
        updateCreatureNeeds(*primary, gameState.water, dtSeconds, config);
    return result;
}

} // namespace engine::game
