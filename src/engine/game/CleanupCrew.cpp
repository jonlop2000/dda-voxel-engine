#include "engine/game/CleanupCrew.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include "engine/game/SpeciesRegistry.h"

namespace engine::game
{
namespace
{

float normalizedReduction(float value, float fallback)
{
    return std::clamp(std::isfinite(value) ? value : fallback, 0.0f, 1.0f);
}

} // namespace

CleanupCrewEffect evaluateCleanupCrewEffect(const GameState& gameState,
                                             const CleanupCrewConfig& config)
{
    CleanupCrewEffect effect{};
    for (const CreatureInstance& creature : gameState.creatures)
    {
        if (creature.uuid.empty() || creature.speciesId.empty())
        {
            continue;
        }

        const SpeciesDefinition* species = findSpecies(creature.speciesId);
        if (species == nullptr ||
            !hasSpeciesRole(species->roles, SpeciesRole::CleanupCrew))
        {
            continue;
        }

        if (effect.creatureCount < std::numeric_limits<uint32_t>::max())
        {
            ++effect.creatureCount;
        }
    }

    const float perCreature = normalizedReduction(
        config.cleanlinessDecayReductionPerCreature, 0.20f);
    const float maximum = normalizedReduction(
        config.maximumCleanlinessDecayReduction, 0.60f);
    const double total = static_cast<double>(perCreature) *
                         static_cast<double>(effect.creatureCount);
    effect.cleanlinessDecayReduction =
        std::min(maximum, static_cast<float>(std::min(total, 1.0)));
    effect.cleanlinessDecayMultiplier =
        1.0f - effect.cleanlinessDecayReduction;
    return effect;
}

} // namespace engine::game
