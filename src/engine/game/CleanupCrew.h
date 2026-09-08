#pragma once

#include <cstdint>

#include "engine/game/GameState.h"

namespace engine::game
{

struct CleanupCrewConfig
{
    float cleanlinessDecayReductionPerCreature = 0.20f;
    float maximumCleanlinessDecayReduction = 0.60f;
};

struct CleanupCrewEffect
{
    uint32_t creatureCount = 0;
    float cleanlinessDecayReduction = 0.0f;
    float cleanlinessDecayMultiplier = 1.0f;
};

// cleanup effects are derived from owned persistent creatures, not disposable
// live/render bindings. unknown species and incomplete saved entries are ignored.
CleanupCrewEffect evaluateCleanupCrewEffect(
    const GameState& gameState, const CleanupCrewConfig& config = {});

} // namespace engine::game
