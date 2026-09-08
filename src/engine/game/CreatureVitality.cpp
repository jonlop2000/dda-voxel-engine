#include "engine/game/CreatureVitality.h"

#include <algorithm>
#include <cmath>

namespace
{

float normalized(float value, float fallback)
{
    return std::clamp(std::isfinite(value) ? value : fallback, 0.0f, 1.0f);
}

float nonNegativeWeight(float value)
{
    return std::max(0.0f, std::isfinite(value) ? value : 0.0f);
}

} // namespace

namespace engine::game
{

float normalizedCreatureBond(float bond)
{
    return normalized(bond, 0.0f);
}

float deriveCreatureVitality(const CreatureInstance& creature,
                             const CreatureVitalityConfig& config)
{
    const float healthWeight = nonNegativeWeight(config.healthWeight);
    const float happinessWeight = nonNegativeWeight(config.happinessWeight);
    const float cleanlinessWeight = nonNegativeWeight(config.cleanlinessWeight);
    const float satietyWeight = nonNegativeWeight(config.satietyWeight);
    const float bondWeight = nonNegativeWeight(config.bondWeight);
    const float totalWeight = healthWeight + happinessWeight +
                              cleanlinessWeight + satietyWeight + bondWeight;
    if (totalWeight <= 1e-6f)
    {
        return 1.0f;
    }

    const float health = normalized(creature.needs.health, 1.0f);
    const float happiness = normalized(creature.needs.happiness, 1.0f);
    const float cleanliness = normalized(creature.needs.cleanliness, 1.0f);
    const float satiety = 1.0f - normalized(creature.needs.hunger, 0.0f);
    const float bond = normalizedCreatureBond(creature.bond);
    return std::clamp((health * healthWeight + happiness * happinessWeight +
                       cleanliness * cleanlinessWeight + satiety * satietyWeight +
                       bond * bondWeight) /
                          totalWeight,
                      0.0f, 1.0f);
}

bool refreshCreatureVitality(CreatureInstance& creature,
                             const CreatureVitalityConfig& config)
{
    const float vitality = deriveCreatureVitality(creature, config);
    if (std::isfinite(creature.vitality) &&
        std::fabs(creature.vitality - vitality) <= 1e-6f)
    {
        return false;
    }
    creature.vitality = vitality;
    return true;
}

CreatureVitalityPresentation creatureVitalityPresentation(float vitality)
{
    const float value = normalized(vitality, 1.0f);
    CreatureVitalityPresentation response{};
    response.pathWobbleScale = std::lerp(0.70f, 1.08f, value);
    response.bodyMotionScale = std::lerp(0.72f, 1.10f, value);
    response.tailMotionScale = std::lerp(0.68f, 1.14f, value);
    return response;
}

} // namespace engine::game
