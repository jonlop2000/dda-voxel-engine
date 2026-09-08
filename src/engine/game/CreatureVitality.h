#pragma once

#include "engine/game/GameState.h"

namespace engine::game
{

struct CreatureVitalityConfig
{
    float healthWeight = 0.30f;
    float happinessWeight = 0.22f;
    float cleanlinessWeight = 0.18f;
    float satietyWeight = 0.20f;
    float bondWeight = 0.10f;
};

struct CreatureVitalityPresentation
{
    float pathWobbleScale = 1.0f;
    float bodyMotionScale = 1.0f;
    float tailMotionScale = 1.0f;
};

float normalizedCreatureBond(float bond);
float deriveCreatureVitality(
    const CreatureInstance& creature,
    const CreatureVitalityConfig& config = {});
bool refreshCreatureVitality(
    CreatureInstance& creature,
    const CreatureVitalityConfig& config = {});
CreatureVitalityPresentation creatureVitalityPresentation(float vitality);

} // namespace engine::game
