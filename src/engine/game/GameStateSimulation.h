#pragma once

#include "engine/game/GameState.h"

struct WaterHeartbeatConfig
{
    float maxStepSeconds = 5.0f;
    float cleanlinessDecayPerSecond = 0.002f;
    float cleanlinessDecayMultiplier = 1.0f;
    float oxygenDecayPerSecond = 0.00035f;
    float oxygenFloor = 0.35f;
};

bool updateWaterHeartbeat(WaterState& water, float dtSeconds,
                          const WaterHeartbeatConfig& config = {});

namespace engine::game
{

struct CreatureNeedsHeartbeatConfig
{
    float maxStepSeconds = 5.0f;
    float hungerIncreasePerSecond = 0.00035f;
    float hungerCeiling = 1.0f;
    float cleanlinessResponsePerSecond = 0.0012f;
    float happinessResponsePerSecond = 0.0008f;
    float healthResponsePerSecond = 0.00035f;
    float cleanlinessFloor = 0.15f;
    float happinessFloor = 0.20f;
    float healthFloor = 0.30f;
};

struct PrimaryCreatureNeedsHeartbeatResult
{
    bool primaryFound = false;
    bool changed = false;
    float simulatedSeconds = 0.0f;
};

// hunger is pressure (higher is worse). cleanliness, happiness, and health are
// conditions (higher is better). this function is pure simulation over explicit
// state and does not read clocks, runtime ids, ui state, or presentation objects.
bool updateCreatureNeeds(
    CreatureInstance& creature, const WaterState& water, float dtSeconds,
    const CreatureNeedsHeartbeatConfig& config = {});

PrimaryCreatureNeedsHeartbeatResult updatePrimaryCreatureNeeds(
    GameState& gameState, float dtSeconds,
    const CreatureNeedsHeartbeatConfig& config = {});

} // namespace engine::game
